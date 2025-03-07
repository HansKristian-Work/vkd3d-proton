/*
 * Copyright 2016-2018 Józef Kucia for CodeWeavers
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

#ifndef __VKD3D_D3D12_CROSSTEST_H
#define __VKD3D_D3D12_CROSSTEST_H

#ifdef _MSC_VER
/* Used for M_PI */
#define _USE_MATH_DEFINES
#endif

#ifdef _WIN32
# include <vkd3d_win32.h>
#endif

#define COBJMACROS
#include "vkd3d_test.h"
#include "vkd3d_windows.h"
#define WIDL_C_INLINE_WRAPPERS
#include "vkd3d_d3d12.h"
#include "vkd3d_device_vkd3d_ext.h"
#include "vkd3d_d3d12sdklayers.h"
#undef WIDL_C_INLINE_WRAPPERS

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <time.h>

#if !defined(_WIN32) || defined(VKD3D_FORCE_UTILS_WRAPPER)
# include "vkd3d_threads.h"
# include "vkd3d.h"
#endif

#include "vkd3d_sonames.h"

#if !defined(_WIN32)
#include <dlfcn.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#endif

#include "d3d12_test_utils.h"

extern PFN_D3D12_CREATE_DEVICE pfn_D3D12CreateDevice;
extern PFN_D3D12_ENABLE_EXPERIMENTAL_FEATURES pfn_D3D12EnableExperimentalFeatures;
extern PFN_D3D12_GET_DEBUG_INTERFACE pfn_D3D12GetDebugInterface;
extern PFN_D3D12_GET_INTERFACE pfn_D3D12GetInterface;
extern PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER pfn_D3D12CreateVersionedRootSignatureDeserializer;
extern PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE pfn_D3D12SerializeVersionedRootSignature;
extern bool use_warp_device;
extern unsigned int use_adapter_idx;

#if defined(_WIN32) && !defined(VKD3D_FORCE_UTILS_WRAPPER)
#define get_d3d12_pfn(name) get_d3d12_pfn_(#name)
static inline void *get_d3d12_pfn_(const char *name)
{
    static HMODULE d3d12_module;
    if (!d3d12_module)
        d3d12_module = LoadLibraryA("d3d12.dll");
    return GetProcAddress(d3d12_module, name);
}
#else
#define get_d3d12_pfn(name) (name)
#endif

#if defined(_WIN32)
static inline HANDLE create_event(void)
{
    return CreateEventA(NULL, FALSE, FALSE, NULL);
}

static inline unsigned int wait_event(HANDLE event, unsigned int milliseconds)
{
    return WaitForSingleObject(event, milliseconds);
}

static inline void signal_event(HANDLE event)
{
    SetEvent(event);
}

static inline void destroy_event(HANDLE event)
{
    CloseHandle(event);
}
#else
#define INFINITE INT_MAX
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 1
#define WAIT_TIMEOUT 1

static inline HANDLE create_event(void)
{
    HANDLE handle;
    int fd;

    fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (fd < 0)
        return NULL;

    /* No way this should happen unless stdin is closed for some reason ...
     * When casting to a HANDLE null will be considered no handle. */
    if (fd == 0)
    {
        fd = dup(0);
        close(0);
    }

    handle = (HANDLE)(intptr_t)fd;
    return handle;
}

static inline unsigned int wait_event(HANDLE event, unsigned int milliseconds)
{
    int fd = (int)(intptr_t)event;
    struct pollfd pfd;
    uint64_t dummy;
    int timeout;

    TRACE("event %p, milliseconds %u.\n", event, milliseconds);

    pfd.events = POLLIN;
    pfd.fd = fd;

    timeout = milliseconds == INFINITE ? -1 : (int)milliseconds;

    for (;;)
    {
        if (poll(&pfd, 1, timeout) <= 0)
            return WAIT_TIMEOUT;

        /* Non-blocking reads, if there are two racing threads that wait on a Win32 event,
         * only one will succeed when auto-reset events are used. */
        if (read(fd, &dummy, sizeof(dummy)) > 0)
            return WAIT_OBJECT_0;
        else if (timeout != INFINITE)
            return WAIT_TIMEOUT;
    }
}

static inline void signal_event(HANDLE event)
{
    int fd = (int)(intptr_t)event;
    const uint64_t value = 1;
    write(fd, &value, sizeof(value));
}

static inline void destroy_event(HANDLE event)
{
    close((int)(intptr_t)event);
}
#endif

