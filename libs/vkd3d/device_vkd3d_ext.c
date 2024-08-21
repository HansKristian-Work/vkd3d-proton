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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API

#include "vkd3d_private.h"

static inline struct d3d12_device *d3d12_device_from_ID3D12DeviceExt(d3d12_device_vkd3d_ext_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_device, ID3D12DeviceExt_iface);
}

ULONG STDMETHODCALLTYPE d3d12_device_vkd3d_ext_AddRef(d3d12_device_vkd3d_ext_iface *iface)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DeviceExt(iface);
    return d3d12_device_add_ref(device);
}

static ULONG STDMETHODCALLTYPE d3d12_device_vkd3d_ext_Release(d3d12_device_vkd3d_ext_iface *iface)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DeviceExt(iface);
    return d3d12_device_release(device);
}

extern HRESULT STDMETHODCALLTYPE d3d12_device_QueryInterface(d3d12_device_iface *iface,
        REFIID riid, void **object);

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_QueryInterface(d3d12_device_vkd3d_ext_iface *iface,
        REFIID iid, void **out)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DeviceExt(iface);
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return d3d12_device_QueryInterface(&device->ID3D12Device_iface, iid, out);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_GetVulkanHandles(d3d12_device_vkd3d_ext_iface *iface, VkInstance *vk_instance, VkPhysicalDevice *vk_physical_device, VkDevice *vk_device)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DeviceExt(iface);
    TRACE("iface %p, vk_instance %p, vk_physical_device %p, vk_device %p\n", iface, vk_instance, vk_physical_device, vk_device);
    if (!vk_device || !vk_instance || !vk_physical_device)
        return E_INVALIDARG;
        
    *vk_instance = device->vkd3d_instance->vk_instance;
    *vk_physical_device = device->vk_physical_device;
    *vk_device = device->vk_device;
    return S_OK;
}

