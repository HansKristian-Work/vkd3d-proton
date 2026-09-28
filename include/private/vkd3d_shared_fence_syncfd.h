/*
 * Copyright 2026 Hans-Kristian Arntzen for Valve Corporation
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

#ifndef __VKD3D_SHARED_FENCE_SYNCFD_H
#define __VKD3D_SHARED_FENCE_SYNCFD_H

#include "vkd3d.h"

typedef struct kmt_device_opaque *kmt_device;
typedef struct kmt_handle_opaque *kmt_handle;

kmt_device kmt_device_create(void);
void kmt_device_destroy(kmt_device device);

kmt_handle kmt_device_create_fence(kmt_device device, uint64_t initial_value);
void kmt_device_destroy_fence(kmt_device device, kmt_handle fence);

/* ID3D12Fence::Signal() */
void kmt_device_signal_fence_immediate(kmt_device device, kmt_handle fence, uint64_t value);

/* Called after a submit. Signal a binary semaphore, then export SYNC_FD payload. */
bool kmt_device_register_sync_file(kmt_device device, kmt_handle fence, int fd, uint64_t value);

/* Immediately queries the value from shmem. */
uint64_t kmt_device_query_fence(kmt_device device, kmt_handle fence);

/* If eventfd -1, we intend to start a GPU wait later via edge_wait_materialization */
bool kmt_device_fence_register_edge(kmt_device device, kmt_handle fence, uint64_t value, int eventfd, uint64_t *edge);

bool kmt_device_fence_edge_wait_materialization(kmt_device device, kmt_handle fence, uint64_t edge, int *sync_fd);

void kmt_device_fence_cancel_edge(kmt_device device, kmt_handle fence, uint64_t edge);

#endif
