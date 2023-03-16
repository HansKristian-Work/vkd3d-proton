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

#define INITGUID
#define COBJMACROS
#include <vkd3d_windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "private/vulkan_private_extensions.h"

#include <dxgi1_6.h>

/* We already included regular DXGI...
 * let's not redefine everything under a new header
 */
#define __vkd3d_dxgibase_h__
#define __vkd3d_dxgi_h__
#define __vkd3d_dxgi1_2_h__
#define __vkd3d_dxgi1_3_h__
#define __vkd3d_dxgi1_4_h__
#define __vkd3d_dxgi1_5_h__

#include <vkd3d_swapchain_factory.h>
#include <vkd3d_command_list_vkd3d_ext.h>
#include <vkd3d_device_vkd3d_ext.h>
#include <vkd3d_d3d12.h>
#include <vkd3d_d3d12sdklayers.h>

#define VKD3D_NO_WIN32_TYPES
#define VKD3D_NO_VULKAN_H
#include <vkd3d.h>

#endif
