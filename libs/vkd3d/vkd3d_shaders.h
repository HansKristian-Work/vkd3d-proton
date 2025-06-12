/*
 * Copyright 2019 Philip Rebohle
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

#ifndef __VKD3D_SPV_SHADERS_H
#define __VKD3D_SPV_SHADERS_H

enum vkd3d_meta_copy_mode
{
    VKD3D_META_COPY_MODE_1D = 0,
    VKD3D_META_COPY_MODE_2D = 1,
    VKD3D_META_COPY_MODE_MS = 2,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_META_COPY_MODE),
};

#include <cs_clear_uav_buffer_float.h>
#include <cs_clear_uav_buffer_uint.h>
#include <cs_clear_uav_buffer_raw.h>
#include <cs_clear_uav_image_1d_array_float.h>
#include <cs_clear_uav_image_1d_array_uint.h>
#include <cs_clear_uav_image_1d_float.h>
#include <cs_clear_uav_image_1d_uint.h>
#include <cs_clear_uav_image_2d_array_float.h>
#include <cs_clear_uav_image_2d_array_uint.h>
#include <cs_clear_uav_image_2d_float.h>
#include <cs_clear_uav_image_2d_uint.h>
#include <cs_clear_uav_image_3d_float.h>
#include <cs_clear_uav_image_3d_uint.h>
#include <cs_predicate_command.h>
#include <cs_predicate_command_execute_indirect.h>
#include <cs_resolve_binary_queries.h>
#include <cs_resolve_predicate.h>
#include <cs_resolve_query.h>
#include <cs_emit_nv_memory_decompression_regions.h>
#include <cs_emit_nv_memory_decompression_workgroups.h>
#include <cs_execute_indirect_patch.h>
#include <cs_execute_indirect_patch_debug_ring.h>
#include <cs_execute_indirect_multi_dispatch.h>
#include <cs_execute_indirect_multi_dispatch_state.h>
#include <vs_fullscreen_layer.h>
#include <vs_fullscreen.h>
#include <gs_fullscreen.h>
#include <fs_copy_image_float.h>
#include <fs_copy_image_uint.h>
#include <fs_copy_image_stencil.h>
#include <fs_copy_image_stencil_no_export.h>
#include <vs_swapchain_fullscreen.h>
#include <fs_swapchain_fullscreen.h>
#include <cs_sampler_feedback_decode_buffer_min_mip.h>
#include <fs_sampler_feedback_decode_image_min_mip.h>
#include <fs_sampler_feedback_decode_image_mip_used.h>
#include <cs_sampler_feedback_encode_buffer_min_mip.h>
#include <cs_sampler_feedback_encode_image_min_mip.h>
#include <cs_sampler_feedback_encode_image_mip_used.h>
#include <fs_resolve_color_float.h>
#include <fs_resolve_color_sint.h>
#include <fs_resolve_color_uint.h>
#include <fs_resolve_depth.h>
#include <fs_resolve_stencil.h>
#include <fs_resolve_stencil_no_export.h>
#include <cs_resolve_color_float.h>
#include <cs_resolve_color_sint.h>
#include <cs_resolve_color_uint.h>
#include <cs_workgraph_distribute_workgroups.h>
#include <cs_workgraph_distribute_payload_offsets.h>
#include <cs_workgraph_complete_compaction.h>
#include <cs_workgraph_setup_gpu_input.h>
#include <cs_gdeflate.h>
#include <cs_gdeflate_prepare.h>

#endif  /* __VKD3D_SPV_SHADERS_H */
