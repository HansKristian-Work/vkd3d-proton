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

#ifndef __VKD3D_SONAMES_H
#define __VKD3D_SONAMES_H

/* These sonames are defined by the loader ABI. */

#if defined(_WIN32)
#define SONAME_LIBVULKAN "vulkan-1.dll"
#elif defined(__linux__)
#define SONAME_LIBVULKAN "libvulkan.so.1"
#elif defined(__APPLE__)
#define SONAME_LIBVULKAN "libvulkan.1.dylib"
#else
#error "Unrecognized platform."
#endif

#endif

