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

static const struct vkd3d_instance_create_info instance_default_create_info =
{
    .wchar_size = sizeof(WCHAR),
    .signal_event_pfn = signal_event,
};

static const struct vkd3d_device_create_info device_default_create_info =
{
    .minimum_feature_level = D3D_FEATURE_LEVEL_11_0,
    .instance_create_info = &instance_default_create_info,
};

static ID3D12Device *create_device(void)
{
    ID3D12Device *device;
    HRESULT hr;

    hr = vkd3d_create_device(&device_default_create_info,
            &IID_ID3D12Device, (void **)&device);
    return SUCCEEDED(hr) ? device : NULL;
}

static ID3D12CommandQueue *create_command_queue(ID3D12Device *device,
        D3D12_COMMAND_LIST_TYPE type)
{
    D3D12_COMMAND_QUEUE_DESC desc;
    ID3D12CommandQueue *queue;
    HRESULT hr;

    desc.Type = type;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;
    hr = ID3D12Device_CreateCommandQueue(device, &desc,
            &IID_ID3D12CommandQueue, (void **)&queue);
    ok(hr == S_OK, "Failed to create command queue, hr %#x.\n", hr);
    return queue;
}

static void test_create_instance(void)
{
    struct vkd3d_instance_create_info create_info;
    struct vkd3d_instance *instance;
    ULONG refcount;
    HRESULT hr;

    create_info = instance_default_create_info;
    hr = vkd3d_create_instance(&create_info, &instance);
    ok(hr == S_OK, "Failed to create instance, hr %#x.\n", hr);
    refcount = vkd3d_instance_incref(instance);
    ok(refcount == 2, "Got unexpected refcount %u.\n", refcount);
    vkd3d_instance_decref(instance);
    refcount = vkd3d_instance_decref(instance);
    ok(!refcount, "Instance has %u references left.\n", refcount);

    create_info = instance_default_create_info;
    create_info.wchar_size = 1;
    hr = vkd3d_create_instance(&create_info, &instance);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    create_info = instance_default_create_info;
    create_info.signal_event_pfn = NULL;
    hr = vkd3d_create_instance(&create_info, &instance);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    create_info = instance_default_create_info;
    create_info.vkGetInstanceProcAddr_pfn = vkGetInstanceProcAddr;
    hr = vkd3d_create_instance(&create_info, &instance);
    ok(hr == S_OK, "Failed to create instance, hr %#x.\n", hr);
    refcount = vkd3d_instance_decref(instance);
    ok(!refcount, "Instance has %u references left.\n", refcount);
}

static void test_create_device(void)
{
    struct vkd3d_instance *instance, *tmp_instance;
    struct vkd3d_device_create_info create_info;
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;

    hr = vkd3d_create_device(NULL, &IID_ID3D12Device, (void **)&device);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    create_info = device_default_create_info;
    create_info.instance = NULL;
    create_info.instance_create_info = NULL;
    hr = vkd3d_create_device(&create_info, &IID_ID3D12Device, (void **)&device);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    create_info.instance_create_info = &instance_default_create_info;
    hr = vkd3d_create_device(&create_info, &IID_ID3D12Device, (void **)&device);
    ok(hr == S_OK, "Failed to create device, hr %#x.\n", hr);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);

    hr = vkd3d_create_instance(&instance_default_create_info, &instance);
    ok(hr == S_OK, "Failed to create instance, hr %#x.\n", hr);

    create_info.instance = instance;
    create_info.instance_create_info = NULL;
    hr = vkd3d_create_device(&create_info, &IID_ID3D12Device, (void **)&device);
    ok(hr == S_OK, "Failed to create device, hr %#x.\n", hr);
    refcount = vkd3d_instance_incref(instance);
    ok(refcount >= 3, "Got unexpected refcount %u.\n", refcount);
    vkd3d_instance_decref(instance);
    tmp_instance = vkd3d_instance_from_device(device);
    ok(tmp_instance == instance, "Got instance %p, expected %p.\n", tmp_instance, instance);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);

    create_info.instance = instance;
    create_info.instance_create_info = &instance_default_create_info;
    hr = vkd3d_create_device(&create_info, &IID_ID3D12Device, (void **)&device);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    refcount = vkd3d_instance_decref(instance);
    ok(!refcount, "Instance has %u references left.\n", refcount);
}

