/*
 * Copyright 2018 Józef Kucia for CodeWeavers
 * Copyright 2020 Joshua Ashton for Valve Software
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

#define VK_NO_PROTOTYPES
#include "vkd3d_win32.h"
#include "vkd3d_atomic.h"
#include "vkd3d_debug.h"

/* We need to specify the __declspec(dllexport) attribute
 * on MinGW because otherwise the stdcall aliases/fixups
 * don't get exported.
 */
#if defined(_MSC_VER)
  #define DLLEXPORT
#else
  #define DLLEXPORT __declspec(dllexport)
#endif

static spinlock_t library_spinlock = { 0 };
static HMODULE vulkan_module = NULL;

static PFN_vkGetInstanceProcAddr load_vulkan(void)
{
    spinlock_acquire(&library_spinlock);

    if (!vulkan_module)
        vulkan_module = LoadLibraryA("vulkan-1.dll");

    spinlock_release(&library_spinlock);

    if (vulkan_module)
        return (void *)GetProcAddress(vulkan_module, "vkGetInstanceProcAddr");

    return NULL;
}

HRESULT WINAPI DLLEXPORT D3D12GetDebugInterface(REFIID iid, void **debug)
{
    TRACE("iid %s, debug %p.\n", debugstr_guid(iid), debug);

    WARN("Returning DXGI_ERROR_SDK_COMPONENT_MISSING.\n");
    return DXGI_ERROR_SDK_COMPONENT_MISSING;
}

HRESULT WINAPI DLLEXPORT D3D12EnableExperimentalFeatures(UINT feature_count,
        const IID *iids, void *configurations, UINT *configurations_sizes)
{
    FIXME("feature_count %u, iids %p, configurations %p, configurations_sizes %p stub!\n",
            feature_count, iids, configurations, configurations_sizes);

    return E_NOINTERFACE;
}

static HRESULT d3d12_signal_event(HANDLE event)
{
    return SetEvent(event) ? S_OK : E_FAIL;
}

struct d3d12_thread_data
{
    PFN_vkd3d_thread main_pfn;
    void *data;
};

static DWORD WINAPI d3d12_thread_main(void *data)
{
    struct d3d12_thread_data *thread_data = data;

    thread_data->main_pfn(thread_data->data);
    free(thread_data);
    return 0;
}

static void *d3d12_create_thread(PFN_vkd3d_thread main_pfn, void *data)
{
    struct d3d12_thread_data *thread_data;
    HANDLE thread;

    if (!(thread_data = malloc(sizeof(*thread_data))))
    {
        ERR("Failed to allocate thread data.\n");
        return NULL;
    }

    thread_data->main_pfn = main_pfn;
    thread_data->data = data;

    if (!(thread = CreateThread(NULL, 0, d3d12_thread_main, thread_data, 0, NULL)))
        free(thread_data);

    return thread;
}

static HRESULT d3d12_join_thread(void *handle)
{
    HANDLE thread = handle;
    DWORD ret;

    if ((ret = WaitForSingleObject(thread, INFINITE)) != WAIT_OBJECT_0)
    {
        ERR("Failed to wait for thread, ret %#x.\n", ret);
    }
    CloseHandle(thread);
    return ret == WAIT_OBJECT_0 ? S_OK : E_FAIL;
}

static HRESULT d3d12_get_adapter(IDXGIAdapter **dxgi_adapter, IUnknown *adapter)
{
    IDXGIFactory4 *factory = NULL;
    HRESULT hr;

    if (!adapter)
    {
        if (FAILED(hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory)))
        {
            WARN("Failed to create DXGI factory, hr %#x.\n", hr);
            goto done;
        }

        if (FAILED(hr = IDXGIFactory4_EnumAdapters(factory, 0, dxgi_adapter)))
        {
            WARN("Failed to enumerate primary adapter, hr %#x.\n", hr);
            goto done;
        }
    }
    else
    {
        if (FAILED(hr = IUnknown_QueryInterface(adapter, &IID_IDXGIAdapter, (void **)dxgi_adapter)))
        {
            WARN("Invalid adapter %p, hr %#x.\n", adapter, hr);
            goto done;
        }
    }

done:
    if (factory)
        IDXGIFactory4_Release(factory);

    return hr;
}

