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
#define VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_XLIB_KHR
#include "vkd3d_test.h"
#include <vkd3d.h>

static ULONG get_refcount(void *iface)
{
    IUnknown *unk = iface;
    IUnknown_AddRef(unk);
    return IUnknown_Release(unk);
}

static ULONG resource_get_internal_refcount(ID3D12Resource *resource)
{
    vkd3d_resource_incref(resource);
    return vkd3d_resource_decref(resource);
}

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

static ID3D12Resource *create_buffer(ID3D12Device *device, D3D12_HEAP_TYPE heap_type,
        size_t size, D3D12_RESOURCE_FLAGS resource_flags, D3D12_RESOURCE_STATES initial_resource_state)
{
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *buffer;
    HRESULT hr;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = heap_type;

    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = 0;
    resource_desc.Width = size;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = resource_flags;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties,
            D3D12_HEAP_FLAG_NONE, &resource_desc, initial_resource_state,
            NULL, &IID_ID3D12Resource, (void **)&buffer);
    ok(hr == S_OK, "Failed to create buffer, hr %#x.\n", hr);
    return buffer;
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

struct vulkan_extension
{
    const char *name;
    bool is_supported;
};

static uint32_t check_extensions(const char **enabled_extensions,
        struct vulkan_extension *extensions, unsigned int extension_count,
        const VkExtensionProperties *properties, unsigned int count)
{
    uint32_t enabled_extension_count = 0;
    unsigned int i, j;

    for (i = 0; i < count; ++i)
    {
        for (j = 0; j < extension_count; ++j)
        {
            if (!strcmp(properties[i].extensionName, extensions[j].name))
            {
                extensions[j].is_supported = true;
                enabled_extensions[enabled_extension_count++] = extensions[j].name;
            }
        }
    }

    return enabled_extension_count;
}

static uint32_t check_instance_extensions(const char **enabled_extensions,
        struct vulkan_extension *extensions, unsigned int extension_count)
{
    VkExtensionProperties *properties;
    uint32_t enabled_extension_count;
    uint32_t count;
    VkResult vr;

    vr = vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    ok(vr == VK_SUCCESS, "Got unexpected VkResult %d.\n", vr);
    if (!count)
        return 0;

    properties = calloc(count, sizeof(*properties));
    ok(properties, "Failed to allocate memory.\n");
    vr = vkEnumerateInstanceExtensionProperties(NULL, &count, properties);
    ok(vr == VK_SUCCESS, "Got unexpected VkResult %d.\n", vr);
    enabled_extension_count = check_extensions(enabled_extensions,
            extensions, extension_count, properties, count);
    free(properties);
    return enabled_extension_count;
}

static uint32_t check_device_extensions(VkPhysicalDevice vk_physical_device,
        const char **enabled_extensions, struct vulkan_extension *extensions,
        unsigned int extension_count)
{
    VkExtensionProperties *properties;
    uint32_t enabled_extension_count;
    uint32_t count;
    VkResult vr;

    vr = vkEnumerateDeviceExtensionProperties(vk_physical_device, NULL, &count, NULL);
    ok(vr == VK_SUCCESS, "Got unexpected VkResult %d.\n", vr);
    if (!count)
        return 0;

    properties = calloc(count, sizeof(*properties));
    ok(properties, "Failed to allocate memory.\n");
    vr = vkEnumerateDeviceExtensionProperties(vk_physical_device, NULL, &count, properties);
    ok(vr == VK_SUCCESS, "Got unexpected VkResult %d.\n", vr);
    enabled_extension_count = check_extensions(enabled_extensions,
            extensions, extension_count, properties, count);
    free(properties);
    return enabled_extension_count;
}

