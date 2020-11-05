/*
 * Copyright 2020 Joshua Ashton for Valve Corporation
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

#ifndef __VKD3D_PLATFORM_H
#define __VKD3D_PLATFORM_H

#include "vkd3d_common.h"

#if defined(_WIN32)
#define VKD3D_PATH_MAX _MAX_PATH
#else
#define VKD3D_PATH_MAX PATH_MAX
#endif

typedef void* vkd3d_module_t;

vkd3d_module_t vkd3d_dlopen(const char *name);

void *vkd3d_dlsym(vkd3d_module_t handle, const char *symbol);

int vkd3d_dlclose(vkd3d_module_t handle);

const char *vkd3d_dlerror(void);

bool vkd3d_get_program_name(char program_name[VKD3D_PATH_MAX]);

#endif
