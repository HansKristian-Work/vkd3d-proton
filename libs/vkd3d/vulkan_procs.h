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

#ifndef VK_INSTANCE_PFN
# define VK_INSTANCE_PFN(x)
#endif

#ifndef VK_INSTANCE_EXT_PFN
# define VK_INSTANCE_EXT_PFN(x)
#endif

#ifndef VK_DEVICE_PFN
# define VK_DEVICE_PFN(x)
#endif

#ifndef VK_DEVICE_EXT_PFN
# define VK_DEVICE_EXT_PFN(x)
#endif

/* Instance functions (obtained by vkGetInstanceProcAddr). */
VK_INSTANCE_PFN(vkDestroyInstance) /* Load vkDestroyInstance() first. */
VK_INSTANCE_PFN(vkCreateDevice)
VK_INSTANCE_PFN(vkEnumerateDeviceExtensionProperties)
VK_INSTANCE_PFN(vkEnumerateDeviceLayerProperties)
VK_INSTANCE_PFN(vkEnumeratePhysicalDevices)
VK_INSTANCE_PFN(vkGetDeviceProcAddr)
VK_INSTANCE_PFN(vkGetPhysicalDeviceFeatures)
VK_INSTANCE_PFN(vkGetPhysicalDeviceFormatProperties)
VK_INSTANCE_PFN(vkGetPhysicalDeviceFormatProperties2)
VK_INSTANCE_PFN(vkGetPhysicalDeviceImageFormatProperties)
VK_INSTANCE_PFN(vkGetPhysicalDeviceMemoryProperties)
VK_INSTANCE_PFN(vkGetPhysicalDeviceProperties)
VK_INSTANCE_PFN(vkGetPhysicalDeviceQueueFamilyProperties)
VK_INSTANCE_PFN(vkGetPhysicalDeviceSparseImageFormatProperties)
VK_INSTANCE_PFN(vkGetPhysicalDeviceFeatures2)
VK_INSTANCE_PFN(vkGetPhysicalDeviceProperties2)

/* VK_EXT_debug_utils */
VK_INSTANCE_EXT_PFN(vkCreateDebugUtilsMessengerEXT)
VK_INSTANCE_EXT_PFN(vkDestroyDebugUtilsMessengerEXT)
VK_DEVICE_EXT_PFN(vkQueueInsertDebugUtilsLabelEXT)