static BOOL STDMETHODCALLTYPE d3d12_device_vkd3d_ext_GetExtensionSupport(d3d12_device_vkd3d_ext_iface *iface, D3D12_VK_EXTENSION extension)
{
    const struct d3d12_device *device = d3d12_device_from_ID3D12DeviceExt(iface);
    bool ret_val = false;
    
    TRACE("iface %p, extension %u\n", iface, extension);
    switch (extension)
    {
        case D3D12_VK_NVX_BINARY_IMPORT:
            ret_val = device->vk_info.NVX_binary_import;
            break;
        case D3D12_VK_NVX_IMAGE_VIEW_HANDLE:
            ret_val = device->vk_info.NVX_image_view_handle;
            break;
        case D3D12_VK_NV_LOW_LATENCY_2:
            ret_val = device->vk_info.NV_low_latency2;
            break;
        default:
            WARN("Invalid extension %x.\n", extension);
    }
    
    return ret_val;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_CreateCubinComputeShaderWithName(d3d12_device_vkd3d_ext_iface *iface, const void *cubin_data,
       UINT32 cubin_size, UINT32 block_x, UINT32 block_y, UINT32 block_z, const char *shader_name, D3D12_CUBIN_DATA_HANDLE **out_handle)
{
    VkCuFunctionCreateInfoNVX functionCreateInfo = { VK_STRUCTURE_TYPE_CU_FUNCTION_CREATE_INFO_NVX };
    VkCuModuleCreateInfoNVX moduleCreateInfo = { VK_STRUCTURE_TYPE_CU_MODULE_CREATE_INFO_NVX };
    const struct vkd3d_vk_device_procs *vk_procs;
    D3D12_CUBIN_DATA_HANDLE *handle;
    struct d3d12_device *device;
    VkDevice vk_device;
    VkResult vr;
    
    TRACE("iface %p, cubin_data %p, cubin_size %u, shader_name %s\n", iface, cubin_data, cubin_size, shader_name);
    if (!cubin_data || !cubin_size || !shader_name)
        return E_INVALIDARG;    

    device = d3d12_device_from_ID3D12DeviceExt(iface);
    vk_device = device->vk_device;
    handle = vkd3d_calloc(1, sizeof(D3D12_CUBIN_DATA_HANDLE));
    handle->blockX = block_x;
    handle->blockY = block_y;
    handle->blockZ = block_z;

    moduleCreateInfo.pData = cubin_data;
    moduleCreateInfo.dataSize = cubin_size;
    vk_procs = &device->vk_procs;
    if ((vr = VK_CALL(vkCreateCuModuleNVX(vk_device, &moduleCreateInfo, NULL, &handle->vkCuModule))) < 0)
    {
        ERR("Failed to create cubin shader, vr %d.\n", vr);
        vkd3d_free(handle);
        return hresult_from_vk_result(vr);
    }

    functionCreateInfo.module = handle->vkCuModule;
    functionCreateInfo.pName = shader_name;

    if ((vr = VK_CALL(vkCreateCuFunctionNVX(vk_device, &functionCreateInfo, NULL, &handle->vkCuFunction))) < 0)
    {
        ERR("Failed to create cubin function module, vr %d.\n", vr);
        VK_CALL(vkDestroyCuModuleNVX(vk_device, handle->vkCuModule, NULL));
        vkd3d_free(handle);
        return hresult_from_vk_result(vr);
    }
    
    *out_handle = handle;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_DestroyCubinComputeShader(d3d12_device_vkd3d_ext_iface *iface, D3D12_CUBIN_DATA_HANDLE *handle)
{   
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_device *device;
    VkDevice vk_device;

    TRACE("iface %p, handle %p\n", iface, handle);
    if (!iface || !handle)
        return E_INVALIDARG;
    
    device = d3d12_device_from_ID3D12DeviceExt(iface);
    vk_device = device->vk_device;
    vk_procs = &device->vk_procs;

    VK_CALL(vkDestroyCuFunctionNVX(vk_device, handle->vkCuFunction, NULL));
    VK_CALL(vkDestroyCuModuleNVX(vk_device, handle->vkCuModule, NULL));
    vkd3d_free(handle);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_GetCudaTextureObject(d3d12_device_vkd3d_ext_iface *iface, D3D12_CPU_DESCRIPTOR_HANDLE srv_handle,
       D3D12_CPU_DESCRIPTOR_HANDLE sampler_handle, UINT32 *cuda_texture_handle)
{
    VkImageViewHandleInfoNVX imageViewHandleInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_HANDLE_INFO_NVX };
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_desc_split sampler_desc;
    struct d3d12_desc_split srv_desc;
    struct d3d12_device *device;

    TRACE("iface %p, srv_handle %zu, sampler_handle %zu, cuda_texture_handle %p.\n",
            iface, srv_handle.ptr, sampler_handle.ptr, cuda_texture_handle);

    if (!cuda_texture_handle)
       return E_INVALIDARG;

    device = d3d12_device_from_ID3D12DeviceExt(iface);
    srv_desc = d3d12_desc_decode_va(srv_handle.ptr);
    sampler_desc = d3d12_desc_decode_va(sampler_handle.ptr);

    imageViewHandleInfo.imageView = srv_desc.view->info.image.view->vk_image_view;
    imageViewHandleInfo.sampler = sampler_desc.view->info.image.view->vk_sampler;
    imageViewHandleInfo.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    vk_procs = &device->vk_procs;
    *cuda_texture_handle = VK_CALL(vkGetImageViewHandleNVX(device->vk_device, &imageViewHandleInfo));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_GetCudaSurfaceObject(d3d12_device_vkd3d_ext_iface *iface, D3D12_CPU_DESCRIPTOR_HANDLE uav_handle, 
        UINT32 *cuda_surface_handle)
{
    VkImageViewHandleInfoNVX imageViewHandleInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_HANDLE_INFO_NVX };
    const struct vkd3d_vk_device_procs *vk_procs;
    struct d3d12_desc_split uav_desc;
    struct d3d12_device *device;

    TRACE("iface %p, uav_handle %zu, cuda_surface_handle %p.\n", iface, uav_handle.ptr, cuda_surface_handle);
    if (!cuda_surface_handle)
       return E_INVALIDARG;

    device = d3d12_device_from_ID3D12DeviceExt(iface);
    uav_desc = d3d12_desc_decode_va(uav_handle.ptr);

    imageViewHandleInfo.imageView = uav_desc.view->info.image.view->vk_image_view;
    imageViewHandleInfo.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    vk_procs = &device->vk_procs;
    *cuda_surface_handle = VK_CALL(vkGetImageViewHandleNVX(device->vk_device, &imageViewHandleInfo));
    return S_OK; 
}

extern VKD3D_THREAD_LOCAL struct D3D12_UAV_INFO *d3d12_uav_info;

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_CaptureUAVInfo(d3d12_device_vkd3d_ext_iface *iface, D3D12_UAV_INFO *uav_info)
{
    if (!uav_info)
       return E_INVALIDARG;

    TRACE("iface %p, uav_info %p.\n", iface, uav_info);
    
    /* CaptureUAVInfo() supposed to capture the information from the next CreateUnorderedAccess() on the same thread. 
       We use d3d12_uav_info pointer to update the information in CreateUnorderedAccess() */
    d3d12_uav_info = uav_info;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_CreateResourceFromBorrowedHandle(d3d12_device_vkd3d_ext_iface *iface,
       const D3D12_RESOURCE_DESC1 *desc, UINT64 vk_handle, ID3D12Resource **ppResource)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DeviceExt(iface);
    struct d3d12_resource *object;
    HRESULT hr = d3d12_resource_create_borrowed(device, desc, vk_handle, &object);
    if (FAILED(hr))
        return hr;

    *ppResource = (ID3D12Resource *)&object->ID3D12Resource_iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_vkd3d_ext_GetVulkanQueueInfoEx(d3d12_device_vkd3d_ext_iface *iface,
       ID3D12CommandQueue *queue, VkQueue *vk_queue, UINT32 *vk_queue_index, UINT32 *vk_queue_flags, UINT32 *vk_queue_family)
{
    TRACE("iface %p, queue %p, vk_queue %p, vk_queue_index %p, vk_queue_flags %p vk_queue_family %p.\n",
            iface, queue, vk_queue, vk_queue_index, vk_queue_flags, vk_queue_family);

    /* This only gets called during D3D11 device creation */
    *vk_queue = vkd3d_acquire_vk_queue(queue);
    vkd3d_release_vk_queue(queue);

    *vk_queue_index = vkd3d_get_vk_queue_index(queue);
    *vk_queue_flags = vkd3d_get_vk_queue_flags(queue);
    *vk_queue_family = vkd3d_get_vk_queue_family_index(queue);
    return S_OK;
}

CONST_VTBL struct ID3D12DeviceExt1Vtbl d3d12_device_vkd3d_ext_vtbl =
{
    /* IUnknown methods */
    d3d12_device_vkd3d_ext_QueryInterface,
    d3d12_device_vkd3d_ext_AddRef,
    d3d12_device_vkd3d_ext_Release,

    /* ID3D12DeviceExt methods */
    d3d12_device_vkd3d_ext_GetVulkanHandles,
    d3d12_device_vkd3d_ext_GetExtensionSupport,
    d3d12_device_vkd3d_ext_CreateCubinComputeShaderWithName,
    d3d12_device_vkd3d_ext_DestroyCubinComputeShader,
    d3d12_device_vkd3d_ext_GetCudaTextureObject,
    d3d12_device_vkd3d_ext_GetCudaSurfaceObject,
    d3d12_device_vkd3d_ext_CaptureUAVInfo,

    /* ID3D12DeviceExt1 methods */
    d3d12_device_vkd3d_ext_CreateResourceFromBorrowedHandle,
    d3d12_device_vkd3d_ext_GetVulkanQueueInfoEx,
};


static inline struct d3d12_device *d3d12_device_from_ID3D12DXVKInteropDevice(d3d12_dxvk_interop_device_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_device, ID3D12DXVKInteropDevice_iface);
}

ULONG STDMETHODCALLTYPE d3d12_dxvk_interop_device_AddRef(d3d12_dxvk_interop_device_iface *iface)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DXVKInteropDevice(iface);
    return d3d12_device_add_ref(device);
}

static ULONG STDMETHODCALLTYPE d3d12_dxvk_interop_device_Release(d3d12_dxvk_interop_device_iface *iface)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DXVKInteropDevice(iface);
    return d3d12_device_release(device);
}

