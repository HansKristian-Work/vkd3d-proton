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

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#include "vkd3d_win32.h"
#endif
#include "vkd3d_sonames.h"
#include "vkd3d.h"
#include "vkd3d_atomic.h"
#include "vkd3d_debug.h"
#include "vkd3d_threads.h"
#include "vkd3d_core_interface.h"

#include "debug.h"

/* We need to specify the __declspec(dllexport) attribute
 * on MinGW because otherwise the stdcall aliases/fixups
 * don't get exported.
 */
#if defined(_MSC_VER)
  #define DLLEXPORT
#elif defined(__MINGW32__)
  #define DLLEXPORT __declspec(dllexport)
#else
  #define DLLEXPORT __attribute__((visibility("default")))
  #include <dlfcn.h>
#endif

typedef IVKD3DCoreInterface d3d12core_interface;
HRESULT WINAPI DLLEXPORT D3D12GetInterface(REFCLSID rcslid, REFIID iid, void** debug);

static pthread_once_t library_once = PTHREAD_ONCE_INIT;

#ifdef _WIN32
static HMODULE vulkan_module = NULL;
#else
static void *vulkan_module = NULL;
#endif
static PFN_vkGetInstanceProcAddr vulkan_vkGetInstanceProcAddr = NULL;

static void load_vulkan_once(void)
{
    if (!vulkan_module)
    {
#ifdef _WIN32
        /* If possible, load winevulkan directly in order to bypass
         * issues with third-party overlays hooking the Vulkan loader */
        static const char *vulkan_dllnames[] =
        {
            "winevulkan.dll",
            "vulkan-1.dll",
        };

        unsigned int i;

        for (i = 0; i < ARRAY_SIZE(vulkan_dllnames); i++)
        {
            vulkan_module = LoadLibraryA(vulkan_dllnames[i]);

            if (vulkan_module)
            {
                vulkan_vkGetInstanceProcAddr = (void*)GetProcAddress(vulkan_module, "vkGetInstanceProcAddr");

                if (vulkan_vkGetInstanceProcAddr)
                    break;

                FreeLibrary(vulkan_module);
                vulkan_module = NULL;
            }
        }
#else
        vulkan_module = dlopen(SONAME_LIBVULKAN, RTLD_LAZY);
        vulkan_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(vulkan_module, "vkGetInstanceProcAddr");
#endif
    }
}

static bool load_vulkan(void)
{
    pthread_once(&library_once, load_vulkan_once);
    return vulkan_vkGetInstanceProcAddr != NULL;
}

#ifdef _WIN32
/* TODO: We need to attempt to dlopen() native DXVK DXGI. */
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

