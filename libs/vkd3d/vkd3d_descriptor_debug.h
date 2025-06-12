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
#include "vkd3d_descriptor_qa_data.h"

/* Cost is 1 bit per cookie, and spending 256 MB of host memory on this is reasonable,
 * and overflowing this pool should never happen. */
#define VKD3D_DESCRIPTOR_DEBUG_DEFAULT_NUM_COOKIES (2 * 1000 * 1000 * 1000)
#define VKD3D_DESCRIPTOR_DEBUG_NUM_PAD_DESCRIPTORS 1

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
static inline VkDeviceAddress vkd3d_descriptor_debug_encode_buffer_va(VkDeviceAddress va, uint32_t elem_size)
{
    /* Assume that only lower 48 bits of VA are meaningful. Encode metadata in upper 16 bits.
     * This is currently only useful for texel buffer stride. */
    return (va & ((1ull << 48) - 1)) | ((uint64_t)elem_size << 48);
}

/* For sync-validation */
void vkd3d_descriptor_debug_sync_validation_barrier(
        struct vkd3d_descriptor_qa_global_info *global_info,
        struct d3d12_device *device, VkCommandBuffer vk_cmd_buffer);
uint32_t vkd3d_descriptor_debug_clear_bloom_filter(
        struct vkd3d_descriptor_qa_global_info *global_info,
        struct d3d12_device *device, VkCommandBuffer vk_cmd_buffer);

HRESULT vkd3d_descriptor_debug_alloc_global_info(
        struct vkd3d_descriptor_qa_global_info **global_info,
        unsigned int num_cookies,
        struct d3d12_device *device);
void vkd3d_descriptor_debug_free_global_info(
        struct vkd3d_descriptor_qa_global_info *global_info,
        struct d3d12_device *device);

void vkd3d_descriptor_debug_kick_qa_check(struct vkd3d_descriptor_qa_global_info *global_info);

const VkDescriptorBufferInfo *vkd3d_descriptor_debug_get_payload_info_descriptor(
        struct vkd3d_descriptor_qa_global_info *global_info);
const VkDescriptorBufferInfo *vkd3d_descriptor_debug_get_control_info_descriptor(
        struct vkd3d_descriptor_qa_global_info *global_info);

uint32_t vkd3d_descriptor_debug_get_shader_interface_flags(
        struct vkd3d_descriptor_qa_global_info *global_info,
        const void *code, size_t size);

void vkd3d_descriptor_debug_init(void);
bool vkd3d_descriptor_debug_active_log(void);
bool vkd3d_descriptor_debug_active_instruction_qa_checks(void);
bool vkd3d_descriptor_debug_active_descriptor_qa_checks(void);

void vkd3d_descriptor_debug_register_heap(
        struct vkd3d_descriptor_qa_heap_buffer_data *heap, uint64_t cookie,
        const D3D12_DESCRIPTOR_HEAP_DESC *desc);
void vkd3d_descriptor_debug_unregister_heap(uint64_t cookie);

void vkd3d_descriptor_debug_register_resource_cookie(
        struct vkd3d_descriptor_qa_global_info *global_info,
        uint64_t cookie, const D3D12_RESOURCE_DESC1 *desc);
void vkd3d_descriptor_debug_register_query_heap_cookie(
        struct vkd3d_descriptor_qa_global_info *global_info,
        uint64_t cookie, const D3D12_QUERY_HEAP_DESC *desc);
void vkd3d_descriptor_debug_register_allocation_cookie(
        struct vkd3d_descriptor_qa_global_info *global_info,
        uint64_t cookie, const struct vkd3d_allocate_memory_info *info);
void vkd3d_descriptor_debug_register_view_cookie(
        struct vkd3d_descriptor_qa_global_info *global_info,
        uint64_t cookie, uint64_t resource_cookie);
void vkd3d_descriptor_debug_unregister_cookie(
        struct vkd3d_descriptor_qa_global_info *global_info,
        uint64_t cookie);

void vkd3d_descriptor_debug_write_descriptor(
        struct vkd3d_descriptor_qa_heap_buffer_data *heap, uint64_t heap_cookie, uint32_t offset,
        vkd3d_descriptor_qa_flags type_flags, uint64_t cookie);
void vkd3d_descriptor_debug_copy_descriptor(
        struct vkd3d_descriptor_qa_heap_buffer_data *dst_heap, uint64_t dst_heap_cookie, uint32_t dst_offset,
        struct vkd3d_descriptor_qa_heap_buffer_data *src_heap, uint64_t src_heap_cookie, uint32_t src_offset,
        uint64_t cookie);

VkDeviceSize vkd3d_descriptor_debug_heap_info_size(unsigned int num_descriptors);
#else
#define vkd3d_descriptor_debug_alloc_global_info(global_info, num_cookies, device) (S_OK)
#define vkd3d_descriptor_debug_free_global_info(global_info, device) ((void)0)
#define vkd3d_descriptor_debug_kick_qa_check(global_info) ((void)0)
#define vkd3d_descriptor_debug_get_payload_info_descriptor(global_info) ((const VkDescriptorBufferInfo *)NULL)
#define vkd3d_descriptor_debug_get_control_info_descriptor(global_info) ((const VkDescriptorBufferInfo *)NULL)
#define vkd3d_descriptor_debug_init() ((void)0)
#define vkd3d_descriptor_debug_active_log() ((void)0)
#define vkd3d_descriptor_debug_active_instruction_qa_checks() (false)
#define vkd3d_descriptor_debug_active_descriptor_qa_checks() (false)
#define vkd3d_descriptor_debug_register_heap(heap, cookie, desc) ((void)0)
#define vkd3d_descriptor_debug_unregister_heap(cookie) ((void)0)
#define vkd3d_descriptor_debug_register_resource_cookie(global_info, cookie, desc) ((void)0)
#define vkd3d_descriptor_debug_register_query_heap_cookie(global_info, cookie, desc) ((void)0)
#define vkd3d_descriptor_debug_register_allocation_cookie(global_info, cookie, info) ((void)0)
#define vkd3d_descriptor_debug_register_view_cookie(global_info, cookie, resource_cookie) ((void)0)
#define vkd3d_descriptor_debug_unregister_cookie(global_info, cookie) ((void)0)
#define vkd3d_descriptor_debug_write_descriptor(heap, heap_cookie, offset, type_flags, cookie) ((void)0)
#define vkd3d_descriptor_debug_copy_descriptor(dst_heap, dst_heap_cookie, dst_offset, src_heap, src_heap_cookie, src_offset, cookie) ((void)0)
#define vkd3d_descriptor_debug_heap_info_size(num_descriptors) 0
#define vkd3d_descriptor_debug_get_shader_interface_flags(global_info, code, size) 0
#define vkd3d_descriptor_debug_sync_validation_barrier(global_info, device, vk_cmd_buffer) ((void)0)
#define vkd3d_descriptor_debug_clear_bloom_filter(global_info, device, vk_cmd_buffer) (0)
#endif

#endif
