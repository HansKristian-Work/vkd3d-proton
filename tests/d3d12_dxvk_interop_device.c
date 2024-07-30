
/*
 * Copyright 2024 NVIDIA Corporation
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
#include "d3d12_crosstest.h"

void test_vkd3d_dxvk_cmdbuf_interop(void)
{
    PFN_vkCmdUpdateBuffer pfn_vkCmdUpdateBuffer;
    ID3D12DXVKInteropDevice1 *interop_device;
    ID3D12GraphicsCommandList *command_list;
    ID3D12CommandAllocator *allocator;
    VkPhysicalDevice vkPhysicalDevice;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    VkCommandBuffer vkCmdBuf;
    ID3D12Resource *buffer;
    ID3D12Device *device;
    VkInstance vkInstance;
    unsigned int value;
    VkDevice vkDevice;
    VkFormat format;
    UINT64 vkBuffer;
    UINT64 offset;
    HRESULT hr;

    static const unsigned int data_values[] = {0xdeadbeef, 0xf00baa, 0xdeadbeef, 0xf00baa};
    static const uint32_t update_data[3] = { 0x1020304, 0xc0d0e0f, 0x5060708};

    // Initialize pointer to GDPA function
    if (!init_vulkan_loader())
        return;

    if (!init_test_context(&context, NULL))
        return;

    device = context.device;

    if (FAILED(hr = ID3D12Device_QueryInterface(device,
            &IID_ID3D12DXVKInteropDevice1, (void **)&interop_device)))
    {
        skip("ID3D12DXVKInteropDevice1 not implemented.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(hr = ID3D12DXVKInteropDevice1_GetVulkanHandles(interop_device,
        &vkInstance,
        &vkPhysicalDevice,
        &vkDevice)))
    {
        ok(hr == S_OK, "ID3D12DXVKInteropDevice1_GetVulkanHandles failed %#x.\n", hr);
        destroy_test_context(&context);
        return;
    }

    pfn_vkCmdUpdateBuffer = (PFN_vkCmdUpdateBuffer)pfn_vkGetDeviceProcAddr(vkDevice, "vkCmdUpdateBuffer");
    if (!pfn_vkCmdUpdateBuffer)
    {
        ok(pfn_vkCmdUpdateBuffer, "vkCmdUpdateBuffer not found.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(hr = ID3D12DXVKInteropDevice1_CreateInteropCommandAllocator(interop_device,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        VK_QUEUE_FAMILY_IGNORED,
        &allocator)))
    {
        ok(hr == S_OK, "ID3D12DXVKInteropDevice1_CreateInteropCommandAllocator failed %#x.\n", hr);
        destroy_test_context(&context);
        return;
    }

    if (FAILED(hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator, NULL, &IID_ID3D12GraphicsCommandList, (void **)&command_list)))
    {
        ok(hr == S_OK, "ID3D12Device_CreateCommandList failed %#x.\n", hr);
        destroy_test_context(&context);
        return;
    }

    if (FAILED(hr = ID3D12DXVKInteropDevice1_BeginVkCommandBufferInterop(interop_device,
        (ID3D12CommandList*)command_list,
        &vkCmdBuf)))
    {
        ok(hr == S_OK, "ID3D12DXVKInteropDevice1_BeginVulkanCmdHandles failed %#x.\n", hr);
        destroy_test_context(&context);
        return;
    }

    queue = context.queue;

    // Create a D3D12 Buffer
    buffer = create_default_buffer(device,
                sizeof(data_values),
                D3D12_RESOURCE_FLAG_NONE,
                D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(buffer, 0, sizeof(data_values), data_values, queue, command_list);
    reset_command_list(command_list, allocator);


    // Export it to VK and use VK functions to manipulate it
    ID3D12DXVKInteropDevice1_GetVulkanResourceInfo1(interop_device,
        buffer,
        &vkBuffer,
        &offset,
        &format);

    // Update in the pattern 0 1 - 2
    pfn_vkCmdUpdateBuffer(vkCmdBuf, (VkBuffer)vkBuffer, 0, sizeof(data_values[0]) * 2, &update_data[0]);
    pfn_vkCmdUpdateBuffer(vkCmdBuf, (VkBuffer)vkBuffer, sizeof(data_values[0]) * 3, sizeof(data_values[0]), &update_data[2]);

    if (FAILED(hr = ID3D12DXVKInteropDevice1_EndVkCommandBufferInterop(interop_device,
        (ID3D12CommandList*)command_list)))
    {
        ok(hr == S_OK, "ID3D12DXVKInteropDevice1_EndVulkanCmdHandles failed %#x.\n", hr);
        destroy_test_context(&context);
        return;
    }

    // Back to D3D12 logic, close the command list + execute it
    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    exec_command_list(queue, command_list);
    wait_queue_idle(device, queue);
    reset_command_list(command_list, allocator);


    // Validate data
    get_buffer_readback_with_command_list(buffer, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    value = get_readback_uint(&rb, 0, 0, 0);
    ok(value == update_data[0], "Got unexpected value %#x, expected %#x.\n", value, update_data[0]);
    value = get_readback_uint(&rb, 1, 0, 0);
    ok(value == update_data[1], "Got unexpected value %#x, expected %#x.\n", value, update_data[1]);
    value = get_readback_uint(&rb, 2, 0, 0);
    ok(value == data_values[2], "Got unexpected value %#x, expected %#x.\n", value, data_values[2]);
    value = get_readback_uint(&rb, 3, 0, 0);
    ok(value == update_data[2], "Got unexpected value %#x, expected %#x.\n", value, update_data[2]);
    release_resource_readback(&rb);
    reset_command_list(command_list, allocator);

    ID3D12Resource_Release(buffer);
    ID3D12GraphicsCommandList_Release(command_list);
    ID3D12CommandAllocator_Release(allocator);
    ID3D12DXVKInteropDevice1_Release(interop_device);
    destroy_test_context(&context);
}
