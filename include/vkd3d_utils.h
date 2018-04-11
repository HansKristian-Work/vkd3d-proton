/*
 * Copyright 2016 Józef Kucia for CodeWeavers
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

#ifndef __VKD3D_UTILS_H
#define __VKD3D_UTILS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define WAIT_OBJECT_0 (0)
#define WAIT_TIMEOUT (1)
#define WAIT_FAILED (~0u)
#define INFINITE (~0u)

HANDLE vkd3d_create_event(void);
HRESULT vkd3d_signal_event(HANDLE event);
unsigned int vkd3d_wait_event(HANDLE event, unsigned int milliseconds);
void vkd3d_destroy_event(HANDLE event);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* __VKD3D_UTILS_H */
