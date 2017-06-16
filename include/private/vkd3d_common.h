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

#ifndef __VKD3D_COMMON_H
#define __VKD3D_COMMON_H

#include "config.h"
#include "vkd3d_windows.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))

static inline size_t align(size_t addr, size_t alignment)
{
    return (addr + (alignment - 1)) & ~(alignment - 1);
}

#ifdef __GNUC__
# define VKD3D_PRINTF_FUNC(fmt, args) __attribute__((format(printf, fmt, args)))
# define VKD3D_UNUSED __attribute__((unused))
#else
# define VKD3D_PRINTF_FUNC(fmt, args)
# define VKD3D_UNUSED
#endif  /* __GNUC__ */

#ifndef _WIN32
# if HAVE_SYNC_ADD_AND_FETCH
static inline LONG InterlockedIncrement(LONG volatile *x)
{
    return __sync_add_and_fetch(x, 1);
}
# else
#  error "InterlockedIncrement not implemented for this platform"
# endif  /* HAVE_SYNC_ADD_AND_FETCH */

# if HAVE_SYNC_SUB_AND_FETCH
static inline LONG InterlockedDecrement(LONG volatile *x)
{
    return __sync_sub_and_fetch(x, 1);
}
# else
#  error "InterlockedDecrement not implemented for this platform"
# endif
#endif  /* _WIN32 */

#endif  /* __VKD3D_COMMON_H */