static void test_physical_device(void)
{
    struct vkd3d_device_create_info create_info;
    VkPhysicalDevice *vk_physical_devices;
    VkPhysicalDevice vk_physical_device;
    struct vkd3d_instance *instance;
    VkInstance vk_instance;
    ID3D12Device *device;
    uint32_t i, count;
    ULONG refcount;
    VkResult vr;
    HRESULT hr;

    hr = vkd3d_create_instance(&instance_default_create_info, &instance);
    ok(hr == S_OK, "Failed to create instance, hr %#x.\n", hr);
    vk_instance = vkd3d_instance_get_vk_instance(instance);
    ok(vk_instance != VK_NULL_HANDLE, "Failed to get Vulkan instance.\n");

    create_info = device_default_create_info;
    create_info.instance = instance;
    create_info.instance_create_info = NULL;
    create_info.vk_physical_device = VK_NULL_HANDLE;
    hr = vkd3d_create_device(&create_info, &IID_ID3D12Device, (void **)&device);
    ok(hr == S_OK, "Failed to create device, hr %#x.\n", hr);
    vk_physical_device = vkd3d_get_vk_physical_device(device);
    trace("Default Vulkan physical device %p.\n", vk_physical_device);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);

    vr = vkEnumeratePhysicalDevices(vk_instance, &count, NULL);
    ok(vr == VK_SUCCESS, "Got unexpected VkResult %d.\n", vr);
    vk_physical_devices = calloc(count, sizeof(*vk_physical_devices));
    ok(vk_physical_devices, "Failed to allocate memory.\n");
    vr = vkEnumeratePhysicalDevices(vk_instance, &count, vk_physical_devices);
    ok(vr == VK_SUCCESS, "Got unexpected VkResult %d.\n", vr);

    for (i = 0; i < count; ++i)
    {
        trace("Creating device for Vulkan physical device %p.\n", vk_physical_devices[i]);

        create_info.vk_physical_device = vk_physical_devices[i];
        hr = vkd3d_create_device(&create_info, &IID_ID3D12Device, (void **)&device);
        ok(hr == S_OK, "Failed to create device, hr %#x.\n", hr);
        vk_physical_device = vkd3d_get_vk_physical_device(device);
        ok(vk_physical_device == vk_physical_devices[i],
                "Got unexpected Vulkan physical device %p.\n", vk_physical_device);
        refcount = ID3D12Device_Release(device);
        ok(!refcount, "Device has %u references left.\n", refcount);
    }

    free(vk_physical_devices);
    refcount = vkd3d_instance_decref(instance);
    ok(!refcount, "Instance has %u references left.\n", refcount);
}

static void test_vkd3d_queue(void)
{
    ID3D12CommandQueue *direct_queue, *compute_queue, *copy_queue;
    uint32_t vk_queue_family;
    ID3D12Device *device;
    VkQueue vk_queue;
    ULONG refcount;

    device = create_device();
    ok(device, "Failed to create device.\n");

    direct_queue = create_command_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    ok(direct_queue, "Failed to create direct command queue.\n");
    compute_queue = create_command_queue(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
    ok(compute_queue, "Failed to create compute command queue.\n");
    copy_queue = create_command_queue(device, D3D12_COMMAND_LIST_TYPE_COPY);
    ok(copy_queue, "Failed to create copy command queue.\n");

    vk_queue_family = vkd3d_get_vk_queue_family_index(direct_queue);
    trace("Direct queue family index %u.\n", vk_queue_family);
    vk_queue_family = vkd3d_get_vk_queue_family_index(compute_queue);
    trace("Compute queue family index %u.\n", vk_queue_family);
    vk_queue_family = vkd3d_get_vk_queue_family_index(copy_queue);
    trace("Copy queue family index %u.\n", vk_queue_family);

    vk_queue = vkd3d_acquire_vk_queue(direct_queue);
    ok(vk_queue != VK_NULL_HANDLE, "Failed to acquire Vulkan queue.\n");
    vkd3d_release_vk_queue(direct_queue);
    vk_queue = vkd3d_acquire_vk_queue(compute_queue);
    ok(vk_queue != VK_NULL_HANDLE, "Failed to acquire Vulkan queue.\n");
    vkd3d_release_vk_queue(compute_queue);
    vk_queue = vkd3d_acquire_vk_queue(copy_queue);
    ok(vk_queue != VK_NULL_HANDLE, "Failed to acquire Vulkan queue.\n");
    vkd3d_release_vk_queue(copy_queue);

    ID3D12CommandQueue_Release(direct_queue);
    ID3D12CommandQueue_Release(compute_queue);
    ID3D12CommandQueue_Release(copy_queue);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static bool have_d3d12_device(void)
{
    ID3D12Device *device;

    if ((device = create_device()))
        ID3D12Device_Release(device);
    return device;
}

START_TEST(vkd3d_api)
{
    if (!have_d3d12_device())
    {
        skip("D3D12 device cannot be created.\n");
        return;
    }

    run_test(test_create_instance);
    run_test(test_create_device);
    run_test(test_physical_device);
    run_test(test_vkd3d_queue);
}
