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

typedef struct VkPhysicalDeviceFeatures2 VkPhysicalDeviceFeatures2;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkCommandBuffer_T *VkCommandBuffer;
typedef struct VkInstance_T *VkInstance;
typedef struct VkDevice_T *VkDevice;
typedef struct VkQueue_T *VkQueue;

typedef enum VkFormat VkFormat;
typedef enum VkResult VkResult;
typedef enum VkImageLayout VkImageLayout;

typedef enum D3D12_VK_EXTENSION
{
    D3D12_VK_NVX_BINARY_IMPORT      = 0x1,
    D3D12_VK_NVX_IMAGE_VIEW_HANDLE  = 0x2,
    D3D12_VK_NV_LOW_LATENCY_2       = 0x3,
    D3D12_VK_NV_OPTICAL_FLOW        = 0x4
} D3D12_VK_EXTENSION;

typedef enum D3D12_OUT_OF_BAND_CQ_TYPE
{
    OUT_OF_BAND_RENDER  = 0,
    OUT_OF_BAND_PRESENT = 1
} D3D12_OUT_OF_BAND_CQ_TYPE;

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

typedef struct D3D12_FRAME_REPORT
{
    UINT64 frameID;
    UINT64 inputSampleTime;
    UINT64 simStartTime;
    UINT64 simEndTime;
    UINT64 renderSubmitStartTime;
    UINT64 renderSubmitEndTime;
    UINT64 presentStartTime;
    UINT64 presentEndTime;
    UINT64 driverStartTime;
    UINT64 driverEndTime;
    UINT64 osRenderQueueStartTime;
    UINT64 osRenderQueueEndTime;
    UINT64 gpuRenderStartTime;
    UINT64 gpuRenderEndTime;
    UINT32 gpuActiveRenderTimeUs;
    UINT32 gpuFrameTimeUs;
    UINT8 rsvd[120];
} D3D12_FRAME_REPORT;

typedef struct D3D12_LATENCY_RESULTS
{
    UINT32 version;
    D3D12_FRAME_REPORT frame_reports[64];
    UINT8 rsvd[32];
} D3D12_LATENCY_RESULTS;

typedef struct D3D12_CREATE_CUBIN_SHADER_PARAMS
{
    void *pNext;
    const void *pCubin;
    UINT32 size;
    UINT32 blockX;
    UINT32 blockY;
    UINT32 blockZ;
    UINT32 dynSharedMemBytes;
    const char *pShaderName;
    UINT32 flags;
    D3D12_CUBIN_DATA_HANDLE *hShader;
} D3D12_CREATE_CUBIN_SHADER_PARAMS;

typedef struct D3D12_GET_CUDA_MERGED_TEXTURE_SAMPLER_OBJECT_PARAMS
{
    void *pNext;
    SIZE_T texDesc;
    SIZE_T smpDesc;
    UINT64 textureHandle;
} D3D12_GET_CUDA_MERGED_TEXTURE_SAMPLER_OBJECT_PARAMS;

typedef enum D3D12_GET_CUDA_INDEPENDENT_DESCRIPTOR_OBJECT_TYPE
{
    D3D12_GET_CUDA_INDEPENDENT_DESCRIPTOR_OBJECT_SURFACE = 0,
    D3D12_GET_CUDA_INDEPENDENT_DESCRIPTOR_OBJECT_TEXTURE = 1,
    D3D12_GET_CUDA_INDEPENDENT_DESCRIPTOR_OBJECT_SAMPLER = 2,
} D3D12_GET_CUDA_INDEPENDENT_DESCRIPTOR_OBJECT_TYPE;

typedef struct D3D12_GET_CUDA_INDEPENDENT_DESCRIPTOR_OBJECT_PARAMS
{
    void *pNext;
    D3D12_GET_CUDA_INDEPENDENT_DESCRIPTOR_OBJECT_TYPE type;
    SIZE_T desc;
    UINT64 handle;
} D3D12_GET_CUDA_INDEPENDENT_DESCRIPTOR_OBJECT_PARAMS;

#endif  // __VKD3D_VK_INCLUDES_H

