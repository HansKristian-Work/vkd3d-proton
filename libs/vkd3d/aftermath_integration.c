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

#include "vkd3d_private.h"
#include "vkd3d_threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef VKD3D_ENABLE_AFTERMATH
#include "GFSDK_Aftermath.h"
#include "GFSDK_Aftermath_GpuCrashDump.h"
#include "GFSDK_Aftermath_GpuCrashDumpDecoding.h"
#endif

static PFN_GFSDK_Aftermath_GetShaderDebugInfoIdentifier aftermath_get_identifier;
static PFN_GFSDK_Aftermath_EnableGpuCrashDumps aftermath_enable_cb;
static PFN_GFSDK_Aftermath_GpuCrashDump_CreateDecoder aftermath_create_decoder;
static PFN_GFSDK_Aftermath_GpuCrashDump_DestroyDecoder aftermath_destroy_decoder;
static PFN_GFSDK_Aftermath_GpuCrashDump_GenerateJSON aftermath_generate_json;
static PFN_GFSDK_Aftermath_GpuCrashDump_GetJSON aftermath_get_json;
static PFN_GFSDK_Aftermath_GetShaderHashSpirv aftermath_get_shader_hash_spirv;
static vkd3d_module_t aftermath_module;
static pthread_once_t aftermath_init_once = PTHREAD_ONCE_INIT;
static const char *aftermath_dump_path;
static bool aftermath_enabled;

static void vkd3d_aftermath_dump_buffer(const void *data, size_t size,
        const struct GFSDK_Aftermath_ShaderDebugInfoIdentifier *identifier, bool text)
{
    unsigned int process_id;
    char path[1024];
    uint32_t val;
    FILE *file;

#ifdef _WIN32
    process_id = GetCurrentProcessId();
#else
    process_id = getpid();
#endif

    if (identifier)
        snprintf(path, sizeof(path), "%s/shader_%016"PRIx64"%016"PRIx64".nvdbg", aftermath_dump_path, identifier->id[0], identifier->id[1]);
    else if (text)
    {
        static uint32_t counter;
        val = vkd3d_atomic_uint32_increment(&counter, vkd3d_memory_order_relaxed);
        snprintf(path, sizeof(path), "%s/aftermath.%u.%u.json", aftermath_dump_path, process_id, val);
    }
    else
    {
        static uint32_t counter;
        val = vkd3d_atomic_uint32_increment(&counter, vkd3d_memory_order_relaxed);
        snprintf(path, sizeof(path), "%s/aftermath.%u.%u.nv-gpudmp", aftermath_dump_path, process_id, val);
    }

    file = fopen(path, "wb");
    if (!file)
    {
        ERR("Failed to open file: %s\n", path);
        return;
    }

    if (fwrite(data, 1, size, file) != size)
        ERR("Failed to write dump.\n");

    INFO("Dumped aftermath blob to: %s\n", path);
    fclose(file);
}

static pthread_mutex_t aftermath_lock = PTHREAD_MUTEX_INITIALIZER;

void vkd3d_aftermath_register_spirv(const void *spirv, size_t size)
{
    GFSDK_Aftermath_SpirvCode code = { (void*)spirv, size };
    GFSDK_Aftermath_ShaderHash hash;
    char path[1024];
    FILE *file;

    aftermath_get_shader_hash_spirv(GFSDK_Aftermath_Version_API, &code, &hash);
    snprintf(path, sizeof(path), "%s/source_%"PRIu64".spv", aftermath_dump_path, hash.hash);

    file = fopen(path, "wbx");
    if (!file)
        return;

    fwrite(spirv, 1, size, file);
    fclose(file);
}