static BOOL check_vk_instance_extension(VkInstance vk_instance,
        PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr, const char *name)
{
    PFN_vkEnumerateInstanceExtensionProperties pfn_vkEnumerateInstanceExtensionProperties;
    VkExtensionProperties *properties;
    BOOL ret = FALSE;
    unsigned int i;
    uint32_t count;

    pfn_vkEnumerateInstanceExtensionProperties
            = (void *)pfn_vkGetInstanceProcAddr(vk_instance, "vkEnumerateInstanceExtensionProperties");

    if (pfn_vkEnumerateInstanceExtensionProperties(NULL, &count, NULL) < 0)
        return FALSE;

    if (!(properties = calloc(count, sizeof(*properties))))
        return FALSE;

    if (pfn_vkEnumerateInstanceExtensionProperties(NULL, &count, properties) >= 0)
    {
        for (i = 0; i < count; ++i)
        {
            if (!strcmp(properties[i].extensionName, name))
            {
                ret = TRUE;
                break;
            }
        }
    }

    free(properties);
    return ret;
}

static VkPhysicalDevice d3d12_get_vk_physical_device(struct vkd3d_instance *instance,
        PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr, struct DXGI_ADAPTER_DESC *adapter_desc)
{
    PFN_vkGetPhysicalDeviceProperties2 pfn_vkGetPhysicalDeviceProperties2 = NULL;
    PFN_vkGetPhysicalDeviceProperties pfn_vkGetPhysicalDeviceProperties;
    PFN_vkEnumeratePhysicalDevices pfn_vkEnumeratePhysicalDevices;
    VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceIDProperties id_properties;
    VkPhysicalDeviceProperties2 properties2;
    VkPhysicalDevice *vk_physical_devices;
    VkInstance vk_instance;
    unsigned int i;
    uint32_t count;
    VkResult vr;

    vk_instance = vkd3d_instance_get_vk_instance(instance);

    pfn_vkEnumeratePhysicalDevices = (void *)pfn_vkGetInstanceProcAddr(vk_instance, "vkEnumeratePhysicalDevices");

    pfn_vkGetPhysicalDeviceProperties = (void *)pfn_vkGetInstanceProcAddr(vk_instance, "vkGetPhysicalDeviceProperties");
    if (check_vk_instance_extension(vk_instance, pfn_vkGetInstanceProcAddr, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        pfn_vkGetPhysicalDeviceProperties2 = (void *)pfn_vkGetInstanceProcAddr(vk_instance, "vkGetPhysicalDeviceProperties2KHR");

    if ((vr = pfn_vkEnumeratePhysicalDevices(vk_instance, &count, NULL)) < 0)
    {
        WARN("Failed to get device count, vr %d.\n", vr);
        return VK_NULL_HANDLE;
    }
    if (!count)
    {
        WARN("No physical device available.\n");
        return VK_NULL_HANDLE;
    }

    if (!(vk_physical_devices = calloc(count, sizeof(*vk_physical_devices))))
        return VK_NULL_HANDLE;

    if ((vr = pfn_vkEnumeratePhysicalDevices(vk_instance, &count, vk_physical_devices)) < 0)
        goto done;

    if (pfn_vkGetPhysicalDeviceProperties2)
    {
        TRACE("Matching adapters by LUIDs.\n");

        for (i = 0; i < count; ++i)
        {
            memset(&id_properties, 0, sizeof(id_properties));
            id_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

            properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties2.pNext = &id_properties;

            pfn_vkGetPhysicalDeviceProperties2(vk_physical_devices[i], &properties2);

            if (!memcmp(id_properties.deviceLUID, &adapter_desc->AdapterLuid, VK_LUID_SIZE))
            {
                vk_physical_device = vk_physical_devices[i];
                break;
            }
        }
    }

    TRACE("Matching adapters by PCI IDs.\n");

    for (i = 0; i < count; ++i)
    {
        pfn_vkGetPhysicalDeviceProperties(vk_physical_devices[i], &properties2.properties);

        if (properties2.properties.deviceID == adapter_desc->DeviceId &&
            properties2.properties.vendorID == adapter_desc->VendorId)
        {
            vk_physical_device = vk_physical_devices[i];
            break;
        }
    }

    if (!vk_physical_device)
    {
        FIXME("Could not find Vulkan physical device for DXGI adapter.\n");
        WARN("Using first available physical device...\n");
        vk_physical_device = vk_physical_devices[0];
    }

done:
    free(vk_physical_devices);
    return vk_physical_device;
}

HRESULT WINAPI DLLEXPORT D3D12CreateDevice(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level,
        REFIID iid, void **device)
{
    struct vkd3d_optional_instance_extensions_info optional_extensions_info;
    struct vkd3d_instance_create_info instance_create_info;
    PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr;
    struct vkd3d_device_create_info device_create_info;
    struct DXGI_ADAPTER_DESC adapter_desc;
    struct vkd3d_instance *instance;
    IDXGIAdapter *dxgi_adapter;
    HRESULT hr;

    static const char * const instance_extensions[] =
    {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };
    static const char * const optional_instance_extensions[] =
    {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    static const char * const device_extensions[] =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    TRACE("adapter %p, minimum_feature_level %#x, iid %s, device %p.\n",
            adapter, minimum_feature_level, debugstr_guid(iid), device);

    if (!(pfn_vkGetInstanceProcAddr = load_vulkan()))
    {
        ERR("Failed to load Vulkan library.\n");
        return E_FAIL;
    }

    if (FAILED(hr = d3d12_get_adapter(&dxgi_adapter, adapter)))
        return hr;

    if (FAILED(hr = IDXGIAdapter_GetDesc(dxgi_adapter, &adapter_desc)))
    {
        WARN("Failed to get adapter desc, hr %#x.\n", hr);
        goto done;
    }

    optional_extensions_info.type = VKD3D_STRUCTURE_TYPE_OPTIONAL_INSTANCE_EXTENSIONS_INFO;
    optional_extensions_info.next = NULL;
    optional_extensions_info.extensions = optional_instance_extensions;
    optional_extensions_info.extension_count = ARRAYSIZE(optional_instance_extensions);

    instance_create_info.type = VKD3D_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_create_info.next = &optional_extensions_info;
    instance_create_info.pfn_signal_event = d3d12_signal_event;
    instance_create_info.pfn_create_thread = d3d12_create_thread;
    instance_create_info.pfn_join_thread = d3d12_join_thread;
    instance_create_info.wchar_size = sizeof(WCHAR);
    instance_create_info.pfn_vkGetInstanceProcAddr = pfn_vkGetInstanceProcAddr;
    instance_create_info.instance_extensions = instance_extensions;
    instance_create_info.instance_extension_count = ARRAYSIZE(instance_extensions);

    if (FAILED(hr = vkd3d_create_instance(&instance_create_info, &instance)))
    {
        WARN("Failed to create vkd3d instance, hr %#x.\n", hr);
        goto done;
    }

    device_create_info.type = VKD3D_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.next = NULL;
    device_create_info.minimum_feature_level = minimum_feature_level;
    device_create_info.instance = instance;
    device_create_info.instance_create_info = NULL;
    device_create_info.vk_physical_device = d3d12_get_vk_physical_device(instance, pfn_vkGetInstanceProcAddr, &adapter_desc);
    device_create_info.device_extensions = device_extensions;
    device_create_info.device_extension_count = ARRAYSIZE(device_extensions);
    device_create_info.parent = (IUnknown *)dxgi_adapter;
    memcpy(&device_create_info.adapter_luid, &adapter_desc.AdapterLuid, VK_LUID_SIZE);

    hr = vkd3d_create_device(&device_create_info, iid, device);

    vkd3d_instance_decref(instance);

done:
    IDXGIAdapter_Release(dxgi_adapter);
    return hr;
}

HRESULT WINAPI DLLEXPORT D3D12CreateRootSignatureDeserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer)
{
    TRACE("data %p, data_size %lu, iid %s, deserializer %p.\n",
            data, data_size, debugstr_guid(iid), deserializer);

    return vkd3d_create_root_signature_deserializer(data, data_size, iid, deserializer);
}

HRESULT WINAPI DLLEXPORT D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC *root_signature_desc,
        D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob, ID3DBlob **error_blob)
{
    TRACE("root_signature_desc %p, version %#x, blob %p, error_blob %p.\n",
            root_signature_desc, version, blob, error_blob);

    return vkd3d_serialize_root_signature(root_signature_desc, version, blob, error_blob);
}

HRESULT WINAPI DLLEXPORT D3D12CreateVersionedRootSignatureDeserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer)
{
    TRACE("data %p, data_size %lu, iid %s, deserializer %p.\n",
            data, data_size, debugstr_guid(iid), deserializer);

    return vkd3d_create_versioned_root_signature_deserializer(data, data_size, iid, deserializer);
}

HRESULT WINAPI DLLEXPORT D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc,
        ID3DBlob **blob, ID3DBlob **error_blob)
{
    TRACE("desc %p, blob %p, error_blob %p.\n", desc, blob, error_blob);

    return vkd3d_serialize_versioned_root_signature(desc, blob, error_blob);
}
