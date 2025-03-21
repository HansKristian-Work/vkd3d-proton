/*
 * Copyright 2020 Hans-Kristian Arntzen for Valve Corporation
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
#include "vkd3d_renderdoc.h"
#include "vkd3d_debug.h"
#include "vkd3d_threads.h"
#include "vkd3d_spinlock.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include "renderdoc_app.h"
#include "vkd3d_shader.h"

static RENDERDOC_API_1_0_0 *renderdoc_api;
static vkd3d_shader_hash_t renderdoc_capture_shader_hash;

struct vkd3d_renderdoc_capture_range
{
    uint32_t lo;
    uint32_t hi;
};

static struct vkd3d_renderdoc_capture_range *renderdoc_capture_counts;
static size_t renderdoc_capture_counts_count;
static bool vkd3d_renderdoc_is_active;
static bool vkd3d_renderdoc_global_capture;

static void vkd3d_renderdoc_init_capture_count_list(const char *env)
{
    size_t array_size = 0;
    uint32_t count;
    char *endp;

    if (strcmp(env, "-1") == 0)
    {
        INFO("Doing one big capture of the entire lifetime of a device.\n");
        vkd3d_renderdoc_global_capture = true;
        return;
    }

    while (*env != '\0')
    {
        errno = 0;
        count = strtoul(env, &endp, 0);
        if (errno != 0)
        {
            ERR("Error parsing auto counts.\n");
            break;
        }

        vkd3d_array_reserve((void**)&renderdoc_capture_counts, &array_size,
                renderdoc_capture_counts_count + 1,
                sizeof(*renderdoc_capture_counts));

        INFO("Enabling automatic RenderDoc capture of submit #%u.\n", count);
        renderdoc_capture_counts[renderdoc_capture_counts_count].lo = count;
        renderdoc_capture_counts[renderdoc_capture_counts_count].hi = count;

        if (*endp == '-')
        {
            count = strtoul(endp + 1, &endp, 0);
            renderdoc_capture_counts[renderdoc_capture_counts_count].hi = count;
        }

        renderdoc_capture_counts_count++;

        if (*endp == ',')
            env = endp + 1;
        else if (*endp != '\0')
        {
            ERR("Unexpected character %c.\n", *endp);
            break;
        }
        else
            env = endp;
    }
}

static bool vkd3d_renderdoc_enable_submit_counter(uint32_t counter)
{
    size_t i;

    /* TODO: Can be smarter about this if we have to. */
    for (i = 0; i < renderdoc_capture_counts_count; i++)
        if (renderdoc_capture_counts[i].lo <= counter && counter <= renderdoc_capture_counts[i].hi)
            return true;

    return false;
}

static void vkd3d_renderdoc_init_once(void)
{
    char counts[VKD3D_PATH_MAX];
    pRENDERDOC_GetAPI get_api;
    char env[VKD3D_PATH_MAX];

#ifdef _WIN32
    HMODULE renderdoc;
    FARPROC fn_ptr;
#else
    void *renderdoc;
    void *fn_ptr;
#endif

    vkd3d_get_env_var("VKD3D_AUTO_CAPTURE_SHADER", env, sizeof(env));
    vkd3d_get_env_var("VKD3D_AUTO_CAPTURE_COUNTS", counts, sizeof(counts));

    if (strlen(env) == 0 && strlen(counts) == 0)
    {
        WARN("VKD3D_AUTO_CAPTURE_SHADER or VKD3D_AUTO_CAPTURE_COUNTS is not set, RenderDoc auto capture will not be enabled.\n");
        return;
    }

    if (strlen(counts) == 0)
        WARN("VKD3D_AUTO_CAPTURE_COUNTS is not set, will assume that only the first submission is captured.\n");

    if (strlen(env) > 0)
        renderdoc_capture_shader_hash = strtoull(env, NULL, 16);

    if (renderdoc_capture_shader_hash)
        INFO("Enabling RenderDoc capture for shader hash: %016"PRIx64".\n", renderdoc_capture_shader_hash);
    else
        INFO("Enabling RenderDoc capture for all shaders.\n");

    if (strlen(counts) > 0)
        vkd3d_renderdoc_init_capture_count_list(counts);
    else
    {
        static struct vkd3d_renderdoc_capture_range default_range;
        renderdoc_capture_counts = &default_range;
        renderdoc_capture_counts_count = 1;
    }

    vkd3d_renderdoc_is_active = true;

    /* The RenderDoc layer must be loaded. */
#ifdef _WIN32
    renderdoc = GetModuleHandleA("renderdoc.dll");
#else
    renderdoc = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
#endif
    if (!renderdoc)
    {
        ERR("Failed to load existing RenderDoc library, falling back to using magic vkQueue label.\n");
        return;
    }

#ifdef _WIN32
    fn_ptr = GetProcAddress(renderdoc, "RENDERDOC_GetAPI");
#else
    fn_ptr = dlsym(renderdoc, "RENDERDOC_GetAPI");
#endif
    /* Workaround compiler warnings about casting to function pointer. */
    memcpy(&get_api, &fn_ptr, sizeof(fn_ptr));

    if (!get_api)
    {
        ERR("Failed to load RENDERDOC_GetAPI.\n");
        return;
    }

    if (!get_api(eRENDERDOC_API_Version_1_0_0, (void**)&renderdoc_api))
    {
        ERR("Failed to obtain RenderDoc API.\n");
        renderdoc_api = NULL;
    }
}

