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

#ifndef __VKD3D_DESCRIPTOR_DEBUG_H
#define __VKD3D_DESCRIPTOR_DEBUG_H

#include "vkd3d_private.h"

void vkd3d_descriptor_debug_init(void);
bool vkd3d_descriptor_debug_active(void);

void vkd3d_descriptor_debug_register_heap(void *heap, const D3D12_DESCRIPTOR_HEAP_DESC *desc);
void vkd3d_descriptor_debug_unregister_heap(void *heap);

void vkd3d_descriptor_debug_register_resource_cookie(uint64_t cookie, const D3D12_RESOURCE_DESC *desc);
void vkd3d_descriptor_debug_register_allocation_cookie(uint64_t cookie, const struct vkd3d_allocate_memory_info *info);
void vkd3d_descriptor_debug_register_view_cookie(uint64_t cookie, uint64_t resource_cookie);
void vkd3d_descriptor_debug_unregister_cookie(uint64_t cookie);

void vkd3d_descriptor_debug_write_descriptor(void *heap, uint32_t offset, VkDescriptorType type, uint64_t cookie);
void vkd3d_descriptor_debug_copy_descriptor(void *dst_heap, uint32_t dst_offset,
        void *src_heap, uint32_t src_offset, uint64_t cookie);

#endif
