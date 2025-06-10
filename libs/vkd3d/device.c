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

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API

#include "vkd3d_private.h"
#include "vkd3d_sonames.h"
#include "vkd3d_descriptor_debug.h"
#include "vkd3d_platform.h"

#ifdef VKD3D_ENABLE_RENDERDOC
#include "vkd3d_renderdoc.h"
#endif

static uint32_t vkd3d_get_vk_version(void)
{
    int major, minor, patch;

    vkd3d_parse_version(PACKAGE_VERSION, &major, &minor, &patch);
    INFO("vkd3d-proton - applicationVersion: %d.%d.%d.\n", major, minor, patch);
    return VK_MAKE_VERSION(major, minor, patch);
}

struct vkd3d_optional_extension_info
{
    const char *extension_name;
    ptrdiff_t vulkan_info_offset;
    uint64_t enable_config_flags;
    uint64_t disable_config_flags;
    uint32_t minimum_spec_version;
};

#define VK_EXTENSION(name, member) \
        {VK_ ## name ## _EXTENSION_NAME, offsetof(struct vkd3d_vulkan_info, member), 0, 0}

#define VK_EXTENSION_COND(name, member, required_flags) \
        {VK_ ## name ## _EXTENSION_NAME, offsetof(struct vkd3d_vulkan_info, member), required_flags, 0}
#define VK_EXTENSION_DISABLE_COND(name, member, disable_flags) \
        {VK_ ## name ## _EXTENSION_NAME, offsetof(struct vkd3d_vulkan_info, member), 0, disable_flags, 0}
#define VK_EXTENSION_VERSION(name, member, spec_version) \
        {VK_ ## name ## _EXTENSION_NAME, offsetof(struct vkd3d_vulkan_info, member), 0, 0, spec_version}

static const struct vkd3d_optional_extension_info optional_instance_extensions[] =
{
    /* EXT extensions */
    VK_EXTENSION_COND(EXT_DEBUG_UTILS, EXT_debug_utils, VKD3D_CONFIG_FLAG_DEBUG_UTILS | VKD3D_CONFIG_FLAG_FAULT),
};

static const struct vkd3d_optional_extension_info optional_device_extensions[] =
{
    /* KHR extensions */
    VK_EXTENSION(KHR_PUSH_DESCRIPTOR, KHR_push_descriptor),
    VK_EXTENSION(KHR_PIPELINE_LIBRARY, KHR_pipeline_library),
    VK_EXTENSION_DISABLE_COND(KHR_RAY_TRACING_PIPELINE, KHR_ray_tracing_pipeline, VKD3D_CONFIG_FLAG_NO_DXR),
    VK_EXTENSION_DISABLE_COND(KHR_ACCELERATION_STRUCTURE, KHR_acceleration_structure, VKD3D_CONFIG_FLAG_NO_DXR),
    VK_EXTENSION_DISABLE_COND(KHR_DEFERRED_HOST_OPERATIONS, KHR_deferred_host_operations, VKD3D_CONFIG_FLAG_NO_DXR),
    VK_EXTENSION_DISABLE_COND(KHR_RAY_QUERY, KHR_ray_query, VKD3D_CONFIG_FLAG_NO_DXR),
    VK_EXTENSION_DISABLE_COND(KHR_RAY_TRACING_MAINTENANCE_1, KHR_ray_tracing_maintenance1, VKD3D_CONFIG_FLAG_NO_DXR),
    VK_EXTENSION(KHR_FRAGMENT_SHADING_RATE, KHR_fragment_shading_rate),
    VK_EXTENSION(KHR_FRAGMENT_SHADER_BARYCENTRIC, KHR_fragment_shader_barycentric),
    VK_EXTENSION(KHR_PRESENT_ID, KHR_present_id),
    VK_EXTENSION(KHR_PRESENT_WAIT, KHR_present_wait),
    VK_EXTENSION(KHR_MAINTENANCE_5, KHR_maintenance5),
    VK_EXTENSION(KHR_MAINTENANCE_6, KHR_maintenance6),
    VK_EXTENSION(KHR_MAINTENANCE_7, KHR_maintenance7),
    VK_EXTENSION(KHR_MAINTENANCE_8, KHR_maintenance8),
    VK_EXTENSION(KHR_SHADER_MAXIMAL_RECONVERGENCE, KHR_shader_maximal_reconvergence),
    VK_EXTENSION(KHR_SHADER_QUAD_CONTROL, KHR_shader_quad_control),
    VK_EXTENSION(KHR_COMPUTE_SHADER_DERIVATIVES, KHR_compute_shader_derivatives),
    VK_EXTENSION(KHR_CALIBRATED_TIMESTAMPS, KHR_calibrated_timestamps),
    VK_EXTENSION(KHR_COOPERATIVE_MATRIX, KHR_cooperative_matrix),
#ifdef _WIN32
    VK_EXTENSION(KHR_EXTERNAL_MEMORY_WIN32, KHR_external_memory_win32),
    VK_EXTENSION(KHR_EXTERNAL_SEMAPHORE_WIN32, KHR_external_semaphore_win32),
#endif
    /* EXT extensions */
    VK_EXTENSION(EXT_CONDITIONAL_RENDERING, EXT_conditional_rendering),
    VK_EXTENSION(EXT_CONSERVATIVE_RASTERIZATION, EXT_conservative_rasterization),
    VK_EXTENSION(EXT_CUSTOM_BORDER_COLOR, EXT_custom_border_color),
    VK_EXTENSION(EXT_DEPTH_CLIP_ENABLE, EXT_depth_clip_enable),
    VK_EXTENSION(EXT_DEVICE_GENERATED_COMMANDS, EXT_device_generated_commands),
    VK_EXTENSION(EXT_IMAGE_VIEW_MIN_LOD, EXT_image_view_min_lod),
    VK_EXTENSION(EXT_ROBUSTNESS_2, EXT_robustness2),
    VK_EXTENSION(EXT_SHADER_STENCIL_EXPORT, EXT_shader_stencil_export),
    VK_EXTENSION(EXT_TRANSFORM_FEEDBACK, EXT_transform_feedback),
    VK_EXTENSION(EXT_VERTEX_ATTRIBUTE_DIVISOR, EXT_vertex_attribute_divisor),
    VK_EXTENSION(EXT_EXTENDED_DYNAMIC_STATE_2, EXT_extended_dynamic_state2),
    VK_EXTENSION(EXT_EXTENDED_DYNAMIC_STATE_3, EXT_extended_dynamic_state3),
    VK_EXTENSION(EXT_EXTERNAL_MEMORY_HOST, EXT_external_memory_host),
    VK_EXTENSION(EXT_SHADER_IMAGE_ATOMIC_INT64, EXT_shader_image_atomic_int64),
    VK_EXTENSION(EXT_MESH_SHADER, EXT_mesh_shader),
    VK_EXTENSION(EXT_MUTABLE_DESCRIPTOR_TYPE, EXT_mutable_descriptor_type),
    VK_EXTENSION(EXT_HDR_METADATA, EXT_hdr_metadata),
    VK_EXTENSION(EXT_SHADER_MODULE_IDENTIFIER, EXT_shader_module_identifier),
    VK_EXTENSION(EXT_DESCRIPTOR_BUFFER, EXT_descriptor_buffer),
    VK_EXTENSION_DISABLE_COND(EXT_PIPELINE_LIBRARY_GROUP_HANDLES, EXT_pipeline_library_group_handles, VKD3D_CONFIG_FLAG_NO_DXR),
    VK_EXTENSION(EXT_IMAGE_SLICED_VIEW_OF_3D, EXT_image_sliced_view_of_3d),
    VK_EXTENSION(EXT_GRAPHICS_PIPELINE_LIBRARY, EXT_graphics_pipeline_library),
    VK_EXTENSION(EXT_FRAGMENT_SHADER_INTERLOCK, EXT_fragment_shader_interlock),
    VK_EXTENSION(EXT_PAGEABLE_DEVICE_LOCAL_MEMORY, EXT_pageable_device_local_memory),
    VK_EXTENSION(EXT_MEMORY_PRIORITY, EXT_memory_priority),
    VK_EXTENSION(EXT_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS, EXT_dynamic_rendering_unused_attachments),
    VK_EXTENSION(EXT_LINE_RASTERIZATION, EXT_line_rasterization),
    VK_EXTENSION(EXT_IMAGE_COMPRESSION_CONTROL, EXT_image_compression_control),
    VK_EXTENSION_COND(EXT_DEVICE_FAULT, EXT_device_fault, VKD3D_CONFIG_FLAG_FAULT),
    VK_EXTENSION(EXT_MEMORY_BUDGET, EXT_memory_budget),
    VK_EXTENSION_COND(EXT_DEVICE_ADDRESS_BINDING_REPORT, EXT_device_address_binding_report, VKD3D_CONFIG_FLAG_FAULT),
    VK_EXTENSION(EXT_DEPTH_BIAS_CONTROL, EXT_depth_bias_control),
    VK_EXTENSION(EXT_ZERO_INITIALIZE_DEVICE_MEMORY, EXT_zero_initialize_device_memory),
    VK_EXTENSION_COND(EXT_OPACITY_MICROMAP, EXT_opacity_micromap, VKD3D_CONFIG_FLAG_DXR_1_2),
    VK_EXTENSION(EXT_SHADER_FLOAT8, EXT_shader_float8),
    /* AMD extensions */
    VK_EXTENSION(AMD_BUFFER_MARKER, AMD_buffer_marker),
    VK_EXTENSION(AMD_DEVICE_COHERENT_MEMORY, AMD_device_coherent_memory),
    VK_EXTENSION(AMD_SHADER_CORE_PROPERTIES, AMD_shader_core_properties),
    VK_EXTENSION(AMD_SHADER_CORE_PROPERTIES_2, AMD_shader_core_properties2),
    /* NV extensions */
    VK_EXTENSION(NV_OPTICAL_FLOW, NV_optical_flow),
    VK_EXTENSION(NV_SHADER_SM_BUILTINS, NV_shader_sm_builtins),
    VK_EXTENSION(NVX_BINARY_IMPORT, NVX_binary_import),
    VK_EXTENSION(NVX_IMAGE_VIEW_HANDLE, NVX_image_view_handle),
    VK_EXTENSION(NV_FRAGMENT_SHADER_BARYCENTRIC, NV_fragment_shader_barycentric),
    VK_EXTENSION(NV_COMPUTE_SHADER_DERIVATIVES, NV_compute_shader_derivatives),
    VK_EXTENSION_COND(NV_DEVICE_DIAGNOSTIC_CHECKPOINTS, NV_device_diagnostic_checkpoints, VKD3D_CONFIG_FLAG_BREADCRUMBS | VKD3D_CONFIG_FLAG_BREADCRUMBS_TRACE),
    VK_EXTENSION(NV_DEVICE_GENERATED_COMMANDS, NV_device_generated_commands),
    VK_EXTENSION(NV_SHADER_SUBGROUP_PARTITIONED, NV_shader_subgroup_partitioned),
    VK_EXTENSION(NV_MEMORY_DECOMPRESSION, NV_memory_decompression),
    VK_EXTENSION(NV_DEVICE_GENERATED_COMMANDS_COMPUTE, NV_device_generated_commands_compute),
    VK_EXTENSION_VERSION(NV_LOW_LATENCY_2, NV_low_latency2, 2),
    VK_EXTENSION(NV_RAW_ACCESS_CHAINS, NV_raw_access_chains),
    VK_EXTENSION(NV_COOPERATIVE_MATRIX_2, NV_cooperative_matrix2),
    /* VALVE extensions */
    VK_EXTENSION(VALVE_MUTABLE_DESCRIPTOR_TYPE, VALVE_mutable_descriptor_type),
    /* MESA extensions */
    VK_EXTENSION(MESA_IMAGE_ALIGNMENT_CONTROL, MESA_image_alignment_control),
};

static const struct vkd3d_optional_extension_info optional_extensions_user[] =
{
    VK_EXTENSION(EXT_SURFACE_MAINTENANCE_1, EXT_surface_maintenance1),
    VK_EXTENSION(EXT_SWAPCHAIN_MAINTENANCE_1, EXT_swapchain_maintenance1),
};

static unsigned int get_spec_version(const VkExtensionProperties *extensions,
        unsigned int count, const char *extension_name)
{
    unsigned int i;

    for (i = 0; i < count; ++i)
    {
        if (!strcmp(extensions[i].extensionName, extension_name))
            return extensions[i].specVersion;
    }
    return 0;
}

static bool is_extension_disabled(const char *extension_name)
{
    char disabled_extensions[VKD3D_PATH_MAX];

    if (!vkd3d_get_env_var("VKD3D_DISABLE_EXTENSIONS", disabled_extensions, sizeof(disabled_extensions)))
        return false;

    return vkd3d_debug_list_has_member(disabled_extensions, extension_name);
}

static bool has_extension(const VkExtensionProperties *extensions,
        unsigned int count, const char *extension_name, uint32_t minimum_spec_version)
{
    unsigned int i;

    for (i = 0; i < count; ++i)
    {
        if (is_extension_disabled(extension_name))
        {
            WARN("Extension %s is disabled.\n", debugstr_a(extension_name));
            continue;
        }
        if (!strcmp(extensions[i].extensionName, extension_name) &&
                (extensions[i].specVersion >= minimum_spec_version))
            return true;
    }
    return false;
}

static unsigned int vkd3d_check_extensions(const VkExtensionProperties *extensions, unsigned int count,
        const char * const *required_extensions, unsigned int required_extension_count,
        const struct vkd3d_optional_extension_info *optional_extensions, unsigned int optional_extension_count,
        const char * const *user_extensions, unsigned int user_extension_count,
        const char * const *optional_user_extensions, unsigned int optional_user_extension_count,
        bool *user_extension_supported, struct vkd3d_vulkan_info *vulkan_info, const char *extension_type)
{
    unsigned int extension_count = 0;
    unsigned int i;

    for (i = 0; i < required_extension_count; ++i)
    {
        if (!has_extension(extensions, count, required_extensions[i], 0))
            ERR("Required %s extension %s is not supported.\n",
                    extension_type, debugstr_a(required_extensions[i]));
        ++extension_count;
    }

    for (i = 0; i < optional_extension_count; ++i)
    {
        uint64_t disable_flags = optional_extensions[i].disable_config_flags;
        const char *extension_name = optional_extensions[i].extension_name;
        uint64_t enable_flags = optional_extensions[i].enable_config_flags;
        uint32_t minimum_spec_version = optional_extensions[i].minimum_spec_version;
        ptrdiff_t offset = optional_extensions[i].vulkan_info_offset;
        bool *supported = (void *)((uintptr_t)vulkan_info + offset);

        if (enable_flags && !(vkd3d_config_flags & enable_flags))
            continue;
        if (disable_flags && (vkd3d_config_flags & disable_flags))
            continue;

        if ((*supported = has_extension(extensions, count, extension_name, minimum_spec_version)))
        {
            TRACE("Found %s extension.\n", debugstr_a(extension_name));
            ++extension_count;
        }
    }

    for (i = 0; i < user_extension_count; ++i)
    {
        if (!has_extension(extensions, count, user_extensions[i], 0))
            ERR("Required user %s extension %s is not supported.\n",
                    extension_type, debugstr_a(user_extensions[i]));
        ++extension_count;
    }

    assert(!optional_user_extension_count || user_extension_supported);
    for (i = 0; i < optional_user_extension_count; ++i)
    {
        if (has_extension(extensions, count, optional_user_extensions[i], 0))
        {
            user_extension_supported[i] = true;
            ++extension_count;
        }
        else
        {
            user_extension_supported[i] = false;
            WARN("Optional user %s extension %s is not supported.\n",
                    extension_type, debugstr_a(optional_user_extensions[i]));
        }
    }

    return extension_count;
}

static unsigned int vkd3d_append_extension(const char *extensions[],
        unsigned int extension_count, const char *extension_name)
{
    unsigned int i;

    /* avoid duplicates */
    for (i = 0; i < extension_count; ++i)
    {
        if (!strcmp(extensions[i], extension_name))
            return extension_count;
    }

    extensions[extension_count++] = extension_name;
    return extension_count;
}

static void vkd3d_mark_enabled_user_extensions(struct vkd3d_vulkan_info *vulkan_info,
        const char * const *optional_user_extensions,
        unsigned int optional_user_extension_count,
        const bool *user_extension_supported)
{
    unsigned int i, j;
    for (i = 0; i < optional_user_extension_count; ++i)
    {
        if (!user_extension_supported[i])
            continue;

        /* Mark these external extensions as supported if outer code explicitly requested them,
         * otherwise, ignore. */
        for (j = 0; j < ARRAY_SIZE(optional_extensions_user); j++)
        {
            if (!strcmp(optional_extensions_user[j].extension_name, optional_user_extensions[i]))
            {
                ptrdiff_t offset = optional_extensions_user[j].vulkan_info_offset;
                bool *supported = (void *)((uintptr_t)vulkan_info + offset);
                *supported = true;
                break;
            }
        }
    }
}

static unsigned int vkd3d_enable_extensions(const char *extensions[],
        const char * const *required_extensions, unsigned int required_extension_count,
        const struct vkd3d_optional_extension_info *optional_extensions, unsigned int optional_extension_count,
        const char * const *user_extensions, unsigned int user_extension_count,
        const char * const *optional_user_extensions, unsigned int optional_user_extension_count,
        const bool *user_extension_supported, const struct vkd3d_vulkan_info *vulkan_info)
{
    unsigned int extension_count = 0;
    unsigned int i;

    for (i = 0; i < required_extension_count; ++i)
    {
        extensions[extension_count++] = required_extensions[i];
    }
    for (i = 0; i < optional_extension_count; ++i)
    {
        ptrdiff_t offset = optional_extensions[i].vulkan_info_offset;
        const bool *supported = (void *)((uintptr_t)vulkan_info + offset);

        if (*supported)
            extensions[extension_count++] = optional_extensions[i].extension_name;
    }

    for (i = 0; i < user_extension_count; ++i)
    {
        extension_count = vkd3d_append_extension(extensions, extension_count, user_extensions[i]);
    }
    assert(!optional_user_extension_count || user_extension_supported);
    for (i = 0; i < optional_user_extension_count; ++i)
    {
        if (!user_extension_supported[i])
            continue;
        extension_count = vkd3d_append_extension(extensions, extension_count, optional_user_extensions[i]);
    }

    return extension_count;
}

static bool vkd3d_remove_extension(const char *to_remove, const char *extensions[], uint32_t *extension_count)
{
    uint32_t i;

    for (i = 0; i < *extension_count; i++)
    {
        if (strcmp(to_remove, extensions[i]) != 0)
            continue;

        WARN("Removing %s extension from the array.\n", to_remove);

        if (i < (*extension_count - 1))
            extensions[i] = extensions[*extension_count - 1];

        (*extension_count)--;

        return true;
    }

    return false;
}

static bool vkd3d_disable_nvx_extensions(struct d3d12_device *device, const char *extensions[], uint32_t *enabled_extension_count)
{
    struct vkd3d_vulkan_info *vk_info = &device->vk_info;
    bool disabled = false;
    unsigned int i;

    static const struct vkd3d_optional_extension_info nvx_extensions[] =
    {
        VK_EXTENSION(NVX_BINARY_IMPORT, NVX_binary_import),
        VK_EXTENSION(NVX_IMAGE_VIEW_HANDLE, NVX_image_view_handle),
    };

    for (i = 0; i < ARRAY_SIZE(nvx_extensions); ++i)
    {
        const char *extension_name = nvx_extensions[i].extension_name;
        bool *supported = (void *)((uintptr_t)vk_info + nvx_extensions[i].vulkan_info_offset);

        if (vkd3d_remove_extension(extension_name, extensions, enabled_extension_count))
        {
            *supported = false;
            disabled = true;
        }
    }

    return disabled;
}

static HRESULT vkd3d_init_instance_caps(struct vkd3d_instance *instance,
        const struct vkd3d_instance_create_info *create_info,
        uint32_t *instance_extension_count, bool *user_extension_supported)
{
    const struct vkd3d_vk_global_procs *vk_procs = &instance->vk_global_procs;
    struct vkd3d_vulkan_info *vulkan_info = &instance->vk_info;
    VkExtensionProperties *vk_extensions;
    uint32_t count;
    VkResult vr;

    memset(vulkan_info, 0, sizeof(*vulkan_info));
    *instance_extension_count = 0;

    if ((vr = vk_procs->vkEnumerateInstanceExtensionProperties(NULL, &count, NULL)) < 0)
    {
        ERR("Failed to enumerate instance extensions, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }
    if (!count)
        return S_OK;

    if (!(vk_extensions = vkd3d_calloc(count, sizeof(*vk_extensions))))
        return E_OUTOFMEMORY;

    TRACE("Enumerating %u instance extensions.\n", count);
    if ((vr = vk_procs->vkEnumerateInstanceExtensionProperties(NULL, &count, vk_extensions)) < 0)
    {
        ERR("Failed to enumerate instance extensions, vr %d.\n", vr);
        vkd3d_free(vk_extensions);
        return hresult_from_vk_result(vr);
    }

    *instance_extension_count = vkd3d_check_extensions(vk_extensions, count, NULL, 0,
            optional_instance_extensions, ARRAY_SIZE(optional_instance_extensions),
            create_info->instance_extensions,
            create_info->instance_extension_count,
            create_info->optional_instance_extensions,
            create_info->optional_instance_extension_count,
            user_extension_supported, vulkan_info, "instance");

    vkd3d_free(vk_extensions);
    return S_OK;
}

static HRESULT vkd3d_init_vk_global_procs(struct vkd3d_instance *instance,
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr)
{
    if (!vkGetInstanceProcAddr)
        return E_INVALIDARG;

    return vkd3d_load_vk_global_procs(&instance->vk_global_procs, vkGetInstanceProcAddr);
}

bool vkd3d_debug_control_mute_message_id(const char *vuid);
bool vkd3d_debug_control_is_test_suite(void);
bool vkd3d_debug_control_explode_on_vvl_error(void);

static VkBool32 VKAPI_PTR vkd3d_debug_messenger_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
        VkDebugUtilsMessageTypeFlagsEXT message_types,
        const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
        void *userdata)
{
    unsigned int i;

    if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        if (callback_data->pMessageIdName)
            if (vkd3d_debug_control_mute_message_id(callback_data->pMessageIdName))
                return VK_FALSE;

        ERR("%s: %s\n", callback_data->pMessageIdName, callback_data->pMessage);

        for (i = 0; i < callback_data->cmdBufLabelCount; i++)
            ERR("Label #%u: %s\n", i, callback_data->pCmdBufLabels[i].pLabelName);

        for (i = 0; i < callback_data->objectCount; i++)
        {
            ERR("Object #%u: type %u, %s\n", i,
                    callback_data->pObjects[i].objectType,
                    callback_data->pObjects[i].pObjectName);
        }

        if (vkd3d_debug_control_explode_on_vvl_error())
            abort();
    }
    else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        WARN("%s\n", callback_data->pMessage);

    (void)userdata;
    (void)message_types;
    return VK_FALSE;
}

static void vkd3d_init_debug_messenger_callback(struct vkd3d_instance *instance)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &instance->vk_procs;
    VkDebugUtilsMessengerCreateInfoEXT callback_info;
    VkInstance vk_instance = instance->vk_instance;
    VkDebugUtilsMessengerEXT callback;
    VkResult vr;

    callback_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    callback_info.pNext = NULL;
    callback_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    callback_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    callback_info.pfnUserCallback = vkd3d_debug_messenger_callback;
    callback_info.pUserData = NULL;
    callback_info.flags = 0;
    if ((vr = VK_CALL(vkCreateDebugUtilsMessengerEXT(vk_instance, &callback_info, NULL, &callback)) < 0))
    {
        WARN("Failed to create debug report callback, vr %d.\n", vr);
        return;
    }

    instance->vk_debug_callback = callback;
}

/* Could be a flag style enum if needed. */
enum vkd3d_application_feature_override
{
    VKD3D_APPLICATION_FEATURE_OVERRIDE_NONE = 0,
    VKD3D_APPLICATION_FEATURE_NO_DEFAULT_DXR_ON_DECK,
    VKD3D_APPLICATION_FEATURE_LIMIT_DXR_1_0,
    VKD3D_APPLICATION_FEATURE_DISABLE_NV_REFLEX,
    VKD3D_APPLICATION_FEATURE_MESH_SHADER_WITHOUT_BARYCENTRICS
};

static enum vkd3d_application_feature_override vkd3d_application_feature_override;
uint64_t vkd3d_config_flags;
struct vkd3d_shader_quirk_info vkd3d_shader_quirk_info;

struct vkd3d_instance_application_meta
{
    enum vkd3d_string_compare_mode mode;
    const char *name;
    uint64_t global_flags_add;
    uint64_t global_flags_remove;
    enum vkd3d_application_feature_override override;
};
static const struct vkd3d_instance_application_meta application_override[] = {
    /* MSVC fails to compile empty array. */
    { VKD3D_STRING_COMPARE_EXACT, "GravityMark.exe", VKD3D_CONFIG_FLAG_FORCE_MINIMUM_SUBGROUP_SIZE, 0 },
    /* Halo Infinite (1240440).
     * Game relies on NON_ZEROED committed UAVs to be cleared to zero on allocation.
     * This works okay with zerovram on first game boot, but not later, since this memory is guaranteed to be recycled.
     * Game also relies on indirectly modifying CBV root descriptors, which means we are forced to rely on RAW_VA_CBV.
     * It also relies on multi-dispatch indirect with state updates which is ... ye.
     * Need another config flag to workaround that as well.
     * Poor loading times and performance with ReBar on some devices.
     */
    { VKD3D_STRING_COMPARE_EXACT, "HaloInfinite.exe",
            VKD3D_CONFIG_FLAG_FORCE_RAW_VA_CBV |
            VKD3D_CONFIG_FLAG_USE_HOST_IMPORT_FALLBACK | VKD3D_CONFIG_FLAG_PREALLOCATE_SRV_MIP_CLAMPS |
            VKD3D_CONFIG_FLAG_REQUIRES_COMPUTE_INDIRECT_TEMPLATES | VKD3D_CONFIG_FLAG_NO_UPLOAD_HVV |
            VKD3D_CONFIG_FLAG_DISABLE_DGCC, 0 },
    /* (1182900) Workaround amdgpu kernel bug with host memory import and concurrent submissions. */
    { VKD3D_STRING_COMPARE_EXACT, "APlagueTaleRequiem_x64.exe",
            VKD3D_CONFIG_FLAG_USE_HOST_IMPORT_FALLBACK | VKD3D_CONFIG_FLAG_DISABLE_UAV_COMPRESSION, 0 },
    /* Shadow of the Tomb Raider (750920).
     * Invariant workarounds actually cause more issues than they resolve on NV.
     * RADV already has workarounds by default.
     * FIXME: The proper workaround will be a workaround which force-emits mul + add + precise. The vertex shaders
     * are broken enough that normal invariance is not enough. */
    { VKD3D_STRING_COMPARE_EXACT, "SOTTR.exe", VKD3D_CONFIG_FLAG_FORCE_NO_INVARIANT_POSITION, 0 },
    /* Elden Ring (1245620).
     * Game is really churny on committed memory allocations, and does not use NOT_ZEROED. Clearing works causes bubbles.
     * It seems to work just fine however to skip the clears. */
    { VKD3D_STRING_COMPARE_EXACT, "eldenring.exe",
            VKD3D_CONFIG_FLAG_MEMORY_ALLOCATOR_SKIP_CLEAR | VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_MISMATCH_DRIVER |
            VKD3D_CONFIG_FLAG_RECYCLE_COMMAND_POOLS, 0 },
    /* Serious Sam 4 (257420).
     * Invariant workarounds cause graphical glitches when rendering foliage on NV. */
    { VKD3D_STRING_COMPARE_EXACT, "Sam4.exe", VKD3D_CONFIG_FLAG_FORCE_NO_INVARIANT_POSITION | VKD3D_CONFIG_FLAG_SMALL_VRAM_REBAR, 0 },
    /* Cyberpunk 2077 (1091500). */
    { VKD3D_STRING_COMPARE_EXACT, "Cyberpunk2077.exe", VKD3D_CONFIG_FLAG_ALLOW_SBT_COLLECTION, 0 },
    /* Control (870780). Control fails to detect DXR if 1.1 is exposed. */
    { VKD3D_STRING_COMPARE_EXACT, "Control_DX12.exe", 0, 0, VKD3D_APPLICATION_FEATURE_LIMIT_DXR_1_0 },
    /* Hellblade: Senua's Sacrifice (414340). Enables RT by default if supported which is ... jarring and particularly jarring on Deck. */
    { VKD3D_STRING_COMPARE_EXACT, "HellbladeGame-Win64-Shipping.exe", 0, 0, VKD3D_APPLICATION_FEATURE_NO_DEFAULT_DXR_ON_DECK },
    /* Lost Judgment (2058190) */
    { VKD3D_STRING_COMPARE_EXACT, "LostJudgment.exe", VKD3D_CONFIG_FLAG_FORCE_INITIAL_TRANSITION, 0 },
    /* Marvel's Spider-Man Remastered (1817070) */
    { VKD3D_STRING_COMPARE_EXACT, "Spider-Man.exe", VKD3D_CONFIG_FLAG_FORCE_INITIAL_TRANSITION, 0 },
    /* Marvel’s Spider-Man: Miles Morales (1817190) */
    { VKD3D_STRING_COMPARE_EXACT, "MilesMorales.exe", VKD3D_CONFIG_FLAG_FORCE_INITIAL_TRANSITION, 0 },
    /* Deus Ex: Mankind United (337000) */
    { VKD3D_STRING_COMPARE_EXACT, "DXMD.exe", VKD3D_CONFIG_FLAG_FORCE_INITIAL_TRANSITION, 0 },
    /* Dead Space (2023) (1693980) */
    { VKD3D_STRING_COMPARE_EXACT, "Dead Space.exe", VKD3D_CONFIG_FLAG_FORCE_DEDICATED_IMAGE_ALLOCATION, 0 },
    /* Witcher 3 (2023) (292030) */
    { VKD3D_STRING_COMPARE_EXACT, "witcher3.exe", VKD3D_CONFIG_FLAG_DISABLE_SIMULTANEOUS_UAV_COMPRESSION, 0 },
    /* Age of Wonders 4 (1669000). Extremely stuttery performance with ReBAR. */
    { VKD3D_STRING_COMPARE_EXACT, "AOW4.exe", VKD3D_CONFIG_FLAG_NO_UPLOAD_HVV, 0 },
    /* Red Dead Redemption (2668510). Inconsistent performance with ReBAR at cutscenes of the game. */
    { VKD3D_STRING_COMPARE_EXACT, "RDR.exe", VKD3D_CONFIG_FLAG_NO_UPLOAD_HVV, 0 },
    /* Horizon Forbidden West (2420110). Work around RADV 23.3.1 that ships on stableOS at time of making WAR. */
    { VKD3D_STRING_COMPARE_EXACT, "HorizonForbiddenWest.exe", VKD3D_CONFIG_FLAG_DRIVER_VERSION_SENSITIVE_SHADERS, 0 },
    /* Starfield (1716740) */
    { VKD3D_STRING_COMPARE_EXACT, "Starfield.exe",
            VKD3D_CONFIG_FLAG_REQUIRES_COMPUTE_INDIRECT_TEMPLATES | VKD3D_CONFIG_FLAG_REJECT_PADDED_SMALL_RESOURCE_ALIGNMENT, 0 },
    /* Persona 3 Reload (2161700). Enables RT by default on Deck and does not run acceptably for a verified title. */
    { VKD3D_STRING_COMPARE_EXACT, "P3R.exe", 0, 0, VKD3D_APPLICATION_FEATURE_NO_DEFAULT_DXR_ON_DECK },
    /* Basically never bothers doing initial transitions.
     * GPU hang observed on RDNA1 cards at least during intro cutscene.
     * Game does not use UAV barrier between ClearUAV and GDeflate shader.
     * NVIDIA does not hit that particular hazard since it uses metacommand, but ClearUAV barrier
     * still works around sync issues. */
    { VKD3D_STRING_COMPARE_STARTS_WITH, "ffxvi", VKD3D_CONFIG_FLAG_FORCE_INITIAL_TRANSITION | VKD3D_CONFIG_FLAG_CLEAR_UAV_SYNC, 0 },
    /* World of Warcraft retail. Broken MSAA code where it renders to multi-sampled target with single sampled PSO. */
    { VKD3D_STRING_COMPARE_EXACT, "Wow.exe", VKD3D_CONFIG_FLAG_FORCE_DYNAMIC_MSAA, 0 },
    /* The Last of Us Part I (1888930). Submits hundreds of command buffers per frame. */
    { VKD3D_STRING_COMPARE_STARTS_WITH, "tlou-i", VKD3D_CONFIG_FLAG_NO_STAGGERED_SUBMIT, 0 },
    /* Skull and Bones (2853730). Seems to require unsupported dcomp when reflex is enabled for some reason *shrug */
    { VKD3D_STRING_COMPARE_EXACT, "skullandbones.exe", 0, 0, VKD3D_APPLICATION_FEATURE_DISABLE_NV_REFLEX },
    /* Star Wars Outlaws (2842040). Attempt to workaround a possible NV driver bug. */
    { VKD3D_STRING_COMPARE_EXACT, "Outlaws.exe", VKD3D_CONFIG_FLAG_ONE_TIME_SUBMIT, 0 },
    { VKD3D_STRING_COMPARE_EXACT, "Outlaws_Plus.exe", VKD3D_CONFIG_FLAG_ONE_TIME_SUBMIT, 0 },
    /* FFVII Rebirth (2909400).
     * Game can destroy PSOs while they are in-flight.
     * Also, add no-staggered since this is a UE title without the common workaround,
     * although that only seems to matter when FSR/DLSS injectors are used. */
    { VKD3D_STRING_COMPARE_EXACT, "ff7rebirth_.exe",
            VKD3D_CONFIG_FLAG_RETAIN_PSOS | VKD3D_CONFIG_FLAG_NO_STAGGERED_SUBMIT, 0,
            VKD3D_APPLICATION_FEATURE_MESH_SHADER_WITHOUT_BARYCENTRICS },
    /* There aren't many games that use mesh shaders outside of UE5 Nanite fallbacks.
     * UE5 is broken w.r.t. feature checks, so we have to do opt-in instead :( */
    { VKD3D_STRING_COMPARE_EXACT, "AlanWake2.exe", 0, 0, VKD3D_APPLICATION_FEATURE_MESH_SHADER_WITHOUT_BARYCENTRICS },
    /* Monster Hunter Wilds (2246340).
     * There is an impossible amdgpu bug with PRT sparse.
     * No upload HVV as a performance opt since it's very CPU intensive, and there's no obvious GPU uplift from this. */
    { VKD3D_STRING_COMPARE_EXACT, "MonsterHunterWilds.exe",
        VKD3D_CONFIG_FLAG_SKIP_NULL_SPARSE_TILES | VKD3D_CONFIG_FLAG_NO_UPLOAD_HVV, 0 },
    /* Wreckfest 2 (1203190). Aliases block-compressed textures with color images on the
     * same heap and expects image data to be interpreted consistently. */
    { VKD3D_STRING_COMPARE_EXACT, "Wreckfest2.exe", VKD3D_CONFIG_FLAG_PLACED_TEXTURE_ALIASING, 0 },
    /* Eve online. Uses DGC with CBV updates. Kinda questionable exename ... */
    { VKD3D_STRING_COMPARE_EXACT, "exefile.exe", VKD3D_CONFIG_FLAG_FORCE_RAW_VA_CBV, 0 },
    /* Unreal Engine catch-all. ReBAR is a massive uplift on RX 7600 for example in Wukong.
     * AMD windows drivers also seem to have some kind of general app-opt for UE titles.
     * Use no-staggered-submit by default on UE. We've only observed issues in Wukong here, but
     * unless we see proof that UE titles want staggered,
     * we'll disable for now to be defensive and de-risk any large scale regressions. */
    { VKD3D_STRING_COMPARE_ENDS_WITH, "-Win64-Shipping.exe",
            VKD3D_CONFIG_FLAG_SMALL_VRAM_REBAR | VKD3D_CONFIG_FLAG_NO_STAGGERED_SUBMIT, 0 },
    /* Rise of the Tomb Raider. Game renders and samples a texture at the same time */
    { VKD3D_STRING_COMPARE_EXACT, "ROTTR.exe", VKD3D_CONFIG_FLAG_DISABLE_COLOR_COMPRESSION, 0 },
    { VKD3D_STRING_COMPARE_NEVER, NULL, 0, 0 }
};

struct vkd3d_shader_quirk_meta
{
    enum vkd3d_string_compare_mode mode;
    const char *name;
    const struct vkd3d_shader_quirk_info *info;
};

static const struct vkd3d_shader_quirk_hash ue4_hashes[] = {
    { 0x08a323ee81c1e393ull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
    { 0x75dcbd76ee898815ull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
    { 0x6c37b5a66059b751ull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
    { 0xaf6d07d7b56a3effull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
    { 0xa48ead2a618e12d8ull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
    { 0xebfd864995d3fc07ull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
    { 0xcca7b582db60199cull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
};

static const struct vkd3d_shader_quirk_info ue4_quirks = {
    ue4_hashes, ARRAY_SIZE(ue4_hashes), 0,
};

static const struct vkd3d_shader_quirk_info f1_2019_2020_quirks = {
    NULL, 0, VKD3D_SHADER_QUIRK_FORCE_TGSM_BARRIERS,
};

static const struct vkd3d_shader_quirk_hash borderlands3_hashes[] = {
    /* Shader breaks due to floor(a / exp(x)) being refactored to floor(a * exp(-x))
     * and shader does not expect this.
     * See https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/19910. */
    { 0xbf0af7db6a7fb86bull, VKD3D_SHADER_QUIRK_FORCE_NOCONTRACT_MATH },
};

static const struct vkd3d_shader_quirk_info borderlands3_quirks = {
    borderlands3_hashes, ARRAY_SIZE(borderlands3_hashes), 0,
};

/* Terrain is rendered with extreme tessellation factors. Limit it to something more reasonable. */
static const struct vkd3d_shader_quirk_info team_ninja_quirks = {
    NULL, 0, VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_8,
};

/* More over-tessellated terrain, but base geometry is more coarse */
static const struct vkd3d_shader_quirk_info atelier_yumia_quirks = {
    NULL, 0, VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_16,
};

/* The subgroup check in CACAO shader is botched and does not handle Wave64 properly.
 * Just pretend the subgroup size is non-sensical to use the normal FFX CACAO code path. */
static const struct vkd3d_shader_quirk_hash re_hashes[] = {
    /* RE4 */
    { 0xa100b53736f9c1bfull, VKD3D_SHADER_QUIRK_FORCE_SUBGROUP_SIZE_1 },
    /* RE2 and RE7 */
    { 0x1c4c8782b75c498bull, VKD3D_SHADER_QUIRK_FORCE_SUBGROUP_SIZE_1 },
    /* Temporary driver workaround for RADV. See https://gitlab.freedesktop.org/mesa/mesa/-/issues/9852. */
    /* This shader trips on Mesa 23.0.3. */
    { 0xdb1593ced60da3f1ull, VKD3D_SHADER_QUIRK_REWRITE_GRAD_TO_BIAS },
    /* This shader hangs on Mesa main. */
    { 0x5784e9e2f7a76819ull, VKD3D_SHADER_QUIRK_REWRITE_GRAD_TO_BIAS },
    /* This shader hangs on RDNA1 */
    { 0x7b3cec4ba6d32cacull, VKD3D_SHADER_QUIRK_REWRITE_GRAD_TO_BIAS },
};

static const struct vkd3d_shader_quirk_info re_quirks = {
    re_hashes, ARRAY_SIZE(re_hashes), 0,
};

/* There are lots of shaders which cause random flicker due to bad 16-bit behavior.
 * These shaders really need 32-bit it seems to render properly, so just do that. */
static const struct vkd3d_shader_quirk_info re4_quirks = {
    re_hashes, ARRAY_SIZE(re_hashes), VKD3D_SHADER_QUIRK_FORCE_MIN16_AS_32BIT,
};

static const struct vkd3d_shader_quirk_hash mhr_hashes[] = {
    /* Shader is extremely sensitive to nocontract behavior.
     * There some places where catastrophic cancellation occurs
     * and one ULP difference is the difference between blown out bloom and not. */
    { 0xd892f8024f52d3ca, VKD3D_SHADER_QUIRK_FORCE_NOCONTRACT_MATH },
};

static const struct vkd3d_shader_quirk_info mhr_quirks = {
    mhr_hashes, ARRAY_SIZE(mhr_hashes), 0,
};

static const struct vkd3d_shader_quirk_hash witcher3_hashes[] = {
    /* In DXR path, the game will write VBO data in a CS which is then followed
     * by a VS -> tess -> geom pass that writes out data to a UAV in the GS.
     * There appears to be missing synchronization here by game (no UAV -> VBO barrier) and
     * forcing barriers fixes a ton of glitches on both NV and RADV. */
    { 0x2c16686e5d9b04a8, VKD3D_SHADER_QUIRK_FORCE_COMPUTE_BARRIER },
};

static const struct vkd3d_shader_quirk_info witcher3_quirks = {
    witcher3_hashes, ARRAY_SIZE(witcher3_hashes), 0,
};

static const struct vkd3d_shader_quirk_info heap_robustness_quirks = {
    NULL, 0, VKD3D_SHADER_QUIRK_DESCRIPTOR_HEAP_ROBUSTNESS,
};

static const struct vkd3d_shader_quirk_hash ac_mirage_hashes[] = {
    /* There is a write-after-read hazard.
     * Index buffer is being read from, and there is a compute shader afterwards
     * that writes to that index buffer without a barrier. */
    { 0x0cb130fa374982e3, VKD3D_SHADER_QUIRK_FORCE_PRE_RASTERIZATION_BARRIER },
};

static const struct vkd3d_shader_quirk_info ac_mirage_quirks = {
    ac_mirage_hashes, ARRAY_SIZE(ac_mirage_hashes), 0,
};

static const struct vkd3d_shader_quirk_hash ffxvi_hashes[] = {
    /* On RADV 24.1.6 RDNA3, we seem to be plagued with a compiler bug/hardware quirk.
     * It works on main, but only by chance.
     * https://gitlab.freedesktop.org/mesa/mesa/-/issues/11738. */
    { 0xa98606e01cdd5924, VKD3D_SHADER_QUIRK_DISABLE_OPTIMIZATIONS },
};

static const struct vkd3d_shader_quirk_info ffxvi_quirks = {
    ffxvi_hashes, ARRAY_SIZE(ffxvi_hashes),
};

/* Some shaders use precise, some don't, leading to invariance issues. */
static const struct vkd3d_shader_quirk_info hunt_quirks = {
    NULL, 0, VKD3D_SHADER_QUIRK_FORCE_NOCONTRACT_MATH_VS,
};

/* Hair strand shaders write to a UAV, then read it back in the same workgroup, but misses a device memory barrier in places,
 * leading to GPU hang. */
static const struct vkd3d_shader_quirk_info veilguard_quirks = {
    NULL, 0, VKD3D_SHADER_QUIRK_FORCE_DEVICE_MEMORY_BARRIER_THREAD_GROUP_COHERENCY,
};

static const struct vkd3d_shader_quirk_hash tfd_hashes[] = {
    /* ReflectionCaptureFilteredImportanceSamplingCS is somewhat broken as it assumes
     * that the lowest res mips are valid, but they are never written to by ReflectionCaptureGenerateMipmapCS.
     * It stops at 8x8 (the workgroup size). The workaround just clamps the explicit LOD to whatever mips is 8x8. */
    { 0x74b8eaf23e3d166c, VKD3D_SHADER_QUIRK_ASSUME_BROKEN_SUB_8x8_CUBE_MIPS },
};

static const struct vkd3d_shader_quirk_info tfd_quirks = {
    tfd_hashes, ARRAY_SIZE(tfd_hashes), 0,
};

/* Game loads a CBV array into alloca(), but then proceeds to access said alloca() array OOB. */
static const struct vkd3d_shader_quirk_info gzw_quirks = {
    NULL, 0, VKD3D_SHADER_QUIRK_FORCE_ROBUST_PHYSICAL_CBV_LOAD_FORWARDING,
};

static const struct vkd3d_shader_quirk_info starfield_quirks = {
    NULL, 0, VKD3D_SHADER_QUIRK_AGGRESSIVE_NONUNIFORM,
};

static const struct vkd3d_shader_quirk_hash rebirth_hashes[] = {
    /* GenerateMassiveEnvironmentBatchedNodesCS(). Missing barrier after a CS based clear.
     * Exactly same bug as before, but then it was ComputeBatchedMeshletOffsetsCS(). */
    { 0xe6cb9c843fa1bd18, VKD3D_SHADER_QUIRK_FORCE_PRE_COMPUTE_BARRIER },
    /* 1.003 update. Hash changed, but didn't fix the bug. */
    { 0xf047c7f2f4f32111, VKD3D_SHADER_QUIRK_FORCE_PRE_COMPUTE_BARRIER },
};

static const struct vkd3d_shader_quirk_info rebirth_quirks = {
    rebirth_hashes, ARRAY_SIZE(rebirth_hashes), 0,
};

/* Game misses a transition from color to resource before FSR3.
 * The shader hash is FSR3-PREPARE-INPUTS. */
static const struct vkd3d_shader_quirk_hash satisfactory_hashes[] = {
    { 0x1bc3c90cfe16ad1e, VKD3D_SHADER_QUIRK_FORCE_GRAPHICS_BARRIER },
};

static const struct vkd3d_shader_quirk_info satisfactory_quirks = {
    satisfactory_hashes, ARRAY_SIZE(satisfactory_hashes), 0,
};

static const struct vkd3d_shader_quirk_meta application_shader_quirks[] = {
    /* F1 2020 (1080110) */
    { VKD3D_STRING_COMPARE_EXACT, "F1_2020_dx12.exe", &f1_2019_2020_quirks },
    /* F1 2019 (928600) */
    { VKD3D_STRING_COMPARE_EXACT, "F1_2019_dx12.exe", &f1_2019_2020_quirks },
    /* Borderlands 3 (397540) */
    { VKD3D_STRING_COMPARE_EXACT, "Borderlands3.exe", &borderlands3_quirks },
    /* Wo Long: Fallen Dynasty (2285240) */
    { VKD3D_STRING_COMPARE_EXACT, "WoLong.exe", &team_ninja_quirks },
    /* Rise of the Ronin (1340990) */
    { VKD3D_STRING_COMPARE_EXACT, "Ronin.exe", &team_ninja_quirks },
    /* Resident Evil 2 (883710) */
    { VKD3D_STRING_COMPARE_EXACT, "re2.exe", &re_quirks },
    /* Resident Evil 7 (418370) */
    { VKD3D_STRING_COMPARE_EXACT, "re7.exe", &re_quirks },
    /* Resident Evil 4 (2050650) */
    { VKD3D_STRING_COMPARE_EXACT, "re4.exe", &re4_quirks },
    /* Monster Hunter Rise (1446780) */
    { VKD3D_STRING_COMPARE_EXACT, "MonsterHunterRise.exe", &mhr_quirks },
    /* Witcher 3 (2023) (292030) */
    { VKD3D_STRING_COMPARE_EXACT, "witcher3.exe", &witcher3_quirks },
    /* Pioneers of Pagonia (2155180) */
    { VKD3D_STRING_COMPARE_EXACT, "Pioneers of Pagonia.exe", &heap_robustness_quirks },
    /* AC: Mirage */
    { VKD3D_STRING_COMPARE_EXACT, "ACMirage.exe", &ac_mirage_quirks },
    { VKD3D_STRING_COMPARE_EXACT, "ACMirage_plus.exe", &ac_mirage_quirks },
    /* FF XVI. */
    { VKD3D_STRING_COMPARE_STARTS_WITH, "ffxvi", &ffxvi_quirks },
    /* Hunt: Showdown 1896 (594650) */
    { VKD3D_STRING_COMPARE_EXACT, "HuntGame.exe", &hunt_quirks },
    /* Dragon Age: The Veilguard (1845910) */
    { VKD3D_STRING_COMPARE_EXACT, "Dragon Age The Veilguard.exe", &veilguard_quirks },
    /* The First Descendant (2074920) */
    { VKD3D_STRING_COMPARE_EXACT, "M1-Win64-Shipping.exe", &tfd_quirks },
    /* Gray Zone Warfare (2479810) */
    { VKD3D_STRING_COMPARE_EXACT, "GZWClientSteam-Win64-Shipping.exe", &gzw_quirks },
    /* Starfield (1716740) */
    { VKD3D_STRING_COMPARE_EXACT, "Starfield.exe", &starfield_quirks },
    /* FFVII Rebirth (2909400). */
    { VKD3D_STRING_COMPARE_EXACT, "ff7rebirth_.exe", &rebirth_quirks },
    /* Atelier Yumia (3123410) */
    { VKD3D_STRING_COMPARE_EXACT, "Atelier_Yumia.exe", &atelier_yumia_quirks },
    /* Monster Hunter Wilds (2246340).
     * As a follow-up for SKIP_NULL_SPARSE, it seems possible that application
     * can end up loading bogus bindless indices from pages which should have been NULL.
     * Chasing through UMR wave dumps and captures,
     * we observe that a faulting index depends on a load from a sparse buffer.
     * This hasn't been confirmed to be a game bug or indirect vkd3d-proton bug,
     * but it's plausible enough to be caused by SKIP_NULL_SPARSE that we can justify this hack
     * until a proper fix is in place. */
    { VKD3D_STRING_COMPARE_EXACT, "MonsterHunterWilds.exe", &heap_robustness_quirks },
    /* Satisfactory (526870). */
    { VKD3D_STRING_COMPARE_EXACT, "FactoryGameSteam-Win64-Shipping.exe", &satisfactory_quirks },
    { VKD3D_STRING_COMPARE_EXACT, "FactoryGameEGS-Win64-Shipping.exe", &satisfactory_quirks },
    /* Unreal Engine 4 */
    { VKD3D_STRING_COMPARE_ENDS_WITH, "-Shipping.exe", &ue4_quirks },
    /* MSVC fails to compile empty array. */
    { VKD3D_STRING_COMPARE_NEVER, NULL, NULL },
};

static void vkd3d_instance_apply_application_workarounds(void)
{
    char app[VKD3D_PATH_MAX];
    size_t i;
    if (!vkd3d_get_program_name(app))
        return;

    INFO("Program name: \"%s\" (hash: %016"PRIx64")\n", app, hash_fnv1_iterate_string(hash_fnv1_init(), app));

    for (i = 0; i < ARRAY_SIZE(application_override); i++)
    {
        if (vkd3d_string_compare(application_override[i].mode, app, application_override[i].name))
        {
            vkd3d_config_flags |= application_override[i].global_flags_add;
            vkd3d_config_flags &= ~application_override[i].global_flags_remove;
            INFO("Detected game %s, adding config 0x%"PRIx64", removing masks 0x%"PRIx64".\n",
                 app, application_override[i].global_flags_add, application_override[i].global_flags_remove);
            vkd3d_application_feature_override = application_override[i].override;
            break;
        }
    }

    for (i = 0; i < ARRAY_SIZE(application_shader_quirks); i++)
    {
        if (vkd3d_string_compare(application_shader_quirks[i].mode, app, application_shader_quirks[i].name))
        {
            vkd3d_shader_quirk_info = *application_shader_quirks[i].info;
            INFO("Detected game %s, adding shader quirks for specific shaders.\n", app);
            break;
        }
    }
}

static void vkd3d_instance_deduce_config_flags_from_environment(void)
{
    char env[VKD3D_PATH_MAX];

    if (vkd3d_get_env_var("VKD3D_SHADER_OVERRIDE", env, sizeof(env)) ||
            vkd3d_get_env_var("VKD3D_SHADER_DUMP_PATH", env, sizeof(env)) ||
            vkd3d_get_env_var("VKD3D_QA_HASHES", env, sizeof(env)))
    {
        INFO("VKD3D_SHADER_OVERRIDE, VKD3D_SHADER_DUMP_PATH or VKD3D_QA_HASHES is used, pipeline_library_ignore_spirv option is enforced.\n");
        vkd3d_config_flags |= VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_SPIRV;
    }

    if (vkd3d_get_env_var("FOSSILIZE", env, sizeof(env)) && strcmp(env, "1") == 0 &&
            vkd3d_get_env_var("FOSSILIZE_DUMP_PATH", env, sizeof(env)))
    {
        INFO("Fossilize is enabled. pipeline_library_ignore_spirv option is enforced.\n");
        vkd3d_config_flags |= VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_SPIRV;
    }

    vkd3d_get_env_var("VKD3D_SHADER_CACHE_PATH", env, sizeof(env));
    if (strcmp(env, "0") == 0)
    {
        INFO("Shader cache is explicitly disabled, relying solely on application caches.\n");
        vkd3d_config_flags |= VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_APP_CACHE_ONLY;
    }

    /* If we're using a global shader cache, it's meaningless to use PSO caches. */
    if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_APP_CACHE_ONLY))
    {
        INFO("shader_cache is used, global_pipeline_cache is enforced.\n");
        vkd3d_config_flags |= VKD3D_CONFIG_FLAG_GLOBAL_PIPELINE_CACHE;
    }

    /* Normally, we would use VK_EXT_tooling_info for this, but we don't observe layers across the winevulkan layer.
     * The global env-var on the other hand, does ... */
    if (vkd3d_get_env_var("ENABLE_VULKAN_RENDERDOC_CAPTURE", env, sizeof(env)) &&
            strcmp(env, "1") == 0)
    {
        INFO("RenderDoc capture is enabled. Forcing HOST CACHED memory types and disabling pipeline caching completely.\n");
        vkd3d_config_flags |= VKD3D_CONFIG_FLAG_FORCE_HOST_CACHED |
                VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_APP_CACHE_ONLY |
                VKD3D_CONFIG_FLAG_GLOBAL_PIPELINE_CACHE |
                VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_NO_SERIALIZE_SPIRV |
                VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_SPIRV;
        vkd3d_config_flags |= VKD3D_CONFIG_FLAG_DEBUG_UTILS;
    }

    /* RADV_THREAD_TRACE_xxx are deprecated and will be removed at some point. */
    if (vkd3d_get_env_var("RADV_THREAD_TRACE", env, sizeof(env)) ||
            vkd3d_get_env_var("RADV_THREAD_TRACE_TRIGGER", env, sizeof(env)) ||
            (vkd3d_get_env_var("MESA_VK_TRACE", env, sizeof(env)) &&
                strcmp(env, "rgp") == 0))
    {
        INFO("RADV thread trace is enabled. Forcing debug utils to be enabled for labels.\n");
        /* Disable caching so we can get full debug information when emitting labels. */
        vkd3d_config_flags |= VKD3D_CONFIG_FLAG_DEBUG_UTILS |
                VKD3D_CONFIG_FLAG_GLOBAL_PIPELINE_CACHE |
                VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_APP_CACHE_ONLY |
                VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_NO_SERIALIZE_SPIRV |
                VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_SPIRV;
    }
}

static void vkd3d_instance_apply_global_shader_quirks(void)
{
    unsigned int level;
    char env[64];

    struct override
    {
        uint64_t config;
        uint32_t quirk;
        bool negative;
    };

    static const struct override overrides[] =
    {
        { VKD3D_CONFIG_FLAG_FORCE_NO_INVARIANT_POSITION, VKD3D_SHADER_QUIRK_INVARIANT_POSITION, true },
    };
    uint64_t eq_test;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(overrides); i++)
    {
        eq_test = overrides[i].negative ? 0 : overrides[i].config;
        if ((vkd3d_config_flags & overrides[i].config) == eq_test)
            vkd3d_shader_quirk_info.global_quirks |= overrides[i].quirk;
    }

    if (vkd3d_get_env_var("VKD3D_LIMIT_TESS_FACTORS", env, sizeof(env)))
    {
        static const struct
        {
            unsigned int level;
            uint32_t quirk;
        } mapping[] = {
            { 4, VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_4 },
            { 8, VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_8 },
            { 12, VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_12 },
            { 16, VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_16 },
            { 32, VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_32 },
        };

        /* Override what any app profile did. */
        vkd3d_shader_quirk_info.global_quirks &= ~(VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_4 |
                VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_8 |
                VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_12 |
                VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_16 |
                VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_32);

        level = strtoul(env, NULL, 0);
        INFO("Attempting to limit tessellation factors to %ux.\n", level);

        for (i = 0; i < ARRAY_SIZE(mapping); i++)
        {
            if (level <= mapping[i].level)
            {
                INFO("Limiting tessellation factors to %ux.\n", mapping[i].level);
                vkd3d_shader_quirk_info.global_quirks |= mapping[i].quirk;
                break;
            }
        }
    }
}

static const struct vkd3d_debug_option vkd3d_config_options[] =
{
    /* Enable Vulkan debug extensions. */
    {"vk_debug", VKD3D_CONFIG_FLAG_VULKAN_DEBUG},
    {"skip_application_workarounds", VKD3D_CONFIG_FLAG_SKIP_APPLICATION_WORKAROUNDS},
    {"debug_utils", VKD3D_CONFIG_FLAG_DEBUG_UTILS},
    {"force_static_cbv", VKD3D_CONFIG_FLAG_FORCE_STATIC_CBV},
    {"dxr", VKD3D_CONFIG_FLAG_DXR},
    {"dxr12", VKD3D_CONFIG_FLAG_DXR_1_2},
    {"nodxr", VKD3D_CONFIG_FLAG_NO_DXR},
    {"single_queue", VKD3D_CONFIG_FLAG_SINGLE_QUEUE},
    {"descriptor_qa_checks", VKD3D_CONFIG_FLAG_DESCRIPTOR_QA_CHECKS},
    {"no_upload_hvv", VKD3D_CONFIG_FLAG_NO_UPLOAD_HVV},
    {"log_memory_budget", VKD3D_CONFIG_FLAG_LOG_MEMORY_BUDGET},
    {"force_host_cached", VKD3D_CONFIG_FLAG_FORCE_HOST_CACHED},
    {"no_invariant_position", VKD3D_CONFIG_FLAG_FORCE_NO_INVARIANT_POSITION},
    {"global_pipeline_cache", VKD3D_CONFIG_FLAG_GLOBAL_PIPELINE_CACHE},
    {"pipeline_library_no_serialize_spirv", VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_NO_SERIALIZE_SPIRV},
    {"pipeline_library_sanitize_spirv", VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_SANITIZE_SPIRV},
    {"pipeline_library_log", VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG},
    {"pipeline_library_ignore_spirv", VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_SPIRV},
    {"mutable_single_set", VKD3D_CONFIG_FLAG_MUTABLE_SINGLE_SET},
    {"memory_allocator_skip_clear", VKD3D_CONFIG_FLAG_MEMORY_ALLOCATOR_SKIP_CLEAR},
    {"memory_allocator_skip_image_heap_clear", VKD3D_CONFIG_FLAG_MEMORY_ALLOCATOR_SKIP_IMAGE_HEAP_CLEAR},
    {"recycle_command_pools", VKD3D_CONFIG_FLAG_RECYCLE_COMMAND_POOLS},
    {"pipeline_library_ignore_mismatch_driver", VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_MISMATCH_DRIVER},
    {"breadcrumbs", VKD3D_CONFIG_FLAG_BREADCRUMBS},
    {"breadcrumbs_sync", VKD3D_CONFIG_FLAG_BREADCRUMBS | VKD3D_CONFIG_FLAG_BREADCRUMBS_SYNC},
    {"fault", VKD3D_CONFIG_FLAG_FAULT},
    {"pipeline_library_app_cache", VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_APP_CACHE_ONLY},
    {"shader_cache_sync", VKD3D_CONFIG_FLAG_SHADER_CACHE_SYNC},
    {"force_raw_va_cbv", VKD3D_CONFIG_FLAG_FORCE_RAW_VA_CBV},
    {"allow_sbt_collection", VKD3D_CONFIG_FLAG_ALLOW_SBT_COLLECTION},
    {"host_import_fallback", VKD3D_CONFIG_FLAG_USE_HOST_IMPORT_FALLBACK},
    {"preallocate_srv_mip_clamps", VKD3D_CONFIG_FLAG_PREALLOCATE_SRV_MIP_CLAMPS},
    {"force_initial_transition", VKD3D_CONFIG_FLAG_FORCE_INITIAL_TRANSITION},
    {"breadcrumbs_trace", VKD3D_CONFIG_FLAG_BREADCRUMBS | VKD3D_CONFIG_FLAG_BREADCRUMBS_TRACE},
    {"requires_compute_indirect_templates", VKD3D_CONFIG_FLAG_REQUIRES_COMPUTE_INDIRECT_TEMPLATES},
    {"skip_driver_workarounds", VKD3D_CONFIG_FLAG_SKIP_DRIVER_WORKAROUNDS},
    {"enable_experimental_features", VKD3D_CONFIG_FLAG_ENABLE_EXPERIMENTAL_FEATURES},
    {"reject_padded_small_resource_alignment", VKD3D_CONFIG_FLAG_REJECT_PADDED_SMALL_RESOURCE_ALIGNMENT},
    {"disable_simultaneous_uav_compression", VKD3D_CONFIG_FLAG_DISABLE_SIMULTANEOUS_UAV_COMPRESSION},
    {"disable_uav_compression", VKD3D_CONFIG_FLAG_DISABLE_UAV_COMPRESSION},
    {"disable_depth_compression", VKD3D_CONFIG_FLAG_DISABLE_DEPTH_COMPRESSION},
    {"disable_color_compression", VKD3D_CONFIG_FLAG_DISABLE_COLOR_COMPRESSION},
    {"app_debug_marker_only", VKD3D_CONFIG_FLAG_APP_DEBUG_MARKER_ONLY},
    {"small_vram_rebar", VKD3D_CONFIG_FLAG_SMALL_VRAM_REBAR},
    {"no_staggered_submit", VKD3D_CONFIG_FLAG_NO_STAGGERED_SUBMIT},
    {"clear_uav_sync", VKD3D_CONFIG_FLAG_CLEAR_UAV_SYNC},
    {"force_dynamic_msaa", VKD3D_CONFIG_FLAG_FORCE_DYNAMIC_MSAA},
    {"instruction_qa_checks", VKD3D_CONFIG_FLAG_INSTRUCTION_QA_CHECKS},
    {"transfer_queue", VKD3D_CONFIG_FLAG_TRANSFER_QUEUE},
    {"no_gpu_upload_heap", VKD3D_CONFIG_FLAG_NO_GPU_UPLOAD_HEAP},
    {"one_time_submit", VKD3D_CONFIG_FLAG_ONE_TIME_SUBMIT},
    {"skip_null_sparse_tiles", VKD3D_CONFIG_FLAG_SKIP_NULL_SPARSE_TILES},
    {"queue_profile_extra", VKD3D_CONFIG_FLAG_QUEUE_PROFILE_EXTRA},
    {"damage_not_zeroed_allocations", VKD3D_CONFIG_FLAG_DAMAGE_NOT_ZEROED_ALLOCATIONS},
};

static void vkd3d_config_flags_init_once(void)
{
    char config[VKD3D_PATH_MAX];

    vkd3d_get_env_var("VKD3D_CONFIG", config, sizeof(config));
    vkd3d_config_flags = vkd3d_parse_debug_options(config, vkd3d_config_options, ARRAY_SIZE(vkd3d_config_options));

    if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_SKIP_APPLICATION_WORKAROUNDS))
        vkd3d_instance_apply_application_workarounds();

    vkd3d_instance_deduce_config_flags_from_environment();
    vkd3d_instance_apply_global_shader_quirks();

    /* If we're going to use vk_debug, make sure we can get debug callbacks. */
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_VULKAN_DEBUG)
        vkd3d_config_flags |= VKD3D_CONFIG_FLAG_DEBUG_UTILS;

    if (vkd3d_config_flags)
        INFO("VKD3D_CONFIG='%s'.\n", config);
}

static pthread_once_t vkd3d_config_flags_once = PTHREAD_ONCE_INIT;

static void vkd3d_config_flags_init(void)
{
    pthread_once(&vkd3d_config_flags_once, vkd3d_config_flags_init_once);
}

static HRESULT vkd3d_instance_init(struct vkd3d_instance *instance,
        const struct vkd3d_instance_create_info *create_info)
{
    const struct vkd3d_vk_global_procs *vk_global_procs = &instance->vk_global_procs;
    const char *debug_layer_name = "VK_LAYER_KHRONOS_validation";
    bool *user_extension_supported = NULL;
    VkApplicationInfo application_info;
    VkInstanceCreateInfo instance_info;
    char application_name[VKD3D_PATH_MAX];
    VkLayerProperties *layers;
    uint32_t extension_count;
    const char **extensions;
    uint32_t layer_count, i;
    VkInstance vk_instance;
    bool dirty_tree_build;
    VkResult vr;
    HRESULT hr;
    uint32_t loader_version = VK_API_VERSION_1_0;

    TRACE("Build: %s.\n", vkd3d_version);

    memset(instance, 0, sizeof(*instance));

    vkd3d_config_flags_init();

    if (FAILED(hr = vkd3d_init_vk_global_procs(instance, create_info->pfn_vkGetInstanceProcAddr)))
    {
        ERR("Failed to initialize Vulkan global procs, hr %#x.\n", hr);
        return hr;
    }

    if (create_info->optional_instance_extension_count)
    {
        if (!(user_extension_supported = vkd3d_calloc(create_info->optional_instance_extension_count, sizeof(bool))))
            return E_OUTOFMEMORY;
    }

    if (FAILED(hr = vkd3d_init_instance_caps(instance, create_info,
            &extension_count, user_extension_supported)))
    {
        vkd3d_free(user_extension_supported);
        return hr;
    }

    if (vk_global_procs->vkEnumerateInstanceVersion)
        vk_global_procs->vkEnumerateInstanceVersion(&loader_version);

    if (loader_version < VKD3D_MIN_API_VERSION)
    {
        ERR("Vulkan %u.%u not supported by loader.\n",
                VK_VERSION_MAJOR(VKD3D_MIN_API_VERSION),
                VK_VERSION_MINOR(VKD3D_MIN_API_VERSION));
        vkd3d_free(user_extension_supported);
        return E_INVALIDARG;
    }

    /* Do not opt-in to versions we don't need yet. */
    if (loader_version > VKD3D_MAX_API_VERSION)
        loader_version = VKD3D_MAX_API_VERSION;

    application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.pNext = NULL;
    application_info.pApplicationName = NULL;
    application_info.applicationVersion = 0;
    application_info.pEngineName = "vkd3d";
    application_info.engineVersion = vkd3d_get_vk_version();
    application_info.apiVersion = loader_version;

    /* Builds from dirty trees generate vkd3d_version ending with a '+'. */
    dirty_tree_build = sizeof(vkd3d_version) >= 2 && vkd3d_version[sizeof(vkd3d_version) - 2] == '+';
    if (dirty_tree_build)
        INFO("vkd3d-proton - build: %015"PRIx64"+.\n", vkd3d_build >> 4);
    else
        INFO("vkd3d-proton - build: %015"PRIx64".\n", vkd3d_build);

    if (vkd3d_get_program_name(application_name))
        application_info.pApplicationName = application_name;

    TRACE("Application: %s.\n", debugstr_a(application_info.pApplicationName));

    if (!(extensions = vkd3d_calloc(extension_count, sizeof(*extensions))))
    {
        vkd3d_free(user_extension_supported);
        return E_OUTOFMEMORY;
    }

    vkd3d_mark_enabled_user_extensions(&instance->vk_info,
            create_info->optional_instance_extensions,
            create_info->optional_instance_extension_count,
            user_extension_supported);

    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pNext = NULL;
    instance_info.flags = 0;
    instance_info.pApplicationInfo = &application_info;
    instance_info.enabledLayerCount = 0;
    instance_info.ppEnabledLayerNames = NULL;
    instance_info.enabledExtensionCount = vkd3d_enable_extensions(extensions, NULL, 0,
            optional_instance_extensions, ARRAY_SIZE(optional_instance_extensions),
            create_info->instance_extensions,
            create_info->instance_extension_count,
            create_info->optional_instance_extensions,
            create_info->optional_instance_extension_count,
            user_extension_supported, &instance->vk_info);
    instance_info.ppEnabledExtensionNames = extensions;
    vkd3d_free(user_extension_supported);

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_VULKAN_DEBUG)
    {
        layers = NULL;

        if (vk_global_procs->vkEnumerateInstanceLayerProperties(&layer_count, NULL) == VK_SUCCESS &&
            layer_count &&
            (layers = vkd3d_malloc(layer_count * sizeof(*layers))) &&
            vk_global_procs->vkEnumerateInstanceLayerProperties(&layer_count, layers) == VK_SUCCESS)
        {
            for (i = 0; i < layer_count; i++)
            {
                if (strcmp(layers[i].layerName, debug_layer_name) == 0)
                {
                    instance_info.enabledLayerCount = 1;
                    instance_info.ppEnabledLayerNames = &debug_layer_name;
                    break;
                }
            }
        }

        if (instance_info.enabledLayerCount == 0)
        {
            ERR("Failed to enumerate instance layers, will not use VK_LAYER_KHRONOS_validation directly.\n"
                "Use VKD3D_CONFIG=vk_debug VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation instead!\n");
        }
        vkd3d_free(layers);
    }

    vr = vk_global_procs->vkCreateInstance(&instance_info, NULL, &vk_instance);
    if (vr < 0)
    {
        ERR("Failed to create Vulkan instance, vr %d.\n", vr);
        vkd3d_free((void *)extensions);
        return hresult_from_vk_result(vr);
    }

    if (FAILED(hr = vkd3d_load_vk_instance_procs(&instance->vk_procs, vk_global_procs, vk_instance)))
    {
        ERR("Failed to load instance procs, hr %#x.\n", hr);
        vkd3d_free((void *)extensions);
        if (instance->vk_procs.vkDestroyInstance)
            instance->vk_procs.vkDestroyInstance(vk_instance, NULL);
        return hr;
    }

    instance->vk_instance = vk_instance;
    instance->instance_version = loader_version;

    instance->vk_info.extension_count = instance_info.enabledExtensionCount;
    instance->vk_info.extension_names = extensions;

    TRACE("Created Vulkan instance %p, version %u.%u.\n", vk_instance,
            VK_VERSION_MAJOR(loader_version),
            VK_VERSION_MINOR(loader_version));

    instance->refcount = 1;

    instance->vk_debug_callback = VK_NULL_HANDLE;
    if (instance->vk_info.EXT_debug_utils && (vkd3d_config_flags & VKD3D_CONFIG_FLAG_VULKAN_DEBUG))
        vkd3d_init_debug_messenger_callback(instance);

#ifdef VKD3D_ENABLE_RENDERDOC
    /* Need to init this sometime after creating the instance so that the layer has loaded. */
    vkd3d_renderdoc_init();
#endif

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    vkd3d_descriptor_debug_init();
#endif

    return S_OK;
}

static struct vkd3d_instance *instance_singleton;
static pthread_mutex_t instance_singleton_lock = PTHREAD_MUTEX_INITIALIZER;

HRESULT vkd3d_create_instance(const struct vkd3d_instance_create_info *create_info,
        struct vkd3d_instance **instance)
{
    struct vkd3d_instance *object;
    HRESULT hr = S_OK;

    /* As long as there are live ID3D12Devices, we should only have one VkInstance that all devices can share. */
    pthread_mutex_lock(&instance_singleton_lock);
    if (instance_singleton)
    {
        vkd3d_instance_incref(*instance = instance_singleton);
        TRACE("Handling out global instance singleton.\n");
        goto out_unlock;
    }

    TRACE("create_info %p, instance %p.\n", create_info, instance);

    vkd3d_init_profiling();

    if (!create_info || !instance)
    {
        hr = E_INVALIDARG;
        goto out_unlock;
    }

    if (!(object = vkd3d_malloc(sizeof(*object))))
    {
        hr = E_OUTOFMEMORY;
        goto out_unlock;
    }

    if (FAILED(hr = vkd3d_instance_init(object, create_info)))
    {
        vkd3d_free(object);
        goto out_unlock;
    }

    TRACE("Created instance %p.\n", object);

    *instance = object;
    instance_singleton = object;

out_unlock:
    pthread_mutex_unlock(&instance_singleton_lock);
    return hr;
}

static void vkd3d_destroy_instance(struct vkd3d_instance *instance)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &instance->vk_procs;
    VkInstance vk_instance = instance->vk_instance;

    if (instance->vk_debug_callback)
        VK_CALL(vkDestroyDebugUtilsMessengerEXT(vk_instance, instance->vk_debug_callback, NULL));

    vkd3d_free((void *)instance->vk_info.extension_names);
    VK_CALL(vkDestroyInstance(vk_instance, NULL));

    vkd3d_free(instance);
}

ULONG vkd3d_instance_incref(struct vkd3d_instance *instance)
{
    ULONG refcount = InterlockedIncrement(&instance->refcount);

    TRACE("%p increasing refcount to %u.\n", instance, refcount);

    return refcount;
}

ULONG vkd3d_instance_decref(struct vkd3d_instance *instance)
{
    ULONG refcount;

    /* The device singleton is more advanced, since it uses a CAS loop.
     * Device references are lowered constantly, but instance references only release
     * when the ID3D12Device itself dies, which should be fairly rare, so we should use simpler code. */
    pthread_mutex_lock(&instance_singleton_lock);
    refcount = InterlockedDecrement(&instance->refcount);

    TRACE("%p decreasing refcount to %u.\n", instance, refcount);

    if (!refcount)
    {
        assert(instance_singleton == instance);
        vkd3d_destroy_instance(instance);
        instance_singleton = NULL;
    }

    pthread_mutex_unlock(&instance_singleton_lock);
    return refcount;
}

VkInstance vkd3d_instance_get_vk_instance(struct vkd3d_instance *instance)
{
    return instance->vk_instance;
}

static uint32_t vkd3d_physical_device_get_time_domains(struct d3d12_device *device)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance->vk_procs;
    VkPhysicalDevice physical_device = device->vk_physical_device;
    uint32_t i, domain_count = 0;
    VkTimeDomainEXT *domains;
    uint32_t result = 0;
    VkResult vr;

    if ((vr = VK_CALL(vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(physical_device, &domain_count, NULL))) < 0)
    {
        ERR("Failed to enumerate time domains, vr %d.\n", vr);
        return 0;
    }

    if (!(domains = vkd3d_calloc(domain_count, sizeof(*domains))))
        return 0;

    if ((vr = VK_CALL(vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(physical_device, &domain_count, domains))) < 0)
    {
        ERR("Failed to enumerate time domains, vr %d.\n", vr);
        vkd3d_free(domains);
        return 0;
    }

    for (i = 0; i < domain_count; i++)
    {
        switch (domains[i])
        {
            case VK_TIME_DOMAIN_DEVICE_KHR:
                result |= VKD3D_TIME_DOMAIN_DEVICE;
                break;
            case VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR:
                result |= VKD3D_TIME_DOMAIN_QPC;
                break;
            default:
                break;
        }
    }

    vkd3d_free(domains);
    return result;
}

bool d3d12_device_supports_ray_tracing_tier_1_0(const struct d3d12_device *device)
{
    return device->device_info.acceleration_structure_features.accelerationStructure &&
            device->device_info.ray_tracing_pipeline_features.rayTracingPipeline &&
            device->d3d12_caps.options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
}

bool d3d12_device_supports_ray_tracing_tier_1_2(const struct d3d12_device *device)
{
    return device->device_info.opacity_micromap_features.micromap &&
            device->d3d12_caps.options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_2;
}

bool d3d12_device_supports_variable_shading_rate_tier_1(struct d3d12_device *device)
{
    const struct vkd3d_physical_device_info *info = &device->device_info;

    return info->fragment_shading_rate_features.pipelineFragmentShadingRate &&
            (device->vk_info.device_limits.framebufferColorSampleCounts & VK_SAMPLE_COUNT_2_BIT);
}

UINT d3d12_determine_shading_rate_image_tile_size(struct d3d12_device *device)
{
    VkExtent2D min_texel_size = device->device_info.fragment_shading_rate_properties.minFragmentShadingRateAttachmentTexelSize;
    VkExtent2D max_texel_size = device->device_info.fragment_shading_rate_properties.maxFragmentShadingRateAttachmentTexelSize;
    const UINT valid_shading_rate_image_tile_sizes[] =
    {
        8, 16, 32
    };

    for (uint32_t i = 0; i < ARRAY_SIZE(valid_shading_rate_image_tile_sizes); i++)
    {
        UINT tile_size = valid_shading_rate_image_tile_sizes[i];
        if (tile_size >= min_texel_size.width && tile_size >= min_texel_size.height &&
                tile_size <= max_texel_size.width && tile_size <= max_texel_size.height)
            return tile_size;
    }

    /* No valid D3D12 tile size. */
    return 0;
}

bool d3d12_device_supports_variable_shading_rate_tier_2(struct d3d12_device *device)
{
    const struct vkd3d_physical_device_info *info = &device->device_info;

    return info->fragment_shading_rate_properties.fragmentShadingRateNonTrivialCombinerOps &&
            info->fragment_shading_rate_features.attachmentFragmentShadingRate &&
            info->fragment_shading_rate_features.primitiveFragmentShadingRate &&
            d3d12_determine_shading_rate_image_tile_size(device) != 0;
}

static D3D12_VARIABLE_SHADING_RATE_TIER d3d12_device_determine_variable_shading_rate_tier(struct d3d12_device *device)
{
    if (!d3d12_device_supports_variable_shading_rate_tier_1(device))
        return D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;

    if (!d3d12_device_supports_variable_shading_rate_tier_2(device))
        return D3D12_VARIABLE_SHADING_RATE_TIER_1;

    return D3D12_VARIABLE_SHADING_RATE_TIER_2;
}

static const struct
{
    VkExtent2D         fragment_size;
    VkSampleCountFlags min_sample_counts;
} additional_shading_rates[] =
{
    { { 2, 4 }, VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT },
    { { 4, 2 }, VK_SAMPLE_COUNT_1_BIT },
    { { 4, 4 }, VK_SAMPLE_COUNT_1_BIT },
};

static bool d3d12_device_determine_additional_shading_rates_supported(struct d3d12_device *device)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance->vk_procs;
    VkPhysicalDeviceFragmentShadingRateKHR *fragment_shading_rates;
    VkPhysicalDevice physical_device = device->vk_physical_device;
    uint32_t additional_shading_rates_supported = 0;
    uint32_t fragment_shading_rate_count;
    uint32_t i, j;
    VkResult vr;

    /* Early out if we don't support at least variable shading rate TIER1 */
    if (d3d12_device_determine_variable_shading_rate_tier(device) == D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED)
        return false;

    if ((vr = VK_CALL(vkGetPhysicalDeviceFragmentShadingRatesKHR(physical_device, &fragment_shading_rate_count, NULL))) < 0)
    {
        ERR("Failed to enumerate additional shading rates, vr %d.\n", vr);
        return false;
    }

    if (!(fragment_shading_rates = vkd3d_calloc(fragment_shading_rate_count, sizeof(*fragment_shading_rates))))
        return false;

    for (i = 0; i < fragment_shading_rate_count; i++)
        fragment_shading_rates[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR;

    if ((vr = VK_CALL(vkGetPhysicalDeviceFragmentShadingRatesKHR(physical_device, &fragment_shading_rate_count, fragment_shading_rates))) < 0)
    {
        ERR("Failed to enumerate additional shading rates, vr %d.\n", vr);
        vkd3d_free(fragment_shading_rates);
        return false;
    }

    for (i = 0; i < fragment_shading_rate_count; i++)
    {
        for (j = 0; j < ARRAY_SIZE(additional_shading_rates); j++)
        {
            if (fragment_shading_rates[i].fragmentSize.width  == additional_shading_rates[j].fragment_size.width &&
                fragment_shading_rates[i].fragmentSize.height == additional_shading_rates[j].fragment_size.height &&
                (fragment_shading_rates[i].sampleCounts & additional_shading_rates[j].min_sample_counts) == additional_shading_rates[j].min_sample_counts)
            {
                additional_shading_rates_supported++;
                break;
            }
        }
    }
    vkd3d_free(fragment_shading_rates);

    return additional_shading_rates_supported == ARRAY_SIZE(additional_shading_rates);
}

bool d3d12_device_supports_required_subgroup_size_for_stage(
        struct d3d12_device *device, VkShaderStageFlagBits stage)
{
    if (device->device_info.vulkan_1_3_properties.minSubgroupSize ==
            device->device_info.vulkan_1_3_properties.maxSubgroupSize)
        return true;

    return (device->device_info.vulkan_1_3_properties.requiredSubgroupSizeStages & stage) != 0;
}

static bool d3d12_device_is_steam_deck(const struct d3d12_device *device)
{
    return device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_MESA_RADV &&
            device->device_info.properties2.properties.vendorID == 0x1002 &&
            (device->device_info.properties2.properties.deviceID == 0x163f ||
             device->device_info.properties2.properties.deviceID == 0x1435);
}

static void vkd3d_physical_device_info_apply_workarounds(struct vkd3d_physical_device_info *info,
        struct d3d12_device *device)
{
    /* A performance workaround for NV.
     * The 16 byte offset is a lie, as that is only actually required when we
     * use vectorized load-stores. When we emit vectorized load-store ops,
     * the storage buffer must be aligned properly, so this is fine in practice
     * and is a nice speed boost. */
    if (info->vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY)
        info->properties2.properties.limits.minStorageBufferOffsetAlignment = 4;

    /* UE5 is broken and assumes that if mesh shaders are supported, barycentrics are also supported.
     * This happens to be the case on RDNA2+ and Turing+ on Windows, but Mesa landed barycentrics long
     * after mesh shaders, so Mesa 23.1 will often fail on boot for practically all UE5 content.
     * The reasonable workaround is to disable mesh shaders unless barys are also supported.
     * Nanite can work without mesh shaders.
     * Unfortunately, we don't know of a robust way to detect UE5, so have to apply this globally.
     * Similarly, Intel Arc does not expose barycentrics, but does expose mesh shaders ...
     * Unclear if that will ever be resolved. */
    if (vkd3d_application_feature_override != VKD3D_APPLICATION_FEATURE_MESH_SHADER_WITHOUT_BARYCENTRICS &&
            !device->vk_info.KHR_fragment_shader_barycentric && device->vk_info.EXT_mesh_shader)
    {
        WARN("Mesh shaders are supported, but not barycentrics. Disabling mesh shaders as a global UE5 workaround.\n");
        device->vk_info.EXT_mesh_shader = false;
        device->device_info.mesh_shader_features.meshShader = VK_FALSE;
        device->device_info.mesh_shader_features.taskShader = VK_FALSE;
        device->device_info.mesh_shader_features.primitiveFragmentShadingRateMeshShader = VK_FALSE;
        device->device_info.mesh_shader_features.meshShaderQueries = VK_FALSE;
        device->device_info.mesh_shader_features.multiviewMeshShader = VK_FALSE;
    }

    if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_SKIP_DRIVER_WORKAROUNDS))
    {
        if (info->vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY &&
                device->vk_info.NV_device_generated_commands_compute &&
                (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DISABLE_DGCC))
        {
            device->vk_info.NV_device_generated_commands_compute = false;
            device->vk_info.EXT_device_generated_commands = false;
            device->device_info.device_generated_commands_compute_features_nv.deviceGeneratedCompute = VK_FALSE;
            device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands = VK_FALSE;
            WARN("Disabling DGCC due to config flag.\n");
        }

        /* Two known bugs in the wild:
         * - presentID = 0 handling when toggling present mode is broken.
         * - swapchain fence is not enough to avoid DEVICE_LOST when resizing swapchain.
         */
        if (info->vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY &&
                info->swapchain_maintenance1_features.swapchainMaintenance1 &&
                info->properties2.properties.driverVersion <= VKD3D_DRIVER_VERSION_MAKE_NV(550, 40, 7))
        {
            WARN("Disabling VK_EXT_swapchain_maintenance1 on NV due to driver bugs.\n");
            device->device_info.swapchain_maintenance1_features.swapchainMaintenance1 = VK_FALSE;
            device->vk_info.EXT_swapchain_maintenance1 = false;
        }
    }
}

static void vkd3d_physical_device_info_init(struct vkd3d_physical_device_info *info, struct d3d12_device *device)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance->vk_procs;
    VkPhysicalDeviceLayeredApiVulkanPropertiesKHR vk_layered_props;
    VkPhysicalDeviceLayeredApiPropertiesListKHR layered_props_list;
    struct vkd3d_vulkan_info *vulkan_info = &device->vk_info;
    VkPhysicalDeviceLayeredApiPropertiesKHR layered_props;
    VkPhysicalDeviceDriverPropertiesKHR real_driver_props;

    memset(info, 0, sizeof(*info));

    info->features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    info->properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

    info->vulkan_1_1_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    vk_prepend_struct(&info->features2, &info->vulkan_1_1_features);
    info->vulkan_1_1_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
    vk_prepend_struct(&info->properties2, &info->vulkan_1_1_properties);

    info->vulkan_1_2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vk_prepend_struct(&info->features2, &info->vulkan_1_2_features);
    info->vulkan_1_2_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
    vk_prepend_struct(&info->properties2, &info->vulkan_1_2_properties);

    info->vulkan_1_3_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk_prepend_struct(&info->features2, &info->vulkan_1_3_features);
    info->vulkan_1_3_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES;
    vk_prepend_struct(&info->properties2, &info->vulkan_1_3_properties);

    if (vulkan_info->KHR_push_descriptor)
    {
        info->push_descriptor_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR;
        vk_prepend_struct(&info->properties2, &info->push_descriptor_properties);
    }

    if (vulkan_info->KHR_calibrated_timestamps)
        info->time_domains = vkd3d_physical_device_get_time_domains(device);

    if (vulkan_info->EXT_conditional_rendering)
    {
        info->conditional_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->conditional_rendering_features);
    }

    if (vulkan_info->EXT_conservative_rasterization)
    {
        info->conservative_rasterization_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT;
        vk_prepend_struct(&info->properties2, &info->conservative_rasterization_properties);
    }

    if (vulkan_info->EXT_custom_border_color)
    {
        info->custom_border_color_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->custom_border_color_features);
        info->custom_border_color_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT;
        vk_prepend_struct(&info->properties2, &info->custom_border_color_properties);
    }

    if (vulkan_info->EXT_depth_clip_enable)
    {
        info->depth_clip_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->depth_clip_features);
    }

    if (vulkan_info->EXT_device_generated_commands)
    {
        info->device_generated_commands_features_ext.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_EXT;
        info->device_generated_commands_properties_ext.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_EXT;
        vk_prepend_struct(&info->features2, &info->device_generated_commands_features_ext);
        vk_prepend_struct(&info->properties2, &info->device_generated_commands_properties_ext);
    }

    if (vulkan_info->EXT_robustness2)
    {
        info->robustness2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->robustness2_features);
        info->robustness2_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT;
        vk_prepend_struct(&info->properties2, &info->robustness2_properties);
    }

    if (vulkan_info->EXT_transform_feedback)
    {
        info->xfb_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->xfb_features);
        info->xfb_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT;
        vk_prepend_struct(&info->properties2, &info->xfb_properties);
    }

    if (vulkan_info->EXT_vertex_attribute_divisor)
    {
        info->vertex_divisor_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->vertex_divisor_features);
        info->vertex_divisor_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT;
        vk_prepend_struct(&info->properties2, &info->vertex_divisor_properties);
    }

    if (vulkan_info->EXT_extended_dynamic_state2)
    {
        info->extended_dynamic_state2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->extended_dynamic_state2_features);
    }

    if (vulkan_info->EXT_extended_dynamic_state3)
    {
        info->extended_dynamic_state3_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->extended_dynamic_state3_features);
    }

    if (vulkan_info->EXT_external_memory_host)
    {
        info->external_memory_host_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT;
        vk_prepend_struct(&info->properties2, &info->external_memory_host_properties);
    }

    if (vulkan_info->AMD_shader_core_properties)
    {
        info->shader_core_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_AMD;
        vk_prepend_struct(&info->properties2, &info->shader_core_properties);
    }

    if (vulkan_info->AMD_shader_core_properties2)
    {
        info->shader_core_properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_2_AMD;
        vk_prepend_struct(&info->properties2, &info->shader_core_properties2);
    }

    if (vulkan_info->NV_shader_sm_builtins)
    {
        info->shader_sm_builtins_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SM_BUILTINS_PROPERTIES_NV;
        vk_prepend_struct(&info->properties2, &info->shader_sm_builtins_properties);
    }

    if (vulkan_info->VALVE_mutable_descriptor_type || vulkan_info->EXT_mutable_descriptor_type)
    {
        info->mutable_descriptor_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->mutable_descriptor_features);
    }

    if (vulkan_info->EXT_image_view_min_lod)
    {
        info->image_view_min_lod_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->image_view_min_lod_features);
    }

    if (vulkan_info->KHR_acceleration_structure && vulkan_info->KHR_ray_tracing_pipeline &&
        vulkan_info->KHR_deferred_host_operations)
    {
        info->acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        info->acceleration_structure_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        info->ray_tracing_pipeline_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        info->ray_tracing_pipeline_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        vk_prepend_struct(&info->features2, &info->acceleration_structure_features);
        vk_prepend_struct(&info->features2, &info->ray_tracing_pipeline_features);
        vk_prepend_struct(&info->properties2, &info->acceleration_structure_properties);
        vk_prepend_struct(&info->properties2, &info->ray_tracing_pipeline_properties);
    }

    if (vulkan_info->KHR_ray_query)
    {
        info->ray_query_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->ray_query_features);
    }

    if (vulkan_info->KHR_ray_tracing_maintenance1)
    {
        info->ray_tracing_maintenance1_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->ray_tracing_maintenance1_features);
    }

    if (vulkan_info->KHR_fragment_shading_rate)
    {
        info->fragment_shading_rate_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR;
        info->fragment_shading_rate_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR;
        vk_prepend_struct(&info->properties2, &info->fragment_shading_rate_properties);
        vk_prepend_struct(&info->features2, &info->fragment_shading_rate_features);
    }

    if (vulkan_info->NV_fragment_shader_barycentric && !vulkan_info->KHR_fragment_shader_barycentric)
    {
        info->barycentric_features_nv.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_NV;
        vk_prepend_struct(&info->features2, &info->barycentric_features_nv);
    }

    if (vulkan_info->KHR_fragment_shader_barycentric)
    {
        info->barycentric_features_khr.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->barycentric_features_khr);
    }

    if (vulkan_info->NV_device_generated_commands)
    {
        info->device_generated_commands_features_nv.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_NV;
        info->device_generated_commands_properties_nv.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_NV;
        vk_prepend_struct(&info->features2, &info->device_generated_commands_features_nv);
        vk_prepend_struct(&info->properties2, &info->device_generated_commands_properties_nv);
    }

    if (vulkan_info->NV_device_generated_commands_compute)
    {
        info->device_generated_commands_compute_features_nv.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_COMPUTE_FEATURES_NV;
        vk_prepend_struct(&info->features2, &info->device_generated_commands_compute_features_nv);
    }

    if (vulkan_info->EXT_shader_image_atomic_int64)
    {
        info->shader_image_atomic_int64_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->shader_image_atomic_int64_features);
    }

    if (vulkan_info->AMD_device_coherent_memory)
    {
        info->device_coherent_memory_features_amd.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD;
        vk_prepend_struct(&info->features2, &info->device_coherent_memory_features_amd);
    }

    if (vulkan_info->EXT_mesh_shader)
    {
        info->mesh_shader_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        info->mesh_shader_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;
        vk_prepend_struct(&info->features2, &info->mesh_shader_features);
        vk_prepend_struct(&info->properties2, &info->mesh_shader_properties);
    }

    if (vulkan_info->EXT_shader_module_identifier)
    {
        info->shader_module_identifier_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_FEATURES_EXT;
        info->shader_module_identifier_properties.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_PROPERTIES_EXT;
        vk_prepend_struct(&info->features2, &info->shader_module_identifier_features);
        vk_prepend_struct(&info->properties2, &info->shader_module_identifier_properties);
    }

    if (vulkan_info->KHR_present_id)
    {
        info->present_id_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->present_id_features);
    }

    if (vulkan_info->KHR_present_wait)
    {
        info->present_wait_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->present_wait_features);
    }

    if (vulkan_info->KHR_maintenance5)
    {
        info->maintenance_5_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR;
        info->maintenance_5_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_PROPERTIES_KHR;
        vk_prepend_struct(&info->features2, &info->maintenance_5_features);
        vk_prepend_struct(&info->properties2, &info->maintenance_5_properties);
    }

    if (vulkan_info->KHR_maintenance6)
    {
        info->maintenance_6_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR;
        info->maintenance_6_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_PROPERTIES_KHR;
        vk_prepend_struct(&info->features2, &info->maintenance_6_features);
        vk_prepend_struct(&info->properties2, &info->maintenance_6_properties);
    }

    memset(&layered_props, 0, sizeof(layered_props));
    memset(&layered_props_list, 0, sizeof(layered_props_list));
    memset(&vk_layered_props, 0, sizeof(vk_layered_props));
    memset(&real_driver_props, 0, sizeof(real_driver_props));

    if (vulkan_info->KHR_maintenance7)
    {
        info->maintenance_7_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_FEATURES_KHR;
        info->maintenance_7_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_7_PROPERTIES_KHR;
        vk_prepend_struct(&info->features2, &info->maintenance_7_features);
        vk_prepend_struct(&info->properties2, &info->maintenance_7_properties);

        layered_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_API_PROPERTIES_KHR;

        /* assume a potentially single-layered implementation: at some point in the future it might be interesting to go deeper */
        layered_props_list.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_API_PROPERTIES_LIST_KHR;
        layered_props_list.layeredApiCount = 1;
        layered_props_list.pLayeredApis = &layered_props;
        vk_prepend_struct(&info->properties2, &layered_props_list);

        vk_layered_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LAYERED_API_VULKAN_PROPERTIES_KHR;
        vk_prepend_struct(&layered_props, &vk_layered_props);

        real_driver_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR;
        vk_prepend_struct(&vk_layered_props.properties, &real_driver_props);
    }

    if (vulkan_info->KHR_maintenance8)
    {
        info->maintenance_8_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_8_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->maintenance_8_features);
    }

    if (vulkan_info->KHR_shader_maximal_reconvergence)
    {
        info->shader_maximal_reconvergence_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MAXIMAL_RECONVERGENCE_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->shader_maximal_reconvergence_features);
    }

    if (vulkan_info->KHR_shader_quad_control)
    {
        info->shader_quad_control_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_QUAD_CONTROL_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->shader_quad_control_features);
    }

    if (vulkan_info->KHR_compute_shader_derivatives || vulkan_info->NV_compute_shader_derivatives)
    {
        info->compute_shader_derivatives_features_khr.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->compute_shader_derivatives_features_khr);
    }

    if (vulkan_info->KHR_compute_shader_derivatives)
    {
        info->compute_shader_derivatives_properties_khr.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_PROPERTIES_KHR;
        vk_prepend_struct(&info->properties2, &info->compute_shader_derivatives_properties_khr);
    }

    if (vulkan_info->EXT_descriptor_buffer)
    {
        info->descriptor_buffer_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
        info->descriptor_buffer_properties.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;
        vk_prepend_struct(&info->features2, &info->descriptor_buffer_features);
        vk_prepend_struct(&info->properties2, &info->descriptor_buffer_properties);
    }

    if (vulkan_info->EXT_pipeline_library_group_handles)
    {
        info->pipeline_library_group_handles_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_LIBRARY_GROUP_HANDLES_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->pipeline_library_group_handles_features);
    }

    if (vulkan_info->EXT_image_sliced_view_of_3d)
    {
        info->image_sliced_view_of_3d_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_SLICED_VIEW_OF_3D_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->image_sliced_view_of_3d_features);
    }

    if (vulkan_info->EXT_graphics_pipeline_library)
    {
        info->graphics_pipeline_library_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT;
        info->graphics_pipeline_library_properties.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_PROPERTIES_EXT;
        vk_prepend_struct(&info->features2, &info->graphics_pipeline_library_features);
        vk_prepend_struct(&info->properties2, &info->graphics_pipeline_library_properties);
    }

    if (vulkan_info->EXT_fragment_shader_interlock)
    {
        info->fragment_shader_interlock_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->fragment_shader_interlock_features);
    }

    if (vulkan_info->EXT_memory_priority)
    {
        info->memory_priority_features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->memory_priority_features);
    }

    if (vulkan_info->EXT_pageable_device_local_memory)
    {
        info->pageable_device_memory_features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->pageable_device_memory_features);
    }

    /* This EXT only exists to remove VUID for validation, it does not add new functionality. */
    if (vulkan_info->EXT_dynamic_rendering_unused_attachments)
    {
        info->dynamic_rendering_unused_attachments_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->dynamic_rendering_unused_attachments_features);
    }

    if (vulkan_info->EXT_line_rasterization)
    {
        info->line_rasterization_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT;
        info->line_rasterization_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES_EXT;
        vk_prepend_struct(&info->features2, &info->line_rasterization_features);
        vk_prepend_struct(&info->properties2, &info->line_rasterization_properties);
    }

    if (vulkan_info->EXT_image_compression_control)
    {
        info->image_compression_control_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->image_compression_control_features);
    }

    if (vulkan_info->NV_memory_decompression)
    {
        info->memory_decompression_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_DECOMPRESSION_FEATURES_NV;
        info->memory_decompression_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_DECOMPRESSION_PROPERTIES_NV;
        vk_prepend_struct(&info->features2, &info->memory_decompression_features);
        vk_prepend_struct(&info->properties2, &info->memory_decompression_properties);
    }

    if (vulkan_info->EXT_device_fault)
    {
        info->fault_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->fault_features);
    }

    if (vulkan_info->EXT_swapchain_maintenance1)
    {
        info->swapchain_maintenance1_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->swapchain_maintenance1_features);
    }

    if (vulkan_info->NV_raw_access_chains)
    {
        info->raw_access_chains_nv.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAW_ACCESS_CHAINS_FEATURES_NV;
        vk_prepend_struct(&info->features2, &info->raw_access_chains_nv);
    }

    if (vulkan_info->EXT_device_address_binding_report)
    {
        info->address_binding_report_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->address_binding_report_features);
    }

    if (vulkan_info->EXT_depth_bias_control)
    {
        info->depth_bias_control_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_BIAS_CONTROL_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->depth_bias_control_features);
    }

    if (vulkan_info->MESA_image_alignment_control)
    {
        info->image_alignment_control_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ALIGNMENT_CONTROL_FEATURES_MESA;
        info->image_alignment_control_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ALIGNMENT_CONTROL_PROPERTIES_MESA;
        vk_prepend_struct(&info->features2, &info->image_alignment_control_features);
        vk_prepend_struct(&info->properties2, &info->image_alignment_control_properties);
    }

    if (vulkan_info->NV_optical_flow)
    {
        info->optical_flow_nv_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPTICAL_FLOW_FEATURES_NV;
        vk_prepend_struct(&info->features2, &info->optical_flow_nv_features);
    }

    if (vulkan_info->KHR_cooperative_matrix)
    {
        info->cooperative_matrix_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR;
        info->cooperative_matrix_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_PROPERTIES_KHR;
        vk_prepend_struct(&info->features2, &info->cooperative_matrix_features);
        vk_prepend_struct(&info->properties2, &info->cooperative_matrix_properties);
    }

    if (vulkan_info->EXT_zero_initialize_device_memory)
    {
        info->zero_initialize_device_memory_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_DEVICE_MEMORY_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->zero_initialize_device_memory_features);
    }

    if (vulkan_info->EXT_opacity_micromap)
    {
        info->opacity_micromap_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->opacity_micromap_features);
    }

    if (vulkan_info->EXT_shader_float8)
    {
        info->shader_float8_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->shader_float8_features);
    }

    if (vulkan_info->NV_cooperative_matrix2)
    {
        info->cooperative_matrix2_features_nv.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_2_FEATURES_NV;
        vk_prepend_struct(&info->features2, &info->cooperative_matrix2_features_nv);
    }

    VK_CALL(vkGetPhysicalDeviceFeatures2(device->vk_physical_device, &info->features2));
    VK_CALL(vkGetPhysicalDeviceProperties2(device->vk_physical_device, &info->properties2));

    /* if nonzero, this is a layered implementation */
    if (real_driver_props.driverID)
    {
        /* store the layer ID here in case it's needed */
        info->layer_driver_id = info->vulkan_1_2_properties.driverID;
        /* swizzle the underlying driver ID here so everything else will use it */
        info->vulkan_1_2_properties.driverID = real_driver_props.driverID;
    }
}

static void vkd3d_trace_physical_device_properties(const VkPhysicalDeviceProperties *properties)
{
    TRACE("Device name: %s.\n", properties->deviceName);
    TRACE("Vendor ID: %#x, Device ID: %#x.\n", properties->vendorID, properties->deviceID);
    if (properties->vendorID == VKD3D_VENDOR_ID_NVIDIA)
    {
        TRACE("Driver version: %#x (%u.%u.%u).\n", properties->driverVersion,
                VKD3D_DRIVER_VERSION_MAJOR_NV(properties->driverVersion),
                VKD3D_DRIVER_VERSION_MINOR_NV(properties->driverVersion),
                VKD3D_DRIVER_VERSION_PATCH_NV(properties->driverVersion));
    }
    else
    {
        TRACE("Driver version: %#x (%u.%u.%u).\n", properties->driverVersion,
                VK_VERSION_MAJOR(properties->driverVersion),
                VK_VERSION_MINOR(properties->driverVersion),
                VK_VERSION_PATCH(properties->driverVersion));
    }

    TRACE("API version: %u.%u.%u.\n",
            VK_VERSION_MAJOR(properties->apiVersion),
            VK_VERSION_MINOR(properties->apiVersion),
            VK_VERSION_PATCH(properties->apiVersion));
}

static void vkd3d_trace_physical_device(VkPhysicalDevice device,
        const struct vkd3d_physical_device_info *info,
        const struct vkd3d_vk_instance_procs *vk_procs)
{
    VKD3D_UNUSED char debug_buffer[VKD3D_DEBUG_FLAGS_BUFFER_SIZE];
    VkPhysicalDeviceMemoryProperties memory_properties;
    VkQueueFamilyProperties *queue_properties;
    unsigned int i, j;
    uint32_t count;

    vkd3d_trace_physical_device_properties(&info->properties2.properties);

    VK_CALL(vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL));
    TRACE("Queue families [%u]:\n", count);

    if (!(queue_properties = vkd3d_calloc(count, sizeof(VkQueueFamilyProperties))))
        return;
    VK_CALL(vkGetPhysicalDeviceQueueFamilyProperties(device, &count, queue_properties));

    for (i = 0; i < count; ++i)
    {
        TRACE(" Queue family [%u]: flags %s, count %u, timestamp bits %u, image transfer granularity %s.\n",
                i, debug_vk_queue_flags(queue_properties[i].queueFlags, debug_buffer),
                queue_properties[i].queueCount, queue_properties[i].timestampValidBits,
                debug_vk_extent_3d(queue_properties[i].minImageTransferGranularity));
    }
    vkd3d_free(queue_properties);

    VK_CALL(vkGetPhysicalDeviceMemoryProperties(device, &memory_properties));
    for (i = 0; i < memory_properties.memoryHeapCount; ++i)
    {
        VKD3D_UNUSED const VkMemoryHeap *heap = &memory_properties.memoryHeaps[i];
        TRACE("Memory heap [%u]: size %#"PRIx64" (%"PRIu64" MiB), flags %s, memory types:\n",
                i, heap->size, heap->size / 1024 / 1024, debug_vk_memory_heap_flags(heap->flags, debug_buffer));
        for (j = 0; j < memory_properties.memoryTypeCount; ++j)
        {
            const VkMemoryType *type = &memory_properties.memoryTypes[j];
            if (type->heapIndex != i)
                continue;
            TRACE("  Memory type [%u]: flags %s.\n", j, debug_vk_memory_property_flags(type->propertyFlags, debug_buffer));
        }
    }
}

static void vkd3d_trace_physical_device_limits(const struct vkd3d_physical_device_info *info)
{
    TRACE("Device limits:\n");
    TRACE("  maxImageDimension1D: %u.\n", info->properties2.properties.limits.maxImageDimension1D);
    TRACE("  maxImageDimension2D: %u.\n", info->properties2.properties.limits.maxImageDimension2D);
    TRACE("  maxImageDimension3D: %u.\n", info->properties2.properties.limits.maxImageDimension3D);
    TRACE("  maxImageDimensionCube: %u.\n", info->properties2.properties.limits.maxImageDimensionCube);
    TRACE("  maxImageArrayLayers: %u.\n", info->properties2.properties.limits.maxImageArrayLayers);
    TRACE("  maxTexelBufferElements: %u.\n", info->properties2.properties.limits.maxTexelBufferElements);
    TRACE("  maxUniformBufferRange: %u.\n", info->properties2.properties.limits.maxUniformBufferRange);
    TRACE("  maxStorageBufferRange: %u.\n", info->properties2.properties.limits.maxStorageBufferRange);
    TRACE("  maxPushConstantsSize: %u.\n", info->properties2.properties.limits.maxPushConstantsSize);
    TRACE("  maxMemoryAllocationCount: %u.\n", info->properties2.properties.limits.maxMemoryAllocationCount);
    TRACE("  maxSamplerAllocationCount: %u.\n", info->properties2.properties.limits.maxSamplerAllocationCount);
    TRACE("  bufferImageGranularity: %#"PRIx64".\n", info->properties2.properties.limits.bufferImageGranularity);
    TRACE("  sparseAddressSpaceSize: %#"PRIx64".\n", info->properties2.properties.limits.sparseAddressSpaceSize);
    TRACE("  maxBoundDescriptorSets: %u.\n", info->properties2.properties.limits.maxBoundDescriptorSets);
    TRACE("  maxPerStageDescriptorSamplers: %u.\n", info->properties2.properties.limits.maxPerStageDescriptorSamplers);
    TRACE("  maxPerStageDescriptorUniformBuffers: %u.\n", info->properties2.properties.limits.maxPerStageDescriptorUniformBuffers);
    TRACE("  maxPerStageDescriptorStorageBuffers: %u.\n", info->properties2.properties.limits.maxPerStageDescriptorStorageBuffers);
    TRACE("  maxPerStageDescriptorSampledImages: %u.\n", info->properties2.properties.limits.maxPerStageDescriptorSampledImages);
    TRACE("  maxPerStageDescriptorStorageImages: %u.\n", info->properties2.properties.limits.maxPerStageDescriptorStorageImages);
    TRACE("  maxPerStageDescriptorInputAttachments: %u.\n", info->properties2.properties.limits.maxPerStageDescriptorInputAttachments);
    TRACE("  maxPerStageResources: %u.\n", info->properties2.properties.limits.maxPerStageResources);
    TRACE("  maxDescriptorSetSamplers: %u.\n", info->properties2.properties.limits.maxDescriptorSetSamplers);
    TRACE("  maxDescriptorSetUniformBuffers: %u.\n", info->properties2.properties.limits.maxDescriptorSetUniformBuffers);
    TRACE("  maxDescriptorSetUniformBuffersDynamic: %u.\n", info->properties2.properties.limits.maxDescriptorSetUniformBuffersDynamic);
    TRACE("  maxDescriptorSetStorageBuffers: %u.\n", info->properties2.properties.limits.maxDescriptorSetStorageBuffers);
    TRACE("  maxDescriptorSetStorageBuffersDynamic: %u.\n", info->properties2.properties.limits.maxDescriptorSetStorageBuffersDynamic);
    TRACE("  maxDescriptorSetSampledImages: %u.\n", info->properties2.properties.limits.maxDescriptorSetSampledImages);
    TRACE("  maxDescriptorSetStorageImages: %u.\n", info->properties2.properties.limits.maxDescriptorSetStorageImages);
    TRACE("  maxDescriptorSetInputAttachments: %u.\n", info->properties2.properties.limits.maxDescriptorSetInputAttachments);
    TRACE("  maxVertexInputAttributes: %u.\n", info->properties2.properties.limits.maxVertexInputAttributes);
    TRACE("  maxVertexInputBindings: %u.\n", info->properties2.properties.limits.maxVertexInputBindings);
    TRACE("  maxVertexInputAttributeOffset: %u.\n", info->properties2.properties.limits.maxVertexInputAttributeOffset);
    TRACE("  maxVertexInputBindingStride: %u.\n", info->properties2.properties.limits.maxVertexInputBindingStride);
    TRACE("  maxVertexOutputComponents: %u.\n", info->properties2.properties.limits.maxVertexOutputComponents);
    TRACE("  maxTessellationGenerationLevel: %u.\n", info->properties2.properties.limits.maxTessellationGenerationLevel);
    TRACE("  maxTessellationPatchSize: %u.\n", info->properties2.properties.limits.maxTessellationPatchSize);
    TRACE("  maxTessellationControlPerVertexInputComponents: %u.\n",
            info->properties2.properties.limits.maxTessellationControlPerVertexInputComponents);
    TRACE("  maxTessellationControlPerVertexOutputComponents: %u.\n",
            info->properties2.properties.limits.maxTessellationControlPerVertexOutputComponents);
    TRACE("  maxTessellationControlPerPatchOutputComponents: %u.\n",
            info->properties2.properties.limits.maxTessellationControlPerPatchOutputComponents);
    TRACE("  maxTessellationControlTotalOutputComponents: %u.\n",
            info->properties2.properties.limits.maxTessellationControlTotalOutputComponents);
    TRACE("  maxTessellationEvaluationInputComponents: %u.\n",
            info->properties2.properties.limits.maxTessellationEvaluationInputComponents);
    TRACE("  maxTessellationEvaluationOutputComponents: %u.\n",
            info->properties2.properties.limits.maxTessellationEvaluationOutputComponents);
    TRACE("  maxGeometryShaderInvocations: %u.\n", info->properties2.properties.limits.maxGeometryShaderInvocations);
    TRACE("  maxGeometryInputComponents: %u.\n", info->properties2.properties.limits.maxGeometryInputComponents);
    TRACE("  maxGeometryOutputComponents: %u.\n", info->properties2.properties.limits.maxGeometryOutputComponents);
    TRACE("  maxGeometryOutputVertices: %u.\n", info->properties2.properties.limits.maxGeometryOutputVertices);
    TRACE("  maxGeometryTotalOutputComponents: %u.\n", info->properties2.properties.limits.maxGeometryTotalOutputComponents);
    TRACE("  maxFragmentInputComponents: %u.\n", info->properties2.properties.limits.maxFragmentInputComponents);
    TRACE("  maxFragmentOutputAttachments: %u.\n", info->properties2.properties.limits.maxFragmentOutputAttachments);
    TRACE("  maxFragmentDualSrcAttachments: %u.\n", info->properties2.properties.limits.maxFragmentDualSrcAttachments);
    TRACE("  maxFragmentCombinedOutputResources: %u.\n", info->properties2.properties.limits.maxFragmentCombinedOutputResources);
    TRACE("  maxComputeSharedMemorySize: %u.\n", info->properties2.properties.limits.maxComputeSharedMemorySize);
    TRACE("  maxComputeWorkGroupCount: %u, %u, %u.\n", info->properties2.properties.limits.maxComputeWorkGroupCount[0],
            info->properties2.properties.limits.maxComputeWorkGroupCount[1], info->properties2.properties.limits.maxComputeWorkGroupCount[2]);
    TRACE("  maxComputeWorkGroupInvocations: %u.\n", info->properties2.properties.limits.maxComputeWorkGroupInvocations);
    TRACE("  maxComputeWorkGroupSize: %u, %u, %u.\n", info->properties2.properties.limits.maxComputeWorkGroupSize[0],
            info->properties2.properties.limits.maxComputeWorkGroupSize[1], info->properties2.properties.limits.maxComputeWorkGroupSize[2]);
    TRACE("  subPixelPrecisionBits: %u.\n", info->properties2.properties.limits.subPixelPrecisionBits);
    TRACE("  subTexelPrecisionBits: %u.\n", info->properties2.properties.limits.subTexelPrecisionBits);
    TRACE("  mipmapPrecisionBits: %u.\n", info->properties2.properties.limits.mipmapPrecisionBits);
    TRACE("  maxDrawIndexedIndexValue: %u.\n", info->properties2.properties.limits.maxDrawIndexedIndexValue);
    TRACE("  maxDrawIndirectCount: %u.\n", info->properties2.properties.limits.maxDrawIndirectCount);
    TRACE("  maxSamplerLodBias: %f.\n", info->properties2.properties.limits.maxSamplerLodBias);
    TRACE("  maxSamplerAnisotropy: %f.\n", info->properties2.properties.limits.maxSamplerAnisotropy);
    TRACE("  maxViewports: %u.\n", info->properties2.properties.limits.maxViewports);
    TRACE("  maxViewportDimensions: %u, %u.\n", info->properties2.properties.limits.maxViewportDimensions[0],
            info->properties2.properties.limits.maxViewportDimensions[1]);
    TRACE("  viewportBoundsRange: %f, %f.\n", info->properties2.properties.limits.viewportBoundsRange[0], info->properties2.properties.limits.viewportBoundsRange[1]);
    TRACE("  viewportSubPixelBits: %u.\n", info->properties2.properties.limits.viewportSubPixelBits);
    TRACE("  minMemoryMapAlignment: %u.\n", (unsigned int)info->properties2.properties.limits.minMemoryMapAlignment);
    TRACE("  minTexelBufferOffsetAlignment: %#"PRIx64".\n", info->properties2.properties.limits.minTexelBufferOffsetAlignment);
    TRACE("  minUniformBufferOffsetAlignment: %#"PRIx64".\n", info->properties2.properties.limits.minUniformBufferOffsetAlignment);
    TRACE("  minStorageBufferOffsetAlignment: %#"PRIx64".\n", info->properties2.properties.limits.minStorageBufferOffsetAlignment);
    TRACE("  minTexelOffset: %d.\n", info->properties2.properties.limits.minTexelOffset);
    TRACE("  maxTexelOffset: %u.\n", info->properties2.properties.limits.maxTexelOffset);
    TRACE("  minTexelGatherOffset: %d.\n", info->properties2.properties.limits.minTexelGatherOffset);
    TRACE("  maxTexelGatherOffset: %u.\n", info->properties2.properties.limits.maxTexelGatherOffset);
    TRACE("  minInterpolationOffset: %f.\n", info->properties2.properties.limits.minInterpolationOffset);
    TRACE("  maxInterpolationOffset: %f.\n", info->properties2.properties.limits.maxInterpolationOffset);
    TRACE("  subPixelInterpolationOffsetBits: %u.\n", info->properties2.properties.limits.subPixelInterpolationOffsetBits);
    TRACE("  maxFramebufferWidth: %u.\n", info->properties2.properties.limits.maxFramebufferWidth);
    TRACE("  maxFramebufferHeight: %u.\n", info->properties2.properties.limits.maxFramebufferHeight);
    TRACE("  maxFramebufferLayers: %u.\n", info->properties2.properties.limits.maxFramebufferLayers);
    TRACE("  framebufferColorSampleCounts: %#x.\n", info->properties2.properties.limits.framebufferColorSampleCounts);
    TRACE("  framebufferDepthSampleCounts: %#x.\n", info->properties2.properties.limits.framebufferDepthSampleCounts);
    TRACE("  framebufferStencilSampleCounts: %#x.\n", info->properties2.properties.limits.framebufferStencilSampleCounts);
    TRACE("  framebufferNoAttachmentsSampleCounts: %#x.\n", info->properties2.properties.limits.framebufferNoAttachmentsSampleCounts);
    TRACE("  maxColorAttachments: %u.\n", info->properties2.properties.limits.maxColorAttachments);
    TRACE("  sampledImageColorSampleCounts: %#x.\n", info->properties2.properties.limits.sampledImageColorSampleCounts);
    TRACE("  sampledImageIntegerSampleCounts: %#x.\n", info->properties2.properties.limits.sampledImageIntegerSampleCounts);
    TRACE("  sampledImageDepthSampleCounts: %#x.\n", info->properties2.properties.limits.sampledImageDepthSampleCounts);
    TRACE("  sampledImageStencilSampleCounts: %#x.\n", info->properties2.properties.limits.sampledImageStencilSampleCounts);
    TRACE("  storageImageSampleCounts: %#x.\n", info->properties2.properties.limits.storageImageSampleCounts);
    TRACE("  maxSampleMaskWords: %u.\n", info->properties2.properties.limits.maxSampleMaskWords);
    TRACE("  timestampComputeAndGraphics: %#x.\n", info->properties2.properties.limits.timestampComputeAndGraphics);
    TRACE("  timestampPeriod: %f.\n", info->properties2.properties.limits.timestampPeriod);
    TRACE("  maxClipDistances: %u.\n", info->properties2.properties.limits.maxClipDistances);
    TRACE("  maxCullDistances: %u.\n", info->properties2.properties.limits.maxCullDistances);
    TRACE("  maxCombinedClipAndCullDistances: %u.\n", info->properties2.properties.limits.maxCombinedClipAndCullDistances);
    TRACE("  discreteQueuePriorities: %u.\n", info->properties2.properties.limits.discreteQueuePriorities);
    TRACE("  pointSizeRange: %f, %f.\n", info->properties2.properties.limits.pointSizeRange[0], info->properties2.properties.limits.pointSizeRange[1]);
    TRACE("  lineWidthRange: %f, %f,\n", info->properties2.properties.limits.lineWidthRange[0], info->properties2.properties.limits.lineWidthRange[1]);
    TRACE("  pointSizeGranularity: %f.\n", info->properties2.properties.limits.pointSizeGranularity);
    TRACE("  lineWidthGranularity: %f.\n", info->properties2.properties.limits.lineWidthGranularity);
    TRACE("  strictLines: %#x.\n", info->properties2.properties.limits.strictLines);
    TRACE("  standardSampleLocations: %#x.\n", info->properties2.properties.limits.standardSampleLocations);
    TRACE("  optimalBufferCopyOffsetAlignment: %#"PRIx64".\n", info->properties2.properties.limits.optimalBufferCopyOffsetAlignment);
    TRACE("  optimalBufferCopyRowPitchAlignment: %#"PRIx64".\n", info->properties2.properties.limits.optimalBufferCopyRowPitchAlignment);
    TRACE("  nonCoherentAtomSize: %#"PRIx64".\n", info->properties2.properties.limits.nonCoherentAtomSize);

    TRACE("  VkPhysicalDeviceDescriptorIndexingPropertiesEXT:\n");
    TRACE("    maxUpdateAfterBindDescriptorsInAllPools: %u.\n",
            info->vulkan_1_2_properties.maxUpdateAfterBindDescriptorsInAllPools);

    TRACE("    shaderUniformBufferArrayNonUniformIndexingNative: %#x.\n",
            info->vulkan_1_2_properties.shaderUniformBufferArrayNonUniformIndexingNative);
    TRACE("    shaderSampledImageArrayNonUniformIndexingNative: %#x.\n",
            info->vulkan_1_2_properties.shaderSampledImageArrayNonUniformIndexingNative);
    TRACE("    shaderStorageBufferArrayNonUniformIndexingNative: %#x.\n",
            info->vulkan_1_2_properties.shaderStorageBufferArrayNonUniformIndexingNative);
    TRACE("    shaderStorageImageArrayNonUniformIndexingNative: %#x.\n",
            info->vulkan_1_2_properties.shaderStorageImageArrayNonUniformIndexingNative);
    TRACE("    shaderInputAttachmentArrayNonUniformIndexingNative: %#x.\n",
            info->vulkan_1_2_properties.shaderInputAttachmentArrayNonUniformIndexingNative);

    TRACE("    robustBufferAccessUpdateAfterBind: %#x.\n",
            info->vulkan_1_2_properties.robustBufferAccessUpdateAfterBind);
    TRACE("    quadDivergentImplicitLod: %#x.\n",
            info->vulkan_1_2_properties.quadDivergentImplicitLod);

    TRACE("    maxPerStageDescriptorUpdateAfterBindSamplers: %u.\n",
            info->vulkan_1_2_properties.maxPerStageDescriptorUpdateAfterBindSamplers);
    TRACE("    maxPerStageDescriptorUpdateAfterBindUniformBuffers: %u.\n",
            info->vulkan_1_2_properties.maxPerStageDescriptorUpdateAfterBindUniformBuffers);
    TRACE("    maxPerStageDescriptorUpdateAfterBindStorageBuffers: %u.\n",
            info->vulkan_1_2_properties.maxPerStageDescriptorUpdateAfterBindStorageBuffers);
    TRACE("    maxPerStageDescriptorUpdateAfterBindSampledImages: %u.\n",
            info->vulkan_1_2_properties.maxPerStageDescriptorUpdateAfterBindSampledImages);
    TRACE("    maxPerStageDescriptorUpdateAfterBindStorageImages: %u.\n",
            info->vulkan_1_2_properties.maxPerStageDescriptorUpdateAfterBindStorageImages);
    TRACE("    maxPerStageDescriptorUpdateAfterBindInputAttachments: %u.\n",
            info->vulkan_1_2_properties.maxPerStageDescriptorUpdateAfterBindInputAttachments);
    TRACE("    maxPerStageUpdateAfterBindResources: %u.\n",
            info->vulkan_1_2_properties.maxPerStageUpdateAfterBindResources);

    TRACE("    maxDescriptorSetUpdateAfterBindSamplers: %u.\n",
            info->vulkan_1_2_properties.maxDescriptorSetUpdateAfterBindSamplers);
    TRACE("    maxDescriptorSetUpdateAfterBindUniformBuffers: %u.\n",
            info->vulkan_1_2_properties.maxDescriptorSetUpdateAfterBindUniformBuffers);
    TRACE("    maxDescriptorSetUpdateAfterBindUniformBuffersDynamic: %u.\n",
            info->vulkan_1_2_properties.maxDescriptorSetUpdateAfterBindUniformBuffersDynamic);
    TRACE("    maxDescriptorSetUpdateAfterBindStorageBuffers: %u.\n",
            info->vulkan_1_2_properties.maxDescriptorSetUpdateAfterBindStorageBuffers);
    TRACE("    maxDescriptorSetUpdateAfterBindStorageBuffersDynamic: %u.\n",
            info->vulkan_1_2_properties.maxDescriptorSetUpdateAfterBindStorageBuffersDynamic);
    TRACE("    maxDescriptorSetUpdateAfterBindSampledImages: %u.\n",
            info->vulkan_1_2_properties.maxDescriptorSetUpdateAfterBindSampledImages);
    TRACE("    maxDescriptorSetUpdateAfterBindStorageImages: %u.\n",
            info->vulkan_1_2_properties.maxDescriptorSetUpdateAfterBindStorageImages);
    TRACE("    maxDescriptorSetUpdateAfterBindInputAttachments: %u.\n",
            info->vulkan_1_2_properties.maxDescriptorSetUpdateAfterBindInputAttachments);

    TRACE("    maxPerSetDescriptors: %u.\n", info->vulkan_1_1_properties.maxPerSetDescriptors);
    TRACE("    maxMemoryAllocationSize: %#"PRIx64".\n", info->vulkan_1_1_properties.maxMemoryAllocationSize);

    TRACE("  VkPhysicalDeviceTexelBufferAlignmentPropertiesEXT:\n");
    TRACE("    storageTexelBufferOffsetAlignmentBytes: %#"PRIx64".\n",
            info->vulkan_1_3_properties.storageTexelBufferOffsetAlignmentBytes);
    TRACE("    storageTexelBufferOffsetSingleTexelAlignment: %#x.\n",
            info->vulkan_1_3_properties.storageTexelBufferOffsetSingleTexelAlignment);
    TRACE("    uniformTexelBufferOffsetAlignmentBytes: %#"PRIx64".\n",
            info->vulkan_1_3_properties.uniformTexelBufferOffsetAlignmentBytes);
    TRACE("    uniformTexelBufferOffsetSingleTexelAlignment: %#x.\n",
            info->vulkan_1_3_properties.uniformTexelBufferOffsetSingleTexelAlignment);

    TRACE("  VkPhysicalDeviceTransformFeedbackPropertiesEXT:\n");
    TRACE("    maxTransformFeedbackStreams: %u.\n", info->xfb_properties.maxTransformFeedbackStreams);
    TRACE("    maxTransformFeedbackBuffers: %u.\n", info->xfb_properties.maxTransformFeedbackBuffers);
    TRACE("    maxTransformFeedbackBufferSize: %#"PRIx64".\n", info->xfb_properties.maxTransformFeedbackBufferSize);
    TRACE("    maxTransformFeedbackStreamDataSize: %u.\n", info->xfb_properties.maxTransformFeedbackStreamDataSize);
    TRACE("    maxTransformFeedbackBufferDataSize: %u.\n", info->xfb_properties.maxTransformFeedbackBufferDataSize);
    TRACE("    maxTransformFeedbackBufferDataStride: %u.\n", info->xfb_properties.maxTransformFeedbackBufferDataStride);
    TRACE("    transformFeedbackQueries: %#x.\n", info->xfb_properties.transformFeedbackQueries);
    TRACE("    transformFeedbackStreamsLinesTriangles: %#x.\n", info->xfb_properties.transformFeedbackStreamsLinesTriangles);
    TRACE("    transformFeedbackRasterizationStreamSelect: %#x.\n", info->xfb_properties.transformFeedbackRasterizationStreamSelect);
    TRACE("    transformFeedbackDraw: %x.\n", info->xfb_properties.transformFeedbackDraw);

    TRACE("  VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT:\n");
    TRACE("    maxVertexAttribDivisor: %u.\n", info->vertex_divisor_properties.maxVertexAttribDivisor);
}

static void vkd3d_trace_physical_device_features(const struct vkd3d_physical_device_info *info)
{
    TRACE("Device features:\n");
    TRACE("  robustBufferAccess: %#x.\n", info->features2.features.robustBufferAccess);
    TRACE("  fullDrawIndexUint32: %#x.\n", info->features2.features.fullDrawIndexUint32);
    TRACE("  imageCubeArray: %#x.\n", info->features2.features.imageCubeArray);
    TRACE("  independentBlend: %#x.\n", info->features2.features.independentBlend);
    TRACE("  geometryShader: %#x.\n", info->features2.features.geometryShader);
    TRACE("  tessellationShader: %#x.\n", info->features2.features.tessellationShader);
    TRACE("  sampleRateShading: %#x.\n", info->features2.features.sampleRateShading);
    TRACE("  dualSrcBlend: %#x.\n", info->features2.features.dualSrcBlend);
    TRACE("  logicOp: %#x.\n", info->features2.features.logicOp);
    TRACE("  multiDrawIndirect: %#x.\n", info->features2.features.multiDrawIndirect);
    TRACE("  drawIndirectFirstInstance: %#x.\n", info->features2.features.drawIndirectFirstInstance);
    TRACE("  depthClamp: %#x.\n", info->features2.features.depthClamp);
    TRACE("  depthBiasClamp: %#x.\n", info->features2.features.depthBiasClamp);
    TRACE("  fillModeNonSolid: %#x.\n", info->features2.features.fillModeNonSolid);
    TRACE("  depthBounds: %#x.\n", info->features2.features.depthBounds);
    TRACE("  wideLines: %#x.\n", info->features2.features.wideLines);
    TRACE("  largePoints: %#x.\n", info->features2.features.largePoints);
    TRACE("  alphaToOne: %#x.\n", info->features2.features.alphaToOne);
    TRACE("  multiViewport: %#x.\n", info->features2.features.multiViewport);
    TRACE("  samplerAnisotropy: %#x.\n", info->features2.features.samplerAnisotropy);
    TRACE("  textureCompressionETC2: %#x.\n", info->features2.features.textureCompressionETC2);
    TRACE("  textureCompressionASTC_LDR: %#x.\n", info->features2.features.textureCompressionASTC_LDR);
    TRACE("  textureCompressionBC: %#x.\n", info->features2.features.textureCompressionBC);
    TRACE("  occlusionQueryPrecise: %#x.\n", info->features2.features.occlusionQueryPrecise);
    TRACE("  pipelineStatisticsQuery: %#x.\n", info->features2.features.pipelineStatisticsQuery);
    TRACE("  vertexOipelineStoresAndAtomics: %#x.\n", info->features2.features.vertexPipelineStoresAndAtomics);
    TRACE("  fragmentStoresAndAtomics: %#x.\n", info->features2.features.fragmentStoresAndAtomics);
    TRACE("  shaderTessellationAndGeometryPointSize: %#x.\n", info->features2.features.shaderTessellationAndGeometryPointSize);
    TRACE("  shaderImageGatherExtended: %#x.\n", info->features2.features.shaderImageGatherExtended);
    TRACE("  shaderStorageImageExtendedFormats: %#x.\n", info->features2.features.shaderStorageImageExtendedFormats);
    TRACE("  shaderStorageImageMultisample: %#x.\n", info->features2.features.shaderStorageImageMultisample);
    TRACE("  shaderStorageImageReadWithoutFormat: %#x.\n", info->features2.features.shaderStorageImageReadWithoutFormat);
    TRACE("  shaderStorageImageWriteWithoutFormat: %#x.\n", info->features2.features.shaderStorageImageWriteWithoutFormat);
    TRACE("  shaderUniformBufferArrayDynamicIndexing: %#x.\n", info->features2.features.shaderUniformBufferArrayDynamicIndexing);
    TRACE("  shaderSampledImageArrayDynamicIndexing: %#x.\n", info->features2.features.shaderSampledImageArrayDynamicIndexing);
    TRACE("  shaderStorageBufferArrayDynamicIndexing: %#x.\n", info->features2.features.shaderStorageBufferArrayDynamicIndexing);
    TRACE("  shaderStorageImageArrayDynamicIndexing: %#x.\n", info->features2.features.shaderStorageImageArrayDynamicIndexing);
    TRACE("  shaderClipDistance: %#x.\n", info->features2.features.shaderClipDistance);
    TRACE("  shaderCullDistance: %#x.\n", info->features2.features.shaderCullDistance);
    TRACE("  shaderFloat64: %#x.\n", info->features2.features.shaderFloat64);
    TRACE("  shaderInt64: %#x.\n", info->features2.features.shaderInt64);
    TRACE("  shaderInt16: %#x.\n", info->features2.features.shaderInt16);
    TRACE("  shaderResourceResidency: %#x.\n", info->features2.features.shaderResourceResidency);
    TRACE("  shaderResourceMinLod: %#x.\n", info->features2.features.shaderResourceMinLod);
    TRACE("  sparseBinding: %#x.\n", info->features2.features.sparseBinding);
    TRACE("  sparseResidencyBuffer: %#x.\n", info->features2.features.sparseResidencyBuffer);
    TRACE("  sparseResidencyImage2D: %#x.\n", info->features2.features.sparseResidencyImage2D);
    TRACE("  sparseResidencyImage3D: %#x.\n", info->features2.features.sparseResidencyImage3D);
    TRACE("  sparseResidency2Samples: %#x.\n", info->features2.features.sparseResidency2Samples);
    TRACE("  sparseResidency4Samples: %#x.\n", info->features2.features.sparseResidency4Samples);
    TRACE("  sparseResidency8Samples: %#x.\n", info->features2.features.sparseResidency8Samples);
    TRACE("  sparseResidency16Samples: %#x.\n", info->features2.features.sparseResidency16Samples);
    TRACE("  sparseResidencyAliased: %#x.\n", info->features2.features.sparseResidencyAliased);
    TRACE("  variableMultisampleRate: %#x.\n", info->features2.features.variableMultisampleRate);
    TRACE("  inheritedQueries: %#x.\n", info->features2.features.inheritedQueries);

    TRACE("  VkPhysicalDeviceDescriptorIndexingFeaturesEXT:\n");
    TRACE("    shaderInputAttachmentArrayDynamicIndexing: %#x.\n",
            info->vulkan_1_2_features.shaderInputAttachmentArrayDynamicIndexing);
    TRACE("    shaderUniformTexelBufferArrayDynamicIndexing: %#x.\n",
            info->vulkan_1_2_features.shaderUniformTexelBufferArrayDynamicIndexing);
    TRACE("    shaderStorageTexelBufferArrayDynamicIndexing: %#x.\n",
            info->vulkan_1_2_features.shaderStorageTexelBufferArrayDynamicIndexing);

    TRACE("    shaderUniformBufferArrayNonUniformIndexing: %#x.\n",
            info->vulkan_1_2_features.shaderUniformBufferArrayNonUniformIndexing);
    TRACE("    shaderSampledImageArrayNonUniformIndexing: %#x.\n",
            info->vulkan_1_2_features.shaderSampledImageArrayNonUniformIndexing);
    TRACE("    shaderStorageBufferArrayNonUniformIndexing: %#x.\n",
            info->vulkan_1_2_features.shaderStorageBufferArrayNonUniformIndexing);
    TRACE("    shaderStorageImageArrayNonUniformIndexing: %#x.\n",
            info->vulkan_1_2_features.shaderStorageImageArrayNonUniformIndexing);
    TRACE("    shaderInputAttachmentArrayNonUniformIndexing: %#x.\n",
            info->vulkan_1_2_features.shaderInputAttachmentArrayNonUniformIndexing);
    TRACE("    shaderUniformTexelBufferArrayNonUniformIndexing: %#x.\n",
            info->vulkan_1_2_features.shaderUniformTexelBufferArrayNonUniformIndexing);
    TRACE("    shaderStorageTexelBufferArrayNonUniformIndexing: %#x.\n",
            info->vulkan_1_2_features.shaderStorageTexelBufferArrayNonUniformIndexing);

    TRACE("    descriptorBindingUniformBufferUpdateAfterBind: %#x.\n",
            info->vulkan_1_2_features.descriptorBindingUniformBufferUpdateAfterBind);
    TRACE("    descriptorBindingSampledImageUpdateAfterBind: %#x.\n",
            info->vulkan_1_2_features.descriptorBindingSampledImageUpdateAfterBind);
    TRACE("    descriptorBindingStorageImageUpdateAfterBind: %#x.\n",
            info->vulkan_1_2_features.descriptorBindingStorageImageUpdateAfterBind);
    TRACE("    descriptorBindingStorageBufferUpdateAfterBind: %#x.\n",
            info->vulkan_1_2_features.descriptorBindingStorageBufferUpdateAfterBind);
    TRACE("    descriptorBindingUniformTexelBufferUpdateAfterBind: %#x.\n",
            info->vulkan_1_2_features.descriptorBindingUniformTexelBufferUpdateAfterBind);
    TRACE("    descriptorBindingStorageTexelBufferUpdateAfterBind: %#x.\n",
            info->vulkan_1_2_features.descriptorBindingStorageTexelBufferUpdateAfterBind);

    TRACE("    descriptorBindingUpdateUnusedWhilePending: %#x.\n",
            info->vulkan_1_2_features.descriptorBindingUpdateUnusedWhilePending);
    TRACE("    descriptorBindingPartiallyBound: %#x.\n",
            info->vulkan_1_2_features.descriptorBindingPartiallyBound);
    TRACE("    descriptorBindingVariableDescriptorCount: %#x.\n",
            info->vulkan_1_2_features.descriptorBindingVariableDescriptorCount);
    TRACE("    runtimeDescriptorArray: %#x.\n",
            info->vulkan_1_2_features.runtimeDescriptorArray);

    TRACE("  VkPhysicalDeviceConditionalRenderingFeaturesEXT:\n");
    TRACE("    conditionalRendering: %#x.\n", info->conditional_rendering_features.conditionalRendering);

    TRACE("  VkPhysicalDeviceDepthClipEnableFeaturesEXT:\n");
    TRACE("    depthClipEnable: %#x.\n", info->depth_clip_features.depthClipEnable);

    TRACE("  VkPhysicalDeviceTransformFeedbackFeaturesEXT:\n");
    TRACE("    transformFeedback: %#x.\n", info->xfb_features.transformFeedback);
    TRACE("    geometryStreams: %#x.\n", info->xfb_features.geometryStreams);

    TRACE("  VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT:\n");
    TRACE("    vertexAttributeInstanceRateDivisor: %#x.\n",
            info->vertex_divisor_features.vertexAttributeInstanceRateDivisor);
    TRACE("    vertexAttributeInstanceRateZeroDivisor: %#x.\n",
            info->vertex_divisor_features.vertexAttributeInstanceRateZeroDivisor);

    TRACE("  VkPhysicalDeviceCustomBorderColorFeaturesEXT:\n");
    TRACE("    customBorderColors: %#x\n", info->custom_border_color_features.customBorderColors);
    TRACE("    customBorderColorWithoutFormat: %#x\n", info->custom_border_color_features.customBorderColorWithoutFormat);

    TRACE("  VkPhysicalDeviceMeshShaderFeaturesEXT:\n");
    TRACE("    meshShader: %#x\n", info->mesh_shader_features.meshShader);
    TRACE("    taskShader: %#x\n", info->mesh_shader_features.taskShader);
    TRACE("    multiviewMeshShader: %#x\n", info->mesh_shader_features.multiviewMeshShader);
    TRACE("    primitiveFragmentShadingRateMeshShader: %#x\n", info->mesh_shader_features.primitiveFragmentShadingRateMeshShader);

    TRACE("  VkPhysicalDeviceLineRasterizationFeaturesEXT:\n");
    TRACE("    rectangularLines: %u\n", info->line_rasterization_features.rectangularLines);
    TRACE("    smoothLines: %u\n", info->line_rasterization_features.smoothLines);
}

static HRESULT vkd3d_init_device_extensions(struct d3d12_device *device,
        const struct vkd3d_device_create_info *create_info,
        uint32_t *device_extension_count, bool *user_extension_supported)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance->vk_procs;
    VkPhysicalDevice physical_device = device->vk_physical_device;
    struct vkd3d_vulkan_info *vulkan_info = &device->vk_info;
    VkExtensionProperties *vk_extensions;
    uint32_t count;
    VkResult vr;

    *device_extension_count = 0;

    if ((vr = VK_CALL(vkEnumerateDeviceExtensionProperties(physical_device, NULL, &count, NULL))) < 0)
    {
        ERR("Failed to enumerate device extensions, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }
    if (!count)
        return S_OK;

    if (!(vk_extensions = vkd3d_calloc(count, sizeof(*vk_extensions))))
        return E_OUTOFMEMORY;

    TRACE("Enumerating %u device extensions.\n", count);
    if ((vr = VK_CALL(vkEnumerateDeviceExtensionProperties(physical_device, NULL, &count, vk_extensions))) < 0)
    {
        ERR("Failed to enumerate device extensions, vr %d.\n", vr);
        vkd3d_free(vk_extensions);
        return hresult_from_vk_result(vr);
    }

    *device_extension_count = vkd3d_check_extensions(vk_extensions, count, NULL, 0,
            optional_device_extensions, ARRAY_SIZE(optional_device_extensions),
            create_info->device_extensions,
            create_info->device_extension_count,
            create_info->optional_device_extensions,
            create_info->optional_device_extension_count,
            user_extension_supported, vulkan_info, "device");

    if (get_spec_version(vk_extensions, count, VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME) < 3)
        vulkan_info->EXT_vertex_attribute_divisor = false;

    vkd3d_free(vk_extensions);
    return S_OK;
}

static bool vkd3d_supports_minimum_coopmat_caps(struct d3d12_device *device)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance->vk_procs;
    VkCooperativeMatrixPropertiesKHR *props;
    bool supports_f32_16x16x16_f16 = false;
    bool supports_f32_16x16x16_f8 = false;
    bool supports_8bit_a = false;
    bool supports_8bit_b = false;
    bool supports_8bit_c = false;
    uint32_t i, count;
    bool fp8;

    fp8 = device->device_info.shader_float8_features.shaderFloat8CooperativeMatrix == VK_TRUE;

    /* There are no sub-capabilities (yet at least).
     * Validate that we support everything that dxil-spirv can emit. */
    if (VK_CALL(vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(
            device->vk_physical_device, &count, NULL)) != VK_SUCCESS)
    {
        ERR("Failed to query cooperative matrix properties.\n");
        return false;
    }

    props = vkd3d_calloc(count, sizeof(*props));
    for (i = 0; i < count; i++)
        props[i].sType = VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR;

    if (VK_CALL(vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(
            device->vk_physical_device, &count, props)) != VK_SUCCESS)
    {
        ERR("Failed to query cooperative matrix properties.\n");
        vkd3d_free(props);
        return false;
    }

    for (i = 0; i < count; i++)
    {
        const VkCooperativeMatrixPropertiesKHR *fmt = &props[i];

        if (fp8)
        {
            if (fmt->AType == VK_COMPONENT_TYPE_FLOAT8_E4M3_EXT)
                supports_8bit_a = true;
            if (fmt->BType == VK_COMPONENT_TYPE_FLOAT8_E4M3_EXT)
                supports_8bit_b = true;
            if (fmt->CType == VK_COMPONENT_TYPE_FLOAT8_E4M3_EXT)
                supports_8bit_c = true;
        }
        else
        {
            /* In the fallback, we use u8 as an intermediate format. */
            if (fmt->AType == VK_COMPONENT_TYPE_UINT8_KHR)
                supports_8bit_a = true;
            if (fmt->BType == VK_COMPONENT_TYPE_UINT8_KHR)
                supports_8bit_b = true;
            if (fmt->CType == VK_COMPONENT_TYPE_UINT8_KHR)
                supports_8bit_c = true;
        }

        if (fmt->KSize != 16 || fmt->MSize != 16 || fmt->NSize != 16 || fmt->scope != VK_SCOPE_SUBGROUP_KHR)
            continue;

        if (fmt->CType == VK_COMPONENT_TYPE_FLOAT32_KHR)
        {
            if (fmt->AType == VK_COMPONENT_TYPE_FLOAT16_KHR && fmt->BType == VK_COMPONENT_TYPE_FLOAT16_KHR)
            {
                if (fmt->CType == VK_COMPONENT_TYPE_FLOAT32_KHR)
                    supports_f32_16x16x16_f16 = true;
            }
            else if (fmt->AType == VK_COMPONENT_TYPE_FLOAT8_E4M3_EXT && fmt->BType == VK_COMPONENT_TYPE_FLOAT8_E4M3_EXT)
            {
                if (fmt->CType == VK_COMPONENT_TYPE_FLOAT32_KHR)
                    supports_f32_16x16x16_f8 = true;
            }
        }
    }

    vkd3d_free(props);

    if (!supports_f32_16x16x16_f16 || !supports_8bit_a || !supports_8bit_b)
    {
        WARN("Missing sufficient features to expose WMMA.\n");
        return false;
    }

    if (fp8 && !supports_f32_16x16x16_f8)
    {
        WARN("Missing sufficient features to expose WMMA.\n");
        return false;
    }

    if (!supports_8bit_c)
    {
        WARN("8-bit Accumulator type not exposed, but assuming it works anyway. "
             "This is required for FSR4 and happens to work in practice on AMD GPUs.\n");
    }

    return true;
}

static HRESULT vkd3d_init_device_caps(struct d3d12_device *device,
        const struct vkd3d_device_create_info *create_info,
        struct vkd3d_physical_device_info *physical_device_info)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance->vk_procs;
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT *extended_dynamic_state3;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR *acceleration_structure;
    VkPhysicalDeviceLineRasterizationFeaturesEXT *line_rasterization;
    VkPhysicalDeviceDescriptorBufferFeaturesEXT *descriptor_buffer;
    VkPhysicalDevice physical_device = device->vk_physical_device;
    struct vkd3d_vulkan_info *vulkan_info = &device->vk_info;
    VkPhysicalDeviceFeatures *features;
    bool single_storage_texel;
    bool single_uniform_texel;

    vkd3d_trace_physical_device(physical_device, physical_device_info, vk_procs);
    vkd3d_trace_physical_device_features(physical_device_info);
    vkd3d_trace_physical_device_limits(physical_device_info);

    features = &physical_device_info->features2.features;

    if (!features->sparseResidencyBuffer || !features->sparseResidencyImage2D)
    {
        features->sparseResidencyBuffer = VK_FALSE;
        features->sparseResidencyImage2D = VK_FALSE;
        physical_device_info->properties2.properties.sparseProperties.residencyNonResidentStrict = VK_FALSE;
    }

    /* We need independent interpolation to use GPL. */
    if (!physical_device_info->graphics_pipeline_library_properties.graphicsPipelineLibraryIndependentInterpolationDecoration)
    {
        vulkan_info->EXT_graphics_pipeline_library = false;
        physical_device_info->graphics_pipeline_library_features.graphicsPipelineLibrary = VK_FALSE;
    }

    vulkan_info->device_limits = physical_device_info->properties2.properties.limits;
    vulkan_info->sparse_properties = physical_device_info->properties2.properties.sparseProperties;
    vulkan_info->rasterization_stream = physical_device_info->xfb_properties.transformFeedbackRasterizationStreamSelect;
    vulkan_info->max_vertex_attrib_divisor = max(physical_device_info->vertex_divisor_properties.maxVertexAttribDivisor, 1);

    if (!physical_device_info->conditional_rendering_features.conditionalRendering)
        vulkan_info->EXT_conditional_rendering = false;
    if (!physical_device_info->depth_clip_features.depthClipEnable)
        vulkan_info->EXT_depth_clip_enable = false;

    if (!physical_device_info->vertex_divisor_features.vertexAttributeInstanceRateDivisor ||
            !physical_device_info->vertex_divisor_features.vertexAttributeInstanceRateZeroDivisor)
    {
        ERR("Lacking support for VK_EXT_vertex_attribute_divisor.\n");
        return E_INVALIDARG;
    }

    if (!physical_device_info->xfb_properties.transformFeedbackQueries)
    {
        ERR("Lacking support for transform feedback.\n");
        return E_INVALIDARG;
    }

    single_storage_texel =
            physical_device_info->vulkan_1_3_properties.storageTexelBufferOffsetSingleTexelAlignment ||
            physical_device_info->vulkan_1_3_properties.storageTexelBufferOffsetAlignmentBytes == 1;

    /* ANV workaround, uniform texel is not set, but alignment requirement is 1, which is basically the same thing. */
    single_uniform_texel =
            physical_device_info->vulkan_1_3_properties.uniformTexelBufferOffsetSingleTexelAlignment ||
            physical_device_info->vulkan_1_3_properties.uniformTexelBufferOffsetAlignmentBytes == 1;

    if (!single_storage_texel || !single_uniform_texel)
    {
        ERR("Lacking support for single texel alignment.\n");
        return E_INVALIDARG;
    }

    /* Disable unused Vulkan features. The following features need to remain enabled
     * for DXVK in order to support D3D11on12: hostQueryReset, vulkanMemoryModel.
     * We need storageBuffer8BitAccess for DStorage fallback. */
    features->shaderTessellationAndGeometryPointSize = VK_FALSE;

    physical_device_info->vulkan_1_1_features.protectedMemory = VK_FALSE;
    physical_device_info->vulkan_1_1_features.samplerYcbcrConversion = VK_FALSE;

    physical_device_info->vulkan_1_2_features.uniformAndStorageBuffer8BitAccess = VK_FALSE;
    physical_device_info->vulkan_1_2_features.storagePushConstant8 = VK_FALSE;
    physical_device_info->vulkan_1_2_features.shaderInputAttachmentArrayDynamicIndexing = VK_FALSE;
    physical_device_info->vulkan_1_2_features.shaderInputAttachmentArrayNonUniformIndexing = VK_FALSE;
    physical_device_info->vulkan_1_2_features.bufferDeviceAddressCaptureReplay = VK_FALSE;
    physical_device_info->vulkan_1_2_features.bufferDeviceAddressMultiDevice = VK_FALSE;
    physical_device_info->vulkan_1_2_features.imagelessFramebuffer = VK_FALSE;
    physical_device_info->vulkan_1_2_features.vulkanMemoryModelAvailabilityVisibilityChains = VK_FALSE;

    physical_device_info->vulkan_1_3_features.robustImageAccess = VK_FALSE;
    physical_device_info->vulkan_1_3_features.inlineUniformBlock = VK_FALSE;
    physical_device_info->vulkan_1_3_features.descriptorBindingInlineUniformBlockUpdateAfterBind = VK_FALSE;
    physical_device_info->vulkan_1_3_features.privateData = VK_FALSE;
    physical_device_info->vulkan_1_3_features.textureCompressionASTC_HDR = VK_FALSE;

    if (!physical_device_info->fragment_shading_rate_features.primitiveFragmentShadingRate)
        physical_device_info->mesh_shader_features.primitiveFragmentShadingRateMeshShader = VK_FALSE;

    descriptor_buffer = &physical_device_info->descriptor_buffer_features;
    descriptor_buffer->descriptorBufferCaptureReplay = VK_FALSE;
    descriptor_buffer->descriptorBufferImageLayoutIgnored = VK_FALSE;

    /* We only use dynamic rasterization samples. Also Keep the following enabled for 11on12:
     * alphaToCoverage, sampleMask, lineRasterizationMode, depthClipEnable. */
    extended_dynamic_state3 = &physical_device_info->extended_dynamic_state3_features;
    extended_dynamic_state3->extendedDynamicState3TessellationDomainOrigin = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3DepthClampEnable = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3PolygonMode = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3AlphaToOneEnable = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3LogicOpEnable = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3ColorBlendEnable = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3ColorBlendEquation = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3ColorWriteMask = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3RasterizationStream = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3ConservativeRasterizationMode = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3ExtraPrimitiveOverestimationSize = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3SampleLocationsEnable = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3ColorBlendAdvanced = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3ProvokingVertexMode = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3LineStippleEnable = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3DepthClipNegativeOneToOne = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3ViewportWScalingEnable = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3ViewportSwizzle = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3CoverageToColorEnable = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3CoverageToColorLocation = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3CoverageModulationMode = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3CoverageModulationTableEnable = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3CoverageModulationTable = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3CoverageReductionMode = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3RepresentativeFragmentTestEnable = VK_FALSE;
    extended_dynamic_state3->extendedDynamicState3ShadingRateImageEnable = VK_FALSE;

    acceleration_structure = &physical_device_info->acceleration_structure_features;
    acceleration_structure->accelerationStructureCaptureReplay = VK_FALSE;
    acceleration_structure->accelerationStructureHostCommands = VK_FALSE;
    acceleration_structure->accelerationStructureIndirectBuild = VK_FALSE;
    physical_device_info->ray_tracing_pipeline_features.rayTracingPipelineShaderGroupHandleCaptureReplay = VK_FALSE;
    physical_device_info->ray_tracing_pipeline_features.rayTracingPipelineShaderGroupHandleCaptureReplayMixed = VK_FALSE;

    line_rasterization = &physical_device_info->line_rasterization_features;
    line_rasterization->bresenhamLines = VK_FALSE;
    line_rasterization->stippledRectangularLines = VK_FALSE;
    line_rasterization->stippledBresenhamLines = VK_FALSE;
    line_rasterization->stippledSmoothLines = VK_FALSE;

    /* Don't need or require this. Dynamic patch control points is nice, but not required. */
    physical_device_info->extended_dynamic_state2_features.extendedDynamicState2LogicOp = VK_FALSE;

    /* Unneeded. */
    physical_device_info->device_generated_commands_compute_features_nv.deviceGeneratedComputeCaptureReplay = VK_FALSE;
    physical_device_info->device_generated_commands_compute_features_nv.deviceGeneratedComputePipelines = VK_FALSE;
    physical_device_info->device_generated_commands_features_ext.dynamicGeneratedPipelineLayout = VK_FALSE;

    {
        const VkShaderStageFlags supported_stages =
                physical_device_info->device_generated_commands_properties_ext.supportedIndirectCommandsShaderStages;
        VkShaderStageFlags required_stages = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT;
        if (physical_device_info->mesh_shader_features.meshShader)
            required_stages |= VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT;

        if ((supported_stages & required_stages) != required_stages)
        {
            /* Can be relaxed if needed in the wild, but in principle, everything needs to be supported
             * to be D3D12 compliant. */
            INFO("Not all relevant pipeline stages are supported by EXT_dgc. Skipping EXT.\n");
            physical_device_info->device_generated_commands_features_ext.deviceGeneratedCommands = VK_FALSE;
        }
    }

    if (physical_device_info->device_generated_commands_features_ext.deviceGeneratedCommands)
    {
        physical_device_info->device_generated_commands_features_nv.deviceGeneratedCommands = VK_FALSE;
        physical_device_info->device_generated_commands_compute_features_nv.deviceGeneratedCompute = VK_FALSE;
    }

    if (!physical_device_info->vulkan_1_2_properties.robustBufferAccessUpdateAfterBind)
    {
        /* Generally, we cannot enable robustness if this is not supported,
         * but this means we cannot support D3D12 at all, so just disabling robustBufferAccess is not a viable option.
         * This can be observed on RADV, where this feature for some reason is not supported at all,
         * but this apparently was just a missed feature bit.
         * https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/9281.
         *
         * Another reason to not disable it, is that we will end up with two
         * split application infos in Fossilize, which is annoying for pragmatic reasons.
         * Validation does not appear to complain, so we just go ahead and enable robustness anyways. */
        WARN("Device does not expose robust buffer access for the update after bind feature, enabling it anyways.\n");
    }

    if (!physical_device_info->vulkan_1_2_features.samplerMirrorClampToEdge)
    {
        ERR("samplerMirrorClampToEdge is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (!physical_device_info->robustness2_features.robustBufferAccess2 ||
            !physical_device_info->robustness2_features.robustImageAccess2)
    {
        ERR("Robustness2 features not supported. This is required.\n");
        return E_INVALIDARG;
    }

    if (!physical_device_info->robustness2_features.nullDescriptor)
    {
        ERR("Null descriptor in VK_EXT_robustness2 is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (vulkan_info->KHR_fragment_shading_rate)
        physical_device_info->additional_shading_rates_supported = d3d12_device_determine_additional_shading_rates_supported(device);

    if (!physical_device_info->vulkan_1_1_features.shaderDrawParameters)
    {
        ERR("shaderDrawParameters is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (!vulkan_info->KHR_push_descriptor)
    {
        ERR("Push descriptors are not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (physical_device_info->cooperative_matrix_features.cooperativeMatrix)
    {
        if (!vkd3d_supports_minimum_coopmat_caps(device))
        {
            physical_device_info->cooperative_matrix_features.cooperativeMatrix = VK_FALSE;
            physical_device_info->cooperative_matrix_features.cooperativeMatrixRobustBufferAccess = VK_FALSE;
            physical_device_info->shader_float8_features.shaderFloat8 = VK_FALSE;
            physical_device_info->shader_float8_features.shaderFloat8CooperativeMatrix = VK_FALSE;
            physical_device_info->cooperative_matrix2_features_nv.cooperativeMatrixBlockLoads = VK_FALSE;
            physical_device_info->cooperative_matrix2_features_nv.cooperativeMatrixConversions = VK_FALSE;
            physical_device_info->cooperative_matrix2_features_nv.cooperativeMatrixFlexibleDimensions = VK_FALSE;
            physical_device_info->cooperative_matrix2_features_nv.cooperativeMatrixPerElementOperations = VK_FALSE;
            physical_device_info->cooperative_matrix2_features_nv.cooperativeMatrixReductions = VK_FALSE;
            physical_device_info->cooperative_matrix2_features_nv.cooperativeMatrixTensorAddressing = VK_FALSE;
            physical_device_info->cooperative_matrix2_features_nv.cooperativeMatrixWorkgroupScope = VK_FALSE;
            vulkan_info->KHR_cooperative_matrix = false;
            vulkan_info->EXT_shader_float8 = false;
            vulkan_info->NV_cooperative_matrix2 = false;
        }
    }

    return S_OK;
}

static HRESULT vkd3d_select_physical_device(struct vkd3d_instance *instance,
        unsigned int device_index, VkPhysicalDevice *selected_device)
{
    VkPhysicalDevice dgpu_device = VK_NULL_HANDLE, igpu_device = VK_NULL_HANDLE;
    const struct vkd3d_vk_instance_procs *vk_procs = &instance->vk_procs;
    VkInstance vk_instance = instance->vk_instance;
    VkPhysicalDeviceProperties device_properties;
    VkPhysicalDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice *physical_devices;
    char filter[VKD3D_PATH_MAX];
    bool has_filter;
    uint32_t count;
    unsigned int i;
    VkResult vr;

    has_filter = vkd3d_get_env_var("VKD3D_FILTER_DEVICE_NAME", filter, sizeof(filter));

    count = 0;
    if ((vr = VK_CALL(vkEnumeratePhysicalDevices(vk_instance, &count, NULL))) < 0)
    {
        ERR("Failed to enumerate physical devices, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }
    if (!count)
    {
        ERR("No physical device available.\n");
        return E_FAIL;
    }
    if (!(physical_devices = vkd3d_calloc(count, sizeof(*physical_devices))))
        return E_OUTOFMEMORY;

    TRACE("Enumerating %u physical device(s).\n", count);
    if ((vr = VK_CALL(vkEnumeratePhysicalDevices(vk_instance, &count, physical_devices))) < 0)
    {
        ERR("Failed to enumerate physical devices, vr %d.\n", vr);
        vkd3d_free(physical_devices);
        return hresult_from_vk_result(vr);
    }

    if (device_index != ~0u && device_index >= count)
        WARN("Device index %u is out of range.\n", device_index);

    for (i = 0; i < count; ++i)
    {
        VK_CALL(vkGetPhysicalDeviceProperties(physical_devices[i], &device_properties));
        vkd3d_trace_physical_device_properties(&device_properties);

        if (device_properties.apiVersion < VKD3D_MIN_API_VERSION)
        {
            WARN("Physical device %u does not support required Vulkan version, ignoring.\n", i);
            continue;
        }

        if (i == device_index)
            device = physical_devices[i];

        if (has_filter && !strstr(device_properties.deviceName, filter))
        {
            INFO("Device %s doesn't match filter %s, skipping.\n", device_properties.deviceName, filter);
            continue;
        }

        if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && !dgpu_device)
            dgpu_device = physical_devices[i];
        else if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU && !igpu_device)
            igpu_device = physical_devices[i];
    }

    if (!device)
        device = dgpu_device ? dgpu_device : igpu_device;
    if (!device)
        device = physical_devices[0];

    vkd3d_free(physical_devices);

    VK_CALL(vkGetPhysicalDeviceProperties(device, &device_properties));
    TRACE("Using device: %s, %#x:%#x.\n", device_properties.deviceName,
            device_properties.vendorID, device_properties.deviceID);

    *selected_device = device;

    return S_OK;
}

/* Vulkan queues */
struct vkd3d_device_queue_info
{
    unsigned int family_index[VKD3D_QUEUE_FAMILY_COUNT];
    VkQueueFamilyProperties vk_properties[VKD3D_QUEUE_FAMILY_COUNT];

    unsigned int vk_family_count;
    VkDeviceQueueCreateInfo vk_queue_create_info[VKD3D_QUEUE_FAMILY_COUNT];
};

static bool vkd3d_queue_family_needs_out_of_band_queue(unsigned int vkd3d_queue_family)
{
    return vkd3d_queue_family == VKD3D_QUEUE_FAMILY_GRAPHICS ||
        vkd3d_queue_family == VKD3D_QUEUE_FAMILY_COMPUTE;
}

static void d3d12_device_destroy_vkd3d_queues(struct d3d12_device *device)
{
    unsigned int i, j;

    for (i = 0; i < VKD3D_QUEUE_FAMILY_COUNT; i++)
    {
        struct vkd3d_queue_family_info *queue_family = device->queue_families[i];

        if (!queue_family)
            continue;

        /* Don't destroy the same queue family twice */
        for (j = i + 1; j < VKD3D_QUEUE_FAMILY_COUNT; j++)
        {
            if (device->queue_families[j] == queue_family)
                device->queue_families[j] = NULL;
        }

        for (j = 0; j < queue_family->queue_count; j++)
        {
            if (queue_family->queues[j])
                vkd3d_queue_drain(queue_family->queues[j], device);
        }

        if (queue_family->out_of_band_queue)
            vkd3d_queue_drain(queue_family->out_of_band_queue, device);
    }

    for (i = 0; i < VKD3D_QUEUE_FAMILY_COUNT; i++)
    {
        struct vkd3d_queue_family_info *queue_family = device->queue_families[i];

        if (!queue_family)
            continue;

        for (j = 0; j < queue_family->queue_count; j++)
        {
            if (queue_family->queues[j])
                vkd3d_queue_destroy(queue_family->queues[j], device);
        }

        if (queue_family->out_of_band_queue)
            vkd3d_queue_destroy(queue_family->out_of_band_queue, device);

        vkd3d_free(queue_family->queues);
        vkd3d_free(queue_family);

        device->queue_families[i] = NULL;
    }
}

static HRESULT d3d12_device_create_vkd3d_queues(struct d3d12_device *device,
        const struct vkd3d_device_queue_info *queue_info)
{
    unsigned int i, j, k;
    HRESULT hr;

    device->unique_queue_mask = 0;
    device->concurrent_queue_family_count = 0;
    memset(device->queue_families, 0, sizeof(device->queue_families));
    for (i = 0; i < VKD3D_QUEUE_FAMILY_COUNT; i++)
    {
        device->concurrent_queue_family_indices[i] = VK_QUEUE_FAMILY_IGNORED;
    }

    for (i = 0, k = 0; i < VKD3D_QUEUE_FAMILY_COUNT; i++)
    {
        struct vkd3d_queue_family_info *info;

        if (queue_info->family_index[i] == VK_QUEUE_FAMILY_IGNORED)
            continue;

        for (j = 0; j < i; j++)
        {
            if (queue_info->family_index[i] == queue_info->family_index[j])
                device->queue_families[i] = device->queue_families[j];
        }

        if (device->queue_families[i])
            continue;

        if (!(info = vkd3d_calloc(1, sizeof(*info))))
        {
            hr = E_OUTOFMEMORY;
            goto out_destroy_queues;
        }

        info->queue_count = queue_info->vk_queue_create_info[k++].queueCount;

        /* Unless the queue family only has a single queue to allocate, when NV_low_latency2
         * is enabled one queue is reserved for out of band work */
        if (device->vk_info.NV_low_latency2 && vkd3d_queue_family_needs_out_of_band_queue(i) &&
                queue_info->vk_properties[i].queueCount > 1)
            info->queue_count--;

        if (!(info->queues = vkd3d_calloc(info->queue_count, sizeof(*info->queues))))
        {
            hr = E_OUTOFMEMORY;
            goto out_destroy_queues;
        }

        for (j = 0; j < info->queue_count; j++)
        {
            if (FAILED((hr = vkd3d_queue_create(device, queue_info->family_index[i],
                    j, &queue_info->vk_properties[i], &info->queues[j]))))
                goto out_destroy_queues;
        }

        if (device->vk_info.NV_low_latency2 && vkd3d_queue_family_needs_out_of_band_queue(i) &&
                queue_info->vk_properties[i].queueCount > 1)
        {
            /* The low latency out of band queue is always the last queue for the family */
            if (FAILED((hr = vkd3d_queue_create(device, queue_info->family_index[i],
                    info->queue_count, &queue_info->vk_properties[i], &info->out_of_band_queue))))
                goto out_destroy_queues;
        }
        else
            WARN("Could not allocate an out of band queue for queue family %u. All out of band work will happen on the in band queue.\n", i);

        info->vk_family_index = queue_info->family_index[i];
        info->vk_queue_flags = queue_info->vk_properties[i].queueFlags;
        info->timestamp_bits = queue_info->vk_properties[i].timestampValidBits;

        device->queue_families[i] = info;

        if (i != VKD3D_QUEUE_FAMILY_SPARSE_BINDING)
            device->concurrent_queue_family_indices[device->concurrent_queue_family_count++] = info->vk_family_index;

        if (info->queue_count && i != VKD3D_QUEUE_FAMILY_INTERNAL_COMPUTE)
            device->unique_queue_mask |= 1u << i;
    }

    return S_OK;

out_destroy_queues:
    d3d12_device_destroy_vkd3d_queues(device);
    return hr;
}

#define VKD3D_MAX_QUEUE_COUNT_PER_FAMILY (4u)

/* The queue priorities list contains VKD3D_MAX_QUEUE_COUNT_PER_FAMILY + 1 priorities
 * because it is possible for low latency to add an additional queue for out of band work
 * submission. */
static float queue_priorities[] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

static uint32_t vkd3d_find_queue(unsigned int count, const VkQueueFamilyProperties *properties,
        VkQueueFlags mask, VkQueueFlags flags)
{
    unsigned int i;

    for (i = 0; i < count; i++)
    {
        if ((properties[i].queueFlags & mask) == flags)
            return i;
    }

    return VK_QUEUE_FAMILY_IGNORED;
}

static HRESULT vkd3d_select_queues(const struct d3d12_device *device,
        VkPhysicalDevice physical_device, struct vkd3d_device_queue_info *info)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance->vk_procs;
    VkQueueFamilyProperties *queue_properties = NULL;
    VkDeviceQueueCreateInfo *queue_info = NULL;
    bool duplicate, single_queue;
    unsigned int i, j;
    uint32_t count;

    memset(info, 0, sizeof(*info));
    single_queue = !!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_SINGLE_QUEUE);

    VK_CALL(vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, NULL));
    if (!(queue_properties = vkd3d_calloc(count, sizeof(*queue_properties))))
        return E_OUTOFMEMORY;
    VK_CALL(vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, queue_properties));

    info->family_index[VKD3D_QUEUE_FAMILY_GRAPHICS] = vkd3d_find_queue(count, queue_properties,
            VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);

    info->family_index[VKD3D_QUEUE_FAMILY_COMPUTE] = vkd3d_find_queue(count, queue_properties,
            VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, VK_QUEUE_COMPUTE_BIT);

    /* Try to find a dedicated sparse queue family. We may use the sparse queue for initialization purposes,
     * and adding that kind of sync will be quite problematic since we get unintentional stalls, especially in graphics queue. */
    info->family_index[VKD3D_QUEUE_FAMILY_SPARSE_BINDING] = vkd3d_find_queue(count, queue_properties,
            VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT,
            VK_QUEUE_SPARSE_BINDING_BIT);

    /* Try to find a queue family that isn't graphics. */
    if (info->family_index[VKD3D_QUEUE_FAMILY_SPARSE_BINDING] == VK_QUEUE_FAMILY_IGNORED)
        info->family_index[VKD3D_QUEUE_FAMILY_SPARSE_BINDING] = vkd3d_find_queue(count, queue_properties,
                VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_SPARSE_BINDING_BIT, VK_QUEUE_SPARSE_BINDING_BIT);

    /* Last resort, pick any queue family that supports sparse. */
    if (info->family_index[VKD3D_QUEUE_FAMILY_SPARSE_BINDING] == VK_QUEUE_FAMILY_IGNORED)
        info->family_index[VKD3D_QUEUE_FAMILY_SPARSE_BINDING] = vkd3d_find_queue(count, queue_properties,
                VK_QUEUE_SPARSE_BINDING_BIT, VK_QUEUE_SPARSE_BINDING_BIT);

    if (device->vk_info.NV_optical_flow)
        info->family_index[VKD3D_QUEUE_FAMILY_OPTICAL_FLOW] = vkd3d_find_queue(count, queue_properties,
                VK_QUEUE_OPTICAL_FLOW_BIT_NV, VK_QUEUE_OPTICAL_FLOW_BIT_NV);

    if (info->family_index[VKD3D_QUEUE_FAMILY_COMPUTE] == VK_QUEUE_FAMILY_IGNORED)
        info->family_index[VKD3D_QUEUE_FAMILY_COMPUTE] = info->family_index[VKD3D_QUEUE_FAMILY_GRAPHICS];

    /* Vulkan transfer queue cannot represent some esoteric edge cases that D3D12 copy queue can. */
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_TRANSFER_QUEUE)
    {
        info->family_index[VKD3D_QUEUE_FAMILY_TRANSFER] = vkd3d_find_queue(count, queue_properties,
                VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, VK_QUEUE_TRANSFER_BIT);
    }
    else
    {
        info->family_index[VKD3D_QUEUE_FAMILY_TRANSFER] = info->family_index[VKD3D_QUEUE_FAMILY_COMPUTE];
    }

    /* Prefer compute queues for transfer. When using concurrent sharing, DMA queue tends to force compression off. */
    if (info->family_index[VKD3D_QUEUE_FAMILY_TRANSFER] == VK_QUEUE_FAMILY_IGNORED)
        info->family_index[VKD3D_QUEUE_FAMILY_TRANSFER] = info->family_index[VKD3D_QUEUE_FAMILY_COMPUTE];
    info->family_index[VKD3D_QUEUE_FAMILY_INTERNAL_COMPUTE] = info->family_index[VKD3D_QUEUE_FAMILY_COMPUTE];

    if (single_queue)
    {
        info->family_index[VKD3D_QUEUE_FAMILY_COMPUTE] = info->family_index[VKD3D_QUEUE_FAMILY_GRAPHICS];
        info->family_index[VKD3D_QUEUE_FAMILY_TRANSFER] = info->family_index[VKD3D_QUEUE_FAMILY_GRAPHICS];
    }

    for (i = 0; i < VKD3D_QUEUE_FAMILY_COUNT; ++i)
    {
        if (info->family_index[i] == VK_QUEUE_FAMILY_IGNORED)
            continue;

        info->vk_properties[i] = queue_properties[info->family_index[i]];

        /* Ensure that we don't create the same queue multiple times */
        duplicate = false;

        for (j = 0; j < i && !duplicate; j++)
            duplicate = info->family_index[i] == info->family_index[j];

        if (duplicate)
            continue;

        queue_info = &info->vk_queue_create_info[info->vk_family_count++];
        queue_info->sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info->pNext = NULL;
        queue_info->flags = 0;
        queue_info->queueFamilyIndex = info->family_index[i];
        queue_info->queueCount = min(info->vk_properties[i].queueCount, VKD3D_MAX_QUEUE_COUNT_PER_FAMILY);
        queue_info->pQueuePriorities = queue_priorities;

        if (single_queue)
            queue_info->queueCount = 1;

        if (device->vk_info.NV_low_latency2 && vkd3d_queue_family_needs_out_of_band_queue(i) &&
                queue_info->queueCount < info->vk_properties[i].queueCount)
            queue_info->queueCount++;
    }

    vkd3d_free(queue_properties);

    if (info->family_index[VKD3D_QUEUE_FAMILY_GRAPHICS] == VK_QUEUE_FAMILY_IGNORED)
    {
        ERR("Could not find a suitable queue family for a direct command queue.\n");
        return E_FAIL;
    }

    return S_OK;
}

static void d3d12_device_init_workarounds(struct d3d12_device *device)
{
    uint32_t major, minor, patch;

    /* IMRs should not have this workaround enabled or else perf will drop.
     * Technically, this isn't really a workaround as much as a speed hack on IMR. */
    switch (device->device_info.vulkan_1_2_properties.driverID)
    {
        case VK_DRIVER_ID_IMAGINATION_PROPRIETARY:
        case VK_DRIVER_ID_QUALCOMM_PROPRIETARY:
        case VK_DRIVER_ID_ARM_PROPRIETARY:
        case VK_DRIVER_ID_BROADCOM_PROPRIETARY:
        case VK_DRIVER_ID_MESA_TURNIP:
        case VK_DRIVER_ID_MESA_V3DV:
        case VK_DRIVER_ID_MESA_PANVK:
        case VK_DRIVER_ID_SAMSUNG_PROPRIETARY:
        case VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA:
        case VK_DRIVER_ID_MESA_HONEYKRISP:
            device->workarounds.tiler_renderpass_barriers = true;
            break;
        /* layered implementations are handled transparently */
        case VK_DRIVER_ID_MOLTENVK:
        case VK_DRIVER_ID_JUICE_PROPRIETARY:
        case VK_DRIVER_ID_MESA_VENUS:
        case VK_DRIVER_ID_MESA_DOZEN:
        case VK_DRIVER_ID_VULKAN_SC_EMULATION_ON_VULKAN:
        default:
            break;
    }

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_SKIP_DRIVER_WORKAROUNDS)
        return;

    if (device->device_info.properties2.properties.vendorID == 0x1002)
    {
        if (vkd3d_get_linux_kernel_version(&major, &minor, &patch))
        {
            /* 6.10 amdgpu kernel changes the clear vram code to do background clears instead
             * of on-demand clearing. This seems to have bugs, and we have been able to observe
             * non-zeroed VRAM coming from the affected kernels.
             * This workaround needs to be in place until we have confirmed a fix in upstream kernel. */
            INFO("Detected Linux kernel version %u.%u.%u\n", major, minor, patch);

            if (major > 6 || (major == 6 && minor >= 10))
            {
                INFO("AMDGPU broken kernel detected. Enabling manual memory clearing path.\n");
                device->workarounds.amdgpu_broken_clearvram = true;
            }
        }

        /* AMDGPU seems to have a strange bug where remapping a page to NULL can cause an impossible
         * page table issue where it's now possible to fault on a PRT page.
         * It's unknown which kernel version introduced it and when it will be fixed.
         * Only seems to affect very specific content which does not really rely on the NULL page behavior anyway. */
        device->workarounds.amdgpu_broken_null_tile_mapping = true;
    }
}

static HRESULT vkd3d_create_vk_device(struct d3d12_device *device,
        const struct vkd3d_device_create_info *create_info)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance->vk_procs;
    struct vkd3d_device_queue_info device_queue_info;
    VkPhysicalDeviceProperties device_properties;
    bool *user_extension_supported = NULL;
    VkPhysicalDevice physical_device;
    VkDeviceCreateInfo device_info;
    unsigned int device_index;
    uint32_t extension_count;
    const char **extensions;
    VkDevice vk_device;
    VkResult vr;
    HRESULT hr;

    TRACE("device %p, create_info %p.\n", device, create_info);

    physical_device = create_info->vk_physical_device;
    device_index = vkd3d_env_var_as_uint("VKD3D_VULKAN_DEVICE", ~0u);
    if ((!physical_device || device_index != ~0u)
            && FAILED(hr = vkd3d_select_physical_device(device->vkd3d_instance, device_index, &physical_device)))
        return hr;

    device->vk_physical_device = physical_device;

    VK_CALL(vkGetPhysicalDeviceProperties(device->vk_physical_device, &device_properties));
    device->api_version = min(device_properties.apiVersion, VKD3D_MAX_API_VERSION);

    VK_CALL(vkGetPhysicalDeviceMemoryProperties(physical_device, &device->memory_properties));

    if (create_info->optional_device_extension_count)
    {
        if (!(user_extension_supported = vkd3d_calloc(create_info->optional_device_extension_count, sizeof(bool))))
            return E_OUTOFMEMORY;
    }

    if (FAILED(hr = vkd3d_init_device_extensions(device, create_info,
            &extension_count, user_extension_supported)))
    {
        vkd3d_free(user_extension_supported);
        return hr;
    }

    /* Mark any user extensions that might be of use to us.
     * Need to do this here so that we can pass down PDF2 as necessary. */
    vkd3d_mark_enabled_user_extensions(&device->vk_info,
            create_info->optional_device_extensions,
            create_info->optional_device_extension_count,
            user_extension_supported);

    vkd3d_physical_device_info_init(&device->device_info, device);
    vkd3d_physical_device_info_apply_workarounds(&device->device_info, device);

    if (FAILED(hr = vkd3d_init_device_caps(device, create_info, &device->device_info)))
    {
        vkd3d_free(user_extension_supported);
        return hr;
    }

    if (!(extensions = vkd3d_calloc(extension_count, sizeof(*extensions))))
    {
        vkd3d_free(user_extension_supported);
        return E_OUTOFMEMORY;
    }

    if (FAILED(hr = vkd3d_select_queues(device, physical_device, &device_queue_info)))
    {
        vkd3d_free(user_extension_supported);
        vkd3d_free(extensions);
        return hr;
    }

    TRACE("Using queue family %u for direct command queues.\n",
            device_queue_info.family_index[VKD3D_QUEUE_FAMILY_GRAPHICS]);
    TRACE("Using queue family %u for compute command queues.\n",
            device_queue_info.family_index[VKD3D_QUEUE_FAMILY_COMPUTE]);
    TRACE("Using queue family %u for copy command queues.\n",
            device_queue_info.family_index[VKD3D_QUEUE_FAMILY_TRANSFER]);
    TRACE("Using queue family %u for sparse binding.\n",
            device_queue_info.family_index[VKD3D_QUEUE_FAMILY_SPARSE_BINDING]);
    TRACE("Using queue family %u for optical flow.\n",
            device_queue_info.family_index[VKD3D_QUEUE_FAMILY_OPTICAL_FLOW]);

    /* Create device */
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = device->device_info.features2.pNext;
    device_info.flags = 0;
    device_info.queueCreateInfoCount = device_queue_info.vk_family_count;
    device_info.pQueueCreateInfos = device_queue_info.vk_queue_create_info;
    device_info.enabledLayerCount = 0;
    device_info.ppEnabledLayerNames = NULL;
    device_info.enabledExtensionCount = vkd3d_enable_extensions(extensions, NULL, 0,
            optional_device_extensions, ARRAY_SIZE(optional_device_extensions),
            create_info->device_extensions,
            create_info->device_extension_count,
            create_info->optional_device_extensions,
            create_info->optional_device_extension_count,
            user_extension_supported, &device->vk_info);
    device_info.ppEnabledExtensionNames = extensions;
    device_info.pEnabledFeatures = &device->device_info.features2.features;
    vkd3d_free(user_extension_supported);

    vr = VK_CALL(vkCreateDevice(physical_device, &device_info, NULL, &vk_device));
    if (vr == VK_ERROR_INITIALIZATION_FAILED &&
            vkd3d_disable_nvx_extensions(device, extensions, &device_info.enabledExtensionCount))
    {
        WARN("Disabled extensions that can cause Vulkan device creation to fail, retrying.\n");
        vr = VK_CALL(vkCreateDevice(physical_device, &device_info, NULL, &vk_device));
    }

    if (vr < 0)
    {
        ERR("Failed to create Vulkan device, vr %d.\n", vr);
        vkd3d_free((void *)extensions);
        return hresult_from_vk_result(vr);
    }

    if (FAILED(hr = vkd3d_load_vk_device_procs(&device->vk_procs, vk_procs, vk_device)))
    {
        ERR("Failed to load device procs, hr %#x.\n", hr);
        if (device->vk_procs.vkDestroyDevice)
            device->vk_procs.vkDestroyDevice(vk_device, NULL);
        return hr;
    }

    device->vk_device = vk_device;

    if (FAILED(hr = d3d12_device_create_vkd3d_queues(device, &device_queue_info)))
    {
        ERR("Failed to create queues, hr %#x.\n", hr);
        device->vk_procs.vkDestroyDevice(vk_device, NULL);
        return hr;
    }

    device->vk_info.extension_count = device_info.enabledExtensionCount;
    device->vk_info.extension_names = extensions;

    d3d12_device_init_workarounds(device);

    TRACE("Created Vulkan device %p.\n", vk_device);

    return hr;
}

struct d3d12_device_singleton
{
    struct list entry;
    LUID adapter_luid;
    struct d3d12_device *device;
};

static pthread_mutex_t d3d12_device_map_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct list d3d12_device_map = LIST_INIT(d3d12_device_map);

static struct d3d12_device *d3d12_find_device_singleton(LUID luid)
{
    struct d3d12_device_singleton* current;

    LIST_FOR_EACH_ENTRY(current, &d3d12_device_map, struct d3d12_device_singleton, entry)
    {
        if (!memcmp(&current->adapter_luid, &luid, sizeof(LUID)))
            return current->device;
    }

    return NULL;
}

static void d3d12_add_device_singleton(struct d3d12_device *device, LUID luid)
{
    struct d3d12_device_singleton *e;

    if (!(e = vkd3d_malloc(sizeof(*e))))
    {
        ERR("Failed to register device singleton for adapter.\n");
        return;
    }
    e->adapter_luid = luid;
    e->device = device;

    list_add_tail(&d3d12_device_map, &e->entry);
}

static void d3d12_remove_device_singleton(LUID luid)
{
    struct d3d12_device_singleton *current;

    LIST_FOR_EACH_ENTRY(current, &d3d12_device_map, struct d3d12_device_singleton, entry)
    {
        if (!memcmp(&current->adapter_luid, &luid, sizeof(LUID)))
        {
            list_remove(&current->entry);
            vkd3d_free(current);
            return;
        }
    }
}

static HRESULT d3d12_device_create_scratch_buffer(struct d3d12_device *device, enum vkd3d_scratch_pool_kind kind,
        VkDeviceSize size, uint32_t memory_types, struct vkd3d_scratch_buffer *scratch)
{
    HRESULT hr;

    TRACE("device %p, size %llu, scratch %p.\n", device, (unsigned long long)size, scratch);

    if (kind == VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE)
    {
        struct vkd3d_allocate_heap_memory_info alloc_info;

        /* We only care about memory types for INDIRECT_PREPROCESS. */
        assert(memory_types == ~0u);

        memset(&alloc_info, 0, sizeof(alloc_info));
        alloc_info.heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        alloc_info.heap_desc.SizeInBytes = size;
        alloc_info.heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        alloc_info.heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
        alloc_info.extra_allocation_flags = VKD3D_ALLOCATION_FLAG_INTERNAL_SCRATCH;
        alloc_info.vk_memory_priority = vkd3d_convert_to_vk_prio(D3D12_RESIDENCY_PRIORITY_NORMAL);

        if (FAILED(hr = vkd3d_allocate_heap_memory(device, &device->memory_allocator,
                &alloc_info, &scratch->allocation)))
            return hr;
    }
    else if (kind == VKD3D_SCRATCH_POOL_KIND_INDIRECT_PREPROCESS)
    {
        struct vkd3d_allocate_memory_info alloc_info;
        memset(&alloc_info, 0, sizeof(alloc_info));

        alloc_info.heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        alloc_info.memory_requirements.size = size;
        alloc_info.memory_requirements.memoryTypeBits = memory_types;
        alloc_info.memory_requirements.alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        alloc_info.heap_flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
        alloc_info.flags = VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER | VKD3D_ALLOCATION_FLAG_INTERNAL_SCRATCH;
        alloc_info.vk_memory_priority = vkd3d_convert_to_vk_prio(D3D12_RESIDENCY_PRIORITY_NORMAL);
        if (device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands)
        {
            /* this flag cannot be used with the existing buffer heaps */
            alloc_info.explicit_global_buffer_usage =
                    VK_BUFFER_USAGE_2_PREPROCESS_BUFFER_BIT_EXT |
                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            /* Need this to override any type flags provided by heap properties.
             * BUFFER_USAGE_2_PREPROCESS_BUFFER implies 32-bit only types. */
            alloc_info.flags |= VKD3D_ALLOCATION_FLAG_DEDICATED;
        }

        if (FAILED(hr = vkd3d_allocate_memory(device, &device->memory_allocator,
                &alloc_info, &scratch->allocation)))
            return hr;
    }
    else if (kind == VKD3D_SCRATCH_POOL_KIND_UNIFORM_UPLOAD)
    {
        struct vkd3d_allocate_heap_memory_info alloc_info;

        /* We only care about memory types for INDIRECT_PREPROCESS. */
        assert(memory_types == ~0u);

        memset(&alloc_info, 0, sizeof(alloc_info));
        alloc_info.heap_desc.Properties.Type = device->memory_info.has_gpu_upload_heap ?
                D3D12_HEAP_TYPE_GPU_UPLOAD : D3D12_HEAP_TYPE_UPLOAD;
        alloc_info.heap_desc.SizeInBytes = size;
        alloc_info.heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        alloc_info.heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
        alloc_info.extra_allocation_flags = VKD3D_ALLOCATION_FLAG_INTERNAL_SCRATCH;
        alloc_info.vk_memory_priority = vkd3d_convert_to_vk_prio(D3D12_RESIDENCY_PRIORITY_NORMAL);

        if (FAILED(hr = vkd3d_allocate_heap_memory(device, &device->memory_allocator,
                &alloc_info, &scratch->allocation)))
            return hr;
    }
    else
    {
        return E_INVALIDARG;
    }

    scratch->offset = 0;
    return S_OK;
}

static void d3d12_device_destroy_scratch_buffer(struct d3d12_device *device, const struct vkd3d_scratch_buffer *scratch)
{
    TRACE("device %p, scratch %p.\n", device, scratch);

    vkd3d_free_memory(device, &device->memory_allocator, &scratch->allocation);
}

HRESULT d3d12_device_get_scratch_buffer(struct d3d12_device *device, enum vkd3d_scratch_pool_kind kind,
        VkDeviceSize min_size, uint32_t memory_types, struct vkd3d_scratch_buffer *scratch)
{
    struct d3d12_device_scratch_pool *pool = &device->scratch_pools[kind];
    struct vkd3d_scratch_buffer *candidate;
    size_t i;

    if (min_size > pool->block_size)
    {
        FIXME("Requesting scratch buffer kind %u larger than limit (%"PRIu64" > %"PRIu64"). Expect bad performance.\n",
                kind, min_size, pool->block_size);
        return d3d12_device_create_scratch_buffer(device, kind, min_size, memory_types, scratch);
    }

    pthread_mutex_lock(&device->mutex);

    for (i = pool->scratch_buffer_count; i; i--)
    {
        candidate = &pool->scratch_buffers[i - 1];

        /* Extremely unlikely to fail since we have separate lists per pool kind, but to be 100% correct ... */
        if (memory_types & (1u << candidate->allocation.device_allocation.vk_memory_type))
        {
            *scratch = *candidate;
            scratch->offset = 0;
            pool->scratch_buffers[i - 1] = pool->scratch_buffers[--pool->scratch_buffer_count];
            pthread_mutex_unlock(&device->mutex);
            return S_OK;
        }
    }

    pthread_mutex_unlock(&device->mutex);
    return d3d12_device_create_scratch_buffer(device, kind, pool->block_size, memory_types, scratch);
}

void d3d12_device_return_scratch_buffer(struct d3d12_device *device, enum vkd3d_scratch_pool_kind kind,
        const struct vkd3d_scratch_buffer *scratch)
{
    struct d3d12_device_scratch_pool *pool = &device->scratch_pools[kind];
    pthread_mutex_lock(&device->mutex);

    if (scratch->allocation.resource.size == pool->block_size &&
            pool->scratch_buffer_count < pool->scratch_buffer_size)
    {
        pool->scratch_buffers[pool->scratch_buffer_count++] = *scratch;
        if (pool->scratch_buffer_count > pool->high_water_mark)
        {
            pool->high_water_mark = pool->scratch_buffer_count;

            /* Warn if we're starting to fill up. Potential performance issue afoot. */
            if (pool->high_water_mark > pool->scratch_buffer_size / 2)
            {
                WARN("New high water mark: %u scratch buffers in flight for kind %u (%"PRIu64" bytes).\n",
                        pool->high_water_mark, kind, pool->high_water_mark * pool->block_size);
            }
        }
        pthread_mutex_unlock(&device->mutex);
    }
    else
    {
        pthread_mutex_unlock(&device->mutex);
        d3d12_device_destroy_scratch_buffer(device, scratch);
        WARN("Too many scratch buffers in flight, cannot recycle kind %u.\n", kind);
    }
}

uint64_t d3d12_device_get_descriptor_heap_gpu_va(struct d3d12_device *device, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    uint64_t va;

    /* The virtual GPU descriptor VAs are of form (unique << 32) | (desc index * sizeof(d3d12_desc)),
     * which simplifies local root signature tables.
     * Also simplifies SetRootDescriptorTable since we can deduce offset without memory lookups. */

    pthread_mutex_lock(&device->mutex);
    if (device->descriptor_heap_gpu_va_count)
        va = device->descriptor_heap_gpu_vas[--device->descriptor_heap_gpu_va_count];
    else
        va = ++device->descriptor_heap_gpu_next;
    pthread_mutex_unlock(&device->mutex);
    va <<= 32;

    if (d3d12_device_use_embedded_mutable_descriptors(device))
    {
        /* Encodes what type this heap is so that we can decode VA to offset properly later.
         * When using embedded descriptors we cannot assume that the descriptor increment
         * is the same for CBV_SRV_UAV and sampler anymore. */
        va <<= 1;
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
            va |= VKD3D_RESOURCE_EMBEDDED_RESOURCE_HEAP_MASK;
    }

    return va;
}

void d3d12_device_return_descriptor_heap_gpu_va(struct d3d12_device *device, uint64_t va)
{
    /* Fixup the magic shift we used when allocating. */
    if (d3d12_device_use_embedded_mutable_descriptors(device))
        va >>= 1;

    pthread_mutex_lock(&device->mutex);
    vkd3d_array_reserve((void **)&device->descriptor_heap_gpu_vas, &device->descriptor_heap_gpu_va_size,
            device->descriptor_heap_gpu_va_count + 1, sizeof(*device->descriptor_heap_gpu_vas));
    device->descriptor_heap_gpu_vas[device->descriptor_heap_gpu_va_count++] = (uint32_t)(va >> 32);
    pthread_mutex_unlock(&device->mutex);
}

static HRESULT d3d12_device_create_query_pool(struct d3d12_device *device, uint32_t type_index, struct vkd3d_query_pool *pool)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkQueryPoolCreateInfo pool_info;
    VkResult vr;

    TRACE("device %p, type_index %u, pool %p.\n", device, type_index, pool);

    pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    pool_info.pNext = NULL;
    pool_info.flags = 0;
    pool_info.pipelineStatistics = 0;

    switch (type_index)
    {
        case VKD3D_QUERY_TYPE_INDEX_OCCLUSION:
            /* Expect a large number of occlusion queries
             * to be used within a single command list */
            pool_info.queryType = VK_QUERY_TYPE_OCCLUSION;
            pool_info.queryCount = 4096;
            break;

        case VKD3D_QUERY_TYPE_INDEX_PIPELINE_STATISTICS:
            pool_info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
            pool_info.queryCount = 128;
            pool_info.pipelineStatistics =
                    VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT |
                    VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
            break;

        case VKD3D_QUERY_TYPE_INDEX_TRANSFORM_FEEDBACK:
            pool_info.queryType = VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;
            pool_info.queryCount = 128;
            break;

        case VKD3D_QUERY_TYPE_INDEX_RT_COMPACTED_SIZE:
            pool_info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
            pool_info.queryCount = 128;
            break;

        case VKD3D_QUERY_TYPE_INDEX_RT_CURRENT_SIZE:
            pool_info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SIZE_KHR;
            pool_info.queryCount = 128;
            break;

        case VKD3D_QUERY_TYPE_INDEX_RT_SERIALIZE_SIZE:
            pool_info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR;
            pool_info.queryCount = 128;
            break;

        case VKD3D_QUERY_TYPE_INDEX_RT_SERIALIZE_SIZE_BOTTOM_LEVEL_POINTERS:
            pool_info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_BOTTOM_LEVEL_POINTERS_KHR;
            pool_info.queryCount = 128;
            break;

        case VKD3D_QUERY_TYPE_INDEX_OMM_COMPACTED_SIZE:
            pool_info.queryType = VK_QUERY_TYPE_MICROMAP_COMPACTED_SIZE_EXT;
            pool_info.queryCount = 128;
            break;

        case VKD3D_QUERY_TYPE_INDEX_OMM_SERIALIZE_SIZE:
            pool_info.queryType = VK_QUERY_TYPE_MICROMAP_SERIALIZATION_SIZE_EXT;
            pool_info.queryCount = 128;
            break;

        default:
            ERR("Unhandled query type %u.\n", type_index);
            return E_INVALIDARG;
    }

    if ((vr = VK_CALL(vkCreateQueryPool(device->vk_device, &pool_info, NULL, &pool->vk_query_pool))) < 0)
    {
        ERR("Failed to create query pool, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    pool->type_index = type_index;
    pool->query_count = pool_info.queryCount;
    pool->next_index = 0;
    return S_OK;
}

static void d3d12_device_destroy_query_pool(struct d3d12_device *device, const struct vkd3d_query_pool *pool)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    TRACE("device %p, pool %p.\n", device, pool);

    VK_CALL(vkDestroyQueryPool(device->vk_device, pool->vk_query_pool, NULL));
}

HRESULT d3d12_device_get_query_pool(struct d3d12_device *device, uint32_t type_index, struct vkd3d_query_pool *pool)
{
    size_t i;

    pthread_mutex_lock(&device->mutex);

    for (i = 0; i < device->query_pool_count; i++)
    {
        if (device->query_pools[i].type_index == type_index)
        {
            *pool = device->query_pools[i];
            pool->next_index = 0;
            if (--device->query_pool_count != i)
                device->query_pools[i] = device->query_pools[device->query_pool_count];
            pthread_mutex_unlock(&device->mutex);
            return S_OK;
        }
    }

    pthread_mutex_unlock(&device->mutex);
    return d3d12_device_create_query_pool(device, type_index, pool);
}

void d3d12_device_return_query_pool(struct d3d12_device *device, const struct vkd3d_query_pool *pool)
{
    pthread_mutex_lock(&device->mutex);

    if (device->query_pool_count < VKD3D_VIRTUAL_QUERY_POOL_COUNT)
    {
        device->query_pools[device->query_pool_count++] = *pool;
        pthread_mutex_unlock(&device->mutex);
    }
    else
    {
        pthread_mutex_unlock(&device->mutex);
        d3d12_device_destroy_query_pool(device, pool);
    }
}

/* ID3D12Device */
extern ULONG STDMETHODCALLTYPE d3d12_device_vkd3d_ext_AddRef(d3d12_device_vkd3d_ext_iface *iface);
extern ULONG STDMETHODCALLTYPE d3d12_dxvk_interop_device_AddRef(d3d12_dxvk_interop_device_iface *iface);
extern ULONG STDMETHODCALLTYPE d3d12_low_latency_device_AddRef(ID3DLowLatencyDevice *iface);

HRESULT STDMETHODCALLTYPE d3d12_device_QueryInterface(d3d12_device_iface *iface,
        REFIID riid, void **object)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(riid, &IID_ID3D12Device)
            || IsEqualGUID(riid, &IID_ID3D12Device1)
            || IsEqualGUID(riid, &IID_ID3D12Device2)
            || IsEqualGUID(riid, &IID_ID3D12Device3)
            || IsEqualGUID(riid, &IID_ID3D12Device4)
            || IsEqualGUID(riid, &IID_ID3D12Device5)
            || IsEqualGUID(riid, &IID_ID3D12Device6)
            || IsEqualGUID(riid, &IID_ID3D12Device7)
            || IsEqualGUID(riid, &IID_ID3D12Device8)
            || IsEqualGUID(riid, &IID_ID3D12Device9)
            || IsEqualGUID(riid, &IID_ID3D12Device10)
            || IsEqualGUID(riid, &IID_ID3D12Device11)
            || IsEqualGUID(riid, &IID_ID3D12Device12)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12Device12_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3D12DeviceExt)
            || IsEqualGUID(riid, &IID_ID3D12DeviceExt1))
    {
        d3d12_device_vkd3d_ext_AddRef(&device->ID3D12DeviceExt_iface);
        *object = &device->ID3D12DeviceExt_iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3D12DXVKInteropDevice)
            || IsEqualGUID(riid, &IID_ID3D12DXVKInteropDevice1))
    {
        d3d12_dxvk_interop_device_AddRef(&device->ID3D12DXVKInteropDevice_iface);
        *object = &device->ID3D12DXVKInteropDevice_iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3DLowLatencyDevice))
    {
        d3d12_low_latency_device_AddRef(&device->ID3DLowLatencyDevice_iface);
        *object = &device->ID3DLowLatencyDevice_iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&device->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &device->destruction_notifier.ID3DDestructionNotifier_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static void d3d12_device_destroy(struct d3d12_device *device);

/* Split up the refcounting from logging so we can more easily deduce if ref-counting bugs
 * are caused by internal issues or external ones. */
ULONG d3d12_device_add_ref_common(struct d3d12_device *device)
{
    return InterlockedIncrement(&device->refcount);
}

ULONG d3d12_device_release_common(struct d3d12_device *device)
{
    ULONG cur_refcount, cas_refcount;
    bool is_locked = false;

    cur_refcount = 0;
    cas_refcount = vkd3d_atomic_uint32_load_explicit(&device->refcount, vkd3d_memory_order_relaxed);

    /* In order to prevent another thread from resurrecting a destroyed device,
     * we need to lock the container mutex before decrementing the ref count to
     * zero, so we'll have to use a CAS loop rather than basic atomic decrement */
    while (cas_refcount != cur_refcount)
    {
        cur_refcount = cas_refcount;

        if (cur_refcount == 1 && !is_locked)
        {
            pthread_mutex_lock(&d3d12_device_map_mutex);
            is_locked = true;
        }

        cas_refcount = vkd3d_atomic_uint32_compare_exchange((uint32_t*)&device->refcount, cur_refcount,
                cur_refcount - 1, vkd3d_memory_order_acq_rel, vkd3d_memory_order_relaxed);
    }

    if (cur_refcount == 1)
    {
        d3d12_remove_device_singleton(device->adapter_luid);
        d3d12_device_destroy(device);
        vkd3d_free_aligned(device);
    }

    if (is_locked)
        pthread_mutex_unlock(&d3d12_device_map_mutex);

    return cur_refcount - 1;
}

static ULONG STDMETHODCALLTYPE d3d12_device_AddRef(d3d12_device_iface *iface)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    ULONG refcount = d3d12_device_add_ref_common(device);
    TRACE("%p increasing refcount to %u.\n", device, refcount);
    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_device_Release(d3d12_device_iface *iface)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    UINT refcount = d3d12_device_release_common(device);
    TRACE("%p decreasing refcount to %u.\n", device, refcount);
    return refcount;
}

static void d3d12_device_free_pipeline_libraries(struct d3d12_device *device)
{
    hash_map_iter(&device->vertex_input_pipelines, vkd3d_vertex_input_pipeline_free, device);
    hash_map_free(&device->vertex_input_pipelines);

    hash_map_iter(&device->fragment_output_pipelines, vkd3d_fragment_output_pipeline_free, device);
    hash_map_free(&device->fragment_output_pipelines);
}

static void vkd3d_null_rtas_allocation_cleanup(
        struct vkd3d_null_rtas_allocation *rtas, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    if (rtas->va)
    {
        VK_CALL(vkDestroyAccelerationStructureKHR(device->vk_device, rtas->rtas, NULL));
        VK_CALL(vkDestroyBuffer(device->vk_device, rtas->buffer, NULL));
        vkd3d_free_device_memory(device, &rtas->alloc);
    }
}

static void d3d12_device_destroy(struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    size_t i, j;

    d3d_destruction_notifier_free(&device->destruction_notifier);

    if (device->internal_sparse_queue)
        d3d12_device_unmap_vkd3d_queue(device->internal_sparse_queue, NULL);

    for (i = 0; i < VKD3D_SCRATCH_POOL_KIND_COUNT; i++)
        for (j = 0; j < device->scratch_pools[i].scratch_buffer_count; j++)
            d3d12_device_destroy_scratch_buffer(device, &device->scratch_pools[i].scratch_buffers[j]);

    for (i = 0; i < device->query_pool_count; i++)
        d3d12_device_destroy_query_pool(device, &device->query_pools[i]);

    for (i = 0; i < device->cached_command_allocator_count; i++)
        VK_CALL(vkDestroyCommandPool(device->vk_device, device->cached_command_allocators[i].vk_command_pool, NULL));

    vkd3d_free(device->descriptor_heap_gpu_vas);

    vkd3d_private_store_destroy(&device->private_store);

    vkd3d_cleanup_format_info(device);
    vkd3d_memory_info_cleanup(&device->memory_info, device);
    vkd3d_address_binding_tracker_cleanup(&device->address_binding_tracker, device);
    vkd3d_queue_timeline_trace_cleanup(&device->queue_timeline_trace);
    vkd3d_shader_debug_ring_cleanup(&device->debug_ring, device);
#ifdef VKD3D_ENABLE_BREADCRUMBS
    vkd3d_breadcrumb_tracer_cleanup_barrier_hashes(&device->breadcrumb_tracer);
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
        vkd3d_breadcrumb_tracer_cleanup(&device->breadcrumb_tracer, device);
#endif
    vkd3d_pipeline_library_flush_disk_cache(&device->disk_cache);
    vkd3d_sampler_state_cleanup(&device->sampler_state, device);
    vkd3d_view_map_destroy(&device->sampler_map, device);
    vkd3d_meta_ops_cleanup(&device->meta_ops, device);
    vkd3d_bindless_state_cleanup(&device->bindless_state, device);
    d3d12_device_destroy_vkd3d_queues(device);
    VK_CALL(vkDestroySemaphore(device->vk_device, device->sparse_init_timeline, NULL));
    vkd3d_null_rtas_allocation_cleanup(&device->null_rtas_allocation, device);
    vkd3d_memory_allocator_cleanup(&device->memory_allocator, device);
    vkd3d_memory_transfer_queue_cleanup(&device->memory_transfers);
    vkd3d_global_descriptor_buffer_cleanup(&device->global_descriptor_buffer, device);
    d3d12_device_free_pipeline_libraries(device);
    /* Tear down descriptor global info late, so we catch last minute faults after we drain the queues. */
    vkd3d_descriptor_debug_free_global_info(device->descriptor_qa_global_info, device);

#ifdef VKD3D_ENABLE_RENDERDOC
    if (vkd3d_renderdoc_active() && vkd3d_renderdoc_global_capture_enabled())
        vkd3d_renderdoc_end_capture(device->vkd3d_instance->vk_instance);
#endif

    vkd3d_free((void *)device->vk_info.extension_names);
    VK_CALL(vkDestroyDevice(device->vk_device, NULL));
    rwlock_destroy(&device->fragment_output_lock);
    rwlock_destroy(&device->vertex_input_lock);
    pthread_mutex_destroy(&device->mutex);
    pthread_mutex_destroy(&device->global_submission_mutex);
    if (device->parent)
        IUnknown_Release(device->parent);
    vkd3d_instance_decref(device->vkd3d_instance);
}

static void d3d12_device_set_name(struct d3d12_device *device, const char *name)
{
    vkd3d_set_vk_object_name(device, (uint64_t)(uintptr_t)device->vk_device,
            VK_OBJECT_TYPE_DEVICE, name);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_GetPrivateData(d3d12_device_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n",
            iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&device->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetPrivateData(d3d12_device_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n",
            iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&device->private_store, guid, data_size, data,
            (vkd3d_set_name_callback) d3d12_device_set_name, device);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetPrivateDataInterface(d3d12_device_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&device->private_store, guid, data,
            (vkd3d_set_name_callback) d3d12_device_set_name, device);
}

static UINT STDMETHODCALLTYPE d3d12_device_GetNodeCount(d3d12_device_iface *iface)
{
    TRACE("iface %p.\n", iface);

    return 1;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandQueue(d3d12_device_iface *iface,
        const D3D12_COMMAND_QUEUE_DESC *desc, REFIID riid, void **command_queue)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_command_queue *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, riid %s, command_queue %p.\n",
            iface, desc, debugstr_guid(riid), command_queue);

    if (FAILED(hr = d3d12_command_queue_create(device, desc, VK_QUEUE_FAMILY_IGNORED, &object)))
        return hr;

    return return_interface(&object->ID3D12CommandQueue_iface, &IID_ID3D12CommandQueue,
            riid, command_queue);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandAllocator(d3d12_device_iface *iface,
        D3D12_COMMAND_LIST_TYPE type, REFIID riid, void **command_allocator)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    ID3D12CommandAllocator *allocator_iface;
    HRESULT hr;

    TRACE("iface %p, type %#x, riid %s, command_allocator %p.\n",
            iface, type, debugstr_guid(riid), command_allocator);

    if (type == D3D12_COMMAND_LIST_TYPE_BUNDLE)
    {
        struct d3d12_bundle_allocator *object;
        if (FAILED(hr = d3d12_bundle_allocator_create(device, &object)))
            return hr;
        allocator_iface = &object->ID3D12CommandAllocator_iface;
    }
    else
    {
        struct d3d12_command_allocator *object;
        if (FAILED(hr = d3d12_command_allocator_create(device, type, VK_QUEUE_FAMILY_IGNORED, &object)))
            return hr;
        allocator_iface = &object->ID3D12CommandAllocator_iface;
    }

    return return_interface(allocator_iface, &IID_ID3D12CommandAllocator, riid, command_allocator);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateGraphicsPipelineState(d3d12_device_iface *iface,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, REFIID riid, void **pipeline_state)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_pipeline_state_desc pipeline_desc;
    struct d3d12_pipeline_state *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, riid %s, pipeline_state %p.\n",
            iface, desc, debugstr_guid(riid), pipeline_state);

    if (FAILED(hr = vkd3d_pipeline_state_desc_from_d3d12_graphics_desc(&pipeline_desc, desc)))
        return hr;

    if (FAILED(hr = d3d12_pipeline_state_create(device,
            VK_PIPELINE_BIND_POINT_GRAPHICS, &pipeline_desc, &object)))
        return hr;

    return return_interface(&object->ID3D12PipelineState_iface,
            &IID_ID3D12PipelineState, riid, pipeline_state);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateComputePipelineState(d3d12_device_iface *iface,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, REFIID riid, void **pipeline_state)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_pipeline_state_desc pipeline_desc;
    struct d3d12_pipeline_state *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, riid %s, pipeline_state %p.\n",
            iface, desc, debugstr_guid(riid), pipeline_state);

    if (FAILED(hr = vkd3d_pipeline_state_desc_from_d3d12_compute_desc(&pipeline_desc, desc)))
        return hr;

    if (FAILED(hr = d3d12_pipeline_state_create(device,
            VK_PIPELINE_BIND_POINT_COMPUTE, &pipeline_desc, &object)))
        return hr;

    return return_interface(&object->ID3D12PipelineState_iface,
            &IID_ID3D12PipelineState, riid, pipeline_state);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandList1(d3d12_device_iface *iface,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS flags,
        REFIID riid, void **command_list);

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandList(d3d12_device_iface *iface,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *command_allocator,
        ID3D12PipelineState *initial_pipeline_state, REFIID riid, void **command_list)
{
    ID3D12GraphicsCommandList *object;
    HRESULT hr;

    TRACE("iface %p, node_mask 0x%08x, type %#x, command_allocator %p, "
            "initial_pipeline_state %p, riid %s, command_list %p.\n",
            iface, node_mask, type, command_allocator,
            initial_pipeline_state, debugstr_guid(riid), command_list);

    if (FAILED(hr = d3d12_device_CreateCommandList1(iface, node_mask, type,
            D3D12_COMMAND_LIST_FLAG_NONE, &IID_ID3D12GraphicsCommandList, (void **)&object)))
        return hr;

    if (FAILED(hr = ID3D12GraphicsCommandList_Reset(object, command_allocator, initial_pipeline_state)))
    {
        ID3D12GraphicsCommandList_Release(object);
        return hr;
    }

    return return_interface(object, &IID_ID3D12GraphicsCommandList, riid, command_list);
}

static void vkd3d_determine_format_support_for_feature_level(const struct d3d12_device *device,
        D3D12_FEATURE_DATA_FORMAT_SUPPORT *format_support)
{
    /* TypedUAVLoadAdditionalFormats is an all or nothing set */
    if (!device->d3d12_caps.options.TypedUAVLoadAdditionalFormats)
        format_support->Support2 &= ~D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;

    /* R32_{UINT, SINT, FLOAT} always have to support typed reads */
    if (format_support->Format == DXGI_FORMAT_R32_UINT ||
                format_support->Format == DXGI_FORMAT_R32_SINT ||
                format_support->Format == DXGI_FORMAT_R32_FLOAT)
    {
        format_support->Support2 |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
    }

    /* Do not report TILED if tiled resource support is disabled */
    if (!device->d3d12_caps.options.TiledResourcesTier)
        format_support->Support2 &= ~D3D12_FORMAT_SUPPORT2_TILED;
}

static HRESULT d3d12_device_check_multisample_quality_levels(struct d3d12_device *device,
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS *data)
{
    const struct vkd3d_format *format;
    VkSampleCountFlagBits vk_samples;
    VkSampleCountFlags sample_counts;

    TRACE("Format %#x, sample count %u, flags %#x.\n", data->Format, data->SampleCount, data->Flags);

    data->NumQualityLevels = 0;

    if (!(vk_samples = vk_samples_from_sample_count(data->SampleCount)))
        WARN("Invalid sample count %u.\n", data->SampleCount);

    if (!data->SampleCount)
        return E_FAIL;

    if (data->SampleCount == 1)
    {
        data->NumQualityLevels = 1;
        goto done;
    }

    if (data->Format == DXGI_FORMAT_UNKNOWN ||
            data->Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE ||
            data->Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE)
        goto done;

    if (!(format = vkd3d_get_format(device, data->Format, false)))
        format = vkd3d_get_format(device, data->Format, true);

    if (!format)
    {
        FIXME("Unhandled format %#x.\n", data->Format);
        return E_INVALIDARG;
    }

    if (data->Flags & ~D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE)
        FIXME("Ignoring flags %#x.\n", data->Flags);

    sample_counts = (data->Flags & D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE)
            ? format->supported_sparse_sample_counts
            : format->supported_sample_counts;

    if (sample_counts & vk_samples)
        data->NumQualityLevels = 1;

done:
    TRACE("Returning %u quality levels.\n", data->NumQualityLevels);
    return S_OK;
}

bool d3d12_device_is_uma(struct d3d12_device *device, bool *coherent)
{
    unsigned int i;

    if (coherent)
        *coherent = true;

    for (i = 0; i < device->memory_properties.memoryTypeCount; ++i)
    {
        if (!(device->memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            return false;
        if (coherent && !(device->memory_properties.memoryTypes[i].propertyFlags
                & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            *coherent = false;
    }

    return true;
}

static DXGI_FORMAT d3d12_device_get_typeless_format(struct d3d12_device *device, DXGI_FORMAT format)
{
    if (format < device->format_compatibility_list_count)
        return device->format_compatibility_lists[format].typeless_format;

    return DXGI_FORMAT_UNKNOWN;
}

static UINT d3d12_device_get_format_displayable_features(struct d3d12_device *device, DXGI_FORMAT format)
{
    DXGI_FORMAT typeless_format;
    unsigned int i;
    UINT flags;

    /* Exhaustive list of all swapchain formats */
    static const DXGI_FORMAT displayable_formats[] =
    {
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
    };

    typeless_format = d3d12_device_get_typeless_format(device, format);
    flags = 0u;

    for (i = 0; i < ARRAY_SIZE(displayable_formats); i++)
    {
        if (displayable_formats[i] == format)
            flags |= D3D12_FORMAT_SUPPORT1_DISPLAY;
        else if (typeless_format && d3d12_device_get_typeless_format(device, displayable_formats[i]) == typeless_format)
            flags |= D3D12_FORMAT_SUPPORT1_BACK_BUFFER_CAST;
    }

    return (flags & D3D12_FORMAT_SUPPORT1_DISPLAY) ? flags : 0u;
}

static bool d3d12_format_is_streamout_compatible(DXGI_FORMAT format)
{
    unsigned int i;

    static const DXGI_FORMAT so_formats[] =
    {
        DXGI_FORMAT_R32G32B32A32_UINT,
        DXGI_FORMAT_R32G32B32A32_SINT,
        DXGI_FORMAT_R32G32B32A32_FLOAT,
        DXGI_FORMAT_R32G32B32_UINT,
        DXGI_FORMAT_R32G32B32_SINT,
        DXGI_FORMAT_R32G32B32_FLOAT,
        DXGI_FORMAT_R32G32_UINT,
        DXGI_FORMAT_R32G32_SINT,
        DXGI_FORMAT_R32G32_FLOAT,
        DXGI_FORMAT_R32_UINT,
        DXGI_FORMAT_R32_SINT,
        DXGI_FORMAT_R32_FLOAT,
    };

    for (i = 0; i < ARRAY_SIZE(so_formats); i++)
    {
        if (so_formats[i] == format)
            return true;
    }

    return false;
}

static HRESULT d3d12_device_get_format_support(struct d3d12_device *device, D3D12_FEATURE_DATA_FORMAT_SUPPORT *data)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkPhysicalDeviceImageFormatInfo2 format_info;
    VkImageFormatProperties2 format_properties;
    VkFormatFeatureFlags2 image_features;
    const struct vkd3d_format *format;
    VkResult vr;

    data->Support1 = D3D12_FORMAT_SUPPORT1_NONE;
    data->Support2 = D3D12_FORMAT_SUPPORT2_NONE;

    if (!is_valid_format(data->Format))
    {
        WARN("Invalid format %d.\n", data->Format);
        return E_INVALIDARG;
    }

    if (!data->Format)
    {
        data->Support1 = D3D12_FORMAT_SUPPORT1_BUFFER;

        if (device->device_info.features2.features.sparseResidencyBuffer)
            data->Support2 = D3D12_FORMAT_SUPPORT2_TILED;
        return S_OK;
    }

    if (!(format = vkd3d_get_format(device, data->Format, false)))
        format = vkd3d_get_format(device, data->Format, true);
    if (!format)
        return E_FAIL;

    /* Special opaque formats. */
    if (data->Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE ||
            data->Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE)
    {
        data->Support1 = D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_MIP;
        data->Support2 = D3D12_FORMAT_SUPPORT2_SAMPLER_FEEDBACK;
        return S_OK;
    }

    if (format->vk_aspect_mask & VK_IMAGE_ASPECT_PLANE_0_BIT)
        image_features = format->vk_format_features_castable;
    else
        image_features = format->vk_format_features;

    if (format->vk_format_features_buffer)
        data->Support1 |= D3D12_FORMAT_SUPPORT1_BUFFER;
    if (format->vk_format_features_buffer & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)
        data->Support1 |= D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER;
    if (data->Format == DXGI_FORMAT_R16_UINT || data->Format == DXGI_FORMAT_R32_UINT)
        data->Support1 |= D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER;

    if (d3d12_format_is_streamout_compatible(data->Format))
        data->Support1 |= D3D12_FORMAT_SUPPORT1_SO_BUFFER;

    if (image_features)
    {
        data->Support1 |= D3D12_FORMAT_SUPPORT1_TEXTURE2D;

        /* Planar formats only support 2D textures with one mip */
        if (!(format->vk_aspect_mask & VK_IMAGE_ASPECT_PLANE_0_BIT))
            data->Support1 |= D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURECUBE | D3D12_FORMAT_SUPPORT1_MIP;

        /* 3D depth-stencil images are not supported */
        if (format->vk_aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT)
            data->Support1 |= D3D12_FORMAT_SUPPORT1_TEXTURE3D;

        if (format->dxgi_format < device->format_compatibility_list_count &&
                device->format_compatibility_lists[format->dxgi_format].typeless_format)
            data->Support1 |= D3D12_FORMAT_SUPPORT1_CAST_WITHIN_BIT_LAYOUT;

        data->Support1 |= d3d12_device_get_format_displayable_features(device, format->dxgi_format);
    }
    if (image_features & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT)
    {
        data->Support1 |= D3D12_FORMAT_SUPPORT1_SHADER_LOAD | D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD
                | D3D12_FORMAT_SUPPORT1_SHADER_GATHER;
        if (image_features & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
        {
            data->Support1 |= D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
        }
        if (format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
        {
            data->Support1 |= D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON
                    | D3D12_FORMAT_SUPPORT1_SHADER_GATHER_COMPARISON;
        }
    }
    if (image_features & VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT)
    {
        data->Support1 |= D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET;

        if (device->device_info.features2.features.logicOp && format->type == VKD3D_FORMAT_TYPE_UINT)
            data->Support2 |= D3D12_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP;

        /* This bit seems to only indicate support for AVERAGE resolves and is
         * not reported for integer or depth/stencil formats on native drivers. */
        if (format->type == VKD3D_FORMAT_TYPE_OTHER)
            data->Support1 |= D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE;
    }
    if (image_features & VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT)
        data->Support1 |= D3D12_FORMAT_SUPPORT1_BLENDABLE;
    if (image_features & VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT)
        data->Support1 |= D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL | D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET;
    if (image_features & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT)
    {
        data->Support1 |= D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
        if (image_features & VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT)
            data->Support2 |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
        if (image_features & VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT)
            data->Support2 |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    }

    if (image_features & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT)
    {
        data->Support2 |= D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD
                | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS
                | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE
                | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE
                | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX
                | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX;
    }

    /* Do not support tiled resources for planar formats, matches D3D12. */
    if (!format->is_emulated && is_power_of_two(format->vk_aspect_mask))
    {
        if ((image_features & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT) &&
                device->device_info.features2.features.sparseResidencyImage2D)
        {
            memset(&format_info, 0, sizeof(format_info));
            format_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
            format_info.type = VK_IMAGE_TYPE_2D;
            format_info.format = format->vk_format;
            format_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
            format_info.flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
                    VK_IMAGE_CREATE_SPARSE_ALIASED_BIT |
                    VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT;
            format_info.tiling = format->vk_image_tiling;

            memset(&format_properties, 0, sizeof(format_properties));
            format_properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;

            vr = VK_CALL(vkGetPhysicalDeviceImageFormatProperties2(
              device->vk_physical_device, &format_info, &format_properties));

            if (vr == VK_SUCCESS)
                data->Support2 |= D3D12_FORMAT_SUPPORT2_TILED;
        }

        if (!image_features && format->vk_format_features_buffer &&
                device->device_info.features2.features.sparseResidencyBuffer)
            data->Support2 |= D3D12_FORMAT_SUPPORT2_TILED;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CheckFeatureSupport(d3d12_device_iface *iface,
        D3D12_FEATURE feature, void *feature_data, UINT feature_data_size)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, feature %#x, feature_data %p, feature_data_size %u.\n",
            iface, feature, feature_data, feature_data_size);

    switch (feature)
    {
        case D3D12_FEATURE_D3D12_OPTIONS:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options;

            TRACE("Double precision shader ops %#x.\n", data->DoublePrecisionFloatShaderOps);
            TRACE("Output merger logic op %#x.\n", data->OutputMergerLogicOp);
            TRACE("Shader min precision support %#x.\n", data->MinPrecisionSupport);
            TRACE("Tiled resources tier %#x.\n", data->TiledResourcesTier);
            TRACE("Resource binding tier %#x.\n", data->ResourceBindingTier);
            TRACE("PS specified stencil ref %#x.\n", data->PSSpecifiedStencilRefSupported);
            TRACE("Typed UAV load and additional formats %#x.\n", data->TypedUAVLoadAdditionalFormats);
            TRACE("ROV %#x.\n", data->ROVsSupported);
            TRACE("Conservative rasterization tier %#x.\n", data->ConservativeRasterizationTier);
            TRACE("Max GPU virtual address bits per resource %u.\n", data->MaxGPUVirtualAddressBitsPerResource);
            TRACE("Standard swizzle 64KB %#x.\n", data->StandardSwizzle64KBSupported);
            TRACE("Cross-node sharing tier %#x.\n", data->CrossNodeSharingTier);
            TRACE("Cross-adapter row-major texture %#x.\n", data->CrossAdapterRowMajorTextureSupported);
            TRACE("VP and RT array index from any shader without GS emulation %#x.\n",
                    data->VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation);
            TRACE("Resource heap tier %#x.\n", data->ResourceHeapTier);
            return S_OK;
        }

        case D3D12_FEATURE_ARCHITECTURE:
        {
            D3D12_FEATURE_DATA_ARCHITECTURE *data = feature_data;
            bool coherent;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            if (data->NodeIndex)
            {
                FIXME("Multi-adapter not supported.\n");
                return E_INVALIDARG;
            }

            WARN("Assuming device does not support tile based rendering.\n");
            data->TileBasedRenderer = FALSE;

            data->UMA = d3d12_device_is_uma(device, &coherent);
            data->CacheCoherentUMA = data->UMA ? coherent : FALSE;

            TRACE("Tile based renderer %#x, UMA %#x, cache coherent UMA %#x.\n",
                    data->TileBasedRenderer, data->UMA, data->CacheCoherentUMA);
            return S_OK;
        }

        case D3D12_FEATURE_FEATURE_LEVELS:
        {
            struct d3d12_caps *caps = &device->d3d12_caps;
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
                if (data->MaxSupportedFeatureLevel < fl && fl <= caps->max_feature_level)
                    data->MaxSupportedFeatureLevel = fl;
            }

            TRACE("Max supported feature level %#x.\n", data->MaxSupportedFeatureLevel);
            return S_OK;
        }

        case D3D12_FEATURE_FORMAT_SUPPORT:
        {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT *data = feature_data;
            HRESULT hr;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            if (FAILED(hr = d3d12_device_get_format_support(device, data)))
                return hr;

            vkd3d_determine_format_support_for_feature_level(device, data);

            TRACE("Format %#x, support1 %#x, support2 %#x.\n", data->Format, data->Support1, data->Support2);
            return S_OK;
        }

        case D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS:
        {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            return d3d12_device_check_multisample_quality_levels(device, data);
        }

        case D3D12_FEATURE_FORMAT_INFO:
        {
            D3D12_FEATURE_DATA_FORMAT_INFO *data = feature_data;
            const struct vkd3d_format *format;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            if (data->Format == DXGI_FORMAT_UNKNOWN)
            {
                data->PlaneCount = 1;
                return S_OK;
            }

            if (!(format = vkd3d_get_format(device, data->Format, false)))
                format = vkd3d_get_format(device, data->Format, true);
            if (!format)
            {
                FIXME("Unhandled format %#x.\n", data->Format);
                return E_INVALIDARG;
            }

            data->PlaneCount = format->plane_count;

            TRACE("Format %#x, plane count %"PRIu8".\n", data->Format, data->PlaneCount);
            return S_OK;
        }

        case D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT:
        {
            const D3D12_FEATURE_DATA_D3D12_OPTIONS *options = &device->d3d12_caps.options;
            D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            data->MaxGPUVirtualAddressBitsPerResource = options->MaxGPUVirtualAddressBitsPerResource;
            data->MaxGPUVirtualAddressBitsPerProcess = options->MaxGPUVirtualAddressBitsPerResource;

            TRACE("Max GPU virtual address bits per resource %u, Max GPU virtual address bits per process %u.\n",
                    data->MaxGPUVirtualAddressBitsPerResource, data->MaxGPUVirtualAddressBitsPerProcess);
            return S_OK;
        }

        case D3D12_FEATURE_SHADER_MODEL:
        {
            D3D12_FEATURE_DATA_SHADER_MODEL *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            TRACE("Request shader model %#x.\n", data->HighestShaderModel);
            data->HighestShaderModel = min(data->HighestShaderModel, device->d3d12_caps.max_shader_model);
            TRACE("Shader model %#x.\n", data->HighestShaderModel);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS1:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS1 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options1;

            TRACE("Wave ops %#x.\n", data->WaveOps);
            TRACE("Min wave lane count %u.\n", data->WaveLaneCountMin);
            TRACE("Max wave lane count %u.\n", data->WaveLaneCountMax);
            TRACE("Total lane count %u.\n", data->TotalLaneCount);
            TRACE("Expanded compute resource states %#x.\n", data->ExpandedComputeResourceStates);
            TRACE("Int64 shader ops %#x.\n", data->Int64ShaderOps);
            return S_OK;
        }

        case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT:
        {
            D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            if (data->NodeIndex)
            {
                FIXME("Multi-adapter not supported.\n");
                return E_INVALIDARG;
            }

            data->Support = D3D12_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAG_NONE;
            TRACE("Protected resource session support %#x.\n", data->Support);
            return S_OK;
        }

        case D3D12_FEATURE_ROOT_SIGNATURE:
        {
            D3D12_FEATURE_DATA_ROOT_SIGNATURE *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            if (!data->HighestVersion || data->HighestVersion > D3D_ROOT_SIGNATURE_VERSION_1_2)
            {
                WARN("Unrecognized root signature version %#x.\n", data->HighestVersion);
                return E_INVALIDARG;
            }

            TRACE("Root signature requested %#x.\n", data->HighestVersion);
            data->HighestVersion = min(data->HighestVersion, D3D_ROOT_SIGNATURE_VERSION_1_2);

            TRACE("Root signature version %#x.\n", data->HighestVersion);
            return S_OK;
        }

        case D3D12_FEATURE_ARCHITECTURE1:
        {
            D3D12_FEATURE_DATA_ARCHITECTURE1 *data = feature_data;
            bool coherent;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            if (data->NodeIndex)
            {
                FIXME("Multi-adapter not supported.\n");
                return E_INVALIDARG;
            }

            WARN("Assuming device does not support tile based rendering.\n");
            data->TileBasedRenderer = FALSE;

            data->UMA = d3d12_device_is_uma(device, &coherent);
            data->CacheCoherentUMA = data->UMA ? coherent : FALSE;
            data->IsolatedMMU = TRUE; /* Appears to be true Windows drivers */

            TRACE("Tile based renderer %#x, UMA %#x, cache coherent UMA %#x, isolated MMU %#x.\n",
                    data->TileBasedRenderer, data->UMA, data->CacheCoherentUMA, data->IsolatedMMU);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS2:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS2 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options2;

            TRACE("Depth bounds test %#x.\n", data->DepthBoundsTestSupported);
            TRACE("Programmable sample positions tier %u.\n", data->ProgrammableSamplePositionsTier);
            return S_OK;
        }

        case D3D12_FEATURE_SHADER_CACHE:
        {
            D3D12_FEATURE_DATA_SHADER_CACHE *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            /* We cannot query shader cache features from the Vulkan driver,
             * but all relevant drivers have their own disk cache, so we'll
             * advertize support for the AUTOMATIC_*_CACHE feature bits. */
            data->SupportFlags = D3D12_SHADER_CACHE_SUPPORT_SINGLE_PSO |
                    D3D12_SHADER_CACHE_SUPPORT_LIBRARY |
                    D3D12_SHADER_CACHE_SUPPORT_AUTOMATIC_INPROC_CACHE |
                    D3D12_SHADER_CACHE_SUPPORT_AUTOMATIC_DISK_CACHE;

            TRACE("Shader cache support flags %#x.\n", data->SupportFlags);
            return S_OK;
        }

        case D3D12_FEATURE_COMMAND_QUEUE_PRIORITY:
        {
            D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            /* FIXME We ignore priorities since Vulkan queues are created up-front */
            data->PriorityForTypeIsSupported = data->Priority <= D3D12_COMMAND_QUEUE_PRIORITY_HIGH;

            TRACE("Command list type %u supports priority %u: %#x.\n",
                    data->CommandListType, data->Priority, data->PriorityForTypeIsSupported);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS3:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS3 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options3;

            TRACE("Copy queue timestamp queries %#x.\n", data->CopyQueueTimestampQueriesSupported);
            TRACE("Casting fully typed formats %#x.\n", data->CastingFullyTypedFormatSupported);
            TRACE("Write buffer immediate support flags %#x.\n", data->WriteBufferImmediateSupportFlags);
            TRACE("View instancing tier %u.\n", data->ViewInstancingTier);
            TRACE("Barycentrics %#x.\n", data->BarycentricsSupported);
            return S_OK;
        }

        case D3D12_FEATURE_EXISTING_HEAPS:
        {
            D3D12_FEATURE_DATA_EXISTING_HEAPS *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            /* Would require some sort of wine
             * interop to support file handles */
            data->Supported = FALSE;

            TRACE("Existing heaps %#x.\n", data->Supported);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS4:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS4 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options4;

            TRACE("64kB alignment for MSAA textures %#x.\n", data->MSAA64KBAlignedTextureSupported);
            TRACE("Shared resource compatibility tier %u.\n", data->SharedResourceCompatibilityTier);
            TRACE("Native 16-bit shader ops %#x.\n", data->Native16BitShaderOpsSupported);
            return S_OK;
        }

        case D3D12_FEATURE_SERIALIZATION:
        {
            D3D12_FEATURE_DATA_SERIALIZATION *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            if (data->NodeIndex)
            {
                FIXME("Multi-adapter not supported.\n");
                return E_INVALIDARG;
            }

            data->HeapSerializationTier = D3D12_HEAP_SERIALIZATION_TIER_0;

            TRACE("Heap serialization tier %u.\n", data->HeapSerializationTier);
            return S_OK;
        }

        case D3D12_FEATURE_CROSS_NODE:
        {
            D3D12_FEATURE_DATA_CROSS_NODE *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            data->SharingTier = D3D12_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED;
            data->AtomicShaderInstructions = FALSE;

            TRACE("Cross-node sharing tier %u.\n", data->SharingTier);
            TRACE("Cross-node atomic shader instructions %#x.\n", data->AtomicShaderInstructions);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS5:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options5;

            TRACE("SRV-only Tiled Resources Tier 3 %#x.\n", data->SRVOnlyTiledResourceTier3);
            TRACE("Render pass tier %u.\n", data->RenderPassesTier);
            TRACE("Raytracing tier %u.\n", data->RaytracingTier);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS6:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS6 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options6;

            TRACE("Additional shading rates %#x.\n", data->AdditionalShadingRatesSupported);
            TRACE("Per-primtive shading rate with viewport indexing %#x.\n", data->PerPrimitiveShadingRateSupportedWithViewportIndexing);
            TRACE("Variable shading rate tier %u.\n", data->VariableShadingRateTier);
            TRACE("Shading rate image tile size %u.\n", data->ShadingRateImageTileSize);
            TRACE("Background processing %#x.\n", data->BackgroundProcessingSupported);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS7:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS7 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options7;

            TRACE("Mesh shading tier %#x.\n", data->MeshShaderTier);
            TRACE("Sampler feedback tier %#x.\n", data->SamplerFeedbackTier);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS8:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS8 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options8;

            TRACE("Unaligned block textures supported %u.\n", data->UnalignedBlockTexturesSupported);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS9:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS9 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options9;

            TRACE("AtomicInt64 on typed resource supported %u.\n", data->AtomicInt64OnTypedResourceSupported);
            TRACE("AtomicInt64 on group shared supported %u.\n", data->AtomicInt64OnGroupSharedSupported);
            TRACE("Mesh shader pipeline stats supported %u.\n", data->MeshShaderPipelineStatsSupported);
            TRACE("Mesh shader supports full range render target array index %u.\n", data->MeshShaderSupportsFullRangeRenderTargetArrayIndex);
            TRACE("Derivatives in mesh and amplification shaders supported %u.\n", data->DerivativesInMeshAndAmplificationShadersSupported);
            TRACE("Wave MMA tier #%x.\n", data->WaveMMATier);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS10:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS10 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options10;

            TRACE("Variable rate shading sum combiner supported %u.\n", data->VariableRateShadingSumCombinerSupported);
            TRACE("Mesh shader per primitive shading rate supported %u.\n", data->MeshShaderPerPrimitiveShadingRateSupported);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS11:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS11 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options11;

            TRACE("AtomicInt64 on descriptor heap resource supported %u.\n", data->AtomicInt64OnDescriptorHeapResourceSupported);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS12:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS12 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options12;
            TRACE("RelaxedFormatCasting supported %u.\n", data->RelaxedFormatCastingSupported);
            TRACE("EnhancedBarriers supported %u.\n", data->EnhancedBarriersSupported);
            TRACE("MSPrimitivesPipelineStatisticIncludesCulledPrimitives %u.\n",
                    data->MSPrimitivesPipelineStatisticIncludesCulledPrimitives);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS13:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS13 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options13;

            TRACE("Inverted viewport height flips Y supported %u.\n", data->InvertedViewportHeightFlipsYSupported);
            TRACE("Inverted viewport deps flips Z supported %u.\n", data->InvertedViewportDepthFlipsZSupported);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS14:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS14 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options14;

            TRACE("AdvancedTextureOpsSupported %u\n", data->AdvancedTextureOpsSupported);
            TRACE("WriteableMSAATexturesSupported %u\n", data->WriteableMSAATexturesSupported);
            TRACE("IndependentFrontAndBackStencilRefMaskSupported %u\n", data->IndependentFrontAndBackStencilRefMaskSupported);

            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS15:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS15 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options15;

            TRACE("TriangleFanSupported %u\n", data->TriangleFanSupported);
            TRACE("DynamicIndexBufferStripCutSupported %u\n", data->DynamicIndexBufferStripCutSupported);

            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS16:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS16 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options16;

            TRACE("DynamicDepthBiasSupported %u\n", data->DynamicDepthBiasSupported);
            TRACE("GPUUploadHeapSupported %u\n", data->GPUUploadHeapSupported);

            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS17:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS17 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options17;

            TRACE("NonNormalizedCoordinateSamplersSupported %u\n", data->NonNormalizedCoordinateSamplersSupported);
            TRACE("ManualWriteTrackingResourceSupported %u\n", data->ManualWriteTrackingResourceSupported);

            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS18:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS18 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options18;

            TRACE("RenderPassesValid %u\n", data->RenderPassesValid);

            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS19:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS19 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options19;

            TRACE("MismatchingOutputDimensionsSupported %u\n", data->MismatchingOutputDimensionsSupported);
            TRACE("SupportedSampleCountsWithNoOutputs %#x\n", data->SupportedSampleCountsWithNoOutputs);
            TRACE("PointSamplingAddressesNeverRoundUp %u\n", data->PointSamplingAddressesNeverRoundUp);
            TRACE("RasterizerDesc2Supported %u\n", data->RasterizerDesc2Supported);
            TRACE("NarrowQuadrilateralLinesSupported %u\n", data->NarrowQuadrilateralLinesSupported);
            TRACE("AnisoFilterWithPointMipSupported %u\n", data->AnisoFilterWithPointMipSupported);
            TRACE("MaxSamplerDescriptorHeapSize %u\n", data->MaxSamplerDescriptorHeapSize);
            TRACE("MaxSamplerDescriptorHeapSizeWithStaticSamplers %u\n", data->MaxSamplerDescriptorHeapSizeWithStaticSamplers);
            TRACE("MaxViewDescriptorHeapSize %u\n", data->MaxViewDescriptorHeapSize);
            TRACE("ComputeOnlyCustomHeapSupported %u\n", data->ComputeOnlyCustomHeapSupported);

            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS20:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS20 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options20;

            TRACE("ComputeOnlyWriteWatchSupported %u\n", data->ComputeOnlyWriteWatchSupported);
            TRACE("RecreateAtTier %#x\n", data->RecreateAtTier);

            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS21:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS21 *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            *data = device->d3d12_caps.options21;

            TRACE("WorkGraphsTier %#x\n", data->WorkGraphsTier);
            TRACE("ExecuteIndirectTier %u\n", data->ExecuteIndirectTier);
            TRACE("SampleCmpGradientAndBiasSupported %u\n", data->SampleCmpGradientAndBiasSupported);
            TRACE("ExtendedCommandInfoSupported %u\n", data->ExtendedCommandInfoSupported);

            return S_OK;
        }

        case D3D12_FEATURE_QUERY_META_COMMAND:
        {
            D3D12_FEATURE_DATA_QUERY_META_COMMAND *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            TRACE("input_data_size = %zu, input_data = %p, output_data_size = %zu, output_data = %p.\n",
                    (size_t)data->QueryInputDataSizeInBytes, data->pQueryInputData,
                    (size_t)data->QueryOutputDataSizeInBytes, data->pQueryOutputData);

            if ((data->QueryInputDataSizeInBytes && !data->pQueryInputData) ||
                    (data->QueryOutputDataSizeInBytes && !data->pQueryOutputData))
                return E_INVALIDARG;

            if (!memcmp(&data->CommandId, &IID_META_COMMAND_DSTORAGE, sizeof(data->CommandId)))
            {
                /* It is not clear what any of these parameters do, but as of right now
                 * it appears that only a single combination of parameters is supported. */
                const struct d3d12_meta_command_dstorage_query_in_args *in_args = data->pQueryInputData;
                struct d3d12_meta_command_dstorage_query_out_args *out_args = data->pQueryOutputData;

                /* Passing an output parameter size larger than the structure,
                 * is allowed but any excess bytes are not written. */
                if (data->QueryInputDataSizeInBytes < sizeof(*in_args) || data->QueryOutputDataSizeInBytes < sizeof(*out_args))
                {
                    FIXME("Unexpected input/output sizes for DirectStorage meta command: %zu, %zu.\n",
                            (size_t)data->QueryInputDataSizeInBytes, (size_t)data->QueryOutputDataSizeInBytes);
                    return E_INVALIDARG;
                }

                memset(out_args, 0, sizeof(*out_args));

                if (in_args->unknown2 == 1)
                {
                    out_args->unknown0 = 1;
                    /* Limit stream count to something reasonable. Given that we have a hard limit on the
                     * number of tiles we can process in one call when using the NV_memory_decompression
                     * path, we should make it unlikely for applications to hit that upper limit. */
                    out_args->max_stream_count = min(256u, in_args->stream_count);
                    /* Reserve one set of dispatch parameters per stream. */
                    out_args->scratch_size = sizeof(VkDispatchIndirectCommand) * out_args->max_stream_count;

                    if (d3d12_device_use_nv_memory_decompression(device))
                    {
                        /* Additionally reserve storage for per-tile memory regions */
                        out_args->scratch_size += sizeof(struct d3d12_meta_command_dstorage_scratch_header);
                    }
                }

                return S_OK;
            }

            FIXME("Unsupported meta command %s.\n", debugstr_guid(&data->CommandId));
            return E_INVALIDARG;
        }

        default:
            FIXME("Unhandled feature %#x.\n", feature);
            return E_NOTIMPL;
    }
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateDescriptorHeap(d3d12_device_iface *iface,
        const D3D12_DESCRIPTOR_HEAP_DESC *desc, REFIID riid, void **descriptor_heap)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_descriptor_heap *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, riid %s, descriptor_heap %p.\n",
            iface, desc, debugstr_guid(riid), descriptor_heap);

    if (FAILED(hr = d3d12_descriptor_heap_create(device, desc, &object)))
        return hr;

    return return_interface(&object->ID3D12DescriptorHeap_iface,
            &IID_ID3D12DescriptorHeap, riid, descriptor_heap);
}

static UINT STDMETHODCALLTYPE d3d12_device_GetDescriptorHandleIncrementSize(d3d12_device_iface *iface,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    TRACE("iface %p, descriptor_heap_type %#x.\n", iface, descriptor_heap_type);

    return d3d12_device_get_descriptor_handle_increment_size(device, descriptor_heap_type);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateRootSignature(d3d12_device_iface *iface,
        UINT node_mask, const void *bytecode, SIZE_T bytecode_length,
        REFIID riid, void **root_signature)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_root_signature *object;
    HRESULT hr;

    TRACE("iface %p, node_mask 0x%08x, bytecode %p, bytecode_length %lu, riid %s, root_signature %p.\n",
            iface, node_mask, bytecode, bytecode_length, debugstr_guid(riid), root_signature);

    debug_ignored_node_mask(node_mask);

    if (FAILED(hr = d3d12_root_signature_create(device, bytecode, bytecode_length, &object)))
        return hr;

    return return_interface(&object->ID3D12RootSignature_iface,
            &IID_ID3D12RootSignature, riid, root_signature);
}

static void STDMETHODCALLTYPE d3d12_device_CreateConstantBufferView_embedded(d3d12_device_iface *iface,
        const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, desc %p, descriptor %#lx.\n", iface, desc, descriptor.ptr);

    d3d12_desc_create_cbv_embedded(descriptor.ptr, device, desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateConstantBufferView_default(d3d12_device_iface *iface,
        const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, desc %p, descriptor %#lx.\n", iface, desc, descriptor.ptr);

    d3d12_desc_create_cbv(descriptor.ptr, device, desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateShaderResourceView_embedded(d3d12_device_iface *iface,
        ID3D12Resource *resource, const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, resource %p, desc %p, descriptor %#lx.\n",
            iface, resource, desc, descriptor.ptr);

    d3d12_desc_create_srv_embedded(descriptor.ptr, device, impl_from_ID3D12Resource(resource), desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateShaderResourceView_default(d3d12_device_iface *iface,
        ID3D12Resource *resource, const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, resource %p, desc %p, descriptor %#lx.\n",
            iface, resource, desc, descriptor.ptr);

    d3d12_desc_create_srv(descriptor.ptr, device, impl_from_ID3D12Resource(resource), desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateUnorderedAccessView_embedded(d3d12_device_iface *iface,
        ID3D12Resource *resource, ID3D12Resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_resource *d3d12_resource_ = impl_from_ID3D12Resource(resource);
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    TRACE("iface %p, resource %p, counter_resource %p, desc %p, descriptor %#lx.\n",
            iface, resource, counter_resource, desc, descriptor.ptr);

    d3d12_desc_create_uav_embedded(descriptor.ptr,
            device, d3d12_resource_,
            impl_from_ID3D12Resource(counter_resource), desc);

    /* Unknown at this time if we can support magic d3d12_uav_info with embedded mutable. */
}

VKD3D_THREAD_LOCAL struct D3D12_UAV_INFO *d3d12_uav_info = NULL;

static void STDMETHODCALLTYPE d3d12_device_CreateUnorderedAccessView_default(d3d12_device_iface *iface,
        ID3D12Resource *resource, ID3D12Resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    const struct vkd3d_vk_device_procs *vk_procs;
    VkResult vr;
    struct d3d12_resource *d3d12_resource_ = impl_from_ID3D12Resource(resource);
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    TRACE("iface %p, resource %p, counter_resource %p, desc %p, descriptor %#lx.\n",
            iface, resource, counter_resource, desc, descriptor.ptr);

    d3d12_desc_create_uav(descriptor.ptr,
            device, d3d12_resource_,
            impl_from_ID3D12Resource(counter_resource), desc);

    /* d3d12_uav_info stores the pointer to data from previous call to d3d12_device_vkd3d_ext_CaptureUAVInfo(). Below code will update the data. */
    if (d3d12_uav_info)
    {
        struct d3d12_desc_split d = d3d12_desc_decode_va(descriptor.ptr);

        if (desc->ViewDimension == D3D12_UAV_DIMENSION_BUFFER)
        {
            d3d12_uav_info->gpuVAStart = d.view->info.buffer.va;
            d3d12_uav_info->gpuVASize = d.view->info.buffer.range;
        }
        else
        {
            VkImageViewAddressPropertiesNVX out_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_ADDRESS_PROPERTIES_NVX };
            VkImageViewHandleInfoNVX imageViewHandleInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_HANDLE_INFO_NVX };

            imageViewHandleInfo.imageView = d.view->info.image.view->vk_image_view;
            imageViewHandleInfo.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

            vk_procs = &device->vk_procs;
            d3d12_uav_info->surfaceHandle = VK_CALL(vkGetImageViewHandleNVX(device->vk_device, &imageViewHandleInfo));

            if ((vr = VK_CALL(vkGetImageViewAddressNVX(device->vk_device, imageViewHandleInfo.imageView, &out_info))) < 0)
            {
                ERR("Failed to get imageview address, vr %d.\n", vr);
                return;
            }

            d3d12_uav_info->gpuVAStart = out_info.deviceAddress;
            d3d12_uav_info->gpuVASize = out_info.size;
        }
        /* Set this to null so that subsequent calls to this API wont update the previous pointer. */
        d3d12_uav_info = NULL;
    }
}

static void STDMETHODCALLTYPE d3d12_device_CreateRenderTargetView(d3d12_device_iface *iface,
        ID3D12Resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    TRACE("iface %p, resource %p, desc %p, descriptor %#lx.\n",
            iface, resource, desc, descriptor.ptr);

    d3d12_rtv_desc_create_rtv(d3d12_rtv_desc_from_cpu_handle(descriptor),
            impl_from_ID3D12Device(iface), impl_from_ID3D12Resource(resource), desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateDepthStencilView(d3d12_device_iface *iface,
        ID3D12Resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    TRACE("iface %p, resource %p, desc %p, descriptor %#lx.\n",
            iface, resource, desc, descriptor.ptr);

    d3d12_rtv_desc_create_dsv(d3d12_rtv_desc_from_cpu_handle(descriptor),
            impl_from_ID3D12Device(iface), impl_from_ID3D12Resource(resource), desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateSampler_embedded(d3d12_device_iface *iface,
        const D3D12_SAMPLER_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    D3D12_SAMPLER_DESC2 desc2;

    TRACE("iface %p, desc %p, descriptor %#lx.\n", iface, desc, descriptor.ptr);

    memcpy(&desc2, desc, sizeof(*desc));
    desc2.Flags = D3D12_SAMPLER_FLAG_NONE;
    d3d12_desc_create_sampler_embedded(descriptor.ptr, device, &desc2);
}

static void STDMETHODCALLTYPE d3d12_device_CreateSampler_default(d3d12_device_iface *iface,
        const D3D12_SAMPLER_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    D3D12_SAMPLER_DESC2 desc2;

    TRACE("iface %p, desc %p, descriptor %#lx.\n", iface, desc, descriptor.ptr);

    memcpy(&desc2, desc, sizeof(*desc));
    desc2.Flags = D3D12_SAMPLER_FLAG_NONE;
    d3d12_desc_create_sampler(descriptor.ptr, device, &desc2);
}

static void STDMETHODCALLTYPE d3d12_device_CreateSampler2_embedded(d3d12_device_iface *iface,
        const D3D12_SAMPLER_DESC2 *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, desc %p, descriptor %#lx.\n", iface, desc, descriptor.ptr);

    d3d12_desc_create_sampler_embedded(descriptor.ptr, device, desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateSampler2_default(d3d12_device_iface *iface,
        const D3D12_SAMPLER_DESC2 *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, desc %p, descriptor %#lx.\n", iface, desc, descriptor.ptr);

    d3d12_desc_create_sampler(descriptor.ptr, device, desc);
}

static inline D3D12_CPU_DESCRIPTOR_HANDLE d3d12_advance_cpu_descriptor_handle(D3D12_CPU_DESCRIPTOR_HANDLE handle,
        unsigned int increment, unsigned int units)
{
    handle.ptr += increment * units;
    return handle;
}

static inline void d3d12_device_copy_descriptors_cbv_srv_uav_sampler(struct d3d12_device *device,
        D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src,
        D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
        UINT descriptor_count)
{
#ifndef VKD3D_ENABLE_DESCRIPTOR_QA
    if (descriptor_count == 1)
    {
        /* Most common path. This path is faster for 1 descriptor. */
        d3d12_desc_copy_single(dst.ptr, src.ptr, device);
    }
    else
#endif
    {
        d3d12_desc_copy(dst.ptr, src.ptr, descriptor_count, heap_type, device);
    }
}

static inline void d3d12_device_copy_descriptors(struct d3d12_device *device,
        UINT dst_descriptor_range_count, const D3D12_CPU_DESCRIPTOR_HANDLE *dst_descriptor_range_offsets,
        const UINT *dst_descriptor_range_sizes,
        UINT src_descriptor_range_count, const D3D12_CPU_DESCRIPTOR_HANDLE *src_descriptor_range_offsets,
        const UINT *src_descriptor_range_sizes,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    unsigned int dst_range_idx, dst_idx, src_range_idx, src_idx;
    D3D12_CPU_DESCRIPTOR_HANDLE dst, src, dst_start, src_start;
    unsigned int dst_range_size, src_range_size, copy_count;
    unsigned int increment;

    increment = d3d12_device_get_descriptor_handle_increment_size(device, descriptor_heap_type);

    dst_range_idx = dst_idx = 0;
    src_range_idx = src_idx = 0;
    while (dst_range_idx < dst_descriptor_range_count && src_range_idx < src_descriptor_range_count)
    {
        dst_range_size = dst_descriptor_range_sizes ? dst_descriptor_range_sizes[dst_range_idx] : 1;
        src_range_size = src_descriptor_range_sizes ? src_descriptor_range_sizes[src_range_idx] : 1;

        dst_start = dst_descriptor_range_offsets[dst_range_idx];
        src_start = src_descriptor_range_offsets[src_range_idx];

        copy_count = min(dst_range_size - dst_idx, src_range_size - src_idx);

        dst = d3d12_advance_cpu_descriptor_handle(dst_start, increment, dst_idx);
        src = d3d12_advance_cpu_descriptor_handle(src_start, increment, src_idx);

        switch (descriptor_heap_type)
        {
            case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
                d3d12_desc_copy(dst.ptr, src.ptr, copy_count,
                        descriptor_heap_type, device);
                break;
            case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
            case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
                d3d12_rtv_desc_copy(d3d12_rtv_desc_from_cpu_handle(dst),
                        d3d12_rtv_desc_from_cpu_handle(src), copy_count);
                break;
            default:
                ERR("Unhandled descriptor heap type %u.\n", descriptor_heap_type);
                return;
        }

        dst_idx += copy_count;
        src_idx += copy_count;

        if (dst_idx >= dst_range_size)
        {
            ++dst_range_idx;
            dst_idx = 0;
        }
        if (src_idx >= src_range_size)
        {
            ++src_range_idx;
            src_idx = 0;
        }
    }
}

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptors(d3d12_device_iface *iface,
        UINT dst_descriptor_range_count, const D3D12_CPU_DESCRIPTOR_HANDLE *dst_descriptor_range_offsets,
        const UINT *dst_descriptor_range_sizes,
        UINT src_descriptor_range_count, const D3D12_CPU_DESCRIPTOR_HANDLE *src_descriptor_range_offsets,
        const UINT *src_descriptor_range_sizes,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    TRACE("iface %p, dst_descriptor_range_count %u, dst_descriptor_range_offsets %p, "
            "dst_descriptor_range_sizes %p, src_descriptor_range_count %u, "
            "src_descriptor_range_offsets %p, src_descriptor_range_sizes %p, "
            "descriptor_heap_type %#x.\n",
            iface, dst_descriptor_range_count, dst_descriptor_range_offsets,
            dst_descriptor_range_sizes, src_descriptor_range_count, src_descriptor_range_offsets,
            src_descriptor_range_sizes, descriptor_heap_type);

    d3d12_device_copy_descriptors(impl_from_ID3D12Device(iface),
            dst_descriptor_range_count, dst_descriptor_range_offsets,
            dst_descriptor_range_sizes,
            src_descriptor_range_count, src_descriptor_range_offsets,
            src_descriptor_range_sizes,
            descriptor_heap_type);
}

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptorsSimple_descriptor_buffer_16_16_4(d3d12_device_iface *iface,
        UINT descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE dst_descriptor_range_offset,
        const D3D12_CPU_DESCRIPTOR_HANDLE src_descriptor_range_offset,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    /* Optimized NVIDIA path. Buffers are 16 byte, but images and samplers are just 4 byte indices,
     * so we cannot use embedded mutable style copies. */

    struct d3d12_device *device;
    struct d3d12_desc_split dst;
    struct d3d12_desc_split src;
    size_t i, n;

    TRACE("iface %p, descriptor_count %u, dst_descriptor_range_offset %#lx, "
          "src_descriptor_range_offset %#lx, descriptor_heap_type %#x.\n",
            iface, descriptor_count, dst_descriptor_range_offset.ptr, src_descriptor_range_offset.ptr,
            descriptor_heap_type);

    if (VKD3D_EXPECT_TRUE(descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
            descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER))
    {
        dst = d3d12_desc_decode_va(dst_descriptor_range_offset.ptr);
        src = d3d12_desc_decode_va(src_descriptor_range_offset.ptr);
    }

    if (VKD3D_EXPECT_TRUE(descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV))
    {
        const uint8_t *src_set0, *src_set1;
        const VkDeviceAddress *src_va;
        uint8_t *dst_set0, *dst_set1;
        VkDeviceAddress *dst_va;

        dst_set0 = dst.heap->fast_pointer_bank[0];
        dst_set1 = dst.heap->fast_pointer_bank[1];
        dst_va = dst.heap->fast_pointer_bank[2];
        src_set0 = src.heap->fast_pointer_bank[0];
        src_set1 = src.heap->fast_pointer_bank[1];
        src_va = src.heap->fast_pointer_bank[2];

        dst_set0 += dst.offset * 16;
        dst_set1 += dst.offset * 16;
        src_set0 += src.offset * 16;
        src_set1 += src.offset * 16;

        if (VKD3D_EXPECT_TRUE(descriptor_count == 1))
        {
            vkd3d_memcpy_aligned_16_cached(dst_set0, src_set0);
            vkd3d_memcpy_aligned_16_cached(dst_set1, src_set1);
            *dst.view = *src.view;
            *dst.types = *src.types;
            dst_va[dst.offset] = src_va[src.offset];
        }
        else
        {
            vkd3d_memcpy_aligned_cached(dst_set0, src_set0, 16 * descriptor_count);
            vkd3d_memcpy_aligned_cached(dst_set1, src_set1, 16 * descriptor_count);
            dst_va += dst.offset;
            src_va += src.offset;

            /* Enforce size_t for better x86 addressing.
             * Avoid memcpy since we need to optimize for small descriptor count. */
            for (i = 0, n = descriptor_count; i < n; i++)
            {
                dst_va[i] = src_va[i];
                dst.view[i] = src.view[i];
                dst.types[i] = src.types[i];
            }
        }
    }
    else if (descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        const uint32_t *src_sampler = src.heap->fast_pointer_bank[0];
        uint32_t *dst_sampler = dst.heap->fast_pointer_bank[0];

        src_sampler += src.offset;
        dst_sampler += dst.offset;

        if (VKD3D_EXPECT_TRUE(descriptor_count == 1))
        {
            *dst_sampler = *src_sampler;
            *dst.view = *src.view;
            *dst.types = *src.types;
        }
        else
        {
            /* Enforce size_t for better x86 addressing.
             * Avoid memcpy since we need to optimize for small descriptor count. */
            for (i = 0, n = descriptor_count; i < n; i++)
            {
                dst_sampler[i] = src_sampler[i];
                dst.view[i] = src.view[i];
                dst.types[i] = src.types[i];
            }
        }
    }
    else
    {
        device = unsafe_impl_from_ID3D12Device(iface);
        d3d12_device_copy_descriptors(device,
                1, &dst_descriptor_range_offset, &descriptor_count,
                1, &src_descriptor_range_offset, &descriptor_count,
                descriptor_heap_type);
    }
}

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptorsSimple_descriptor_buffer_64_64_32(d3d12_device_iface *iface,
        UINT descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE dst_descriptor_range_offset,
        const D3D12_CPU_DESCRIPTOR_HANDLE src_descriptor_range_offset,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    /* Optimized Intel Arc path. */

    struct d3d12_device *device;
    struct d3d12_desc_split dst;
    struct d3d12_desc_split src;
    size_t i, n;

    TRACE("iface %p, descriptor_count %u, dst_descriptor_range_offset %#lx, "
          "src_descriptor_range_offset %#lx, descriptor_heap_type %#x.\n",
            iface, descriptor_count, dst_descriptor_range_offset.ptr, src_descriptor_range_offset.ptr,
            descriptor_heap_type);

    if (VKD3D_EXPECT_TRUE(descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
            descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER))
    {
        dst = d3d12_desc_decode_va(dst_descriptor_range_offset.ptr);
        src = d3d12_desc_decode_va(src_descriptor_range_offset.ptr);
    }

    if (VKD3D_EXPECT_TRUE(descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV))
    {
        const uint8_t *src_set0, *src_set1;
        const VkDeviceAddress *src_va;
        uint8_t *dst_set0, *dst_set1;
        VkDeviceAddress *dst_va;

        dst_set0 = dst.heap->fast_pointer_bank[0];
        dst_set1 = dst.heap->fast_pointer_bank[1];
        dst_va = dst.heap->fast_pointer_bank[2];
        src_set0 = src.heap->fast_pointer_bank[0];
        src_set1 = src.heap->fast_pointer_bank[1];
        src_va = src.heap->fast_pointer_bank[2];

        dst_set0 += dst.offset * 64;
        dst_set1 += dst.offset * 64;
        src_set0 += src.offset * 64;
        src_set1 += src.offset * 64;

        if (VKD3D_EXPECT_TRUE(descriptor_count == 1))
        {
            vkd3d_memcpy_aligned_cached(dst_set0, src_set0, 64);
            vkd3d_memcpy_aligned_cached(dst_set1, src_set1, 64);
            *dst.view = *src.view;
            *dst.types = *src.types;
            dst_va[dst.offset] = src_va[src.offset];
        }
        else
        {
            vkd3d_memcpy_aligned_cached(dst_set0, src_set0, 64 * descriptor_count);
            vkd3d_memcpy_aligned_cached(dst_set1, src_set1, 64 * descriptor_count);
            dst_va += dst.offset;
            src_va += src.offset;

            /* Enforce size_t for better x86 addressing.
             * Avoid memcpy since we need to optimize for small descriptor count. */
            for (i = 0, n = descriptor_count; i < n; i++)
            {
                dst_va[i] = src_va[i];
                dst.view[i] = src.view[i];
                dst.types[i] = src.types[i];
            }
        }
    }
    else if (descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        const uint8_t *src_sampler = src.heap->fast_pointer_bank[0];
        uint8_t *dst_sampler = dst.heap->fast_pointer_bank[0];

        src_sampler += src.offset * 32;
        dst_sampler += dst.offset * 32;

        if (VKD3D_EXPECT_TRUE(descriptor_count == 1))
        {
            vkd3d_memcpy_aligned_cached(dst_sampler, src_sampler, 32);
            *dst.view = *src.view;
            *dst.types = *src.types;
        }
        else
        {
            vkd3d_memcpy_aligned_cached(dst_sampler, src_sampler, 32 * descriptor_count);
            /* Enforce size_t for better x86 addressing.
             * Avoid memcpy since we need to optimize for small descriptor count. */
            for (i = 0, n = descriptor_count; i < n; i++)
            {
                dst.view[i] = src.view[i];
                dst.types[i] = src.types[i];
            }
        }
    }
    else
    {
        device = unsafe_impl_from_ID3D12Device(iface);
        d3d12_device_copy_descriptors(device,
                1, &dst_descriptor_range_offset, &descriptor_count,
                1, &src_descriptor_range_offset, &descriptor_count,
                descriptor_heap_type);
    }
}

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptorsSimple_embedded_64_16_packed(d3d12_device_iface *iface,
        UINT descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE dst_descriptor_range_offset,
        const D3D12_CPU_DESCRIPTOR_HANDLE src_descriptor_range_offset,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    struct d3d12_device *device;
    TRACE("iface %p, descriptor_count %u, dst_descriptor_range_offset %#lx, "
          "src_descriptor_range_offset %#lx, descriptor_heap_type %#x.\n",
            iface, descriptor_count, dst_descriptor_range_offset.ptr, src_descriptor_range_offset.ptr,
            descriptor_heap_type);

    if (VKD3D_EXPECT_TRUE(descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV))
    {
        /* If metadata is packed, this collapses to pure memcpy. */

        /* The cached mask is used to mark if a page is write-combined or not.
         * For CPU -> GPU copies, we can use NT stores. This helps Deck performance,
         * so it's worthwhile to go to extreme lengths. This memory is always mapped write-combined.
         * For CPU -> CPU copies, we cannot use NT stores since it breaks memory ordering if
         * other threads aim to read the CPU descriptors later. Fortunately, CPU -> CPU copies
         * are quirky at best and never used, so the branch predictor should be able to hide all overhead. */

        /* Using a subtract here (instead of the more idiomatic negative mask)
         * is a cute way of asserting the API requirement that the
         * src VA must be a non-shader visible heap. We use aligned loads and stores which will fault if
         * there is a misalignment.
         * It is also faster since the subtract is folded in to the constant address offsets. */

        if (VKD3D_EXPECT_TRUE(descriptor_count == 1))
        {
            if (VKD3D_EXPECT_TRUE(!(dst_descriptor_range_offset.ptr & VKD3D_RESOURCE_EMBEDDED_CACHED_MASK)))
            {
                vkd3d_memcpy_aligned_64_non_temporal(
                        (void *)dst_descriptor_range_offset.ptr,
                        (const void *)(src_descriptor_range_offset.ptr - VKD3D_RESOURCE_EMBEDDED_CACHED_MASK));
            }
            else
            {
                /* If we're copying to host visible descriptor memory, we have to be careful
                 * not to break memory ordering by using NT stores.
                 * This path is basically never taken. */
                vkd3d_memcpy_aligned_64_cached(
                        (void *)(dst_descriptor_range_offset.ptr - VKD3D_RESOURCE_EMBEDDED_CACHED_MASK),
                        (const void *)(src_descriptor_range_offset.ptr - VKD3D_RESOURCE_EMBEDDED_CACHED_MASK));
            }
        }
        else
        {
            if (VKD3D_EXPECT_TRUE(!(dst_descriptor_range_offset.ptr & VKD3D_RESOURCE_EMBEDDED_CACHED_MASK)))
            {
                vkd3d_memcpy_aligned_non_temporal(
                        (void *)dst_descriptor_range_offset.ptr,
                        (const void *)(src_descriptor_range_offset.ptr - VKD3D_RESOURCE_EMBEDDED_CACHED_MASK),
                        64 * descriptor_count);
            }
            else
            {
                /* If we're copying to host visible descriptor memory, we have to be careful
                 * not to break memory ordering by using NT stores.
                 * This path is basically never taken. */
                vkd3d_memcpy_aligned_cached(
                        (void *)(dst_descriptor_range_offset.ptr - VKD3D_RESOURCE_EMBEDDED_CACHED_MASK),
                        (const void *)(src_descriptor_range_offset.ptr - VKD3D_RESOURCE_EMBEDDED_CACHED_MASK),
                        64 * descriptor_count);
            }
        }
    }
    else if (descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        if (VKD3D_EXPECT_TRUE(descriptor_count == 1))
        {
            vkd3d_memcpy_aligned_16_cached(
                    (void *)dst_descriptor_range_offset.ptr,
                    (const void *)src_descriptor_range_offset.ptr);
        }
        else
        {
            vkd3d_memcpy_aligned_cached(
                    (void *)dst_descriptor_range_offset.ptr,
                    (const void *)src_descriptor_range_offset.ptr,
                    16 * descriptor_count);
        }
    }
    else
    {
        device = unsafe_impl_from_ID3D12Device(iface);
        d3d12_device_copy_descriptors(device,
                1, &dst_descriptor_range_offset, &descriptor_count,
                1, &src_descriptor_range_offset, &descriptor_count,
                descriptor_heap_type);
    }
}

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptorsSimple_embedded_32_16_planar(d3d12_device_iface *iface,
        UINT descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE dst_descriptor_range_offset,
        const D3D12_CPU_DESCRIPTOR_HANDLE src_descriptor_range_offset,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    struct d3d12_device *device;
    TRACE("iface %p, descriptor_count %u, dst_descriptor_range_offset %#lx, "
          "src_descriptor_range_offset %#lx, descriptor_heap_type %#x.\n",
            iface, descriptor_count, dst_descriptor_range_offset.ptr, src_descriptor_range_offset.ptr,
            descriptor_heap_type);

    if (VKD3D_EXPECT_TRUE(descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV))
    {
        if (VKD3D_EXPECT_TRUE(descriptor_count == 1))
        {
            /* Expected path. */
            d3d12_desc_copy_embedded_resource_single_32(
                    dst_descriptor_range_offset.ptr,
                    src_descriptor_range_offset.ptr);
        }
        else
        {
            /* Rare path. */
            d3d12_desc_copy_embedded_resource(
                    dst_descriptor_range_offset.ptr,
                    src_descriptor_range_offset.ptr,
                    32 * descriptor_count);
        }
    }
    else if (descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        if (VKD3D_EXPECT_TRUE(descriptor_count == 1))
        {
            /* Expected path. */
            vkd3d_memcpy_aligned_16_cached(
                    (void *)dst_descriptor_range_offset.ptr,
                    (const void *)src_descriptor_range_offset.ptr);
        }
        else
        {
            /* Rare path. */
            vkd3d_memcpy_aligned_cached(
                    (void *)dst_descriptor_range_offset.ptr,
                    (const void *)src_descriptor_range_offset.ptr,
                    16 * descriptor_count);
        }
    }
    else
    {
        device = unsafe_impl_from_ID3D12Device(iface);
        d3d12_device_copy_descriptors(device,
                1, &dst_descriptor_range_offset, &descriptor_count,
                1, &src_descriptor_range_offset, &descriptor_count,
                descriptor_heap_type);
    }
}

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptorsSimple_embedded_generic(d3d12_device_iface *iface,
        UINT descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE dst_descriptor_range_offset,
        const D3D12_CPU_DESCRIPTOR_HANDLE src_descriptor_range_offset,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    struct d3d12_device *device;
    TRACE("iface %p, descriptor_count %u, dst_descriptor_range_offset %#lx, "
          "src_descriptor_range_offset %#lx, descriptor_heap_type %#x.\n",
            iface, descriptor_count, dst_descriptor_range_offset.ptr, src_descriptor_range_offset.ptr,
            descriptor_heap_type);

    device = unsafe_impl_from_ID3D12Device(iface);

    if (VKD3D_EXPECT_TRUE(descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV))
    {
        d3d12_desc_copy_embedded_resource(
                dst_descriptor_range_offset.ptr,
                src_descriptor_range_offset.ptr,
                device->bindless_state.descriptor_buffer_cbv_srv_uav_size * descriptor_count);
    }
    else if (descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        vkd3d_memcpy_aligned_cached(
                (void *)dst_descriptor_range_offset.ptr,
                (const void *)src_descriptor_range_offset.ptr,
                device->bindless_state.descriptor_buffer_sampler_size * descriptor_count);
    }
    else
    {
        d3d12_device_copy_descriptors(device,
                1, &dst_descriptor_range_offset, &descriptor_count,
                1, &src_descriptor_range_offset, &descriptor_count,
                descriptor_heap_type);
    }
}

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptorsSimple_default(d3d12_device_iface *iface,
        UINT descriptor_count, const D3D12_CPU_DESCRIPTOR_HANDLE dst_descriptor_range_offset,
        const D3D12_CPU_DESCRIPTOR_HANDLE src_descriptor_range_offset,
        D3D12_DESCRIPTOR_HEAP_TYPE descriptor_heap_type)
{
    struct d3d12_device *device;
    TRACE("iface %p, descriptor_count %u, dst_descriptor_range_offset %#lx, "
            "src_descriptor_range_offset %#lx, descriptor_heap_type %#x.\n",
            iface, descriptor_count, dst_descriptor_range_offset.ptr, src_descriptor_range_offset.ptr,
            descriptor_heap_type);

    device = unsafe_impl_from_ID3D12Device(iface);

    if (VKD3D_EXPECT_TRUE(descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
            descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER))
    {
        /* Not quite as fast as embedded, but still not bad. */
        d3d12_device_copy_descriptors_cbv_srv_uav_sampler(device,
                dst_descriptor_range_offset, src_descriptor_range_offset,
                descriptor_heap_type,
                descriptor_count);
    }
    else
    {
        d3d12_device_copy_descriptors(device,
                1, &dst_descriptor_range_offset, &descriptor_count,
                1, &src_descriptor_range_offset, &descriptor_count,
                descriptor_heap_type);
    }
}

static D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE d3d12_device_GetResourceAllocationInfo1(d3d12_device_iface *iface,
        D3D12_RESOURCE_ALLOCATION_INFO *info, UINT visible_mask, UINT count, const D3D12_RESOURCE_DESC *resource_descs,
        D3D12_RESOURCE_ALLOCATION_INFO1 *resource_infos);

static D3D12_RESOURCE_ALLOCATION_INFO * STDMETHODCALLTYPE d3d12_device_GetResourceAllocationInfo(
        d3d12_device_iface *iface, D3D12_RESOURCE_ALLOCATION_INFO *info, UINT visible_mask,
        UINT count, const D3D12_RESOURCE_DESC *resource_descs)
{
    TRACE("iface %p, info %p, visible_mask 0x%08x, count %u, resource_descs %p.\n",
            iface, info, visible_mask, count, resource_descs);

    return d3d12_device_GetResourceAllocationInfo1(iface, info, visible_mask, count, resource_descs, NULL);
}

static D3D12_HEAP_PROPERTIES * STDMETHODCALLTYPE d3d12_device_GetCustomHeapProperties(d3d12_device_iface *iface,
        D3D12_HEAP_PROPERTIES *heap_properties, UINT node_mask, D3D12_HEAP_TYPE heap_type)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    bool coherent;

    TRACE("iface %p, heap_properties %p, node_mask 0x%08x, heap_type %#x.\n",
            iface, heap_properties, node_mask, heap_type);

    debug_ignored_node_mask(node_mask);

    heap_properties->Type = D3D12_HEAP_TYPE_CUSTOM;

    switch (heap_type)
    {
        case D3D12_HEAP_TYPE_DEFAULT:
            heap_properties->CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
            heap_properties->MemoryPoolPreference = d3d12_device_is_uma(device, NULL)
                    ? D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1;
            break;

        case D3D12_HEAP_TYPE_UPLOAD:
            heap_properties->CPUPageProperty = d3d12_device_is_uma(device, &coherent) && coherent
                    ? D3D12_CPU_PAGE_PROPERTY_WRITE_BACK : D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
            heap_properties->MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
            break;

        case D3D12_HEAP_TYPE_READBACK:
            heap_properties->CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
            heap_properties->MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
            break;

        case D3D12_HEAP_TYPE_GPU_UPLOAD:
            heap_properties->CPUPageProperty = d3d12_device_is_uma(device, &coherent) && coherent
                    ? D3D12_CPU_PAGE_PROPERTY_WRITE_BACK : D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
            heap_properties->MemoryPoolPreference = d3d12_device_is_uma(device, NULL)
                    ? D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1;
            break;

        default:
            FIXME("Unhandled heap type %#x.\n", heap_type);
            break;
    };

    heap_properties->CreationNodeMask = 1;
    heap_properties->VisibleNodeMask = 1;

    return heap_properties;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommittedResource1(d3d12_device_iface *iface,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        ID3D12ProtectedResourceSession *protected_session,
        REFIID iid, void **resource);

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommittedResource(d3d12_device_iface *iface,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, REFIID iid, void **resource)
{
    TRACE("iface %p, heap_properties %p, heap_flags %#x, desc %p, initial_state %#x, "
            "optimized_clear_value %p, iid %s, resource %p.\n",
            iface, heap_properties, heap_flags, desc, initial_state,
            optimized_clear_value, debugstr_guid(iid), resource);

    return d3d12_device_CreateCommittedResource1(iface, heap_properties, heap_flags,
            desc, initial_state, optimized_clear_value, NULL, iid, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateHeap1(d3d12_device_iface *iface,
        const D3D12_HEAP_DESC *desc, ID3D12ProtectedResourceSession *protected_session,
        REFIID iid, void **heap);

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateHeap(d3d12_device_iface *iface,
        const D3D12_HEAP_DESC *desc, REFIID iid, void **heap)
{
    TRACE("iface %p, desc %p, iid %s, heap %p.\n",
            iface, desc, debugstr_guid(iid), heap);

    return d3d12_device_CreateHeap1(iface, desc, NULL, iid, heap);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreatePlacedResource1(d3d12_device_iface *iface,
        ID3D12Heap *heap, UINT64 heap_offset, const D3D12_RESOURCE_DESC1 *resource_desc,
        D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *optimized_clear_value,
        REFIID riid, void **resource);

static HRESULT STDMETHODCALLTYPE d3d12_device_CreatePlacedResource(d3d12_device_iface *iface,
        ID3D12Heap *heap, UINT64 heap_offset,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, REFIID iid, void **resource)
{
    D3D12_RESOURCE_DESC1 desc1;

    TRACE("iface %p, heap %p, heap_offset %#"PRIx64", desc %p, initial_state %#x, "
            "optimized_clear_value %p, riid %s, resource %p.\n",
            iface, heap, heap_offset, desc, initial_state,
            optimized_clear_value, debugstr_guid(iid), resource);

    d3d12_resource_promote_desc(desc, &desc1);

    return d3d12_device_CreatePlacedResource1(iface, heap, heap_offset, &desc1,
            initial_state, optimized_clear_value, iid, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateReservedResource1(d3d12_device_iface *iface,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *optimized_clear_value,
        ID3D12ProtectedResourceSession *protected_session, REFIID iid, void **resource);

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateReservedResource(d3d12_device_iface *iface,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, REFIID iid, void **resource)
{
    TRACE("iface %p, desc %p, initial_state %#x, optimized_clear_value %p, iid %s, resource %p.\n",
            iface, desc, initial_state, optimized_clear_value, debugstr_guid(iid), resource);

    return d3d12_device_CreateReservedResource1(iface, desc, initial_state,
            optimized_clear_value, NULL, iid, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateSharedHandle(d3d12_device_iface *iface,
        ID3D12DeviceChild *object, const SECURITY_ATTRIBUTES *attributes, DWORD access,
        const WCHAR *name, HANDLE *handle)
{
#ifdef _WIN32
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    struct DxvkSharedTextureMetadata metadata;
    ID3D12Resource *resource_iface;
    ID3D12Fence *fence_iface;

    vk_procs = &device->vk_procs;

    TRACE("iface %p, object %p, attributes %p, access %#x, name %s, handle %p\n",
            iface, object, attributes, access, debugstr_w(name), handle);

    if (SUCCEEDED(ID3D12DeviceChild_QueryInterface(object, &IID_ID3D12Resource, (void**)&resource_iface)))
    {
        struct d3d12_resource *resource = impl_from_ID3D12Resource(resource_iface);
        VkMemoryGetWin32HandleInfoKHR win32_handle_info;
        VkResult vr;

        if (!(resource->heap_flags & D3D12_HEAP_FLAG_SHARED))
        {
            ID3D12Resource_Release(resource_iface);
            return DXGI_ERROR_INVALID_CALL;
        }

        if (attributes)
            FIXME("attributes %p not handled.\n", attributes);
        if (access)
            FIXME("access %#x not handled.\n", access);
        if (name)
            FIXME("name %s not handled.\n", debugstr_w(name));

        win32_handle_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
        win32_handle_info.pNext = NULL;
        win32_handle_info.memory = resource->mem.device_allocation.vk_memory;
        win32_handle_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

        vr = VK_CALL(vkGetMemoryWin32HandleKHR(device->vk_device, &win32_handle_info, handle));

        if (vr == VK_SUCCESS)
        {
            if (resource->desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
            {
                FIXME("Shared texture metadata structure only supports 2D textures.");
            }
            else
            {
                metadata.Width = resource->desc.Width;
                metadata.Height = resource->desc.Height;
                metadata.MipLevels = resource->desc.MipLevels;
                metadata.ArraySize = resource->desc.DepthOrArraySize;
                metadata.Format = resource->desc.Format;
                metadata.SampleDesc = resource->desc.SampleDesc;
                metadata.Usage = D3D11_USAGE_DEFAULT;
                metadata.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                metadata.CPUAccessFlags = 0;
                metadata.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

                if (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
                    metadata.BindFlags |= D3D11_BIND_RENDER_TARGET;
                if (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
                    metadata.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
                if (resource->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
                    metadata.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
                if (resource->desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)
                    metadata.BindFlags &= ~D3D11_BIND_SHADER_RESOURCE;

                if (!vkd3d_set_shared_metadata(*handle, &metadata, sizeof(metadata)))
                    ERR("Failed to set metadata for shared resource, importing created handle will fail.\n");
            }
        }

        ID3D12Resource_Release(resource_iface);
        return vr ? E_FAIL : S_OK;
    }

    if (SUCCEEDED(ID3D12DeviceChild_QueryInterface(object, &IID_ID3D12Fence, (void**)&fence_iface)))
    {
        VkSemaphoreGetWin32HandleInfoKHR win32_handle_info;
        struct d3d12_shared_fence *fence;
        VkResult vr;

        if (!is_shared_ID3D12Fence(fence_iface))
        {
            ID3D12Fence_Release(fence_iface);
            return DXGI_ERROR_INVALID_CALL;
        }

        fence = shared_impl_from_ID3D12Fence(fence_iface);

        if (attributes)
            FIXME("attributes %p not handled\n", attributes);
        if (access)
            FIXME("access %#x not handled\n", access);
        if (name)
            FIXME("name %s not handled\n", debugstr_w(name));

        win32_handle_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
        win32_handle_info.pNext = NULL;
        win32_handle_info.semaphore = fence->timeline_semaphore;
        win32_handle_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;

        vr = VK_CALL(vkGetSemaphoreWin32HandleKHR(device->vk_device, &win32_handle_info, handle));

        ID3D12Fence_Release(fence_iface);
        return vr ? E_FAIL : S_OK;
    }

    FIXME("Creating shared handle for type of object %p unsupported.\n", object);
    return E_NOTIMPL;
#else
    FIXME("CreateSharedHandle can only be implemented in native Win32.\n");
    return E_NOTIMPL;
#endif
}

#ifdef _WIN32
static inline bool handle_is_kmt_style(HANDLE handle)
{
    return ((ULONG_PTR)handle & 0x40000000) && ((ULONG_PTR)handle - 2) % 4 == 0;
}
#endif

static HRESULT STDMETHODCALLTYPE d3d12_device_OpenSharedHandle(d3d12_device_iface *iface,
        HANDLE handle, REFIID riid, void **object)
{
#ifdef _WIN32
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    const struct vkd3d_vk_device_procs *vk_procs;
    HRESULT hr;

    vk_procs = &device->vk_procs;

    TRACE("iface %p, handle %p, riid %s, object %p\n",
            iface, handle, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12Resource))
    {
        struct DxvkSharedTextureMetadata metadata;
        D3D12_HEAP_PROPERTIES heap_props;
        struct d3d12_resource *resource;
        D3D12_RESOURCE_DESC1 desc;
        bool kmt_handle = false;

        if (handle_is_kmt_style(handle))
        {
            handle = vkd3d_open_kmt_handle(handle);
            kmt_handle = true;

            if (handle == INVALID_HANDLE_VALUE)
            {
                WARN("Failed to open KMT-style ID3D12Resource shared handle.\n");
                *object = NULL;
                return E_INVALIDARG;
            }
        }

        if (!vkd3d_get_shared_metadata(handle, &metadata, sizeof(metadata), NULL))
        {
            WARN("Failed to get ID3D12Resource shared handle metadata.\n");
            if (kmt_handle)
                CloseHandle(handle);

            *object = NULL;
            return E_INVALIDARG;
        }

        memset(&desc, 0, sizeof(desc));
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = metadata.Width;
        desc.Height = metadata.Height;
        desc.DepthOrArraySize = metadata.ArraySize;
        desc.MipLevels = metadata.MipLevels;
        desc.Format = metadata.Format;
        desc.SampleDesc = metadata.SampleDesc;

        switch (metadata.TextureLayout)
        {
            case D3D11_TEXTURE_LAYOUT_UNDEFINED: desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; break;
            case D3D11_TEXTURE_LAYOUT_ROW_MAJOR: desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; break;
            case D3D11_TEXTURE_LAYOUT_64K_STANDARD_SWIZZLE: desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE; break;
            default: desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        }

        if (metadata.BindFlags & D3D11_BIND_RENDER_TARGET)
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        if (metadata.BindFlags & D3D11_BIND_DEPTH_STENCIL)
        {
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            if (!(metadata.BindFlags & D3D11_BIND_SHADER_RESOURCE))
                desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        }
        else
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

        if (metadata.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        desc.SamplerFeedbackMipRegion.Width = 0;
        desc.SamplerFeedbackMipRegion.Height = 0;
        desc.SamplerFeedbackMipRegion.Depth = 0;

        heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap_props.CreationNodeMask = 0;
        heap_props.VisibleNodeMask = 0;

        hr = d3d12_resource_create_committed(device, &desc, &heap_props,
                D3D12_HEAP_FLAG_SHARED, D3D12_RESOURCE_STATE_COMMON, NULL, 0, NULL, handle, &resource);
        if (kmt_handle)
            CloseHandle(handle);

        if (FAILED(hr))
        {
            WARN("Failed to open shared ID3D12Resource, hr %#x.\n", hr);
            *object = NULL;
            return hr;
        }

        return return_interface(&resource->ID3D12Resource_iface, &IID_ID3D12Resource, riid, object);
    }

    if (IsEqualGUID(riid, &IID_ID3D12Fence))
    {
        VkImportSemaphoreWin32HandleInfoKHR import_info;
        struct d3d12_shared_fence *fence;
        VkResult vr;

        hr = d3d12_shared_fence_create(device, 0, D3D12_FENCE_FLAG_SHARED, &fence);

        if (FAILED(hr))
        {
            WARN("Failed to create object for imported ID3D12Fence, hr %#x.\n", hr);
            *object = NULL;
            return hr;
        }

        import_info.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR;
        import_info.pNext = NULL;
        import_info.semaphore = fence->timeline_semaphore;
        import_info.flags = 0;
        import_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
        import_info.handle = handle;
        import_info.name = NULL;

        vr = VK_CALL(vkImportSemaphoreWin32HandleKHR(device->vk_device, &import_info));

        if (vr != VK_SUCCESS)
        {
            WARN("Failed to open shared ID3D12Fence, vr %d.\n", vr);
            ID3D12Fence1_Release(&fence->ID3D12Fence_iface);
            *object = NULL;
            return E_FAIL;
        }

        return return_interface(&fence->ID3D12Fence_iface, &IID_ID3D12Fence, riid, object);
    }

    FIXME("Opening shared handle type %s unsupported\n", debugstr_guid(riid));
    return E_NOTIMPL;
#else
    FIXME("OpenSharedhandle can only be implemented in native Win32.\n");
    return E_NOTIMPL;
#endif
}

static HRESULT STDMETHODCALLTYPE d3d12_device_OpenSharedHandleByName(d3d12_device_iface *iface,
        const WCHAR *name, DWORD access, HANDLE *handle)
{
    FIXME("iface %p, name %s, access %#x, handle %p stub!\n",
            iface, debugstr_w(name), access, handle);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_MakeResident(d3d12_device_iface *iface,
        UINT object_count, ID3D12Pageable * const *objects)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    TRACE("iface %p, object_count %u, objects %p\n",
            iface, object_count, objects);

    if (device->device_info.pageable_device_memory_features.pageableDeviceLocalMemory)
    {
        uint32_t i;

        for (i = 0; i < object_count; i++)
        {
            VkDeviceMemory memory = VK_NULL_HANDLE;
            D3D12_RESIDENCY_PRIORITY priority;
            ID3D12Resource *resource_iface;
            ID3D12Heap *heap_iface;

            if (SUCCEEDED(ID3D12Pageable_QueryInterface(objects[i], &IID_ID3D12Heap, (void**)&heap_iface)))
            {
                struct d3d12_heap *heap_object = impl_from_ID3D12Heap(heap_iface);

                if (heap_object->priority.allows_dynamic_residency)
                {
                    memory = heap_object->allocation.device_allocation.vk_memory;
                    spinlock_acquire(&heap_object->priority.spinlock);
                    priority = heap_object->priority.d3d12priority;
                    heap_object->priority.residency_count++;
                    spinlock_release(&heap_object->priority.spinlock);
                }

                ID3D12Heap_Release(heap_iface);
            }
            else if (SUCCEEDED(ID3D12Pageable_QueryInterface(objects[i], &IID_ID3D12Resource, (void**)&resource_iface)))
            {
                struct d3d12_resource *resource_object = impl_from_ID3D12Resource(resource_iface);

                if (resource_object->priority.allows_dynamic_residency)
                {
                    memory = resource_object->mem.device_allocation.vk_memory;
                    spinlock_acquire(&resource_object->priority.spinlock);
                    priority = resource_object->priority.d3d12priority;
                    resource_object->priority.residency_count++;
                    spinlock_release(&resource_object->priority.spinlock);
                }

                ID3D12Resource_Release(resource_iface);
            }

            if (memory)
            {
                VK_CALL(vkSetDeviceMemoryPriorityEXT(device->vk_device, memory, vkd3d_convert_to_vk_prio(priority)));
            }
        }
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_EnqueueMakeResident(d3d12_device_iface *iface,
        D3D12_RESIDENCY_FLAGS flags, UINT num_objects, ID3D12Pageable *const *objects,
        ID3D12Fence *fence_to_signal, UINT64 fence_value_to_signal)
{
    TRACE("iface %p, flags %#x, num_objects %u, objects %p, fence_to_signal %p, fence_value_to_signal %"PRIu64"\n",
            iface, flags, num_objects, objects, fence_to_signal, fence_value_to_signal);

    /* note: we ignore flags/D3D12_RESIDENCY_FLAG_DENY_OVERBUDGET; it involves
       knowing if the app will be made over-budget.  We act as if it won't.  Could perhaps
       use VK_EXT_memory_budget but don't have an app in-hand that clearly cares. */
    d3d12_device_MakeResident(iface, num_objects, objects);

    /* we don't block anyway - signal the fence immediately */
    return ID3D12Fence_Signal(fence_to_signal, fence_value_to_signal);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_Evict(d3d12_device_iface *iface,
        UINT object_count, ID3D12Pageable * const *objects)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    TRACE("iface %p, object_count %u, objects %p\n",
            iface, object_count, objects);

    if (device->device_info.pageable_device_memory_features.pageableDeviceLocalMemory)
    {
        uint32_t i;

        for (i = 0; i < object_count; i++)
        {
            VkDeviceMemory memory = VK_NULL_HANDLE;
            ID3D12Resource *resource_iface;
            bool now_evicted = false;
            ID3D12Heap *heap_iface;

            if (SUCCEEDED(ID3D12Pageable_QueryInterface(objects[i], &IID_ID3D12Heap, (void**)&heap_iface)))
            {
                struct d3d12_heap *heap_object = impl_from_ID3D12Heap(heap_iface);

                if (heap_object->priority.allows_dynamic_residency)
                {
                    memory = heap_object->allocation.device_allocation.vk_memory;
                    spinlock_acquire(&heap_object->priority.spinlock);
                    now_evicted = (0 == --heap_object->priority.residency_count);
                    spinlock_release(&heap_object->priority.spinlock);
                }

                ID3D12Heap_Release(heap_iface);
            }
            else if (SUCCEEDED(ID3D12Pageable_QueryInterface(objects[i], &IID_ID3D12Resource, (void**)&resource_iface)))
            {
                struct d3d12_resource *resource_object = impl_from_ID3D12Resource(resource_iface);

                if (resource_object->priority.allows_dynamic_residency)
                {
                    memory = resource_object->mem.device_allocation.vk_memory;
                    spinlock_acquire(&resource_object->priority.spinlock);
                    now_evicted = (0 == --resource_object->priority.residency_count);
                    spinlock_release(&resource_object->priority.spinlock);
                }

                ID3D12Resource_Release(resource_iface);
            }

            if (memory && now_evicted)
            {
                VK_CALL(vkSetDeviceMemoryPriorityEXT(device->vk_device, memory, 0.0f));
            }
        }
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateFence(d3d12_device_iface *iface,
        UINT64 initial_value, D3D12_FENCE_FLAGS flags, REFIID riid, void **fence)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_shared_fence *shared_object;
    struct d3d12_fence *object;
    HRESULT hr;

    TRACE("iface %p, intial_value %#"PRIx64", flags %#x, riid %s, fence %p.\n",
            iface, initial_value, flags, debugstr_guid(riid), fence);

    if (flags & D3D12_FENCE_FLAG_SHARED)
    {
        if (SUCCEEDED(hr = d3d12_shared_fence_create(device, initial_value, flags, &shared_object)))
            return return_interface(&shared_object->ID3D12Fence_iface, &IID_ID3D12Fence, riid, fence);

        if (hr != E_NOTIMPL)
            return hr;

        FIXME("Shared fences not supported by Vulkan host, returning regular fence.\n");
    }

    if (FAILED(hr = d3d12_fence_create(device, initial_value, flags, &object)))
        return hr;

    return return_interface(&object->ID3D12Fence_iface, &IID_ID3D12Fence, riid, fence);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_GetDeviceRemovedReason(d3d12_device_iface *iface)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p.\n", iface);

    return vkd3d_atomic_uint32_load_explicit(&device->removed_reason, vkd3d_memory_order_acquire);
}

static void STDMETHODCALLTYPE d3d12_device_GetCopyableFootprints1(d3d12_device_iface *iface,
        const D3D12_RESOURCE_DESC1 *desc, UINT first_sub_resource, UINT sub_resource_count,
        UINT64 base_offset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts, UINT *row_counts,
        UINT64 *row_sizes, UINT64 *total_bytes);

static void STDMETHODCALLTYPE d3d12_device_GetCopyableFootprints(d3d12_device_iface *iface,
        const D3D12_RESOURCE_DESC *desc, UINT first_sub_resource, UINT sub_resource_count,
        UINT64 base_offset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts,
        UINT *row_counts, UINT64 *row_sizes, UINT64 *total_bytes)
{
    D3D12_RESOURCE_DESC1 desc1;

    TRACE("iface %p, desc %p, first_sub_resource %u, sub_resource_count %u, base_offset %#"PRIx64", "
            "layouts %p, row_counts %p, row_sizes %p, total_bytes %p.\n",
            iface, desc, first_sub_resource, sub_resource_count, base_offset,
            layouts, row_counts, row_sizes, total_bytes);

    d3d12_resource_promote_desc(desc, &desc1);

    d3d12_device_GetCopyableFootprints1(iface, &desc1, first_sub_resource,
            sub_resource_count, base_offset, layouts, row_counts, row_sizes, total_bytes);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateQueryHeap(d3d12_device_iface *iface,
        const D3D12_QUERY_HEAP_DESC *desc, REFIID iid, void **heap)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_query_heap *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, iid %s, heap %p.\n",
            iface, desc, debugstr_guid(iid), heap);

    if (FAILED(hr = d3d12_query_heap_create(device, desc, &object)))
        return hr;

    return return_interface(&object->ID3D12QueryHeap_iface, &IID_ID3D12QueryHeap, iid, heap);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetStablePowerState(d3d12_device_iface *iface, BOOL enable)
{
    FIXME("iface %p, enable %#x stub!\n", iface, enable);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandSignature(d3d12_device_iface *iface,
        const D3D12_COMMAND_SIGNATURE_DESC *desc, ID3D12RootSignature *root_signature_iface,
        REFIID iid, void **command_signature)
{
    struct d3d12_root_signature *root_signature = impl_from_ID3D12RootSignature(root_signature_iface);
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_command_signature *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, root_signature %p, iid %s, command_signature %p.\n",
            iface, desc, root_signature, debugstr_guid(iid), command_signature);

    if (FAILED(hr = d3d12_command_signature_create(device, root_signature, desc, &object)))
        return hr;

    return return_interface(&object->ID3D12CommandSignature_iface,
            &IID_ID3D12CommandSignature, iid, command_signature);
}

static void STDMETHODCALLTYPE d3d12_device_GetResourceTiling(d3d12_device_iface *iface,
        ID3D12Resource *resource, UINT *tile_count, D3D12_PACKED_MIP_INFO *packed_mip_info,
        D3D12_TILE_SHAPE *tile_shape, UINT *tiling_count, UINT first_tiling,
        D3D12_SUBRESOURCE_TILING *tilings)
{
    struct d3d12_sparse_info *sparse = &impl_from_ID3D12Resource(resource)->sparse;
    unsigned int max_tiling_count, i;

    TRACE("iface %p, resource %p, tile_count %p, packed_mip_info %p, "
            "tile_shape %p, tiling_count %p, first_tiling %u, tilings %p.\n",
            iface, resource, tile_count, packed_mip_info, tile_shape, tiling_count,
            first_tiling, tilings);

    if (tile_count)
        *tile_count = sparse->tile_count;

    if (packed_mip_info)
        *packed_mip_info = sparse->packed_mips;

    if (tile_shape)
        *tile_shape = sparse->tile_shape;

    if (tiling_count)
    {
        max_tiling_count = sparse->tiling_count - min(first_tiling, sparse->tiling_count);
        max_tiling_count = min(max_tiling_count, *tiling_count);

        for (i = 0; i < max_tiling_count; i++)
            tilings[i] = sparse->tilings[first_tiling + i];

        *tiling_count = max_tiling_count;
    }
}

static LUID * STDMETHODCALLTYPE d3d12_device_GetAdapterLuid(d3d12_device_iface *iface, LUID *luid)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, luid %p.\n", iface, luid);

    *luid = device->adapter_luid;

    return luid;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreatePipelineLibrary(d3d12_device_iface *iface,
        const void *blob, SIZE_T blob_size, REFIID iid, void **lib)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_pipeline_library *pipeline_library;
    uint32_t flags;
    HRESULT hr;

    TRACE("iface %p, blob %p, blob_size %lu, iid %s, lib %p.\n",
            iface, blob, blob_size, debugstr_guid(iid), lib);

    flags = 0;

    /* If we use a disk cache, it is somewhat meaningless to use application pipeline libraries
     * to store SPIR-V / blob.
     * We only need to store metadata so we can implement the API correctly w.r.t. return values, and
     * PSO reload. */
    if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_NO_SERIALIZE_SPIRV) &&
            !device->disk_cache.library)
        flags |= VKD3D_PIPELINE_LIBRARY_FLAG_SAVE_FULL_SPIRV;

    /* If we're using global pipeline caches, these are irrelevant.
     * Do not use pipeline library blobs at all. */
    if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_GLOBAL_PIPELINE_CACHE))
    {
        flags |= VKD3D_PIPELINE_LIBRARY_FLAG_SAVE_PSO_BLOB |
                VKD3D_PIPELINE_LIBRARY_FLAG_USE_PIPELINE_CACHE_UUID;
    }

    if (FAILED(hr = d3d12_pipeline_library_create(device, blob, blob_size,
            flags, &pipeline_library)))
        return hr;

    if (lib)
    {
        return return_interface(&pipeline_library->ID3D12PipelineLibrary_iface,
                &IID_ID3D12PipelineLibrary, iid, lib);
    }
    else
    {
        ID3D12PipelineLibrary1_Release(&pipeline_library->ID3D12PipelineLibrary_iface);
        return S_FALSE;
    }
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetEventOnMultipleFenceCompletion(d3d12_device_iface *iface,
        ID3D12Fence *const *fences, const UINT64 *values, UINT fence_count,
        D3D12_MULTIPLE_FENCE_WAIT_FLAGS flags, HANDLE event)
{
    enum vkd3d_waiting_event_type wait_type;
    vkd3d_native_sync_handle handle;
    uint32_t *payload = NULL;
    unsigned int i;
    HRESULT hr;

    TRACE("iface %p, fences %p, values %p, fence_count %u, flags %#x, event %p.\n",
            iface, fences, values, fence_count, flags, event);

    if (flags && flags != D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY)
    {
        FIXME("Unsupported wait flags %#x.\n", flags);
        return E_INVALIDARG;
    }

    if (!fence_count)
        return E_INVALIDARG;

    if (fence_count == 1)
        return ID3D12Fence_SetEventOnCompletion(fences[0], values[0], event);

    wait_type = (flags & D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY)
            ? VKD3D_WAITING_EVENT_MULTI_ANY : VKD3D_WAITING_EVENT_MULTI_ALL;

    if (!event && wait_type == VKD3D_WAITING_EVENT_MULTI_ANY)
    {
        /* We need to stall the calling thread if any fence gets signaled.
         * Create a temporary event and wait for it later. */
        hr = vkd3d_native_sync_handle_create(0, VKD3D_NATIVE_SYNC_HANDLE_TYPE_EVENT, &handle);

        if (FAILED(hr))
        {
            ERR("Failed to create temporary event, hr %#x.\n", hr);
            return hr;
        }
    }
    else
    {
        handle = vkd3d_native_sync_handle_wrap(event, VKD3D_NATIVE_SYNC_HANDLE_TYPE_EVENT);
    }

    /* Each fence that processes this wait will decrement the payload
     * counter by 1, and only signal the event if the signal bit is set */
    payload = vkd3d_malloc(sizeof(*payload));
    *payload = fence_count | VKD3D_WAITING_EVENT_SIGNAL_BIT;

    for (i = 0; i < fence_count; i++)
    {
        hr = d3d12_fence_iface_set_native_sync_handle_on_completion_explicit(
                fences[i], wait_type, values[i], handle, payload);

        if (FAILED(hr))
        {
            /* Ensure that the event does not get signaled by any fence
             * that we may already have added it to. */
            uint32_t payload_value = vkd3d_atomic_uint32_and(payload, ~VKD3D_WAITING_EVENT_SIGNAL_BIT, vkd3d_memory_order_relaxed);

            /* If WAIT_ANY is used, the event may already have been signaled.
             * Return success in that case since signaling the event on error
             * would be unexpected. */
            if (!(payload_value & VKD3D_WAITING_EVENT_SIGNAL_BIT))
                hr = S_OK;

            if (!vkd3d_atomic_uint32_sub(payload, fence_count - i, vkd3d_memory_order_relaxed))
                vkd3d_free(payload);

            goto fail;
        }
    }

    if (!event && wait_type == VKD3D_WAITING_EVENT_MULTI_ANY)
    {
        hr = vkd3d_native_sync_handle_acquire(handle) ? S_OK : E_FAIL;
        vkd3d_native_sync_handle_destroy(handle);

        if (FAILED(hr))
            ERR("Failed to wait for temporary event.\n");

        return hr;
    }

    return S_OK;

fail:
    if (!event && wait_type == VKD3D_WAITING_EVENT_MULTI_ANY)
        vkd3d_native_sync_handle_destroy(handle);

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetResidencyPriority(d3d12_device_iface *iface,
        UINT object_count, ID3D12Pageable *const *objects, const D3D12_RESIDENCY_PRIORITY *priorities)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, object_count %u, objects %p, priorities %p\n",
            iface, object_count, objects, priorities);

    if (device->device_info.pageable_device_memory_features.pageableDeviceLocalMemory)
    {
        const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
        uint32_t i;

        for (i = 0; i < object_count; i++)
        {
            D3D12_RESIDENCY_PRIORITY priority = priorities[i];
            VkDeviceMemory memory = VK_NULL_HANDLE;
            ID3D12Resource *resource_iface;
            ID3D12Heap *heap_iface;

            if (SUCCEEDED(ID3D12Pageable_QueryInterface(objects[i], &IID_ID3D12Heap, (void**)&heap_iface)))
            {
                struct d3d12_heap *heap_object = impl_from_ID3D12Heap(heap_iface);

                if (heap_object->priority.allows_dynamic_residency)
                {
                    spinlock_acquire(&heap_object->priority.spinlock);
                    heap_object->priority.d3d12priority = priority;
                    if (heap_object->priority.residency_count)
                    {
                        memory = heap_object->allocation.device_allocation.vk_memory;
                    }
                    spinlock_release(&heap_object->priority.spinlock);
                }

                ID3D12Heap_Release(heap_iface);
            }
            else if (SUCCEEDED(ID3D12Pageable_QueryInterface(objects[i], &IID_ID3D12Resource, (void**)&resource_iface)))
            {
                struct d3d12_resource *resource_object = impl_from_ID3D12Resource(resource_iface);

                if (resource_object->priority.allows_dynamic_residency)
                {
                    spinlock_acquire(&resource_object->priority.spinlock);
                    resource_object->priority.d3d12priority = priority;
                    if (resource_object->priority.residency_count)
                    {
                        memory = resource_object->mem.device_allocation.vk_memory;
                    }
                    spinlock_release(&resource_object->priority.spinlock);
                }

                ID3D12Resource_Release(resource_iface);
            }

            if (memory)
            {
                VK_CALL(vkSetDeviceMemoryPriorityEXT(device->vk_device, memory, vkd3d_convert_to_vk_prio(priority)));
            }
        }
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreatePipelineState(d3d12_device_iface *iface,
        const D3D12_PIPELINE_STATE_STREAM_DESC *desc, REFIID riid, void **pipeline_state)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_pipeline_state_desc pipeline_desc;
    struct d3d12_pipeline_state *object;
    VkPipelineBindPoint pipeline_type;
    HRESULT hr;

    TRACE("iface %p, desc %p, riid %s, pipeline_state %p.\n",
            iface, desc, debugstr_guid(riid), pipeline_state);

    if (FAILED(hr = vkd3d_pipeline_state_desc_from_d3d12_stream_desc(&pipeline_desc, desc, &pipeline_type)))
        return hr;

    if (FAILED(hr = d3d12_pipeline_state_create(device, pipeline_type, &pipeline_desc, &object)))
        return hr;

    return return_interface(&object->ID3D12PipelineState_iface,
            &IID_ID3D12PipelineState, riid, pipeline_state);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_OpenExistingHeapFromAddress(d3d12_device_iface *iface,
        void *address, REFIID riid, void **heap)
{
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION info;
    struct d3d12_device *device;
    struct d3d12_heap *object;
    D3D12_HEAP_DESC heap_desc;
    size_t allocation_size;
    HRESULT hr;

    TRACE("iface %p, address %p, riid %s, heap %p\n",
          iface, address, debugstr_guid(riid), heap);

    if (!VirtualQuery(address, &info, sizeof(info)))
    {
        ERR("Failed to VirtualQuery host pointer.\n");
        return E_INVALIDARG;
    }

    /* Allocation base must equal address. */
    if (info.AllocationBase != address)
        return E_INVALIDARG;
    if (info.BaseAddress != info.AllocationBase)
        return E_INVALIDARG;

    /* All pages must be committed. */
    if (info.State != MEM_COMMIT)
        return E_INVALIDARG;

    /* We can only have one region of page protection types.
     * Verify this by querying the end of the range. */
    allocation_size = info.RegionSize;
    if (VirtualQuery((uint8_t *)address + allocation_size, &info, sizeof(info)) &&
            info.AllocationBase == address)
    {
        /* All pages must have same protections, so there cannot be multiple regions for VirtualQuery. */
        return E_INVALIDARG;
    }

    device = impl_from_ID3D12Device(iface);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS |
            (address ? (D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER) : 0);
    heap_desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heap_desc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap_desc.Properties.CreationNodeMask = 1;
    heap_desc.Properties.VisibleNodeMask = 1;
    heap_desc.SizeInBytes = allocation_size;

    if (FAILED(hr = d3d12_heap_create(device, &heap_desc, address, &object)))
    {
        *heap = NULL;
        return hr;
    }

    return return_interface(&object->ID3D12Heap_iface, &IID_ID3D12Heap, riid, heap);
#else
    FIXME("OpenExistingHeapFromAddress can only be implemented in native Win32.\n");
    return E_NOTIMPL;
#endif
}

static HRESULT STDMETHODCALLTYPE d3d12_device_OpenExistingHeapFromFileMapping(d3d12_device_iface *iface,
        HANDLE file_mapping, REFIID riid, void **heap)
{
#ifdef _WIN32
    void *addr;
    HRESULT hr;
    TRACE("iface %p, file_mapping %p, riid %s, heap %p\n",
          iface, file_mapping, debugstr_guid(riid), heap);

    /* 0 size maps everything. */
    addr = MapViewOfFile(file_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!addr)
        return E_INVALIDARG;

    hr = d3d12_device_OpenExistingHeapFromAddress(iface, addr, riid, heap);
    UnmapViewOfFile(addr);
    return hr;
#else
    FIXME("OpenExistingHeapFromFileMapping can only be implemented in native Win32.\n");
    return E_NOTIMPL;
#endif
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandList1(d3d12_device_iface *iface,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS flags,
        REFIID riid, void **command_list)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    d3d12_command_list_iface *command_list_iface;
    HRESULT hr;

    TRACE("iface %p, node_mask 0x%08x, type %#x, flags %#x, riid %s, command_list %p.\n",
            iface, node_mask, type, flags, debugstr_guid(riid), command_list);

    if (type == D3D12_COMMAND_LIST_TYPE_BUNDLE)
    {
        struct d3d12_bundle *object;
        if (FAILED(hr = d3d12_bundle_create(device, node_mask, type, &object)))
            return hr;
        command_list_iface = &object->ID3D12GraphicsCommandList_iface;
    }
    else
    {
        struct d3d12_command_list *object;
        if (FAILED(hr = d3d12_command_list_create(device, node_mask, type, &object)))
            return hr;
        command_list_iface = &object->ID3D12GraphicsCommandList_iface;
    }

    return return_interface(command_list_iface, &IID_ID3D12GraphicsCommandList, riid, command_list);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateProtectedResourceSession(d3d12_device_iface *iface,
        const D3D12_PROTECTED_RESOURCE_SESSION_DESC *desc, REFIID iid, void **session)
{
    FIXME("iface %p, desc %p, iid %s, session %p stub!\n",
            iface, desc, debugstr_guid(iid), session);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommittedResource2(d3d12_device_iface *iface,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags, const D3D12_RESOURCE_DESC1 *desc,
        D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *optimized_clear_value,
        ID3D12ProtectedResourceSession *protected_session, REFIID iid, void **resource);

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommittedResource1(d3d12_device_iface *iface,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        ID3D12ProtectedResourceSession *protected_session,
        REFIID iid, void **resource)
{
    D3D12_RESOURCE_DESC1 desc1;

    TRACE("iface %p, heap_properties %p, heap_flags %#x,  desc %p, initial_state %#x, "
            "optimized_clear_value %p, protected_session %p, iid %s, resource %p.\n",
            iface, heap_properties, heap_flags, desc, initial_state,
            optimized_clear_value, protected_session, debugstr_guid(iid), resource);

    d3d12_resource_promote_desc(desc, &desc1);

    return d3d12_device_CreateCommittedResource2(iface, heap_properties, heap_flags, &desc1,
            initial_state, optimized_clear_value, protected_session, iid, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateHeap1(d3d12_device_iface *iface,
        const D3D12_HEAP_DESC *desc, ID3D12ProtectedResourceSession *protected_session,
        REFIID iid, void **heap)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_heap *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, protected_session %p, iid %s, heap %p.\n",
            iface, desc, protected_session, debugstr_guid(iid), heap);

    if (protected_session)
        FIXME("Ignoring protected session %p.\n", protected_session);

    if (FAILED(hr = d3d12_heap_create(device, desc, NULL, &object)))
    {
        *heap = NULL;
        return hr;
    }

    return return_interface(&object->ID3D12Heap_iface, &IID_ID3D12Heap, iid, heap);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateReservedResource1(d3d12_device_iface *iface,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *optimized_clear_value,
        ID3D12ProtectedResourceSession *protected_session, REFIID iid, void **resource)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_resource *object;
    D3D12_RESOURCE_DESC1 desc1;
    HRESULT hr;

    TRACE("iface %p, desc %p, initial_state %#x, optimized_clear_value %p, protected_session %p, iid %s, resource %p.\n",
            iface, desc, initial_state, optimized_clear_value, protected_session, debugstr_guid(iid), resource);

    if (protected_session)
        FIXME("Ignoring protected session %p.\n", protected_session);

    d3d12_resource_promote_desc(desc, &desc1);

    if (FAILED(hr = d3d12_resource_create_reserved(device, &desc1,
            initial_state, optimized_clear_value, 0, NULL, &object)))
        return hr;

    return return_interface(&object->ID3D12Resource_iface, &IID_ID3D12Resource, iid, resource);
}

static D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE d3d12_device_GetResourceAllocationInfo2(d3d12_device_iface *iface,
        D3D12_RESOURCE_ALLOCATION_INFO *info, UINT visible_mask, UINT count, const D3D12_RESOURCE_DESC1 *resource_descs,
        D3D12_RESOURCE_ALLOCATION_INFO1 *resource_infos);

static D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE d3d12_device_GetResourceAllocationInfo1(d3d12_device_iface *iface,
        D3D12_RESOURCE_ALLOCATION_INFO *info, UINT visible_mask, UINT count, const D3D12_RESOURCE_DESC *resource_descs,
        D3D12_RESOURCE_ALLOCATION_INFO1 *resource_infos)
{
    D3D12_RESOURCE_DESC1 local_descs[16];
    D3D12_RESOURCE_DESC1 *desc1;
    unsigned int i;

    TRACE("iface %p, info %p, visible_mask 0x%08x, count %u, resource_descs %p, resource_infos %p.\n",
            iface, info, visible_mask, count, resource_descs, resource_infos);

    if (count > ARRAY_SIZE(local_descs))
        desc1 = vkd3d_malloc(sizeof(*desc1) * count);
    else
    {
        /* Avoid a compiler warning */
        memset(local_descs, 0, sizeof(local_descs));
        desc1 = local_descs;
    }


    for (i = 0; i < count; i++)
        d3d12_resource_promote_desc(&resource_descs[i], &desc1[i]);

    d3d12_device_GetResourceAllocationInfo2(iface, info, visible_mask, count, desc1, resource_infos);

    if (desc1 != local_descs)
        vkd3d_free(desc1);

    return info;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateLifetimeTracker(d3d12_device_iface *iface,
        ID3D12LifetimeOwner *owner, REFIID iid, void **tracker)
{
    FIXME("iface %p, owner %p, iid %s, tracker %p stub!\n",
            iface, owner, debugstr_guid(iid), tracker);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d3d12_device_RemoveDevice(d3d12_device_iface *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_EnumerateMetaCommands(d3d12_device_iface *iface,
        UINT *count, D3D12_META_COMMAND_DESC *descs)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, count %p, descs %p.\n", iface, count, descs);

    if (!count)
        return E_INVALIDARG;

    vkd3d_enumerate_meta_commands(device, count, descs);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_EnumerateMetaCommandParameters(d3d12_device_iface *iface,
        REFGUID command_id, D3D12_META_COMMAND_PARAMETER_STAGE stage, UINT *total_size,
        UINT *param_count, D3D12_META_COMMAND_PARAMETER_DESC *param_descs)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, command_id %s, stage %u, total_size %p, param_count %p, param_descs %p.\n",
            iface, debugstr_guid(command_id), stage, total_size, param_count, param_descs);

    if (!vkd3d_enumerate_meta_command_parameters(device,
            command_id, stage, total_size, param_count, param_descs))
        return E_INVALIDARG;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateMetaCommand(d3d12_device_iface *iface,
        REFGUID command_id, UINT node_mask, const void *param_data, SIZE_T param_size,
        REFIID iid, void **meta_command)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_meta_command *object;
    HRESULT hr;

    TRACE("iface %p, command_id %s, node_mask %#x, param_data %p, param_size %lu, iid %s, meta_command %p.\n",
            iface, debugstr_guid(command_id), node_mask, param_data, param_size, debugstr_guid(iid), meta_command);

    if (FAILED(hr = d3d12_meta_command_create(device, command_id, param_data, param_size, &object)))
        return hr;

    return return_interface(&object->ID3D12MetaCommand_iface, &IID_ID3D12MetaCommand, iid, meta_command);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateStateObject(d3d12_device_iface *iface,
        const D3D12_STATE_OBJECT_DESC *desc, REFIID iid, void **state_object)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    bool has_workgraph;
    unsigned int i;
    HRESULT hr;

    TRACE("iface %p, desc %p, iid %s, state_object %p!\n",
            iface, desc, debugstr_guid(iid), state_object);

    /* Both RT pipelines and workgraphs go through this same interface.
     * If we detect any workgraph-only state, we dispatch to workgraphs creator. */
    has_workgraph = false;
    for (i = 0; i < desc->NumSubobjects; i++)
    {
        if (desc->pSubobjects[i].Type == D3D12_STATE_SUBOBJECT_TYPE_WORK_GRAPH)
        {
            has_workgraph = true;
            break;
        }
    }

    if (desc->Type == D3D12_STATE_OBJECT_TYPE_COLLECTION && has_workgraph)
    {
        FIXME("COLLECTION of workgraphs currently not supported.\n");
        return E_NOTIMPL;
    }

    if (desc->Type == D3D12_STATE_OBJECT_TYPE_EXECUTABLE)
    {
        FIXME("Workgraph PSOs currently not supported.\n");
        return E_NOTIMPL;
    }
    else
    {
        struct d3d12_rt_state_object *state;
        if (FAILED(hr = d3d12_rt_state_object_create(device, desc, NULL, &state)))
            return hr;
        return return_interface(&state->ID3D12StateObject_iface, &IID_ID3D12StateObject, iid, state_object);
    }
}

static void d3d12_device_get_raytracing_opacity_micromap_array_prebuild_info(struct d3d12_device *device,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *desc,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO *info)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkMicromapUsageEXT usages_stack[VKD3D_BUILD_INFO_STACK_COUNT];
    VkMicromapBuildSizesInfoEXT size_info;
    VkMicromapBuildInfoEXT build_info;
    VkMicromapUsageEXT *usages;
    uint32_t usages_count;

    if (!d3d12_device_supports_ray_tracing_tier_1_2(device))
    {
        ERR("Opacity micromap is not supported. Calling this is invalid.\n");
        memset(info, 0, sizeof(*info));
        return;
    }

    usages_count = desc->pOpacityMicromapArrayDesc->NumOmmHistogramEntries;

    if (usages_count > VKD3D_BUILD_INFO_STACK_COUNT)
        usages = vkd3d_malloc(usages_count * sizeof(*usages));
    else
        usages = usages_stack;

    if (!vkd3d_opacity_micromap_convert_inputs(device, desc, &build_info, usages))
    {
        ERR("Failed to convert inputs.\n");
        memset(info, 0, sizeof(*info));
        goto cleanup;
    }

    memset(&size_info, 0, sizeof(size_info));
    size_info.sType = VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT;

    VK_CALL(vkGetMicromapBuildSizesEXT(device->vk_device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_info, &size_info));

    info->ResultDataMaxSizeInBytes = size_info.micromapSize;
    info->ScratchDataSizeInBytes = size_info.buildScratchSize;
    info->UpdateScratchDataSizeInBytes = 0;

    TRACE("ResultDataMaxSizeInBytes: %"PRIu64".\n", (uint64_t)info->ResultDataMaxSizeInBytes);
    TRACE("ScratchDataSizeInBytes: %"PRIu64".\n", (uint64_t)info->ScratchDataSizeInBytes);

cleanup:

    if (usages_count > VKD3D_BUILD_INFO_STACK_COUNT)
        vkd3d_free(usages);
}

static void STDMETHODCALLTYPE d3d12_device_GetRaytracingAccelerationStructurePrebuildInfo(d3d12_device_iface *iface,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *desc,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO *info)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    VkAccelerationStructureTrianglesOpacityMicromapEXT omms_stack[VKD3D_BUILD_INFO_STACK_COUNT];
    VkAccelerationStructureGeometryKHR geometries_stack[VKD3D_BUILD_INFO_STACK_COUNT];
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    uint32_t primitive_counts_stack[VKD3D_BUILD_INFO_STACK_COUNT];
    VkAccelerationStructureTrianglesOpacityMicromapEXT *omms;
    VkAccelerationStructureBuildGeometryInfoKHR build_info;
    VkAccelerationStructureBuildSizesInfoKHR size_info;
    VkAccelerationStructureGeometryKHR *geometries;
    uint32_t *primitive_counts;
    uint32_t geometry_count;

    TRACE("iface %p, desc %p, info %p!\n", iface, desc, info);

    if (!d3d12_device_supports_ray_tracing_tier_1_0(device))
    {
        ERR("Acceleration structure is not supported. Calling this is invalid.\n");
        memset(info, 0, sizeof(*info));
        return;
    }

    if (desc->Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY)
    {
        d3d12_device_get_raytracing_opacity_micromap_array_prebuild_info(device, desc, info);
        return;
    }

    geometry_count = vkd3d_acceleration_structure_get_geometry_count(desc);
    primitive_counts = primitive_counts_stack;
    geometries = geometries_stack;
    omms = omms_stack;

    if (geometry_count > VKD3D_BUILD_INFO_STACK_COUNT)
    {
        primitive_counts = vkd3d_malloc(geometry_count * sizeof(*primitive_counts));
        geometries = vkd3d_malloc(geometry_count * sizeof(*geometries));
        omms = vkd3d_malloc(geometry_count * sizeof(*omms));
    }

    if (!vkd3d_acceleration_structure_convert_inputs(device,
            desc, &build_info, geometries, omms, NULL, primitive_counts))
    {
        ERR("Failed to convert inputs.\n");
        memset(info, 0, sizeof(*info));
        goto cleanup;
    }

    build_info.pGeometries = geometries;

    memset(&size_info, 0, sizeof(size_info));
    size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    VK_CALL(vkGetAccelerationStructureBuildSizesKHR(device->vk_device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info,
            primitive_counts, &size_info));

    info->ResultDataMaxSizeInBytes = size_info.accelerationStructureSize;
    info->ScratchDataSizeInBytes = size_info.buildScratchSize;
    info->UpdateScratchDataSizeInBytes = size_info.updateScratchSize;

    TRACE("ResultDataMaxSizeInBytes: %"PRIu64".\n", info->ResultDataMaxSizeInBytes);
    TRACE("ScratchDatSizeInBytes: %"PRIu64".\n", info->ScratchDataSizeInBytes);
    TRACE("UpdateScratchDataSizeInBytes: %"PRIu64".\n", info->UpdateScratchDataSizeInBytes);

cleanup:

    if (geometry_count > VKD3D_BUILD_INFO_STACK_COUNT)
    {
        vkd3d_free(primitive_counts);
        vkd3d_free(geometries);
        vkd3d_free(omms);
    }
}

static D3D12_DRIVER_MATCHING_IDENTIFIER_STATUS STDMETHODCALLTYPE d3d12_device_CheckDriverMatchingIdentifier(d3d12_device_iface *iface,
        D3D12_SERIALIZED_DATA_TYPE serialized_data_type, const D3D12_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER *identifier)
{
    FIXME("iface %p, serialized_data_type %u, identifier %p stub!\n",
            iface, serialized_data_type, identifier);

    if (serialized_data_type != D3D12_SERIALIZED_DATA_RAYTRACING_ACCELERATION_STRUCTURE)
        return D3D12_DRIVER_MATCHING_IDENTIFIER_UNSUPPORTED_TYPE;

    return D3D12_DRIVER_MATCHING_IDENTIFIER_UNRECOGNIZED;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetBackgroundProcessingMode(d3d12_device_iface *iface,
        D3D12_BACKGROUND_PROCESSING_MODE mode, D3D12_MEASUREMENTS_ACTION action, HANDLE event,
        BOOL further_measurements)
{
    FIXME("iface %p, mode %u, action %u, event %p, further_measurements %#x stub!\n",
            iface, mode, action, event, further_measurements);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_AddToStateObject(d3d12_device_iface *iface,
        const D3D12_STATE_OBJECT_DESC *addition,
        ID3D12StateObject *parent_state, REFIID riid, void **new_state_object)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_rt_state_object *parent;
    struct d3d12_rt_state_object *state;
    HRESULT hr;

    TRACE("iface %p, addition %p, state_object %p, riid %s, new_state_object %p stub!\n",
            iface, addition, parent_state, debugstr_guid(riid), new_state_object);

    parent = rt_impl_from_ID3D12StateObject(parent_state);
    if (FAILED(hr = d3d12_rt_state_object_add(device, addition, parent, &state)))
        return hr;

    return return_interface(&state->ID3D12StateObject_iface, &IID_ID3D12StateObject, riid, new_state_object);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateProtectedResourceSession1(d3d12_device_iface *iface,
        const D3D12_PROTECTED_RESOURCE_SESSION_DESC1 *desc, REFIID riid, void **session)
{
    FIXME("iface %p, desc %p, riid %s, session %p stub!\n",
            iface, desc, debugstr_guid(riid), session);

    return E_NOTIMPL;
}

static D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE d3d12_device_GetResourceAllocationInfo3(d3d12_device_iface *iface,
        D3D12_RESOURCE_ALLOCATION_INFO *info, UINT visible_mask, UINT count, const D3D12_RESOURCE_DESC1 *resource_descs,
        const UINT32 *p_num_castable_formats, const DXGI_FORMAT * const *pp_castable_formats,
        D3D12_RESOURCE_ALLOCATION_INFO1 *resource_infos)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    uint64_t requested_alignment, resource_offset;
    D3D12_RESOURCE_ALLOCATION_INFO resource_info;
    bool hasMsaaResource = false;
    unsigned int i;

    TRACE("iface %p, info %p, visible_mask 0x%08x, count %u, resource_descs %p.\n",
            iface, info, visible_mask, count, resource_descs);

    debug_ignored_node_mask(visible_mask);

    info->SizeInBytes = 0;
    info->Alignment = 0;

    for (i = 0; i < count; i++)
    {
        const D3D12_RESOURCE_DESC1 *desc = &resource_descs[i];
        const DXGI_FORMAT *p_castable_formats = NULL;
        UINT num_castable_formats = 0;
        hasMsaaResource |= desc->SampleDesc.Count > 1;

        if (p_num_castable_formats)
            num_castable_formats = p_num_castable_formats[i];
        if (pp_castable_formats)
            p_castable_formats = pp_castable_formats[i];

        if (FAILED(d3d12_resource_validate_desc(desc, num_castable_formats, p_castable_formats, device)))
        {
            WARN("Invalid resource desc.\n");
            goto invalid;
        }

        if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            resource_info.SizeInBytes = desc->Width;
            resource_info.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        }
        else
        {
            if (FAILED(vkd3d_get_image_allocation_info(device, desc,
                    num_castable_formats, p_castable_formats,
                    &resource_info)))
            {
                WARN("Failed to get allocation info for texture.\n");
                goto invalid;
            }

            requested_alignment = desc->Alignment
                    ? desc->Alignment : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            resource_info.Alignment = max(resource_info.Alignment, requested_alignment);
        }

        resource_info.SizeInBytes = align(resource_info.SizeInBytes, resource_info.Alignment);
        resource_offset = align(info->SizeInBytes, resource_info.Alignment);

        if (resource_infos)
        {
            resource_infos[i].Offset = resource_offset;
            resource_infos[i].SizeInBytes = resource_info.SizeInBytes;
            resource_infos[i].Alignment = resource_info.Alignment;
        }

        info->SizeInBytes = resource_offset + resource_info.SizeInBytes;
        info->Alignment = max(info->Alignment, resource_info.Alignment);
    }

    return info;

invalid:
    info->SizeInBytes = ~(uint64_t)0;

    /* FIXME: Should we support D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT for small MSSA resources? */
    if (hasMsaaResource)
        info->Alignment = D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;
    else
        info->Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

    return info;
}

static D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE d3d12_device_GetResourceAllocationInfo2(d3d12_device_iface *iface,
        D3D12_RESOURCE_ALLOCATION_INFO *info, UINT visible_mask, UINT count, const D3D12_RESOURCE_DESC1 *resource_descs,
        D3D12_RESOURCE_ALLOCATION_INFO1 *resource_infos)
{
    TRACE("iface %p, info %p, visible_mask 0x%08x, count %u, resource_descs %p.\n",
            iface, info, visible_mask, count, resource_descs);

    return d3d12_device_GetResourceAllocationInfo3(iface, info, visible_mask, count, resource_descs, NULL, NULL, resource_infos);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommittedResource2(d3d12_device_iface *iface,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags, const D3D12_RESOURCE_DESC1 *desc,
        D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *optimized_clear_value,
        ID3D12ProtectedResourceSession *protected_session, REFIID iid, void **resource)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_resource *object;
    HRESULT hr;

    TRACE("iface %p, heap_properties %p, heap_flags %#x, desc %p, initial_state %#x, "
            "optimized_clear_value %p, protected_session %p, iid %s, resource %p.\n",
            iface, heap_properties, heap_flags, desc, initial_state,
            optimized_clear_value, protected_session, debugstr_guid(iid), resource);

    if (protected_session)
        FIXME("Ignoring protected session %p.\n", protected_session);

    if (FAILED(hr = d3d12_resource_create_committed(device, desc, heap_properties,
            heap_flags, initial_state, optimized_clear_value, 0, NULL, NULL, &object)))
    {
        if (resource)
            *resource = NULL;
        return hr;
    }

    return return_interface(&object->ID3D12Resource_iface, &IID_ID3D12Resource, iid, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreatePlacedResource1(d3d12_device_iface *iface,
        ID3D12Heap *heap, UINT64 heap_offset, const D3D12_RESOURCE_DESC1 *resource_desc,
        D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *optimized_clear_value,
        REFIID iid, void **resource)
{
    struct d3d12_heap *heap_object = impl_from_ID3D12Heap(heap);
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_resource *object;
    HRESULT hr;

    TRACE("iface %p, heap %p, heap_offset %#"PRIx64", desc %p, initial_state %#x, "
            "optimized_clear_value %p, iid %s, resource %p.\n",
            iface, heap, heap_offset, resource_desc, initial_state,
            optimized_clear_value, debugstr_guid(iid), resource);

    if (FAILED(hr = d3d12_resource_create_placed(device, resource_desc, heap_object,
            heap_offset, initial_state, optimized_clear_value, 0, NULL, &object)))
        return hr;

    return return_interface(&object->ID3D12Resource_iface, &IID_ID3D12Resource, iid, resource);
}

static void d3d12_device_create_sampler_feedback_desc(D3D12_UNORDERED_ACCESS_VIEW_DESC *uav_desc,
        struct d3d12_resource *feedback)
{
    /* We really mean 64-bit here, but reusing the UAV path simplifies things. */
    memset(uav_desc, 0, sizeof(*uav_desc));
    uav_desc->Format = DXGI_FORMAT_R32G32_UINT;

    if (feedback && feedback->desc.DepthOrArraySize > 1)
    {
        uav_desc->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uav_desc->Texture2DArray.ArraySize = feedback->desc.DepthOrArraySize;
    }
    else
        uav_desc->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
}

static void STDMETHODCALLTYPE d3d12_device_CreateSamplerFeedbackUnorderedAccessView_default(d3d12_device_iface *iface,
        ID3D12Resource *target_resource, ID3D12Resource *feedback_resource, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_resource *feedback = impl_from_ID3D12Resource(feedback_resource);
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;

    TRACE("iface %p, target_resource %p, feedback_resource %p, descriptor %#lx\n",
            iface, target_resource, feedback_resource, descriptor.ptr);

    /* NULL paired resource means NULL descriptor.
     * https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html#null-feedback-map-binding-is-permitted */
    if (!target_resource)
        feedback = NULL;

    d3d12_device_create_sampler_feedback_desc(&uav_desc, feedback);
    d3d12_desc_create_uav(descriptor.ptr, device, feedback, NULL, &uav_desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateSamplerFeedbackUnorderedAccessView_embedded(d3d12_device_iface *iface,
        ID3D12Resource *target_resource, ID3D12Resource *feedback_resource, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_resource *feedback = impl_from_ID3D12Resource(feedback_resource);
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;

    TRACE("iface %p, target_resource %p, feedback_resource %p, descriptor %#lx\n",
            iface, target_resource, feedback_resource, descriptor.ptr);

    /* NULL paired resource means NULL descriptor.
     * https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html#null-feedback-map-binding-is-permitted */
    if (!target_resource)
        feedback = NULL;

    d3d12_device_create_sampler_feedback_desc(&uav_desc, feedback);
    d3d12_desc_create_uav_embedded(descriptor.ptr, device, feedback, NULL, &uav_desc);
}

static void STDMETHODCALLTYPE d3d12_device_GetCopyableFootprints1(d3d12_device_iface *iface,
        const D3D12_RESOURCE_DESC1 *desc, UINT first_sub_resource, UINT sub_resource_count,
        UINT64 base_offset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts, UINT *row_counts,
        UINT64 *row_sizes, UINT64 *total_bytes)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    static const struct vkd3d_format vkd3d_format_unknown
            = {DXGI_FORMAT_UNKNOWN, VK_FORMAT_UNDEFINED, 1, 1, 1, 1, VK_IMAGE_ASPECT_COLOR_BIT, 1};

    unsigned int i, sub_resource_idx, row_count, row_size, row_pitch;
    unsigned int num_subresources_per_plane, plane_idx;
    struct vkd3d_format_footprint plane_footprint;
    unsigned int num_planes, num_subresources;
    const struct vkd3d_format *format;
    uint64_t offset, size, total;
    VkExtent3D extent;

    TRACE("iface %p, desc %p, first_sub_resource %u, sub_resource_count %u, base_offset %#"PRIx64", "
            "layouts %p, row_counts %p, row_sizes %p, total_bytes %p.\n",
            iface, desc, first_sub_resource, sub_resource_count, base_offset,
            layouts, row_counts, row_sizes, total_bytes);

    if (layouts)
        memset(layouts, 0xff, sizeof(*layouts) * sub_resource_count);
    if (row_counts)
        memset(row_counts, 0xff, sizeof(*row_counts) * sub_resource_count);
    if (row_sizes)
        memset(row_sizes, 0xff, sizeof(*row_sizes) * sub_resource_count);

    total = ~(uint64_t)0;

    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        format = &vkd3d_format_unknown;
    }
    else if (!(format = vkd3d_format_from_d3d12_resource_desc(device, desc, 0)))
    {
        WARN("Invalid format %#x.\n", desc->Format);
        goto end;
    }

    if (FAILED(d3d12_resource_validate_desc(desc, 0, NULL, device)))
    {
        WARN("Invalid resource desc.\n");
        goto end;
    }

    num_planes = format->plane_count;
    num_subresources_per_plane = d3d12_resource_desc_get_sub_resource_count_per_plane(desc);
    num_subresources = d3d12_resource_desc_get_sub_resource_count(device, desc);

    if (first_sub_resource >= num_subresources
            || sub_resource_count > num_subresources - first_sub_resource)
    {
        WARN("Invalid sub-resource range %u-%u for resource.\n", first_sub_resource, sub_resource_count);
        goto end;
    }

    offset = 0;
    total = 0;
    for (i = 0; i < sub_resource_count; ++i)
    {
        sub_resource_idx = first_sub_resource + i;
        extent = d3d12_resource_desc_get_subresource_extent(desc, format, sub_resource_idx);

        plane_idx = sub_resource_idx / num_subresources_per_plane;
        plane_footprint = vkd3d_format_footprint_for_plane(format, plane_idx);

        extent.width = align(extent.width, plane_footprint.block_width);
        extent.height = align(extent.height, plane_footprint.block_height);

        row_count = extent.height / plane_footprint.block_height;
        row_size = (extent.width / plane_footprint.block_width) * plane_footprint.block_byte_count;

        /* For whatever reason, we need to use 512 bytes of alignment for depth-stencil formats.
         * This is not documented, but it is observed behavior on both NV and WARP drivers.
         * See test_get_copyable_footprints_planar(). */
        row_pitch = align(row_size, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * num_planes);

        if (layouts)
        {
            layouts[i].Offset = base_offset + offset;
            layouts[i].Footprint.Format = plane_footprint.dxgi_format;
            layouts[i].Footprint.Width = extent.width;
            layouts[i].Footprint.Height = extent.height;
            layouts[i].Footprint.Depth = extent.depth;
            layouts[i].Footprint.RowPitch = row_pitch;
        }
        if (row_counts)
            row_counts[i] = row_count;
        if (row_sizes)
            row_sizes[i] = row_size;

        size = max(0, row_count - 1) * row_pitch + row_size;
        size = max(0, extent.depth - 1) * align(size, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * num_planes) + size;

        total = offset + size;
        offset = align(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    }

end:
    if (total_bytes)
        *total_bytes = total;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateShaderCacheSession(d3d12_device_iface *iface,
        const D3D12_SHADER_CACHE_SESSION_DESC *desc, REFIID iid, void **session)
{
    FIXME("iface %p, desc %p, iid %s, session %p stub!\n",
            iface, desc, debugstr_guid(iid), session);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_ShaderCacheControl(d3d12_device_iface *iface,
        D3D12_SHADER_CACHE_KIND_FLAGS kinds, D3D12_SHADER_CACHE_CONTROL_FLAGS control)
{
    FIXME("iface %p, kinds %#x, control %#x stub!\n", iface, kinds, control);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandQueue1(d3d12_device_iface *iface,
        const D3D12_COMMAND_QUEUE_DESC *desc, REFIID creator_id, REFIID iid, void **command_queue)
{
    TRACE("iface %p, desc %p, creator_id %s, iid %s, command_queue %p.\n",
            iface, desc, debugstr_guid(creator_id), debugstr_guid(iid), command_queue);

    WARN("Ignoring creator id %s.\n", debugstr_guid(creator_id));

    return d3d12_device_CreateCommandQueue(iface, desc, iid, command_queue);
}

static D3D12_RESOURCE_STATES vkd3d_barrier_layout_to_resource_state(D3D12_BARRIER_LAYOUT layout, D3D12_RESOURCE_FLAGS flags)
{
    if (flags & D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE)
        return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

    /* We cannot make meaningful use of the DIRECT_QUEUE and COMPUTE_QUEUE special layouts.
     * There is no explicit ownership transfer in D3D12 like in Vulkan, so we have to use CONCURRENT either way. */
    switch (layout)
    {
        case D3D12_BARRIER_LAYOUT_COMMON:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COMMON:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COMMON:
            return D3D12_RESOURCE_STATE_COMMON;

        case D3D12_BARRIER_LAYOUT_GENERIC_READ:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_GENERIC_READ:
            return D3D12_RESOURCE_STATE_GENERIC_READ;

        case D3D12_BARRIER_LAYOUT_RENDER_TARGET:
            return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_UNORDERED_ACCESS:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_UNORDERED_ACCESS:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE:
            return D3D12_RESOURCE_STATE_DEPTH_WRITE;

        case D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ:
            return D3D12_RESOURCE_STATE_DEPTH_READ;

        case D3D12_BARRIER_LAYOUT_SHADER_RESOURCE:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE:
            return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_SHADER_RESOURCE:
            return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        case D3D12_BARRIER_LAYOUT_COPY_SOURCE:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_SOURCE:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_SOURCE:
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case D3D12_BARRIER_LAYOUT_COPY_DEST:
        case D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_COPY_DEST:
        case D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_COPY_DEST:
            return D3D12_RESOURCE_STATE_COPY_DEST;

        case D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE:
            return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
        case D3D12_BARRIER_LAYOUT_RESOLVE_DEST:
            return D3D12_RESOURCE_STATE_RESOLVE_DEST;
        case D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE:
            return D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;

        default:
            /* Generic fallback.
             * It is unclear what the intention of initial layout = D3D12_BARRIER_LAYOUT_UNDEFINED means.
             * To be defensive, fall back to COMMON here. The first use of such a resource must be a DISCARD
             * barrier either way which will elide any initial transition. */
            return D3D12_RESOURCE_STATE_COMMON;
    }
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommittedResource3(d3d12_device_iface *iface,
    const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags, 
    const D3D12_RESOURCE_DESC1 *desc, D3D12_BARRIER_LAYOUT initial_layout,
    const D3D12_CLEAR_VALUE *optimized_clear_value, ID3D12ProtectedResourceSession *protected_session,
    UINT32 num_castable_formats, const DXGI_FORMAT *castable_formats, REFIID iid, void **resource)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_resource *object;
    HRESULT hr;

    TRACE("iface %p, heap_properties %p, heap_flags %u, desc %p, initial_layout %u, "
            "optimized_clear_value %p, protected_session %p, num_castable_formats %u, "
            "castable_formats %p, iid %s, resource %p stub!\n", iface,
            heap_properties, heap_flags, desc, initial_layout, optimized_clear_value, 
            protected_session, num_castable_formats, castable_formats, debugstr_guid(iid), resource);

    if (protected_session)
        FIXME("Ignoring protected session %p.\n", protected_session);

    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && initial_layout != D3D12_BARRIER_LAYOUT_UNDEFINED)
    {
        WARN("Using non-undefined layout for buffer. This is not allowed.\n");
        return E_INVALIDARG;
    }

    /* For initial resource state, we cannot make use of the enhanced barrier layouts in any meaningful way.
     * Just collapse them into the equivalent legacy layout.
     * This can be refactored later if need be. */
    if (FAILED(hr = d3d12_resource_create_committed(device, desc, heap_properties,
            heap_flags, vkd3d_barrier_layout_to_resource_state(initial_layout, desc->Flags),
            optimized_clear_value, num_castable_formats, castable_formats, NULL, &object)))
    {
        if (resource)
            *resource = NULL;
        return hr;
    }

    return return_interface(&object->ID3D12Resource_iface, &IID_ID3D12Resource, iid, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreatePlacedResource2(d3d12_device_iface *iface,
    ID3D12Heap *heap, UINT64 heap_offset, const D3D12_RESOURCE_DESC1 *desc, D3D12_BARRIER_LAYOUT initial_layout,
    const D3D12_CLEAR_VALUE *optimized_clear_value, UINT32 num_castable_formats, 
    const DXGI_FORMAT *castable_formats, REFIID iid, void **resource)
{
    struct d3d12_heap *heap_object = impl_from_ID3D12Heap(heap);
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_resource *object;
    HRESULT hr;

    TRACE("iface %p, heap %p, heap_offset %#"PRIx64", desc %p, initial_layout %u, optimized_clear_value %p, "
            "num_castable_formats %u, castable_formats %p, iid %s, resource %p stub!\n", iface,
            heap, heap_offset, desc, initial_layout, optimized_clear_value, num_castable_formats,
            castable_formats, debugstr_guid(iid), resource);

    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER && initial_layout != D3D12_BARRIER_LAYOUT_UNDEFINED)
    {
        WARN("Using non-undefined layout for buffer. This is not allowed.\n");
        return E_INVALIDARG;
    }

    if (FAILED(hr = d3d12_resource_create_placed(device, desc, heap_object,
            heap_offset, vkd3d_barrier_layout_to_resource_state(initial_layout, desc->Flags),
            optimized_clear_value, num_castable_formats, castable_formats, &object)))
        return hr;

    return return_interface(&object->ID3D12Resource_iface, &IID_ID3D12Resource, iid, resource);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateReservedResource2(d3d12_device_iface *iface,
    const D3D12_RESOURCE_DESC *desc, D3D12_BARRIER_LAYOUT initial_layout, const D3D12_CLEAR_VALUE *optimized_clear_value,
    ID3D12ProtectedResourceSession *protected_session, UINT32 num_castable_formats,
    const DXGI_FORMAT *castable_formats, REFIID iid, void **resource)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_resource *object;
    D3D12_RESOURCE_DESC1 desc1;
    HRESULT hr;

    TRACE("iface %p, desc %p, initial_layout %u, optimized_clear_value %p, protected_session %p, "
            "num_castable_formats %u, castable_formats %p, iid %s, resource %p stub!\n", iface,
            desc, initial_layout, optimized_clear_value, protected_session, num_castable_formats,
            castable_formats, debugstr_guid(iid), resource);

    if (protected_session)
        FIXME("Ignoring protected session %p.\n", protected_session);

    d3d12_resource_promote_desc(desc, &desc1);

    if (FAILED(hr = d3d12_resource_create_reserved(device, &desc1,
            vkd3d_barrier_layout_to_resource_state(initial_layout, desc1.Flags), optimized_clear_value,
            num_castable_formats, castable_formats, &object)))
        return hr;

    return return_interface(&object->ID3D12Resource_iface, &IID_ID3D12Resource, iid, resource);
}

/* Gotta love C sometimes ... :') */
#define VKD3D_DECLARE_D3D12_DEVICE_VARIANT(name, create_desc, copy_desc_variant) \
CONST_VTBL struct ID3D12Device12Vtbl d3d12_device_vtbl_##name = \
{ \
    /* IUnknown methods */ \
    d3d12_device_QueryInterface, \
    d3d12_device_AddRef, \
    d3d12_device_Release, \
    /* ID3D12Object methods */ \
    d3d12_device_GetPrivateData, \
    d3d12_device_SetPrivateData, \
    d3d12_device_SetPrivateDataInterface, \
    (void *)d3d12_object_SetName, \
    /* ID3D12Device methods */ \
    d3d12_device_GetNodeCount, \
    d3d12_device_CreateCommandQueue, \
    d3d12_device_CreateCommandAllocator, \
    d3d12_device_CreateGraphicsPipelineState, \
    d3d12_device_CreateComputePipelineState, \
    d3d12_device_CreateCommandList, \
    d3d12_device_CheckFeatureSupport, \
    d3d12_device_CreateDescriptorHeap, \
    d3d12_device_GetDescriptorHandleIncrementSize, \
    d3d12_device_CreateRootSignature, \
    d3d12_device_CreateConstantBufferView_##create_desc, \
    d3d12_device_CreateShaderResourceView_##create_desc, \
    d3d12_device_CreateUnorderedAccessView_##create_desc, \
    d3d12_device_CreateRenderTargetView, \
    d3d12_device_CreateDepthStencilView, \
    d3d12_device_CreateSampler_##create_desc, \
    d3d12_device_CopyDescriptors, \
    d3d12_device_CopyDescriptorsSimple_##copy_desc_variant, \
    d3d12_device_GetResourceAllocationInfo, \
    d3d12_device_GetCustomHeapProperties, \
    d3d12_device_CreateCommittedResource, \
    d3d12_device_CreateHeap, \
    d3d12_device_CreatePlacedResource, \
    d3d12_device_CreateReservedResource, \
    d3d12_device_CreateSharedHandle, \
    d3d12_device_OpenSharedHandle, \
    d3d12_device_OpenSharedHandleByName, \
    d3d12_device_MakeResident, \
    d3d12_device_Evict, \
    d3d12_device_CreateFence, \
    d3d12_device_GetDeviceRemovedReason, \
    d3d12_device_GetCopyableFootprints, \
    d3d12_device_CreateQueryHeap, \
    d3d12_device_SetStablePowerState, \
    d3d12_device_CreateCommandSignature, \
    d3d12_device_GetResourceTiling, \
    d3d12_device_GetAdapterLuid, \
    /* ID3D12Device1 methods */ \
    d3d12_device_CreatePipelineLibrary, \
    d3d12_device_SetEventOnMultipleFenceCompletion, \
    d3d12_device_SetResidencyPriority, \
    /* ID3D12Device2 methods */ \
    d3d12_device_CreatePipelineState, \
    /* ID3D12Device3 methods */ \
    d3d12_device_OpenExistingHeapFromAddress, \
    d3d12_device_OpenExistingHeapFromFileMapping, \
    d3d12_device_EnqueueMakeResident, \
    /* ID3D12Device4 methods */ \
    d3d12_device_CreateCommandList1, \
    d3d12_device_CreateProtectedResourceSession, \
    d3d12_device_CreateCommittedResource1, \
    d3d12_device_CreateHeap1, \
    d3d12_device_CreateReservedResource1, \
    d3d12_device_GetResourceAllocationInfo1, \
    /* ID3D12Device5 methods */ \
    d3d12_device_CreateLifetimeTracker, \
    d3d12_device_RemoveDevice, \
    d3d12_device_EnumerateMetaCommands, \
    d3d12_device_EnumerateMetaCommandParameters, \
    d3d12_device_CreateMetaCommand, \
    d3d12_device_CreateStateObject, \
    d3d12_device_GetRaytracingAccelerationStructurePrebuildInfo, \
    d3d12_device_CheckDriverMatchingIdentifier, \
    /* ID3D12Device6 methods */ \
    d3d12_device_SetBackgroundProcessingMode, \
    /* ID3D12Device7 methods */ \
    d3d12_device_AddToStateObject, \
    d3d12_device_CreateProtectedResourceSession1, \
    /* ID3D12Device8 methods */ \
    d3d12_device_GetResourceAllocationInfo2, \
    d3d12_device_CreateCommittedResource2, \
    d3d12_device_CreatePlacedResource1, \
    d3d12_device_CreateSamplerFeedbackUnorderedAccessView_##create_desc, \
    d3d12_device_GetCopyableFootprints1, \
    /* ID3D12Device9 methods */ \
    d3d12_device_CreateShaderCacheSession, \
    d3d12_device_ShaderCacheControl, \
    d3d12_device_CreateCommandQueue1, \
    /* ID3D12Device10 methods */ \
    d3d12_device_CreateCommittedResource3, \
    d3d12_device_CreatePlacedResource2, \
    d3d12_device_CreateReservedResource2, \
    /* ID3D12Device11 methods */ \
    d3d12_device_CreateSampler2_##create_desc, \
    /* ID3D12Device12 methods */ \
    d3d12_device_GetResourceAllocationInfo3, \
}

VKD3D_DECLARE_D3D12_DEVICE_VARIANT(default, default, default);
VKD3D_DECLARE_D3D12_DEVICE_VARIANT(embedded_64_16_packed, embedded, embedded_64_16_packed);
VKD3D_DECLARE_D3D12_DEVICE_VARIANT(embedded_32_16_planar, embedded, embedded_32_16_planar);
VKD3D_DECLARE_D3D12_DEVICE_VARIANT(embedded_generic, embedded, embedded_generic);
VKD3D_DECLARE_D3D12_DEVICE_VARIANT(descriptor_buffer_16_16_4, default, descriptor_buffer_16_16_4);
VKD3D_DECLARE_D3D12_DEVICE_VARIANT(descriptor_buffer_64_64_32, default, descriptor_buffer_64_64_32);

#ifdef VKD3D_ENABLE_PROFILING
#include "device_profiled.h"
#endif

static D3D12_TILED_RESOURCES_TIER d3d12_device_determine_tiled_resources_tier(struct d3d12_device *device)
{
    const VkPhysicalDeviceSparseProperties *sparse_properties = &device->device_info.properties2.properties.sparseProperties;
    const VkPhysicalDeviceFeatures *features = &device->device_info.features2.features;

    if (!features->sparseBinding || !features->sparseResidencyAliased ||
            !features->sparseResidencyBuffer || !features->sparseResidencyImage2D ||
            !sparse_properties->residencyStandard2DBlockShape ||
            !device->queue_families[VKD3D_QUEUE_FAMILY_SPARSE_BINDING] ||
            !device->queue_families[VKD3D_QUEUE_FAMILY_SPARSE_BINDING]->queue_count)
        return D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;

    if (!features->shaderResourceResidency || !features->shaderResourceMinLod ||
            sparse_properties->residencyAlignedMipSize ||
            !sparse_properties->residencyNonResidentStrict ||
            !device->device_info.vulkan_1_2_properties.filterMinmaxSingleComponentFormats)
        return D3D12_TILED_RESOURCES_TIER_1;

    if (!features->sparseResidencyImage3D ||
            !sparse_properties->residencyStandard3DBlockShape)
        return D3D12_TILED_RESOURCES_TIER_2;

    return D3D12_TILED_RESOURCES_TIER_4;
}

static D3D12_CONSERVATIVE_RASTERIZATION_TIER d3d12_device_determine_conservative_rasterization_tier(struct d3d12_device *device)
{
    const VkPhysicalDeviceConservativeRasterizationPropertiesEXT *conservative_properties = &device->device_info.conservative_rasterization_properties;

    if (!device->vk_info.EXT_conservative_rasterization)
        return D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED;

    if (!conservative_properties->degenerateTrianglesRasterized)
        return D3D12_CONSERVATIVE_RASTERIZATION_TIER_1;

    if (!conservative_properties->fullyCoveredFragmentShaderInputVariable)
        return D3D12_CONSERVATIVE_RASTERIZATION_TIER_2;

    return D3D12_CONSERVATIVE_RASTERIZATION_TIER_3;
}

static bool d3d12_device_supports_rtas_formats(struct d3d12_device *device, const VkFormat *format, size_t count)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance->vk_procs;
    VkFormatProperties properties;
    size_t i;

    for (i = 0; i < count; i++)
    {
        VK_CALL(vkGetPhysicalDeviceFormatProperties(device->vk_physical_device, format[i], &properties));

        if (!(properties.bufferFeatures & VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR))
        {
            INFO("Vulkan format %u is not supported for RTAS VBO.\n", format[i]);
            return false;
        }
    }

    return true;
}

static D3D12_RAYTRACING_TIER d3d12_device_determine_ray_tracing_tier(struct d3d12_device *device)
{
    const struct vkd3d_physical_device_info *info = &device->device_info;
    D3D12_RAYTRACING_TIER tier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    bool supports_vbo_formats;

    /* Tier 1.0 formats. 1.1 adds:
     * - RGBA8_{U,S}NORM
     * - RG16_UNORM
     * - RGBA16_UNORM
     */
    static const VkFormat required_vbo_formats[] = {
        VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_R32G32B32_SFLOAT,
        VK_FORMAT_R16G16_SFLOAT,
        VK_FORMAT_R16G16_SNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16B16A16_SNORM,
    };

    static const VkFormat required_vbo_formats_tier_11[] = {
        VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_R16G16_UNORM,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R8G8_UNORM,
        VK_FORMAT_R8G8B8A8_SNORM,
        VK_FORMAT_R8G8_SNORM,
    };

    if (info->ray_tracing_pipeline_features.rayTracingPipeline &&
        info->acceleration_structure_features.accelerationStructure &&
        info->ray_tracing_pipeline_properties.maxRayHitAttributeSize >= D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES &&
        /* Group handle size must match exactly or ShaderRecord layout will not match.
         * Can potentially fixup local root signature if HandleSize < 32,
         * but Vulkan is essentially specced to match DXR directly. */
        info->ray_tracing_pipeline_properties.shaderGroupHandleSize == D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES &&
        info->ray_tracing_pipeline_properties.shaderGroupBaseAlignment <= D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT &&
        info->ray_tracing_pipeline_properties.shaderGroupHandleAlignment <= D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT)
    {
        /* DXR has 31 ray depth min-spec, but no content uses that. We will instead pass through implementations
         * which only expose 1 level of recursion and fail PSO compiles if they actually exceed device limits. */
        supports_vbo_formats = d3d12_device_supports_rtas_formats(device,
                required_vbo_formats, ARRAY_SIZE(required_vbo_formats));

        if (supports_vbo_formats)
        {
            INFO("DXR support enabled.\n");
            tier = D3D12_RAYTRACING_TIER_1_0;
        }
    }

    if (tier == D3D12_RAYTRACING_TIER_1_0 && info->ray_query_features.rayQuery &&
            info->ray_tracing_pipeline_features.rayTraversalPrimitiveCulling)
    {
        /* Try to enable DXR 1.1.
         * Hide this support behind a CONFIG flag for time being.
         * TODO: require VK_KHR_ray_tracing_maintenance1. */
        supports_vbo_formats = d3d12_device_supports_rtas_formats(device,
                required_vbo_formats_tier_11, ARRAY_SIZE(required_vbo_formats_tier_11));

        if (supports_vbo_formats)
        {
            INFO("DXR 1.1 support enabled.\n");
            tier = D3D12_RAYTRACING_TIER_1_1;
        }
    }

    if (tier == D3D12_RAYTRACING_TIER_1_1 && info->opacity_micromap_features.micromap)
    {
        INFO("DXR 1.2 support enabled.\n");
        tier = D3D12_RAYTRACING_TIER_1_2;
    }

    return tier;
}

static D3D12_RESOURCE_HEAP_TIER d3d12_device_determine_heap_tier(struct d3d12_device *device)
{
    const VkPhysicalDeviceLimits *limits = &device->device_info.properties2.properties.limits;
    const struct vkd3d_memory_info *mem_info = &device->memory_info;
    const struct vkd3d_memory_info_domain *non_cpu_domain;

    non_cpu_domain = &mem_info->non_cpu_accessible_domain;

    /* Heap Tier 2 requires us to be able to create a heap that supports all resource
     * categories at the same time, except RT/DS textures on UPLOAD/READBACK heaps.
     * Ignore CPU visible heaps since we only place buffers there. Textures are promoted to committed always. */
    if (limits->bufferImageGranularity > D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT ||
            !(non_cpu_domain->buffer_type_mask & non_cpu_domain->sampled_type_mask & non_cpu_domain->rt_ds_type_mask))
        return D3D12_RESOURCE_HEAP_TIER_1;

    return D3D12_RESOURCE_HEAP_TIER_2;
}

static bool d3d12_device_determine_additional_typed_uav_support(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support;
    size_t i;

    /* from https://docs.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads#supported-formats-and-api-calls */
    static const DXGI_FORMAT required_formats[] =
    {
        /* required */
        DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R32_UINT,
        DXGI_FORMAT_R32_SINT,
        /* supported as a set */
        DXGI_FORMAT_R32G32B32A32_FLOAT,
        DXGI_FORMAT_R32G32B32A32_UINT,
        DXGI_FORMAT_R32G32B32A32_SINT,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        DXGI_FORMAT_R16G16B16A16_UINT,
        DXGI_FORMAT_R16G16B16A16_SINT,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UINT,
        DXGI_FORMAT_R8G8B8A8_SINT,
        DXGI_FORMAT_R16_FLOAT,
        DXGI_FORMAT_R16_UINT,
        DXGI_FORMAT_R16_SINT,
        DXGI_FORMAT_R8_UNORM,
        DXGI_FORMAT_R8_UINT,
        DXGI_FORMAT_R8_SINT,
    };

    for (i = 0; i < ARRAY_SIZE(required_formats); i++)
    {
        format_support.Format = required_formats[i];
        d3d12_device_get_format_support(device, &format_support);
        if (!(format_support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD))
            return false;
    }
    return true;
}

static D3D12_MESH_SHADER_TIER d3d12_device_determine_mesh_shader_tier(struct d3d12_device *device)
{
    const VkPhysicalDeviceMeshShaderFeaturesEXT *mesh_shader_features = &device->device_info.mesh_shader_features;

    if (!mesh_shader_features->meshShader || !mesh_shader_features->taskShader)
        return D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;

    return D3D12_MESH_SHADER_TIER_1;
}

static D3D12_SAMPLER_FEEDBACK_TIER d3d12_device_determine_sampler_feedback_tier(struct d3d12_device *device)
{
    if (!device->device_info.features2.features.shaderInt64)
        return D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;
    if (!device->device_info.shader_image_atomic_int64_features.shaderImageInt64Atomics)
        return D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;

    /* Enough for FL 12.2. */
    return D3D12_SAMPLER_FEEDBACK_TIER_0_9;
}

uint32_t d3d12_device_get_max_descriptor_heap_size(struct d3d12_device *device, D3D12_DESCRIPTOR_HEAP_TYPE heap_type)
{
    /* For now, hard-code descriptor counts to the minimum numbers required by D3D12.
     * We could support more based on device capabilities, but that would change
     * pipeline layouts. */
    switch (heap_type)
    {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            return VKD3D_MIN_VIEW_DESCRIPTOR_COUNT;

        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            return VKD3D_MIN_SAMPLER_DESCRIPTOR_COUNT;

        default:
            WARN("Unhandled descriptor heap type %u.\n", heap_type);
            return 0u;
    }
}

static bool d3d12_device_supports_16bit_shader_ops(struct d3d12_device *device)
{
    return device->device_info.vulkan_1_2_features.shaderFloat16 &&
            device->device_info.vulkan_1_1_features.uniformAndStorageBuffer16BitAccess &&
            device->device_info.vulkan_1_2_properties.shaderDenormPreserveFloat16 &&
            device->device_info.vulkan_1_2_properties.denormBehaviorIndependence != VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE &&
            device->device_info.properties2.properties.limits.minStorageBufferOffsetAlignment <= 16;
}

static void d3d12_device_caps_init_feature_options(struct d3d12_device *device)
{
    const VkPhysicalDeviceFeatures *features = &device->device_info.features2.features;
    D3D12_FEATURE_DATA_D3D12_OPTIONS *options = &device->d3d12_caps.options;
    const struct vkd3d_vulkan_info *vk_info = &device->vk_info;
    bool supports_denorm_fp64;

    /* NV driver does not expose it, yet it seems to work. Similar story as FP32. */
    if (device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY)
        supports_denorm_fp64 = true;
    else
        supports_denorm_fp64 = device->device_info.vulkan_1_2_properties.shaderDenormPreserveFloat64;

    /* FP64 must preserve denorms (oof). */
    options->DoublePrecisionFloatShaderOps = features->shaderFloat64 &&
            supports_denorm_fp64 &&
            device->device_info.vulkan_1_2_properties.denormBehaviorIndependence != VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;

    options->OutputMergerLogicOp = features->logicOp;
    /* Ignored in DXBC, but properly supported in DXIL if device supports 16-bit ops */
    options->MinPrecisionSupport = d3d12_device_supports_16bit_shader_ops(device)
            ? D3D12_SHADER_MIN_PRECISION_SUPPORT_16_BIT : D3D12_SHADER_MIN_PRECISION_SUPPORT_NONE;
    options->TiledResourcesTier = d3d12_device_determine_tiled_resources_tier(device);
    options->ResourceBindingTier = D3D12_RESOURCE_BINDING_TIER_3;
    options->PSSpecifiedStencilRefSupported = vk_info->EXT_shader_stencil_export;
    options->TypedUAVLoadAdditionalFormats = d3d12_device_determine_additional_typed_uav_support(device);
    options->ROVsSupported = device->device_info.fragment_shader_interlock_features.fragmentShaderPixelInterlock &&
            device->device_info.fragment_shader_interlock_features.fragmentShaderSampleInterlock;
    options->ConservativeRasterizationTier = d3d12_device_determine_conservative_rasterization_tier(device);
    options->MaxGPUVirtualAddressBitsPerResource = 40; /* XXX */
    options->StandardSwizzle64KBSupported = FALSE;
    options->CrossNodeSharingTier = D3D12_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED;
    options->CrossAdapterRowMajorTextureSupported = FALSE;
    options->VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation =
            device->device_info.vulkan_1_2_features.shaderOutputViewportIndex &&
            device->device_info.vulkan_1_2_features.shaderOutputLayer;
    options->ResourceHeapTier = d3d12_device_determine_heap_tier(device);
}

static void d3d12_device_caps_init_feature_options1(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 *options1 = &device->d3d12_caps.options1;

    options1->WaveOps = device->d3d12_caps.max_shader_model >= D3D_SHADER_MODEL_6_0;
    options1->WaveLaneCountMin = device->device_info.vulkan_1_3_properties.minSubgroupSize;
    options1->WaveLaneCountMax = device->device_info.vulkan_1_3_properties.maxSubgroupSize;

    if (device->vk_info.AMD_shader_core_properties)
    {
        const VkPhysicalDeviceShaderCorePropertiesAMD *amd = &device->device_info.shader_core_properties;
        const VkPhysicalDeviceShaderCoreProperties2AMD *amd2 = &device->device_info.shader_core_properties2;
        uint32_t compute_units;

        if (device->vk_info.AMD_shader_core_properties2)
            compute_units = amd2->activeComputeUnitCount;
        else
            compute_units = amd->shaderEngineCount *
                    amd->computeUnitsPerShaderArray;

        /* There doesn't seem to be a way to get the SIMD width,
         * so just assume 64 lanes per CU. True on GCN and RDNA. */
        options1->TotalLaneCount = compute_units * 64;
    }
    else if (device->vk_info.NV_shader_sm_builtins)
    {
        /* Approximation, since we cannot query the number of lanes
         * per SM. Appears to be correct on Pascal and Turing. */
        const VkPhysicalDeviceShaderSMBuiltinsPropertiesNV *nv = &device->device_info.shader_sm_builtins_properties;
        options1->TotalLaneCount = nv->shaderSMCount * nv->shaderWarpsPerSM * 2;
    }
    else
    {
        options1->TotalLaneCount = 32 * device->device_info.vulkan_1_1_properties.subgroupSize;
        WARN("No device info available for TotalLaneCount = %u.\n", options1->TotalLaneCount);
    }

    options1->ExpandedComputeResourceStates = TRUE;
    options1->Int64ShaderOps = device->device_info.features2.features.shaderInt64;

    FIXME("TotalLaneCount = %u, may be inaccurate.\n", options1->TotalLaneCount);
}

static void d3d12_device_caps_init_feature_options2(struct d3d12_device *device)
{
    const VkPhysicalDeviceFeatures *features = &device->device_info.features2.features;
    D3D12_FEATURE_DATA_D3D12_OPTIONS2 *options2 = &device->d3d12_caps.options2;

    options2->DepthBoundsTestSupported = features->depthBounds;
    /* Requires VK_EXT_sample_locations */
    options2->ProgrammableSamplePositionsTier = D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_NOT_SUPPORTED;
}

static void d3d12_device_caps_init_feature_options3(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 *options3 = &device->d3d12_caps.options3;

    options3->CopyQueueTimestampQueriesSupported = !!device->queue_families[VKD3D_QUEUE_FAMILY_TRANSFER]->timestamp_bits;
    options3->CastingFullyTypedFormatSupported = TRUE;
    options3->WriteBufferImmediateSupportFlags = D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT |
            D3D12_COMMAND_LIST_SUPPORT_FLAG_COMPUTE | D3D12_COMMAND_LIST_SUPPORT_FLAG_COPY |
            D3D12_COMMAND_LIST_SUPPORT_FLAG_BUNDLE;
    /* Currently not supported */
    options3->ViewInstancingTier = D3D12_VIEW_INSTANCING_TIER_NOT_SUPPORTED;
    options3->BarycentricsSupported =
            device->device_info.barycentric_features_nv.fragmentShaderBarycentric ||
            device->device_info.barycentric_features_khr.fragmentShaderBarycentric;
}

static void d3d12_device_caps_init_feature_options4(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 *options4 = &device->d3d12_caps.options4;

    /* Requires changes to format compatibility */
    options4->MSAA64KBAlignedTextureSupported = FALSE;
    /* Shared resources not supported */
    options4->SharedResourceCompatibilityTier = D3D12_SHARED_RESOURCE_COMPATIBILITY_TIER_0;

    /* If SSBO alignment is > 16, we cannot use SSBOs due to robustness rules.
     * If we cannot use SSBOs, we cannot use 16-bit raw buffers, which is a requirement for this feature. */

    /* FP16 and FP64 must preserve denorms. Only FP32 can change, so we can accept both 32_BIT_INDEPENDENCY_ONLY and ALL. */
    options4->Native16BitShaderOpsSupported = d3d12_device_supports_16bit_shader_ops(device);
}

static void d3d12_device_caps_init_feature_options5(struct d3d12_device *device)
{
    const D3D12_FEATURE_DATA_D3D12_OPTIONS *options = &device->d3d12_caps.options;
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 *options5 = &device->d3d12_caps.options5;

    options5->SRVOnlyTiledResourceTier3 = options->TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_3;
    /* Currently not supported */
    options5->RenderPassesTier = D3D12_RENDER_PASS_TIER_0;
    options5->RaytracingTier = d3d12_device_determine_ray_tracing_tier(device);
}

static void d3d12_device_caps_init_feature_options6(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 *options6 = &device->d3d12_caps.options6;

    options6->AdditionalShadingRatesSupported = device->device_info.additional_shading_rates_supported;
    options6->VariableShadingRateTier = d3d12_device_determine_variable_shading_rate_tier(device);
    if (options6->VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2)
    {
        options6->ShadingRateImageTileSize = d3d12_determine_shading_rate_image_tile_size(device);

        options6->PerPrimitiveShadingRateSupportedWithViewportIndexing =
                device->device_info.fragment_shading_rate_properties.primitiveFragmentShadingRateWithMultipleViewports;
    }
    else
    {
        options6->ShadingRateImageTileSize = 0;
        options6->PerPrimitiveShadingRateSupportedWithViewportIndexing = FALSE;
    }
    /* Not implemented */
    options6->BackgroundProcessingSupported = FALSE;
}

static void d3d12_device_caps_init_feature_options7(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 *options7 = &device->d3d12_caps.options7;

    options7->MeshShaderTier = d3d12_device_determine_mesh_shader_tier(device);
    options7->SamplerFeedbackTier = d3d12_device_determine_sampler_feedback_tier(device);
}

static void d3d12_device_caps_init_feature_options8(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS8 *options8 = &device->d3d12_caps.options8;

    /* Advertise that it is possible to use a top mip level which is not aligned to 4.
     * Weird legacy D3D11 requirement that is not relevant anymore, and does not exist in Vulkan. */
    options8->UnalignedBlockTexturesSupported = TRUE;
}

static void d3d12_device_caps_init_feature_options9(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS9 *options9 = &device->d3d12_caps.options9;

    options9->AtomicInt64OnGroupSharedSupported =
            device->device_info.vulkan_1_2_features.shaderSharedInt64Atomics;
    /* Unsure if sparse 64-bit image atomics is also required. */
    /* If we cannot expose AtomicInt64OnDescriptorHeapResourceSupported, we cannot expose this one either. */
    options9->AtomicInt64OnTypedResourceSupported =
            device->device_info.shader_image_atomic_int64_features.shaderImageInt64Atomics &&
            device->device_info.properties2.properties.limits.minStorageBufferOffsetAlignment <= 16;
    options9->DerivativesInMeshAndAmplificationShadersSupported = d3d12_device_determine_mesh_shader_tier(device) &&
            device->d3d12_caps.max_shader_model >= D3D_SHADER_MODEL_6_6 &&
            device->device_info.compute_shader_derivatives_properties_khr.meshAndTaskShaderDerivatives;
    options9->MeshShaderSupportsFullRangeRenderTargetArrayIndex = d3d12_device_determine_mesh_shader_tier(device) &&
            device->device_info.mesh_shader_properties.maxMeshOutputLayers >= device->device_info.properties2.properties.limits.maxFramebufferLayers;
    options9->MeshShaderPipelineStatsSupported = FALSE;
    options9->WaveMMATier = D3D12_WAVE_MMA_TIER_NOT_SUPPORTED;
}

static void d3d12_device_caps_init_feature_options10(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS10 *options10 = &device->d3d12_caps.options10;

    options10->VariableRateShadingSumCombinerSupported =
            d3d12_device_determine_variable_shading_rate_tier(device) >= D3D12_VARIABLE_SHADING_RATE_TIER_1;
    options10->MeshShaderPerPrimitiveShadingRateSupported = d3d12_device_determine_mesh_shader_tier(device) &&
            d3d12_device_determine_variable_shading_rate_tier(device) &&
            device->device_info.mesh_shader_features.primitiveFragmentShadingRateMeshShader;
}

static void d3d12_device_caps_init_feature_options11(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS11 *options11 = &device->d3d12_caps.options11;

    /* If we're not using raw SSBOs, we cannot support 64-bit atomics. */
    options11->AtomicInt64OnDescriptorHeapResourceSupported =
            device->device_info.vulkan_1_2_features.shaderBufferInt64Atomics &&
            device->device_info.properties2.properties.limits.minStorageBufferOffsetAlignment <= 16;
}

static void d3d12_device_caps_init_feature_options12(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 *options12 = &device->d3d12_caps.options12;
    options12->RelaxedFormatCastingSupported = TRUE;
    options12->EnhancedBarriersSupported = TRUE;
}

static void d3d12_device_caps_init_feature_options13(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS13 *options13 = &device->d3d12_caps.options13;

    options13->UnrestrictedBufferTextureCopyPitchSupported = TRUE;
    options13->InvertedViewportHeightFlipsYSupported = TRUE;
    options13->InvertedViewportDepthFlipsZSupported = TRUE;
    options13->TextureCopyBetweenDimensionsSupported = TRUE;
    options13->AlphaBlendFactorSupported = TRUE;
}

static void d3d12_device_caps_init_feature_options14(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS14 *options14 = &device->d3d12_caps.options14;

    /* Texture with dynamic offsets works fine in practice and is officially supported by maintenance8.
     * Enable with experimental features for older drivers.
     */
    options14->AdvancedTextureOpsSupported = device->d3d12_caps.max_shader_model >= D3D_SHADER_MODEL_6_7 &&
        (device->device_info.maintenance_8_features.maintenance8 || (vkd3d_config_flags & VKD3D_CONFIG_FLAG_ENABLE_EXPERIMENTAL_FEATURES));
    options14->WriteableMSAATexturesSupported = device->d3d12_caps.max_shader_model >= D3D_SHADER_MODEL_6_7 &&
            device->device_info.features2.features.shaderStorageImageMultisample;
    options14->IndependentFrontAndBackStencilRefMaskSupported = TRUE;
}

static void d3d12_device_caps_init_feature_options15(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS15 *options15 = &device->d3d12_caps.options15;

    options15->TriangleFanSupported = TRUE;
    options15->DynamicIndexBufferStripCutSupported = TRUE;
}

static void d3d12_device_caps_init_feature_options16(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 *options16 = &device->d3d12_caps.options16;

    options16->DynamicDepthBiasSupported = TRUE;
    options16->GPUUploadHeapSupported = device->memory_info.has_gpu_upload_heap;
}

static void d3d12_device_caps_init_feature_options17(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS17 *options17 = &device->d3d12_caps.options17;

    options17->NonNormalizedCoordinateSamplersSupported = TRUE;
    /* Debug-only feature, always reported to be false by the runtime */
    options17->ManualWriteTrackingResourceSupported = FALSE;
}

static void d3d12_device_caps_init_feature_options18(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS18 *options18 = &device->d3d12_caps.options18;
    options18->RenderPassesValid = TRUE;
}

static void d3d12_device_caps_init_feature_options19(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS19 *options19 = &device->d3d12_caps.options19;

    /* We trivially support this by not validating resource types in rendering
     * and computing renderArea to be the intersection of all bound views. */
    options19->MismatchingOutputDimensionsSupported = TRUE;
    /* Requires SampleCount > 1 for pipelinesm, not just ForcedSamplecount */
    options19->SupportedSampleCountsWithNoOutputs = 0x1;
    /* D3D12 expectations w.r.t. rounding match Vulkan spec.
     * However, both AMD and Intel native drivers round to even. RADV has no-trunc-coord workarounds.
     * Turnip enables round-to-even behavior for vkd3d. */
    options19->PointSamplingAddressesNeverRoundUp =
            device->device_info.vulkan_1_2_properties.driverID != VK_DRIVER_ID_MESA_RADV &&
            device->device_info.vulkan_1_2_properties.driverID != VK_DRIVER_ID_MESA_TURNIP;
    options19->RasterizerDesc2Supported = TRUE;
    /* We default to a line width of 1.0 anyway */
    options19->NarrowQuadrilateralLinesSupported = TRUE;
    options19->AnisoFilterWithPointMipSupported = TRUE;
    /* Report legacy D3D12 limits for now. Increasing descriptor count limits
     * would require changing changing descriptor set layouts, and more samplers
     * need additional considerations w.r.t. Vulkan device limits. */
    options19->MaxSamplerDescriptorHeapSize = d3d12_device_get_max_descriptor_heap_size(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    options19->MaxSamplerDescriptorHeapSizeWithStaticSamplers = options19->MaxSamplerDescriptorHeapSize;
    options19->MaxViewDescriptorHeapSize = d3d12_device_get_max_descriptor_heap_size(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    options19->ComputeOnlyCustomHeapSupported = FALSE;
}

static void d3d12_device_caps_init_feature_options20(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS20 *options20 = &device->d3d12_caps.options20;

    options20->ComputeOnlyWriteWatchSupported = FALSE;
    options20->RecreateAtTier = D3D12_RECREATE_AT_TIER_NOT_SUPPORTED;
}

static void d3d12_device_caps_init_feature_options21(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS21 *options21 = &device->d3d12_caps.options21;

    options21->WorkGraphsTier = D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED;
    options21->ExecuteIndirectTier = device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands ?
            D3D12_EXECUTE_INDIRECT_TIER_1_1 : D3D12_EXECUTE_INDIRECT_TIER_1_0;
    options21->SampleCmpGradientAndBiasSupported = device->d3d12_caps.max_shader_model >= D3D_SHADER_MODEL_6_8 &&
            device->d3d12_caps.options14.AdvancedTextureOpsSupported;
    options21->ExtendedCommandInfoSupported = device->d3d12_caps.max_shader_model >= D3D_SHADER_MODEL_6_8;
}

static void d3d12_device_caps_init_feature_level(struct d3d12_device *device)
{
    const VkPhysicalDeviceFeatures *features = &device->device_info.features2.features;
    const struct vkd3d_vulkan_info *vk_info = &device->vk_info;
    struct d3d12_caps *caps = &device->d3d12_caps;

    caps->max_feature_level = D3D_FEATURE_LEVEL_11_0;

    if (caps->options.OutputMergerLogicOp && features->vertexPipelineStoresAndAtomics &&
            vk_info->device_limits.maxPerStageDescriptorStorageBuffers >= D3D12_UAV_SLOT_COUNT &&
            vk_info->device_limits.maxPerStageDescriptorStorageImages >= D3D12_UAV_SLOT_COUNT)
        caps->max_feature_level = D3D_FEATURE_LEVEL_11_1;

    if (caps->max_feature_level >= D3D_FEATURE_LEVEL_11_1 &&
            caps->options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_2 &&
            caps->options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2 &&
            caps->options.TypedUAVLoadAdditionalFormats)
        caps->max_feature_level = D3D_FEATURE_LEVEL_12_0;

    if (caps->max_feature_level >= D3D_FEATURE_LEVEL_12_0 && caps->options.ROVsSupported &&
            caps->options.ConservativeRasterizationTier >= D3D12_CONSERVATIVE_RASTERIZATION_TIER_1)
        caps->max_feature_level = D3D_FEATURE_LEVEL_12_1;

    if (caps->max_feature_level >= D3D_FEATURE_LEVEL_12_1 && caps->max_shader_model >= D3D_SHADER_MODEL_6_5 &&
            caps->options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation &&
            caps->options1.WaveOps && caps->options1.Int64ShaderOps && caps->options2.DepthBoundsTestSupported &&
            caps->options3.CopyQueueTimestampQueriesSupported && caps->options3.CastingFullyTypedFormatSupported &&
            caps->options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 &&
            caps->options.ConservativeRasterizationTier >= D3D12_CONSERVATIVE_RASTERIZATION_TIER_3 &&
            caps->options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_3 &&
            caps->options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1 &&
            caps->options6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2 &&
            caps->options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1 &&
            caps->options7.SamplerFeedbackTier >= D3D12_SAMPLER_FEEDBACK_TIER_0_9)
        caps->max_feature_level = D3D_FEATURE_LEVEL_12_2;

    TRACE("Max feature level: %#x.\n", caps->max_feature_level);

    if (caps->max_feature_level >= D3D_FEATURE_LEVEL_12_2)
        INFO("DX Ultimate supported!\n");
}

static void d3d12_device_caps_shader_model_override(struct d3d12_device *device)
{
    D3D_SHADER_MODEL sm_override = (D3D_SHADER_MODEL)0;
    char sm_string[VKD3D_PATH_MAX];
    unsigned int i;

    static const struct
    {
        const char *string;
        D3D_SHADER_MODEL shader_model;
    }
    shader_models[] =
    {
        { "5_1", D3D_SHADER_MODEL_5_1 },
        { "6_0", D3D_SHADER_MODEL_6_0 },
        { "6_1", D3D_SHADER_MODEL_6_1 },
        { "6_2", D3D_SHADER_MODEL_6_2 },
        { "6_3", D3D_SHADER_MODEL_6_3 },
        { "6_4", D3D_SHADER_MODEL_6_4 },
        { "6_5", D3D_SHADER_MODEL_6_5 },
        { "6_6", D3D_SHADER_MODEL_6_6 },
        { "6_7", D3D_SHADER_MODEL_6_7 },
        { "6_8", D3D_SHADER_MODEL_6_8 },
    };

    if (!vkd3d_get_env_var("VKD3D_SHADER_MODEL", sm_string, sizeof(sm_string)))
        return;

    for (i = 0; i < ARRAY_SIZE(shader_models); i++)
    {
        if (!strcmp(sm_string, shader_models[i].string))
        {
            sm_override = shader_models[i].shader_model;
            break;
        }
    }

    if (!sm_override)
    {
        WARN("Unrecognized shader model %s.\n", sm_string);
    }
    else
    {
        device->d3d12_caps.max_shader_model = sm_override;
        WARN("Overriding supported shader model: %s.\n", sm_string);
    }
}

static void d3d12_device_caps_init_shader_model(struct d3d12_device *device)
{
    const struct vkd3d_physical_device_info *physical_device_info = &device->device_info;
    bool denorm_behavior;

    /* SHUFFLE is required to implement WaveReadLaneAt with dynamically uniform index before SPIR-V 1.5 / Vulkan 1.2. */
    static const VkSubgroupFeatureFlags required =
            VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
            VK_SUBGROUP_FEATURE_BASIC_BIT |
            VK_SUBGROUP_FEATURE_BALLOT_BIT |
            VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
            VK_SUBGROUP_FEATURE_QUAD_BIT |
            VK_SUBGROUP_FEATURE_VOTE_BIT;

    static const VkSubgroupFeatureFlags required_stages =
            VK_SHADER_STAGE_COMPUTE_BIT |
            VK_SHADER_STAGE_FRAGMENT_BIT;

    /* We need to support modern cbuffer layout in SM 6.0, which is equivalent to array of scalars with
     * tight packing. Either scalar block layout or the more relaxed UBO standard layout feature exposes this. */

    /* Require Int16 support as well. Technically, this isn't required unless SM 6.2 native 16-bit is exposed,
     * but in practice we need to translate min16int to proper 16-bit integers, since otherwise breakage occurs.
     * This is non-controversial since everyone supports 16-bit ints. */

    if (physical_device_info->vulkan_1_1_properties.subgroupSize >= 4 &&
        (physical_device_info->vulkan_1_1_properties.subgroupSupportedOperations & required) == required &&
        (physical_device_info->vulkan_1_1_properties.subgroupSupportedStages & required_stages) == required_stages &&
        (physical_device_info->vulkan_1_2_features.scalarBlockLayout || physical_device_info->vulkan_1_2_features.uniformBufferStandardLayout) &&
        physical_device_info->features2.features.shaderInt16)
    {
        /* From testing on native Polaris drivers, AMD expose SM 6.5, even if lots of features are not supported.
         * This is a good hint that shader model versions are not tied to features which have caps bits.
         * Only consider required features here. */

        /* SM 6.0 adds:
         * https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model-6.0
         * WaveOps, int64 (optional)
         */
        device->d3d12_caps.max_shader_model = D3D_SHADER_MODEL_6_0;
        TRACE("Enabling support for SM 6.0.\n");

        /* SM 6.1 adds:
         * https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model-6.1
         * SV_ViewID (VK_KHR_multiview?), SV_Barycentrics
         */

        /* SM 6.2 adds:
         * https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model-6.2
         * FP16, Denorm modes (float controls extension)
         */

        /* DXIL allows control over denorm behavior for FP32 only.
         * shaderDenorm handling appears to work just fine on NV, despite the properties struct saying otherwise.
         * Assume that this is just a driver oversight, since otherwise we cannot expose SM 6.2 there ... */
        denorm_behavior = device->device_info.vulkan_1_2_properties.denormBehaviorIndependence !=
                VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;
        if (denorm_behavior)
        {
            if (device->device_info.vulkan_1_2_properties.driverID != VK_DRIVER_ID_NVIDIA_PROPRIETARY)
            {
                denorm_behavior = device->device_info.vulkan_1_2_properties.shaderDenormFlushToZeroFloat32 &&
                        device->device_info.vulkan_1_2_properties.shaderDenormPreserveFloat32;
            }
        }

        if (denorm_behavior)
        {
            device->d3d12_caps.max_shader_model = D3D_SHADER_MODEL_6_2;
            TRACE("Enabling support for SM 6.2.\n");
        }

        /* SM 6.3 adds:
         * https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model-6.3
         * Ray tracing (lib_6_3 multi entry point targets).
         */
        if (device->d3d12_caps.max_shader_model == D3D_SHADER_MODEL_6_2)
        {
            /* SPIR-V 1.4 is required for lib_6_3 since that is required for RT. */
            device->d3d12_caps.max_shader_model = D3D_SHADER_MODEL_6_3;
            TRACE("Enabling support for SM 6.3.\n");
        }

        /* SM 6.4 adds:
         * https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model-6.4
         * Integer dot products (8-bit)
         * Mixed FP16 dot products
         * Variable rate shading
         * Library subobjects
         */

        /* SM 6.5 adds:
         * https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model-6.5
         * DXR 1.1, Sampler feedback, Mesh shaders, Amplification shaders, more wave ops.
         */
        if (device->d3d12_caps.max_shader_model == D3D_SHADER_MODEL_6_3)
        {
            /* Nothing in SM 6.4 requires special support (except for VRS which is optional).
             * The rest is just shader arithmetic intrinsics and reflection. */
            /* The only required features in SM 6.5 are WaveMulti and WaveMultiPrefix wave ops
             * which map directly to normal wave operations. */
            device->d3d12_caps.max_shader_model = D3D_SHADER_MODEL_6_5;
            TRACE("Enabling support for SM 6.5.\n");
        }

        /* Required features:
         * - ComputeShader derivatives (linear only, dxil-spirv can synthesize Quad).
         *   NV Pascal does not support this, despite their D3D12 driver exposing it.
         *   Use a fallback path on NV proprietary since UE5 started relying on SM 6.6 to work.
         *   Current use of SM 6.6 compute shader derivatives in the wild is trivial,
         *   so this should be alright.
         * - 64-bit atomics. Only buffer atomics are required for SM 6.6.
         * - Strict IsHelperInvocation(). The emulated path might have some edge cases here,
         *   no reason not to require it.
         * - 8-bit integers. Widely supported, even on older targets. Can be emulated if need be.
         * - WaveSize attribute, requiredSubgroupSizeStages + FullSubgroups feature is required.
         *   If minSize == maxSize, we are trivially guaranteed the desired wave size anyways.
         * - RayPayload attribute (purely metadata in DXIL land, irrelevant for us).
         */
        if (device->d3d12_caps.max_shader_model == D3D_SHADER_MODEL_6_5 &&
                (device->device_info.compute_shader_derivatives_features_khr.computeDerivativeGroupLinear ||
                        device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY) &&
                device->device_info.vulkan_1_2_features.shaderBufferInt64Atomics &&
                device->device_info.vulkan_1_2_features.shaderInt8 &&
                d3d12_device_supports_required_subgroup_size_for_stage(device, VK_SHADER_STAGE_COMPUTE_BIT))
        {
            INFO("Enabling support for SM 6.6.\n");
            if (!device->device_info.compute_shader_derivatives_features_khr.computeDerivativeGroupLinear)
                WARN("Enabling SM 6.6 on pre-Turing NVIDIA despite lack of native compute shader derivatives.\n");
            device->d3d12_caps.max_shader_model = D3D_SHADER_MODEL_6_6;
        }

        /* SM 6.7 adds:
         * - QuadAny / All (required)
         *   - This can be implemented directly with quad shuffles.
         *   - In both D3D12 docs and on real implementations, undefined behavior happens when inactive lanes are used.
         * - Helper lanes in wave ops (required)
         *   - Vulkan by default says that helper lanes participate, but they may not participate in any non-quad operation.
         *     KHR_shader_maximal_reconvergence is needed to guarantee this behaviour.
         * - Programmable offsets (AdvancedTextureOps)
         *   - maintenance8 enables this.
         *   - It's optional and depends on castable texture formats either way.
         *   - We can enable it through app-opt if there is a real need for it.
         * - MSAA UAV (separate feature)
         *   - Trivial Vulkan catch-up
         * - SampleCmpLevel (AdvancedTextureOps)
         *   - Trivial Vulkan catch-up
         * - Raw Gather (AdvancedTextureOps)
         *   - Looks scary, but the view format must be R16, R32 or R32G32_UINT, which makes it trivial.
         *   - It behaves exactly like you're doing GatherRed or bitcast(GatherRed, GatherGreen).
         *   - Tested against RGBA8, and it does *not* reinterpret RGBA8 to R32 in the shader.
         * - Integer sampling (AdvancedTextureOps)
         *   - Trivial Vulkan catch-up. Requires implementing border colors as well.
         */
        if (device->d3d12_caps.max_shader_model == D3D_SHADER_MODEL_6_6 && ((
                device->device_info.shader_maximal_reconvergence_features.shaderMaximalReconvergence &&
                device->device_info.shader_quad_control_features.shaderQuadControl) ||
                (vkd3d_config_flags & VKD3D_CONFIG_FLAG_ENABLE_EXPERIMENTAL_FEATURES)))
        {
            /* Helper lanes in wave ops behavior appears to work as intended on NV and RADV.
             * Technically needs an extension to *guarantee* this behavior however ... */
            INFO("Enabling support for SM 6.7.\n");
            device->d3d12_caps.max_shader_model = D3D_SHADER_MODEL_6_7;
        }

        /* SM6.8 adds:
         * - SV_StartInstanceLocation / SV_StartVertexLocation (required)
         *   - Trivially maps to BaseVertex and BaseInstance, respectively
         * - WaveSizeRange (required)
         *   - Completely replaces WaveSize at the DXIL level
         *   - Specifies a range of subgroup sizes that the shader can operate
         *     with, as well as a preferred subgroup size.
         * - Expanded Comparison Sampling
         *   - Basically Grad/Bias operands for Dref instructions
         * - Work graphs
         *   - uhh...
         */
        if (device->d3d12_caps.max_shader_model == D3D_SHADER_MODEL_6_7)
        {
            INFO("Enabling support for SM 6.8.\n");
            device->d3d12_caps.max_shader_model = D3D_SHADER_MODEL_6_8;
        }
    }
    else
    {
        device->d3d12_caps.max_shader_model = D3D_SHADER_MODEL_5_1;
        TRACE("Enabling support for SM 5.1.\n");
    }
}

static void d3d12_device_caps_override_application(struct d3d12_device *device)
{
    /* Some games rely on certain features to be exposed before they let the primary feature
     * be exposed. */
    switch (vkd3d_application_feature_override)
    {
        case VKD3D_APPLICATION_FEATURE_NO_DEFAULT_DXR_ON_DECK:
            /* For games which automatically enable RT even on Deck, leading to very poor performance by default. */
            if (d3d12_device_is_steam_deck(device) && !(vkd3d_config_flags & VKD3D_CONFIG_FLAG_DXR))
            {
                INFO("Disabling automatic enablement of DXR on Deck.\n");
                device->d3d12_caps.options5.RaytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
            }
            break;

        case VKD3D_APPLICATION_FEATURE_LIMIT_DXR_1_0:
            if (device->d3d12_caps.options5.RaytracingTier > D3D12_RAYTRACING_TIER_1_0)
            {
                INFO("Limiting reported DXR tier to 1.0.\n");
                device->d3d12_caps.options5.RaytracingTier = D3D12_RAYTRACING_TIER_1_0;
            }
            break;

        case VKD3D_APPLICATION_FEATURE_DISABLE_NV_REFLEX:
            device->vk_info.NV_low_latency2 = false;
            break;

        default:
            break;
    }
}

static void d3d12_device_caps_override(struct d3d12_device *device)
{
    D3D_FEATURE_LEVEL fl_override = (D3D_FEATURE_LEVEL)0;
    struct d3d12_caps *caps = &device->d3d12_caps;
    char fl_string[VKD3D_PATH_MAX];
    unsigned int i;

    static const struct
    {
        const char *string;
        D3D_FEATURE_LEVEL feature_level;
    }
    feature_levels[] =
    {
        { "11_0", D3D_FEATURE_LEVEL_11_0 },
        { "11_1", D3D_FEATURE_LEVEL_11_1 },
        { "12_0", D3D_FEATURE_LEVEL_12_0 },
        { "12_1", D3D_FEATURE_LEVEL_12_1 },
        { "12_2", D3D_FEATURE_LEVEL_12_2 },
    };

    if (!vkd3d_get_env_var("VKD3D_FEATURE_LEVEL", fl_string, sizeof(fl_string)))
        return;

    for (i = 0; i < ARRAY_SIZE(feature_levels); i++)
    {
        if (!strcmp(fl_string, feature_levels[i].string))
        {
            fl_override = feature_levels[i].feature_level;
            break;
        }
    }

    if (!fl_override)
    {
        WARN("Unrecognized feature level %s.\n", fl_string);
        return;
    }

    if (fl_override >= D3D_FEATURE_LEVEL_11_1)
        caps->options.OutputMergerLogicOp = TRUE;

    if (fl_override >= D3D_FEATURE_LEVEL_12_0)
    {
        caps->options.TiledResourcesTier = max(caps->options.TiledResourcesTier, D3D12_TILED_RESOURCES_TIER_2);
        caps->options.ResourceBindingTier = max(caps->options.ResourceBindingTier, D3D12_RESOURCE_BINDING_TIER_2);
        caps->max_shader_model = max(caps->max_shader_model, D3D_SHADER_MODEL_6_0);
        caps->options.TypedUAVLoadAdditionalFormats = TRUE;
    }

    if (fl_override >= D3D_FEATURE_LEVEL_12_1)
    {
        caps->options.ROVsSupported = TRUE;
        caps->options.ConservativeRasterizationTier = max(caps->options.ConservativeRasterizationTier, D3D12_CONSERVATIVE_RASTERIZATION_TIER_1);
    }

    if (fl_override >= D3D_FEATURE_LEVEL_12_2)
    {
        caps->options5.RaytracingTier = max(caps->options5.RaytracingTier, D3D12_RAYTRACING_TIER_1_1);
        caps->options6.VariableShadingRateTier = max(caps->options6.VariableShadingRateTier, D3D12_VARIABLE_SHADING_RATE_TIER_1);
        caps->options.ResourceBindingTier = max(caps->options.ResourceBindingTier, D3D12_RESOURCE_BINDING_TIER_3);
        caps->options.TiledResourcesTier = max(caps->options.TiledResourcesTier, D3D12_TILED_RESOURCES_TIER_3);
        caps->options.ConservativeRasterizationTier = max(caps->options.ConservativeRasterizationTier, D3D12_CONSERVATIVE_RASTERIZATION_TIER_3);
        caps->max_shader_model = max(caps->max_shader_model, D3D_SHADER_MODEL_6_5);
        caps->options7.MeshShaderTier = max(caps->options7.MeshShaderTier, D3D12_MESH_SHADER_TIER_1);
        caps->options7.SamplerFeedbackTier = max(caps->options7.SamplerFeedbackTier, D3D12_SAMPLER_FEEDBACK_TIER_1_0);
    }

    caps->max_feature_level = fl_override;
    WARN("Overriding feature level: %#x.\n", fl_override);
}

static void d3d12_device_caps_init(struct d3d12_device *device)
{
    d3d12_device_caps_init_shader_model(device);
    d3d12_device_caps_shader_model_override(device);

    d3d12_device_caps_init_feature_options(device);
    d3d12_device_caps_init_feature_options1(device);
    d3d12_device_caps_init_feature_options2(device);
    d3d12_device_caps_init_feature_options3(device);
    d3d12_device_caps_init_feature_options4(device);
    d3d12_device_caps_init_feature_options5(device);
    d3d12_device_caps_init_feature_options6(device);
    d3d12_device_caps_init_feature_options7(device);
    d3d12_device_caps_init_feature_options8(device);
    d3d12_device_caps_init_feature_options9(device);
    d3d12_device_caps_init_feature_options10(device);
    d3d12_device_caps_init_feature_options11(device);
    d3d12_device_caps_init_feature_options12(device);
    d3d12_device_caps_init_feature_options13(device);
    d3d12_device_caps_init_feature_options14(device);
    d3d12_device_caps_init_feature_options15(device);
    d3d12_device_caps_init_feature_options16(device);
    d3d12_device_caps_init_feature_options17(device);
    d3d12_device_caps_init_feature_options18(device);
    d3d12_device_caps_init_feature_options19(device);
    d3d12_device_caps_init_feature_options20(device);
    d3d12_device_caps_init_feature_options21(device);
    d3d12_device_caps_init_feature_level(device);

    d3d12_device_caps_override(device);
    d3d12_device_caps_override_application(device);
}

static void vkd3d_init_shader_extensions(struct d3d12_device *device)
{
    bool allow_denorm_control;
    device->vk_info.shader_extension_count = 0;

    device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
            VKD3D_SHADER_TARGET_EXTENSION_SPV_KHR_INTEGER_DOT_PRODUCT;
    device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
            VKD3D_SHADER_TARGET_EXTENSION_SPV_EXT_DEMOTE_TO_HELPER_INVOCATION;

    if (d3d12_device_determine_additional_typed_uav_support(device))
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_READ_STORAGE_IMAGE_WITHOUT_FORMAT;
    }

    if (device->device_info.ray_tracing_pipeline_features.rayTraversalPrimitiveCulling)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_RAY_TRACING_PRIMITIVE_CULLING;
    }

    if (device->device_info.vulkan_1_2_features.scalarBlockLayout)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_SCALAR_BLOCK_LAYOUT;

        if (device->device_info.properties2.properties.vendorID == VKD3D_VENDOR_ID_AMD)
        {
            /* Raw load-store instructions on AMD are bounds checked correctly per component.
             * In other cases, we must be careful when emitting byte address buffers and block
             * any attempt to vectorize vec3.
             * We can still vectorize vec3 structured buffers however as long as SCALAR_BLOCK_LAYOUT
             * is supported. */
            device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                    VKD3D_SHADER_TARGET_EXTENSION_ASSUME_PER_COMPONENT_SSBO_ROBUSTNESS;
        }
    }

    if (device->device_info.barycentric_features_khr.fragmentShaderBarycentric)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_BARYCENTRIC_KHR;
    }

    if (device->device_info.compute_shader_derivatives_features_khr.computeDerivativeGroupLinear)
    {
        enum vkd3d_shader_target_extension compute_derivative_ext = VKD3D_SHADER_TARGET_EXTENSION_COMPUTE_SHADER_DERIVATIVES_NV;

        if (device->vk_info.KHR_compute_shader_derivatives)
            compute_derivative_ext = VKD3D_SHADER_TARGET_EXTENSION_COMPUTE_SHADER_DERIVATIVES_KHR;

        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] = compute_derivative_ext;
    }

    if (device->d3d12_caps.options4.Native16BitShaderOpsSupported)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_MIN_PRECISION_IS_NATIVE_16BIT;
    }

    /* NV driver implies denorm preserve by default in FP16 and 64, but there's an issue where
     * the explicit denorm preserve state spills into FP32 even when we explicitly want FTZ for FP32.
     * Denorm preserve is only exposed on FP16 on this implementation,
     * so it's technically in-spec to do this,
     * but the only way we can make NV pass our tests is to *not* emit anything at all for 16 and 64. */
    allow_denorm_control =
            device->device_info.vulkan_1_2_properties.driverID != VK_DRIVER_ID_NVIDIA_PROPRIETARY ||
            (vkd3d_config_flags & VKD3D_CONFIG_FLAG_SKIP_DRIVER_WORKAROUNDS);

    if (device->device_info.vulkan_1_2_properties.denormBehaviorIndependence !=
            VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE &&
            allow_denorm_control)
    {
        if (device->device_info.vulkan_1_2_properties.shaderDenormPreserveFloat16)
        {
            device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                    VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP16_DENORM_PRESERVE;
        }

        if (device->device_info.vulkan_1_2_properties.shaderDenormFlushToZeroFloat32)
        {
            device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                    VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP32_DENORM_FLUSH;
        }

        if (device->device_info.vulkan_1_2_properties.shaderDenormPreserveFloat64)
        {
            device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                    VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP64_DENORM_PRESERVE;
        }
    }

    if (device->device_info.vulkan_1_2_properties.shaderSignedZeroInfNanPreserveFloat16)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP16_INF_NAN_PRESERVE;
    }

    if (device->device_info.vulkan_1_2_properties.shaderSignedZeroInfNanPreserveFloat32)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP32_INF_NAN_PRESERVE;
    }

    if (device->device_info.vulkan_1_2_properties.shaderSignedZeroInfNanPreserveFloat64)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP64_INF_NAN_PRESERVE;
    }

    if (device->vk_info.NV_shader_subgroup_partitioned &&
            (device->device_info.vulkan_1_1_properties.subgroupSupportedOperations & VK_SUBGROUP_FEATURE_PARTITIONED_BIT_NV))
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_SUBGROUP_PARTITIONED_NV;
    }

    if (device->device_info.shader_maximal_reconvergence_features.shaderMaximalReconvergence &&
            device->device_info.shader_quad_control_features.shaderQuadControl)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_QUAD_CONTROL_RECONVERGENCE;
    }

    if (device->device_info.raw_access_chains_nv.shaderRawAccessChains)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_RAW_ACCESS_CHAINS_NV;
    }

    if (device->device_info.opacity_micromap_features.micromap)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_OPACITY_MICROMAP;
    }

    if (device->device_info.shader_float8_features.shaderFloat8CooperativeMatrix)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_WMMA_FP8;
    }

    if (device->device_info.cooperative_matrix2_features_nv.cooperativeMatrixConversions)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_NV_COOPMAT2_CONVERSIONS;
    }
}

static void vkd3d_compute_shader_interface_key(struct d3d12_device *device)
{
    /* This key needs to hold all state which could potentially affect shader compilation.
     * We may generate different SPIR-V based on the bindless state flags.
     * The bindless states are affected by various flags. */
    unsigned int i;
    char env[64];
    uint64_t key;

    key = hash_fnv1_init();

    /* Technically, any changes in vkd3d-shader will be reflected in the vkd3d-proton Git hash,
     * but it is useful to be able to modify the internal revision while developing since
     * we have no mechanism for emitting dirty Git revisions. */
    key = hash_fnv1_iterate_u64(key, vkd3d_shader_get_revision());
    key = hash_fnv1_iterate_u32(key, device->device_info.vulkan_1_3_properties.minSubgroupSize);
    key = hash_fnv1_iterate_u32(key, device->device_info.vulkan_1_3_properties.maxSubgroupSize);
    key = hash_fnv1_iterate_u32(key, device->bindless_state.flags);
    key = hash_fnv1_iterate_u32(key, device->bindless_state.cbv_srv_uav_count);
    key = hash_fnv1_iterate_u32(key, device->bindless_state.set_count);
    for (i = 0; i < device->bindless_state.set_count; i++)
    {
        key = hash_fnv1_iterate_u32(key, device->bindless_state.set_info[i].flags);
        key = hash_fnv1_iterate_u32(key, device->bindless_state.set_info[i].binding_index);
        key = hash_fnv1_iterate_u32(key, device->bindless_state.set_info[i].set_index);
        key = hash_fnv1_iterate_u32(key, device->bindless_state.set_info[i].heap_type);
        key = hash_fnv1_iterate_u32(key, device->bindless_state.set_info[i].vk_descriptor_type);
    }

    if (d3d12_device_use_embedded_mutable_descriptors(device))
    {
        /* Will affect shaders which use raw VA descriptors like RTAS, UAV counters and local root signatures. */
        key = hash_fnv1_iterate_u32(key, device->bindless_state.descriptor_buffer_cbv_srv_uav_size);
        /* Will affect shaders which use local root signatures. */
        key = hash_fnv1_iterate_u32(key, device->bindless_state.descriptor_buffer_sampler_size);
    }

    key = hash_fnv1_iterate_u32(key, vkd3d_shader_quirk_info.global_quirks);
    key = hash_fnv1_iterate_u32(key, vkd3d_shader_quirk_info.default_quirks);
    key = hash_fnv1_iterate_u32(key, vkd3d_shader_quirk_info.num_hashes);
    /* If apps attempt to use the same shader cache with different executables, we might end up with different
     * quirk tables due to app workarounds, so hash that too. */
    for (i = 0; i < vkd3d_shader_quirk_info.num_hashes; i++)
    {
        key = hash_fnv1_iterate_u64(key, vkd3d_shader_quirk_info.hashes[i].shader_hash);
        key = hash_fnv1_iterate_u32(key, vkd3d_shader_quirk_info.hashes[i].quirks);
    }

    for (i = 0; i < device->vk_info.shader_extension_count; i++)
        key = hash_fnv1_iterate_u32(key, device->vk_info.shader_extensions[i]);

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DRIVER_VERSION_SENSITIVE_SHADERS)
    {
        key = hash_fnv1_iterate_u32(key, device->device_info.vulkan_1_2_properties.driverID);
        key = hash_fnv1_iterate_u32(key, device->device_info.properties2.properties.driverVersion);
    }

    /* QA checks don't necessarily modify bindless flags, so have to check them separately. */
    hash_fnv1_iterate_u32(key, vkd3d_descriptor_debug_active_instruction_qa_checks());
    hash_fnv1_iterate_u32(key, vkd3d_descriptor_debug_active_descriptor_qa_checks());

    if (vkd3d_get_env_var("DXIL_SPIRV_CONFIG", env, sizeof(env)))
    {
        INFO("Using DXIL_SPIRV_CONFIG = %s\n", env);
        key = hash_fnv1_iterate_string(key, env);
    }

    device->shader_interface_key = key;
}

static bool d3d12_device_supports_feature_level(struct d3d12_device *device, D3D_FEATURE_LEVEL feature_level)
{
    return feature_level <= device->d3d12_caps.max_feature_level;
}

static void d3d12_device_replace_vtable(struct d3d12_device *device)
{
    /* Don't bother replacing the vtable unless we have to. */

    if (vkd3d_descriptor_debug_active_descriptor_qa_checks())
        return;

#ifdef VKD3D_ENABLE_PROFILING
    /* For now, we don't do vtable variant shenanigans for profiled devices.
     * This can be fixed, but it's not that important at this time. */
    if (vkd3d_uses_profiling())
        return;
#endif

    /* Add special optimized paths that are tailored for known configurations.
     * If we don't find any, fall back to the generic path
     * (which is still very fast, but every nanosecond counts in these functions). */

    /* Don't bother modifying CopyDescriptors path, its main overhead is chasing other pointers anyway,
     * and that code path handles embedded mutable descriptors. */

    if (d3d12_device_use_embedded_mutable_descriptors(device))
    {
        if ((device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_EMBEDDED_PACKED_METADATA) &&
                device->bindless_state.descriptor_buffer_cbv_srv_uav_size == 64 &&
                device->bindless_state.descriptor_buffer_sampler_size == 16)
        {
            device->ID3D12Device_iface.lpVtbl = &d3d12_device_vtbl_embedded_64_16_packed;
        }
        else if (!(device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_EMBEDDED_PACKED_METADATA) &&
                device->bindless_state.descriptor_buffer_cbv_srv_uav_size == 32 &&
                device->bindless_state.descriptor_buffer_sampler_size == 16)
        {
            device->ID3D12Device_iface.lpVtbl = &d3d12_device_vtbl_embedded_32_16_planar;
        }
        else
        {
            device->ID3D12Device_iface.lpVtbl = &d3d12_device_vtbl_embedded_generic;
        }
    }
    else if (d3d12_device_uses_descriptor_buffers(device))
    {
        /* Matches NVIDIA on Turing+.
         * Only attempt to optimize the copy itself by avoid various indirections. */
        if (device->device_info.mutable_descriptor_features.mutableDescriptorType &&
                !(device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_TYPE_RAW_SSBO) &&
                device->bindless_state.descriptor_buffer_cbv_srv_uav_size == 16 &&
                device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize == 16 &&
                device->bindless_state.descriptor_buffer_sampler_size == 4)
        {
            device->ID3D12Device_iface.lpVtbl = &d3d12_device_vtbl_descriptor_buffer_16_16_4;
        }
        else if (device->device_info.mutable_descriptor_features.mutableDescriptorType &&
                !(device->bindless_state.flags & VKD3D_BINDLESS_MUTABLE_TYPE_RAW_SSBO) &&
                device->bindless_state.descriptor_buffer_cbv_srv_uav_size == 64 &&
                device->device_info.descriptor_buffer_properties.robustStorageBufferDescriptorSize == 64 &&
                device->bindless_state.descriptor_buffer_sampler_size == 32)
        {
            /* Matches Intel Arc on ANV. */
            device->ID3D12Device_iface.lpVtbl = &d3d12_device_vtbl_descriptor_buffer_64_64_32;
        }
    }
}

extern CONST_VTBL struct ID3D12DeviceExt1Vtbl d3d12_device_vkd3d_ext_vtbl;
extern CONST_VTBL struct ID3D12DXVKInteropDevice1Vtbl d3d12_dxvk_interop_device_vtbl;
extern CONST_VTBL struct ID3DLowLatencyDeviceVtbl d3d_low_latency_device_vtbl;

static void vkd3d_scratch_pool_init(struct d3d12_device *device)
{
    unsigned int i;

    for (i = 0; i < VKD3D_SCRATCH_POOL_KIND_COUNT; i++)
    {
        device->scratch_pools[i].block_size = VKD3D_SCRATCH_BUFFER_SIZE_DEFAULT;
        device->scratch_pools[i].scratch_buffer_size = VKD3D_SCRATCH_BUFFER_COUNT_DEFAULT;
    }

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_REQUIRES_COMPUTE_INDIRECT_TEMPLATES) &&
            device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY &&
            (device->device_info.device_generated_commands_compute_features_nv.deviceGeneratedCompute ||
                    device->device_info.device_generated_commands_features_ext.deviceGeneratedCommands))
    {
        /* DGCC preprocess buffers are gigantic on NV. Starfield requires 27 MB for 4096 dispatches ... */
        device->scratch_pools[VKD3D_SCRATCH_POOL_KIND_INDIRECT_PREPROCESS].block_size =
                VKD3D_SCRATCH_BUFFER_SIZE_DGCC_PREPROCESS_NV;
    }

    /* DGC tends to be pretty spammy with indirect buffers.
     * Tuned for Starfield which is the "worst case scenario" so far. */
    device->scratch_pools[VKD3D_SCRATCH_POOL_KIND_INDIRECT_PREPROCESS].scratch_buffer_size =
            VKD3D_SCRATCH_BUFFER_COUNT_INDIRECT_PREPROCESS;
}

static HRESULT d3d12_device_create_sparse_init_timeline(struct d3d12_device *device)
{
    if (!device->queue_families[VKD3D_QUEUE_FAMILY_SPARSE_BINDING])
        return S_OK;

    return vkd3d_create_timeline_semaphore(device, 0, false, &device->sparse_init_timeline);
}

static void d3d12_device_reserve_internal_sparse_queue(struct d3d12_device *device)
{
    /* This cannot fail. We're not allocating memory here. */
    if (device->queue_families[VKD3D_QUEUE_FAMILY_SPARSE_BINDING])
    {
        device->internal_sparse_queue = d3d12_device_allocate_vkd3d_queue(
                device->queue_families[VKD3D_QUEUE_FAMILY_SPARSE_BINDING], NULL);
    }
}

static HRESULT d3d12_device_init(struct d3d12_device *device,
        struct vkd3d_instance *instance, const struct vkd3d_device_create_info *create_info)
{
    const struct vkd3d_vk_device_procs *vk_procs;
    HRESULT hr;
    int rc;

#ifdef VKD3D_ENABLE_PROFILING
    if (vkd3d_uses_profiling())
        device->ID3D12Device_iface.lpVtbl = &d3d12_device_vtbl_profiled;
    else
        device->ID3D12Device_iface.lpVtbl = &d3d12_device_vtbl_default;
#else
    device->ID3D12Device_iface.lpVtbl = &d3d12_device_vtbl_default;
#endif

    device->refcount = 1;

    vkd3d_instance_incref(device->vkd3d_instance = instance);
    device->vk_info = instance->vk_info;
    device->vk_info.extension_count = 0;
    device->vk_info.extension_names = NULL;

    device->adapter_luid = create_info->adapter_luid;
    device->removed_reason = S_OK;

    device->vk_device = VK_NULL_HANDLE;

    if ((rc = pthread_mutex_init(&device->mutex, NULL)))
    {
        ERR("Failed to initialize mutex, error %d.\n", rc);
        hr = hresult_from_errno(rc);
        goto out_free_instance;
    }

    if ((rc = pthread_mutex_init(&device->global_submission_mutex, NULL)))
    {
        ERR("Failed to initialize mutex, error %d.\n", rc);
        hr = hresult_from_errno(rc);
        goto out_free_mutex;
    }

    spinlock_init(&device->low_latency_swapchain_spinlock);

    device->ID3D12DeviceExt_iface.lpVtbl = &d3d12_device_vkd3d_ext_vtbl;
    device->ID3D12DXVKInteropDevice_iface.lpVtbl = &d3d12_dxvk_interop_device_vtbl;
    device->ID3DLowLatencyDevice_iface.lpVtbl = &d3d_low_latency_device_vtbl;

    if ((rc = rwlock_init(&device->vertex_input_lock)))
    {
        hr = hresult_from_errno(rc);
        goto out_free_global_submission_mutex;
    }

    if ((rc = rwlock_init(&device->fragment_output_lock)))
    {
        hr = hresult_from_errno(rc);
        goto out_free_vertex_input_lock;
    }

    if (FAILED(hr = vkd3d_create_vk_device(device, create_info)))
        goto out_free_fragment_output_lock;

    if (FAILED(hr = vkd3d_private_store_init(&device->private_store)))
        goto out_free_vk_resources;

    if (FAILED(hr = vkd3d_memory_transfer_queue_init(&device->memory_transfers, device)))
        goto out_free_private_store;

    if (FAILED(hr = vkd3d_memory_allocator_init(&device->memory_allocator, device)))
        goto out_free_memory_transfers;

    if (FAILED(hr = vkd3d_init_format_info(device)))
        goto out_free_memory_allocator;

    if (FAILED(hr = vkd3d_memory_info_init(&device->memory_info, device)))
        goto out_cleanup_format_info;

    if (FAILED(hr = vkd3d_global_descriptor_buffer_init(&device->global_descriptor_buffer, device)))
        goto out_cleanup_memory_info;

    if (FAILED(hr = vkd3d_bindless_state_init(&device->bindless_state, device)))
        goto out_cleanup_global_descriptor_buffer;

    if (FAILED(hr = vkd3d_view_map_init(&device->sampler_map)))
        goto out_cleanup_bindless_state;

    if (FAILED(hr = vkd3d_sampler_state_init(&device->sampler_state, device)))
        goto out_cleanup_view_map;

    if (FAILED(hr = d3d12_device_create_sparse_init_timeline(device)))
        goto out_cleanup_sampler_state;

    if (FAILED(hr = vkd3d_meta_ops_init(&device->meta_ops, device)))
        goto out_cleanup_sparse_timeline;

    if (FAILED(hr = vkd3d_shader_debug_ring_init(&device->debug_ring, device)))
        goto out_cleanup_meta_ops;

    if (FAILED(hr = vkd3d_queue_timeline_trace_init(&device->queue_timeline_trace, device)))
        goto out_cleanup_debug_ring;

    if (FAILED(hr = vkd3d_address_binding_tracker_init(&device->address_binding_tracker, device)))
        goto out_cleanup_queue_timeline_trace;

    vkd3d_scratch_pool_init(device);

#ifdef VKD3D_ENABLE_BREADCRUMBS
    vkd3d_breadcrumb_tracer_init_barrier_hashes(&device->breadcrumb_tracer);
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
    {
        if (FAILED(hr = vkd3d_breadcrumb_tracer_init(&device->breadcrumb_tracer, device)))
        {
            vkd3d_breadcrumb_tracer_cleanup_barrier_hashes(&device->breadcrumb_tracer);
            goto out_cleanup_address_binding_tracker;
        }
    }
#endif

    if (vkd3d_descriptor_debug_active_instruction_qa_checks() || vkd3d_descriptor_debug_active_descriptor_qa_checks())
    {
        if (FAILED(hr = vkd3d_descriptor_debug_alloc_global_info(&device->descriptor_qa_global_info,
                VKD3D_DESCRIPTOR_DEBUG_DEFAULT_NUM_COOKIES, device)))
            goto out_cleanup_breadcrumb_tracer;
    }

    hash_map_init(&device->vertex_input_pipelines,
            vkd3d_vertex_input_pipeline_desc_hash,
            vkd3d_vertex_input_pipeline_desc_compare,
            sizeof(struct vkd3d_vertex_input_pipeline));

    hash_map_init(&device->fragment_output_pipelines,
            vkd3d_fragment_output_pipeline_desc_hash,
            vkd3d_fragment_output_pipeline_desc_compare,
            sizeof(struct vkd3d_fragment_output_pipeline));

    if ((device->parent = create_info->parent))
        IUnknown_AddRef(device->parent);

    d3d12_device_caps_init(device);

    vkd3d_init_shader_extensions(device);
    vkd3d_compute_shader_interface_key(device);

    /* Make sure all extensions and shader interface keys are computed. */
    if (FAILED(hr = vkd3d_pipeline_library_init_disk_cache(&device->disk_cache, device)))
        goto out_cleanup_descriptor_qa_global_info;

    d3d12_device_replace_vtable(device);

#ifdef VKD3D_ENABLE_RENDERDOC
    if (vkd3d_renderdoc_active() && vkd3d_renderdoc_global_capture_enabled())
        vkd3d_renderdoc_begin_capture(device->vkd3d_instance->vk_instance);
#endif

    d3d12_device_reserve_internal_sparse_queue(device);
    d3d_destruction_notifier_init(&device->destruction_notifier, (IUnknown*)&device->ID3D12Device_iface);
    return S_OK;

out_cleanup_descriptor_qa_global_info:
    vkd3d_descriptor_debug_free_global_info(device->descriptor_qa_global_info, device);
out_cleanup_breadcrumb_tracer:
#ifdef VKD3D_ENABLE_BREADCRUMBS
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
        vkd3d_breadcrumb_tracer_cleanup(&device->breadcrumb_tracer, device);
out_cleanup_address_binding_tracker:
#endif
    vkd3d_address_binding_tracker_cleanup(&device->address_binding_tracker, device);
out_cleanup_queue_timeline_trace:
    vkd3d_queue_timeline_trace_cleanup(&device->queue_timeline_trace);
out_cleanup_debug_ring:
    vkd3d_shader_debug_ring_cleanup(&device->debug_ring, device);
out_cleanup_meta_ops:
    vkd3d_meta_ops_cleanup(&device->meta_ops, device);
out_cleanup_sparse_timeline:
    vk_procs = &device->vk_procs;
    VK_CALL(vkDestroySemaphore(device->vk_device, device->sparse_init_timeline, NULL));
out_cleanup_sampler_state:
    vkd3d_sampler_state_cleanup(&device->sampler_state, device);
out_cleanup_view_map:
    vkd3d_view_map_destroy(&device->sampler_map, device);
out_cleanup_bindless_state:
    vkd3d_bindless_state_cleanup(&device->bindless_state, device);
out_cleanup_global_descriptor_buffer:
    vkd3d_global_descriptor_buffer_cleanup(&device->global_descriptor_buffer, device);
out_cleanup_memory_info:
    vkd3d_memory_info_cleanup(&device->memory_info, device);
out_cleanup_format_info:
    vkd3d_cleanup_format_info(device);
out_free_memory_allocator:
    vkd3d_memory_allocator_cleanup(&device->memory_allocator, device);
out_free_memory_transfers:
    vkd3d_memory_transfer_queue_cleanup(&device->memory_transfers);
out_free_private_store:
    vkd3d_private_store_destroy(&device->private_store);
out_free_vk_resources:
    d3d12_device_destroy_vkd3d_queues(device);
    vk_procs = &device->vk_procs;
    VK_CALL(vkDestroyDevice(device->vk_device, NULL));
out_free_instance:
    vkd3d_instance_decref(device->vkd3d_instance);
out_free_fragment_output_lock:
    rwlock_destroy(&device->fragment_output_lock);
out_free_vertex_input_lock:
    rwlock_destroy(&device->vertex_input_lock);
out_free_global_submission_mutex:
    pthread_mutex_destroy(&device->global_submission_mutex);
out_free_mutex:
    pthread_mutex_destroy(&device->mutex);
    return hr;
}

bool d3d12_device_validate_shader_meta(struct d3d12_device *device, const struct vkd3d_shader_meta *meta)
{
    if ((meta->flags & VKD3D_SHADER_META_FLAG_USES_NATIVE_16BIT_OPERATIONS) &&
            !device->d3d12_caps.options4.Native16BitShaderOpsSupported)
    {
        WARN("Attempting to use 16-bit operations in shader %016"PRIx64", but this is not supported.\n", meta->hash);
        return false;
    }

    if ((meta->flags & VKD3D_SHADER_META_FLAG_USES_FP64) && !device->d3d12_caps.options.DoublePrecisionFloatShaderOps)
    {
        WARN("Attempting to use FP64 operations in shader %016"PRIx64", but this is not supported.\n", meta->hash);
        return false;
    }

    if ((meta->flags & VKD3D_SHADER_META_FLAG_USES_INT64) && !device->d3d12_caps.options1.Int64ShaderOps)
    {
        WARN("Attempting to use Int64 operations in shader %016"PRIx64", but this is not supported.\n", meta->hash);
        return false;
    }

    /* This check isn't 100% precise since we cannot distinguish these individual features,
     * but allow it if one of the 64-bit atomic features is supported. */
    if ((meta->flags & VKD3D_SHADER_META_FLAG_USES_INT64_ATOMICS) &&
            !device->d3d12_caps.options9.AtomicInt64OnGroupSharedSupported &&
            !device->d3d12_caps.options11.AtomicInt64OnDescriptorHeapResourceSupported &&
            !device->d3d12_caps.options9.AtomicInt64OnTypedResourceSupported)
    {
        WARN("Attempting to use Int64Atomic operations in shader %016"PRIx64", but this is not supported.\n", meta->hash);
        return false;
    }

    if ((meta->flags & VKD3D_SHADER_META_FLAG_USES_INT64_ATOMICS_IMAGE) &&
            !device->device_info.shader_image_atomic_int64_features.shaderImageInt64Atomics)
    {
        WARN("Attempting to use typed Int64Atomic operations in shader %016"PRIx64", but this is not supported.\n", meta->hash);
        return false;
    }

    if ((meta->flags & VKD3D_SHADER_META_FLAG_USES_FRAGMENT_BARYCENTRIC) &&
            !device->d3d12_caps.options3.BarycentricsSupported)
    {
        WARN("Attempting to use barycentrics in shader %016"PRIx64", but this is not supported.\n", meta->hash);
        return false;
    }

    if ((meta->flags & VKD3D_SHADER_META_FLAG_USES_STENCIL_EXPORT) &&
            !device->d3d12_caps.options.PSSpecifiedStencilRefSupported)
    {
        WARN("Attempting to use stencil reference in shader %016"PRIx64", but this is not supported.\n", meta->hash);
        return false;
    }

    if ((meta->flags & VKD3D_SHADER_META_FLAG_USES_FRAGMENT_FULLY_COVERED) &&
            device->d3d12_caps.options.ConservativeRasterizationTier < D3D12_CONSERVATIVE_RASTERIZATION_TIER_3)
    {
        WARN("Attempting to use fragment fully covered in shader %016"PRIx64", but this requires conservative raster tier 3.\n", meta->hash);
        return false;
    }

    /* From Vulkan 1.2 promotion of the extension:
     * Enabling both features is equivalent to enabling the VK_EXT_shader_viewport_index_layer extension. */
    if ((meta->flags & VKD3D_SHADER_META_FLAG_USES_SHADER_VIEWPORT_INDEX_LAYER) &&
            (!device->device_info.vulkan_1_2_features.shaderOutputLayer ||
                    !device->device_info.vulkan_1_2_features.shaderOutputViewportIndex))
    {
        WARN("Attempting to use shader viewport index layer in shader %016"PRIx64", but this requires VK_EXT_shader_viewport_index_layer.\n", meta->hash);
        return false;
    }

    if ((meta->flags & VKD3D_SHADER_META_FLAG_USES_SPARSE_RESIDENCY) &&
            device->d3d12_caps.options.TiledResourcesTier < D3D12_TILED_RESOURCES_TIER_2)
    {
        WARN("Attempting to use sparse residency query in shader %016"PRIx64", but this requires sparse tier 2.\n", meta->hash);
        return false;
    }

    if ((meta->flags & VKD3D_SHADER_META_FLAG_USES_RASTERIZER_ORDERED_VIEWS) &&
            !device->d3d12_caps.options.ROVsSupported)
    {
        WARN("Attempting to use rasterizer ordered views in shader %016"PRIx64", but this requires capability to be supported.\n", meta->hash);
        return false;
    }

    if (meta->cs_wave_size_min)
    {
        const struct vkd3d_physical_device_info *info = &device->device_info;

        if (!d3d12_device_supports_required_subgroup_size_for_stage(device, VK_SHADER_STAGE_COMPUTE_BIT))
        {
            ERR("Required subgroup size control features are not supported for SM 6.6 WaveSize.\n");
            return false;
        }

        if (meta->cs_wave_size_min > info->vulkan_1_3_properties.maxSubgroupSize ||
                meta->cs_wave_size_max < info->vulkan_1_3_properties.minSubgroupSize)
        {
            ERR("Required WaveSize range [%u, %u], but supported range is [%u, %u].\n",
                    meta->cs_wave_size_min, meta->cs_wave_size_max,
                    info->vulkan_1_3_properties.minSubgroupSize,
                    info->vulkan_1_3_properties.maxSubgroupSize);
            return false;
        }
    }

    if (meta->flags & VKD3D_SHADER_META_FLAG_USES_COOPERATIVE_MATRIX)
    {
        if (!device->device_info.cooperative_matrix_features.cooperativeMatrix)
        {
            ERR("Missing sufficient features to expose WMMA.\n");
            return false;
        }
    }

    if (meta->flags & VKD3D_SHADER_META_FLAG_USES_COOPERATIVE_MATRIX_FP8)
    {
        if (!device->device_info.shader_float8_features.shaderFloat8CooperativeMatrix)
        {
            ERR("Missing sufficient features to expose WMMA FP8.\n");
            return false;
        }
    }

    return true;
}

HRESULT d3d12_device_create(struct vkd3d_instance *instance,
        const struct vkd3d_device_create_info *create_info, struct d3d12_device **device)
{
    struct d3d12_device *object;
    HRESULT hr;

    pthread_mutex_lock(&d3d12_device_map_mutex);
    if ((object = d3d12_find_device_singleton(create_info->adapter_luid)))
    {
        TRACE("Returned existing singleton device %p.\n", object);

        d3d12_device_add_ref(*device = object);
        pthread_mutex_unlock(&d3d12_device_map_mutex);
        return S_OK;
    }

    if (!(object = vkd3d_malloc_aligned(sizeof(*object), 64)))
    {
        pthread_mutex_unlock(&d3d12_device_map_mutex);
        return E_OUTOFMEMORY;
    }

    memset(object, 0, sizeof(*object));

    if (FAILED(hr = d3d12_device_init(object, instance, create_info)))
    {
        vkd3d_free_aligned(object);
        pthread_mutex_unlock(&d3d12_device_map_mutex);
        return hr;
    }

    if (!d3d12_device_supports_feature_level(object, create_info->minimum_feature_level))
    {
        WARN("Feature level %#x is not supported.\n", create_info->minimum_feature_level);
        d3d12_device_destroy(object);
        vkd3d_free_aligned(object);
        pthread_mutex_unlock(&d3d12_device_map_mutex);
        return E_INVALIDARG;
    }

    TRACE("Created device %p (dummy d3d12_device_AddRef for debug grep purposes).\n", object);

    d3d12_add_device_singleton(object, create_info->adapter_luid);

    pthread_mutex_unlock(&d3d12_device_map_mutex);

    *device = object;

    return S_OK;
}

void d3d12_device_report_fault(struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    static pthread_mutex_t report_lock = PTHREAD_MUTEX_INITIALIZER;
    VkDeviceFaultCountsEXT fault_counts;
    VkDeviceFaultInfoEXT fault_info;
    static bool reported = false;
    VkResult vr;
    uint32_t i;

    d3d12_device_mark_as_removed(device, DXGI_ERROR_DEVICE_REMOVED, "VK_ERROR_DEVICE_LOST");

    if (!device->device_info.fault_features.deviceFault)
        return;

    fault_counts.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
    fault_counts.pNext = NULL;
    if ((vr = VK_CALL(vkGetDeviceFaultInfoEXT(device->vk_device, &fault_counts, NULL)) < 0))
    {
        ERR("Failed to query device fault info, vr %d.\n", vr);
        return;
    }

    pthread_mutex_lock(&report_lock);
    if (reported)
    {
        pthread_mutex_unlock(&report_lock);
        return;
    }
    reported = true;

    memset(&fault_info, 0, sizeof(fault_info));
    fault_info.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT;

    /* Don't have to explicitly check vendor binary feature,
     * implementations must return 0 size if not enabled. */
    fault_info.pAddressInfos = vkd3d_calloc(fault_counts.addressInfoCount, sizeof(*fault_info.pAddressInfos));
    fault_info.pVendorBinaryData = vkd3d_malloc(fault_counts.vendorBinarySize);
    fault_info.pVendorInfos = vkd3d_calloc(fault_counts.vendorInfoCount, sizeof(*fault_info.pVendorInfos));

    vr = VK_CALL(vkGetDeviceFaultInfoEXT(device->vk_device, &fault_counts, &fault_info));

    if (vr < 0)
    {
        ERR("Failed to query device fault info, vr %d.\n", vr);
    }
    else
    {
        static const char *address_type_to_str[] =
        {
            "N/A",
            "ReadInvalid",
            "WriteInvalid",
            "ExecuteInvalid",
            "UnknownPC",
            "InvalidPC",
            "FaultPC",
        };

        ERR("DEVICE_LOST received, reporting fault.\n");
        ERR("Desc: %s\n", fault_info.description);

        for (i = 0; i < fault_counts.addressInfoCount; i++)
        {
            const VkDeviceFaultAddressInfoEXT *addr = &fault_info.pAddressInfos[i];
            const char *type;

            if (addr->addressType < ARRAY_SIZE(address_type_to_str))
                type = address_type_to_str[addr->addressType];
            else
                type = "?";

            ERR("Address [%u]: %016"PRIx64" (granularity %"PRIx64"), type %s\n", i,
                    addr->reportedAddress, addr->addressPrecision, type);

            vkd3d_address_binding_tracker_check_va(&device->address_binding_tracker, addr->reportedAddress);
        }

        for (i = 0; i < fault_counts.vendorInfoCount; i++)
        {
            const VkDeviceFaultVendorInfoEXT *vend = &fault_info.pVendorInfos[i];
            ERR("Vendor [%u]: (code #%"PRIx64") (data #%"PRIx64") %s\n",
                    i, vend->vendorFaultCode, vend->vendorFaultData,
                    vend->description);
        }

        if (fault_counts.vendorBinarySize >= sizeof(VkDeviceFaultVendorBinaryHeaderVersionOneEXT))
        {
            const VkDeviceFaultVendorBinaryHeaderVersionOneEXT *header = fault_info.pVendorBinaryData;
            if (header->headerVersion == VK_DEVICE_FAULT_VENDOR_BINARY_HEADER_VERSION_ONE_EXT &&
                    header->headerSize <= fault_counts.vendorBinarySize)
            {
                const char *path = "vkd3d-proton.fault.bin";
                char cache_uuid[VK_UUID_SIZE * 2 + 1];
                FILE *file;

                ERR("Dumping vendor blob to \"%s\".\n", path);

                ERR("vendorID: #%x\n", header->vendorID);
                ERR("driverVersion: #%x\n", header->driverVersion);
                ERR("deviceID: #%x\n", header->deviceID);
                ERR("apiVersion: #%x\n", header->apiVersion);
                if (header->applicationNameOffset)
                {
                    ERR("applicationName: %s\n",
                            ((const char *)fault_info.pVendorBinaryData) + header->applicationNameOffset);
                    ERR("applicationVersion: #%x\n", header->applicationVersion);
                }

                if (header->engineNameOffset)
                {
                    ERR("engineName: %s\n", ((const char *)fault_info.pVendorBinaryData) + header->engineNameOffset);
                    ERR("engineVersion: #%x\n", header->engineVersion);
                }

                for (i = 0; i < VK_UUID_SIZE; i++)
                    sprintf(cache_uuid + i * 2, "%02x", header->pipelineCacheUUID[i]);

                ERR("pipelineCacheUUID: %s\n", cache_uuid);

                file = fopen(path, "wb");
                if (file)
                {
                    size_t write_size = fault_counts.vendorBinarySize - header->headerSize;
                    if (fwrite((const uint8_t *)fault_info.pVendorBinaryData + header->headerSize, 1,
                            write_size, file) != write_size)
                    {
                        ERR("Failed to write fault file.\n");
                    }
                    fclose(file);
                }
                else
                    ERR("Failed to open fault file for writing.\n");
            }
            else
                ERR("Binary header is not version one as expected.\n");
        }
    }

    pthread_mutex_unlock(&report_lock);
    vkd3d_free(fault_info.pAddressInfos);
    vkd3d_free(fault_info.pVendorBinaryData);
    vkd3d_free(fault_info.pVendorInfos);
}

void d3d12_device_mark_as_removed(struct d3d12_device *device, HRESULT reason,
        const char *message, ...)
{
    va_list args;

    va_start(args, message);
    WARN("Device %p is lost (reason %#x, \"%s\").\n",
            device, reason, vkd3d_dbg_vsprintf(message, args));
    va_end(args);

    vkd3d_atomic_uint32_store_explicit(&device->removed_reason, reason, vkd3d_memory_order_release);
}

VkPipeline d3d12_device_get_or_create_vertex_input_pipeline(struct d3d12_device *device,
        const struct vkd3d_vertex_input_pipeline_desc *desc)
{
    struct vkd3d_vertex_input_pipeline pipeline, *entry;

    memset(&pipeline, 0, sizeof(pipeline));

    rwlock_lock_read(&device->vertex_input_lock);
    entry = (void*)hash_map_find(&device->vertex_input_pipelines, desc);

    if (entry)
        pipeline.vk_pipeline = entry->vk_pipeline;

    rwlock_unlock_read(&device->vertex_input_lock);

    if (!pipeline.vk_pipeline)
    {
        rwlock_lock_write(&device->vertex_input_lock);
        pipeline.desc = *desc;

        entry = (void*)hash_map_insert(&device->vertex_input_pipelines, desc, &pipeline.entry);

        if (!entry->vk_pipeline)
            entry->vk_pipeline = vkd3d_vertex_input_pipeline_create(device, desc);

        pipeline.vk_pipeline = entry->vk_pipeline;
        rwlock_unlock_write(&device->vertex_input_lock);
    }

    return pipeline.vk_pipeline;
}

VkPipeline d3d12_device_get_or_create_fragment_output_pipeline(struct d3d12_device *device,
        const struct vkd3d_fragment_output_pipeline_desc *desc)
{
    struct vkd3d_fragment_output_pipeline pipeline, *entry;

    memset(&pipeline, 0, sizeof(pipeline));

    rwlock_lock_read(&device->fragment_output_lock);
    entry = (void*)hash_map_find(&device->fragment_output_pipelines, desc);

    if (entry)
        pipeline.vk_pipeline = entry->vk_pipeline;

    rwlock_unlock_read(&device->fragment_output_lock);

    if (!pipeline.vk_pipeline)
    {
        rwlock_lock_write(&device->fragment_output_lock);
        pipeline.desc = *desc;

        entry = (void*)hash_map_insert(&device->fragment_output_pipelines, desc, &pipeline.entry);

        if (!entry->vk_pipeline)
            entry->vk_pipeline = vkd3d_fragment_output_pipeline_create(device, desc);

        pipeline.vk_pipeline = entry->vk_pipeline;
        rwlock_unlock_write(&device->fragment_output_lock);
    }

    return pipeline.vk_pipeline;
}

IUnknown *vkd3d_get_device_parent(ID3D12Device *device)
{
    struct d3d12_device *d3d12_device = impl_from_ID3D12Device((d3d12_device_iface *)device);

    return d3d12_device->parent;
}

VkDevice vkd3d_get_vk_device(ID3D12Device *device)
{
    struct d3d12_device *d3d12_device = impl_from_ID3D12Device((d3d12_device_iface *)device);

    return d3d12_device->vk_device;
}

VkPhysicalDevice vkd3d_get_vk_physical_device(ID3D12Device *device)
{
    struct d3d12_device *d3d12_device = impl_from_ID3D12Device((d3d12_device_iface *)device);

    return d3d12_device->vk_physical_device;
}

struct vkd3d_instance *vkd3d_instance_from_device(ID3D12Device *device)
{
    struct d3d12_device *d3d12_device = impl_from_ID3D12Device((d3d12_device_iface *)device);

    return d3d12_device->vkd3d_instance;
}
