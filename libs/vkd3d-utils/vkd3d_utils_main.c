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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API

#include "vkd3d_common.h"
#include "vkd3d_utils_private.h"

#ifndef _WIN32
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <unistd.h>
#endif

VKD3D_UTILS_EXPORT HRESULT WINAPI D3D12GetDebugInterface(REFIID iid, void **debug)
{
    FIXME("iid %s, debug %p stub!\n", debugstr_guid(iid), debug);

    return E_NOTIMPL;
}

VKD3D_UTILS_EXPORT HRESULT WINAPI D3D12CreateDevice(IUnknown *adapter,
        D3D_FEATURE_LEVEL minimum_feature_level, REFIID iid, void **device)
{
    struct vkd3d_instance_create_info instance_create_info;
    struct vkd3d_device_create_info device_create_info;

    static const char * const instance_extensions[] =
    {
        VK_KHR_SURFACE_EXTENSION_NAME,
    };
    static const char * const optional_instance_extensions[] =
    {
        "VK_KHR_xcb_surface",
        "VK_MVK_macos_surface",
    };
    static const char * const device_extensions[] =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    TRACE("adapter %p, minimum_feature_level %#x, iid %s, device %p.\n",
            adapter, minimum_feature_level, debugstr_guid(iid), device);

    if (adapter)
        FIXME("Ignoring adapter %p.\n", adapter);

    memset(&instance_create_info, 0, sizeof(instance_create_info));
    instance_create_info.instance_extensions = instance_extensions;
    instance_create_info.instance_extension_count = ARRAY_SIZE(instance_extensions);
    instance_create_info.optional_instance_extensions = optional_instance_extensions;
    instance_create_info.optional_instance_extension_count = ARRAY_SIZE(optional_instance_extensions);

    memset(&device_create_info, 0, sizeof(device_create_info));
    device_create_info.minimum_feature_level = minimum_feature_level;
    device_create_info.instance_create_info = &instance_create_info;
    device_create_info.device_extensions = device_extensions;
    device_create_info.device_extension_count = ARRAY_SIZE(device_extensions);

    return vkd3d_create_device(&device_create_info, iid, device);
}

VKD3D_UTILS_EXPORT HRESULT WINAPI D3D12CreateRootSignatureDeserializer(const void *data, SIZE_T data_size,
        REFIID iid, void **deserializer)
{
    TRACE("data %p, data_size %lu, iid %s, deserializer %p.\n",
            data, data_size, debugstr_guid(iid), deserializer);

    return vkd3d_create_root_signature_deserializer(data, data_size, iid, deserializer);
}

VKD3D_UTILS_EXPORT HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(const void *data, SIZE_T data_size,
        REFIID iid,void **deserializer)
{
    TRACE("data %p, data_size %lu, iid %s, deserializer %p.\n",
            data, data_size, debugstr_guid(iid), deserializer);

    return vkd3d_create_versioned_root_signature_deserializer(data, data_size, iid, deserializer);
}

VKD3D_UTILS_EXPORT HRESULT WINAPI D3D12EnableExperimentalFeatures(UINT feature_count,
        const IID *iids, void *configurations, UINT *configurations_sizes)
{
    FIXME("feature_count %u, iids %p, configurations %p, configurations_sizes %p stub!\n",
            feature_count, iids, configurations, configurations_sizes);

    return E_NOINTERFACE;
}

VKD3D_UTILS_EXPORT HRESULT WINAPI D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC *desc,
        D3D_ROOT_SIGNATURE_VERSION version, ID3DBlob **blob, ID3DBlob **error_blob)
{
    TRACE("desc %p, version %#x, blob %p, error_blob %p.\n", desc, version, blob, error_blob);

    return vkd3d_serialize_root_signature(desc, version, blob, error_blob);
}

VKD3D_UTILS_EXPORT HRESULT WINAPI D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc,
        ID3DBlob **blob, ID3DBlob **error_blob)
{
    TRACE("desc %p, blob %p, error_blob %p.\n", desc, blob, error_blob);

    return vkd3d_serialize_versioned_root_signature(desc, blob, error_blob);
}

#ifndef _WIN32
/* Events */
VKD3D_UTILS_EXPORT HANDLE vkd3d_create_eventfd(void)
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
    TRACE("Created event %p.\n", handle);
    return handle;
}

VKD3D_UTILS_EXPORT unsigned int vkd3d_wait_eventfd(HANDLE event, unsigned int milliseconds)
{
    int fd = (int)(intptr_t)event;
    struct pollfd pfd;
    uint64_t dummy;
    int timeout;

    TRACE("event %p, milliseconds %u.\n", event, milliseconds);

    pfd.events = POLLIN;
    pfd.fd = fd;

    timeout = milliseconds == VKD3D_INFINITE ? -1 : (int)milliseconds;
    if (poll(&pfd, 1, timeout) <= 0)
        return VKD3D_WAIT_TIMEOUT;

    /* Non-blocking reads, if there are two racing threads that wait on a Win32 event,
     * only one will succeed when auto-reset events are used. */
    if (read(fd, &dummy, sizeof(dummy)) > 0)
        return VKD3D_WAIT_OBJECT_0;
    else
        return VKD3D_WAIT_TIMEOUT;
}

VKD3D_UTILS_EXPORT void vkd3d_signal_eventfd(HANDLE event)
{
    int fd = (int)(intptr_t)event;
    const uint64_t value = 1;
    write(fd, &value, sizeof(value));
}

VKD3D_UTILS_EXPORT void vkd3d_destroy_eventfd(HANDLE event)
{
    close((int)(intptr_t)event);
}
#endif
