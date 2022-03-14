/*
 * Copyright 2022 Hans-Kristian Arntzen for Valve Corporation
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

#ifndef __VKD3D_FILE_UTILS_H
#define __VKD3D_FILE_UTILS_H

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

struct vkd3d_memory_mapped_file
{
    void *mapped;
    size_t mapped_size;
};

/* On failure, ensures the struct is cleared to zero.
 * A reference to the file is kept through the memory mapping. */
bool vkd3d_file_map_read_only(const char *path, struct vkd3d_memory_mapped_file *file);
/* Clears out file on unmap. */
void vkd3d_file_unmap(struct vkd3d_memory_mapped_file *file);
bool vkd3d_file_rename_overwrite(const char *from_path, const char *to_path);
bool vkd3d_file_rename_no_replace(const char *from_path, const char *to_path);
bool vkd3d_file_delete(const char *path);
FILE *vkd3d_file_open_exclusive_write(const char *path);

#endif
