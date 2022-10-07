/*
 * * Copyright 2021 NVIDIA Corporation
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
#ifndef __VKD3D_VK_INCLUDES_H
#define __VKD3D_VK_INCLUDES_H

#if defined(__LP64__) || defined(_WIN64) || (defined(__x86_64__) && !defined(__ILP32__) ) || defined(_M_X64) || defined(__ia64) || defined (_M_IA64) || defined(__aarch64__) || defined(__powerpc64__)
    typedef struct VkCuFunctionNVX_T *VkCuFunctionNVX;
    typedef struct VkCuModuleNVX_T *VkCuModuleNVX;
    typedef struct VkSurfaceKHR_T *VkSurfaceKHR;
#else
    typedef UINT64 VkCuFunctionNVX;
    typedef UINT64 VkCuModuleNVX;
    typedef UINT64 VkSurfaceKHR;
#endif 

typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkCommandBuffer_T *VkCommandBuffer;
typedef struct VkInstance_T *VkInstance;
typedef struct VkDevice_T *VkDevice;

typedef enum VkResult VkResult;

typedef enum D3D12_VK_EXTENSION
{
    D3D12_VK_NVX_BINARY_IMPORT      = 0x1,
    D3D12_VK_NVX_IMAGE_VIEW_HANDLE  = 0x2
} D3D12_VK_EXTENSION;

typedef struct D3D12_CUBIN_DATA_HANDLE
{
    VkCuFunctionNVX vkCuFunction;
    VkCuModuleNVX vkCuModule;
    UINT32 blockX;
    UINT32 blockY;
    UINT32 blockZ;
} D3D12_CUBIN_DATA_HANDLE;

typedef struct D3D12_UAV_INFO
{
    UINT32 version;
    UINT32 surfaceHandle;
    UINT64 gpuVAStart;
    UINT64 gpuVASize;  
} D3D12_UAV_INFO;

#endif  // __VKD3D_VK_INCLUDES_H

