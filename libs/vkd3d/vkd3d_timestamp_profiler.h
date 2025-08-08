/*
 * Copyright 2025 Hans-Kristian Arntzen for Valve Corporation
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

#ifndef __VKD3D_TIMESTAMP_PROFILER_H
#define __VKD3D_TIMESTAMP_PROFILER_H

#include "vkd3d_private.h"

#ifdef VKD3D_ENABLE_PROFILING

struct vkd3d_timestamp_profiler *vkd3d_timestamp_profiler_init(struct d3d12_device *device);
void vkd3d_timestamp_profiler_deinit(struct vkd3d_timestamp_profiler *profiler);

void vkd3d_timestamp_profiler_set_pipeline_state(
        struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_command_list *list,
        struct d3d12_pipeline_state *state);

void vkd3d_timestamp_profiler_register_pipeline_state(struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_pipeline_state *state);
/* Call before every action command which takes a shader. */
void vkd3d_timestamp_profiler_mark_pre_command(struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_command_list *list);

void vkd3d_timestamp_profiler_end_render_pass(struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_command_list *list);

/* Flush any pending end queries. */
void vkd3d_timestamp_profiler_end_command_buffer(struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_command_list *list);

/* Release allocated timestamp indices. */
void vkd3d_timestamp_profiler_reset_command_list(struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_command_list *list);

/* Queues up timestamps to be queried. */
void vkd3d_timestamp_profiler_submit_command_list(struct vkd3d_timestamp_profiler *profiler,
        struct d3d12_command_list *list);

void vkd3d_timestamp_profiler_mark_frame_boundary(struct vkd3d_timestamp_profiler *profiler);

#endif
#endif