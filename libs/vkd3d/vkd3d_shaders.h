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
#include <cs_resolve_binary_queries.h>
#include <cs_resolve_predicate.h>
#include <cs_resolve_query.h>
#include <vs_fullscreen_layer.h>
#include <vs_fullscreen.h>
#include <gs_fullscreen.h>
#include <fs_copy_image_float.h>
#include <vs_swapchain_fullscreen.h>
#include <fs_swapchain_fullscreen.h>

#endif  /* __VKD3D_SPV_SHADERS_H */