#if defined(_WIN32)
static inline void vkd3d_sleep(unsigned int ms)
{
    Sleep(ms);
}

#else
static inline void vkd3d_sleep(unsigned int ms)
{
    usleep(1000 * ms);
}
#endif

typedef void (*thread_main_pfn)(void *data);

struct test_thread_data
{
    thread_main_pfn main_pfn;
    void *user_data;
};

#if defined(_WIN32) && !defined(VKD3D_FORCE_UTILS_WRAPPER)
static inline DWORD WINAPI test_thread_main(void *untyped_data)
{
    struct test_thread_data *data = untyped_data;
    data->main_pfn(data->user_data);
    free(untyped_data);
    return 0;
}

static inline HANDLE create_thread(thread_main_pfn main_pfn, void *user_data)
{
    struct test_thread_data *data;

    if (!(data = malloc(sizeof(*data))))
        return NULL;
    data->main_pfn = main_pfn;
    data->user_data = user_data;

    return CreateThread(NULL, 0, test_thread_main, data, 0, NULL);
}

static inline bool join_thread(HANDLE thread)
{
    unsigned int ret;

    ret = WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return ret == WAIT_OBJECT_0;
}
#else
static void *test_thread_main(void *untyped_data)
{
    struct test_thread_data *data = untyped_data;
    data->main_pfn(data->user_data);
    free(untyped_data);
    return NULL;
}

static inline HANDLE create_thread(thread_main_pfn main_pfn, void *user_data)
{
    struct test_thread_data *data;
    pthread_t *thread;

    if (!(thread = malloc(sizeof(*thread))))
        return NULL;

    if (!(data = malloc(sizeof(*data))))
    {
        free(thread);
        return NULL;
    }
    data->main_pfn = main_pfn;
    data->user_data = user_data;

    if (pthread_create(thread, NULL, test_thread_main, data))
    {
        free(data);
        free(thread);
        return NULL;
    }

    return thread;
}

static inline bool join_thread(HANDLE untyped_thread)
{
    pthread_t *thread = untyped_thread;
    int rc;

    rc = pthread_join(*thread, NULL);
    free(thread);
    return !rc;
}
#endif

extern D3D_FEATURE_LEVEL vkd3d_device_feature_level;
static inline void enable_feature_level_override(int argc, char **argv)
{
    static const struct
    {
        D3D_FEATURE_LEVEL level;
        const char *tag;
    } level_map[] = {
        { D3D_FEATURE_LEVEL_11_0, "11_0" },
        { D3D_FEATURE_LEVEL_11_1, "11_1" },
        { D3D_FEATURE_LEVEL_12_0, "12_0" },
        { D3D_FEATURE_LEVEL_12_1, "12_1" },
        { D3D_FEATURE_LEVEL_12_2, "12_2" },
    };

    const char *level = NULL;
    int i;

    for (i = 1; i + 1 < argc; ++i)
    {
        if (!strcmp(argv[i], "--feature-level"))
        {
            level = argv[i + 1];
            break;
        }
    }

    vkd3d_device_feature_level = D3D_FEATURE_LEVEL_11_0;
    if (level)
    {
        for (i = 0; i < (int)ARRAY_SIZE(level_map); i++)
        {
            if (!strcmp(level_map[i].tag, level))
            {
                INFO("Overriding feature level %s.\n", level);
                vkd3d_device_feature_level = level_map[i].level;
                break;
            }
        }
    }
}

static HRESULT wait_for_fence(ID3D12Fence *fence, uint64_t value)
{
    unsigned int ret;
    HANDLE event;
    HRESULT hr;

    if (ID3D12Fence_GetCompletedValue(fence) >= value)
        return S_OK;

    if (!(event = create_event()))
        return E_FAIL;

    if (FAILED(hr = ID3D12Fence_SetEventOnCompletion(fence, value, event)))
    {
        destroy_event(event);
        return hr;
    }

    ret = wait_event(event, INFINITE);
    destroy_event(event);
    return ret == WAIT_OBJECT_0 ? S_OK : E_FAIL;
}

static HRESULT wait_for_fence_no_event(ID3D12Fence *fence, uint64_t value)
{
    if (ID3D12Fence_GetCompletedValue(fence) >= value)
        return S_OK;

    /* This is defined to block on the value with infinite timeout. */
    return ID3D12Fence_SetEventOnCompletion(fence, value, NULL);
}