/* Device functions (obtained by vkGetDeviceProcAddr). */
VK_DEVICE_PFN(vkDestroyDevice) /* Load vkDestroyDevice() first. */
VK_DEVICE_PFN(vkAllocateCommandBuffers)
VK_DEVICE_PFN(vkAllocateDescriptorSets)
VK_DEVICE_PFN(vkAllocateMemory)
VK_DEVICE_PFN(vkBeginCommandBuffer)
VK_DEVICE_PFN(vkCmdBeginQuery)
VK_DEVICE_PFN(vkCmdBindDescriptorSets)
VK_DEVICE_PFN(vkCmdBindIndexBuffer)
VK_DEVICE_PFN(vkCmdBindPipeline)
VK_DEVICE_PFN(vkCmdBindVertexBuffers)
VK_DEVICE_PFN(vkCmdClearAttachments)
VK_DEVICE_PFN(vkCmdClearColorImage)
VK_DEVICE_PFN(vkCmdClearDepthStencilImage)
VK_DEVICE_PFN(vkCmdCopyQueryPoolResults)
VK_DEVICE_PFN(vkCmdDispatch)
VK_DEVICE_PFN(vkCmdDispatchIndirect)
VK_DEVICE_PFN(vkCmdDraw)
VK_DEVICE_PFN(vkCmdDrawIndexed)
VK_DEVICE_PFN(vkCmdDrawIndexedIndirect)
VK_DEVICE_PFN(vkCmdDrawIndirect)
VK_DEVICE_PFN(vkCmdEndQuery)
VK_DEVICE_PFN(vkCmdExecuteCommands)
VK_DEVICE_PFN(vkCmdFillBuffer)
VK_DEVICE_PFN(vkCmdNextSubpass)
VK_DEVICE_PFN(vkCmdPipelineBarrier)
VK_DEVICE_PFN(vkCmdPushConstants)
VK_DEVICE_PFN(vkCmdResetEvent)
VK_DEVICE_PFN(vkCmdResetQueryPool)
VK_DEVICE_PFN(vkCmdSetBlendConstants)
VK_DEVICE_PFN(vkCmdSetDepthBias)
VK_DEVICE_PFN(vkCmdSetDepthBounds)
VK_DEVICE_PFN(vkCmdSetEvent)
VK_DEVICE_PFN(vkCmdSetLineWidth)
VK_DEVICE_PFN(vkCmdSetScissor)
VK_DEVICE_PFN(vkCmdSetStencilCompareMask)
VK_DEVICE_PFN(vkCmdSetStencilReference)
VK_DEVICE_PFN(vkCmdSetStencilWriteMask)
VK_DEVICE_PFN(vkCmdSetViewport)
VK_DEVICE_PFN(vkCmdUpdateBuffer)
VK_DEVICE_PFN(vkCmdWaitEvents)
VK_DEVICE_PFN(vkCmdWriteTimestamp)
VK_DEVICE_PFN(vkCreateBuffer)
VK_DEVICE_PFN(vkCreateBufferView)
VK_DEVICE_PFN(vkCreateCommandPool)
VK_DEVICE_PFN(vkCreateComputePipelines)
VK_DEVICE_PFN(vkCreateDescriptorPool)
VK_DEVICE_PFN(vkCreateDescriptorSetLayout)
VK_DEVICE_PFN(vkCreateEvent)
VK_DEVICE_PFN(vkCreateFence)
VK_DEVICE_PFN(vkCreateFramebuffer)
VK_DEVICE_PFN(vkCreateGraphicsPipelines)
VK_DEVICE_PFN(vkCreateImage)
VK_DEVICE_PFN(vkCreateImageView)
VK_DEVICE_PFN(vkCreatePipelineCache)
VK_DEVICE_PFN(vkCreatePipelineLayout)
VK_DEVICE_PFN(vkCreateQueryPool)
VK_DEVICE_PFN(vkCreateSampler)
VK_DEVICE_PFN(vkCreateSemaphore)
VK_DEVICE_PFN(vkCreateShaderModule)
VK_DEVICE_PFN(vkDestroyBuffer)
VK_DEVICE_PFN(vkDestroyBufferView)
VK_DEVICE_PFN(vkDestroyCommandPool)
VK_DEVICE_PFN(vkDestroyDescriptorPool)
VK_DEVICE_PFN(vkDestroyDescriptorSetLayout)
VK_DEVICE_PFN(vkDestroyEvent)
VK_DEVICE_PFN(vkDestroyFence)
VK_DEVICE_PFN(vkDestroyFramebuffer)
VK_DEVICE_PFN(vkDestroyImage)
VK_DEVICE_PFN(vkDestroyImageView)
VK_DEVICE_PFN(vkDestroyPipeline)
VK_DEVICE_PFN(vkDestroyPipelineCache)
VK_DEVICE_PFN(vkDestroyPipelineLayout)
VK_DEVICE_PFN(vkDestroyQueryPool)
VK_DEVICE_PFN(vkDestroySampler)
VK_DEVICE_PFN(vkDestroySemaphore)
VK_DEVICE_PFN(vkDestroyShaderModule)
VK_DEVICE_PFN(vkDeviceWaitIdle)
VK_DEVICE_PFN(vkEndCommandBuffer)
VK_DEVICE_PFN(vkFlushMappedMemoryRanges)
VK_DEVICE_PFN(vkFreeCommandBuffers)
VK_DEVICE_PFN(vkFreeDescriptorSets)
VK_DEVICE_PFN(vkFreeMemory)
VK_DEVICE_PFN(vkGetBufferMemoryRequirements)
VK_DEVICE_PFN(vkGetBufferMemoryRequirements2)
VK_DEVICE_PFN(vkGetDescriptorSetLayoutSupport)
VK_DEVICE_PFN(vkGetDeviceMemoryCommitment)
VK_DEVICE_PFN(vkGetDeviceQueue)
VK_DEVICE_PFN(vkGetEventStatus)
VK_DEVICE_PFN(vkGetFenceStatus)
VK_DEVICE_PFN(vkGetImageMemoryRequirements)
VK_DEVICE_PFN(vkGetImageMemoryRequirements2)
VK_DEVICE_PFN(vkGetImageSparseMemoryRequirements)
VK_DEVICE_PFN(vkGetImageSparseMemoryRequirements2)
VK_DEVICE_PFN(vkGetImageSubresourceLayout)
VK_DEVICE_PFN(vkGetPipelineCacheData)
VK_DEVICE_PFN(vkGetQueryPoolResults)
VK_DEVICE_PFN(vkInvalidateMappedMemoryRanges)
VK_DEVICE_PFN(vkMapMemory)
VK_DEVICE_PFN(vkMergePipelineCaches)
VK_DEVICE_PFN(vkQueueBindSparse)
VK_DEVICE_PFN(vkQueueSubmit)
VK_DEVICE_PFN(vkQueueWaitIdle)
VK_DEVICE_PFN(vkResetCommandBuffer)
VK_DEVICE_PFN(vkResetCommandPool)
VK_DEVICE_PFN(vkResetDescriptorPool)
VK_DEVICE_PFN(vkResetEvent)
VK_DEVICE_PFN(vkResetFences)
VK_DEVICE_PFN(vkSetEvent)
VK_DEVICE_PFN(vkUnmapMemory)
VK_DEVICE_PFN(vkUpdateDescriptorSets)
VK_DEVICE_PFN(vkWaitForFences)

