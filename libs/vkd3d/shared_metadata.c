/*
 * Copyright 2021 Derek Lesho for Codeweavers
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

#include "winioctl.h"

#define IOCTL_SHARED_GPU_RESOURCE_SET_METADATA           CTL_CODE(FILE_DEVICE_VIDEO, 4, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_SHARED_GPU_RESOURCE_GET_METADATA           CTL_CODE(FILE_DEVICE_VIDEO, 5, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_SHARED_GPU_RESOURCE_OPEN                   CTL_CODE(FILE_DEVICE_VIDEO, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)

bool vkd3d_set_shared_metadata(HANDLE handle, void *buf, uint32_t buf_size)
{
    DWORD ret_size;

    return DeviceIoControl(handle, IOCTL_SHARED_GPU_RESOURCE_SET_METADATA, buf, buf_size, NULL, 0, &ret_size, NULL);
}

bool vkd3d_get_shared_metadata(HANDLE handle, void *buf, uint32_t buf_size, uint32_t *metadata_size)
{
    DWORD ret_size;

    bool ret = DeviceIoControl(handle, IOCTL_SHARED_GPU_RESOURCE_GET_METADATA, NULL, 0, buf, buf_size, &ret_size, NULL);

    if (metadata_size)
        *metadata_size = ret_size;

    return ret;
}

HANDLE vkd3d_open_kmt_handle(HANDLE kmt_handle)
{
    struct
    {
        unsigned int kmt_handle;
        /* the following parameter represents a larger sized string for a dynamically allocated struct for use when opening an object by name */
        WCHAR name[1];
    } shared_resource_open;

    HANDLE nt_handle = CreateFileA("\\\\.\\SharedGpuResource", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (nt_handle == INVALID_HANDLE_VALUE)
        return nt_handle;

    shared_resource_open.kmt_handle = (ULONG_PTR)kmt_handle;
    shared_resource_open.name[0] = 0;
    if (!DeviceIoControl(nt_handle, IOCTL_SHARED_GPU_RESOURCE_OPEN, &shared_resource_open, sizeof(shared_resource_open), NULL, 0, NULL, NULL))
    {
        CloseHandle(nt_handle);
        return INVALID_HANDLE_VALUE;
    }
    return nt_handle;
}