static void wait_queue_idle_(unsigned int line, ID3D12Device *device, ID3D12CommandQueue *queue)
{
    ID3D12Fence *fence;
    HRESULT hr;

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
            &IID_ID3D12Fence, (void **)&fence);
    assert_that_(line)(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);

    hr = ID3D12CommandQueue_Signal(queue, fence, 1);
    assert_that_(line)(hr == S_OK, "Failed to signal fence, hr %#x.\n", hr);
    hr = wait_for_fence(fence, 1);
    assert_that_(line)(hr == S_OK, "Failed to wait for fence, hr %#x.\n", hr);

    ID3D12Fence_Release(fence);
}

static inline void wait_queue_idle_no_event_(unsigned int line, ID3D12Device *device, ID3D12CommandQueue *queue)
{
    ID3D12Fence *fence;
    HRESULT hr;

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
        &IID_ID3D12Fence, (void **)&fence);
    assert_that_(line)(hr == S_OK, "Failed to create fence, hr %#x.\n", hr);

    hr = ID3D12CommandQueue_Signal(queue, fence, 1);
    assert_that_(line)(hr == S_OK, "Failed to signal fence, hr %#x.\n", hr);
    hr = wait_for_fence_no_event(fence, 1);
    assert_that_(line)(hr == S_OK, "Failed to wait for fence, hr %#x.\n", hr);

    ID3D12Fence_Release(fence);
}

static PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr;
static PFN_vkGetDeviceProcAddr pfn_vkGetDeviceProcAddr;
static inline bool init_vulkan_loader(void)
{
#ifdef _WIN32
    HMODULE hmod;
#else
    void *mod;
#endif

    if (pfn_vkGetInstanceProcAddr)
        return true;

    if (pfn_vkGetDeviceProcAddr)
        return true;

#ifdef _WIN32
    hmod = LoadLibraryA(SONAME_LIBVULKAN);
    if (!hmod)
        return false;

    pfn_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(hmod, "vkGetInstanceProcAddr");
    pfn_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)(void *)GetProcAddress(hmod, "vkGetDeviceProcAddr");
#else
    mod = dlopen(SONAME_LIBVULKAN, RTLD_LAZY);
    if (!mod)
        return false;

    pfn_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(mod, "vkGetInstanceProcAddr");
    pfn_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)dlsym(mod, "vkGetDeviceProcAddr");
#endif

    return pfn_vkGetInstanceProcAddr != NULL;
}

#if defined(_WIN32) && !defined(VKD3D_FORCE_UTILS_WRAPPER)
static IUnknown *create_warp_adapter(IDXGIFactory4 *factory)
{
    IUnknown *adapter;
    HRESULT hr;

    adapter = NULL;
    hr = IDXGIFactory4_EnumWarpAdapter(factory, &IID_IUnknown, (void **)&adapter);
    if (FAILED(hr))
        trace("Failed to get WARP adapter, hr %#x.\n", hr);
    return adapter;
}

static IUnknown *create_adapter(void)
{
    IUnknown *adapter = NULL;
    IDXGIFactory4 *factory;
    HRESULT hr;

    hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory);
    ok(hr == S_OK, "Failed to create IDXGIFactory4, hr %#x.\n", hr);

    if (use_warp_device && (adapter = create_warp_adapter(factory)))
    {
        IDXGIFactory4_Release(factory);
        return adapter;
    }

    hr = IDXGIFactory4_EnumAdapters(factory, use_adapter_idx, (IDXGIAdapter **)&adapter);
    IDXGIFactory4_Release(factory);
    if (FAILED(hr))
        trace("Failed to get adapter, hr %#x.\n", hr);
    return adapter;
}

static ID3D12Device *create_device(void)
{
    IUnknown *adapter = NULL;
    ID3D12Device *device;
    HRESULT hr;

    if ((use_warp_device || use_adapter_idx) && !(adapter = create_adapter()))
    {
        trace("Failed to create adapter.\n");
        return NULL;
    }

    /* Enable support for 6_3+, we need this for some tests. */
    if (pfn_D3D12EnableExperimentalFeatures)
        pfn_D3D12EnableExperimentalFeatures(1, &D3D12ExperimentalShaderModels, NULL, NULL);

    hr = pfn_D3D12CreateDevice(adapter, vkd3d_device_feature_level, &IID_ID3D12Device, (void **)&device);
    if (adapter)
        IUnknown_Release(adapter);

    return SUCCEEDED(hr) ? device : NULL;
}