/* VK_KHR_buffer_device_address */
VK_DEVICE_EXT_PFN(vkGetBufferDeviceAddressKHR)
VK_DEVICE_EXT_PFN(vkGetBufferOpaqueCaptureAddressKHR)
VK_DEVICE_EXT_PFN(vkGetDeviceMemoryOpaqueCaptureAddressKHR)

/* VK_KHR_timeline_semaphore */
VK_DEVICE_EXT_PFN(vkGetSemaphoreCounterValueKHR)
VK_DEVICE_EXT_PFN(vkWaitSemaphoresKHR)
VK_DEVICE_EXT_PFN(vkSignalSemaphoreKHR)

/* VK_KHR_draw_indirect_count */
VK_DEVICE_EXT_PFN(vkCmdDrawIndirectCountKHR)
VK_DEVICE_EXT_PFN(vkCmdDrawIndexedIndirectCountKHR)

/* VK_KHR_push_descriptor */
VK_DEVICE_EXT_PFN(vkCmdPushDescriptorSetKHR)

/* VK_KHR_ray_tracing_pipeline */
VK_DEVICE_EXT_PFN(vkCreateRayTracingPipelinesKHR)
VK_DEVICE_EXT_PFN(vkGetRayTracingShaderGroupHandlesKHR)
VK_DEVICE_EXT_PFN(vkGetRayTracingShaderGroupStackSizeKHR)
VK_DEVICE_EXT_PFN(vkCmdSetRayTracingPipelineStackSizeKHR)
VK_DEVICE_EXT_PFN(vkCmdTraceRaysKHR)
VK_DEVICE_EXT_PFN(vkCmdTraceRaysIndirectKHR)