extern HRESULT STDMETHODCALLTYPE d3d12_device_QueryInterface(d3d12_device_iface *iface,
        REFIID riid, void **object);

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_QueryInterface(d3d12_dxvk_interop_device_iface *iface,
        REFIID iid, void **out)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DXVKInteropDevice(iface);
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return d3d12_device_QueryInterface(&device->ID3D12Device_iface, iid, out);
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_GetDXGIAdapter(d3d12_dxvk_interop_device_iface *iface,
        REFIID iid, void **object)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DXVKInteropDevice(iface);
    TRACE("iface %p, iid %s, object %p.\n", iface, debugstr_guid(iid), object);
    return IUnknown_QueryInterface(device->parent, iid, object);
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_GetVulkanHandles(d3d12_dxvk_interop_device_iface *iface,
        VkInstance *vk_instance, VkPhysicalDevice *vk_physical_device, VkDevice *vk_device)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DXVKInteropDevice(iface);
    TRACE("iface %p, vk_instance %p, vk_physical_device %p, vk_device %p\n", iface, vk_instance, vk_physical_device, vk_device);
    if (!vk_device || !vk_instance || !vk_physical_device)
        return E_INVALIDARG;

    *vk_instance = device->vkd3d_instance->vk_instance;
    *vk_physical_device = device->vk_physical_device;
    *vk_device = device->vk_device;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_GetInstanceExtensions(d3d12_dxvk_interop_device_iface *iface, UINT *extension_count, const char **extensions)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DXVKInteropDevice(iface);
    struct vkd3d_instance *instance = device->vkd3d_instance;

    TRACE("iface %p, extension_count %p, extensions %p.\n", iface, extension_count, extensions);

    if (!extension_count || (extensions && (*extension_count < instance->vk_info.extension_count)))
        return E_INVALIDARG;

    *extension_count = instance->vk_info.extension_count;

    if (!extensions)
        return S_OK;

    memcpy(extensions, instance->vk_info.extension_names,
            sizeof(*extensions) * instance->vk_info.extension_count);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_GetDeviceExtensions(d3d12_dxvk_interop_device_iface *iface, UINT *extension_count, const char **extensions)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DXVKInteropDevice(iface);

    TRACE("iface %p, extension_count %p, extensions %p.\n", iface, extension_count, extensions);

    if (!extension_count || (extensions && (*extension_count < device->vk_info.extension_count)))
        return E_INVALIDARG;

    *extension_count = device->vk_info.extension_count;

    if (!extensions)
        return S_OK;

    memcpy(extensions, device->vk_info.extension_names,
            sizeof(*extensions) * device->vk_info.extension_count);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_GetDeviceFeatures(d3d12_dxvk_interop_device_iface *iface, const VkPhysicalDeviceFeatures2 **features)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DXVKInteropDevice(iface);

    TRACE("iface %p, features %p.\n", iface, features);

    *features = &device->device_info.features2;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_GetVulkanQueueInfo(d3d12_dxvk_interop_device_iface *iface,
        ID3D12CommandQueue *queue, VkQueue *vk_queue, UINT32 *vk_queue_family)
{
    TRACE("iface %p, queue %p, vk_queue %p, vk_queue_family %p.\n", iface, queue, vk_queue, vk_queue_family);

    /* This only gets called during D3D11 device creation */
    *vk_queue = vkd3d_acquire_vk_queue(queue);
    vkd3d_release_vk_queue(queue);

    *vk_queue_family = vkd3d_get_vk_queue_family_index(queue);
    return S_OK;
}

