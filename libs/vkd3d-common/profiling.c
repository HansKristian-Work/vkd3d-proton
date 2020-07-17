/*
 * Copyright 2020 Hans-Kristian Arntzen for Valve Corporation
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

#ifdef VKD3D_ENABLE_PROFILING

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API

#include "vkd3d_profiling.h"
#include "vkd3d_threads.h"
#include "vkd3d_debug.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#endif

static pthread_once_t profiling_block_once = PTHREAD_ONCE_INIT;
static unsigned int profiling_region_count;
static spinlock_t profiling_lock;

struct vkd3d_profiling_block
{
    uint64_t ticks_total;
    uint64_t iteration_total;
    char name[64 - 2 * sizeof(uint64_t)];
};

static struct vkd3d_profiling_block *mapped_blocks;

#define VKD3D_MAX_PROFILING_REGIONS 256
static spinlock_t region_locks[VKD3D_MAX_PROFILING_REGIONS];

#ifdef _WIN32
static void vkd3d_init_profiling_path(const char *path)
{
    HANDLE profiling_fd;
    HANDLE file_view;
    char path_pid[_MAX_PATH];

    snprintf(path_pid, sizeof(path_pid), "%s.%u", path, GetCurrentProcessId());
    profiling_fd = CreateFileA(path_pid, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ, NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, INVALID_HANDLE_VALUE);

    if (profiling_fd == INVALID_HANDLE_VALUE)
    {
        ERR("Failed to open profiling FD.\n");
        return;
    }

    file_view = CreateFileMappingA(profiling_fd, NULL, PAGE_READWRITE, 0,
            VKD3D_MAX_PROFILING_REGIONS * sizeof(*mapped_blocks), NULL);
    if (file_view == INVALID_HANDLE_VALUE)
    {
        ERR("Failed to create profiling file view.\n");
        CloseHandle(profiling_fd);
        return;
    }

    mapped_blocks = MapViewOfFile(file_view, FILE_MAP_ALL_ACCESS, 0, 0,
            VKD3D_MAX_PROFILING_REGIONS * sizeof(*mapped_blocks));
    if (!mapped_blocks)
        ERR("Failed to map view of file.\n");
    CloseHandle(file_view);
    CloseHandle(profiling_fd);
}
#else
static void vkd3d_init_profiling_path(const char *path)
{
    int profiling_fd;
    char path_pid[PATH_MAX];

    snprintf(path_pid, sizeof(path_pid), "%s.%u", path, getpid());
    profiling_fd = open(path_pid, O_RDWR | O_CREAT, 0644);

    if (profiling_fd >= 0)
    {
        if (ftruncate(profiling_fd, VKD3D_MAX_PROFILING_REGIONS * sizeof(*mapped_blocks)) < 0)
        {
            ERR("Failed to resize profiling FD.\n");
            close(profiling_fd);
            return;
        }
        mapped_blocks = mmap(NULL, VKD3D_MAX_PROFILING_REGIONS * sizeof(*mapped_blocks),
                PROT_READ | PROT_WRITE, MAP_SHARED, profiling_fd, 0);
        if (!mapped_blocks)
        {
            ERR("Failed to map block.\n");
            close(profiling_fd);
            return;
        }
        memset(mapped_blocks, 0, VKD3D_MAX_PROFILING_REGIONS * sizeof(*mapped_blocks));
        close(profiling_fd);
    }
    else
    {
        ERR("Failed to open profiling FD.\n");
    }
}
#endif

static void vkd3d_init_profiling_once(void)
{
    const char *path = getenv("VKD3D_PROFILE_PATH");
    if (path)
        vkd3d_init_profiling_path(path);
}

void vkd3d_init_profiling(void)
{
    pthread_once(&profiling_block_once, vkd3d_init_profiling_once);
}

bool vkd3d_uses_profiling(void)
{
    return mapped_blocks != NULL;
}

unsigned int vkd3d_profiling_register_region(const char *name, spinlock_t *lock, uint32_t *latch)
{
    unsigned int index;
    if (!mapped_blocks)
        return 0;

    spinlock_acquire(lock);

    if (*latch == 0)
    {
        spinlock_acquire(&profiling_lock);
        /* Begin at 1, 0 is reserved as a sentinel. */
        index = ++profiling_region_count;
        if (index <= VKD3D_MAX_PROFILING_REGIONS)
        {
            strncpy(mapped_blocks[index - 1].name, name, sizeof(mapped_blocks[index - 1].name) - 1);
            /* Important to store with release semantics after we've initialized the block. */
            vkd3d_atomic_uint32_store_explicit(latch, index, vkd3d_memory_order_release);
        }
        else
        {
            ERR("Too many profiling regions!\n");
            index = 0;
        }
        spinlock_release(&profiling_lock);
    }
    else
        index = *latch;

    spinlock_release(lock);
    return index;
}

void vkd3d_profiling_notify_work(unsigned int index,
        uint64_t start_ticks, uint64_t end_ticks,
        unsigned int iteration_count)
{
    struct vkd3d_profiling_block *block;
    spinlock_t *lock;

    if (index == 0 || index > VKD3D_MAX_PROFILING_REGIONS || !mapped_blocks)
        return;
    index--;

    lock = &region_locks[index];
    block = &mapped_blocks[index];

    spinlock_acquire(lock);
    block->iteration_total += iteration_count;
    block->ticks_total += end_ticks - start_ticks;
    spinlock_release(lock);
}

#endif /* VKD3D_ENABLE_PROFILING */
