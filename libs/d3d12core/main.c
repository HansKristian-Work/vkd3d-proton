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
#include <stdio.h>
#include "vkd3d_win32.h"
#endif
#include "vkd3d_sonames.h"
#include "vkd3d.h"
#include "vkd3d_atomic.h"
#include "vkd3d_debug.h"
#include "vkd3d_threads.h"
#include "vkd3d_core_interface.h"
#include "vkd3d_platform.h"

#include "debug.h"

#if defined(__WINE__) || !defined(_WIN32)
#define DLLEXPORT __attribute__((visibility("default")))
#include <dlfcn.h>
#else
#define DLLEXPORT
#endif

typedef IVKD3DCoreInterface d3d12core_interface;
HRESULT WINAPI DLLEXPORT D3D12GetInterface(REFCLSID rcslid, REFIID iid, void** debug);

static pthread_once_t library_once = PTHREAD_ONCE_INIT;

#ifdef _WIN32
static HMODULE vulkan_module = NULL;
static HMODULE wineopenxr_module = NULL;
typedef int (WINAPI *PFN___wineopenxr_GetVulkanExtensions)(uint32_t, uint32_t *, char *);
PFN___wineopenxr_GetVulkanExtensions p___wineopenxr_GetVulkanInstanceExtensions = NULL;
PFN___wineopenxr_GetVulkanExtensions p___wineopenxr_GetVulkanDeviceExtensions = NULL;
#else
static void *vulkan_module = NULL;
#endif
static PFN_vkGetInstanceProcAddr vulkan_vkGetInstanceProcAddr = NULL;

#ifdef _WIN32
static BOOL wait_vr_key(HKEY vr_key)
{
    DWORD type, value, wait_status, size;
    DWORD max_retry = 10; /* Each value query and each timeout counts as a try. */
    LSTATUS status;
    HANDLE event;

    size = sizeof(value);
    if ((status = RegQueryValueExA(vr_key, "state", NULL, &type, (BYTE *)&value, &size)))
    {
        ERR("OpenVR: could not query value, status %d.\n", status);
        return false;
    }
    if (type != REG_DWORD)
    {
        ERR("OpenVR: unexpected value type %d.\n", type);
        return false;
    }

    if (value)
        return value == 1;

    event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (event == NULL)
    {
        ERR("Cannot create event.\n");
        return false;
    }

    while (max_retry)
    {
        if (RegNotifyChangeKeyValue(vr_key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, event, TRUE))
        {
            ERR("Error registering registry change notification.\n");
            break;
        }
        size = sizeof(value);
        if ((status = RegQueryValueExA(vr_key, "state", NULL, &type, (BYTE *)&value, &size)))
        {
            ERR("OpenVR: could not query value, status %d.\n", status);
            break;
        }
        if (value)
            break;
        max_retry--;

        while ((wait_status = WaitForSingleObject(event, 1000)) == WAIT_TIMEOUT && max_retry)
        {
            WARN("VR state wait timeout (retries left %u).\n", max_retry);
            max_retry--;
        }

        if (wait_status != WAIT_OBJECT_0 && wait_status != WAIT_TIMEOUT)
        {
            ERR("Got unexpected wait status %u.\n", wait_status);
            break;
        }
    }

    CloseHandle(event);
    return value == 1;
}

static char *openvr_instance_extensions()
{
    HKEY vr_key;

    LSTATUS status = RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\VR", &vr_key);
    char *openvr_instance_extensions = NULL;
    DWORD len = 0, type;

    if (status != ERROR_SUCCESS)
    {
        WARN("Failed to open VR registry key, status %d.\n", status);
        return NULL;
    }
    if (!wait_vr_key(vr_key))
    {
        WARN("Failed to wait for VR registry key ready.\n");
        RegCloseKey(vr_key);
        return NULL;
    }

    status = RegQueryValueExA(vr_key, "openvr_vulkan_instance_extensions", NULL, &type, NULL, &len);
    if (status != ERROR_SUCCESS)
    {
        WARN("Failed to query VR instance extensions, status %d.\n", status);
        RegCloseKey(vr_key);
        return NULL;
    }
    if (type != REG_SZ)
    {
        WARN("Unexpected VR instance extensions type %d.\n", type);
        RegCloseKey(vr_key);
        return NULL;
    }
    openvr_instance_extensions = vkd3d_calloc(len, sizeof(BYTE));
    status = RegQueryValueExA(vr_key, "openvr_vulkan_instance_extensions", NULL, &type, (BYTE *)openvr_instance_extensions, &len);
    if (status != ERROR_SUCCESS)
    {
        WARN("Failed to query VR instance extensions, status %d.\n", status);
        vkd3d_free(openvr_instance_extensions);
        openvr_instance_extensions = NULL;
    }
    RegCloseKey(vr_key);
    return openvr_instance_extensions;
}

