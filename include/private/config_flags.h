/*
* Copyright 2026 Hans-Kristian Arntzen for Valve Corporation
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

#ifndef VKD3D_CONFIG_FLAGS_H_
#define VKD3D_CONFIG_FLAGS_H_

struct vkd3d_config_flags_bitfield
{
#define VKD3D_DECL_CONFIG(name, CONF) uint32_t CONF : 1;
#define VKD3D_DECL_CONFIG_PLAIN(CONF) uint32_t CONF : 1;
#include "config_flag_decl.h"
#undef VKD3D_DECL_CONFIG
#undef VKD3D_DECL_CONFIG_PLAIN
	/* With designated initializers, we will be initializing only this union portion.
	 * If the size isn't padded out fully,
	 * we may end up with stray uninitialized bits which can subtly break bitwise operations later.
	 * Adding more configs will cause the static assert below to fail,
	 * which indicates the need to subtract a reserved bit. */
	uint32_t reserved0 : 32;
};

STATIC_ASSERT(sizeof(struct vkd3d_config_flags_bitfield) == 12);

union vkd3d_config_flags
{
	struct vkd3d_config_flags_bitfield bits;
    uint32_t words[3]; /* If we exhaust the limits again, keep adding words. */
};

/* static const initializers in C cannot be used as part of other constant expressions, hnnnnnng.
 * Need macros to aid inlining them. */
#define VKD3D_CONFIG_FLAG(CONF) ((union vkd3d_config_flags) { .bits = { .CONF = 1 } })
#define VKD3D_CONFIG_FLAG_INIT(...) ((union vkd3d_config_flags) { .bits = { __VA_ARGS__ } })
#define VKD3D_CONFIG_FLAGS_NONE ((union vkd3d_config_flags) { .bits = { } })

extern union vkd3d_config_flags vkd3d_config_flags;

static inline bool vkd3d_config_flag_overlaps(union vkd3d_config_flags a, union vkd3d_config_flags b)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(a.words); i++)
		if (a.words[i] & b.words[i])
			return true;
	return false;
}

static inline bool vkd3d_config_flag_is_set(union vkd3d_config_flags flags)
{
	return vkd3d_config_flag_overlaps(vkd3d_config_flags, flags);
}

#define VKD3D_CONFIG_FLAG_IS_SET(CONFIG) vkd3d_config_flag_is_set(VKD3D_CONFIG_FLAG(CONFIG))

static inline void vkd3d_config_flag_global_add(union vkd3d_config_flags flags)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(flags.words); i++)
		vkd3d_config_flags.words[i] |= flags.words[i];
}

static inline void vkd3d_config_flag_add(union vkd3d_config_flags *target, union vkd3d_config_flags flags)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(flags.words); i++)
		target->words[i] |= flags.words[i];
}

static inline void vkd3d_config_flag_global_remove(union vkd3d_config_flags flags)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(flags.words); i++)
		vkd3d_config_flags.words[i] &= ~flags.words[i];
}

static inline bool vkd3d_config_flag_is_nonzero(union vkd3d_config_flags flags)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(flags.words); i++)
		if (flags.words[i])
			return true;
	return false;
}

static inline unsigned int vkd3d_config_flag_popcount(union vkd3d_config_flags flags)
{
	unsigned int count = 0;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(flags.words); i++)
		count += vkd3d_popcount(flags.words[i]);
	return count;
}

#endif