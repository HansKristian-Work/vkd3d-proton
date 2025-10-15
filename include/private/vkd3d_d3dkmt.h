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

#include <d3d9.h>

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

typedef enum _D3DKMT_ESCAPETYPE
{
    D3DKMT_ESCAPE_UPDATE_RESOURCE_WINE = 0x80000000
} D3DKMT_ESCAPETYPE;

typedef struct _D3DDDI_ESCAPEFLAGS
{
    union
    {
        struct
        {
            UINT HardwareAccess :1;
            UINT Reserved       :31;
        };
        UINT Value;
    };
} D3DDDI_ESCAPEFLAGS;

typedef struct _D3DKMT_ESCAPE
{
    D3DKMT_HANDLE      hAdapter;
    D3DKMT_HANDLE      hDevice;
    D3DKMT_ESCAPETYPE  Type;
    D3DDDI_ESCAPEFLAGS Flags;
    void              *pPrivateDriverData;
    UINT               PrivateDriverDataSize;
    D3DKMT_HANDLE      hContext;
} D3DKMT_ESCAPE;

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

typedef struct _UNICODE_STRING {
    USHORT Length;        /* bytes */
    USHORT MaximumLength; /* bytes */
    WCHAR *Buffer;
} UNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    UNICODE_STRING *ObjectName;
    ULONG Attributes;
    void *SecurityDescriptor;
    void *SecurityQualityOfService;
} OBJECT_ATTRIBUTES;

#define OBJ_CASE_INSENSITIVE 0x00000040

/* undocumented D3D runtime data descriptors */

struct d3dkmt_dxgi_desc
{
    UINT                        size;
    UINT                        version;
    UINT                        width;
    UINT                        height;
    DXGI_FORMAT                 format;
    UINT                        unknown_0;
    UINT                        unknown_1;
    UINT                        keyed_mutex;
    D3DKMT_HANDLE               mutex_handle;
    D3DKMT_HANDLE               sync_handle;
    UINT                        nt_shared;
    UINT                        unknown_2;
    UINT                        unknown_3;
    UINT                        unknown_4;
};

struct d3dkmt_d3d9_desc
{
    struct d3dkmt_dxgi_desc     dxgi;
    D3DFORMAT                   format;
    D3DRESOURCETYPE             type;
    UINT                        usage;
    union
    {
        struct
        {
            UINT                unknown_0;
            UINT                width;
            UINT                height;
            UINT                levels;
            UINT                depth;
        } texture;
        struct
        {
            UINT                unknown_0;
            UINT                unknown_1;
            UINT                unknown_2;
            UINT                width;
            UINT                height;
        } surface;
        struct
        {
            UINT                unknown_0;
            UINT                width;
            UINT                format;
            UINT                unknown_1;
            UINT                unknown_2;
        } buffer;
    };
};

C_ASSERT( sizeof(struct d3dkmt_d3d9_desc) == 0x58 );

struct d3dkmt_d3d11_desc
{
    struct d3dkmt_dxgi_desc     dxgi;
    D3D11_RESOURCE_DIMENSION    dimension;
    union
    {
        D3D11_BUFFER_DESC       d3d11_buf;
        D3D11_TEXTURE1D_DESC    d3d11_1d;
        D3D11_TEXTURE2D_DESC    d3d11_2d;
        D3D11_TEXTURE3D_DESC    d3d11_3d;
    };
};

C_ASSERT( sizeof(struct d3dkmt_d3d11_desc) == 0x68 );

struct d3dkmt_d3d12_desc
{
    struct d3dkmt_d3d11_desc    d3d11;
    UINT                        unknown_5[4];
    UINT                        resource_size;
    UINT                        unknown_6[7];
    UINT                        resource_align;
    UINT                        unknown_7[9];
    union
    {
        D3D12_RESOURCE_DESC     desc;
        D3D12_RESOURCE_DESC1    desc1;
        UINT                    __pad[16];
    };
    UINT64                      unknown_8[1];
};

C_ASSERT( sizeof(struct d3dkmt_d3d12_desc) == 0x108 );

union d3dkmt_desc
{
    struct d3dkmt_dxgi_desc     dxgi;
    struct d3dkmt_d3d9_desc     d3d9;   /* if dxgi.size == sizeof(d3d9)  && dxgi.version == 1 && sizeof(desc) == sizeof(d3d9) */
    struct d3dkmt_d3d11_desc    d3d11;  /* if dxgi.size == sizeof(d3d11) && dxgi.version == 4 && sizeof(desc) >= sizeof(d3d11) */
    struct d3dkmt_d3d12_desc    d3d12;  /* if dxgi.size == sizeof(d3d11) && dxgi.version == 0 && sizeof(desc) == sizeof(d3d12) */
};

EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTCloseAdapter(const D3DKMT_CLOSEADAPTER *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTCreateDevice(D3DKMT_CREATEDEVICE *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTDestroyAllocation(const D3DKMT_DESTROYALLOCATION *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTDestroyDevice(const D3DKMT_DESTROYDEVICE *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTDestroyKeyedMutex(const D3DKMT_DESTROYKEYEDMUTEX *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTDestroySynchronizationObject(const D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTEscape(const D3DKMT_ESCAPE *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTOpenAdapterFromLuid(D3DKMT_OPENADAPTERFROMLUID *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTOpenResourceFromNtHandle(D3DKMT_OPENRESOURCEFROMNTHANDLE *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTOpenSyncObjectFromNtHandle(D3DKMT_OPENSYNCOBJECTFROMNTHANDLE *desc);
EXTERN_C WINBASEAPI NTSTATUS WINAPI D3DKMTShareObjects(UINT count, const D3DKMT_HANDLE *handles, OBJECT_ATTRIBUTES *attr, UINT access, HANDLE *handle);

#endif  /* _WIN32 */

extern void d3d12_device_open_kmt(struct d3d12_device *device);
extern void d3d12_device_close_kmt(struct d3d12_device *device);

extern void d3d12_shared_fence_open_export_kmt(struct d3d12_shared_fence *fence, struct d3d12_device *device);
extern void d3d12_shared_fence_close_export_kmt(struct d3d12_shared_fence *fence);

extern void d3d12_resource_open_export_kmt(struct d3d12_resource *resource, struct d3d12_device *device,
        struct vkd3d_memory_allocation *allocation);
extern void d3d12_resource_close_export_kmt(struct d3d12_resource *resource, struct d3d12_device *device);

#endif  /* __VKD3D_D3DKMT_H */
