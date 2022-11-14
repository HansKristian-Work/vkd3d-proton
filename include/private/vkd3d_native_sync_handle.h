/*
 * Copyright 2022 Hans-Kristian Arntzen for Valve Corporation
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

#ifndef __VKD3D_NATIVE_SYNC_HANDLE_H
#define __VKD3D_NATIVE_SYNC_HANDLE_H

enum vkd3d_native_sync_handle_type
{
    VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE = 0,
    VKD3D_NATIVE_SYNC_HANDLE_TYPE_EVENT = 1,
    VKD3D_NATIVE_SYNC_HANDLE_TYPE_SEMAPHORE = 2,
};

#ifdef _WIN32
typedef struct { HANDLE handle; enum vkd3d_native_sync_handle_type type; } vkd3d_native_sync_handle;
#else
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <unistd.h>
typedef struct { int fd; enum vkd3d_native_sync_handle_type type; } vkd3d_native_sync_handle;
#endif

static inline bool vkd3d_native_sync_handle_eq(vkd3d_native_sync_handle a, vkd3d_native_sync_handle b)
{
#ifdef _WIN32
    return a.handle == b.handle;
#else
    return a.fd == b.fd;
#endif
}

static inline vkd3d_native_sync_handle vkd3d_native_sync_handle_wrap(HANDLE os_handle,
        enum vkd3d_native_sync_handle_type type)
{
    vkd3d_native_sync_handle handle;
#ifdef _WIN32
    handle.handle = os_handle;
    handle.type = os_handle ? type : VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE;
#else
    handle.fd = (int)(intptr_t)os_handle;
    /* Treats FD 0 as invalid. FD 0 is technically valid since it's STDIN by default.
     * Calling code can ensure that FD 0 is not used for an eventfd()
     * through dup() and then closing fd 0. */
    handle.type = os_handle && handle.fd >= 0 ? type : VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE;
#endif
    return handle;
}

static inline int vkd3d_native_sync_handle_release(vkd3d_native_sync_handle handle, UINT count)
{
#ifdef _WIN32
    if (handle.type == VKD3D_NATIVE_SYNC_HANDLE_TYPE_SEMAPHORE)
    {
        LONG prev = 0;
        if (!ReleaseSemaphore(handle.handle, count, &prev))
        {
            /* Failing to release semaphore is expected if the counter exceeds the maximum limit.
             * If the application does not wait for the semaphore once per present, this
             * will eventually happen. */
            WARN("Failed to release semaphore (#%x).\n", GetLastError());
            prev = -1;
        }
        return prev;
    }
    else if (handle.type == VKD3D_NATIVE_SYNC_HANDLE_TYPE_EVENT)
    {
        if (!SetEvent(handle.handle))
        {
            ERR("Failed to set event %p (#%x).\n", handle.handle, GetLastError());
            return -1;
        }
        return 0;
    }
    else
        return -1;
#else
    if (handle.type != VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE)
    {
        const uint64_t value = count;
        int ret = 0;
        if (write(handle.fd, &value, sizeof(value)) < 0)
        {
            ERR("Failed to release eventfd.\n");
            ret = -1;
        }
        return ret;
    }
    else
        return -1;
#endif
}

static inline HRESULT vkd3d_native_sync_handle_signal(vkd3d_native_sync_handle handle)
{
    return vkd3d_native_sync_handle_release(handle, 1) >= 0 ? S_OK : E_FAIL;
}

static inline bool vkd3d_native_sync_handle_acquire(vkd3d_native_sync_handle handle)
{
    if (handle.type != VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE)
    {
#ifdef _WIN32
        return WaitForSingleObject(handle.handle, INFINITE) == WAIT_OBJECT_0;
#else
        uint64_t dummy;
        return (size_t) read(handle.fd, &dummy, sizeof(dummy)) == sizeof(dummy);
#endif
    }
    else
        return false;
}

static inline bool vkd3d_native_sync_handle_acquire_timeout(vkd3d_native_sync_handle handle,
        unsigned int mills)
{
    if (handle.type != VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE)
    {
#ifdef _WIN32
        return WaitForSingleObject(handle.handle, mills) == WAIT_OBJECT_0;
#else
        struct pollfd pfd;
        uint64_t dummy;

        pfd.fd = handle.fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, (int) mills) <= 0)
            return false;

        /* If concurrent readers of the event needs to be considered (where only one waiter wins),
         * this must be O_NONBLOCK. This is only used internally however, so it's okay to leave as-is. */
        return (size_t) read(handle.fd, &dummy, sizeof(dummy)) == sizeof(dummy);
#endif
    }
    else
        return false;
}

static inline bool vkd3d_native_sync_handle_is_valid(vkd3d_native_sync_handle handle)
{
    return handle.type != VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE;
}

static inline HRESULT vkd3d_native_sync_handle_create(UINT initial,
        enum vkd3d_native_sync_handle_type type,
        vkd3d_native_sync_handle *handle)
{
    VKD3D_UNUSED unsigned int flags;
    HRESULT hr = S_OK;
    assert(type != VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE);

#ifdef _WIN32
    if (type == VKD3D_NATIVE_SYNC_HANDLE_TYPE_SEMAPHORE)
    {
        if (!(handle->handle = CreateSemaphoreA(NULL, initial, DXGI_MAX_SWAP_CHAIN_BUFFERS, NULL)))
            hr = HRESULT_FROM_WIN32(GetLastError());
    }
    else
    {
        if (!(handle->handle = CreateEventA(NULL, FALSE, FALSE, NULL)))
            hr = HRESULT_FROM_WIN32(GetLastError());
    }
#else
    flags = EFD_CLOEXEC;
    if (type == VKD3D_NATIVE_SYNC_HANDLE_TYPE_SEMAPHORE)
        flags |= EFD_SEMAPHORE;
    if ((handle->fd = eventfd(initial, flags)) < 0)
        hr = E_OUTOFMEMORY;
#endif
    handle->type = hr == S_OK ? type : VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE;
    return hr;
}

static inline void vkd3d_native_sync_handle_destroy(vkd3d_native_sync_handle handle)
{
    if (handle.type != VKD3D_NATIVE_SYNC_HANDLE_TYPE_NONE)
    {
#ifdef _WIN32
        CloseHandle(handle.handle);
#else
        close(handle.fd);
#endif
    }
}

#endif