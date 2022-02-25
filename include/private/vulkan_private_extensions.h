#ifndef __VULKAN_PRIVATE_EXTENSIONS_H__
#define __VULKAN_PRIVATE_EXTENSIONS_H__

/* Temporary kludge since these types are not public. */

#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_SET_HOST_MAPPING_FEATURES_VALVE ((VkStructureType)1000420000)
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_BINDING_REFERENCE_VALVE ((VkStructureType)1000420001)
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_HOST_MAPPING_INFO_VALVE ((VkStructureType)1000420002)

#define VK_VALVE_DESCRIPTOR_SET_HOST_MAPPING_SPEC_VERSION 1
#define VK_VALVE_DESCRIPTOR_SET_HOST_MAPPING_EXTENSION_NAME "VK_VALVE_descriptor_set_host_mapping"
typedef struct VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE {
    VkStructureType    sType;
    void*              pNext;
    VkBool32           descriptorSetHostMapping;
} VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE;

typedef struct VkDescriptorSetBindingReferenceVALVE {
    VkStructureType          sType;
    const void*              pNext;
    VkDescriptorSetLayout    descriptorSetLayout;
    uint32_t                 binding;
} VkDescriptorSetBindingReferenceVALVE;

typedef struct VkDescriptorSetLayoutHostMappingInfoVALVE {
    VkStructureType    sType;
    void*              pNext;
    size_t             descriptorOffset;
    uint32_t           descriptorSize;
} VkDescriptorSetLayoutHostMappingInfoVALVE;

typedef void (VKAPI_PTR *PFN_vkGetDescriptorSetLayoutHostMappingInfoVALVE)(VkDevice device, const VkDescriptorSetBindingReferenceVALVE* pBindingReference, VkDescriptorSetLayoutHostMappingInfoVALVE* pHostMapping);
typedef void (VKAPI_PTR *PFN_vkGetDescriptorSetHostMappingVALVE)(VkDevice device, VkDescriptorSet descriptorSet, void** ppData);

#endif