static VkPhysicalDevice d3d12_find_physical_device(struct vkd3d_instance *instance,
        PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr, struct DXGI_ADAPTER_DESC *adapter_desc)
{
    PFN_vkGetPhysicalDeviceProperties2 pfn_vkGetPhysicalDeviceProperties2;
    PFN_vkGetPhysicalDeviceProperties pfn_vkGetPhysicalDeviceProperties;
    PFN_vkEnumeratePhysicalDevices pfn_vkEnumeratePhysicalDevices;
    VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceIDProperties id_properties;
    VkPhysicalDeviceProperties2 properties2;
    VkPhysicalDevice *vk_physical_devices;
    VkInstance vk_instance;
    unsigned int i, j;
    uint32_t count;
    VkResult vr;
    bool match;

    vk_instance = vkd3d_instance_get_vk_instance(instance);

    pfn_vkEnumeratePhysicalDevices = (void *)pfn_vkGetInstanceProcAddr(vk_instance, "vkEnumeratePhysicalDevices");
    pfn_vkGetPhysicalDeviceProperties = (void *)pfn_vkGetInstanceProcAddr(vk_instance, "vkGetPhysicalDeviceProperties");
    pfn_vkGetPhysicalDeviceProperties2 = (void *)pfn_vkGetInstanceProcAddr(vk_instance, "vkGetPhysicalDeviceProperties2");

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

    TRACE("Matching adapters by LUIDs.\n");

    for (i = 0; i < count; ++i)
    {
        pfn_vkGetPhysicalDeviceProperties(vk_physical_devices[i], &properties2.properties);

        /* Skip over physical devices below our minimum API version */
        if (properties2.properties.apiVersion < VKD3D_MIN_API_VERSION)
        {
            WARN("Skipped adapter %s as it is below our minimum API version.", properties2.properties.deviceName);
            continue;
        }

        id_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        id_properties.pNext = NULL;

        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &id_properties;

        pfn_vkGetPhysicalDeviceProperties2(vk_physical_devices[i], &properties2);

        if (id_properties.deviceLUIDValid && !memcmp(id_properties.deviceLUID, &adapter_desc->AdapterLuid, VK_LUID_SIZE))
        {
            match = true;

            if (vk_physical_device)
            {
                WARN("Multiple adapters found with LUID %#x%x.\n", adapter_desc->AdapterLuid.HighPart, adapter_desc->AdapterLuid.LowPart);

                match = properties2.properties.deviceID == adapter_desc->DeviceId &&
                        properties2.properties.vendorID == adapter_desc->VendorId;

                if (!match)
                {
                    /* For simplicity, assume that adapter names are all ASCII characters */
                    match = true;

                    for (j = 0; j < ARRAY_SIZE(adapter_desc->Description); j++)
                    {
                        WCHAR a = (WCHAR)properties2.properties.deviceName[j];
                        WCHAR b = adapter_desc->Description[j];

                        if (!(match = (a == b)) || !a || !b)
                          break;
                    }
                }
            }

            if (match)
                vk_physical_device = vk_physical_devices[i];
        }
    }

    if (!vk_physical_device)
    {
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
#endif

static HRESULT vkd3d_create_instance_global(struct vkd3d_instance **out_instance)
{
    struct vkd3d_instance_create_info instance_create_info;
    HRESULT hr = S_OK;

    static const char * const instance_extensions[] =
    {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#else
        /* TODO: We need to attempt to dlopen() native DXVK DXGI and handle this more gracefully. */
        "VK_KHR_xcb_surface",
#endif
    };

    if (!load_vulkan())
    {
        ERR("Failed to load Vulkan library.\n");
        return E_FAIL;
    }

    memset(&instance_create_info, 0, sizeof(instance_create_info));
    instance_create_info.pfn_vkGetInstanceProcAddr = vulkan_vkGetInstanceProcAddr;
    instance_create_info.instance_extensions = instance_extensions;
    instance_create_info.instance_extension_count = ARRAY_SIZE(instance_extensions);
    instance_create_info.optional_instance_extensions = NULL;
    instance_create_info.optional_instance_extension_count = 0;

    if (FAILED(hr = vkd3d_create_instance(&instance_create_info, out_instance)))
        WARN("Failed to create vkd3d instance, hr %#x.\n", hr);

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12core_CreateDevice(d3d12core_interface *core,
        IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level, REFIID iid, void **device)
{
    struct vkd3d_device_create_info device_create_info;
    struct vkd3d_instance *instance;
    HRESULT hr;

#ifdef _WIN32
    struct DXGI_ADAPTER_DESC adapter_desc;
    IDXGIAdapter *dxgi_adapter;
#endif

    static const char * const device_extensions[] =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,
    };

    TRACE("adapter %p, minimum_feature_level %#x, iid %s, device %p.\n",
            adapter, minimum_feature_level, debugstr_guid(iid), device);

#ifdef _WIN32
    if (FAILED(hr = d3d12_get_adapter(&dxgi_adapter, adapter)))
        return hr;

    if (FAILED(hr = IDXGIAdapter_GetDesc(dxgi_adapter, &adapter_desc)))
    {
        WARN("Failed to get adapter desc, hr %#x.\n", hr);
        goto out_release_adapter;
    }
#else
    /* TODO: We need to attempt to dlopen() native DXVK DXGI and handle this more gracefully. */
    if (adapter)
        FIXME("Ignoring adapter.\n");
#endif

    if (FAILED(hr = vkd3d_create_instance_global(&instance)))
        return hr;

    memset(&device_create_info, 0, sizeof(device_create_info));
    device_create_info.minimum_feature_level = minimum_feature_level;
    device_create_info.instance = instance;
    device_create_info.instance_create_info = NULL;
    device_create_info.device_extensions = device_extensions;
    device_create_info.device_extension_count = ARRAY_SIZE(device_extensions);
    device_create_info.optional_device_extensions = NULL;
    device_create_info.optional_device_extension_count = 0;

#ifdef _WIN32
    device_create_info.vk_physical_device = d3d12_find_physical_device(instance, vulkan_vkGetInstanceProcAddr, &adapter_desc);
    device_create_info.parent = (IUnknown *)dxgi_adapter;
    memcpy(&device_create_info.adapter_luid, &adapter_desc.AdapterLuid, VK_LUID_SIZE);
#endif

    hr = vkd3d_create_device(&device_create_info, iid, device);
    vkd3d_instance_decref(instance);

#ifdef _WIN32
out_release_adapter:
    IDXGIAdapter_Release(dxgi_adapter);
#endif
    return hr;
}

HRESULT STDMETHODCALLTYPE d3d12core_CreateRootSignatureDeserializer(d3d12core_interface *core,
        const void *data, SIZE_T data_size, REFIID iid, void **deserializer)
{
    TRACE("data %p, data_size %lu, iid %s, deserializer %p.\n",
            data, data_size, debugstr_guid(iid), deserializer);

    return vkd3d_create_root_signature_deserializer(data, data_size, iid, deserializer);
}

HRESULT STDMETHODCALLTYPE d3d12core_SerializeRootSignature(d3d12core_interface *core,
        const D3D12_ROOT_SIGNATURE_DESC *root_signature_desc, D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob, ID3DBlob **error_blob)
{
    TRACE("root_signature_desc %p, version %#x, blob %p, error_blob %p.\n",
            root_signature_desc, version, blob, error_blob);

    return vkd3d_serialize_root_signature(root_signature_desc, version, blob, error_blob);
}

HRESULT STDMETHODCALLTYPE d3d12core_CreateVersionedRootSignatureDeserializer(d3d12core_interface *core,
        const void *data, SIZE_T data_size, REFIID iid, void **deserializer)
{
    TRACE("data %p, data_size %lu, iid %s, deserializer %p.\n",
            data, data_size, debugstr_guid(iid), deserializer);

    return vkd3d_create_versioned_root_signature_deserializer(data, data_size, iid, deserializer);
}

HRESULT STDMETHODCALLTYPE d3d12core_SerializeVersionedRootSignature(d3d12core_interface *core,
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc, ID3DBlob **blob, ID3DBlob **error_blob)
{
    TRACE("desc %p, blob %p, error_blob %p.\n", desc, blob, error_blob);

    return vkd3d_serialize_versioned_root_signature(desc, blob, error_blob);
}

HRESULT STDMETHODCALLTYPE d3d12core_GetDebugInterface(d3d12core_interface *core,
        REFIID iid, void** debug)
{
    ID3D12DeviceRemovedExtendedDataSettings *dred_settings;
    HRESULT hr;

    TRACE("iid %s, debug %p.\n", debugstr_guid(iid), debug);

    if (debug)
        *debug = NULL;

    if (!memcmp(iid, &IID_ID3D12DeviceRemovedExtendedDataSettings, sizeof(*iid)))
    {
        hr = d3d12_dred_settings_create(&dred_settings);
        *debug = dred_settings;
        return hr;
    }

    WARN("Returning DXGI_ERROR_SDK_COMPONENT_MISSING.\n");
    return DXGI_ERROR_SDK_COMPONENT_MISSING;
}

HRESULT STDMETHODCALLTYPE d3d12core_EnableExperimentalFeatures(d3d12core_interface *core,
        UINT feature_count, const IID *iids, void *configurations, UINT *configurations_sizes)
{
    FIXME("feature_count %u, iids %p, configurations %p, configurations_sizes %p stub!\n",
            feature_count, iids, configurations, configurations_sizes);

    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE d3d12core_GetInterface(d3d12core_interface *core,
        REFCLSID rcslid, REFIID iid, void **debug)
{
    TRACE("rcslid %s iid %s, debug %p.\n", debugstr_guid(rcslid), debugstr_guid(iid), debug);

    return D3D12GetInterface(rcslid, iid, debug);
}

static CONST_VTBL struct IVKD3DCoreInterfaceVtbl d3d12core_interface_vtbl =
{
    d3d12core_CreateDevice,
    d3d12core_CreateRootSignatureDeserializer,
    d3d12core_SerializeRootSignature,
    d3d12core_CreateVersionedRootSignatureDeserializer,
    d3d12core_SerializeVersionedRootSignature,
    d3d12core_GetDebugInterface,
    d3d12core_EnableExperimentalFeatures,
    d3d12core_GetInterface,
};

static const d3d12core_interface d3d12core_interface_instance =
{
    .lpVtbl = &d3d12core_interface_vtbl,
};

HRESULT WINAPI DLLEXPORT D3D12GetInterface(REFCLSID rcslid, REFIID iid, void **debug)
{
    TRACE("rcslid %s iid %s, debug %p.\n", debugstr_guid(rcslid), debugstr_guid(iid), debug);

    if (IsEqualGUID(rcslid, &CLSID_VKD3DCore))
    {
        if (IsEqualGUID(iid, &IID_IVKD3DCoreInterface))
        {
            *debug = (void*)&d3d12core_interface_instance;
            return S_OK;
        }
    }
    return E_NOINTERFACE;
}


/* Just expose the latest stable AgilitySDK version.
 * This is actually exported as a UINT and not a function it seems. */
DLLEXPORT const UINT D3D12SDKVersion = 610;
