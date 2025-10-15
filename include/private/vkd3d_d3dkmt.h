/*
 * Copyright 2025 Rémi Bernon for CodeWeavers
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

#ifndef __VKD3D_D3DKMT_H
#define __VKD3D_D3DKMT_H

#ifdef _WIN32
#include "vkd3d_windows.h"

typedef LONG NTSTATUS;
#define STATUS_SUCCESS 0

typedef ULONGLONG D3DGPU_VIRTUAL_ADDRESS;

typedef struct _D3DKMT_CLOSEADAPTER
{
    D3DKMT_HANDLE hAdapter;
} D3DKMT_CLOSEADAPTER;

typedef struct _D3DKMT_CREATEDEVICEFLAGS
{
    UINT LegacyMode : 1;
    UINT RequestVSync : 1;
    UINT DisableGpuTimeout : 1;
    UINT Reserved : 29;
} D3DKMT_CREATEDEVICEFLAGS;

typedef struct _D3DDDI_ALLOCATIONLIST
{
    D3DKMT_HANDLE hAllocation;
    union
    {
        struct
        {
            UINT WriteOperation : 1;
            UINT DoNotRetireInstance : 1;
            UINT OfferPriority : 3;
            UINT Reserved : 27;
        } DUMMYSTRUCTNAME;
        UINT Value;
    } DUMMYUNIONNAME;
} D3DDDI_ALLOCATIONLIST;

typedef struct _D3DDDI_PATCHLOCATIONLIST
{
    UINT AllocationIndex;
    union
    {
        struct
        {
            UINT SlotId : 24;
            UINT Reserved : 8;
        } DUMMYSTRUCTNAME;
        UINT Value;
    } DUMMYUNIONNAME;
    UINT DriverId;
    UINT AllocationOffset;
    UINT PatchOffset;
    UINT SplitOffset;
} D3DDDI_PATCHLOCATIONLIST;

typedef struct _D3DKMT_CREATEDEVICE
{
    union
    {
        D3DKMT_HANDLE hAdapter;
        VOID *pAdapter;
    } DUMMYUNIONNAME;
    D3DKMT_CREATEDEVICEFLAGS Flags;
    D3DKMT_HANDLE hDevice;
    VOID *pCommandBuffer;
    UINT CommandBufferSize;
    D3DDDI_ALLOCATIONLIST *pAllocationList;
    UINT AllocationListSize;
    D3DDDI_PATCHLOCATIONLIST *pPatchLocationList;
    UINT PatchLocationListSize;
} D3DKMT_CREATEDEVICE;

typedef struct _D3DKMT_DESTROYALLOCATION
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hResource;
    const D3DKMT_HANDLE *phAllocationList;
    UINT AllocationCount;
} D3DKMT_DESTROYALLOCATION;

typedef struct _D3DKMT_DESTROYDEVICE
{
    D3DKMT_HANDLE hDevice;
} D3DKMT_DESTROYDEVICE;

typedef struct _D3DKMT_DESTROYKEYEDMUTEX
{
    D3DKMT_HANDLE hKeyedMutex;
} D3DKMT_DESTROYKEYEDMUTEX;

typedef struct _D3DKMT_DESTROYSYNCHRONIZATIONOBJECT
{
    D3DKMT_HANDLE hSyncObject;
} D3DKMT_DESTROYSYNCHRONIZATIONOBJECT;

typedef struct _D3DKMT_OPENADAPTERFROMLUID
{
    LUID AdapterLuid;
    D3DKMT_HANDLE hAdapter;
} D3DKMT_OPENADAPTERFROMLUID;

typedef struct _D3DDDI_OPENALLOCATIONINFO
{
    D3DKMT_HANDLE hAllocation;
    const void *pPrivateDriverData;
    UINT PrivateDriverDataSize;
} D3DDDI_OPENALLOCATIONINFO;

typedef struct _D3DDDI_OPENALLOCATIONINFO2
{
    D3DKMT_HANDLE hAllocation;
    const void *pPrivateDriverData;
    UINT PrivateDriverDataSize;
    D3DGPU_VIRTUAL_ADDRESS GpuVirtualAddress;
    ULONG_PTR Reserved[6];
} D3DDDI_OPENALLOCATIONINFO2;

typedef struct _D3DKMT_OPENRESOURCEFROMNTHANDLE
{
    D3DKMT_HANDLE hDevice;
    HANDLE hNtHandle;
    UINT NumAllocations;
    D3DDDI_OPENALLOCATIONINFO2 *pOpenAllocationInfo2;
    UINT PrivateRuntimeDataSize;
    void *pPrivateRuntimeData;
    UINT ResourcePrivateDriverDataSize;
    void *pResourcePrivateDriverData;
    UINT TotalPrivateDriverDataBufferSize;
    void *pTotalPrivateDriverDataBuffer;
    D3DKMT_HANDLE hResource;
    D3DKMT_HANDLE hKeyedMutex;
    void *pKeyedMutexPrivateRuntimeData;
    UINT KeyedMutexPrivateRuntimeDataSize;
    D3DKMT_HANDLE hSyncObject;
} D3DKMT_OPENRESOURCEFROMNTHANDLE;

typedef struct _D3DKMT_OPENSYNCOBJECTFROMNTHANDLE
{
    HANDLE hNtHandle;
    D3DKMT_HANDLE hSyncObject;
} D3DKMT_OPENSYNCOBJECTFROMNTHANDLE;

EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTCloseAdapter(const D3DKMT_CLOSEADAPTER *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTCreateDevice(D3DKMT_CREATEDEVICE *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTDestroyAllocation(const D3DKMT_DESTROYALLOCATION *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTDestroyDevice(const D3DKMT_DESTROYDEVICE *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTDestroyKeyedMutex(const D3DKMT_DESTROYKEYEDMUTEX *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTDestroySynchronizationObject(const D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTOpenAdapterFromLuid(D3DKMT_OPENADAPTERFROMLUID *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTOpenResourceFromNtHandle(D3DKMT_OPENRESOURCEFROMNTHANDLE *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTOpenSyncObjectFromNtHandle(D3DKMT_OPENSYNCOBJECTFROMNTHANDLE *desc);

#endif  /* _WIN32 */

extern void d3d12_device_open_kmt(struct d3d12_device *device);
extern void d3d12_device_close_kmt(struct d3d12_device *device);

extern void d3d12_shared_fence_open_export_kmt(struct d3d12_shared_fence *fence, struct d3d12_device *device);
extern void d3d12_shared_fence_close_export_kmt(struct d3d12_shared_fence *fence);

extern void d3d12_resource_open_export_kmt(struct d3d12_resource *resource, struct d3d12_device *device,
        struct vkd3d_memory_allocation *allocation);
extern void d3d12_resource_close_export_kmt(struct d3d12_resource *resource, struct d3d12_device *device);

#endif  /* __VKD3D_D3DKMT_H */
