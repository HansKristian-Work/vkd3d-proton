/*
 * Copyright 2025 Rémi Bernon for Codeweavers
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
#include "vkd3d_private.h"
#include "vkd3d_d3dkmt.h"

#ifdef _WIN32

void d3d12_device_open_kmt(struct d3d12_device *device)
{
    D3DKMT_OPENADAPTERFROMLUID open_adapter = {0};
    open_adapter.AdapterLuid = device->adapter_luid;

    if (D3DKMTOpenAdapterFromLuid(&open_adapter) == STATUS_SUCCESS)
    {
        D3DKMT_CREATEDEVICE create_device = {0};
        D3DKMT_CLOSEADAPTER close_adapter = {0};

        close_adapter.hAdapter = open_adapter.hAdapter;
        create_device.hAdapter = open_adapter.hAdapter;
        if (D3DKMTCreateDevice(&create_device) == STATUS_SUCCESS)
            device->kmt_local = create_device.hDevice;

        D3DKMTCloseAdapter(&close_adapter);
    }
}

void d3d12_device_close_kmt(struct d3d12_device *device)
{
    D3DKMT_DESTROYDEVICE destroy = {0};
    destroy.hDevice = device->kmt_local;
    D3DKMTDestroyDevice(&destroy);
}

void d3d12_shared_fence_open_export_kmt(struct d3d12_shared_fence *fence, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkSemaphoreGetWin32HandleInfoKHR win32_handle_info;
    D3DKMT_OPENSYNCOBJECTFROMNTHANDLE open = {0};
    VkResult vr;

    if (!device->kmt_local) return; /* D3DKMT API isn't supported */

    win32_handle_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
    win32_handle_info.pNext = NULL;
    win32_handle_info.semaphore = fence->timeline_semaphore;
    win32_handle_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;

    if ((vr = VK_CALL(vkGetSemaphoreWin32HandleKHR(device->vk_device, &win32_handle_info, &open.hNtHandle))))
        ERR("Failed to get exported semaphore handle, vr %d.\n", vr);
    else
    {
        if (D3DKMTOpenSyncObjectFromNtHandle(&open) == STATUS_SUCCESS)
            fence->kmt_local = open.hSyncObject;
        CloseHandle(open.hNtHandle);
    }
}

void d3d12_shared_fence_close_export_kmt(struct d3d12_shared_fence *fence)
{
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT destroy = {0};
    destroy.hSyncObject = fence->kmt_local;
    D3DKMTDestroySynchronizationObject(&destroy);
}

void d3d12_resource_open_export_kmt(struct d3d12_resource *resource, struct d3d12_device *device,
        struct vkd3d_memory_allocation *allocation)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkMemoryGetWin32HandleInfoKHR win32_handle_info;
    D3DKMT_OPENRESOURCEFROMNTHANDLE open = {0};
    D3DDDI_OPENALLOCATIONINFO2 alloc = {0};
    VkResult vr;
    char dummy;

    if (!device->kmt_local) return; /* D3DKMT API isn't supported */

    open.hDevice = device->kmt_local;
    open.NumAllocations = 1;
    open.pOpenAllocationInfo2 = &alloc;
    open.pPrivateRuntimeData = &dummy;
    open.PrivateRuntimeDataSize = 0;
    open.pTotalPrivateDriverDataBuffer = &dummy;
    open.TotalPrivateDriverDataBufferSize = 0;

    win32_handle_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    win32_handle_info.pNext = NULL;
    win32_handle_info.memory = allocation->device_allocation.vk_memory;
    win32_handle_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    if ((vr = VK_CALL(vkGetMemoryWin32HandleKHR(device->vk_device, &win32_handle_info, &open.hNtHandle))))
        ERR("Failed to get exported image memory handle, vr %d.\n", vr);
    else
    {
        if (D3DKMTOpenResourceFromNtHandle(&open) == STATUS_SUCCESS)
        {
            resource->kmt_local = open.hResource;

            if (open.hKeyedMutex)
            {
                D3DKMT_DESTROYKEYEDMUTEX destroy_mutex = {0};
                FIXME("Unexpected bundled keyed mutex\n");
                destroy_mutex.hKeyedMutex = open.hKeyedMutex;
                D3DKMTDestroyKeyedMutex(&destroy_mutex);
            }
            if (open.hSyncObject)
            {
                D3DKMT_DESTROYSYNCHRONIZATIONOBJECT destroy_sync = {0};
                FIXME("Unexpected bundled sync object\n");
                destroy_sync.hSyncObject = open.hSyncObject;
                D3DKMTDestroySynchronizationObject(&destroy_sync);
            }
        }

        CloseHandle(open.hNtHandle);
    }
}

void d3d12_resource_close_export_kmt(struct d3d12_resource *resource, struct d3d12_device *device)
{
    D3DKMT_DESTROYALLOCATION destroy = {0};
    destroy.hDevice = device->kmt_local;
    destroy.hResource = resource->kmt_local;
    D3DKMTDestroyAllocation(&destroy);
}

#else /* _WIN32 */

void d3d12_device_open_kmt(struct d3d12_device *device)
{
    WARN("Not implemented on this platform\n");
}

void d3d12_device_close_kmt(struct d3d12_device *device)
{
}

void d3d12_shared_fence_open_export_kmt(struct d3d12_shared_fence *fence, struct d3d12_device *device)
{
    WARN("Not implemented on this platform\n");
}

void d3d12_shared_fence_close_export_kmt(struct d3d12_shared_fence *fence)
{
}

void d3d12_resource_open_export_kmt(struct d3d12_resource *resource, struct d3d12_device *device,
        struct vkd3d_memory_allocation *allocation)
{
    WARN("Not implemented on this platform\n");
}

void d3d12_resource_close_export_kmt(struct d3d12_resource *resource, struct d3d12_device *device)
{
}

#endif /* _WIN32 */