static void STDMETHODCALLTYPE d3d12_dxvk_interop_device_GetVulkanImageLayout(d3d12_dxvk_interop_device_iface *iface,
        ID3D12Resource *resource, D3D12_RESOURCE_STATES state, VkImageLayout *vk_layout)
{
    struct d3d12_resource *resource_impl = impl_from_ID3D12Resource(resource);

    TRACE("iface %p, resource %p, state %#x.\n", iface, resource, state);

    *vk_layout = vk_image_layout_from_d3d12_resource_state(NULL, resource_impl, state);
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_GetVulkanResourceInfo1(d3d12_dxvk_interop_device_iface *iface,
        ID3D12Resource *resource, UINT64 *vk_handle, UINT64 *buffer_offset, VkFormat *format)
{
    struct d3d12_resource *resource_impl = impl_from_ID3D12Resource(resource);

    TRACE("iface %p, resource %p, vk_handle %p.\n", iface, resource, vk_handle);

    if (resource_impl->desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        *vk_handle = (UINT64)resource_impl->res.vk_buffer;
        *buffer_offset = (UINT64)resource_impl->mem.offset;
        *format = VK_FORMAT_UNDEFINED;
    }
    else
    {
        *vk_handle = (UINT64)resource_impl->res.vk_image;
        if (resource_impl->format)
            *format = resource_impl->format->vk_format;
        else
            *format = VK_FORMAT_UNDEFINED;

        *buffer_offset = 0;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_GetVulkanResourceInfo(d3d12_dxvk_interop_device_iface *iface,
        ID3D12Resource *resource, UINT64 *vk_handle, UINT64 *buffer_offset)
{
    return d3d12_dxvk_interop_device_GetVulkanResourceInfo1(iface, resource, vk_handle, buffer_offset, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_LockCommandQueue(d3d12_dxvk_interop_device_iface *iface, ID3D12CommandQueue *queue)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DXVKInteropDevice(iface);

    TRACE("iface %p, queue %p.\n", iface, queue);

    /* Flushing the transfer queue adds a wait to all other queues, and the
     * acquire operation will drain the queue, ensuring that any pending clear
     * or upload happens before D3D11 submissions on the GPU timeline. */
    vkd3d_memory_transfer_queue_flush(&device->memory_transfers);
    vkd3d_acquire_vk_queue(queue);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_UnlockCommandQueue(d3d12_dxvk_interop_device_iface *iface, ID3D12CommandQueue *queue)
{
    TRACE("iface %p, queue %p.\n", iface, queue);

    vkd3d_release_vk_queue(queue);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_CreateInteropCommandQueue(d3d12_dxvk_interop_device_iface *iface,
        const D3D12_COMMAND_QUEUE_DESC *desc, uint32_t vk_family_index, ID3D12CommandQueue **command_queue)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DXVKInteropDevice(iface);
    struct d3d12_command_queue *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, vk_family %d, command_queue %p.\n",
            iface, desc, command_queue);

    hr = d3d12_command_queue_create(device, desc, vk_family_index, &object);
    if (FAILED(hr))
        return hr;

    *command_queue = (ID3D12CommandQueue *)&object->ID3D12CommandQueue_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_CreateInteropCommandAllocator(d3d12_dxvk_interop_device_iface *iface,
        D3D12_COMMAND_LIST_TYPE type, uint32_t vk_family_index, ID3D12CommandAllocator **command_allocator)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12DXVKInteropDevice(iface);
    struct d3d12_command_allocator *object;
    HRESULT hr;

    TRACE("iface %p, type %x, vk_family %d, command_allocator %p.\n",
            iface, type, command_allocator);

    hr = d3d12_command_allocator_create(device, type, vk_family_index, &object);
    if (FAILED(hr))
        return hr;

    *command_allocator = (ID3D12CommandAllocator *)&object->ID3D12CommandAllocator_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_BeginVkCommandBufferInterop(d3d12_dxvk_interop_device_iface *iface,
        ID3D12CommandList *pCmdList, VkCommandBuffer *cmdBuf)
{
    struct d3d12_command_list *cmd_list;
    cmd_list = d3d12_command_list_from_iface(pCmdList);

    if (!cmd_list)
        return E_INVALIDARG;

    if (cmd_list->is_inside_render_pass)
        FIXME("Interop may not work inside a D3D12 render pass.\n");
    if (cmd_list->predication.enabled_on_command_buffer)
        FIXME("Leaking predication across interop barrier. May not work as intended.\n");

    d3d12_command_list_decay_tracked_state(cmd_list);
    d3d12_command_list_invalidate_all_state(cmd_list);

    *cmdBuf = cmd_list->cmd.vk_command_buffer;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_dxvk_interop_device_EndVkCommandBufferInterop(d3d12_dxvk_interop_device_iface *iface,
        ID3D12CommandList *pCmdList)
{
    /* Reserved for future VK interop use */
    return S_OK;
}

CONST_VTBL struct ID3D12DXVKInteropDevice1Vtbl d3d12_dxvk_interop_device_vtbl =
{
    /* IUnknown methods */
    d3d12_dxvk_interop_device_QueryInterface,
    d3d12_dxvk_interop_device_AddRef,
    d3d12_dxvk_interop_device_Release,

    /* ID3D12DXVKInteropDevice methods */
    d3d12_dxvk_interop_device_GetDXGIAdapter,
    d3d12_dxvk_interop_device_GetInstanceExtensions,
    d3d12_dxvk_interop_device_GetDeviceExtensions,
    d3d12_dxvk_interop_device_GetDeviceFeatures,
    d3d12_dxvk_interop_device_GetVulkanHandles,
    d3d12_dxvk_interop_device_GetVulkanQueueInfo,
    d3d12_dxvk_interop_device_GetVulkanImageLayout,
    d3d12_dxvk_interop_device_GetVulkanResourceInfo,
    d3d12_dxvk_interop_device_LockCommandQueue,
    d3d12_dxvk_interop_device_UnlockCommandQueue,

    /* ID3D12DXVKInteropDevice1 methods */
    d3d12_dxvk_interop_device_GetVulkanResourceInfo1,
    d3d12_dxvk_interop_device_CreateInteropCommandQueue,
    d3d12_dxvk_interop_device_CreateInteropCommandAllocator,
    d3d12_dxvk_interop_device_BeginVkCommandBufferInterop,
    d3d12_dxvk_interop_device_EndVkCommandBufferInterop,
};

static inline struct d3d12_device *d3d12_device_from_ID3DLowLatencyDevice(d3d_low_latency_device_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_device, ID3DLowLatencyDevice_iface);
}

ULONG STDMETHODCALLTYPE d3d12_low_latency_device_AddRef(d3d_low_latency_device_iface *iface)
{
    struct d3d12_device *device = d3d12_device_from_ID3DLowLatencyDevice(iface);
    return d3d12_device_add_ref(device);
}

static ULONG STDMETHODCALLTYPE d3d12_low_latency_device_Release(d3d_low_latency_device_iface *iface)
{
    struct d3d12_device *device = d3d12_device_from_ID3DLowLatencyDevice(iface);
    return d3d12_device_release(device);
}

extern HRESULT STDMETHODCALLTYPE d3d12_device_QueryInterface(d3d12_device_iface *iface,
        REFIID riid, void **object);

static HRESULT STDMETHODCALLTYPE d3d12_low_latency_device_QueryInterface(d3d_low_latency_device_iface *iface,
        REFIID iid, void **out)
{
    struct d3d12_device *device = d3d12_device_from_ID3DLowLatencyDevice(iface);
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return d3d12_device_QueryInterface(&device->ID3D12Device_iface, iid, out);
}

static BOOL STDMETHODCALLTYPE d3d12_low_latency_device_SupportsLowLatency(d3d_low_latency_device_iface *iface)
{
    struct d3d12_device *device;

    device = d3d12_device_from_ID3DLowLatencyDevice(iface);

    return device->vk_info.NV_low_latency2;
}

static HRESULT STDMETHODCALLTYPE d3d12_low_latency_device_LatencySleep(d3d_low_latency_device_iface *iface)
{
    struct dxgi_vk_swap_chain *low_latency_swapchain;
    struct d3d12_device *device;

    device = d3d12_device_from_ID3DLowLatencyDevice(iface);

    if (!device->vk_info.NV_low_latency2)
        return E_NOTIMPL;

    spinlock_acquire(&device->swapchain_info.spinlock);
    if ((low_latency_swapchain = device->swapchain_info.low_latency_swapchain))
        dxgi_vk_swap_chain_incref(low_latency_swapchain);
    spinlock_release(&device->swapchain_info.spinlock);

    if (low_latency_swapchain)
    {
        dxgi_vk_swap_chain_latency_sleep(low_latency_swapchain);
        dxgi_vk_swap_chain_decref(low_latency_swapchain);
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_low_latency_device_SetLatencySleepMode(d3d_low_latency_device_iface *iface,
        BOOL low_latency_mode, BOOL low_latency_boost,
        UINT32 minimum_interval_us)
{
    struct dxgi_vk_swap_chain *low_latency_swapchain;
    struct d3d12_device *device;

    device = d3d12_device_from_ID3DLowLatencyDevice(iface);

    if (!device->vk_info.NV_low_latency2)
        return E_NOTIMPL;

    spinlock_acquire(&device->swapchain_info.spinlock);
    device->swapchain_info.mode = low_latency_mode;
    device->swapchain_info.boost = low_latency_boost;
    device->swapchain_info.minimum_us = minimum_interval_us;
    if ((low_latency_swapchain = device->swapchain_info.low_latency_swapchain))
        dxgi_vk_swap_chain_incref(low_latency_swapchain);
    spinlock_release(&device->swapchain_info.spinlock);

    if (low_latency_swapchain)
    {
        dxgi_vk_swap_chain_set_latency_sleep_mode(low_latency_swapchain, low_latency_mode, low_latency_boost, minimum_interval_us);
        dxgi_vk_swap_chain_decref(low_latency_swapchain);
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_low_latency_device_SetLatencyMarker(d3d_low_latency_device_iface *iface, UINT64 frameID, UINT32 markerType)
{
    struct dxgi_vk_swap_chain *low_latency_swapchain;
    VkLatencyMarkerNV vk_marker;
    struct d3d12_device *device;
    uint64_t internal_frame_id;

    device = d3d12_device_from_ID3DLowLatencyDevice(iface);
    vk_marker = (VkLatencyMarkerNV)markerType;

    if (!device->vk_info.NV_low_latency2)
        return E_NOTIMPL;

    if (frameID == 0)
    {
        WARN("FrameID is 0. Not a valid present ID.\n");
        return S_OK;
    }
    else if (frameID >= (UINT64_MAX >> 1) / VKD3D_LOW_LATENCY_FRAME_ID_STRIDE)
    {
        /* Don't allow the frame ID to be set to the upper half of present ID space.
         * We risk that swapchain runs out of IDs to increment if we allow application to set a present ID
         * that is close enough to UINT64_MAX. */
        WARN("FrameID %"PRIu64" is unexpectedly large. Effective present ID risks overflow. Ignoring.\n", frameID);
        return S_OK;
    }

    /* Skip ahead. If application does not set frame counter, we'll still internally increment over time to fill in the gap.
     * If application starts to use the frame IDs appropriately again, we'll catch up almost instantly,
     * where low_latency_frame_id should overtake internal present ID counter.
     * Frame marker needs to be device level monotonic. */
    internal_frame_id = frameID * VKD3D_LOW_LATENCY_FRAME_ID_STRIDE;

    switch (vk_marker)
    {
        case VK_LATENCY_MARKER_SIMULATION_START_NV:
            if (internal_frame_id <= device->frame_markers.simulation)
            {
                WARN("SIMULATION_START_NV is non-monotonic %"PRIu64" <= %"PRIu64".\n",
                        internal_frame_id, device->frame_markers.simulation);
            }
            device->frame_markers.simulation = internal_frame_id;
            break;
        case VK_LATENCY_MARKER_RENDERSUBMIT_START_NV:
            if (internal_frame_id <= device->frame_markers.render)
            {
                WARN("RENDERSUBMIT_START_NV is non-monotonic %"PRIu64" <= %"PRIu64".\n",
                        internal_frame_id, device->frame_markers.render);
            }
            device->frame_markers.render = internal_frame_id;
            break;
        case VK_LATENCY_MARKER_PRESENT_START_NV:
            if (internal_frame_id <= device->frame_markers.present)
            {
                WARN("PRESENT_START_NV is non-monotonic %"PRIu64" <= %"PRIu64".\n",
                        internal_frame_id, device->frame_markers.present);
            }
            device->frame_markers.present = internal_frame_id;
            break;
        default:
            break;
    }

    spinlock_acquire(&device->swapchain_info.spinlock);
    if ((low_latency_swapchain = device->swapchain_info.low_latency_swapchain))
        dxgi_vk_swap_chain_incref(low_latency_swapchain);
    spinlock_release(&device->swapchain_info.spinlock);

    if (low_latency_swapchain)
    {
        dxgi_vk_swap_chain_set_latency_marker(low_latency_swapchain, internal_frame_id, vk_marker);
        dxgi_vk_swap_chain_decref(low_latency_swapchain);
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_low_latency_device_GetLatencyInfo(d3d_low_latency_device_iface *iface, D3D12_LATENCY_RESULTS *latency_results)
{
    struct dxgi_vk_swap_chain *low_latency_swapchain;
    struct d3d12_device *device;

    device = d3d12_device_from_ID3DLowLatencyDevice(iface);

    if (!device->vk_info.NV_low_latency2)
        return E_NOTIMPL;

    spinlock_acquire(&device->swapchain_info.spinlock);
    if ((low_latency_swapchain = device->swapchain_info.low_latency_swapchain))
        dxgi_vk_swap_chain_incref(low_latency_swapchain);
    spinlock_release(&device->swapchain_info.spinlock);

    if (low_latency_swapchain)
    {
        dxgi_vk_swap_chain_get_latency_info(low_latency_swapchain, latency_results);
        dxgi_vk_swap_chain_decref(low_latency_swapchain);
    }

    return S_OK;
}

CONST_VTBL struct ID3DLowLatencyDeviceVtbl d3d_low_latency_device_vtbl =
{
    /* IUnknown methods */
    d3d12_low_latency_device_QueryInterface,
    d3d12_low_latency_device_AddRef,
    d3d12_low_latency_device_Release,

    /* ID3DLowLatencyDevice methods */
    d3d12_low_latency_device_SupportsLowLatency,
    d3d12_low_latency_device_LatencySleep,
    d3d12_low_latency_device_SetLatencySleepMode,
    d3d12_low_latency_device_SetLatencyMarker,
    d3d12_low_latency_device_GetLatencyInfo
};