static inline void init_adapter_info(void)
{
    char name[MEMBER_SIZE(DXGI_ADAPTER_DESC, Description)];
    IDXGIAdapter *dxgi_adapter;
    DXGI_ADAPTER_DESC desc;
    IUnknown *adapter;
    unsigned int i;
    HRESULT hr;

    if (!(adapter = create_adapter()))
        return;

    hr = IUnknown_QueryInterface(adapter, &IID_IDXGIAdapter, (void **)&dxgi_adapter);
    ok(hr == S_OK, "Failed to query IDXGIAdapter, hr %#x.\n", hr);
    IUnknown_Release(adapter);

    hr = IDXGIAdapter_GetDesc(dxgi_adapter, &desc);
    ok(hr == S_OK, "Failed to get adapter desc, hr %#x.\n", hr);

    /* FIXME: Use debugstr_w(). */
    for (i = 0; i < ARRAY_SIZE(desc.Description) && isprint(desc.Description[i]); ++i)
        name[i] = desc.Description[i];
    name[min(i, ARRAY_SIZE(name) - 1)] = '\0';

    trace("Adapter: %s, %04x:%04x.\n", name, desc.VendorId, desc.DeviceId);

    if (desc.VendorId == 0x1414 && desc.DeviceId == 0x008c)
    {
        trace("Using WARP device.\n");
        use_warp_device = true;
    }

    IDXGIAdapter_Release(dxgi_adapter);
}

static inline bool get_adapter_desc(ID3D12Device *device, DXGI_ADAPTER_DESC *desc)
{
    IDXGIFactory4 *factory;
    IDXGIAdapter *adapter;
    HRESULT hr;
    LUID luid;

    memset(desc, 0, sizeof(*desc));

    if (!vkd3d_test_platform_is_windows())
        return false;

    luid = ID3D12Device_GetAdapterLuid(device);

    hr = CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&factory);
    ok(hr == S_OK, "Failed to create IDXGIFactory4, hr %#x.\n", hr);

    hr = IDXGIFactory4_EnumAdapterByLuid(factory, luid, &IID_IDXGIAdapter, (void **)&adapter);
    if (SUCCEEDED(hr))
    {
        hr = IDXGIAdapter_GetDesc(adapter, desc);
        ok(hr == S_OK, "Failed to get adapter desc, hr %#x.\n", hr);
        IDXGIAdapter_Release(adapter);
    }

    IDXGIFactory4_Release(factory);
    return SUCCEEDED(hr);
}

static inline bool is_amd_windows_device(ID3D12Device *device)
{
    DXGI_ADAPTER_DESC desc;

    return get_adapter_desc(device, &desc) && desc.VendorId == 0x1002;
}

static inline bool is_intel_windows_device(ID3D12Device *device)
{
    DXGI_ADAPTER_DESC desc;

    return get_adapter_desc(device, &desc) && desc.VendorId == 0x8086;
}

static inline bool is_nvidia_windows_device(ID3D12Device *device)
{
    DXGI_ADAPTER_DESC desc;

    return get_adapter_desc(device, &desc) && desc.VendorId == 0x10de;
}

static inline bool is_mesa_device(ID3D12Device *device)
{
    return false;
}

static inline bool is_mesa_intel_device(ID3D12Device *device)
{
    return false;
}

static inline bool is_nvidia_device(ID3D12Device *device)
{
    return false;
}

static inline bool is_radv_device(ID3D12Device *device)
{
    return false;
}

static inline bool is_amd_vulkan_device(ID3D12Device *device)
{
    return false;
}

static inline bool is_adreno_device(ID3D12Device *device)
{
    return false;
}

static inline bool is_vk_device_extension_supported(ID3D12Device *device, const char *ext)
{
    return false;
}
#else

static ID3D12Device *create_device(void)
{
    ID3D12Device *device;
    HRESULT hr;
    hr = D3D12CreateDevice(NULL, vkd3d_device_feature_level, &IID_ID3D12Device, (void **)&device);
    return SUCCEEDED(hr) ? device : NULL;
}

