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

#ifndef __VKD3D_STRING_H
#define __VKD3D_STRING_H

#include "vkd3d_common.h"
#include <stddef.h>

/* Various string utilities. */

WCHAR *vkd3d_dup_entry_point(const char *str);
WCHAR *vkd3d_dup_entry_point_n(const char *str, size_t len);
WCHAR *vkd3d_dup_demangled_entry_point(const char *str);

bool vkd3d_export_strequal(const WCHAR *a, const WCHAR *b);

char *vkd3d_strdup(const char *str);
WCHAR *vkd3d_wstrdup(const WCHAR *str);
WCHAR *vkd3d_wstrdup_n(const WCHAR *str, size_t n);

#endif /* __VKD3D_STRING_H */