/* VK_KHR_acceleration_structure */
VK_DEVICE_EXT_PFN(vkGetAccelerationStructureBuildSizesKHR)
VK_DEVICE_EXT_PFN(vkCreateAccelerationStructureKHR)
VK_DEVICE_EXT_PFN(vkDestroyAccelerationStructureKHR)
VK_DEVICE_EXT_PFN(vkGetAccelerationStructureDeviceAddressKHR)
VK_DEVICE_EXT_PFN(vkCmdBuildAccelerationStructuresKHR)
VK_DEVICE_EXT_PFN(vkCmdWriteAccelerationStructuresPropertiesKHR)
VK_DEVICE_EXT_PFN(vkCmdCopyAccelerationStructureKHR)

/* VK_KHR_fragment_shading_rate */
VK_INSTANCE_EXT_PFN(vkGetPhysicalDeviceFragmentShadingRatesKHR)
VK_DEVICE_EXT_PFN(vkCmdSetFragmentShadingRateKHR)

/* VK_KHR_bind_memory2 */
VK_DEVICE_EXT_PFN(vkBindBufferMemory2KHR)
VK_DEVICE_EXT_PFN(vkBindImageMemory2KHR)

/* VK_KHR_copy_commands2 */
VK_DEVICE_EXT_PFN(vkCmdBlitImage2KHR)
VK_DEVICE_EXT_PFN(vkCmdCopyBuffer2KHR)
VK_DEVICE_EXT_PFN(vkCmdCopyBufferToImage2KHR)
VK_DEVICE_EXT_PFN(vkCmdCopyImage2KHR)
VK_DEVICE_EXT_PFN(vkCmdCopyImageToBuffer2KHR)
VK_DEVICE_EXT_PFN(vkCmdResolveImage2KHR)

/* VK_KHR_maintenance4 */
VK_DEVICE_EXT_PFN(vkGetDeviceBufferMemoryRequirementsKHR)
VK_DEVICE_EXT_PFN(vkGetDeviceImageMemoryRequirementsKHR)
VK_DEVICE_EXT_PFN(vkGetDeviceImageSparseMemoryRequirementsKHR)

/* VK_EXT_calibrated_timestamps */
VK_DEVICE_EXT_PFN(vkGetCalibratedTimestampsEXT)
VK_INSTANCE_EXT_PFN(vkGetPhysicalDeviceCalibrateableTimeDomainsEXT)

/* VK_EXT_conditional_rendering */
VK_DEVICE_EXT_PFN(vkCmdBeginConditionalRenderingEXT)
VK_DEVICE_EXT_PFN(vkCmdEndConditionalRenderingEXT)

/* VK_EXT_debug_utils */
VK_DEVICE_EXT_PFN(vkSetDebugUtilsObjectNameEXT)
VK_DEVICE_EXT_PFN(vkCmdBeginDebugUtilsLabelEXT)
VK_DEVICE_EXT_PFN(vkCmdEndDebugUtilsLabelEXT)
VK_DEVICE_EXT_PFN(vkCmdInsertDebugUtilsLabelEXT)

/* VK_EXT_transform_feedback */
VK_DEVICE_EXT_PFN(vkCmdBeginQueryIndexedEXT)
VK_DEVICE_EXT_PFN(vkCmdBeginTransformFeedbackEXT)
VK_DEVICE_EXT_PFN(vkCmdBindTransformFeedbackBuffersEXT)
VK_DEVICE_EXT_PFN(vkCmdEndQueryIndexedEXT)
VK_DEVICE_EXT_PFN(vkCmdEndTransformFeedbackEXT)

/* VK_EXT_extended_dynamic_state */
VK_DEVICE_EXT_PFN(vkCmdBindVertexBuffers2EXT)
VK_DEVICE_EXT_PFN(vkCmdSetPrimitiveTopologyEXT)
VK_DEVICE_EXT_PFN(vkCmdSetScissorWithCountEXT)
VK_DEVICE_EXT_PFN(vkCmdSetViewportWithCountEXT)