static bool get_driver_properties(ID3D12Device *device, VkPhysicalDeviceDriverPropertiesKHR *driver_properties)
{
    PFN_vkGetPhysicalDeviceProperties2 pfn_vkGetPhysicalDeviceProperties2;
    VkPhysicalDeviceProperties2 device_properties2;
    VkPhysicalDevice vk_physical_device;
    VkInstance vk_instance;
    ID3D12DeviceExt *ext;
    VkDevice vk_device;

    if (!init_vulkan_loader())
        return false;

    if (FAILED(ID3D12Device_QueryInterface(device, &IID_ID3D12DeviceExt, (void **)&ext)))
        return false;

    ID3D12DeviceExt_GetVulkanHandles(ext, &vk_instance, &vk_physical_device, &vk_device);
	ID3D12DeviceExt_Release(ext);

    memset(driver_properties, 0, sizeof(*driver_properties));
    driver_properties->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR;

    pfn_vkGetPhysicalDeviceProperties2
        = (void *)pfn_vkGetInstanceProcAddr(vk_instance, "vkGetPhysicalDeviceProperties2");
    ok(pfn_vkGetPhysicalDeviceProperties2, "vkGetPhysicalDeviceProperties2 is NULL.\n");

    memset(&device_properties2, 0, sizeof(device_properties2));
    device_properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    device_properties2.pNext = driver_properties;
    pfn_vkGetPhysicalDeviceProperties2(vk_physical_device, &device_properties2);
    return true;
}

static inline bool is_vk_device_extension_supported(ID3D12Device *device, const char *ext)
{
    ID3D12DXVKInteropDevice *dxvk_device = NULL;
    const char **exts = NULL;
    bool supported = false;
    UINT extension_count;
    unsigned int i;

    if (!init_vulkan_loader())
        return false;

    if (FAILED(ID3D12Device_QueryInterface(device, &IID_ID3D12DXVKInteropDevice, (void **)&dxvk_device)))
        goto err;

    if (FAILED(ID3D12DXVKInteropDevice_GetDeviceExtensions(dxvk_device, &extension_count, NULL)))
        goto err;

    exts = malloc(sizeof(*exts) * extension_count);
    if (!exts)
        goto err;

    if (FAILED(ID3D12DXVKInteropDevice_GetDeviceExtensions(dxvk_device, &extension_count, exts)))
        goto err;

    for (i = 0; i < extension_count; i++)
    {
        if (!strcmp(ext, exts[i]))
        {
            supported = true;
            break;
        }
    }

err:
    if (dxvk_device)
        ID3D12DXVKInteropDevice_Release(dxvk_device);
    free(exts);
    return supported;
}

static inline void init_adapter_info(void)
{
    VkPhysicalDeviceDriverPropertiesKHR driver_properties;
    ID3D12Device *device;

    if ((device = create_device()))
    {
        if (get_driver_properties(device, &driver_properties))
            trace("Driver name: %s, driver info: %s.\n", driver_properties.driverName, driver_properties.driverInfo);
        ID3D12Device_Release(device);
    }
}

static inline bool is_amd_windows_device(ID3D12Device *device)
{
    return false;
}

static inline bool is_intel_windows_device(ID3D12Device *device)
{
    return false;
}

static inline bool is_nvidia_windows_device(ID3D12Device *device)
{
    return false;
}

static inline bool is_mesa_device(ID3D12Device *device)
{
    VkPhysicalDeviceDriverPropertiesKHR properties;

    get_driver_properties(device, &properties);
    return properties.driverID == VK_DRIVER_ID_MESA_RADV_KHR
            || properties.driverID == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA_KHR;
}

static inline bool is_mesa_intel_device(ID3D12Device *device)
{
    VkPhysicalDeviceDriverPropertiesKHR properties;

    get_driver_properties(device, &properties);
    return properties.driverID == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA_KHR;
}

static inline bool is_nvidia_device(ID3D12Device *device)
{
    VkPhysicalDeviceDriverPropertiesKHR properties;

    get_driver_properties(device, &properties);
    return properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY_KHR;
}

static inline bool is_radv_device(ID3D12Device *device)
{
    VkPhysicalDeviceDriverPropertiesKHR properties;

    get_driver_properties(device, &properties);
    return properties.driverID == VK_DRIVER_ID_MESA_RADV_KHR;
}

static inline bool is_amd_vulkan_device(ID3D12Device *device)
{
    VkPhysicalDeviceDriverPropertiesKHR properties;

    get_driver_properties(device, &properties);
    return properties.driverID == VK_DRIVER_ID_MESA_RADV_KHR ||
            properties.driverID == VK_DRIVER_ID_AMD_OPEN_SOURCE_KHR ||
            properties.driverID == VK_DRIVER_ID_AMD_PROPRIETARY;
}

