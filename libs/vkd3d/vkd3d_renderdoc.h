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

#ifndef __VKD3D_RENDERDOC_H
#define __VKD3D_RENDERDOC_H

#include "vkd3d_private.h"
#include "vkd3d_shader.h"
#include "vkd3d_common.h"

bool vkd3d_renderdoc_active(void);
bool vkd3d_renderdoc_loaded_api(void);
bool vkd3d_renderdoc_should_capture_shader_hash(vkd3d_shader_hash_t hash);
bool vkd3d_renderdoc_global_capture_enabled(void);

bool vkd3d_renderdoc_begin_capture(void *instance);
void vkd3d_renderdoc_end_capture(void *instance);

void vkd3d_renderdoc_init(void);

void vkd3d_renderdoc_command_list_check_capture(struct d3d12_command_list *list,
        struct d3d12_pipeline_state *state);
bool vkd3d_renderdoc_command_queue_begin_capture(struct d3d12_command_queue *command_queue);
void vkd3d_renderdoc_command_queue_end_capture(struct d3d12_command_queue *command_queue);

#endif
