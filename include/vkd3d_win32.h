/*
 * Copyright 2020 Joshua Ashton for Valve Software
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
 *
 */

#ifndef __VKD3D_WIN32_H
#define __VKD3D_WIN32_H

/* Hack for MinGW-w64 headers.
 *
 * We want to use WIDL C inline wrappers because some methods
 * in D3D12 interfaces return aggregate objects. Unfortunately,
 * WIDL C inline wrappers are broken when used with MinGW-w64
 * headers because FORCEINLINE expands to extern inline
 * which leads to the "multiple storage classes in declaration
 * specifiers" compiler error.
 *
 * This hack will define static to be meaningless when these
 * headers are included, which are the only things declared
 * static.
 */
#ifdef __MINGW32__
# define static
#endif

#define INITGUID
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <vkd3d_windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <dxgi1_6.h>

/* We already included regular DXGI...
 * let's not redefine everything under a new header
 */
#define __vkd3d_dxgibase_h__
#define __vkd3d_dxgi_h__
#define __vkd3d_dxgi1_2_h__
#define __vkd3d_dxgi1_3_h__
#define __vkd3d_dxgi1_4_h__

#include <vkd3d_swapchain_factory.h>
#include <vkd3d_d3d12.h>
#include <vkd3d_d3d12sdklayers.h>
#include <vkd3d_dxvk_interop.h>

/* End of MinGW hack. All Windows headers have been included */
#ifdef __MINGW32__
# undef static
#endif

#define VKD3D_NO_WIN32_TYPES
#define VKD3D_NO_VULKAN_H
#include <vkd3d.h>

#endif
