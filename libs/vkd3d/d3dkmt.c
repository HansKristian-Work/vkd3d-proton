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

    if (!device->kmt_local)
    {
        /* D3DKMT API isn't supported */
        return;
    }

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
    if (fence->kmt_local)
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT destroy = {0};
        destroy.hSyncObject = fence->kmt_local;
        D3DKMTDestroySynchronizationObject(&destroy);
    }
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

    if (!device->kmt_local)
    {
        /* D3DKMT API isn't supported */
        return;
    }

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

    if (resource->kmt_local)
    {
        struct d3dkmt_d3d12_desc desc = {0};
        D3DKMT_ESCAPE escape = {0};

        desc.d3d11.dxgi.size = sizeof(desc.d3d11);
        desc.d3d11.dxgi.nt_shared = 1;
        desc.desc1 = resource->desc;

        if (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
        {
            desc.d3d11.dxgi.width = resource->desc.Width;
            desc.d3d11.dxgi.height = resource->desc.Height;
            desc.d3d11.dxgi.format = resource->desc.Format;

            if (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)
            {
                desc.d3d11.dxgi.version = 4;
                switch (resource->desc.Dimension)
                {
                    case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
                        desc.d3d11.dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
                        desc.d3d11.d3d11_2d.Width = resource->desc.Width;
                        desc.d3d11.d3d11_2d.Height = resource->desc.Height;
                        desc.d3d11.d3d11_2d.MipLevels = resource->desc.MipLevels;
                        desc.d3d11.d3d11_2d.ArraySize = resource->desc.DepthOrArraySize;
                        desc.d3d11.d3d11_2d.Format = resource->desc.Format;
                        desc.d3d11.d3d11_2d.SampleDesc = resource->desc.SampleDesc;
                        desc.d3d11.d3d11_2d.Usage = D3D11_USAGE_DEFAULT;
                        desc.d3d11.d3d11_2d.BindFlags = D3D11_BIND_RENDER_TARGET;
                        if (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
                            desc.d3d11.d3d11_2d.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
                        if (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
                            desc.d3d11.d3d11_2d.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
                        if (!(resource->desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
                            desc.d3d11.d3d11_2d.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
                        desc.d3d11.d3d11_2d.CPUAccessFlags = 0;
                        desc.d3d11.d3d11_2d.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
                        break;
                    default:
                        WARN("Unsupported shared resource dimension %#x\n", resource->desc.Dimension);
                        break;
                }
            }
        }

        escape.Type = D3DKMT_ESCAPE_UPDATE_RESOURCE_WINE;
        escape.hContext = resource->kmt_local;
        escape.pPrivateDriverData = &desc;
        escape.PrivateDriverDataSize = sizeof(desc);
        D3DKMTEscape(&escape);
    }
}

void d3d12_resource_close_export_kmt(struct d3d12_resource *resource, struct d3d12_device *device)
{
    if (resource->kmt_local)
    {
        D3DKMT_DESTROYALLOCATION destroy = {0};
        destroy.hDevice = device->kmt_local;
        destroy.hResource = resource->kmt_local;
        D3DKMTDestroyAllocation(&destroy);
    }
}

HRESULT d3d12_device_open_resource_descriptor(struct d3d12_device *device, HANDLE handle, D3D12_RESOURCE_DESC1 *desc)
{
    D3DKMT_DESTROYALLOCATION destroy = {0};
    union d3dkmt_desc d3dkmt = {0};
    UINT size;

    if (!device->kmt_local)
    {
        /* D3DKMT API isn't supported */
        return E_NOTIMPL;
    }

    if ((UINT_PTR)handle & 0xc0000000)
    {
        D3DDDI_OPENALLOCATIONINFO2 alloc = {0};
        D3DKMT_QUERYRESOURCEINFO query = {0};
        D3DKMT_OPENRESOURCE open = {0};

        query.hDevice = device->kmt_local;
        query.hGlobalShare = (UINT_PTR)handle;
        query.pPrivateRuntimeData = &d3dkmt;
        query.PrivateRuntimeDataSize = sizeof(d3dkmt);

        if (D3DKMTQueryResourceInfo(&query) != STATUS_SUCCESS)
            return E_INVALIDARG;
        if (query.PrivateRuntimeDataSize < sizeof(d3dkmt.dxgi) || query.PrivateRuntimeDataSize > sizeof(d3dkmt))
        {
            WARN("Unsupported shared resource runtime data size %#x\n", query.PrivateRuntimeDataSize);
            return E_NOTIMPL;
        }

        open.hDevice = device->kmt_local;
        open.hGlobalShare = (UINT_PTR)handle;
        open.NumAllocations = 1;
        open.pOpenAllocationInfo2 = &alloc;
        open.pPrivateRuntimeData = &d3dkmt;
        open.PrivateRuntimeDataSize = query.PrivateRuntimeDataSize;

        if (D3DKMTOpenResource2(&open) != STATUS_SUCCESS)
        {
            WARN("Failed to open shared resource handle %p\n", handle);
            return E_INVALIDARG;
        }
        size = open.PrivateRuntimeDataSize;
        destroy.hResource = open.hResource;
    }
    else
    {
        D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE query = {0};
        D3DKMT_OPENRESOURCEFROMNTHANDLE open = {0};
        D3DDDI_OPENALLOCATIONINFO2 alloc = {0};
        char dummy;

        query.hDevice = device->kmt_local;
        query.hNtHandle = handle;
        query.pPrivateRuntimeData = &d3dkmt;
        query.PrivateRuntimeDataSize = sizeof(d3dkmt);

        if (D3DKMTQueryResourceInfoFromNtHandle(&query) != STATUS_SUCCESS)
            return E_INVALIDARG;
        if (query.PrivateRuntimeDataSize < sizeof(d3dkmt.dxgi) || query.PrivateRuntimeDataSize > sizeof(d3dkmt))
        {
            WARN("Unsupported shared resource runtime data size %#x\n", query.PrivateRuntimeDataSize);
            return E_NOTIMPL;
        }

        open.hDevice = device->kmt_local;
        open.hNtHandle = handle;
        open.NumAllocations = 1;
        open.pOpenAllocationInfo2 = &alloc;
        open.pPrivateRuntimeData = &d3dkmt;
        open.PrivateRuntimeDataSize = query.PrivateRuntimeDataSize;
        open.pTotalPrivateDriverDataBuffer = &dummy;
        open.TotalPrivateDriverDataBufferSize = 0;

        if (D3DKMTOpenResourceFromNtHandle(&open) != STATUS_SUCCESS)
        {
            WARN("Failed to open shared resource handle %p\n", handle);
            return E_INVALIDARG;
        }
        if (open.hKeyedMutex)
        {
            D3DKMT_DESTROYKEYEDMUTEX destroy_mutex = {0};
            FIXME("Ignoring bundled keyed mutex\n");
            destroy_mutex.hKeyedMutex = open.hKeyedMutex;
            D3DKMTDestroyKeyedMutex(&destroy_mutex);
        }
        if (open.hSyncObject)
        {
            D3DKMT_DESTROYSYNCHRONIZATIONOBJECT destroy_sync = {0};
            FIXME("Ignoring bundled sync object\n");
            destroy_sync.hSyncObject = open.hSyncObject;
            D3DKMTDestroySynchronizationObject(&destroy_sync);
        }

        size = open.PrivateRuntimeDataSize;
        destroy.hResource = open.hResource;
    }

    destroy.hDevice = device->kmt_local;
    D3DKMTDestroyAllocation(&destroy);

    TRACE("Found descriptor with size %u/%u version %u\n", size, d3dkmt.dxgi.size, d3dkmt.dxgi.version);
    if (size == sizeof(d3dkmt.d3d12) && d3dkmt.dxgi.size == sizeof(d3dkmt.d3d11) && (d3dkmt.dxgi.version == 0 || d3dkmt.dxgi.version == 4))
    {
        TRACE("Using D3D12 descriptor\n");
        *desc = d3dkmt.d3d12.desc1;
        return S_OK;
    }

    if (size == sizeof(d3dkmt.d3d11) && d3dkmt.dxgi.size == sizeof(d3dkmt.d3d11) && d3dkmt.dxgi.version == 4)
    {
        TRACE("Found D3D11 desc with dimension %u\n", d3dkmt.d3d11.dimension);

        switch (d3dkmt.d3d11.dimension)
        {
            case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
                memset(desc, 0, sizeof(*desc));

                desc->Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc->Width = d3dkmt.d3d11.d3d11_2d.Width;
                desc->Height = d3dkmt.d3d11.d3d11_2d.Height;
                desc->DepthOrArraySize = d3dkmt.d3d11.d3d11_2d.ArraySize;
                desc->MipLevels = d3dkmt.d3d11.d3d11_2d.MipLevels;
                desc->Format = d3dkmt.d3d11.d3d11_2d.Format;
                desc->SampleDesc = d3dkmt.d3d11.d3d11_2d.SampleDesc;

                desc->Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                if (d3dkmt.d3d11.d3d11_2d.BindFlags & D3D11_BIND_RENDER_TARGET)
                    desc->Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                if (d3dkmt.d3d11.d3d11_2d.BindFlags & D3D11_BIND_DEPTH_STENCIL)
                {
                    desc->Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
                    if (!(d3dkmt.d3d11.d3d11_2d.BindFlags & D3D11_BIND_SHADER_RESOURCE))
                        desc->Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
                }
                else
                    desc->Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
                if (d3dkmt.d3d11.d3d11_2d.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
                    desc->Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                return S_OK;

            default:
                FIXME("D3D11 dimension %#x not implemented!\n", d3dkmt.d3d11.dimension);
                return E_INVALIDARG;
        }
    }

    if (size == sizeof(d3dkmt.d3d9) && d3dkmt.dxgi.size == sizeof(d3dkmt.d3d9) && d3dkmt.dxgi.version == 1)
    {
        TRACE("Found D3D9 desc type %#x\n", d3dkmt.d3d9.type);
        TRACE("  dxgi.width %u\n", d3dkmt.d3d9.dxgi.width);
        TRACE("  dxgi.height %u\n", d3dkmt.d3d9.dxgi.height);
        TRACE("  format %#x\n", d3dkmt.d3d9.format);
        TRACE("  usage %#x\n", d3dkmt.d3d9.usage);
        if (d3dkmt.d3d9.type == D3DRTYPE_TEXTURE)
        {
            TRACE("  texture.width %u\n", d3dkmt.d3d9.texture.width);
            TRACE("  texture.height %u\n", d3dkmt.d3d9.texture.height);
            TRACE("  texture.depth %u\n", d3dkmt.d3d9.texture.depth);
            TRACE("  texture.levels %u\n", d3dkmt.d3d9.texture.levels);
        }
        else if (d3dkmt.d3d9.type == D3DRTYPE_SURFACE)
        {
            TRACE("  surface.width %u\n", d3dkmt.d3d9.surface.width);
            TRACE("  surface.height %u\n", d3dkmt.d3d9.surface.height);
        }
        else
        {
            FIXME("D3D9 type %#x not implemented!\n", d3dkmt.d3d9.type);
            return E_INVALIDARG;
        }

        memset(desc, 0, sizeof(*desc));
        desc->Width = d3dkmt.d3d9.dxgi.width;
        desc->Height = d3dkmt.d3d9.dxgi.height;
        desc->DepthOrArraySize = 1;
        desc->MipLevels = 1;
        desc->Format = d3dkmt.d3d9.dxgi.format;
        desc->SampleDesc.Count = 1;
        desc->SampleDesc.Quality = 0;
        desc->Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc->Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        switch (d3dkmt.d3d9.type)
        {
            case D3DRTYPE_TEXTURE:
                desc->Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc->Width = d3dkmt.d3d9.texture.width;
                desc->Height = d3dkmt.d3d9.texture.height;
                desc->MipLevels = d3dkmt.d3d9.texture.levels;
                desc->DepthOrArraySize = d3dkmt.d3d9.texture.depth ? d3dkmt.d3d9.texture.depth : 1;
                break;
            case D3DRTYPE_SURFACE:
                desc->Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc->Width = d3dkmt.d3d9.surface.width;
                desc->Height = d3dkmt.d3d9.surface.height;
                break;
            default:
                break;
        }

        return S_OK;
    }

    FIXME("Unsupported data size %u/%u version %u\n", size, d3dkmt.dxgi.size, d3dkmt.dxgi.version);
    return E_INVALIDARG;
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
