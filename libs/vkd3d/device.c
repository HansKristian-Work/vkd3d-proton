/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "vkd3d_private.h"

static HRESULT vkd3d_instance_init(struct vkd3d_instance *instance)
{
    VkApplicationInfo application_info;
    VkInstanceCreateInfo instance_info;
    VkInstance vk_instance;
    VkResult vr;
    HRESULT hr;

    TRACE("instance %p.\n", instance);

    application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.pNext = NULL;
    application_info.pApplicationName = PACKAGE_NAME;
    application_info.applicationVersion = 0;
    application_info.pEngineName = NULL;
    application_info.engineVersion = 0;
    application_info.apiVersion = VK_API_VERSION_1_0;

    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pNext = NULL;
    instance_info.flags = 0;
    instance_info.pApplicationInfo = &application_info;
    instance_info.enabledLayerCount = 0;
    instance_info.ppEnabledLayerNames = NULL;
    instance_info.enabledExtensionCount = 0;
    instance_info.ppEnabledExtensionNames = NULL;

    if ((vr = vkCreateInstance(&instance_info, NULL, &vk_instance)))
    {
        ERR("Failed to create Vulkan instance, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (FAILED(hr = vkd3d_load_vk_instance_procs(&instance->vk_procs, vk_instance)))
    {
        ERR("Failed to load instance procs, hr %#x.\n", hr);
        vkDestroyInstance(vk_instance, NULL);
        return hr;
    }

    instance->vk_instance = vk_instance;

    TRACE("Created Vulkan instance %p.\n", vk_instance);

    return S_OK;
}

static void vkd3d_instance_destroy(struct vkd3d_instance *instance)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &instance->vk_procs;

    TRACE("instance %p.\n", instance);

    VK_CALL(vkDestroyInstance(instance->vk_instance, NULL));
}

static void vkd3d_trace_physical_device(VkPhysicalDevice device,
        const struct vkd3d_vk_instance_procs *vk_procs)
{
    VkPhysicalDeviceProperties device_properties;
    VkQueueFamilyProperties *queue_properties;
    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceLimits *limits;
    uint32_t count;
    unsigned int i;

    VK_CALL(vkGetPhysicalDeviceProperties(device, &device_properties));
    TRACE("Device name: %s.\n", device_properties.deviceName);
    TRACE("Vendor ID: %#x, Device ID: %#x.\n", device_properties.vendorID, device_properties.deviceID);
    TRACE("Driver version: %#x.\n", device_properties.driverVersion);
    TRACE("API version: %u.%u.%u.\n", VK_VERSION_MAJOR(device_properties.apiVersion),
            VK_VERSION_MINOR(device_properties.apiVersion), VK_VERSION_PATCH(device_properties.apiVersion));

    VK_CALL(vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL));
    TRACE("Queue families [%u]:\n", count);

    if (!(queue_properties = vkd3d_calloc(count, sizeof(VkQueueFamilyProperties))))
        return;
    VK_CALL(vkGetPhysicalDeviceQueueFamilyProperties(device, &count, queue_properties));

    for (i = 0; i < count; ++i)
    {
        TRACE(" Queue family[%u]: flags %s, count %u, timestamp bits %u, image transfer granularity %s.\n",
                i, debug_vk_queue_flags(queue_properties[i].queueFlags),
                queue_properties[i].queueCount, queue_properties[i].timestampValidBits,
                debug_vk_extent_3d(queue_properties[i].minImageTransferGranularity));
    }
    vkd3d_free(queue_properties);

    limits = &device_properties.limits;
    TRACE("Device limits:\n");
    TRACE("  maxImageDimension1D: %u.\n", limits->maxImageDimension1D);
    TRACE("  maxImageDimension2D: %u.\n", limits->maxImageDimension2D);
    TRACE("  maxImageDimension3D: %u.\n", limits->maxImageDimension3D);
    TRACE("  maxImageDimensionCube: %u.\n", limits->maxImageDimensionCube);
    TRACE("  maxImageArrayLayers: %u.\n", limits->maxImageArrayLayers);
    TRACE("  maxTexelBufferElements: %u.\n", limits->maxTexelBufferElements);
    TRACE("  maxUniformBufferRange: %u.\n", limits->maxUniformBufferRange);
    TRACE("  maxStorageBufferRange: %u.\n", limits->maxStorageBufferRange);
    TRACE("  maxPushConstantsSize: %u.\n", limits->maxPushConstantsSize);
    TRACE("  maxMemoryAllocationCount: %u.\n", limits->maxMemoryAllocationCount);
    TRACE("  maxSamplerAllocationCount: %u.\n", limits->maxSamplerAllocationCount);
    TRACE("  bufferImageGranularity: %s.\n", debugstr_uint64(limits->bufferImageGranularity));
    TRACE("  sparseAddressSpaceSize: %s.\n", debugstr_uint64(limits->sparseAddressSpaceSize));
    TRACE("  maxBoundDescriptorSets: %u.\n", limits->maxBoundDescriptorSets);
    TRACE("  maxPerStageDescriptorSamplers: %u.\n", limits->maxPerStageDescriptorSamplers);
    TRACE("  maxPerStageDescriptorUniformBuffers: %u.\n", limits->maxPerStageDescriptorUniformBuffers);
    TRACE("  maxPerStageDescriptorStorageBuffers: %u.\n", limits->maxPerStageDescriptorStorageBuffers);
    TRACE("  maxPerStageDescriptorSampledImages: %u.\n", limits->maxPerStageDescriptorSampledImages);
    TRACE("  maxPerStageDescriptorStorageImages: %u.\n", limits->maxPerStageDescriptorStorageImages);
    TRACE("  maxPerStageDescriptorInputAttachments: %u.\n", limits->maxPerStageDescriptorInputAttachments);
    TRACE("  maxPerStageResources: %u.\n", limits->maxPerStageResources);
    TRACE("  maxDescriptorSetSamplers: %u.\n", limits->maxDescriptorSetSamplers);
    TRACE("  maxDescriptorSetUniformBuffers: %u.\n", limits->maxDescriptorSetUniformBuffers);
    TRACE("  maxDescriptorSetUniformBuffersDynamic: %u.\n", limits->maxDescriptorSetUniformBuffersDynamic);
    TRACE("  maxDescriptorSetStorageBuffers: %u.\n", limits->maxDescriptorSetStorageBuffers);
    TRACE("  maxDescriptorSetStorageBuffersDynamic: %u.\n", limits->maxDescriptorSetStorageBuffersDynamic);
    TRACE("  maxDescriptorSetSampledImages: %u.\n", limits->maxDescriptorSetSampledImages);
    TRACE("  maxDescriptorSetStorageImages: %u.\n", limits->maxDescriptorSetStorageImages);
    TRACE("  maxDescriptorSetInputAttachments: %u.\n", limits->maxDescriptorSetInputAttachments);
    TRACE("  maxVertexInputAttributes: %u.\n", limits->maxVertexInputAttributes);
    TRACE("  maxVertexInputBindings: %u.\n", limits->maxVertexInputBindings);
    TRACE("  maxVertexInputAttributeOffset: %u.\n", limits->maxVertexInputAttributeOffset);
    TRACE("  maxVertexInputBindingStride: %u.\n", limits->maxVertexInputBindingStride);
    TRACE("  maxVertexOutputComponents: %u.\n", limits->maxVertexOutputComponents);
    TRACE("  maxTessellationGenerationLevel: %u.\n", limits->maxTessellationGenerationLevel);
    TRACE("  maxTessellationPatchSize: %u.\n", limits->maxTessellationPatchSize);
    TRACE("  maxTessellationControlPerVertexInputComponents: %u.\n",
            limits->maxTessellationControlPerVertexInputComponents);
    TRACE("  maxTessellationControlPerVertexOutputComponents: %u.\n",
            limits->maxTessellationControlPerVertexOutputComponents);
    TRACE("  maxTessellationControlPerPatchOutputComponents: %u.\n",
            limits->maxTessellationControlPerPatchOutputComponents);
    TRACE("  maxTessellationControlTotalOutputComponents: %u.\n",
            limits->maxTessellationControlTotalOutputComponents);
    TRACE("  maxTessellationEvaluationInputComponents: %u.\n",
            limits->maxTessellationEvaluationInputComponents);
    TRACE("  maxTessellationEvaluationOutputComponents: %u.\n",
            limits->maxTessellationEvaluationOutputComponents);
    TRACE("  maxGeometryShaderInvocations: %u.\n", limits->maxGeometryShaderInvocations);
    TRACE("  maxGeometryInputComponents: %u.\n", limits->maxGeometryInputComponents);
    TRACE("  maxGeometryOutputComponents: %u.\n", limits->maxGeometryOutputComponents);
    TRACE("  maxGeometryOutputVertices: %u.\n", limits->maxGeometryOutputVertices);
    TRACE("  maxGeometryTotalOutputComponents: %u.\n", limits->maxGeometryTotalOutputComponents);
    TRACE("  maxFragmentInputComponents: %u.\n", limits->maxFragmentInputComponents);
    TRACE("  maxFragmentOutputAttachments: %u.\n", limits->maxFragmentOutputAttachments);
    TRACE("  maxFragmentDualSrcAttachments: %u.\n", limits->maxFragmentDualSrcAttachments);
    TRACE("  maxFragmentCombinedOutputResources: %u.\n", limits->maxFragmentCombinedOutputResources);
    TRACE("  maxComputeSharedMemorySize: %u.\n", limits->maxComputeSharedMemorySize);
    TRACE("  maxComputeWorkGroupCount: %u, %u, %u.\n", limits->maxComputeWorkGroupCount[0],
            limits->maxComputeWorkGroupCount[1], limits->maxComputeWorkGroupCount[2]);
    TRACE("  maxComputeWorkGroupInvocations: %u.\n", limits->maxComputeWorkGroupInvocations);
    TRACE("  maxComputeWorkGroupSize: %u, %u, %u.\n", limits->maxComputeWorkGroupSize[0],
            limits->maxComputeWorkGroupSize[1], limits->maxComputeWorkGroupSize[2]);
    TRACE("  subPixelPrecisionBits: %u.\n", limits->subPixelPrecisionBits);
    TRACE("  subTexelPrecisionBits: %u.\n", limits->subTexelPrecisionBits);
    TRACE("  mipmapPrecisionBits: %u.\n", limits->mipmapPrecisionBits);
    TRACE("  maxDrawIndexedIndexValue: %u.\n", limits->maxDrawIndexedIndexValue);
    TRACE("  maxDrawIndirectCount: %u.\n", limits->maxDrawIndirectCount);
    TRACE("  maxSamplerLodBias: %f.\n", limits->maxSamplerLodBias);
    TRACE("  maxSamplerAnisotropy: %f.\n", limits->maxSamplerAnisotropy);
    TRACE("  maxViewports: %u.\n", limits->maxViewports);
    TRACE("  maxViewportDimensions: %u, %u.\n", limits->maxViewportDimensions[0],
            limits->maxViewportDimensions[1]);
    TRACE("  viewportBoundsRange: %f, %f.\n", limits->viewportBoundsRange[0], limits->viewportBoundsRange[1]);
    TRACE("  viewportSubPixelBits: %u.\n", limits->viewportSubPixelBits);
    TRACE("  minMemoryMapAlignment: %u.\n", (unsigned int)limits->minMemoryMapAlignment);
    TRACE("  minTexelBufferOffsetAlignment: %s.\n", debugstr_uint64(limits->minTexelBufferOffsetAlignment));
    TRACE("  minUniformBufferOffsetAlignment: %s.\n", debugstr_uint64(limits->minUniformBufferOffsetAlignment));
    TRACE("  minStorageBufferOffsetAlignment: %s.\n", debugstr_uint64(limits->minStorageBufferOffsetAlignment));
    TRACE("  minTexelOffset: %d.\n", limits->minTexelOffset);
    TRACE("  maxTexelOffset: %u.\n", limits->maxTexelOffset);
    TRACE("  minTexelGatherOffset: %d.\n", limits->minTexelGatherOffset);
    TRACE("  maxTexelGatherOffset: %u.\n", limits->maxTexelGatherOffset);
    TRACE("  minInterpolationOffset: %f.\n", limits->minInterpolationOffset);
    TRACE("  maxInterpolationOffset: %f.\n", limits->maxInterpolationOffset);
    TRACE("  subPixelInterpolationOffsetBits: %u.\n", limits->subPixelInterpolationOffsetBits);
    TRACE("  maxFramebufferWidth: %u.\n", limits->maxFramebufferWidth);
    TRACE("  maxFramebufferHeight: %u.\n", limits->maxFramebufferHeight);
    TRACE("  maxFramebufferLayers: %u.\n", limits->maxFramebufferLayers);
    TRACE("  framebufferColorSampleCounts: %#x.\n", limits->framebufferColorSampleCounts);
    TRACE("  framebufferDepthSampleCounts: %#x.\n", limits->framebufferDepthSampleCounts);
    TRACE("  framebufferStencilSampleCounts: %#x.\n", limits->framebufferStencilSampleCounts);
    TRACE("  framebufferNoAttachmentsSampleCounts: %#x.\n", limits->framebufferNoAttachmentsSampleCounts);
    TRACE("  maxColorAttachments: %u.\n", limits->maxColorAttachments);
    TRACE("  sampledImageColorSampleCounts: %#x.\n", limits->sampledImageColorSampleCounts);
    TRACE("  sampledImageIntegerSampleCounts: %#x.\n", limits->sampledImageIntegerSampleCounts);
    TRACE("  sampledImageDepthSampleCounts: %#x.\n", limits->sampledImageDepthSampleCounts);
    TRACE("  sampledImageStencilSampleCounts: %#x.\n", limits->sampledImageStencilSampleCounts);
    TRACE("  storageImageSampleCounts: %#x.\n", limits->storageImageSampleCounts);
    TRACE("  maxSampleMaskWords: %u.\n", limits->maxSampleMaskWords);
    TRACE("  timestampComputeAndGraphics: %#x.\n", limits->timestampComputeAndGraphics);
    TRACE("  timestampPeriod: %f.\n", limits->timestampPeriod);
    TRACE("  maxClipDistances: %u.\n", limits->maxClipDistances);
    TRACE("  maxCullDistances: %u.\n", limits->maxCullDistances);
    TRACE("  maxCombinedClipAndCullDistances: %u.\n", limits->maxCombinedClipAndCullDistances);
    TRACE("  discreteQueuePriorities: %u.\n", limits->discreteQueuePriorities);
    TRACE("  pointSizeRange: %f, %f.\n", limits->pointSizeRange[0], limits->pointSizeRange[1]);
    TRACE("  lineWidthRange: %f, %f,\n", limits->lineWidthRange[0], limits->lineWidthRange[1]);
    TRACE("  pointSizeGranularity: %f.\n", limits->pointSizeGranularity);
    TRACE("  lineWidthGranularity: %f.\n", limits->lineWidthGranularity);
    TRACE("  strictLines: %#x.\n", limits->strictLines);
    TRACE("  standardSampleLocations: %#x.\n", limits->standardSampleLocations);
    TRACE("  optimalBufferCopyOffsetAlignment: %s.\n",
            debugstr_uint64(limits->optimalBufferCopyOffsetAlignment));
    TRACE("  optimalBufferCopyRowPitchAlignment: %s.\n",
            debugstr_uint64(limits->optimalBufferCopyRowPitchAlignment));
    TRACE("  nonCoherentAtomSize: %s.\n", debugstr_uint64(limits->nonCoherentAtomSize));

    VK_CALL(vkGetPhysicalDeviceFeatures(device, &features));
    TRACE("Device features:\n");
    TRACE("  robustBufferAccess: %#x.\n", features.robustBufferAccess);
    TRACE("  fullDrawIndexUint32: %#x.\n", features.fullDrawIndexUint32);
    TRACE("  imageCubeArray: %#x.\n", features.imageCubeArray);
    TRACE("  independentBlend: %#x.\n", features.independentBlend);
    TRACE("  geometryShader: %#x.\n", features.geometryShader);
    TRACE("  tessellationShader: %#x.\n", features.tessellationShader);
    TRACE("  sampleRateShading: %#x.\n", features.sampleRateShading);
    TRACE("  dualSrcBlend: %#x.\n", features.dualSrcBlend);
    TRACE("  logicOp: %#x.\n", features.logicOp);
    TRACE("  multiDrawIndirect: %#x.\n", features.multiDrawIndirect);
    TRACE("  drawIndirectFirstInstance: %#x.\n", features.drawIndirectFirstInstance);
    TRACE("  depthClamp: %#x.\n", features.depthClamp);
    TRACE("  depthBiasClamp: %#x.\n", features.depthBiasClamp);
    TRACE("  fillModeNonSolid: %#x.\n", features.fillModeNonSolid);
    TRACE("  depthBounds: %#x.\n", features.depthBounds);
    TRACE("  wideLines: %#x.\n", features.wideLines);
    TRACE("  largePoints: %#x.\n", features.largePoints);
    TRACE("  alphaToOne: %#x.\n", features.alphaToOne);
    TRACE("  multiViewport: %#x.\n", features.multiViewport);
    TRACE("  samplerAnisotropy: %#x.\n", features.samplerAnisotropy);
    TRACE("  textureCompressionETC2: %#x.\n", features.textureCompressionETC2);
    TRACE("  textureCompressionASTC_LDR: %#x.\n", features.textureCompressionASTC_LDR);
    TRACE("  textureCompressionBC: %#x.\n", features.textureCompressionBC);
    TRACE("  occlusionQueryPrecise: %#x.\n", features.occlusionQueryPrecise);
    TRACE("  pipelineStatisticsQuery: %#x.\n", features.pipelineStatisticsQuery);
    TRACE("  vertexOipelineStoresAndAtomics: %#x.\n", features.vertexPipelineStoresAndAtomics);
    TRACE("  fragmentStoresAndAtomics: %#x.\n", features.fragmentStoresAndAtomics);
    TRACE("  shaderTessellationAndGeometryPointSize: %#x.\n", features.shaderTessellationAndGeometryPointSize);
    TRACE("  shaderImageGatherExtended: %#x.\n", features.shaderImageGatherExtended);
    TRACE("  shaderStorageImageExtendedFormats: %#x.\n", features.shaderStorageImageExtendedFormats);
    TRACE("  shaderStorageImageMultisample: %#x.\n", features.shaderStorageImageMultisample);
    TRACE("  shaderStorageImageReadWithoutFormat: %#x.\n", features.shaderStorageImageReadWithoutFormat);
    TRACE("  shaderStorageImageWriteWithoutFormat: %#x.\n", features.shaderStorageImageWriteWithoutFormat);
    TRACE("  shaderUniformBufferArrayDynamicIndexing: %#x.\n", features.shaderUniformBufferArrayDynamicIndexing);
    TRACE("  shaderSampledImageArrayDynamicIndexing: %#x.\n", features.shaderSampledImageArrayDynamicIndexing);
    TRACE("  shaderStorageBufferArrayDynamicIndexing: %#x.\n", features.shaderStorageBufferArrayDynamicIndexing);
    TRACE("  shaderStorageImageArrayDynamicIndexing: %#x.\n", features.shaderStorageImageArrayDynamicIndexing);
    TRACE("  shaderClipDistance: %#x.\n", features.shaderClipDistance);
    TRACE("  shaderCullDistance: %#x.\n", features.shaderCullDistance);
    TRACE("  shaderFloat64: %#x.\n", features.shaderFloat64);
    TRACE("  shaderInt64: %#x.\n", features.shaderInt64);
    TRACE("  shaderInt16: %#x.\n", features.shaderInt16);
    TRACE("  shaderResourceResidency: %#x.\n", features.shaderResourceResidency);
    TRACE("  shaderResourceMinLod: %#x.\n", features.shaderResourceMinLod);
    TRACE("  sparseBinding: %#x.\n", features.sparseBinding);
    TRACE("  sparseResidencyBuffer: %#x.\n", features.sparseResidencyBuffer);
    TRACE("  sparseResidencyImage2D: %#x.\n", features.sparseResidencyImage2D);
    TRACE("  sparseResidencyImage3D: %#x.\n", features.sparseResidencyImage3D);
    TRACE("  sparseResidency2Samples: %#x.\n", features.sparseResidency2Samples);
    TRACE("  sparseResidency4Samples: %#x.\n", features.sparseResidency4Samples);
    TRACE("  sparseResidency8Samples: %#x.\n", features.sparseResidency8Samples);
    TRACE("  sparseResidency16Samples: %#x.\n", features.sparseResidency16Samples);
    TRACE("  sparseResidencyAliased: %#x.\n", features.sparseResidencyAliased);
    TRACE("  variableMultisampleRate: %#x.\n", features.variableMultisampleRate);
    TRACE("  inheritedQueries: %#x.\n", features.inheritedQueries);
}