static void test_additional_instance_extensions(void)
{
    struct vulkan_extension extensions[] =
    {
        {VK_KHR_SURFACE_EXTENSION_NAME},
        {VK_KHR_XCB_SURFACE_EXTENSION_NAME},
        {VK_KHR_XLIB_SURFACE_EXTENSION_NAME},
    };

    const char *enabled_extensions[ARRAY_SIZE(extensions)];
    struct vkd3d_instance_create_info create_info;
    struct vkd3d_instance *instance;
    uint32_t extension_count;
    PFN_vkVoidFunction pfn;
    VkInstance vk_instance;
    unsigned int i;
    ULONG refcount;
    HRESULT hr;

    if (!(extension_count = check_instance_extensions(enabled_extensions,
            extensions, ARRAY_SIZE(extensions))))
    {
        skip("Found 0 extensions.\n");
        return;
    }

    create_info = instance_default_create_info;
    create_info.instance_extensions = enabled_extensions;
    create_info.instance_extension_count = extension_count;
    hr = vkd3d_create_instance(&create_info, &instance);
    ok(hr == S_OK, "Failed to create instance, hr %#x.\n", hr);
    vk_instance = vkd3d_instance_get_vk_instance(instance);
    ok(vk_instance != VK_NULL_HANDLE, "Failed to get Vulkan instance.\n");

    for (i = 0; i < ARRAY_SIZE(extensions); ++i)
    {
        if (!extensions[i].is_supported)
            continue;

        if (!strcmp(extensions[i].name, VK_KHR_XCB_SURFACE_EXTENSION_NAME))
        {
            pfn = vkGetInstanceProcAddr(vk_instance, "vkCreateXcbSurfaceKHR");
            ok(pfn, "Failed to get proc addr for vkCreateXcbSurfaceKHR.\n");
        }
        else if (!strcmp(extensions[i].name, VK_KHR_XLIB_SURFACE_EXTENSION_NAME))
        {
            pfn = vkGetInstanceProcAddr(vk_instance, "vkCreateXlibSurfaceKHR");
            ok(pfn, "Failed to get proc addr for vkCreateXlibSurfaceKHR.\n");
        }
    }

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

static VkResult VKAPI_CALL fake_vkEnumerateDeviceExtensionProperties(
        VkPhysicalDevice physical_device, const char *layer_name,
        uint32_t *out_count, VkExtensionProperties *out_properties)
{
    VkExtensionProperties *properties;
    uint32_t count, i, j;
    VkResult vr;

    vr = vkEnumerateDeviceExtensionProperties(physical_device, layer_name, &count, NULL);
    ok(vr == VK_SUCCESS, "Got unexpected VkResult %d.\n", vr);
    if (!count)
    {
        *out_count = count;
        return vr;
    }

    properties = calloc(count, sizeof(*properties));
    ok(properties, "Failed to allocate memory.\n");

    vr = vkEnumerateDeviceExtensionProperties(physical_device, layer_name, &count, properties);
    ok(vr == VK_SUCCESS, "Got unexpected VkResult %d.\n", vr);

    vr = VK_SUCCESS;
    for (i = 0, j = 0; i < count; ++i)
    {
        /* Filter out VK_KHR_maintenance1. */
        if (!strcmp(properties[i].extensionName, VK_KHR_MAINTENANCE1_EXTENSION_NAME))
            continue;

        if (out_properties)
        {
            if (j < *out_count)
                out_properties[j] = properties[i];
            else
                vr = VK_INCOMPLETE;
        }

        ++j;
    }

    free(properties);

    *out_count = j;
    return vr;
}

static VkResult VKAPI_CALL fake_vkCreateDevice(VkPhysicalDevice physical_device,
        const VkDeviceCreateInfo *create_info, const VkAllocationCallbacks *allocator,
        VkDevice *device)
{
    uint32_t i;

    for (i = 0; i < create_info->enabledExtensionCount; ++i)
    {
        if (!strcmp(create_info->ppEnabledExtensionNames[i], VK_KHR_MAINTENANCE1_EXTENSION_NAME))
            return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    return vkCreateDevice(physical_device, create_info, allocator, device);
}

static PFN_vkVoidFunction VKAPI_CALL fake_vkGetInstanceProcAddr(VkInstance instance,
        const char *name)
{
    if (!strcmp(name, "vkCreateDevice"))
        return (PFN_vkVoidFunction)fake_vkCreateDevice;
    if (!strcmp(name, "vkEnumerateDeviceExtensionProperties"))
        return (PFN_vkVoidFunction)fake_vkEnumerateDeviceExtensionProperties;

    return vkGetInstanceProcAddr(instance, name);
}

static void test_required_device_extensions(void)
{
    struct vkd3d_instance_create_info instance_create_info;
    struct vkd3d_device_create_info device_create_info;
    struct vkd3d_instance *instance;
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;

    instance_create_info = instance_default_create_info;
    instance_create_info.vkGetInstanceProcAddr_pfn = fake_vkGetInstanceProcAddr;
    hr = vkd3d_create_instance(&instance_create_info, &instance);
    ok(hr == S_OK, "Failed to create instance, hr %#x.\n", hr);

    device_create_info = device_default_create_info;
    device_create_info.instance = instance;
    device_create_info.instance_create_info = NULL;
    hr = vkd3d_create_device(&device_create_info, &IID_ID3D12Device, (void **)&device);
    ok(hr == E_FAIL, "Failed to create device, hr %#x.\n", hr);

    refcount = vkd3d_instance_decref(instance);
    ok(!refcount, "Instance has %u references left.\n", refcount);
}

static void test_additional_device_extensions(void)
{
    struct vulkan_extension extensions[] =
    {
        {VK_KHR_SWAPCHAIN_EXTENSION_NAME},
    };

    struct vkd3d_instance_create_info instance_create_info;
    const char *enabled_extensions[ARRAY_SIZE(extensions)];
    struct vkd3d_device_create_info device_create_info;
    VkPhysicalDevice vk_physical_device;
    struct vkd3d_instance *instance;
    uint32_t extension_count;
    PFN_vkVoidFunction pfn;
    VkInstance vk_instance;
    ID3D12Device *device;
    VkDevice vk_device;
    uint32_t count;
    ULONG refcount;
    VkResult vr;
    HRESULT hr;

    /* Required by VK_KHR_swapchain. */
    enabled_extensions[0] = VK_KHR_SURFACE_EXTENSION_NAME;
    extension_count = 1;

    instance_create_info = instance_default_create_info;
    instance_create_info.instance_extensions = enabled_extensions;
    instance_create_info.instance_extension_count = extension_count;
    if (FAILED(hr = vkd3d_create_instance(&instance_create_info, &instance)))
    {
        skip("Failed to create instance, hr %#x.\n", hr);
        return;
    }
    ok(hr == S_OK, "Failed to create instance, hr %#x.\n", hr);
    vk_instance = vkd3d_instance_get_vk_instance(instance);
    ok(vk_instance != VK_NULL_HANDLE, "Failed to get Vulkan instance.\n");

    vr = vkEnumeratePhysicalDevices(vk_instance, &count, NULL);
    ok(vr == VK_SUCCESS, "Got unexpected VkResult %d.\n", vr);
    count = 1;
    vr = vkEnumeratePhysicalDevices(vk_instance, &count, &vk_physical_device);
    ok(vr == VK_SUCCESS || vr == VK_INCOMPLETE, "Got unexpected VkResult %d.\n", vr);

    if (!(extension_count = check_device_extensions(vk_physical_device,
            enabled_extensions, extensions, ARRAY_SIZE(extensions))))
    {
        skip("%s is not available.\n", VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        vkd3d_instance_decref(instance);
        return;
    }

    device_create_info = device_default_create_info;
    device_create_info.instance = instance;
    device_create_info.instance_create_info = NULL;
    device_create_info.vk_physical_device = vk_physical_device;
    device_create_info.device_extensions = enabled_extensions;
    device_create_info.device_extension_count = extension_count;
    hr = vkd3d_create_device(&device_create_info, &IID_ID3D12Device, (void **)&device);
    ok(hr == S_OK, "Failed to create device, hr %#x.\n", hr);

    vk_device = vkd3d_get_vk_device(device);

    pfn = vkGetDeviceProcAddr(vk_device, "vkCreateSwapchainKHR");
    ok(pfn, "Failed to get proc addr for vkCreateSwapchainKHR.\n");

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
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

static void test_adapter_luid(void)
{
    struct vkd3d_device_create_info create_info;
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;
    LUID luid;

    create_info = device_default_create_info;
    create_info.adapter_luid.HighPart = 0xdeadc0de;
    create_info.adapter_luid.LowPart = 0xdeadbeef;
    hr = vkd3d_create_device(&create_info, &IID_ID3D12Device, (void **)&device);
    ok(hr == S_OK, "Failed to create device, hr %#x.\n", hr);

    luid = ID3D12Device_GetAdapterLuid(device);
    ok(luid.HighPart == 0xdeadc0de && luid.LowPart == 0xdeadbeef,
            "Got unexpected LUID %08x:%08x.\n", luid.HighPart, luid.LowPart);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

struct parent
{
    IUnknown IUnknown_iface;
    LONG refcount;
};

static struct parent *parent_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct parent, IUnknown_iface);
}

static HRESULT STDMETHODCALLTYPE parent_QueryInterface(IUnknown *iface,
        REFIID iid, void **object)
{
    if (IsEqualGUID(iid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    ok(false, "Unexpected QueryInterface() call.\n");
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE parent_AddRef(IUnknown *iface)
{
    struct parent *parent = parent_from_IUnknown(iface);

    return InterlockedIncrement(&parent->refcount);
}

static ULONG STDMETHODCALLTYPE parent_Release(IUnknown *iface)
{
    struct parent *parent = parent_from_IUnknown(iface);

    return InterlockedDecrement(&parent->refcount);
}

static const struct IUnknownVtbl parent_vtbl =
{
    parent_QueryInterface,
    parent_AddRef,
    parent_Release,
};

static void test_device_parent(void)
{
    struct vkd3d_device_create_info create_info;
    struct parent parent_impl;
    ID3D12Device *device;
    IUnknown *unknown;
    IUnknown *parent;
    ULONG refcount;
    HRESULT hr;

    parent_impl.IUnknown_iface.lpVtbl = &parent_vtbl;
    parent_impl.refcount = 1;
    parent = &parent_impl.IUnknown_iface;

    refcount = get_refcount(parent);
    ok(refcount == 1, "Got unexpected refcount %u.\n", refcount);

    create_info = device_default_create_info;
    create_info.parent = parent;
    hr = vkd3d_create_device(&create_info, &IID_ID3D12Device, (void **)&device);
    ok(hr == S_OK, "Failed to create device, hr %#x.\n", hr);

    refcount = get_refcount(parent);
    ok(refcount == 2, "Got unexpected refcount %u.\n", refcount);

    unknown = vkd3d_get_device_parent(device);
    ok(unknown == parent, "Got device parent %p, expected %p.\n", unknown, parent);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);

    refcount = get_refcount(parent);
    ok(refcount == 1, "Got unexpected refcount %u.\n", refcount);
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

static void test_resource_internal_refcount(void)
{
    ID3D12Resource *resource;
    ID3D12Device *device;
    ULONG refcount;

    device = create_device();
    ok(device, "Failed to create device.\n");

    resource = create_buffer(device, D3D12_HEAP_TYPE_UPLOAD, 1024,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    refcount = vkd3d_resource_incref(resource);
    ok(refcount == 2, "Got refcount %u.\n", refcount);
    refcount = ID3D12Resource_Release(resource);
    ok(!refcount, "Got refcount %u.\n", refcount);
    refcount = resource_get_internal_refcount(resource);
    ok(refcount == 1, "Got refcount %u.\n", refcount);
    refcount = vkd3d_resource_decref(resource);
    ok(!refcount, "Got refcount %u.\n", refcount);

    resource = create_buffer(device, D3D12_HEAP_TYPE_UPLOAD, 1024,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
    refcount = vkd3d_resource_incref(resource);
    ok(refcount == 2, "Got refcount %u.\n", refcount);
    refcount = ID3D12Resource_Release(resource);
    ok(!refcount, "Got refcount %u.\n", refcount);
    refcount = resource_get_internal_refcount(resource);
    ok(refcount == 1, "Got refcount %u.\n", refcount);
    refcount = ID3D12Resource_AddRef(resource);
    ok(refcount == 1, "Got refcount %u.\n", refcount);
    refcount = vkd3d_resource_decref(resource);
    ok(refcount == 1, "Got refcount %u.\n", refcount);
    refcount = ID3D12Resource_Release(resource);
    ok(!refcount, "Got refcount %u.\n", refcount);

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
    run_test(test_additional_instance_extensions);
    run_test(test_create_device);
    run_test(test_required_device_extensions);
    run_test(test_additional_device_extensions);
    run_test(test_physical_device);
    run_test(test_adapter_luid);
    run_test(test_device_parent);
    run_test(test_vkd3d_queue);
    run_test(test_resource_internal_refcount);
}
