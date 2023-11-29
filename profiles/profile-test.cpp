/*
 * Copyright 2023 Hans-Kristian Arntzen for Valve Corporation
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

#include "vulkan/vulkan_profiles.h"
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Crude ad-hoc check. */

int main(void)
{
    VkInstanceCreateInfo instance_create_info;
    VpInstanceCreateInfo vp_instance_info;
    VkPhysicalDeviceProperties gpu_props;
    VkApplicationInfo app_info;
    VpProfileProperties *props;
    VkPhysicalDevice *gpus;
    uint32_t profile_count;
    VkInstance instance;
    uint32_t gpu_count;
    VkBool32 supported;
    uint32_t i, j;

    vpGetProfiles(&profile_count, NULL);
    props = (VpProfileProperties*)calloc(profile_count, sizeof(*props));
    vpGetProfiles(&profile_count, props);

    for (i = 0; i < profile_count; i++)
    {
        printf("Testing profile %s.\n", props[i].profileName);

        memset(&instance_create_info, 0, sizeof(instance_create_info));
        memset(&vp_instance_info, 0, sizeof(vp_instance_info));
        memset(&app_info, 0, sizeof(app_info));
        instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        app_info.apiVersion = VK_API_VERSION_1_3;
        app_info.pEngineName = "vkd3d";
        instance_create_info.pApplicationInfo = &app_info;
        vp_instance_info.pCreateInfo = &instance_create_info;

        if (vpGetInstanceProfileSupport(NULL, &props[i], &supported) != VK_SUCCESS || !supported)
        {
            fprintf(stderr, "Profile %s is not supported at instance level.\n",
                    props[i].profileName);
        }

        if (vpCreateInstance(&vp_instance_info, NULL, &instance) != VK_SUCCESS)
        {
            fprintf(stderr, "Failed to create instance ...\n");
            continue;
        }

        vkEnumeratePhysicalDevices(instance, &gpu_count, NULL);
        gpus = (VkPhysicalDevice*)calloc(gpu_count, sizeof(*gpus));
        vkEnumeratePhysicalDevices(instance, &gpu_count, gpus);

        for (j = 0; j < gpu_count; j++)
        {
            vkGetPhysicalDeviceProperties(gpus[j], &gpu_props);
            if (vpGetPhysicalDeviceProfileSupport(instance, gpus[j], &props[i], &supported) != VK_SUCCESS)
                supported = VK_FALSE;

			printf("[RESULT] GPU: %s, Profile: %s, supported: %s\n",
					gpu_props.deviceName, props[i].profileName, supported ? "yes" : "no");
        }

        free(gpus);
        vkDestroyInstance(instance, NULL);
    }
}