static HRESULT vkd3d_create_vk_device(struct d3d12_device *device)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance.vk_procs;
    unsigned int direct_queue_family_index, copy_queue_family_index;
    VkInstance instance = device->vkd3d_instance.vk_instance;
    VkQueueFamilyProperties *queue_properties;
    VkPhysicalDevice selected_physical_device;
    VkPhysicalDeviceFeatures device_features;
    VkDeviceQueueCreateInfo *queue_info;
    VkPhysicalDevice *physical_devices;
    VkDeviceCreateInfo device_info;
    uint32_t physical_device_count;
    uint32_t queue_family_count;
    VkDevice vk_device;
    unsigned int i;
    VkResult vr;
    HRESULT hr;

    TRACE("device %p.\n", device);

    /* Select a physical device */
    physical_device_count = 0;
    if ((vr = VK_CALL(vkEnumeratePhysicalDevices(instance, &physical_device_count, NULL))))
    {
        ERR("Failed to enumerate physical devices, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }
    if (!physical_device_count)
    {
        ERR("No physical device available.\n");
        return E_FAIL;
    }
    if (!(physical_devices = vkd3d_calloc(physical_device_count, sizeof(*physical_devices))))
        return E_OUTOFMEMORY;

    TRACE("Enumerating %u physical device(s).\n", physical_device_count);
    if ((vr = VK_CALL(vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices))))
    {
        ERR("Failed to enumerate physical devices, vr %d.\n", vr);
        vkd3d_free(physical_devices);
        return hresult_from_vk_result(vr);
    }

    for (i = 0; i < physical_device_count; ++i)
        vkd3d_trace_physical_device(physical_devices[i], vk_procs);

    if (physical_device_count > 1)
        FIXME("Multiple physical devices available, selecting the first one.\n");

    selected_physical_device = physical_devices[0];
    vkd3d_free(physical_devices);

    /* Create command queues */
    VK_CALL(vkGetPhysicalDeviceQueueFamilyProperties(selected_physical_device, &queue_family_count, NULL));
    if (!(queue_properties = vkd3d_calloc(queue_family_count, sizeof(*queue_properties))))
        return E_OUTOFMEMORY;
    VK_CALL(vkGetPhysicalDeviceQueueFamilyProperties(selected_physical_device,
            &queue_family_count, queue_properties));

    if (!(queue_info = vkd3d_calloc(queue_family_count, sizeof(*queue_info))))
    {
        vkd3d_free(queue_properties);
        return E_OUTOFMEMORY;
    }

    direct_queue_family_index = ~0u;
    copy_queue_family_index = ~0u;
    for (i = 0; i < queue_family_count; ++i)
    {
        static float priorities[] = {1.0f};

        queue_info[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[i].pNext = NULL;
        queue_info[i].flags = 0;
        queue_info[i].queueFamilyIndex = i;
        queue_info[i].queueCount = 1;
        queue_info[i].pQueuePriorities = priorities;

        if ((queue_properties[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
                == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
            direct_queue_family_index = i;
        if (queue_properties[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
            copy_queue_family_index = i;
    }
    vkd3d_free(queue_properties);

    if (direct_queue_family_index == ~0u)
    {
        FIXME("Could not find a suitable queue family for a direct command queue.\n");
        vkd3d_free(queue_info);
        return E_FAIL;
    }
    if (copy_queue_family_index == ~0u)
    {
        FIXME("Could not find a suitable queue family for a copy command queue.\n");
        vkd3d_free(queue_info);
        return E_FAIL;
    }

    /* Create device */
    VK_CALL(vkGetPhysicalDeviceFeatures(selected_physical_device, &device_features));

    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = NULL;
    device_info.flags = 0;
    device_info.queueCreateInfoCount = queue_family_count;
    device_info.pQueueCreateInfos = queue_info;
    device_info.enabledLayerCount = 0;
    device_info.ppEnabledLayerNames = NULL;
    device_info.enabledExtensionCount = 0;
    device_info.ppEnabledExtensionNames = NULL;
    device_info.pEnabledFeatures = &device_features;

    vr = VK_CALL(vkCreateDevice(selected_physical_device, &device_info, NULL, &vk_device));
    vkd3d_free(queue_info);
    if (vr)
    {
        ERR("Failed to create Vulkan device, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (FAILED(hr = vkd3d_load_vk_device_procs(&device->vk_procs, vk_procs, vk_device)))
    {
        ERR("Failed to load device procs, hr %#x.\n", hr);
        vkDestroyDevice(vk_device, NULL);
        return hr;
    }

    device->vk_device = vk_device;

    TRACE("Created Vulkan device %p.\n", vk_device);

    return S_OK;
}

/* ID3D12Device */
static inline struct d3d12_device *impl_from_ID3D12Device(ID3D12Device *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_device, ID3D12Device_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_QueryInterface(ID3D12Device *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12Device)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12Device_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_device_AddRef(ID3D12Device *iface)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    ULONG refcount = InterlockedIncrement(&device->refcount);

    TRACE("%p increasing refcount to %u.\n", device, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_device_Release(ID3D12Device *iface)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    ULONG refcount = InterlockedDecrement(&device->refcount);

    TRACE("%p decreasing refcount to %u.\n", device, refcount);

    if (!refcount)
    {
        const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

        VK_CALL(vkDestroyDevice(device->vk_device, NULL));
        vkd3d_instance_destroy(&device->vkd3d_instance);

        vkd3d_free(device);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_GetPrivateData(ID3D12Device *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    FIXME("iface %p, guid %s, data_size %p, data %p stub!",
            iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetPrivateData(ID3D12Device *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    FIXME("iface %p, guid %s, data_size %u, data %p stub!\n",
            iface, debugstr_guid(guid), data_size, data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetPrivateDataInterface(ID3D12Device *iface,
        REFGUID guid, const IUnknown *data)
{
    FIXME("iface %p, guid %s, data %p stub!\n", iface, debugstr_guid(guid), data);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetName(ID3D12Device *iface, const WCHAR *name)
{
    FIXME("iface %p, name %s stub!\n", iface, debugstr_w(name));

    return E_NOTIMPL;
}

static UINT STDMETHODCALLTYPE d3d12_device_GetNodeCount(ID3D12Device *iface)
{
    TRACE("iface %p.\n", iface);

    return 1;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandQueue(ID3D12Device *iface,
        const D3D12_COMMAND_QUEUE_DESC *desc, REFIID riid, void **command_queue)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_command_queue *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, riid %s, command_queue %p.\n",
            iface, desc, debugstr_guid(riid), command_queue);

    if (FAILED(hr = d3d12_command_queue_create(device, desc, &object)))
        return hr;

    return return_interface((IUnknown *)&object->ID3D12CommandQueue_iface, &IID_ID3D12CommandQueue,
            riid, command_queue);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandAllocator(ID3D12Device *iface,
        D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **command_allocator)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_command_allocator *object;
    HRESULT hr;

    TRACE("iface %p, type %#x, riid %s, command_allocator %p.\n",
            iface, type, debugstr_guid(riid), command_allocator);

    if (FAILED(hr = d3d12_command_allocator_create(device, type, &object)))
        return hr;

    return return_interface((IUnknown *)&object->ID3D12CommandAllocator_iface, &IID_ID3D12CommandAllocator,
            riid, command_allocator);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateGraphicsPipelineState(ID3D12Device *iface,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, REFIID riid, void **pipeline_state)
{
    FIXME("iface %p, desc %p, riid %s, pipeline_state %p stub!\n",
            iface, desc, debugstr_guid(riid), pipeline_state);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateComputePipelineState(ID3D12Device *iface,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, REFIID riid, void **pipeline_state)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_pipeline_state *object;
    HRESULT hr;

    FIXME("iface %p, desc %p, riid %s, pipeline_state %p stub!\n",
            iface, desc, debugstr_guid(riid), pipeline_state);

    if (FAILED(hr = d3d12_pipeline_state_create_compute(device, desc, &object)))
        return hr;

    return return_interface((IUnknown *)&object->ID3D12PipelineState_iface,
            &IID_ID3D12PipelineState, riid, pipeline_state);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandList(ID3D12Device *iface,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *command_allocator,
        ID3D12PipelineState *initial_pipeline_state, REFIID riid, void **command_list)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_command_list *object;
    HRESULT hr;

    TRACE("iface %p, node_mask 0x%08x, type %#x, command_allocator %p, "
            "initial_pipeline_state %p, riid %s, command_list %p.\n",
            iface, node_mask, type, command_allocator,
            initial_pipeline_state, debugstr_guid(riid), command_list);

    if (FAILED(hr = d3d12_command_list_create(device, node_mask, type, command_allocator,
            initial_pipeline_state, &object)))
        return hr;

    return return_interface((IUnknown *)&object->ID3D12GraphicsCommandList_iface,
            &IID_ID3D12GraphicsCommandList, riid, command_list);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CheckFeatureSupport(ID3D12Device *iface,
        D3D12_FEATURE feature, void *feature_data, UINT feature_data_size)
{
    TRACE("iface %p, feature %#x, feature_data %p, feature_data_size %u.\n",
            iface, feature, feature_data, feature_data_size);

    switch (feature)
    {
        case D3D12_FEATURE_FEATURE_LEVELS:
        {
            D3D12_FEATURE_DATA_FEATURE_LEVELS *data = feature_data;
            unsigned int i;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }
            if (!data->NumFeatureLevels)
                return E_INVALIDARG;

            data->MaxSupportedFeatureLevel = 0;
            for (i = 0; i < data->NumFeatureLevels; ++i)
            {
                D3D_FEATURE_LEVEL fl = data->pFeatureLevelsRequested[i];
                if (data->MaxSupportedFeatureLevel < fl && check_feature_level_support(fl))
                    data->MaxSupportedFeatureLevel = fl;
            }
            break;
        }

        default:
            FIXME("Unhandled feature %#x.\n", feature);
            return E_NOTIMPL;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateDescriptorHeap(ID3D12Device *iface,
        const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID riid, void **descriptor_heap)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_descriptor_heap *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, riid %s, descriptor_heap %p.\n",
            iface, desc, debugstr_guid(riid), descriptor_heap);

    if (FAILED(hr = d3d12_descriptor_heap_create(device, desc, &object)))
        return hr;

    return return_interface((IUnknown *)&object->ID3D12DescriptorHeap_iface,
            &IID_ID3D12DescriptorHeap, riid, descriptor_heap);
}

static UINT STDMETHODCALLTYPE d3d12_device_GetDescriptorHandleIncrementSize(ID3D12Device *iface,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    FIXME("iface %p, descriptor_heap_type %#x stub!\n", iface, descriptor_heap_type);

    return 0;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateRootSignature(ID3D12Device *iface,
        UINT node_mask, const void *bytecode, SIZE_T bytecode_length,
        REFIID riid, void **root_signature)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_root_signature *object;
    HRESULT hr;

    TRACE("iface %p, node_mask 0x%08x, bytecode %p, bytecode_length %lu, riid %s, root_signature %p.\n",
            iface, node_mask, bytecode, bytecode_length, debugstr_guid(riid), root_signature);

    if (node_mask && node_mask != 1)
        FIXME("Ignoring node mask 0x%08x.\n", node_mask);

    if (bytecode_length != ~0u)
    {
        FIXME("Root signature byte code not supported.\n");
        return E_NOTIMPL;
    }

    if (FAILED(hr = d3d12_root_signature_create(device,
            (const D3D12_ROOT_SIGNATURE_DESC *)bytecode, &object)))
        return hr;

    return return_interface((IUnknown *)&object->ID3D12RootSignature_iface,
            &IID_ID3D12RootSignature, riid, root_signature);
}

static void STDMETHODCALLTYPE d3d12_device_CreateConstantBufferView(ID3D12Device *iface,
        const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    FIXME("iface %p, desc %p, descriptor %#lx stub!\n", iface, desc, descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_device_CreateShaderResourceView(ID3D12Device *iface,
        ID3D12Resource *resource, const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    FIXME("iface %p, resource %p, desc %p, descriptor %#lx stub!\n",
            iface, resource, desc, descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_device_CreateUnorderedAccessView(ID3D12Device *iface,
        ID3D12Resource *resource, ID3D12Resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    FIXME("iface %p, resource %p, counter_resource %p, desc %p, descriptor %#lx stub!\n",
            iface, resource, counter_resource, desc, descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_device_CreateRenderTargetView(ID3D12Device *iface,
        ID3D12Resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    FIXME("iface %p, resource %p, desc %p, descriptor %#lx stub!\n",
            iface, resource, desc, descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_device_CreateDepthStencilView(ID3D12Device *iface,
        ID3D12Resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    FIXME("iface %p, resource %p, desc %p, descriptor %#lx stub!\n",
            iface, resource, desc, descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_device_CreateSampler(ID3D12Device *iface,
        const D3D12_SAMPLER_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    FIXME("iface %p, desc %p, descriptor %#lx stub!\n",
            iface, desc, descriptor.ptr);
}

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptors(ID3D12Device *iface,
        UINT dst_descriptor_range_count, const D3D12_CPU_DESCRIPTOR_HANDLE *dst_descriptor_range_offsets,
        const UINT *dst_descriptor_range_sizes,
        UINT src_descriptor_range_count, const D3D12_CPU_DESCRIPTOR_HANDLE *src_descriptor_range_offsets,
        const UINT *src_descriptor_range_sizes,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    FIXME("iface %p, dst_descriptor_range_count %u, dst_descriptor_range_offsets %p, "
            "dst_descriptor_range_sizes %p, src_descriptor_range_count %u, "
            "src_descriptor_range_offsets %p, src_descriptor_range_sizes %p, "
            "descriptor_heap_type %#x stub!\n",
            iface, dst_descriptor_range_count, dst_descriptor_range_offsets,
            dst_descriptor_range_sizes, src_descriptor_range_count, src_descriptor_range_offsets,
            src_descriptor_range_sizes, descriptor_heap_type);
}

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptorsSimple(ID3D12Device *iface,
        UINT descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE dst_descriptor_range_offset,
        const D3D12_CPU_DESCRIPTOR_HANDLE src_descriptor_range_offset,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    FIXME("iface %p, descriptor_count %u, dst_descriptor_range_offset %#lx, "
            "src_descriptor_range_offset %#lx, descriptor_heap_type %#x stub!\n",
            iface, descriptor_count, dst_descriptor_range_offset.ptr, src_descriptor_range_offset.ptr,
            descriptor_heap_type);
}

static D3D12_RESOURCE_ALLOCATION_INFO * STDMETHODCALLTYPE d3d12_device_GetResourceAllocationInfo(
        ID3D12Device *iface, D3D12_RESOURCE_ALLOCATION_INFO *allocation_info, UINT visible_mask,
        UINT resource_desc_count, const D3D12_RESOURCE_DESC *resource_descs)
{
    FIXME("iface %p, allocation_info %p, visible_mask 0x%08x, resource_desc_count %u, "
            "resource_descs %p stub!\n",
            iface, allocation_info, visible_mask, resource_desc_count, resource_descs);

    return allocation_info;
}

static D3D12_HEAP_PROPERTIES * STDMETHODCALLTYPE d3d12_device_GetCustomHeapProperties(ID3D12Device *iface,
        D3D12_HEAP_PROPERTIES *heap_properties, UINT node_mask, D3D12_HEAP_TYPE heap_type)
{
    FIXME("iface %p, heap_properties %p, node_mask 0x%08x, heap_type %#x stub!\n",
            iface, heap_properties, node_mask, heap_type);

    return heap_properties;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommittedResource(ID3D12Device *iface,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, REFIID riid, void **resource)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_resource *object;
    HRESULT hr;

    FIXME("iface %p, heap_properties %p, heap_flags %#x,  desc %p, initial_state %#x, "
            "optimized_clear_value %p, riid %s, resource %p stub!\n",
            iface, heap_properties, heap_flags, desc, initial_state,
            optimized_clear_value, debugstr_guid(riid), resource);

    if (FAILED(hr = d3d12_committed_resource_create(device, heap_properties, heap_flags,
            desc, initial_state, optimized_clear_value, &object)))
        return hr;

    return return_interface((IUnknown *)&object->ID3D12Resource_iface, &IID_ID3D12Resource,
            riid, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateHeap(ID3D12Device *iface,
        const D3D12_HEAP_DESC *desc, REFIID riid, void **heap)
{
    FIXME("iface %p, desc %p, riid %s, heap %p stub!\n",
            iface, desc, debugstr_guid(riid), heap);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreatePlacedResource(ID3D12Device *iface,
        ID3D12Heap *heap, UINT64 heap_offset,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        REFIID riid, void **resource)
{
    FIXME("iface %p, heap %p, heap_offset %s, desc %p, initial_state %#x, "
            "optimized_clear_value %p, riid %s, resource %p stub!\n",
            iface, heap, debugstr_uint64(heap_offset), desc, initial_state,
            optimized_clear_value, debugstr_guid(riid), resource);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateReservedResource(ID3D12Device *iface,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        REFIID riid, void **resource)
{
    FIXME("iface %p, desc %p, initial_state %#x, optimized_clear_value %p, "
            "riid %s, resource %p stub!\n",
            iface, desc, initial_state, optimized_clear_value, debugstr_guid(riid), resource);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateSharedHandle(ID3D12Device *iface,
        ID3D12DeviceChild *object, const SECURITY_ATTRIBUTES *attributes, DWORD access,
        const WCHAR *name, HANDLE *handle)
{
    FIXME("iface %p, object %p, attributes %p, access %#x, name %s, handle %p stub!\n",
            iface, object, attributes, access, debugstr_w(name), handle);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_OpenSharedHandle(ID3D12Device *iface,
        HANDLE handle, REFIID riid, void **object)
{
    FIXME("iface %p, handle %p, riid %s, object %p stub!\n",
            iface, handle, debugstr_guid(riid), object);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_OpenSharedHandleByName(ID3D12Device *iface,
        const WCHAR *name, DWORD access, HANDLE *handle)
{
    FIXME("iface %p, name %s, access %#x, handle %p stub!\n",
            iface, debugstr_w(name), access, handle);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_MakeResident(ID3D12Device *iface,
        UINT object_count, ID3D12Pageable * const *objects)
{
    FIXME("iface %p, object_count %u, objects %p stub!\n",
            iface, object_count, objects);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_Evict(ID3D12Device *iface,
        UINT object_count, ID3D12Pageable * const *objects)
{
    FIXME("iface %p, object_count %u, objects %p stub!\n",
            iface, object_count, objects);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateFence(ID3D12Device *iface,
        UINT64 initial_value, D3D12_FENCE_FLAGS flags, REFIID riid, void **fence)
{
    FIXME("iface %p, intial_value %s, flags %#x, riid %s, fence %p stub!\n",
            iface, debugstr_uint64(initial_value), flags, debugstr_guid(riid), fence);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_GetDeviceRemovedReason(ID3D12Device *iface)
{
    FIXME("iface %p stub!\n", iface);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d12_device_GetCopyableFootprints(ID3D12Device *iface,
        const D3D12_RESOURCE_DESC *desc,
        UINT first_sub_resource,
        UINT sub_resource_count,
        UINT64 base_offset,
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts,
        UINT *row_count,
        UINT64 *row_size,
        UINT64 *total_bytes)
{
    FIXME("iface %p, desc %p, first_sub_resource %u, sub_resource_count %u, base_offset %s, "
            "layouts %p, row_count %p, row_size %p, total_bytes %p stub!\n",
            iface, desc, first_sub_resource, sub_resource_count, debugstr_uint64(base_offset), layouts,
            row_count, row_size, total_bytes);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateQueryHeap(ID3D12Device *iface,
        const D3D12_QUERY_HEAP_DESC *desc, REFIID riid, void **heap)
{
    FIXME("iface %p, desc %p, riid %s, heap %p stub!\n",
            iface, desc, debugstr_guid(riid), heap);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetStablePowerState(ID3D12Device *iface, BOOL enable)
{
    FIXME("iface %p, enable %#x stub!\n", iface, enable);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandSignature(ID3D12Device *iface,
        const D3D12_COMMAND_SIGNATURE_DESC *desc, ID3D12RootSignature *root_signature,
        REFIID riid, void **command_signature)
{
    FIXME("iface %p, desc %p, root_signature %p, riid %s, command_signature %p stub!\n",
            iface, desc, root_signature, debugstr_guid(riid), command_signature);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d12_device_GetResourceTiling(ID3D12Device *iface,
        ID3D12Resource *resource, UINT *total_tile_count,
        D3D12_PACKED_MIP_INFO *packed_mip_info, D3D12_TILE_SHAPE *standard_tile_shape,
        UINT *sub_resource_tiling_count, UINT first_sub_resource_tiling,
        D3D12_SUBRESOURCE_TILING *sub_resource_tilings)
{
    FIXME("iface %p, resource %p, total_tile_count %p, packed_mip_info %p, "
            "standard_title_shape %p, sub_resource_tiling_count %p, "
            "first_sub_resource_tiling %u, sub_resource_tilings %p stub!\n",
            iface, resource, total_tile_count, packed_mip_info, standard_tile_shape,
            sub_resource_tiling_count, first_sub_resource_tiling,
            sub_resource_tilings);
}

static LUID * STDMETHODCALLTYPE d3d12_device_GetAdapterLuid(ID3D12Device *iface, LUID *luid)
{
    FIXME("iface %p, luid %p stub!\n", iface, luid);

    return luid;
}

static const struct ID3D12DeviceVtbl d3d12_device_vtbl =
{
    /* IUnknown methods */
    d3d12_device_QueryInterface,
    d3d12_device_AddRef,
    d3d12_device_Release,
    /* ID3D12Object methods */
    d3d12_device_GetPrivateData,
    d3d12_device_SetPrivateData,
    d3d12_device_SetPrivateDataInterface,
    d3d12_device_SetName,
    /* ID3D12Device methods */
    d3d12_device_GetNodeCount,
    d3d12_device_CreateCommandQueue,
    d3d12_device_CreateCommandAllocator,
    d3d12_device_CreateGraphicsPipelineState,
    d3d12_device_CreateComputePipelineState,
    d3d12_device_CreateCommandList,
    d3d12_device_CheckFeatureSupport,
    d3d12_device_CreateDescriptorHeap,
    d3d12_device_GetDescriptorHandleIncrementSize,
    d3d12_device_CreateRootSignature,
    d3d12_device_CreateConstantBufferView,
    d3d12_device_CreateShaderResourceView,
    d3d12_device_CreateUnorderedAccessView,
    d3d12_device_CreateRenderTargetView,
    d3d12_device_CreateDepthStencilView,
    d3d12_device_CreateSampler,
    d3d12_device_CopyDescriptors,
    d3d12_device_CopyDescriptorsSimple,
    d3d12_device_GetResourceAllocationInfo,
    d3d12_device_GetCustomHeapProperties,
    d3d12_device_CreateCommittedResource,
    d3d12_device_CreateHeap,
    d3d12_device_CreatePlacedResource,
    d3d12_device_CreateReservedResource,
    d3d12_device_CreateSharedHandle,
    d3d12_device_OpenSharedHandle,
    d3d12_device_OpenSharedHandleByName,
    d3d12_device_MakeResident,
    d3d12_device_Evict,
    d3d12_device_CreateFence,
    d3d12_device_GetDeviceRemovedReason,
    d3d12_device_GetCopyableFootprints,
    d3d12_device_CreateQueryHeap,
    d3d12_device_SetStablePowerState,
    d3d12_device_CreateCommandSignature,
    d3d12_device_GetResourceTiling,
    d3d12_device_GetAdapterLuid,
};

static HRESULT d3d12_device_init(struct d3d12_device *device)
{
    HRESULT hr;

    device->ID3D12Device_iface.lpVtbl = &d3d12_device_vtbl;
    device->refcount = 1;

    if (FAILED(hr = vkd3d_instance_init(&device->vkd3d_instance)))
        return hr;

    if (FAILED(hr = vkd3d_create_vk_device(device)))
    {
        vkd3d_instance_destroy(&device->vkd3d_instance);
        return hr;
    }

    return S_OK;
}

HRESULT d3d12_device_create(struct d3d12_device **device)
{
    struct d3d12_device *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_device_init(object)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created device %p.\n", object);

    *device = object;

    return S_OK;
}