static void vkd3d_aftermath_get_data(PFN_GFSDK_Aftermath_SetData set_data, const char *path)
{
    FILE *file;
    size_t len;
    void *buf;

    file = fopen(path, "rb");
    if (!file)
    {
        ERR("Failed to open file: %s.\n", path);
        return;
    }

    fseek(file, 0, SEEK_END);
    len = ftell(file);
    rewind(file);

    buf = vkd3d_malloc(len);
    if (!buf)
    {
        ERR("Failed to allocate %zu bytes.\n", len);
        goto free_data;
    }

    if (fread(buf, 1, len, file) != len)
    {
        ERR("Failed to read file: %s.\n", path);
        goto free_data;
    }

    set_data(buf, len);

free_data:
    vkd3d_free(buf);
    fclose(file);
}

static void GFSDK_AFTERMATH_CALL vkd3d_aftermath_shader_debug_info_lookup(
        const struct GFSDK_Aftermath_ShaderDebugInfoIdentifier *identifier,
        PFN_GFSDK_Aftermath_SetData set_data, void *userdata)
{
    char path[1024];
    (void)userdata;
    snprintf(path, sizeof(path), "%s/shader_%016"PRIx64"%016"PRIx64".nvdbg", aftermath_dump_path,
             identifier->id[0], identifier->id[1]);
    vkd3d_aftermath_get_data(set_data, path);
}

static void GFSDK_AFTERMATH_CALL vkd3d_aftermath_shader_lookup(
        const struct GFSDK_Aftermath_ShaderHash *hash,
        PFN_GFSDK_Aftermath_SetData set_data, void *userdata)
{
    char path[1024];
    (void)userdata;
    snprintf(path, sizeof(path), "%s/source_%"PRIu64".spv", aftermath_dump_path, hash->hash);
    vkd3d_aftermath_get_data(set_data, path);
}

static void GFSDK_AFTERMATH_CALL vkd3d_aftermath_gpu_crash_dump_callback(
        const void *dump, const uint32_t dump_size, void *userdata)
{
    GFSDK_Aftermath_GpuCrashDump_Decoder decoder;
    uint32_t json_size;
    char *json;

    (void)userdata;
    ERR("Got GPU crash!\n");

    if (aftermath_create_decoder(GFSDK_Aftermath_Version_API, dump, dump_size, &decoder) !=
            GFSDK_Aftermath_Result_Success)
    {
        ERR("Failed to create dump decoder.\n");
        return;
    }

    if (aftermath_generate_json(decoder, GFSDK_Aftermath_GpuCrashDumpDecoderFlags_ALL_INFO,
            GFSDK_Aftermath_GpuCrashDumpFormatterFlags_UTF8_OUTPUT,
            vkd3d_aftermath_shader_debug_info_lookup,
            vkd3d_aftermath_shader_lookup,
            NULL, NULL,
            NULL, &json_size) != GFSDK_Aftermath_Result_Success)
    {
        goto destroy_decoder;
    }

    json = vkd3d_malloc(json_size);
    aftermath_get_json(decoder, json_size, json);

    vkd3d_aftermath_dump_buffer(json, json_size, NULL, true);
    vkd3d_aftermath_dump_buffer(dump, dump_size, NULL, false);

    vkd3d_free(json);

destroy_decoder:
    aftermath_destroy_decoder(decoder);
}

static void GFSDK_AFTERMATH_CALL vkd3d_aftermath_shader_debug_info_callback(
        const void *debug_info, const uint32_t debug_info_size, void *userdata)
{
    GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier;
    (void)userdata;
    ERR("Got GPU shader debug info!\n");
    pthread_mutex_lock(&aftermath_lock);

    memset(&identifier, 0, sizeof(identifier));
    aftermath_get_identifier(GFSDK_Aftermath_Version_API,
            debug_info, debug_info_size,
            &identifier);

    vkd3d_aftermath_dump_buffer(debug_info, debug_info_size, &identifier, false);
    pthread_mutex_unlock(&aftermath_lock);
}

static void GFSDK_AFTERMATH_CALL vkd3d_aftermath_crash_dump_description_callback(
        PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription add_value, void *userdata)
{
    add_value(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, "vkd3d-proton");
    (void)userdata;
}