static pthread_once_t vkd3d_renderdoc_once = PTHREAD_ONCE_INIT;

void vkd3d_renderdoc_init(void)
{
    pthread_once(&vkd3d_renderdoc_once, vkd3d_renderdoc_init_once);
}

bool vkd3d_renderdoc_active(void)
{
    return vkd3d_renderdoc_is_active;
}

bool vkd3d_renderdoc_global_capture_enabled(void)
{
    return vkd3d_renderdoc_global_capture;
}

bool vkd3d_renderdoc_should_capture_shader_hash(vkd3d_shader_hash_t hash)
{
    return (renderdoc_capture_shader_hash == hash) || (renderdoc_capture_shader_hash == 0);
}

bool vkd3d_renderdoc_begin_capture(void *instance)
{
    static uint32_t overall_counter;
    uint32_t counter;

    if (!vkd3d_renderdoc_global_capture)
    {
        counter = vkd3d_atomic_uint32_increment(&overall_counter, vkd3d_memory_order_relaxed) - 1;
        if (!vkd3d_renderdoc_enable_submit_counter(counter))
            return false;
    }

    if (renderdoc_api)
        renderdoc_api->StartFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(instance), NULL);
    return true;
}

bool vkd3d_renderdoc_loaded_api(void)
{
    return renderdoc_api != NULL;
}

void vkd3d_renderdoc_end_capture(void *instance)
{
    if (renderdoc_api)
        renderdoc_api->EndFrameCapture(RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(instance), NULL);
}

void vkd3d_renderdoc_command_list_check_capture(struct d3d12_command_list *list,
        struct d3d12_pipeline_state *state)
{
    unsigned int i;

    if (vkd3d_renderdoc_global_capture_enabled())
        return;

    if (vkd3d_renderdoc_active() && state)
    {
        if (state->pipeline_type == VKD3D_PIPELINE_TYPE_COMPUTE)
        {
            if (vkd3d_renderdoc_should_capture_shader_hash(state->compute.code.meta.hash))
            {
                WARN("Triggering RenderDoc capture for this command list.\n");
                list->debug_capture = true;
            }
        }
        else if (state->pipeline_type == VKD3D_PIPELINE_TYPE_GRAPHICS ||
                state->pipeline_type == VKD3D_PIPELINE_TYPE_MESH_GRAPHICS)
        {
            for (i = 0; i < state->graphics.stage_count; i++)
            {
                if (vkd3d_renderdoc_should_capture_shader_hash(state->graphics.code[i].meta.hash))
                {
                    WARN("Triggering RenderDoc capture for this command list.\n");
                    list->debug_capture = true;
                    break;
                }
            }
        }
    }
}

bool vkd3d_renderdoc_command_queue_begin_capture(struct d3d12_command_queue *command_queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = &command_queue->device->vk_procs;
    VkDebugUtilsLabelEXT capture_label;
    bool debug_capture;

    if (vkd3d_renderdoc_global_capture_enabled())
        return false;

    debug_capture = vkd3d_renderdoc_begin_capture(command_queue->device->vkd3d_instance->vk_instance);
    if (debug_capture && !vkd3d_renderdoc_loaded_api())
    {
        if (command_queue->device->vk_info.EXT_debug_utils)
        {
            /* Magic fallback which lets us bridge the Wine barrier over to Linux RenderDoc. */
            capture_label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
            capture_label.pNext = NULL;
            capture_label.pLabelName = "capture-marker,begin_capture";
            memset(capture_label.color, 0, sizeof(capture_label.color));
            VK_CALL(vkQueueInsertDebugUtilsLabelEXT(command_queue->vkd3d_queue->vk_queue, &capture_label));
        }
        else
        {
            ERR("Attempting to use magic tag fallback for RenderDoc captures but VK_EXT_debug_utils is not supported.\n");
            debug_capture = false;
        }
    }

    return debug_capture;
}

void vkd3d_renderdoc_command_queue_end_capture(struct d3d12_command_queue *command_queue)
{
    const struct vkd3d_vk_device_procs *vk_procs = &command_queue->device->vk_procs;
    VkDebugUtilsLabelEXT capture_label;

    if (vkd3d_renderdoc_global_capture_enabled())
        return;

    if (!vkd3d_renderdoc_loaded_api())
    {
        /* Magic fallback which lets us bridge the Wine barrier over to Linux RenderDoc. */
        capture_label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        capture_label.pNext = NULL;
        capture_label.pLabelName = "capture-marker,end_capture";
        memset(capture_label.color, 0, sizeof(capture_label.color));
        VK_CALL(vkQueueInsertDebugUtilsLabelEXT(command_queue->vkd3d_queue->vk_queue, &capture_label));
    }
    else
        vkd3d_renderdoc_end_capture(command_queue->device->vkd3d_instance->vk_instance);
}