/* VK_EXT_extended_dynamic_state2 */
VK_DEVICE_EXT_PFN(vkCmdSetPrimitiveRestartEnableEXT)

/* VK_EXT_external_memory_host */
VK_DEVICE_EXT_PFN(vkGetMemoryHostPointerPropertiesEXT)

/* VK_KHR_surface */
VK_INSTANCE_EXT_PFN(vkGetPhysicalDeviceSurfacePresentModesKHR)
VK_INSTANCE_EXT_PFN(vkGetPhysicalDeviceSurfaceSupportKHR)
VK_INSTANCE_EXT_PFN(vkDestroySurfaceKHR)
VK_INSTANCE_EXT_PFN(vkGetPhysicalDeviceSurfaceFormatsKHR)
VK_INSTANCE_EXT_PFN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)

/* VK_KHR_win32_surface */
#ifdef VK_KHR_win32_surface
VK_INSTANCE_EXT_PFN(vkCreateWin32SurfaceKHR)
VK_INSTANCE_EXT_PFN(vkGetPhysicalDeviceWin32PresentationSupportKHR)
#endif

/* VK_KHR_swapchain */
VK_DEVICE_EXT_PFN(vkCreateSwapchainKHR)
VK_DEVICE_EXT_PFN(vkDestroySwapchainKHR)
VK_DEVICE_EXT_PFN(vkGetSwapchainImagesKHR)
VK_DEVICE_EXT_PFN(vkAcquireNextImageKHR)
VK_DEVICE_EXT_PFN(vkQueuePresentKHR)

/* VK_KHR_dynamic_rendering */
VK_DEVICE_EXT_PFN(vkCmdBeginRenderingKHR)
VK_DEVICE_EXT_PFN(vkCmdEndRenderingKHR)

/* VK_KHR_ray_tracing_maintenance1 */
VK_DEVICE_EXT_PFN(vkCmdTraceRaysIndirect2KHR)

/* VK_AMD_buffer_marker */
VK_DEVICE_EXT_PFN(vkCmdWriteBufferMarkerAMD)

/* VK_NV_device_diagnostic_checkpoints */
VK_DEVICE_EXT_PFN(vkCmdSetCheckpointNV)
VK_DEVICE_EXT_PFN(vkGetQueueCheckpointDataNV)

/* VK_NVX_binary_import */
VK_DEVICE_EXT_PFN(vkCreateCuModuleNVX)
VK_DEVICE_EXT_PFN(vkCreateCuFunctionNVX)
VK_DEVICE_EXT_PFN(vkDestroyCuModuleNVX)
VK_DEVICE_EXT_PFN(vkDestroyCuFunctionNVX)
VK_DEVICE_EXT_PFN(vkCmdCuLaunchKernelNVX)

/* VK_NVX_image_view_handle */
VK_DEVICE_EXT_PFN(vkGetImageViewHandleNVX)
VK_DEVICE_EXT_PFN(vkGetImageViewAddressNVX)

/* VK_VALVE_descriptor_set_host_mapping */
VK_DEVICE_EXT_PFN(vkGetDescriptorSetLayoutHostMappingInfoVALVE)
VK_DEVICE_EXT_PFN(vkGetDescriptorSetHostMappingVALVE)

/* VK_NV_device_generated_commands */
VK_DEVICE_EXT_PFN(vkCreateIndirectCommandsLayoutNV)
VK_DEVICE_EXT_PFN(vkDestroyIndirectCommandsLayoutNV)
VK_DEVICE_EXT_PFN(vkGetGeneratedCommandsMemoryRequirementsNV)
VK_DEVICE_EXT_PFN(vkCmdExecuteGeneratedCommandsNV)

#undef VK_INSTANCE_PFN
#undef VK_INSTANCE_EXT_PFN
#undef VK_DEVICE_PFN
#undef VK_DEVICE_EXT_PFN