static char *openvr_device_extensions(DXGI_ADAPTER_DESC *desc)
{
    HKEY vr_key;

    LSTATUS status = RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\VR", &vr_key);
    char *openvr_device_extensions = NULL;
    DWORD len = 0, type;
    char key_name[32];

    if (status != ERROR_SUCCESS)
    {
        WARN("Failed to open VR registry key, status %d.\n", status);
        return NULL;
    }
    if (!wait_vr_key(vr_key))
    {
        WARN("Failed to wait for VR registry key ready.\n");
        RegCloseKey(vr_key);
        return NULL;
    }

    snprintf(key_name, sizeof(key_name), "PCIID:%04x:%04x", desc->VendorId, desc->DeviceId);

    status = RegQueryValueExA(vr_key, key_name, NULL, &type, NULL, &len);
    if (status != ERROR_SUCCESS)
    {
        WARN("Failed to query VR instance extensions, status %d.\n", status);
        RegCloseKey(vr_key);
        return NULL;
    }
    if (type != REG_SZ)
    {
        WARN("Unexpected VR instance extensions type %d.\n", type);
        RegCloseKey(vr_key);
        return NULL;
    }
    openvr_device_extensions = vkd3d_calloc(len, sizeof(BYTE));
    status = RegQueryValueExA(vr_key, key_name, NULL, &type, (BYTE *)openvr_device_extensions, &len);
    if (status != ERROR_SUCCESS)
    {
        WARN("Failed to query VR instance extensions, status %d.\n", status);
        vkd3d_free(openvr_device_extensions);
        openvr_device_extensions = NULL;
    }
    RegCloseKey(vr_key);
    return openvr_device_extensions;
}

static char *openxr_vulkan_extensions(bool device_extensions)
{
    PFN___wineopenxr_GetVulkanExtensions get_extensions = NULL;
    char *ret = NULL;
    uint32_t len;

    get_extensions = device_extensions ? p___wineopenxr_GetVulkanDeviceExtensions
                                       : p___wineopenxr_GetVulkanInstanceExtensions;
    if (!get_extensions)
    {
        WARN("wineopenxr.dll is missing required symbols.\n");
        return NULL;
    }

    if (get_extensions(0, &len, NULL))
    {
        WARN("Failed to get OpenXR extensions size from wineopenxr.\n");
        return NULL;
    }

    ret = vkd3d_malloc(len);
    if (get_extensions(len, &len, ret))
    {
        WARN("Failed to get OpenXR extensions from wineopenxr.\n");
        return NULL;
    }
    return ret;
}

static uint32_t parse_extension_list(char *extension_str, char **extension_list)
{
    char *cursor = extension_str;
    uint32_t count = 0;

    if (!cursor)
        return 0;

    while (*cursor)
    {
        char *next = strchr(cursor, ' ');
        count++;
        if (!next)
            break;
        cursor = next + 1;
    }

    if (!extension_list)
        return count;

    cursor = extension_str;
    for (uint32_t i = 0; i < count; i++)
    {
        char *next = strchr(cursor, ' ');
        extension_list[i] = cursor;
        if (!next)
            break;
        *next = '\0';
        cursor = next + 1;
    }
    return count;
}

static void passthrough_unix_environment(const char *env_name)
{
    char env[256];
    if (vkd3d_get_env_var(env_name, env, sizeof(env)))
    {
        const char *value;
        HMODULE ntdll;
        char *tok;

        tok = strchr(env, '=');
        if (!tok)
            return;
        *tok = '\0';

        value = tok + 1;

        ntdll = GetModuleHandleA("ntdll.dll");
        if (ntdll)
        {
            NTSTATUS (WINAPI *func)(const char *, const char *) = (void *)GetProcAddress(ntdll, "__wine_set_unix_env");
            if (func)
            {
                INFO("Forwarding env-var \"%s=%s\" to Linux.\n", env, value);
                func(env, value);
                SetEnvironmentVariableA(env, value);
            }
            else
                ERR("Could not find __wine_set_unix_env in ntdll. Not running in Proton?\n");
        }
        else
            ERR("Could not find ntdll?!\n");
    }
}
#endif