static inline bool is_adreno_device(ID3D12Device *device)
{
    VkPhysicalDeviceDriverPropertiesKHR properties;

    get_driver_properties(device, &properties);
    return properties.driverID == VK_DRIVER_ID_QUALCOMM_PROPRIETARY ||
            properties.driverID == VK_DRIVER_ID_MESA_TURNIP;
}
#endif

static inline void parse_args(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--warp"))
            use_warp_device = true;
        else if (!strcmp(argv[i], "--adapter") && i + 1 < argc)
            use_adapter_idx = atoi(argv[++i]);
    }
}

static inline void enable_d3d12_debug_layer(int argc, char **argv)
{
    bool enable_debug_layer = false, enable_gpu_based_validation = false;
    ID3D12Debug1 *debug1;
    ID3D12Debug *debug;
    int i;

    for (i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--validate"))
            enable_debug_layer = true;
        else if (!strcmp(argv[i], "--gbv"))
            enable_gpu_based_validation = true;
    }

    if (enable_gpu_based_validation)
    {
        if (SUCCEEDED(pfn_D3D12GetDebugInterface(&IID_ID3D12Debug1, (void **)&debug1)))
        {
            ID3D12Debug1_SetEnableGPUBasedValidation(debug1, true);
            ID3D12Debug1_Release(debug1);
            enable_debug_layer = true;
        }
        else
        {
            trace("Failed to enable GPU-based validation.\n");
        }
    }

    if (enable_debug_layer && SUCCEEDED(pfn_D3D12GetDebugInterface(&IID_ID3D12Debug, (void **)&debug)))
    {
        ID3D12Debug_EnableDebugLayer(debug);
        ID3D12Debug_Release(debug);
    }
}

static inline bool device_supports_gpu_upload_heap(ID3D12Device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16;
    HRESULT hr;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS16, &options16, sizeof(options16));
    if (hr != S_OK)
        return false;

    return options16.GPUUploadHeapSupported;
}

static inline void vkd3d_set_running_in_test_suite(void)
{
    IVKD3DDebugControlInterface *dbg = NULL;
    if (!pfn_D3D12GetInterface)
        return;

    if (SUCCEEDED(pfn_D3D12GetInterface(&CLSID_VKD3DDebugControl, &IID_IVKD3DDebugControlInterface, (void**)&dbg)))
    {
        IVKD3DDebugControlInterface_SetRunningUnderTest(dbg);
        if (getenv("VKD3D_TEST_EXPLODE_ON_VVL"))
            IVKD3DDebugControlInterface_SetExplodeOnValidationError(dbg, TRUE);
    }
}

static inline void vkd3d_mute_validation_message(const char *vuid, const char *explanation)
{
    IVKD3DDebugControlInterface *dbg = NULL;
    if (!pfn_D3D12GetInterface)
        return;
    if (SUCCEEDED(pfn_D3D12GetInterface(&CLSID_VKD3DDebugControl, &IID_IVKD3DDebugControlInterface, (void**)&dbg)))
        ok(SUCCEEDED(IVKD3DDebugControlInterface_MuteValidationMessageID(dbg, vuid, explanation)), "Failed to mute validation.\n");
}

static inline void vkd3d_unmute_validation_message(const char *vuid)
{
    IVKD3DDebugControlInterface *dbg = NULL;
    if (!pfn_D3D12GetInterface)
        return;
    if (SUCCEEDED(pfn_D3D12GetInterface(&CLSID_VKD3DDebugControl, &IID_IVKD3DDebugControlInterface, (void**)&dbg)))
        ok(SUCCEEDED(IVKD3DDebugControlInterface_UnmuteValidationMessageID(dbg, vuid)), "Failed to unmute validation.\n");
}

static inline void vkd3d_set_out_of_spec_test_behavior(VKD3D_DEBUG_CONTROL_OUT_OF_SPEC_BEHAVIOR behavior, BOOL enable)
{
    IVKD3DDebugControlInterface *dbg = NULL;
    if (!pfn_D3D12GetInterface)
        return;
    if (SUCCEEDED(pfn_D3D12GetInterface(&CLSID_VKD3DDebugControl, &IID_IVKD3DDebugControlInterface, (void**)&dbg)))
        ok(SUCCEEDED(IVKD3DDebugControlInterface_SetOutOfSpecTestBehavior(dbg, behavior, enable)), "Failed to unmute validation.\n");
}

#endif  /* __VKD3D_D3D12_CROSSTEST_H */
