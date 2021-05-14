/*
 * Copyright 2021 Hans-Kristian Arntzen for Valve Corporation
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

#ifndef __VKD3D_DESCRIPTOR_QA_DATA_H
#define __VKD3D_DESCRIPTOR_QA_DATA_H

#include <stdint.h>

/* Data types which are used by shader backends when emitting code. */

enum vkd3d_descriptor_qa_flag_bits
{
    VKD3D_DESCRIPTOR_QA_TYPE_NONE_BIT = 0,
    VKD3D_DESCRIPTOR_QA_TYPE_SAMPLED_IMAGE_BIT = 1 << 0,
    VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_IMAGE_BIT = 1 << 1,
    VKD3D_DESCRIPTOR_QA_TYPE_UNIFORM_BUFFER_BIT = 1 << 2,
    VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_BUFFER_BIT = 1 << 3,
    VKD3D_DESCRIPTOR_QA_TYPE_UNIFORM_TEXEL_BUFFER_BIT = 1 << 4,
    VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_TEXEL_BUFFER_BIT = 1 << 5,
    VKD3D_DESCRIPTOR_QA_TYPE_RT_ACCELERATION_STRUCTURE_BIT = 1 << 6,
    VKD3D_DESCRIPTOR_QA_TYPE_SAMPLER_BIT = 1 << 7,
    VKD3D_DESCRIPTOR_QA_TYPE_RAW_VA_BIT = 1 << 8
};
typedef uint32_t vkd3d_descriptor_qa_flags;

struct vkd3d_descriptor_qa_cookie_descriptor
{
    uint32_t cookie;
    uint32_t descriptor_type;
};

enum vkd3d_descriptor_debug_fault_type
{
    VKD3D_DESCRIPTOR_FAULT_TYPE_HEAP_OF_OF_RANGE = 1 << 0,
    VKD3D_DESCRIPTOR_FAULT_TYPE_MISMATCH_DESCRIPTOR_TYPE = 1 << 1,
    VKD3D_DESCRIPTOR_FAULT_TYPE_DESTROYED_RESOURCE = 1 << 2
};

/* Physical layout of QA buffer. */
struct vkd3d_descriptor_qa_global_buffer_data
{
    uint64_t failed_hash;
    uint32_t failed_offset;
    uint32_t failed_heap;
    uint32_t failed_cookie;
    uint32_t fault_atomic;
    uint32_t failed_instruction;
    uint32_t failed_descriptor_type_mask;
    uint32_t actual_descriptor_type_mask;
    uint32_t fault_type;
    uint32_t live_status_table[];
};

/* Physical layout of QA heap buffer. */
struct vkd3d_descriptor_qa_heap_buffer_data
{
    uint32_t num_descriptors;
    uint32_t heap_index;
    struct vkd3d_descriptor_qa_cookie_descriptor desc[];
};

#endif