static void load_modules_once(void)
{
#ifdef _WIN32
    if (!wineopenxr_module)
        wineopenxr_module = LoadLibraryA("wineopenxr.dll");
    if (wineopenxr_module)
    {
        p___wineopenxr_GetVulkanDeviceExtensions =
                (void *)GetProcAddress(wineopenxr_module, "__wineopenxr_GetVulkanDeviceExtensions");
        p___wineopenxr_GetVulkanInstanceExtensions =
                (void *)GetProcAddress(wineopenxr_module, "__wineopenxr_GetVulkanInstanceExtensions");
    }
#endif
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

static bool load_modules(void)
{
    pthread_once(&library_once, load_modules_once);
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
            WARN("Skipped adapter %s as it is below our minimum API version.\n", properties2.properties.deviceName);
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
#endif
    };

    static const char * const optional_instance_extensions[] =
    {
        VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
#ifndef _WIN32
        /* TODO: We need to attempt to dlopen() native DXVK DXGI and handle this more gracefully. */
        "VK_KHR_xcb_surface",
        "VK_KHR_xlib_surface",
        "VK_KHR_wayland_surface",
#endif
    };

#ifdef _WIN32
    char *openvr_extensions, *openxr_extensions;
    uint32_t vr_extension_count;
#endif

    if (!load_modules())
    {
        ERR("Failed to load Vulkan library.\n");
        return E_FAIL;
    }

    memset(&instance_create_info, 0, sizeof(instance_create_info));
    instance_create_info.pfn_vkGetInstanceProcAddr = vulkan_vkGetInstanceProcAddr;
    instance_create_info.instance_extensions = instance_extensions;
    instance_create_info.instance_extension_count = ARRAY_SIZE(instance_extensions);
    instance_create_info.optional_instance_extensions = optional_instance_extensions;
    instance_create_info.optional_instance_extension_count = ARRAY_SIZE(optional_instance_extensions);

#ifdef _WIN32
    openvr_extensions = openvr_instance_extensions();
    openxr_extensions = openxr_vulkan_extensions(false);
    vr_extension_count = parse_extension_list(openvr_extensions, NULL) + parse_extension_list(openxr_extensions, NULL);
    if (vr_extension_count > 0)
    {
        size_t len = ARRAY_SIZE(instance_extensions) + vr_extension_count;
        uint32_t offset = ARRAY_SIZE(instance_extensions);
        char **extensions = calloc(len, sizeof(char *));
        memcpy(extensions, instance_extensions, sizeof(char *) * ARRAY_SIZE(instance_extensions));
        offset += parse_extension_list(openvr_extensions, extensions + offset);
        parse_extension_list(openxr_extensions, extensions + offset);
        instance_create_info.instance_extensions = (const char *const *)extensions;
        instance_create_info.instance_extension_count = len;
    }
#endif

    if (FAILED(hr = vkd3d_create_instance(&instance_create_info, out_instance)))
        WARN("Failed to create vkd3d instance, hr %#x.\n", hr);

#ifdef _WIN32
    if (instance_create_info.instance_extensions != instance_extensions)
        vkd3d_free((void *)instance_create_info.instance_extensions);
    vkd3d_free(openvr_extensions);
    vkd3d_free(openxr_extensions);
#endif

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12core_CreateDeviceFromFactory(
        IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level,
        ID3D12DeviceFactory *factory,
        REFIID iid, void **device)
{
    struct vkd3d_device_create_info device_create_info;
    struct vkd3d_instance *instance;
    HRESULT hr;

#ifdef _WIN32
    char *openvr_extensions, *openxr_extensions;
    struct DXGI_ADAPTER_DESC adapter_desc;
    uint32_t vr_extension_count;
    IDXGIAdapter *dxgi_adapter;
#endif

    static const char * const device_extensions[] =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    static const char * const optional_device_extensions[] =
    {
        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
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

    passthrough_unix_environment("VKD3D_UNIX_ENV");
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
    device_create_info.optional_device_extensions = optional_device_extensions;
    device_create_info.optional_device_extension_count = ARRAY_SIZE(optional_device_extensions);
    device_create_info.independent = factory != NULL;
    if (factory)
        device_create_info.device_factory_flags = ID3D12DeviceFactory_GetFlags(factory);

#ifdef _WIN32
    device_create_info.vk_physical_device = d3d12_find_physical_device(instance, vulkan_vkGetInstanceProcAddr, &adapter_desc);
    device_create_info.parent = (IUnknown *)dxgi_adapter;
    memcpy(&device_create_info.adapter_luid, &adapter_desc.AdapterLuid, VK_LUID_SIZE);

    openvr_extensions = openvr_device_extensions(&adapter_desc);
    openxr_extensions = openxr_vulkan_extensions(true);
    vr_extension_count = parse_extension_list(openvr_extensions, NULL) + parse_extension_list(openxr_extensions, NULL);
    if (vr_extension_count > 0)
    {
        size_t len = ARRAY_SIZE(device_extensions) + vr_extension_count;
        uint32_t offset = ARRAY_SIZE(device_extensions);
        char **extensions = calloc(len, sizeof(char *));
        memcpy(extensions, device_extensions, sizeof(char *) * ARRAY_SIZE(device_extensions));
        offset += parse_extension_list(openvr_extensions, extensions + offset);
        parse_extension_list(openxr_extensions, extensions + offset);
        device_create_info.device_extensions = (const char *const *)extensions;
        device_create_info.device_extension_count = len;
    }
#endif

    hr = vkd3d_create_device(&device_create_info, iid, device);
    vkd3d_instance_decref(instance);

#ifdef _WIN32
    passthrough_unix_environment("VKD3D_UNIX_POST_ENV");
    if (device_create_info.device_extensions != device_extensions)
        vkd3d_free((void *)device_create_info.device_extensions);
    vkd3d_free(openvr_extensions);
    vkd3d_free(openxr_extensions);

out_release_adapter:
    IDXGIAdapter_Release(dxgi_adapter);
#endif
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12core_CreateDevice(d3d12core_interface *core,
        IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level, REFIID iid, void **device)
{
    return d3d12core_CreateDeviceFromFactory(adapter, minimum_feature_level, NULL, iid, device);
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

    /* FIXME: Unsure what to do about S_FALSE returns here since there is no clsid. */

    if (debug)
        *debug = NULL;

    if (IsEqualGUID(iid, &IID_ID3D12DeviceRemovedExtendedDataSettings))
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

static HRESULT d3d12core_D3D12GetInterface(REFCLSID rcslid, REFIID iid, void **debug);

static HRESULT STDMETHODCALLTYPE d3d12core_GetInterface(d3d12core_interface *core,
        REFCLSID rcslid, REFIID iid, void **debug)
{
    TRACE("rcslid %s iid %s, debug %p.\n", debugstr_guid(rcslid), debugstr_guid(iid), debug);

    /* Need to call the static one here, otherwise we end up calling back into d3d12, not d3d12core, creating
     * a stack overflow. */
    return d3d12core_D3D12GetInterface(rcslid, iid, debug);
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

static pthread_mutex_t vkd3d_debug_control_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t vkd3d_debug_control_is_running_under_test;
static uint32_t vkd3d_debug_control_explode_on_error;
static uint32_t vkd3d_debug_control_out_of_spec_behavior[VKD3D_DEBUG_CONTROL_OUT_OF_SPEC_BEHAVIOR_COUNT];
static uint32_t vkd3d_debug_control_mute_validation_global_counter;
static uint32_t vkd3d_debug_control_behavior_flags;

static struct vkd3d_debug_control_muted_vuid
{
    char vuid[8];
    char explanation[248];
} vkd3d_debug_control_muted_vuids[32];
static unsigned int vkd3d_debug_control_muted_vuid_count;

bool vkd3d_debug_control_is_test_suite(void)
{
    return vkd3d_atomic_uint32_load_explicit(&vkd3d_debug_control_is_running_under_test, vkd3d_memory_order_relaxed) != 0;
}

bool vkd3d_debug_control_explode_on_vvl_error(void)
{
    return vkd3d_atomic_uint32_load_explicit(&vkd3d_debug_control_explode_on_error, vkd3d_memory_order_relaxed) != 0;
}

bool vkd3d_debug_control_has_out_of_spec_test_behavior(VKD3D_DEBUG_CONTROL_OUT_OF_SPEC_BEHAVIOR behavior)
{
    if (behavior >= VKD3D_DEBUG_CONTROL_OUT_OF_SPEC_BEHAVIOR_COUNT)
        return false;
    return vkd3d_atomic_uint32_load_explicit(&vkd3d_debug_control_out_of_spec_behavior[behavior], vkd3d_memory_order_relaxed) != 0;
}

VKD3D_DEBUG_CONTROL_BEHAVIOR_FLAGS vkd3d_debug_control_get_behavior_flags(void)
{
    return vkd3d_atomic_uint32_load_explicit(&vkd3d_debug_control_behavior_flags, vkd3d_memory_order_relaxed);
}

bool vkd3d_debug_control_mute_message_id(const char *vuid)
{
    const struct vkd3d_debug_control_muted_vuid *entry;
    bool ret = false;
    unsigned int i;
    if (vkd3d_atomic_uint32_load_explicit(&vkd3d_debug_control_mute_validation_global_counter, vkd3d_memory_order_relaxed))
        return true;

    pthread_mutex_lock(&vkd3d_debug_control_lock);
    for (i = 0; i < vkd3d_debug_control_muted_vuid_count && !ret; i++)
    {
        entry = &vkd3d_debug_control_muted_vuids[i];

        if (strstr(vuid, entry->vuid))
        {
            ret = true;
            if (entry->explanation[0])
                INFO("Muted %s: %s\n", vuid, entry->explanation);
            else
                WARN("Muted %s.\n", vuid);
        }
    }
    pthread_mutex_unlock(&vkd3d_debug_control_lock);
    return ret;
}

static HRESULT STDMETHODCALLTYPE vkd3d_debug_control_SetRunningUnderTest(IVKD3DDebugControlInterface *iface)
{
    (void)iface;
    vkd3d_atomic_uint32_store_explicit(&vkd3d_debug_control_is_running_under_test, 1, vkd3d_memory_order_relaxed);
    INFO("Running in test suite.\n");
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vkd3d_debug_control_SetExplodeOnValidationError(
        IVKD3DDebugControlInterface *iface, BOOL enable)
{
    (void)iface;
    vkd3d_atomic_uint32_store_explicit(&vkd3d_debug_control_explode_on_error, enable, vkd3d_memory_order_relaxed);
    if (enable)
        INFO("Enabling explode-on-VVL-error test mode.\n");
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vkd3d_debug_control_MuteValidationGlobal(IVKD3DDebugControlInterface *iface)
{
    (void)iface;
    vkd3d_atomic_uint32_increment(&vkd3d_debug_control_mute_validation_global_counter, vkd3d_memory_order_relaxed);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vkd3d_debug_control_UnmuteValidationGlobal(IVKD3DDebugControlInterface *iface)
{
    (void)iface;
    vkd3d_atomic_uint32_decrement(&vkd3d_debug_control_mute_validation_global_counter, vkd3d_memory_order_relaxed);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vkd3d_debug_control_MuteValidationMessageID(
        IVKD3DDebugControlInterface *iface, const char *vuid, const char *explanation)
{
    struct vkd3d_debug_control_muted_vuid *entry;
    HRESULT hr = S_OK;
    unsigned int i;
    (void)iface;

    pthread_mutex_lock(&vkd3d_debug_control_lock);

    for (i = 0; i < vkd3d_debug_control_muted_vuid_count; i++)
    {
        entry = &vkd3d_debug_control_muted_vuids[i];

        /* Ignore duplicates, it is possible for apps to initialize d3d12
         * multiple times while keeping d3d12core loaded. */
        if (!strncmp(entry->vuid, vuid, ARRAY_SIZE(entry->vuid)))
        {
            hr = S_FALSE;
            goto out;
        }
    }

    if (vkd3d_debug_control_muted_vuid_count == ARRAY_SIZE(vkd3d_debug_control_muted_vuids))
    {
        hr = E_OUTOFMEMORY;
        goto out;
    }

    entry = &vkd3d_debug_control_muted_vuids[vkd3d_debug_control_muted_vuid_count++];
    strncpy(entry->vuid, vuid, ARRAY_SIZE(entry->vuid) - 1u);

    if (explanation)
        strncpy(entry->explanation, explanation, ARRAY_SIZE(entry->explanation) - 1u);

out:
    pthread_mutex_unlock(&vkd3d_debug_control_lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE vkd3d_debug_control_UnmuteValidationMessageID(
        IVKD3DDebugControlInterface *iface, const char *vuid)
{
    struct vkd3d_debug_control_muted_vuid *entry, *last;
    unsigned int i;
    (void)iface;

    pthread_mutex_lock(&vkd3d_debug_control_lock);

    for (i = 0; i < vkd3d_debug_control_muted_vuid_count; i++)
    {
        entry = &vkd3d_debug_control_muted_vuids[i];

        if (strncmp(entry->vuid, vuid, ARRAY_SIZE(entry->vuid)) == 0)
        {
            last = &vkd3d_debug_control_muted_vuids[--vkd3d_debug_control_muted_vuid_count];

            memcpy(entry, last, sizeof(*last));
            memset(last, 0, sizeof(*last));

            pthread_mutex_unlock(&vkd3d_debug_control_lock);
            return S_OK;
        }
    }

    pthread_mutex_unlock(&vkd3d_debug_control_lock);
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE vkd3d_debug_control_SetOutOfSpecTestBehavior(
        IVKD3DDebugControlInterface *iface, VKD3D_DEBUG_CONTROL_OUT_OF_SPEC_BEHAVIOR behavior, BOOL enable)
{
    (void)iface;

    if (behavior < VKD3D_DEBUG_CONTROL_OUT_OF_SPEC_BEHAVIOR_COUNT)
    {
        vkd3d_atomic_uint32_store_explicit(
                &vkd3d_debug_control_out_of_spec_behavior[behavior], enable,
                vkd3d_memory_order_relaxed);
        return S_OK;
    }
    else
    {
        return E_INVALIDARG;
    }
}

static HRESULT STDMETHODCALLTYPE vkd3d_debug_control_SetBehaviorFlags(
        IVKD3DDebugControlInterface *iface, VKD3D_DEBUG_CONTROL_BEHAVIOR_FLAGS behavior)
{
    (void)iface;
    vkd3d_atomic_uint32_store_explicit(&vkd3d_debug_control_behavior_flags, behavior, vkd3d_memory_order_relaxed);
    return S_OK;
}

static CONST_VTBL struct IVKD3DDebugControlInterfaceVtbl vkd3d_debug_control_vtbl =
{
    vkd3d_debug_control_SetRunningUnderTest,
    vkd3d_debug_control_SetExplodeOnValidationError,
    vkd3d_debug_control_MuteValidationGlobal,
    vkd3d_debug_control_UnmuteValidationGlobal,
    vkd3d_debug_control_MuteValidationMessageID,
    vkd3d_debug_control_UnmuteValidationMessageID,
    vkd3d_debug_control_SetOutOfSpecTestBehavior,
    vkd3d_debug_control_SetBehaviorFlags,
};

static const struct IVKD3DDebugControlInterface vkd3d_debug_control_instance =
{
    .lpVtbl = &vkd3d_debug_control_vtbl,
};

static const d3d12core_interface d3d12core_interface_instance =
{
    .lpVtbl = &d3d12core_interface_vtbl,
};

struct d3d12_device_factory
{
    ID3D12DeviceFactory ID3D12DeviceFactory_iface;
    ID3D12DeviceConfiguration1 ID3D12DeviceConfiguration1_iface;
    LONG refcount;
    D3D12_DEVICE_FACTORY_FLAGS flags;
};

static struct d3d12_device_factory *impl_from_ID3D12DeviceFactory(ID3D12DeviceFactory *iface)
{
    if (!iface)
        return NULL;
    return CONTAINING_RECORD(iface, struct d3d12_device_factory, ID3D12DeviceFactory_iface);
}

static struct d3d12_device_factory *impl_from_ID3D12DeviceConfiguration1(ID3D12DeviceConfiguration1 *iface)
{
    if (!iface)
        return NULL;
    return CONTAINING_RECORD(iface, struct d3d12_device_factory, ID3D12DeviceConfiguration1_iface);
}

static ULONG STDMETHODCALLTYPE d3d12_device_factory_AddRef(ID3D12DeviceFactory *iface)
{
    struct d3d12_device_factory *factory = impl_from_ID3D12DeviceFactory(iface);
    TRACE("iface %p\n", iface);
    return InterlockedIncrement(&factory->refcount);
}

static ULONG STDMETHODCALLTYPE d3d12_device_factory_Release(ID3D12DeviceFactory *iface)
{
    struct d3d12_device_factory *factory = impl_from_ID3D12DeviceFactory(iface);
    ULONG refcount;
    TRACE("iface %p\n", iface);
    refcount = InterlockedDecrement(&factory->refcount);
    if (refcount == 0)
        vkd3d_free(factory);
    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_factory_QueryInterface(ID3D12DeviceFactory *iface,
        REFIID iid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(iid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID_ID3D12DeviceFactory))
    {
        d3d12_device_factory_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ID3D12DeviceConfiguration) ||
        IsEqualGUID(iid, &IID_ID3D12DeviceConfiguration1))
    {
        struct d3d12_device_factory *factory = impl_from_ID3D12DeviceFactory(iface);
        d3d12_device_factory_AddRef(iface);
        *object = &factory->ID3D12DeviceConfiguration1_iface;
        return S_OK;
    }

    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_factory_InitializeFromGlobalState(ID3D12DeviceFactory *iface)
{
    FIXME("iface %p stub!\n", iface);
    /* This is rather murky since flags don't apply, only things like debug options and stuff
     * which we don't support anyway. */
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_factory_ApplyToGlobalState(ID3D12DeviceFactory *iface)
{
    FIXME("iface %p stub!\n", iface);
    /* This is rather murky since flags don't apply, only things like debug options and stuff
     * which we don't support anyway. */
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_factory_SetFlags(ID3D12DeviceFactory *iface, D3D12_DEVICE_FACTORY_FLAGS flags)
{
    struct d3d12_device_factory *factory = impl_from_ID3D12DeviceFactory(iface);
    TRACE("iface %p, flags #%x\n", iface, flags);
    factory->flags = flags;
    return S_OK;
}

static D3D12_DEVICE_FACTORY_FLAGS STDMETHODCALLTYPE d3d12_device_factory_GetFlags(ID3D12DeviceFactory *iface)
{
    struct d3d12_device_factory *factory = impl_from_ID3D12DeviceFactory(iface);
    TRACE("iface %p\n", iface);
    return factory->flags;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_factory_GetConfigurationInterface(ID3D12DeviceFactory *iface,
        REFCLSID clsid, REFIID iid, void **ppv)
{
    HRESULT hr;
    TRACE("iface %p, clsid %s, iid %s, ppv %p!\n", iface, debugstr_guid(clsid), debugstr_guid(iid), ppv);

    /* GetConfigurationInterface says only these classes are exposed by this particular interface.
     * It's supposed to be equivalent to D3D12GetInterface more or less. */
    if (IsEqualGUID(clsid, &CLSID_D3D12Debug) ||
        IsEqualGUID(clsid, &CLSID_D3D12DeviceRemovedExtendedData) ||
        IsEqualGUID(clsid, &CLSID_D3D12Tools))
    {
        /* GetDebugInterface and S_FALSE behavior is murky since there is no clsid we can key off of.
         * Anything we expose via GetInterface would work without hackery.
         * DRED is a stubbed out special case for now. */
        if (!ppv && IsEqualGUID(clsid, &CLSID_D3D12DeviceRemovedExtendedData))
            return S_FALSE;

        /* If one of the global interfaces understand the interface, go with that. */
        if (SUCCEEDED(hr = d3d12core_GetInterface(NULL, clsid, iid, ppv)))
            return hr;
        if (SUCCEEDED(hr = d3d12core_GetDebugInterface(NULL, iid, ppv)))
            return hr;
    }

    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_factory_EnableExperimentalFeatures(ID3D12DeviceFactory *iface,
        UINT NumFeatures, const IID *pIIDs, void *pConfigurationStructs, UINT *pConfigurationStructSizes)
{
    TRACE("iface %p\n", iface);
    return d3d12core_EnableExperimentalFeatures(NULL, NumFeatures, pIIDs, pConfigurationStructs, pConfigurationStructSizes);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_factory_CreateDevice(ID3D12DeviceFactory *iface,
        IUnknown *adapter, D3D_FEATURE_LEVEL FeatureLevel, REFIID iid, void **ppvDevice)
{
    TRACE("iface %p, adapter %p, FeatureLevel %u, iid %s, ppvDevice %p\n", iface,
        adapter, FeatureLevel, debugstr_guid(iid), ppvDevice);
    return d3d12core_CreateDeviceFromFactory(adapter, FeatureLevel, iface, iid, ppvDevice);
}

static CONST_VTBL ID3D12DeviceFactoryVtbl d3d12_device_factory_vtbl =
{
    /* IUnknown methods */
    d3d12_device_factory_QueryInterface,
    d3d12_device_factory_AddRef,
    d3d12_device_factory_Release,
    /* ID3D12DeviceFactory methods */
    d3d12_device_factory_InitializeFromGlobalState,
    d3d12_device_factory_ApplyToGlobalState,
    d3d12_device_factory_SetFlags,
    d3d12_device_factory_GetFlags,
    d3d12_device_factory_GetConfigurationInterface,
    d3d12_device_factory_EnableExperimentalFeatures,
    d3d12_device_factory_CreateDevice,
};

static ULONG STDMETHODCALLTYPE d3d12_device_configuration_AddRef(ID3D12DeviceConfiguration1 *iface)
{
    struct d3d12_device_factory *factory = impl_from_ID3D12DeviceConfiguration1(iface);
    return d3d12_device_factory_AddRef(&factory->ID3D12DeviceFactory_iface);
}

static ULONG STDMETHODCALLTYPE d3d12_device_configuration_Release(ID3D12DeviceConfiguration1 *iface)
{
    struct d3d12_device_factory *factory = impl_from_ID3D12DeviceConfiguration1(iface);
    return d3d12_device_factory_Release(&factory->ID3D12DeviceFactory_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_configuration_QueryInterface(ID3D12DeviceConfiguration1 *iface,
        REFIID iid, void **object)
{
    struct d3d12_device_factory *factory = impl_from_ID3D12DeviceConfiguration1(iface);
    return d3d12_device_factory_QueryInterface(&factory->ID3D12DeviceFactory_iface, iid, object);
}

static D3D12_DEVICE_CONFIGURATION_DESC * STDMETHODCALLTYPE d3d12_device_configuration_GetDesc(
        ID3D12DeviceConfiguration1 *iface, D3D12_DEVICE_CONFIGURATION_DESC *desc)
{
    TRACE("iface %p\n", iface);
    memset(desc, 0, sizeof(*desc));
    desc->SDKVersion = D3D12_SDK_VERSION;
    return desc;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_configuration_GetEnabledExperimentalFeatures(
        ID3D12DeviceConfiguration1 *iface, GUID *pGuids, UINT NumGuids)
{
    FIXME("iface %p, pGuids %p, NumGuids %u stub!\n", iface, pGuids, NumGuids);
    /* The spec doesn't say how this is supposed to work. We don't support experimental features anyway. */
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_configuration_SerializeVersionedRootSignature(
        ID3D12DeviceConfiguration1 *iface, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *pDesc,
        ID3DBlob **ppResult, ID3DBlob **ppError)
{
    TRACE("iface %p, pDesc %p, ppResult %p, ppError %p\n", iface, pDesc, ppResult, ppError);
    return vkd3d_serialize_versioned_root_signature(pDesc, ppResult, ppError);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_configuration_CreateVersionedRootSignatureDeserializer(
        ID3D12DeviceConfiguration1 *iface, const void *pBlob, SIZE_T Size,
        REFIID iid, void **ppvDeserializer)
{
    TRACE("iface %p, pBlob %p, Size %zu, iid %s, ppvDeserializer %p\n",
        iface, pBlob, Size, debugstr_guid(iid), ppvDeserializer);
    return vkd3d_create_versioned_root_signature_deserializer(pBlob, Size, iid, ppvDeserializer);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_configuration_CreateVersionedRootSignatureDeserializerFromSubobjectInLibrary(
        ID3D12DeviceConfiguration1 *iface, const void *pLibraryBlob, SIZE_T Size,
        LPCWSTR RootSignatureSubobjectName, REFIID iid, void **ppvDeserializer)
{
    FIXME("iface %p, pLibraryBlob %p, Size %zu, RootSignatureSubobjectName %s, iid %s, ppvDeserializer %p stub!\n",
        iface, pLibraryBlob, Size, debugstr_w(RootSignatureSubobjectName), debugstr_guid(iid), ppvDeserializer);
    return E_NOTIMPL;
}

static CONST_VTBL ID3D12DeviceConfiguration1Vtbl d3d12_device_configuration_vtbl =
{
    /* IUnknown methods */
    d3d12_device_configuration_QueryInterface,
    d3d12_device_configuration_AddRef,
    d3d12_device_configuration_Release,
    /* ID3D12DeviceConfiguration methods */
    d3d12_device_configuration_GetDesc,
    d3d12_device_configuration_GetEnabledExperimentalFeatures,
    d3d12_device_configuration_SerializeVersionedRootSignature,
    d3d12_device_configuration_CreateVersionedRootSignatureDeserializer,
    /* ID3D12DeviceConfiguration1 methods */
    d3d12_device_configuration_CreateVersionedRootSignatureDeserializerFromSubobjectInLibrary,
};

static ID3D12DeviceFactory *create_device_factory(void)
{
    struct d3d12_device_factory *factory = vkd3d_calloc(1, sizeof(*factory));
    if (!factory)
        return NULL;

    factory->refcount = 1;
    factory->ID3D12DeviceFactory_iface.lpVtbl = &d3d12_device_factory_vtbl;
    factory->ID3D12DeviceConfiguration1_iface.lpVtbl = &d3d12_device_configuration_vtbl;
    return &factory->ID3D12DeviceFactory_iface;
}

static HRESULT d3d12core_D3D12GetInterface(REFCLSID rcslid, REFIID iid, void **debug)
{
    TRACE("rcslid %s iid %s, debug %p.\n", debugstr_guid(rcslid), debugstr_guid(iid), debug);

    if (IsEqualGUID(rcslid, &CLSID_D3D12DeviceFactory))
    {
        ID3D12DeviceFactory *factory;
        HRESULT hr;

        if (!debug)
            return S_FALSE;

        factory = create_device_factory();
        if (!factory)
            return E_OUTOFMEMORY;

        hr = ID3D12DeviceFactory_QueryInterface(factory, iid, debug);
        ID3D12DeviceFactory_Release(factory);
        return hr;
    }

    if (IsEqualGUID(rcslid, &CLSID_VKD3DCore))
    {
        if (!debug)
            return S_FALSE;

        if (IsEqualGUID(iid, &IID_IVKD3DCoreInterface))
        {
            *debug = (void*)&d3d12core_interface_instance;
            return S_OK;
        }
    }

    if (IsEqualGUID(rcslid, &CLSID_VKD3DDebugControl))
    {
        if (!debug)
            return S_FALSE;

        if (IsEqualGUID(iid, &IID_IVKD3DDebugControlInterface))
        {
            *debug = (void*)&vkd3d_debug_control_instance;
            return S_OK;
        }
    }

    return E_NOINTERFACE;
}

HRESULT WINAPI DLLEXPORT D3D12GetInterface(REFCLSID rcslid, REFIID iid, void **debug)
{
    return d3d12core_D3D12GetInterface(rcslid, iid, debug);
}

/* Just expose the latest stable AgilitySDK version.
 * This is actually exported as a UINT and not a function it seems. */
DLLEXPORT const UINT D3D12SDKVersion = D3D12_SDK_VERSION;
