/*
 * Copyright 2024 Hans-Kristian Arntzen for Valve Corporation
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
#include "vkd3d_debug.h"
#include "vkd3d_common.h"
#include <assert.h>
#include <stdio.h>

/* If we receive out of band BIND/UNBINDs, we assume they are for sparse binds
 * as they would be triggered from submission threads.
 * Otherwise, BindBufferMemory and BindImageMemory must trigger callbacks inline. */
static VKD3D_THREAD_LOCAL bool vkd3d_address_binding_is_user_thread;

void vkd3d_address_binding_tracker_mark_user_thread(void)
{
    vkd3d_address_binding_is_user_thread = true;
}

static VkBool32 VKAPI_PTR vkd3d_address_binding_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
        VkDebugUtilsMessageTypeFlagsEXT message_types,
        const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
        void *userdata)
{
    struct vkd3d_address_binding_tracker *tracker = userdata;
    const VkDeviceAddressBindingCallbackDataEXT *data;
    struct vkd3d_address_binding_report *report;
    VKD3D_UNUSED const char *type;
    VKD3D_UNUSED bool internal;
    size_t i;

    if (message_severity != VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        return VK_FALSE;
    if (message_types != VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT)
        return VK_FALSE;
    if (!callback_data)
        return VK_FALSE;

    data = vk_find_pnext(callback_data->pNext, VK_STRUCTURE_TYPE_DEVICE_ADDRESS_BINDING_CALLBACK_DATA_EXT);
    if (!data)
        return VK_FALSE;

    /* Spec says it has to provide the object handle. We'll correlate with that. */
    if (callback_data->objectCount != 1 || callback_data->pObjects[0].objectHandle == 0)
        return VK_FALSE;

    /* Just record every call we got. On a fault, we'll scan through the list and determine what happened.
     * Performance of that scan is not particularly interesting. */
    pthread_mutex_lock(&tracker->lock);
    vkd3d_array_reserve((void **)&tracker->reports, &tracker->reports_size,
            tracker->reports_count + 1, sizeof(*tracker->reports));

    report = &tracker->reports[tracker->reports_count];
    memset(report, 0, sizeof(*report));
    report->handle = callback_data->pObjects[0].objectHandle;
    report->addr = data->baseAddress;
    report->size = data->size;
    report->type = callback_data->pObjects[0].objectType;
    report->flags = data->flags;
    report->binding_type = data->bindingType;
    report->timestamp_ns = vkd3d_get_current_time_ns();

    /* If this was not initiated from a user controlled thread, assume this is vkQueueSparseBind's work. */
    report->sparse = !vkd3d_address_binding_is_user_thread;

#ifndef VKD3D_NO_TRACE_MESSAGES
    /* If we get internal buffer/image allocation we can assume it's reserving sparse VA range up front. */
    internal = !!(report->flags & VK_DEVICE_ADDRESS_BINDING_INTERNAL_OBJECT_BIT_EXT);

    switch (callback_data->pObjects[0].objectType)
    {
        case VK_OBJECT_TYPE_BUFFER:
            type = internal ? "Sparse VkBuffer" : "VkBuffer";
            break;
        case VK_OBJECT_TYPE_IMAGE:
            type = internal ? "Sparse VkImage" : "VkImage";
            break;
        case VK_OBJECT_TYPE_DEVICE_MEMORY:
            type = "VkDevice";
            break;

        default:
            type = NULL;
            break;
    }

    if (type)
    {
        TRACE("%s%s || %s || VA %016"PRIx64" || size %016"PRIx64".\n",
                data->bindingType == VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT ? "BIND" : "UNBIND",
                report->sparse ? " SPARSE" : "",
                type, data->baseAddress, data->size);
    }
#endif

    if (callback_data->pObjects[0].objectType == VK_OBJECT_TYPE_DEVICE_MEMORY)
    {
        vkd3d_array_reserve((void **) &tracker->recent_memory_indices,
                &tracker->recent_memory_indices_size,
                tracker->recent_memory_indices_count + 1,
                sizeof(*tracker->recent_memory_indices));

        /* Need to keep track of new indices so we can assign info to object handles.
         * These callbacks may happen before we know the object handle. */
        tracker->recent_memory_indices[tracker->recent_memory_indices_count++] = tracker->reports_count;
    }
    else
    {
        /* Consume any mapping information now. */
        for (i = 0; i < tracker->mappings_count; i++)
        {
            struct vkd3d_address_binding_mapping *mapping = &tracker->mappings[i];
            if (mapping->type == callback_data->pObjects[0].objectType &&
                    mapping->handle == callback_data->pObjects[0].objectHandle)
            {
                report->info = mapping->info;
                report->cookie = mapping->cookie;
                *mapping = tracker->mappings[--tracker->mappings_count];
                break;
            }
        }
    }

    tracker->reports_count++;
    pthread_mutex_unlock(&tracker->lock);

    return VK_FALSE;
}

static void vkd3d_address_binding_tracker_report_entry(const struct vkd3d_address_binding_report *report,
        VkDeviceAddress address)
{
    bool internal = !!(report->flags & VK_DEVICE_ADDRESS_BINDING_INTERNAL_OBJECT_BIT_EXT);

    ERR("VA range [%016"PRIx64", %016"PRIx64") [size = %"PRIu64" (0x%"PRIx64")]\n",
            report->addr, report->addr + report->size, report->size, report->size);
    ERR("  Fault offset: %"PRIu64" (0x%"PRIx64")\n", address - report->addr, address - report->addr);
    ERR("  %s%s\n",
            report->binding_type == VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT ? "BIND" : "UNBIND",
            report->sparse ? " SPARSE" : "");
    ERR("  T: %.6f s\n", (double)report->timestamp_ns * 1e-9);

    if (report->binding_type == VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT)
    {
        ERR("  Cookie: %"PRIu64".\n", report->cookie);

        if (report->type == VK_OBJECT_TYPE_BUFFER)
        {
            ERR("  %sVkBuffer [%s]\n", internal ? "SPARSE " : "",
                    report->info.buffer.tag ? report->info.buffer.tag : "N/A");
        }
        else if (report->type == VK_OBJECT_TYPE_IMAGE)
        {
            ERR("  %sVkImage [%u x %u x %u] [levels = %u] [layers = %u] [fmt = %d] [type = %d] [usage = #%x]\n",
                    internal ? "Sparse " : "",
                    report->info.image.extent.width,
                    report->info.image.extent.height,
                    report->info.image.extent.depth,
                    report->info.image.levels,
                    report->info.image.layers,
                    report->info.image.format,
                    report->info.image.type,
                    report->info.image.usage);
        }
        else if (report->type == VK_OBJECT_TYPE_DEVICE_MEMORY)
        {
            ERR("  VkDeviceMemory [memoryTypeIndex = %u]\n", report->info.memory.memory_type_index);
        }
        else
        {
            ERR(" Unknown object type %u.\n", report->type);
        }
    }
    else
    {
        switch (report->type)
        {
            case VK_OBJECT_TYPE_DEVICE_MEMORY:
                ERR("  VkDeviceMemory\n");
                break;

            case VK_OBJECT_TYPE_IMAGE:
                ERR("  VkImage\n");
                break;

            case VK_OBJECT_TYPE_BUFFER:
                ERR("  VkBuffer\n");
                break;

            default:
                ERR(" Unknown object type %u.\n", report->type);
                break;
        }
    }
}

void vkd3d_address_binding_tracker_check_va(struct vkd3d_address_binding_tracker *tracker,
        VkDeviceAddress address)
{
    const struct vkd3d_address_binding_report *report;
    VkDeviceAddress closest_va = 0;
    uint32_t binding_depth = 0;
    bool found_entry = false;
    size_t i;

    if (!tracker->messenger)
        return;

    ERR("Received fault in address %"PRIx64" @ %.6f s. Scanning through address log ...\n",
            address, (double)vkd3d_get_current_time_ns() * 1e-9);

    pthread_mutex_lock(&tracker->lock);
    for (i = 0; i < tracker->reports_count; i++)
    {
        report = &tracker->reports[i];
        if (address >= report->addr && address < report->addr + report->size)
        {
            vkd3d_address_binding_tracker_report_entry(report, address);

            if (report->binding_type == VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT)
            {
                binding_depth++;
            }
            else
            {
                if (binding_depth)
                    binding_depth--;
                else
                    ERR("Binding depth is currently 0. Double unbind?\n");
            }

            found_entry = true;
        }

        /* If we cannot find a candidate, find the closest VA that could explain it from an OOB perspective. */
        if (report->addr <= address)
            closest_va = max(closest_va, report->addr);
    }

    if (!found_entry)
    {
        ERR("Found no candidate VA entries. Page fault was likely caused by accessing never used memory or out of bounds access."
            " Scanning for nearby allocations ...\n");

        for (i = 0; i < tracker->reports_count; i++)
        {
            report = &tracker->reports[i];
            if (closest_va >= report->addr && closest_va < report->addr + report->size)
                vkd3d_address_binding_tracker_report_entry(report, address);
        }
    }
    else if (binding_depth)
        ERR("Fault address is currently mapped. This should not normally page fault.\n");
    else
        ERR("Fault address is currently unmapped. This is very likely use-after-free.\n");

    ERR("Done reporting VA fault.\n");
    pthread_mutex_unlock(&tracker->lock);
}

void vkd3d_address_binding_tracker_assign_info(struct vkd3d_address_binding_tracker *tracker,
        VkObjectType type, uint64_t handle, const union vkd3d_address_binding_report_resource_info *info)
{
    struct vkd3d_address_binding_report *report;
    size_t index;
    size_t i, j;

    if (!tracker->messenger)
        return;

    assert(type == VK_OBJECT_TYPE_DEVICE_MEMORY || type == VK_OBJECT_TYPE_IMAGE || type == VK_OBJECT_TYPE_BUFFER);

    /* For device memory, we expect some internal operations to happen where it allocates VA space.
     * For buffers and images, the message callback must be called in Bind instead. */
    pthread_mutex_lock(&tracker->lock);

    if (type == VK_OBJECT_TYPE_DEVICE_MEMORY)
    {
        for (i = 0, j = 0; i < tracker->recent_memory_indices_count; i++)
        {
            index = tracker->recent_memory_indices[i];
            report = &tracker->reports[index];

            if (report->type == type && report->handle == handle)
                report->info = *info;
            else
                tracker->recent_memory_indices[j++] = tracker->recent_memory_indices[i];
        }

        tracker->recent_memory_indices_count = j;
    }
    else
    {
        for (i = 0; i < tracker->mappings_count; i++)
            if (tracker->mappings[i].type == type && tracker->mappings[i].handle == handle)
                break;

        if (i < tracker->mappings_count)
        {
            tracker->mappings[i].info = *info;
        }
        else
        {
            struct vkd3d_address_binding_mapping *mapping;
            vkd3d_array_reserve((void **)&tracker->mappings, &tracker->mappings_size,
                    tracker->mappings_count + 1, sizeof(*tracker->mappings));

            mapping = &tracker->mappings[tracker->mappings_count++];
            mapping->type = type;
            mapping->handle = handle;
            mapping->info = *info;
            mapping->cookie = 0;
        }
    }

    pthread_mutex_unlock(&tracker->lock);
}

void vkd3d_address_binding_tracker_assign_cookie(struct vkd3d_address_binding_tracker *tracker,
        VkObjectType type, uint64_t handle, uint64_t cookie)
{
    size_t i;

    if (!tracker->messenger)
        return;

    assert(type == VK_OBJECT_TYPE_IMAGE || type == VK_OBJECT_TYPE_BUFFER);

    pthread_mutex_lock(&tracker->lock);

    for (i = 0; i < tracker->mappings_count; i++)
    {
        if (tracker->mappings[i].type == type && tracker->mappings[i].handle == handle)
        {
            tracker->mappings[i].cookie = cookie;
            break;
        }
    }

    pthread_mutex_unlock(&tracker->lock);
}

HRESULT vkd3d_address_binding_tracker_init(struct vkd3d_address_binding_tracker *tracker, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkDebugUtilsMessengerCreateInfoEXT create_info;

    if (!device->device_info.address_binding_report_features.reportAddressBinding ||
            !device->vk_info.EXT_debug_utils)
    {
        return S_OK;
    }

    pthread_mutex_init(&tracker->lock, NULL);

    memset(&create_info, 0, sizeof(create_info));
    create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
    create_info.pUserData = tracker;
    create_info.pfnUserCallback = vkd3d_address_binding_callback;
    create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;

    VK_CALL(vkCreateDebugUtilsMessengerEXT(device->vkd3d_instance->vk_instance, &create_info, NULL, &tracker->messenger));

    return S_OK;
}

void vkd3d_address_binding_tracker_cleanup(struct vkd3d_address_binding_tracker *tracker, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    if (!device->device_info.address_binding_report_features.reportAddressBinding ||
            !device->vk_info.EXT_debug_utils)
    {
        return;
    }

    vkd3d_free(tracker->reports);
    vkd3d_free(tracker->recent_memory_indices);
    vkd3d_free(tracker->mappings);

    pthread_mutex_destroy(&tracker->lock);
    VK_CALL(vkDestroyDebugUtilsMessengerEXT(device->vkd3d_instance->vk_instance, tracker->messenger, NULL));
}