static void vkd3d_aftermath_init_library_once(void)
{
    const char *aftermath_path;
    aftermath_dump_path = getenv("VKD3D_AFTERMATH");
    if (!aftermath_dump_path)
    {
        ERR("VKD3D_AFTERMATH is not set.\n");
        return;
    }

#ifdef _WIN64
    aftermath_path = "GFSDK_Aftermath_Lib.x64.dll";
#elif defined(_WIN32)
    aftermath_path = "GFSDK_Aftermath_Lib.x86.dll";
#elif defined(__x86_64) || defined(__x86_64__) || defined(__amd64) || defined(__amd64__)
    aftermath_path = "libGFSDK_Aftermath_Lib.x64.so";
#else
    aftermath_path = "libGFSDK_Aftermath_Lib.x86.so";
#endif

    aftermath_module = vkd3d_dlopen(aftermath_path);
    if (aftermath_module)
    {
        aftermath_enable_cb =
                (PFN_GFSDK_Aftermath_EnableGpuCrashDumps)vkd3d_dlsym(aftermath_module,
                  "GFSDK_Aftermath_EnableGpuCrashDumps");
    }

    if (aftermath_enable_cb)
    {
        aftermath_get_identifier = (PFN_GFSDK_Aftermath_GetShaderDebugInfoIdentifier)
                vkd3d_dlsym(aftermath_module, "GFSDK_Aftermath_GetShaderDebugInfoIdentifier");
        aftermath_get_json = (PFN_GFSDK_Aftermath_GpuCrashDump_GetJSON)
                vkd3d_dlsym(aftermath_module, "GFSDK_Aftermath_GpuCrashDump_GetJSON");
        aftermath_generate_json = (PFN_GFSDK_Aftermath_GpuCrashDump_GenerateJSON)
                vkd3d_dlsym(aftermath_module, "GFSDK_Aftermath_GpuCrashDump_GenerateJSON");
        aftermath_create_decoder = (PFN_GFSDK_Aftermath_GpuCrashDump_CreateDecoder)
                vkd3d_dlsym(aftermath_module, "GFSDK_Aftermath_GpuCrashDump_CreateDecoder");
        aftermath_destroy_decoder = (PFN_GFSDK_Aftermath_GpuCrashDump_DestroyDecoder)
                vkd3d_dlsym(aftermath_module, "GFSDK_Aftermath_GpuCrashDump_DestroyDecoder");
        aftermath_get_shader_hash_spirv = (PFN_GFSDK_Aftermath_GetShaderHashSpirv)
                vkd3d_dlsym(aftermath_module, "GFSDK_Aftermath_GetShaderHashSpirv");

        aftermath_enabled = aftermath_get_identifier &&
                aftermath_create_decoder &&
                aftermath_destroy_decoder &&
                aftermath_generate_json &&
                aftermath_get_json &&
                aftermath_get_shader_hash_spirv;

        if (!aftermath_enabled)
            return;

        if (aftermath_enable_cb(GFSDK_Aftermath_Version_API, GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_Vulkan,
                GFSDK_Aftermath_GpuCrashDumpFeatureFlags_Default,
                vkd3d_aftermath_gpu_crash_dump_callback,
                vkd3d_aftermath_shader_debug_info_callback,
                vkd3d_aftermath_crash_dump_description_callback, NULL) != GFSDK_Aftermath_Result_Success)
        {
            ERR("Failed to initialize Aftermath.\n");
        }
    }
    else
    {
        ERR("Could not open Aftermath library %s.\n", aftermath_path);
    }
}

bool vkd3d_aftermath_init_library(VkDeviceDiagnosticsConfigCreateInfoNV *diagnostics)
{
    pthread_once(&aftermath_init_once, vkd3d_aftermath_init_library_once);
    diagnostics->sType = VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV;
    diagnostics->pNext = NULL;
    diagnostics->flags =
            VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV |
            VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV |
            VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV;
    return aftermath_enabled;
}

