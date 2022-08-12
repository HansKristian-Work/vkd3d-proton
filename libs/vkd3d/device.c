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
    uint64_t required_config_flags;
};

#define VK_EXTENSION(name, member) \
        {VK_ ## name ## _EXTENSION_NAME, offsetof(struct vkd3d_vulkan_info, member), 0}

#define VK_EXTENSION_COND(name, member, required_flags) \
        {VK_ ## name ## _EXTENSION_NAME, offsetof(struct vkd3d_vulkan_info, member), required_flags}

static const struct vkd3d_optional_extension_info optional_instance_extensions[] =
{
    /* EXT extensions */
    VK_EXTENSION_COND(EXT_DEBUG_UTILS, EXT_debug_utils, VKD3D_CONFIG_FLAG_DEBUG_UTILS),
};

static const struct vkd3d_optional_extension_info optional_device_extensions[] =
{
    /* KHR extensions */
    VK_EXTENSION(KHR_BUFFER_DEVICE_ADDRESS, KHR_buffer_device_address),
    VK_EXTENSION(KHR_DRAW_INDIRECT_COUNT, KHR_draw_indirect_count),
    VK_EXTENSION(KHR_IMAGE_FORMAT_LIST, KHR_image_format_list),
    VK_EXTENSION(KHR_PUSH_DESCRIPTOR, KHR_push_descriptor),
    VK_EXTENSION(KHR_TIMELINE_SEMAPHORE, KHR_timeline_semaphore),
    VK_EXTENSION(KHR_SHADER_FLOAT16_INT8, KHR_shader_float16_int8),
    VK_EXTENSION(KHR_SHADER_SUBGROUP_EXTENDED_TYPES, KHR_shader_subgroup_extended_types),
    VK_EXTENSION_COND(KHR_RAY_TRACING_PIPELINE, KHR_ray_tracing_pipeline, VKD3D_CONFIG_FLAG_DXR),
    VK_EXTENSION_COND(KHR_ACCELERATION_STRUCTURE, KHR_acceleration_structure, VKD3D_CONFIG_FLAG_DXR),
    VK_EXTENSION_COND(KHR_DEFERRED_HOST_OPERATIONS, KHR_deferred_host_operations, VKD3D_CONFIG_FLAG_DXR),
    VK_EXTENSION_COND(KHR_PIPELINE_LIBRARY, KHR_pipeline_library, VKD3D_CONFIG_FLAG_DXR),
    VK_EXTENSION_COND(KHR_RAY_QUERY, KHR_ray_query, VKD3D_CONFIG_FLAG_DXR11),
    VK_EXTENSION_COND(KHR_RAY_TRACING_MAINTENANCE_1, KHR_ray_tracing_maintenance1, VKD3D_CONFIG_FLAG_DXR11),
    VK_EXTENSION(KHR_SPIRV_1_4, KHR_spirv_1_4),
    VK_EXTENSION(KHR_SHADER_FLOAT_CONTROLS, KHR_shader_float_controls),
    VK_EXTENSION(KHR_FRAGMENT_SHADING_RATE, KHR_fragment_shading_rate),
    /* Only required to silence validation errors. */
    VK_EXTENSION(KHR_CREATE_RENDERPASS_2, KHR_create_renderpass2),
    VK_EXTENSION(KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE, KHR_sampler_mirror_clamp_to_edge),
    VK_EXTENSION(KHR_SEPARATE_DEPTH_STENCIL_LAYOUTS, KHR_separate_depth_stencil_layouts),
    VK_EXTENSION(KHR_SHADER_INTEGER_DOT_PRODUCT, KHR_shader_integer_dot_product),
    VK_EXTENSION(KHR_FORMAT_FEATURE_FLAGS_2, KHR_format_feature_flags2),
    VK_EXTENSION(KHR_SHADER_ATOMIC_INT64, KHR_shader_atomic_int64),
    VK_EXTENSION(KHR_BIND_MEMORY_2, KHR_bind_memory2),
    VK_EXTENSION(KHR_COPY_COMMANDS_2, KHR_copy_commands2),
    VK_EXTENSION(KHR_DYNAMIC_RENDERING, KHR_dynamic_rendering),
    /* Only required to silence validation errors. */
    VK_EXTENSION(KHR_DEPTH_STENCIL_RESOLVE, KHR_depth_stencil_resolve),
    VK_EXTENSION(KHR_DRIVER_PROPERTIES, KHR_driver_properties),
    VK_EXTENSION(KHR_UNIFORM_BUFFER_STANDARD_LAYOUT, KHR_uniform_buffer_standard_layout),
    VK_EXTENSION(KHR_MAINTENANCE_4, KHR_maintenance4),
    VK_EXTENSION(KHR_FRAGMENT_SHADER_BARYCENTRIC, KHR_fragment_shader_barycentric),
#ifdef _WIN32
    VK_EXTENSION(KHR_EXTERNAL_MEMORY_WIN32, KHR_external_memory_win32),
    VK_EXTENSION(KHR_EXTERNAL_SEMAPHORE_WIN32, KHR_external_semaphore_win32),
#endif
    /* EXT extensions */
    VK_EXTENSION(EXT_CALIBRATED_TIMESTAMPS, EXT_calibrated_timestamps),
    VK_EXTENSION(EXT_CONDITIONAL_RENDERING, EXT_conditional_rendering),
    VK_EXTENSION(EXT_CONSERVATIVE_RASTERIZATION, EXT_conservative_rasterization),
    VK_EXTENSION(EXT_CUSTOM_BORDER_COLOR, EXT_custom_border_color),
    VK_EXTENSION(EXT_DEPTH_CLIP_ENABLE, EXT_depth_clip_enable),
    VK_EXTENSION(EXT_DESCRIPTOR_INDEXING, EXT_descriptor_indexing),
    VK_EXTENSION(EXT_IMAGE_VIEW_MIN_LOD, EXT_image_view_min_lod),
    VK_EXTENSION(EXT_INLINE_UNIFORM_BLOCK, EXT_inline_uniform_block),
    VK_EXTENSION(EXT_ROBUSTNESS_2, EXT_robustness2),
    VK_EXTENSION(EXT_SAMPLER_FILTER_MINMAX, EXT_sampler_filter_minmax),
    VK_EXTENSION(EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION, EXT_shader_demote_to_helper_invocation),
    VK_EXTENSION(EXT_SHADER_STENCIL_EXPORT, EXT_shader_stencil_export),
    VK_EXTENSION(EXT_SHADER_VIEWPORT_INDEX_LAYER, EXT_shader_viewport_index_layer),
    VK_EXTENSION(EXT_SUBGROUP_SIZE_CONTROL, EXT_subgroup_size_control),
    VK_EXTENSION(EXT_TEXEL_BUFFER_ALIGNMENT, EXT_texel_buffer_alignment),
    VK_EXTENSION(EXT_TRANSFORM_FEEDBACK, EXT_transform_feedback),
    VK_EXTENSION(EXT_VERTEX_ATTRIBUTE_DIVISOR, EXT_vertex_attribute_divisor),
    VK_EXTENSION(EXT_EXTENDED_DYNAMIC_STATE, EXT_extended_dynamic_state),
    VK_EXTENSION(EXT_EXTENDED_DYNAMIC_STATE_2, EXT_extended_dynamic_state2),
    VK_EXTENSION(EXT_EXTERNAL_MEMORY_HOST, EXT_external_memory_host),
    VK_EXTENSION(EXT_4444_FORMATS, EXT_4444_formats),
    VK_EXTENSION(EXT_SHADER_IMAGE_ATOMIC_INT64, EXT_shader_image_atomic_int64),
    VK_EXTENSION(EXT_SCALAR_BLOCK_LAYOUT, EXT_scalar_block_layout),
    VK_EXTENSION(EXT_PIPELINE_CREATION_FEEDBACK, EXT_pipeline_creation_feedback),
    /* AMD extensions */
    VK_EXTENSION(AMD_BUFFER_MARKER, AMD_buffer_marker),
    VK_EXTENSION(AMD_DEVICE_COHERENT_MEMORY, AMD_device_coherent_memory),
    VK_EXTENSION(AMD_SHADER_CORE_PROPERTIES, AMD_shader_core_properties),
    VK_EXTENSION(AMD_SHADER_CORE_PROPERTIES_2, AMD_shader_core_properties2),
    /* NV extensions */
    VK_EXTENSION(NV_SHADER_SM_BUILTINS, NV_shader_sm_builtins),
    VK_EXTENSION(NVX_BINARY_IMPORT, NVX_binary_import),
    VK_EXTENSION(NVX_IMAGE_VIEW_HANDLE, NVX_image_view_handle),
    VK_EXTENSION(NV_FRAGMENT_SHADER_BARYCENTRIC, NV_fragment_shader_barycentric),
    VK_EXTENSION(NV_COMPUTE_SHADER_DERIVATIVES, NV_compute_shader_derivatives),
    VK_EXTENSION_COND(NV_DEVICE_DIAGNOSTIC_CHECKPOINTS, NV_device_diagnostic_checkpoints, VKD3D_CONFIG_FLAG_BREADCRUMBS),
    VK_EXTENSION(NV_DEVICE_GENERATED_COMMANDS, NV_device_generated_commands),
    /* VALVE extensions */
    VK_EXTENSION(VALVE_MUTABLE_DESCRIPTOR_TYPE, VALVE_mutable_descriptor_type),
    VK_EXTENSION(VALVE_DESCRIPTOR_SET_HOST_MAPPING, VALVE_descriptor_set_host_mapping),
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
        unsigned int count, const char *extension_name)
{
    unsigned int i;

    for (i = 0; i < count; ++i)
    {
        if (is_extension_disabled(extension_name))
        {
            WARN("Extension %s is disabled.\n", debugstr_a(extension_name));
            continue;
        }
        if (!strcmp(extensions[i].extensionName, extension_name))
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
        if (!has_extension(extensions, count, required_extensions[i]))
            ERR("Required %s extension %s is not supported.\n",
                    extension_type, debugstr_a(required_extensions[i]));
        ++extension_count;
    }

    for (i = 0; i < optional_extension_count; ++i)
    {
        const char *extension_name = optional_extensions[i].extension_name;
        uint64_t required_flags = optional_extensions[i].required_config_flags;
        bool has_required_flags = (vkd3d_config_flags & required_flags) == required_flags;
        ptrdiff_t offset = optional_extensions[i].vulkan_info_offset;
        bool *supported = (void *)((uintptr_t)vulkan_info + offset);

        if (!has_required_flags)
            continue;

        if ((*supported = has_extension(extensions, count, extension_name)))
        {
            TRACE("Found %s extension.\n", debugstr_a(extension_name));
            ++extension_count;
        }
    }

    for (i = 0; i < user_extension_count; ++i)
    {
        if (!has_extension(extensions, count, user_extensions[i]))
            ERR("Required user %s extension %s is not supported.\n",
                    extension_type, debugstr_a(user_extensions[i]));
        ++extension_count;
    }

    assert(!optional_user_extension_count || user_extension_supported);
    for (i = 0; i < optional_user_extension_count; ++i)
    {
        if (has_extension(extensions, count, optional_user_extensions[i]))
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

static unsigned int vkd3d_enable_extensions(const char *extensions[],
        const char * const *required_extensions, unsigned int required_extension_count,
        const struct vkd3d_optional_extension_info *optional_extensions, unsigned int optional_extension_count,
        const char * const *user_extensions, unsigned int user_extension_count,
        const char * const *optional_user_extensions, unsigned int optional_user_extension_count,
        bool *user_extension_supported, const struct vkd3d_vulkan_info *vulkan_info)
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
    HRESULT hr;

    if (!vkGetInstanceProcAddr)
    {
        if (!(instance->libvulkan = vkd3d_dlopen(SONAME_LIBVULKAN)))
        {
            ERR("Failed to load libvulkan: %s.\n", vkd3d_dlerror());
            return E_FAIL;
        }

        if (!(vkGetInstanceProcAddr = vkd3d_dlsym(instance->libvulkan, "vkGetInstanceProcAddr")))
        {
            ERR("Could not load function pointer for vkGetInstanceProcAddr().\n");
            vkd3d_dlclose(instance->libvulkan);
            instance->libvulkan = NULL;
            return E_FAIL;
        }
    }
    else
    {
        instance->libvulkan = NULL;
    }

    if (FAILED(hr = vkd3d_load_vk_global_procs(&instance->vk_global_procs, vkGetInstanceProcAddr)))
    {
        if (instance->libvulkan)
            vkd3d_dlclose(instance->libvulkan);
        instance->libvulkan = NULL;
        return hr;
    }

    return S_OK;
}

static VkBool32 VKAPI_PTR vkd3d_debug_messenger_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
        VkDebugUtilsMessageTypeFlagsEXT message_types,
        const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
        void *userdata)
{
    /* Avoid some useless validation warnings which don't contribute much.
     * - Map memory, likely validation layer bug due to memory alloc flags.
     * - Pipeline layout limits on NV which are not relevant here.
     * - SPV_EXT_buffer_device_address shenanigans (need to fix glslang).
     * - Sample count mismatch in fallback copy shaders.
     */
    unsigned int i;
    static const uint32_t ignored_ids[] = {
        0xc05b3a9du,
        0x2864340eu,
        0xbfcfaec2u,
        0x96f03c1cu,
        0x8189c842u,
        0x3d492883u,
        0x1608dec0u,
    };

    for (i = 0; i < ARRAY_SIZE(ignored_ids); i++)
        if ((uint32_t)callback_data->messageIdNumber == ignored_ids[i])
            return VK_FALSE;

    if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        ERR("%s\n", debugstr_a(callback_data->pMessage));
    else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        WARN("%s\n", debugstr_a(callback_data->pMessage));

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
    VKD3D_APPLICATION_FEATURE_OVERRIDE_PROMOTE_DXR_TO_ULTIMATE,
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
    { VKD3D_STRING_COMPARE_EXACT, "Deathloop.exe", VKD3D_CONFIG_FLAG_IGNORE_RTV_HOST_VISIBLE, 0 },
    /* Halo Infinite (1240440).
     * Game relies on NON_ZEROED committed UAVs to be cleared to zero on allocation.
     * This works okay with zerovram on first game boot, but not later, since this memory is guaranteed to be recycled.
     * Game also relies on indirectly modifying CBV root descriptors, which means we are forced to rely on RAW_VA_CBV. */
    { VKD3D_STRING_COMPARE_EXACT, "HaloInfinite.exe",
            VKD3D_CONFIG_FLAG_ZERO_MEMORY_WORKAROUNDS_COMMITTED_BUFFER_UAV | VKD3D_CONFIG_FLAG_FORCE_RAW_VA_CBV |
            VKD3D_CONFIG_FLAG_USE_HOST_IMPORT_FALLBACK, 0 },
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
    { VKD3D_STRING_COMPARE_EXACT, "Sam4.exe", VKD3D_CONFIG_FLAG_FORCE_NO_INVARIANT_POSITION, 0 },
    /* Cyberpunk 2077 (1091500). */
    { VKD3D_STRING_COMPARE_EXACT, "Cyberpunk2077.exe", VKD3D_CONFIG_FLAG_ALLOW_SBT_COLLECTION, 0 },
    /* Resident Evil: Village (1196590).
     * Game relies on mesh + sampler feedback to be exposed to use DXR.
     * Likely used as a proxy for Turing+ to avoid potential software fallbacks on Pascal. */
    { VKD3D_STRING_COMPARE_EXACT, "re8.exe",
            VKD3D_CONFIG_FLAG_FORCE_NATIVE_FP16, 0, VKD3D_APPLICATION_FEATURE_OVERRIDE_PROMOTE_DXR_TO_ULTIMATE },
    /* Resident Evil 2 remake (883710). Same as RE: Village. */
    { VKD3D_STRING_COMPARE_EXACT, "re2.exe",
            VKD3D_CONFIG_FLAG_FORCE_NATIVE_FP16, 0, VKD3D_APPLICATION_FEATURE_OVERRIDE_PROMOTE_DXR_TO_ULTIMATE },
    { VKD3D_STRING_COMPARE_EXACT, "re3.exe",
            VKD3D_CONFIG_FLAG_FORCE_NATIVE_FP16, 0, VKD3D_APPLICATION_FEATURE_OVERRIDE_PROMOTE_DXR_TO_ULTIMATE },
    { VKD3D_STRING_COMPARE_EXACT, "re7.exe",
            VKD3D_CONFIG_FLAG_FORCE_NATIVE_FP16, 0, VKD3D_APPLICATION_FEATURE_OVERRIDE_PROMOTE_DXR_TO_ULTIMATE },
    /* Control (870780).
     * Control.exe is the launcher - it doesn't dispaly anything and defaults to DX11 if DXR is not supported. */
    { VKD3D_STRING_COMPARE_EXACT, "Control.exe", VKD3D_CONFIG_FLAG_DXR, 0 },
    { VKD3D_STRING_COMPARE_EXACT, "Control_DX12.exe", VKD3D_CONFIG_FLAG_DXR, 0 },
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
};

static const struct vkd3d_shader_quirk_info ue4_quirks = {
    ue4_hashes, ARRAY_SIZE(ue4_hashes), 0,
};

static const struct vkd3d_shader_quirk_info f1_2019_2020_quirks = {
    NULL, 0, VKD3D_SHADER_QUIRK_FORCE_TGSM_BARRIERS,
};

static const struct vkd3d_shader_quirk_meta application_shader_quirks[] = {
    /* Unreal Engine 4 */
    { VKD3D_STRING_COMPARE_ENDS_WITH, "-Shipping.exe", &ue4_quirks },
    /* F1 2020 (1080110) */
    { VKD3D_STRING_COMPARE_EXACT, "F1_2020_dx12.exe", &f1_2019_2020_quirks },
    /* F1 2019 (928600) */
    { VKD3D_STRING_COMPARE_EXACT, "F1_2019_dx12.exe", &f1_2019_2020_quirks },
    /* MSVC fails to compile empty array. */
    { VKD3D_STRING_COMPARE_NEVER, NULL, NULL },
};

static void vkd3d_instance_apply_application_workarounds(void)
{
    char app[VKD3D_PATH_MAX];
    size_t i;
    if (!vkd3d_get_program_name(app))
        return;

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
            vkd3d_get_env_var("VKD3D_SHADER_DUMP_PATH", env, sizeof(env)))
    {
        INFO("VKD3D_SHADER_OVERRIDE or VKD3D_SHADER_DUMP_PATH is used, pipeline_library_ignore_spirv option is enforced.\n");
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
}

static void vkd3d_instance_apply_global_shader_quirks(void)
{
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
}

static const struct vkd3d_debug_option vkd3d_config_options[] =
{
    /* Enable Vulkan debug extensions. */
    {"vk_debug", VKD3D_CONFIG_FLAG_VULKAN_DEBUG},
    {"skip_application_workarounds", VKD3D_CONFIG_FLAG_SKIP_APPLICATION_WORKAROUNDS},
    {"debug_utils", VKD3D_CONFIG_FLAG_DEBUG_UTILS},
    {"force_static_cbv", VKD3D_CONFIG_FLAG_FORCE_STATIC_CBV},
    {"dxr", VKD3D_CONFIG_FLAG_DXR},
    {"dxr11", VKD3D_CONFIG_FLAG_DXR | VKD3D_CONFIG_FLAG_DXR11},
    {"single_queue", VKD3D_CONFIG_FLAG_SINGLE_QUEUE},
    {"descriptor_qa_checks", VKD3D_CONFIG_FLAG_DESCRIPTOR_QA_CHECKS},
    {"force_rtv_exclusive_queue", VKD3D_CONFIG_FLAG_FORCE_RTV_EXCLUSIVE_QUEUE},
    {"force_dsv_exclusive_queue", VKD3D_CONFIG_FLAG_FORCE_DSV_EXCLUSIVE_QUEUE},
    {"force_exclusive_queue", VKD3D_CONFIG_FLAG_FORCE_RTV_EXCLUSIVE_QUEUE | VKD3D_CONFIG_FLAG_FORCE_DSV_EXCLUSIVE_QUEUE},
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
    {"recycle_command_pools", VKD3D_CONFIG_FLAG_RECYCLE_COMMAND_POOLS},
    {"pipeline_library_ignore_mismatch_driver", VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_MISMATCH_DRIVER},
    {"breadcrumbs", VKD3D_CONFIG_FLAG_BREADCRUMBS},
    {"pipeline_library_app_cache", VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_APP_CACHE_ONLY},
    {"shader_cache_sync", VKD3D_CONFIG_FLAG_SHADER_CACHE_SYNC},
    {"force_raw_va_cbv", VKD3D_CONFIG_FLAG_FORCE_RAW_VA_CBV},
    {"allow_sbt_collection", VKD3D_CONFIG_FLAG_ALLOW_SBT_COLLECTION},
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

#ifdef VKD3D_ENABLE_RENDERDOC
    /* Enable debug utils by default for Renderdoc builds */
    TRACE("Renderdoc build implies VKD3D_CONFIG_FLAG_DEBUG_UTILS.\n");
    vkd3d_config_flags |= VKD3D_CONFIG_FLAG_DEBUG_UTILS;
#endif

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
    VkResult vr;
    HRESULT hr;
    uint32_t loader_version = VK_API_VERSION_1_0;

    TRACE("Build: %s.\n", vkd3d_version);

    memset(instance, 0, sizeof(*instance));

    if (!create_info->pfn_signal_event)
    {
        ERR("Invalid signal event function pointer.\n");
        return E_INVALIDARG;
    }
    if (!create_info->pfn_create_thread != !create_info->pfn_join_thread)
    {
        ERR("Invalid create/join thread function pointers.\n");
        return E_INVALIDARG;
    }

    instance->signal_event = create_info->pfn_signal_event;
    instance->create_thread = create_info->pfn_create_thread;
    instance->join_thread = create_info->pfn_join_thread;

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
        if (instance->libvulkan)
            vkd3d_dlclose(instance->libvulkan);
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
        if (instance->libvulkan)
            vkd3d_dlclose(instance->libvulkan);
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

    INFO("vkd3d-proton - build: %"PRIx64".\n", vkd3d_build);

    if (vkd3d_get_program_name(application_name))
        application_info.pApplicationName = application_name;

    TRACE("Application: %s.\n", debugstr_a(application_info.pApplicationName));

    if (!(extensions = vkd3d_calloc(extension_count, sizeof(*extensions))))
    {
        if (instance->libvulkan)
            vkd3d_dlclose(instance->libvulkan);
        vkd3d_free(user_extension_supported);
        return E_OUTOFMEMORY;
    }

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
    vkd3d_free((void *)extensions);
    if (vr < 0)
    {
        ERR("Failed to create Vulkan instance, vr %d.\n", vr);
        if (instance->libvulkan)
            vkd3d_dlclose(instance->libvulkan);
        return hresult_from_vk_result(vr);
    }

    if (FAILED(hr = vkd3d_load_vk_instance_procs(&instance->vk_procs, vk_global_procs, vk_instance)))
    {
        ERR("Failed to load instance procs, hr %#x.\n", hr);
        if (instance->vk_procs.vkDestroyInstance)
            instance->vk_procs.vkDestroyInstance(vk_instance, NULL);
        if (instance->libvulkan)
            vkd3d_dlclose(instance->libvulkan);
        return hr;
    }

    instance->vk_instance = vk_instance;
    instance->instance_version = loader_version;

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

VKD3D_EXPORT HRESULT vkd3d_create_instance(const struct vkd3d_instance_create_info *create_info,
        struct vkd3d_instance **instance)
{
    struct vkd3d_instance *object;
    HRESULT hr;

    TRACE("create_info %p, instance %p.\n", create_info, instance);

    vkd3d_init_profiling();

    if (!create_info || !instance)
        return E_INVALIDARG;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = vkd3d_instance_init(object, create_info)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created instance %p.\n", object);

    *instance = object;

    return S_OK;
}

static void vkd3d_destroy_instance(struct vkd3d_instance *instance)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &instance->vk_procs;
    VkInstance vk_instance = instance->vk_instance;

    if (instance->vk_debug_callback)
        VK_CALL(vkDestroyDebugUtilsMessengerEXT(vk_instance, instance->vk_debug_callback, NULL));

    VK_CALL(vkDestroyInstance(vk_instance, NULL));

    if (instance->libvulkan)
        vkd3d_dlclose(instance->libvulkan);

    vkd3d_free(instance);
}

VKD3D_EXPORT ULONG vkd3d_instance_incref(struct vkd3d_instance *instance)
{
    ULONG refcount = InterlockedIncrement(&instance->refcount);

    TRACE("%p increasing refcount to %u.\n", instance, refcount);

    return refcount;
}

VKD3D_EXPORT ULONG vkd3d_instance_decref(struct vkd3d_instance *instance)
{
    ULONG refcount = InterlockedDecrement(&instance->refcount);

    TRACE("%p decreasing refcount to %u.\n", instance, refcount);

    if (!refcount)
        vkd3d_destroy_instance(instance);

    return refcount;
}

VKD3D_EXPORT VkInstance vkd3d_instance_get_vk_instance(struct vkd3d_instance *instance)
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

    if ((vr = VK_CALL(vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(physical_device, &domain_count, NULL))) < 0)
    {
        ERR("Failed to enumerate time domains, vr %d.\n", vr);
        return 0;
    }

    if (!(domains = vkd3d_calloc(domain_count, sizeof(*domains))))
        return 0;

    if ((vr = VK_CALL(vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(physical_device, &domain_count, domains))) < 0)
    {
        ERR("Failed to enumerate time domains, vr %d.\n", vr);
        vkd3d_free(domains);
        return 0;
    }

    for (i = 0; i < domain_count; i++)
    {
        switch (domains[i])
        {
            case VK_TIME_DOMAIN_DEVICE_EXT:
                result |= VKD3D_TIME_DOMAIN_DEVICE;
                break;
            case VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_EXT:
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
                tile_size <= max_texel_size.height && tile_size <= max_texel_size.height)
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

static void vkd3d_physical_device_info_apply_workarounds(struct vkd3d_physical_device_info *info)
{
    /* A performance workaround for NV.
     * The 16 byte offset is a lie, as that is only actually required when we
     * use vectorized load-stores. When we emit vectorized load-store ops,
     * the storage buffer must be aligned properly, so this is fine in practice
     * and is a nice speed boost. */
    if (info->properties2.properties.vendorID == VKD3D_VENDOR_ID_NVIDIA)
        info->properties2.properties.limits.minStorageBufferOffsetAlignment = 4;
}

static void vkd3d_physical_device_info_init(struct vkd3d_physical_device_info *info, struct d3d12_device *device)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance->vk_procs;
    struct vkd3d_vulkan_info *vulkan_info = &device->vk_info;

    memset(info, 0, sizeof(*info));

    info->features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    info->properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

    info->subgroup_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    vk_prepend_struct(&info->properties2, &info->subgroup_properties);

    info->maintenance3_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;
    vk_prepend_struct(&info->properties2, &info->maintenance3_properties);

    if (vulkan_info->KHR_buffer_device_address)
    {
        info->buffer_device_address_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->buffer_device_address_features);
    }

    if (vulkan_info->KHR_timeline_semaphore)
    {
        info->timeline_semaphore_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->timeline_semaphore_features);
        info->timeline_semaphore_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES_KHR;
        vk_prepend_struct(&info->properties2, &info->timeline_semaphore_properties);
    }

    if (vulkan_info->KHR_push_descriptor)
    {
        info->push_descriptor_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR;
        vk_prepend_struct(&info->properties2, &info->push_descriptor_properties);
    }

    if (vulkan_info->KHR_shader_float16_int8)
    {
        info->float16_int8_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT16_INT8_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->float16_int8_features);
    }

    /* Core in Vulkan 1.1. */
    info->storage_16bit_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
    vk_prepend_struct(&info->features2, &info->storage_16bit_features);

    if (vulkan_info->KHR_shader_subgroup_extended_types)
    {
        info->subgroup_extended_types_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->subgroup_extended_types_features);
    }

    if (vulkan_info->KHR_maintenance4)
    {
        info->maintenance4_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES;
        vk_prepend_struct(&info->properties2, &info->maintenance4_properties);
        info->maintenance4_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES;
        vk_prepend_struct(&info->features2, &info->maintenance4_features);
    }

    if (vulkan_info->EXT_calibrated_timestamps)
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

    if (vulkan_info->EXT_descriptor_indexing)
    {
        info->descriptor_indexing_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->descriptor_indexing_features);
        info->descriptor_indexing_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES_EXT;
        vk_prepend_struct(&info->properties2, &info->descriptor_indexing_properties);
    }

    if (vulkan_info->EXT_inline_uniform_block)
    {
        info->inline_uniform_block_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->inline_uniform_block_features);
        info->inline_uniform_block_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES_EXT;
        vk_prepend_struct(&info->properties2, &info->inline_uniform_block_properties);
    }

    if (vulkan_info->EXT_robustness2)
    {
        info->robustness2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->robustness2_features);
        info->robustness2_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT;
        vk_prepend_struct(&info->properties2, &info->robustness2_properties);
    }

    if (vulkan_info->EXT_sampler_filter_minmax)
    {
        info->sampler_filter_minmax_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES_EXT;
        vk_prepend_struct(&info->properties2, &info->sampler_filter_minmax_properties);
    }

    if (vulkan_info->EXT_shader_demote_to_helper_invocation)
    {
        info->demote_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->demote_features);
    }

    if (vulkan_info->EXT_subgroup_size_control)
    {
        info->subgroup_size_control_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT;
        info->subgroup_size_control_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT;
        vk_prepend_struct(&info->properties2, &info->subgroup_size_control_properties);
        vk_prepend_struct(&info->features2, &info->subgroup_size_control_features);
    }

    if (vulkan_info->EXT_texel_buffer_alignment)
    {
        info->texel_buffer_alignment_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->texel_buffer_alignment_features);
        info->texel_buffer_alignment_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES_EXT;
        vk_prepend_struct(&info->properties2, &info->texel_buffer_alignment_properties);
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

    if (vulkan_info->EXT_extended_dynamic_state)
    {
        info->extended_dynamic_state_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->extended_dynamic_state_features);
    }

    if (vulkan_info->EXT_extended_dynamic_state2)
    {
        info->extended_dynamic_state2_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->extended_dynamic_state2_features);
    }

    if (vulkan_info->EXT_external_memory_host)
    {
        info->external_memory_host_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT;
        vk_prepend_struct(&info->properties2, &info->external_memory_host_properties);
    }

    if (vulkan_info->EXT_4444_formats)
    {
        info->ext_4444_formats_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->ext_4444_formats_features);
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

    if (vulkan_info->VALVE_mutable_descriptor_type)
    {
        info->mutable_descriptor_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_VALVE;
        vk_prepend_struct(&info->features2, &info->mutable_descriptor_features);
    }

    if (vulkan_info->EXT_image_view_min_lod)
    {
        info->image_view_min_lod_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->image_view_min_lod_features);
    }

    if (vulkan_info->KHR_acceleration_structure && vulkan_info->KHR_ray_tracing_pipeline &&
        vulkan_info->KHR_deferred_host_operations && vulkan_info->KHR_spirv_1_4)
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

    if (vulkan_info->KHR_shader_float_controls)
    {
        info->float_control_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES_KHR;
        vk_prepend_struct(&info->properties2, &info->float_control_properties);
    }

    if (vulkan_info->KHR_fragment_shading_rate)
    {
        info->fragment_shading_rate_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR;
        info->fragment_shading_rate_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR;
        vk_prepend_struct(&info->properties2, &info->fragment_shading_rate_properties);
        vk_prepend_struct(&info->features2, &info->fragment_shading_rate_features);
    }

    if (vulkan_info->KHR_separate_depth_stencil_layouts)
    {
        info->separate_depth_stencil_layout_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES;
        vk_prepend_struct(&info->features2, &info->separate_depth_stencil_layout_features);
    }

    if (vulkan_info->KHR_shader_integer_dot_product)
    {
        info->shader_integer_dot_product_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES_KHR;
        info->shader_integer_dot_product_properties.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES_KHR;
        vk_prepend_struct(&info->features2, &info->shader_integer_dot_product_features);
        vk_prepend_struct(&info->properties2, &info->shader_integer_dot_product_properties);
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

    if (vulkan_info->NV_compute_shader_derivatives)
    {
        info->compute_shader_derivatives_features_nv.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV;
        vk_prepend_struct(&info->features2, &info->compute_shader_derivatives_features_nv);
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

    if (vulkan_info->KHR_shader_atomic_int64)
    {
        info->shader_atomic_int64_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->shader_atomic_int64_features);
    }

    if (vulkan_info->EXT_shader_image_atomic_int64)
    {
        info->shader_image_atomic_int64_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->shader_image_atomic_int64_features);
    }

    if (vulkan_info->EXT_scalar_block_layout)
    {
        info->scalar_block_layout_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT;
        vk_prepend_struct(&info->features2, &info->scalar_block_layout_features);
    }

    if (vulkan_info->KHR_uniform_buffer_standard_layout)
    {
        info->uniform_buffer_standard_layout_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES;
        vk_prepend_struct(&info->features2, &info->uniform_buffer_standard_layout_features);
    }

    if (vulkan_info->KHR_dynamic_rendering)
    {
        info->dynamic_rendering_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
        vk_prepend_struct(&info->features2, &info->dynamic_rendering_features);
    }

    if (vulkan_info->VALVE_descriptor_set_host_mapping)
    {
        info->descriptor_set_host_mapping_features.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_SET_HOST_MAPPING_FEATURES_VALVE;
        vk_prepend_struct(&info->features2, &info->descriptor_set_host_mapping_features);
    }

    if (vulkan_info->KHR_driver_properties)
    {
        info->driver_properties.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR;
        vk_prepend_struct(&info->properties2, &info->driver_properties);
	}

    if (vulkan_info->AMD_device_coherent_memory)
    {
        info->device_coherent_memory_features_amd.sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD;
        vk_prepend_struct(&info->features2, &info->device_coherent_memory_features_amd);
    }

    /* Core in Vulkan 1.1. */
    info->shader_draw_parameters_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
    vk_prepend_struct(&info->features2, &info->shader_draw_parameters_features);

    VK_CALL(vkGetPhysicalDeviceFeatures2(device->vk_physical_device, &info->features2));
    VK_CALL(vkGetPhysicalDeviceProperties2(device->vk_physical_device, &info->properties2));
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
            info->descriptor_indexing_properties.maxUpdateAfterBindDescriptorsInAllPools);

    TRACE("    shaderUniformBufferArrayNonUniformIndexingNative: %#x.\n",
            info->descriptor_indexing_properties.shaderUniformBufferArrayNonUniformIndexingNative);
    TRACE("    shaderSampledImageArrayNonUniformIndexingNative: %#x.\n",
            info->descriptor_indexing_properties.shaderSampledImageArrayNonUniformIndexingNative);
    TRACE("    shaderStorageBufferArrayNonUniformIndexingNative: %#x.\n",
            info->descriptor_indexing_properties.shaderStorageBufferArrayNonUniformIndexingNative);
    TRACE("    shaderStorageImageArrayNonUniformIndexingNative: %#x.\n",
            info->descriptor_indexing_properties.shaderStorageImageArrayNonUniformIndexingNative);
    TRACE("    shaderInputAttachmentArrayNonUniformIndexingNative: %#x.\n",
            info->descriptor_indexing_properties.shaderInputAttachmentArrayNonUniformIndexingNative);

    TRACE("    robustBufferAccessUpdateAfterBind: %#x.\n",
            info->descriptor_indexing_properties.robustBufferAccessUpdateAfterBind);
    TRACE("    quadDivergentImplicitLod: %#x.\n",
            info->descriptor_indexing_properties.quadDivergentImplicitLod);

    TRACE("    maxPerStageDescriptorUpdateAfterBindSamplers: %u.\n",
            info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindSamplers);
    TRACE("    maxPerStageDescriptorUpdateAfterBindUniformBuffers: %u.\n",
            info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindUniformBuffers);
    TRACE("    maxPerStageDescriptorUpdateAfterBindStorageBuffers: %u.\n",
            info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindStorageBuffers);
    TRACE("    maxPerStageDescriptorUpdateAfterBindSampledImages: %u.\n",
            info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindSampledImages);
    TRACE("    maxPerStageDescriptorUpdateAfterBindStorageImages: %u.\n",
            info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindStorageImages);
    TRACE("    maxPerStageDescriptorUpdateAfterBindInputAttachments: %u.\n",
            info->descriptor_indexing_properties.maxPerStageDescriptorUpdateAfterBindInputAttachments);
    TRACE("    maxPerStageUpdateAfterBindResources: %u.\n",
            info->descriptor_indexing_properties.maxPerStageUpdateAfterBindResources);

    TRACE("    maxDescriptorSetUpdateAfterBindSamplers: %u.\n",
            info->descriptor_indexing_properties.maxDescriptorSetUpdateAfterBindSamplers);
    TRACE("    maxDescriptorSetUpdateAfterBindUniformBuffers: %u.\n",
            info->descriptor_indexing_properties.maxDescriptorSetUpdateAfterBindUniformBuffers);
    TRACE("    maxDescriptorSetUpdateAfterBindUniformBuffersDynamic: %u.\n",
            info->descriptor_indexing_properties.maxDescriptorSetUpdateAfterBindUniformBuffersDynamic);
    TRACE("    maxDescriptorSetUpdateAfterBindStorageBuffers: %u.\n",
            info->descriptor_indexing_properties.maxDescriptorSetUpdateAfterBindStorageBuffers);
    TRACE("    maxDescriptorSetUpdateAfterBindStorageBuffersDynamic: %u.\n",
            info->descriptor_indexing_properties.maxDescriptorSetUpdateAfterBindStorageBuffersDynamic);
    TRACE("    maxDescriptorSetUpdateAfterBindSampledImages: %u.\n",
            info->descriptor_indexing_properties.maxDescriptorSetUpdateAfterBindSampledImages);
    TRACE("    maxDescriptorSetUpdateAfterBindStorageImages: %u.\n",
            info->descriptor_indexing_properties.maxDescriptorSetUpdateAfterBindStorageImages);
    TRACE("    maxDescriptorSetUpdateAfterBindInputAttachments: %u.\n",
            info->descriptor_indexing_properties.maxDescriptorSetUpdateAfterBindInputAttachments);

    TRACE("    maxPerSetDescriptors: %u.\n", info->maintenance3_properties.maxPerSetDescriptors);
    TRACE("    maxMemoryAllocationSize: %#"PRIx64".\n", info->maintenance3_properties.maxMemoryAllocationSize);

    TRACE("  VkPhysicalDeviceTexelBufferAlignmentPropertiesEXT:\n");
    TRACE("    storageTexelBufferOffsetAlignmentBytes: %#"PRIx64".\n",
            info->texel_buffer_alignment_properties.storageTexelBufferOffsetAlignmentBytes);
    TRACE("    storageTexelBufferOffsetSingleTexelAlignment: %#x.\n",
            info->texel_buffer_alignment_properties.storageTexelBufferOffsetSingleTexelAlignment);
    TRACE("    uniformTexelBufferOffsetAlignmentBytes: %#"PRIx64".\n",
            info->texel_buffer_alignment_properties.uniformTexelBufferOffsetAlignmentBytes);
    TRACE("    uniformTexelBufferOffsetSingleTexelAlignment: %#x.\n",
            info->texel_buffer_alignment_properties.uniformTexelBufferOffsetSingleTexelAlignment);

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
            info->descriptor_indexing_features.shaderInputAttachmentArrayDynamicIndexing);
    TRACE("    shaderUniformTexelBufferArrayDynamicIndexing: %#x.\n",
            info->descriptor_indexing_features.shaderUniformTexelBufferArrayDynamicIndexing);
    TRACE("    shaderStorageTexelBufferArrayDynamicIndexing: %#x.\n",
            info->descriptor_indexing_features.shaderStorageTexelBufferArrayDynamicIndexing);

    TRACE("    shaderUniformBufferArrayNonUniformIndexing: %#x.\n",
            info->descriptor_indexing_features.shaderUniformBufferArrayNonUniformIndexing);
    TRACE("    shaderSampledImageArrayNonUniformIndexing: %#x.\n",
            info->descriptor_indexing_features.shaderSampledImageArrayNonUniformIndexing);
    TRACE("    shaderStorageBufferArrayNonUniformIndexing: %#x.\n",
            info->descriptor_indexing_features.shaderStorageBufferArrayNonUniformIndexing);
    TRACE("    shaderStorageImageArrayNonUniformIndexing: %#x.\n",
            info->descriptor_indexing_features.shaderStorageImageArrayNonUniformIndexing);
    TRACE("    shaderInputAttachmentArrayNonUniformIndexing: %#x.\n",
            info->descriptor_indexing_features.shaderInputAttachmentArrayNonUniformIndexing);
    TRACE("    shaderUniformTexelBufferArrayNonUniformIndexing: %#x.\n",
            info->descriptor_indexing_features.shaderUniformTexelBufferArrayNonUniformIndexing);
    TRACE("    shaderStorageTexelBufferArrayNonUniformIndexing: %#x.\n",
            info->descriptor_indexing_features.shaderStorageTexelBufferArrayNonUniformIndexing);

    TRACE("    descriptorBindingUniformBufferUpdateAfterBind: %#x.\n",
            info->descriptor_indexing_features.descriptorBindingUniformBufferUpdateAfterBind);
    TRACE("    descriptorBindingSampledImageUpdateAfterBind: %#x.\n",
            info->descriptor_indexing_features.descriptorBindingSampledImageUpdateAfterBind);
    TRACE("    descriptorBindingStorageImageUpdateAfterBind: %#x.\n",
            info->descriptor_indexing_features.descriptorBindingStorageImageUpdateAfterBind);
    TRACE("    descriptorBindingStorageBufferUpdateAfterBind: %#x.\n",
            info->descriptor_indexing_features.descriptorBindingStorageBufferUpdateAfterBind);
    TRACE("    descriptorBindingUniformTexelBufferUpdateAfterBind: %#x.\n",
            info->descriptor_indexing_features.descriptorBindingUniformTexelBufferUpdateAfterBind);
    TRACE("    descriptorBindingStorageTexelBufferUpdateAfterBind: %#x.\n",
            info->descriptor_indexing_features.descriptorBindingStorageTexelBufferUpdateAfterBind);

    TRACE("    descriptorBindingUpdateUnusedWhilePending: %#x.\n",
            info->descriptor_indexing_features.descriptorBindingUpdateUnusedWhilePending);
    TRACE("    descriptorBindingPartiallyBound: %#x.\n",
            info->descriptor_indexing_features.descriptorBindingPartiallyBound);
    TRACE("    descriptorBindingVariableDescriptorCount: %#x.\n",
            info->descriptor_indexing_features.descriptorBindingVariableDescriptorCount);
    TRACE("    runtimeDescriptorArray: %#x.\n",
            info->descriptor_indexing_features.runtimeDescriptorArray);

    TRACE("  VkPhysicalDeviceConditionalRenderingFeaturesEXT:\n");
    TRACE("    conditionalRendering: %#x.\n", info->conditional_rendering_features.conditionalRendering);

    TRACE("  VkPhysicalDeviceDepthClipEnableFeaturesEXT:\n");
    TRACE("    depthClipEnable: %#x.\n", info->depth_clip_features.depthClipEnable);

    TRACE("  VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT:\n");
    TRACE("    shaderDemoteToHelperInvocation: %#x.\n", info->demote_features.shaderDemoteToHelperInvocation);

    TRACE("  VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT:\n");
    TRACE("    texelBufferAlignment: %#x.\n", info->texel_buffer_alignment_features.texelBufferAlignment);

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

static HRESULT vkd3d_init_device_caps(struct d3d12_device *device,
        const struct vkd3d_device_create_info *create_info,
        struct vkd3d_physical_device_info *physical_device_info)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance->vk_procs;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR *acceleration_structure;
    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR *buffer_device_address;
    VkPhysicalDeviceDescriptorIndexingFeaturesEXT *descriptor_indexing;
    VkPhysicalDevice physical_device = device->vk_physical_device;
    struct vkd3d_vulkan_info *vulkan_info = &device->vk_info;
    VkPhysicalDeviceFeatures *features;

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

    vulkan_info->device_limits = physical_device_info->properties2.properties.limits;
    vulkan_info->sparse_properties = physical_device_info->properties2.properties.sparseProperties;
    vulkan_info->rasterization_stream = physical_device_info->xfb_properties.transformFeedbackRasterizationStreamSelect;
    vulkan_info->transform_feedback_queries = physical_device_info->xfb_properties.transformFeedbackQueries;
    vulkan_info->max_vertex_attrib_divisor = max(physical_device_info->vertex_divisor_properties.maxVertexAttribDivisor, 1);

    if (!physical_device_info->conditional_rendering_features.conditionalRendering)
        vulkan_info->EXT_conditional_rendering = false;
    if (!physical_device_info->depth_clip_features.depthClipEnable)
        vulkan_info->EXT_depth_clip_enable = false;
    if (!physical_device_info->demote_features.shaderDemoteToHelperInvocation)
        vulkan_info->EXT_shader_demote_to_helper_invocation = false;
    if (!physical_device_info->texel_buffer_alignment_features.texelBufferAlignment)
        vulkan_info->EXT_texel_buffer_alignment = false;

    vulkan_info->texel_buffer_alignment_properties = physical_device_info->texel_buffer_alignment_properties;
    vulkan_info->vertex_attrib_zero_divisor = physical_device_info->vertex_divisor_features.vertexAttributeInstanceRateZeroDivisor;

    /* Disable unused Vulkan features. */
    features->shaderTessellationAndGeometryPointSize = VK_FALSE;

    buffer_device_address = &physical_device_info->buffer_device_address_features;
    buffer_device_address->bufferDeviceAddressCaptureReplay = VK_FALSE;
    buffer_device_address->bufferDeviceAddressMultiDevice = VK_FALSE;

    descriptor_indexing = &physical_device_info->descriptor_indexing_features;
    descriptor_indexing->shaderInputAttachmentArrayDynamicIndexing = VK_FALSE;
    descriptor_indexing->shaderInputAttachmentArrayNonUniformIndexing = VK_FALSE;

    acceleration_structure = &physical_device_info->acceleration_structure_features;
    acceleration_structure->accelerationStructureCaptureReplay = VK_FALSE;

    /* Don't need or require these. */
    physical_device_info->extended_dynamic_state2_features.extendedDynamicState2LogicOp = VK_FALSE;
    physical_device_info->extended_dynamic_state2_features.extendedDynamicState2PatchControlPoints = VK_FALSE;

    if (!physical_device_info->descriptor_indexing_properties.robustBufferAccessUpdateAfterBind)
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

    if (!vulkan_info->KHR_timeline_semaphore)
    {
        ERR("Timeline semaphores are not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (!physical_device_info->separate_depth_stencil_layout_features.separateDepthStencilLayouts)
    {
        ERR("separateDepthStencilLayouts is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (!vulkan_info->KHR_sampler_mirror_clamp_to_edge)
    {
        ERR("KHR_sampler_mirror_clamp_to_edge is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (!physical_device_info->robustness2_features.nullDescriptor)
    {
        ERR("Null descriptor in VK_EXT_robustness2 is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (vulkan_info->KHR_fragment_shading_rate)
        physical_device_info->additional_shading_rates_supported = d3d12_device_determine_additional_shading_rates_supported(device);

    if (!physical_device_info->shader_draw_parameters_features.shaderDrawParameters)
    {
        ERR("shaderDrawParameters is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (!vulkan_info->KHR_bind_memory2)
    {
        ERR("KHR_bind_memory2 is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (!vulkan_info->KHR_copy_commands2)
    {
        ERR("KHR_copy_commands2 is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (!physical_device_info->dynamic_rendering_features.dynamicRendering)
    {
        ERR("KHR_dynamic_rendering is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (!physical_device_info->extended_dynamic_state_features.extendedDynamicState)
    {
        ERR("EXT_extended_dynamic_state is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (!physical_device_info->extended_dynamic_state2_features.extendedDynamicState2)
    {
        ERR("EXT_extended_dynamic_state2 is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (!physical_device_info->maintenance4_features.maintenance4)
    {
        ERR("KHR_maintenance4 is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
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

static void d3d12_device_destroy_vkd3d_queues(struct d3d12_device *device)
{
    unsigned int i, j;

    for (i = 0; i < VKD3D_QUEUE_FAMILY_COUNT; i++)
    {
        struct vkd3d_queue_family_info *queue_family = device->queue_families[i];

        if (!queue_family)
            continue;

        /* Don't destroy the same queue family twice */
        for (j = i; j < VKD3D_QUEUE_FAMILY_COUNT; j++)
        {
            if (device->queue_families[j] == queue_family)
                device->queue_families[j] = NULL;
        }

        for (j = 0; j < queue_family->queue_count; j++)
        {
            if (queue_family->queues[j])
                vkd3d_queue_destroy(queue_family->queues[j], device);
        }

        vkd3d_free(queue_family->queues);
        vkd3d_free(queue_family);
    }
}

static HRESULT d3d12_device_create_vkd3d_queues(struct d3d12_device *device,
        const struct vkd3d_device_queue_info *queue_info)
{
    unsigned int i, j, k;
    HRESULT hr;

    device->unique_queue_mask = 0;
    device->queue_family_count = 0;
    memset(device->queue_families, 0, sizeof(device->queue_families));
    memset(device->queue_family_indices, 0, sizeof(device->queue_family_indices));

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

        info->vk_family_index = queue_info->family_index[i];
        info->vk_queue_flags = queue_info->vk_properties[i].queueFlags;
        info->timestamp_bits = queue_info->vk_properties[i].timestampValidBits;

        device->queue_families[i] = info;
        device->queue_family_indices[device->queue_family_count++] = info->vk_family_index;

        if (info->queue_count && i != VKD3D_QUEUE_FAMILY_INTERNAL_COMPUTE)
            device->unique_queue_mask |= 1u << i;
    }

    return S_OK;

out_destroy_queues:
    d3d12_device_destroy_vkd3d_queues(device);
    return hr;
}

#define VKD3D_MAX_QUEUE_COUNT_PER_FAMILY (4u)
static float queue_priorities[] = {1.0f, 1.0f, 1.0f, 1.0f};

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

static HRESULT vkd3d_select_queues(const struct vkd3d_instance *vkd3d_instance,
        VkPhysicalDevice physical_device, struct vkd3d_device_queue_info *info)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &vkd3d_instance->vk_procs;
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

    info->family_index[VKD3D_QUEUE_FAMILY_SPARSE_BINDING] = vkd3d_find_queue(count, queue_properties,
            VK_QUEUE_SPARSE_BINDING_BIT, VK_QUEUE_SPARSE_BINDING_BIT);

    if (info->family_index[VKD3D_QUEUE_FAMILY_COMPUTE] == VK_QUEUE_FAMILY_IGNORED)
        info->family_index[VKD3D_QUEUE_FAMILY_COMPUTE] = info->family_index[VKD3D_QUEUE_FAMILY_GRAPHICS];

    /* Prefer compute queues for transfer. When using concurrent sharing, DMA queue tends to force compression off. */
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
    }

    vkd3d_free(queue_properties);

    if (info->family_index[VKD3D_QUEUE_FAMILY_GRAPHICS] == VK_QUEUE_FAMILY_IGNORED)
    {
        ERR("Could not find a suitable queue family for a direct command queue.\n");
        return E_FAIL;
    }

    return S_OK;
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

    if (FAILED(hr = vkd3d_select_queues(device->vkd3d_instance, physical_device, &device_queue_info)))
        return hr;

    TRACE("Using queue family %u for direct command queues.\n",
            device_queue_info.family_index[VKD3D_QUEUE_FAMILY_GRAPHICS]);
    TRACE("Using queue family %u for compute command queues.\n",
            device_queue_info.family_index[VKD3D_QUEUE_FAMILY_COMPUTE]);
    TRACE("Using queue family %u for copy command queues.\n",
            device_queue_info.family_index[VKD3D_QUEUE_FAMILY_TRANSFER]);
    TRACE("Using queue family %u for sparse binding.\n",
            device_queue_info.family_index[VKD3D_QUEUE_FAMILY_SPARSE_BINDING]);

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

    vkd3d_physical_device_info_init(&device->device_info, device);
    vkd3d_physical_device_info_apply_workarounds(&device->device_info);

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
    vkd3d_free((void *)extensions);
    if (vr < 0)
    {
        ERR("Failed to create Vulkan device, vr %d.\n", vr);
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
        ERR("Failed to register device singleton for adapter.");
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

    TRACE("device %p, size %llu, scratch %p.\n", device, size, scratch);

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
        alloc_info.optional_memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        alloc_info.flags = VKD3D_ALLOCATION_FLAG_GLOBAL_BUFFER | VKD3D_ALLOCATION_FLAG_INTERNAL_SCRATCH;

        if (FAILED(hr = vkd3d_allocate_memory(device, &device->memory_allocator,
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

    if (min_size > VKD3D_SCRATCH_BUFFER_SIZE)
        return d3d12_device_create_scratch_buffer(device, kind, min_size, memory_types, scratch);

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
    return d3d12_device_create_scratch_buffer(device, kind, VKD3D_SCRATCH_BUFFER_SIZE, memory_types, scratch);
}

void d3d12_device_return_scratch_buffer(struct d3d12_device *device, enum vkd3d_scratch_pool_kind kind,
        const struct vkd3d_scratch_buffer *scratch)
{
    struct d3d12_device_scratch_pool *pool = &device->scratch_pools[kind];
    pthread_mutex_lock(&device->mutex);

    if (scratch->allocation.resource.size == VKD3D_SCRATCH_BUFFER_SIZE &&
            pool->scratch_buffer_count < VKD3D_SCRATCH_BUFFER_COUNT)
    {
        pool->scratch_buffers[pool->scratch_buffer_count++] = *scratch;
        pthread_mutex_unlock(&device->mutex);
    }
    else
    {
        pthread_mutex_unlock(&device->mutex);
        d3d12_device_destroy_scratch_buffer(device, scratch);
    }
}

uint64_t d3d12_device_get_descriptor_heap_gpu_va(struct d3d12_device *device)
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
    return va;
}

void d3d12_device_return_descriptor_heap_gpu_va(struct d3d12_device *device, uint64_t va)
{
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

        default:
            ERR("Unhandled query type %u.\n", type_index);
            return E_INVALIDARG;
    }

    if ((vr = VK_CALL(vkCreateQueryPool(device->vk_device, &pool_info, NULL, &pool->vk_query_pool))) < 0)
    {
        ERR("Failed to create query pool, vr %u.\n", vr);
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
extern ULONG STDMETHODCALLTYPE d3d12_device_vkd3d_ext_AddRef(ID3D12DeviceExt *iface);

HRESULT STDMETHODCALLTYPE d3d12_device_QueryInterface(d3d12_device_iface *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

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
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12Device_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3D12DeviceExt))
    {
        struct d3d12_device *device = impl_from_ID3D12Device(iface);
        d3d12_device_vkd3d_ext_AddRef(&device->ID3D12DeviceExt_iface);
        *object = &device->ID3D12DeviceExt_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d3d12_device_AddRef(d3d12_device_iface *iface)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    ULONG refcount = InterlockedIncrement(&device->refcount);

    TRACE("%p increasing refcount to %u.\n", device, refcount);

    return refcount;
}

static void d3d12_device_destroy(struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    size_t i, j;

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
    vkd3d_shader_debug_ring_cleanup(&device->debug_ring, device);
#ifdef VKD3D_ENABLE_BREADCRUMBS
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
        vkd3d_breadcrumb_tracer_cleanup(&device->breadcrumb_tracer, device);
#endif
    vkd3d_pipeline_library_flush_disk_cache(&device->disk_cache);
    vkd3d_sampler_state_cleanup(&device->sampler_state, device);
    vkd3d_view_map_destroy(&device->sampler_map, device);
    vkd3d_meta_ops_cleanup(&device->meta_ops, device);
    vkd3d_bindless_state_cleanup(&device->bindless_state, device);
    d3d12_device_destroy_vkd3d_queues(device);
    vkd3d_memory_allocator_cleanup(&device->memory_allocator, device);
    /* Tear down descriptor global info late, so we catch last minute faults after we drain the queues. */
    vkd3d_descriptor_debug_free_global_info(device->descriptor_qa_global_info, device);

#ifdef VKD3D_ENABLE_RENDERDOC
    if (vkd3d_renderdoc_active() && vkd3d_renderdoc_global_capture_enabled())
        vkd3d_renderdoc_end_capture(device->vkd3d_instance->vk_instance);
#endif

    VK_CALL(vkDestroyDevice(device->vk_device, NULL));
    pthread_mutex_destroy(&device->mutex);
    if (device->parent)
        IUnknown_Release(device->parent);
    vkd3d_instance_decref(device->vkd3d_instance);
}

static void d3d12_device_set_name(struct d3d12_device *device, const char *name)
{
    vkd3d_set_vk_object_name(device, (uint64_t)(uintptr_t)device->vk_device,
            VK_OBJECT_TYPE_DEVICE, name);
}

static ULONG STDMETHODCALLTYPE d3d12_device_Release(d3d12_device_iface *iface)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
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
        vkd3d_free(device);
    }

    if (is_locked)
        pthread_mutex_unlock(&d3d12_device_map_mutex);

    TRACE("%p decreasing refcount to %u.\n", device, cur_refcount - 1);
    return cur_refcount - 1;
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

    if (FAILED(hr = d3d12_command_queue_create(device, desc, &object)))
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
        if (FAILED(hr = d3d12_command_allocator_create(device, type, &object)))
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
}

static HRESULT d3d12_device_check_multisample_quality_levels(struct d3d12_device *device,
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS *data)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkImageFormatProperties vk_properties;
    const struct vkd3d_format *format;
    VkSampleCountFlagBits vk_samples;
    VkImageUsageFlags vk_usage = 0;
    VkResult vr;

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

    if (data->Format == DXGI_FORMAT_UNKNOWN)
        goto done;

    if (!(format = vkd3d_get_format(device, data->Format, false)))
        format = vkd3d_get_format(device, data->Format, true);
    if (!format)
    {
        FIXME("Unhandled format %#x.\n", data->Format);
        return E_INVALIDARG;
    }
    if (data->Flags)
        FIXME("Ignoring flags %#x.\n", data->Flags);

    if (format->vk_aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT)
        vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    else
        vk_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    vr = VK_CALL(vkGetPhysicalDeviceImageFormatProperties(device->vk_physical_device,
            format->vk_format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, vk_usage, 0, &vk_properties));
    if (vr == VK_ERROR_FORMAT_NOT_SUPPORTED)
    {
        WARN("Format %#x is not supported.\n", format->dxgi_format);
        goto done;
    }
    if (vr < 0)
    {
        ERR("Failed to get image format properties, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (vk_properties.sampleCounts & vk_samples)
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

static bool vk_format_is_supported_by_global_read_write_without_format(VkFormat format)
{
    size_t i;

    /* from https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#formats-without-shader-storage-format */
    static const VkFormat supported_formats[] =
    {
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R8G8B8A8_SNORM,
        VK_FORMAT_R8G8B8A8_UINT,
        VK_FORMAT_R8G8B8A8_SINT,
        VK_FORMAT_R32_UINT,
        VK_FORMAT_R32_SINT,
        VK_FORMAT_R32_SFLOAT,
        VK_FORMAT_R32G32_UINT,
        VK_FORMAT_R32G32_SINT,
        VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_R32G32B32A32_UINT,
        VK_FORMAT_R32G32B32A32_SINT,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_FORMAT_R16G16B16A16_UINT,
        VK_FORMAT_R16G16B16A16_SINT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16_SFLOAT,
        VK_FORMAT_B10G11R11_UFLOAT_PACK32,
        VK_FORMAT_R16_SFLOAT,
        VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_R16G16_UNORM,
        VK_FORMAT_R8G8_UNORM,
        VK_FORMAT_R16_UNORM,
        VK_FORMAT_R8_UNORM,
        VK_FORMAT_R16G16B16A16_SNORM,
        VK_FORMAT_R16G16_SNORM,
        VK_FORMAT_R8G8_SNORM,
        VK_FORMAT_R16_SNORM,
        VK_FORMAT_R8_SNORM,
        VK_FORMAT_R16G16_SINT,
        VK_FORMAT_R8G8_SINT,
        VK_FORMAT_R16_SINT,
        VK_FORMAT_R8_SINT,
        VK_FORMAT_A2B10G10R10_UINT_PACK32,
        VK_FORMAT_R16G16_UINT,
        VK_FORMAT_R8G8_UINT,
        VK_FORMAT_R16_UINT,
        VK_FORMAT_R8_UINT,
    };

    for (i = 0; i < ARRAY_SIZE(supported_formats); i++)
    {
        if (format == supported_formats[i])
            return true;
    }

    return false;
}

static HRESULT d3d12_device_get_format_support(struct d3d12_device *device, D3D12_FEATURE_DATA_FORMAT_SUPPORT *data)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    VkFormatFeatureFlags2KHR image_features;
    VkFormatProperties3KHR properties3;
    const struct vkd3d_format *format;
    VkFormatProperties2 properties;

    data->Support1 = D3D12_FORMAT_SUPPORT1_NONE;
    data->Support2 = D3D12_FORMAT_SUPPORT2_NONE;
    if (!(format = vkd3d_get_format(device, data->Format, false)))
        format = vkd3d_get_format(device, data->Format, true);
    if (!format)
    {
        FIXME("Unhandled format %#x.\n", data->Format);
        return E_INVALIDARG;
    }

    properties.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    properties.pNext = NULL;

    if (device->vk_info.KHR_format_feature_flags2)
    {
        properties3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3_KHR;
        properties3.pNext = NULL;
        vk_prepend_struct(&properties, &properties3);
    }

    VK_CALL(vkGetPhysicalDeviceFormatProperties2(device->vk_physical_device, format->vk_format, &properties));

    if (device->vk_info.KHR_format_feature_flags2)
        image_features = properties3.linearTilingFeatures | properties3.optimalTilingFeatures;
    else
        image_features = properties.formatProperties.linearTilingFeatures | properties.formatProperties.optimalTilingFeatures;

    if (properties.formatProperties.bufferFeatures)
        data->Support1 |= D3D12_FORMAT_SUPPORT1_BUFFER;
    if (properties.formatProperties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)
        data->Support1 |= D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER;
    if (data->Format == DXGI_FORMAT_R16_UINT || data->Format == DXGI_FORMAT_R32_UINT)
        data->Support1 |= D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER;
    if (image_features)
    {
        data->Support1 |= D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURE2D
                | D3D12_FORMAT_SUPPORT1_TEXTURE3D | D3D12_FORMAT_SUPPORT1_TEXTURECUBE;
    }
    if (image_features & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT_KHR)
    {
        data->Support1 |= D3D12_FORMAT_SUPPORT1_SHADER_LOAD | D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD
                | D3D12_FORMAT_SUPPORT1_SHADER_GATHER;
        if (image_features & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT_KHR)
        {
            data->Support1 |= D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE
                    | D3D12_FORMAT_SUPPORT1_MIP;
        }
        if (format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
        {
            data->Support1 |= D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON
                    | D3D12_FORMAT_SUPPORT1_SHADER_GATHER_COMPARISON;
        }
    }
    if (image_features & VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT_KHR)
        data->Support1 |= D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET;
    if (image_features & VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT_KHR)
        data->Support1 |= D3D12_FORMAT_SUPPORT1_BLENDABLE;
    if (image_features & VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT_KHR)
        data->Support1 |= D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
    if (image_features & VK_FORMAT_FEATURE_2_BLIT_SRC_BIT_KHR)
        data->Support1 |= D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE;
    if (image_features & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT_KHR)
    {
        data->Support1 |= D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
        if (image_features & VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT_KHR)
            data->Support2 |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
        if (image_features & VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT_KHR)
            data->Support2 |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;

        if (!device->vk_info.KHR_format_feature_flags2 &&
                vk_format_is_supported_by_global_read_write_without_format(format->vk_format))
        {
            if (device->device_info.features2.features.shaderStorageImageReadWithoutFormat)
                data->Support2 |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
            if (device->device_info.features2.features.shaderStorageImageWriteWithoutFormat)
                data->Support2 |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
        }
    }

    if (image_features & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT_KHR)
    {
        data->Support2 |= D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD
                | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS
                | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE
                | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE
                | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX
                | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX;
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
            TRACE("Protected resource session support %#x.", data->Support);
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

            TRACE("Root signature requested %#x.\n", data->HighestVersion);
            data->HighestVersion = min(data->HighestVersion, D3D_ROOT_SIGNATURE_VERSION_1_1);

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

            TRACE("Shader cache support flags %#x.", data->SupportFlags);
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

            TRACE("Command list type %u supports priority %u: %#x.",
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

            TRACE("Copy queue timestamp queries %#x.", data->CopyQueueTimestampQueriesSupported);
            TRACE("Casting fully typed formats %#x.", data->CastingFullyTypedFormatSupported);
            TRACE("Write buffer immediate support flags %#x.", data->WriteBufferImmediateSupportFlags);
            TRACE("View instancing tier %u.", data->ViewInstancingTier);
            TRACE("Barycentrics %#x.", data->BarycentricsSupported);
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

            TRACE("Existing heaps %#x.", data->Supported);
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

        case D3D12_FEATURE_QUERY_META_COMMAND:
        {
            D3D12_FEATURE_DATA_QUERY_META_COMMAND *data = feature_data;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
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
    TRACE("iface %p, descriptor_heap_type %#x.\n", iface, descriptor_heap_type);

    return d3d12_device_get_descriptor_handle_increment_size(descriptor_heap_type);
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

static void STDMETHODCALLTYPE d3d12_device_CreateConstantBufferView(d3d12_device_iface *iface,
        const D3D12_CONSTANT_BUFFER_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, desc %p, descriptor %#lx.\n", iface, desc, descriptor.ptr);

    d3d12_desc_create_cbv(descriptor.ptr, device, desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateShaderResourceView(d3d12_device_iface *iface,
        ID3D12Resource *resource, const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, resource %p, desc %p, descriptor %#lx.\n",
            iface, resource, desc, descriptor.ptr);

    d3d12_desc_create_srv(descriptor.ptr, device, impl_from_ID3D12Resource(resource), desc);
}

VKD3D_THREAD_LOCAL struct D3D12_UAV_INFO *d3d12_uav_info = NULL;

static void STDMETHODCALLTYPE d3d12_device_CreateUnorderedAccessView(d3d12_device_iface *iface,
        ID3D12Resource *resource, ID3D12Resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    VkImageViewAddressPropertiesNVX out_info = { VK_STRUCTURE_TYPE_IMAGE_VIEW_ADDRESS_PROPERTIES_NVX };
    VkImageViewHandleInfoNVX imageViewHandleInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_HANDLE_INFO_NVX };
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

        imageViewHandleInfo.imageView = d.view->info.view->vk_image_view;
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

static void STDMETHODCALLTYPE d3d12_device_CreateSampler(d3d12_device_iface *iface,
        const D3D12_SAMPLER_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
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

    increment = d3d12_device_get_descriptor_handle_increment_size(descriptor_heap_type);

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

static void STDMETHODCALLTYPE d3d12_device_CopyDescriptorsSimple(d3d12_device_iface *iface,
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

    if (descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
            descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        /* Fast and hot path. */
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
                    ?  D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1;
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
    TRACE("iface %p, heap_properties %p, heap_flags %#x,  desc %p, initial_state %#x, "
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
                metadata.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

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

        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = 0;
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
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        if (metadata.BindFlags & D3D11_BIND_RENDER_TARGET)
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (metadata.BindFlags & D3D11_BIND_DEPTH_STENCIL)
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if (metadata.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if ((metadata.BindFlags & D3D11_BIND_DEPTH_STENCIL) && !(metadata.BindFlags & D3D11_BIND_SHADER_RESOURCE))
            desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        desc.SamplerFeedbackMipRegion.Width = 0;
        desc.SamplerFeedbackMipRegion.Height = 0;
        desc.SamplerFeedbackMipRegion.Depth = 0;

        heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap_props.CreationNodeMask = 0;
        heap_props.VisibleNodeMask = 0;

        hr = d3d12_resource_create_committed(device, &desc, &heap_props,
                D3D12_HEAP_FLAG_SHARED, D3D12_RESOURCE_STATE_COMMON, NULL, handle, &resource);
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
    FIXME_ONCE("iface %p, object_count %u, objects %p stub!\n",
            iface, object_count, objects);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_Evict(d3d12_device_iface *iface,
        UINT object_count, ID3D12Pageable * const *objects)
{
    FIXME_ONCE("iface %p, object_count %u, objects %p stub!\n",
            iface, object_count, objects);

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

    return device->removed_reason;
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
        ID3D12PipelineLibrary_Release(&pipeline_library->ID3D12PipelineLibrary_iface);
        return S_FALSE;
    }
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetEventOnMultipleFenceCompletion(d3d12_device_iface *iface,
        ID3D12Fence *const *fences, const UINT64 *values, UINT fence_count,
        D3D12_MULTIPLE_FENCE_WAIT_FLAGS flags, HANDLE event)
{
    FIXME("iface %p, fences %p, values %p, fence_count %u, flags %#x, event %p stub!\n",
            iface, fences, values, fence_count, flags, event);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_SetResidencyPriority(d3d12_device_iface *iface,
        UINT object_count, ID3D12Pageable *const *objects, const D3D12_RESIDENCY_PRIORITY *priorities)
{
    FIXME_ONCE("iface %p, object_count %u, objects %p, priorities %p stub!\n",
            iface, object_count, objects, priorities);

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

static HRESULT STDMETHODCALLTYPE d3d12_device_EnqueueMakeResident(d3d12_device_iface *iface,
        D3D12_RESIDENCY_FLAGS flags, UINT num_objects, ID3D12Pageable *const *objects,
        ID3D12Fence *fence_to_signal, UINT64 fence_value_to_signal)
{
    FIXME_ONCE("iface %p, flags %#x, num_objects %u, objects %p, fence_to_signal %p, fence_value_to_signal %"PRIu64" stub!\n",
            iface, flags, num_objects, objects, fence_to_signal, fence_value_to_signal);

    return ID3D12Fence_Signal(fence_to_signal, fence_value_to_signal);
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
            initial_state, optimized_clear_value, &object)))
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
    FIXME("iface %p, count %p, descs %p stub!\n", iface, count, descs);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_EnumerateMetaCommandParameters(d3d12_device_iface *iface,
        REFGUID command_id, D3D12_META_COMMAND_PARAMETER_STAGE stage, UINT *total_size,
        UINT *param_count, D3D12_META_COMMAND_PARAMETER_DESC *param_descs)
{
    FIXME("iface %p, command_id %s, stage %u, total_size %p, param_count %p, param_descs %p stub!\n",
            iface, debugstr_guid(command_id), stage, total_size, param_count, param_descs);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateMetaCommand(d3d12_device_iface *iface,
        REFGUID command_id, UINT node_mask, const void *param_data, SIZE_T param_size,
        REFIID iid, void **meta_command)
{
    FIXME("iface %p, command_id %s, node_mask %#x, param_data %p, param_size %lu, iid %s, meta_command %p stub!\n",
            iface, debugstr_guid(command_id), node_mask, param_data, param_size, debugstr_guid(iid), meta_command);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateStateObject(d3d12_device_iface *iface,
        const D3D12_STATE_OBJECT_DESC *desc, REFIID iid, void **state_object)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_state_object *state;
    HRESULT hr;

    TRACE("iface %p, desc %p, iid %s, state_object %p!\n",
            iface, desc, debugstr_guid(iid), state_object);

    if (FAILED(hr = d3d12_state_object_create(device, desc, NULL, &state)))
        return hr;

    return return_interface(&state->ID3D12StateObject_iface, &IID_ID3D12StateObject, iid, state_object);
}

static void STDMETHODCALLTYPE d3d12_device_GetRaytracingAccelerationStructurePrebuildInfo(d3d12_device_iface *iface,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *desc,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO *info)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_acceleration_structure_build_info build_info;
    VkAccelerationStructureBuildSizesInfoKHR size_info;

    TRACE("iface %p, desc %p, info %p!\n", iface, desc, info);

    if (!d3d12_device_supports_ray_tracing_tier_1_0(device))
    {
        ERR("Acceleration structure is not supported. Calling this is invalid.\n");
        memset(info, 0, sizeof(*info));
        return;
    }

    if (!vkd3d_acceleration_structure_convert_inputs(device, &build_info, desc))
    {
        ERR("Failed to convert inputs.\n");
        memset(info, 0, sizeof(*info));
        return;
    }

    memset(&size_info, 0, sizeof(size_info));
    size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    VK_CALL(vkGetAccelerationStructureBuildSizesKHR(device->vk_device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info.build_info,
            build_info.primitive_counts, &size_info));

    vkd3d_acceleration_structure_build_info_cleanup(&build_info);

    info->ResultDataMaxSizeInBytes = size_info.accelerationStructureSize;
    info->ScratchDataSizeInBytes = size_info.buildScratchSize;
    info->UpdateScratchDataSizeInBytes = size_info.updateScratchSize;

    TRACE("ResultDataMaxSizeInBytes: %"PRIu64".\n", info->ResultDataMaxSizeInBytes);
    TRACE("ScratchDatSizeInBytes: %"PRIu64".\n", info->ScratchDataSizeInBytes);
    TRACE("UpdateScratchDataSizeInBytes: %"PRIu64".\n", info->UpdateScratchDataSizeInBytes);
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
    struct d3d12_state_object *parent;
    struct d3d12_state_object *state;
    HRESULT hr;

    TRACE("iface %p, addition %p, state_object %p, riid %s, new_state_object %p stub!\n",
            iface, addition, parent_state, debugstr_guid(riid), new_state_object);

    parent = impl_from_ID3D12StateObject(parent_state);
    if (FAILED(hr = d3d12_state_object_add(device, addition, parent, &state)))
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

static D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE d3d12_device_GetResourceAllocationInfo2(d3d12_device_iface *iface,
        D3D12_RESOURCE_ALLOCATION_INFO *info, UINT visible_mask, UINT count, const D3D12_RESOURCE_DESC1 *resource_descs,
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
        hasMsaaResource |= desc->SampleDesc.Count > 1;

        if (FAILED(d3d12_resource_validate_desc(desc, device)))
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
            if (FAILED(vkd3d_get_image_allocation_info(device, desc, &resource_info)))
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

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommittedResource2(d3d12_device_iface *iface,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags, const D3D12_RESOURCE_DESC1 *desc,
        D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *optimized_clear_value,
        ID3D12ProtectedResourceSession *protected_session, REFIID iid, void **resource)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_resource *object;
    HRESULT hr;

    TRACE("iface %p, heap_properties %p, heap_flags %#x,  desc %p, initial_state %#x, "
            "optimized_clear_value %p, protected_session %p, iid %s, resource %p.\n",
            iface, heap_properties, heap_flags, desc, initial_state,
            optimized_clear_value, protected_session, debugstr_guid(iid), resource);

    if (protected_session)
        FIXME("Ignoring protected session %p.\n", protected_session);

    if (FAILED(hr = d3d12_resource_create_committed(device, desc, heap_properties,
            heap_flags, initial_state, optimized_clear_value, NULL, &object)))
    {
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
            heap_offset, initial_state, optimized_clear_value, &object)))
        return hr;

    return return_interface(&object->ID3D12Resource_iface, &IID_ID3D12Resource, iid, resource);
}

static void STDMETHODCALLTYPE d3d12_device_CreateSamplerFeedbackUnorderedAccessView(d3d12_device_iface *iface,
        ID3D12Resource *target_resource, ID3D12Resource *feedback_resource, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    FIXME("iface %p, target_resource %p, feedback_resource %p, descriptor %#lx stub!\n",
            iface, target_resource, feedback_resource, descriptor);
}

static void STDMETHODCALLTYPE d3d12_device_GetCopyableFootprints1(d3d12_device_iface *iface,
        const D3D12_RESOURCE_DESC1 *desc, UINT first_sub_resource, UINT sub_resource_count,
        UINT64 base_offset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts, UINT *row_counts,
        UINT64 *row_sizes, UINT64 *total_bytes)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    static const struct vkd3d_format vkd3d_format_unknown
            = {DXGI_FORMAT_UNKNOWN, VK_FORMAT_UNDEFINED, 1, 1, 1, 1, 0, 1};

    unsigned int i, sub_resource_idx, miplevel_idx, row_count, row_size, row_pitch;
    unsigned int width, height, depth, num_planes, num_subresources;
    unsigned int num_subresources_per_plane, plane_idx;
    struct vkd3d_format_footprint plane_footprint;
    const struct vkd3d_format *format;
    uint64_t offset, size, total;

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

    if (FAILED(d3d12_resource_validate_desc(desc, device)))
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
        plane_idx = sub_resource_idx / num_subresources_per_plane;

        plane_footprint = vkd3d_format_footprint_for_plane(format, plane_idx);

        miplevel_idx = sub_resource_idx % desc->MipLevels;
        width = align(d3d12_resource_desc_get_width(desc, miplevel_idx), plane_footprint.block_width);
        height = align(d3d12_resource_desc_get_height(desc, miplevel_idx), plane_footprint.block_height);
        depth = d3d12_resource_desc_get_depth(desc, miplevel_idx);
        row_count = height / plane_footprint.block_height;
        row_size = (width / plane_footprint.block_width) * plane_footprint.block_byte_count;

        /* For whatever reason, we need to use 512 bytes of alignment for depth-stencil formats.
         * This is not documented, but it is observed behavior on both NV and WARP drivers.
         * See test_get_copyable_footprints_planar(). */
        row_pitch = align(row_size, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * num_planes);

        if (layouts)
        {
            layouts[i].Offset = base_offset + offset;
            layouts[i].Footprint.Format = plane_footprint.dxgi_format;
            layouts[i].Footprint.Width = width;
            layouts[i].Footprint.Height = height;
            layouts[i].Footprint.Depth = depth;
            layouts[i].Footprint.RowPitch = row_pitch;
        }
        if (row_counts)
            row_counts[i] = row_count;
        if (row_sizes)
            row_sizes[i] = row_size;

        size = max(0, row_count - 1) * row_pitch + row_size;
        size = max(0, depth - 1) * align(size, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * num_planes) + size;

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

CONST_VTBL struct ID3D12Device9Vtbl d3d12_device_vtbl =
{
    /* IUnknown methods */
    d3d12_device_QueryInterface,
    d3d12_device_AddRef,
    d3d12_device_Release,
    /* ID3D12Object methods */
    d3d12_device_GetPrivateData,
    d3d12_device_SetPrivateData,
    d3d12_device_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
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
    /* ID3D12Device1 methods */
    d3d12_device_CreatePipelineLibrary,
    d3d12_device_SetEventOnMultipleFenceCompletion,
    d3d12_device_SetResidencyPriority,
    /* ID3D12Device2 methods */
    d3d12_device_CreatePipelineState,
    /* ID3D12Device3 methods */
    d3d12_device_OpenExistingHeapFromAddress,
    d3d12_device_OpenExistingHeapFromFileMapping,
    d3d12_device_EnqueueMakeResident,
    /* ID3D12Device4 methods */
    d3d12_device_CreateCommandList1,
    d3d12_device_CreateProtectedResourceSession,
    d3d12_device_CreateCommittedResource1,
    d3d12_device_CreateHeap1,
    d3d12_device_CreateReservedResource1,
    d3d12_device_GetResourceAllocationInfo1,
    /* ID3D12Device5 methods */
    d3d12_device_CreateLifetimeTracker,
    d3d12_device_RemoveDevice,
    d3d12_device_EnumerateMetaCommands,
    d3d12_device_EnumerateMetaCommandParameters,
    d3d12_device_CreateMetaCommand,
    d3d12_device_CreateStateObject,
    d3d12_device_GetRaytracingAccelerationStructurePrebuildInfo,
    d3d12_device_CheckDriverMatchingIdentifier,
    /* ID3D12Device6 methods */
    d3d12_device_SetBackgroundProcessingMode,
    /* ID3D12Device7 methods */
    d3d12_device_AddToStateObject,
    d3d12_device_CreateProtectedResourceSession1,
    /* ID3D12Device8 methods */
    d3d12_device_GetResourceAllocationInfo2,
    d3d12_device_CreateCommittedResource2,
    d3d12_device_CreatePlacedResource1,
    d3d12_device_CreateSamplerFeedbackUnorderedAccessView,
    d3d12_device_GetCopyableFootprints1,
    /* ID3D12Device9 methods */
    d3d12_device_CreateShaderCacheSession,
    d3d12_device_ShaderCacheControl,
    d3d12_device_CreateCommandQueue1,
};

#ifdef VKD3D_ENABLE_PROFILING
#include "device_profiled.h"
#endif

static D3D12_RESOURCE_BINDING_TIER d3d12_device_determine_resource_binding_tier(struct d3d12_device *device)
{
    const uint32_t tier_2_required_flags = VKD3D_BINDLESS_SRV | VKD3D_BINDLESS_SAMPLER;
    const uint32_t tier_3_required_flags = VKD3D_BINDLESS_CBV | VKD3D_BINDLESS_UAV;

    uint32_t bindless_flags = device->bindless_state.flags;

    if ((bindless_flags & tier_2_required_flags) != tier_2_required_flags)
        return D3D12_RESOURCE_BINDING_TIER_1;

    if ((bindless_flags & tier_3_required_flags) != tier_3_required_flags)
        return D3D12_RESOURCE_BINDING_TIER_2;

    return D3D12_RESOURCE_BINDING_TIER_3;
}

static D3D12_TILED_RESOURCES_TIER d3d12_device_determine_tiled_resources_tier(struct d3d12_device *device)
{
    const VkPhysicalDeviceSamplerFilterMinmaxProperties *minmax_properties = &device->device_info.sampler_filter_minmax_properties;
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
            !minmax_properties->filterMinmaxSingleComponentFormats)
        return D3D12_TILED_RESOURCES_TIER_1;

    if (!features->sparseResidencyImage3D ||
            !sparse_properties->residencyStandard3DBlockShape)
        return D3D12_TILED_RESOURCES_TIER_2;

    return D3D12_TILED_RESOURCES_TIER_3;
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
            if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DXR)
            {
                INFO("DXR support enabled.\n");
                tier = D3D12_RAYTRACING_TIER_1_0;
            }
            else
                INFO("Could enable DXR, but VKD3D_CONFIG=dxr is not used.\n");
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

static void d3d12_device_caps_init_feature_options(struct d3d12_device *device)
{
    const VkPhysicalDeviceFeatures *features = &device->device_info.features2.features;
    D3D12_FEATURE_DATA_D3D12_OPTIONS *options = &device->d3d12_caps.options;
    const struct vkd3d_vulkan_info *vk_info = &device->vk_info;

    options->DoublePrecisionFloatShaderOps = features->shaderFloat64;
    options->OutputMergerLogicOp = features->logicOp;
    /* Currently not supported */
    options->MinPrecisionSupport = D3D12_SHADER_MIN_PRECISION_SUPPORT_NONE;
    options->TiledResourcesTier = d3d12_device_determine_tiled_resources_tier(device);
    options->ResourceBindingTier = d3d12_device_determine_resource_binding_tier(device);
    options->PSSpecifiedStencilRefSupported = vk_info->EXT_shader_stencil_export;
    options->TypedUAVLoadAdditionalFormats = d3d12_device_determine_additional_typed_uav_support(device);
    /* Requires VK_EXT_fragment_shader_interlock */
    options->ROVsSupported = FALSE;
    options->ConservativeRasterizationTier = d3d12_device_determine_conservative_rasterization_tier(device);
    options->MaxGPUVirtualAddressBitsPerResource = 40; /* XXX */
    options->StandardSwizzle64KBSupported = FALSE;
    options->CrossNodeSharingTier = D3D12_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED;
    options->CrossAdapterRowMajorTextureSupported = FALSE;
    options->VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation = vk_info->EXT_shader_viewport_index_layer;
    options->ResourceHeapTier = d3d12_device_determine_heap_tier(device);
}

static void d3d12_device_caps_init_feature_options1(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 *options1 = &device->d3d12_caps.options1;

    options1->WaveOps = device->d3d12_caps.max_shader_model >= D3D_SHADER_MODEL_6_0;

    if (device->vk_info.EXT_subgroup_size_control)
    {
        options1->WaveLaneCountMin = device->device_info.subgroup_size_control_properties.minSubgroupSize;
        options1->WaveLaneCountMax = device->device_info.subgroup_size_control_properties.maxSubgroupSize;
    }
    else
    {
        WARN("Device info for WaveLaneCountMin and WaveLaneCountMax may be inaccurate.\n");
        options1->WaveLaneCountMin = device->device_info.subgroup_properties.subgroupSize;
        options1->WaveLaneCountMax = device->device_info.subgroup_properties.subgroupSize;
    }

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
        options1->TotalLaneCount = 32 * device->device_info.subgroup_properties.subgroupSize;
        WARN("No device info available for TotalLaneCount = .\n");
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
    options4->Native16BitShaderOpsSupported = device->device_info.float16_int8_features.shaderFloat16 &&
            device->device_info.features2.features.shaderInt16 &&
            device->device_info.storage_16bit_features.uniformAndStorageBuffer16BitAccess &&
            device->device_info.subgroup_extended_types_features.shaderSubgroupExtendedTypes;
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

    /* Not supported */
    options7->MeshShaderTier = D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
    options7->SamplerFeedbackTier = D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED;
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
            device->device_info.shader_atomic_int64_features.shaderSharedInt64Atomics;
    /* Unsure if sparse 64-bit image atomics is also required. */
    options9->AtomicInt64OnTypedResourceSupported =
            device->device_info.shader_image_atomic_int64_features.shaderImageInt64Atomics;
    options9->DerivativesInMeshAndAmplificationShadersSupported = FALSE;
    options9->MeshShaderSupportsFullRangeRenderTargetArrayIndex = FALSE;
    options9->MeshShaderPipelineStatsSupported = FALSE;
    options9->WaveMMATier = D3D12_WAVE_MMA_TIER_NOT_SUPPORTED;
}

static void d3d12_device_caps_init_feature_options10(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS10 *options10 = &device->d3d12_caps.options10;

    options10->VariableRateShadingSumCombinerSupported =
            d3d12_device_determine_variable_shading_rate_tier(device) >= D3D12_VARIABLE_SHADING_RATE_TIER_1;
    options10->MeshShaderPerPrimitiveShadingRateSupported = FALSE;
}

static void d3d12_device_caps_init_feature_options11(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS11 *options11 = &device->d3d12_caps.options11;

    options11->AtomicInt64OnDescriptorHeapResourceSupported =
            device->device_info.shader_atomic_int64_features.shaderBufferInt64Atomics;
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

    if (device->api_version >= VK_API_VERSION_1_1 &&
        physical_device_info->subgroup_properties.subgroupSize >= 4 &&
        (physical_device_info->uniform_buffer_standard_layout_features.uniformBufferStandardLayout ||
         physical_device_info->scalar_block_layout_features.scalarBlockLayout) &&
        (physical_device_info->subgroup_properties.supportedOperations & required) == required &&
        (physical_device_info->subgroup_properties.supportedStages & required_stages) == required_stages)
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
        denorm_behavior = ((device->device_info.float_control_properties.denormBehaviorIndependence ==
                VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY) ||
                (device->device_info.float_control_properties.denormBehaviorIndependence ==
                        VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL)) &&
                (device->device_info.properties2.properties.vendorID == VKD3D_VENDOR_ID_NVIDIA ||
                        (device->device_info.float_control_properties.shaderDenormFlushToZeroFloat32 &&
                                device->device_info.float_control_properties.shaderDenormPreserveFloat32));

        if (denorm_behavior)
        {
            device->d3d12_caps.max_shader_model = D3D_SHADER_MODEL_6_2;
            TRACE("Enabling support for SM 6.2.\n");
        }

        /* SM 6.3 adds:
         * https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model-6.3
         * Ray tracing (lib_6_3 multi entry point targets).
         */
        if (device->d3d12_caps.max_shader_model == D3D_SHADER_MODEL_6_2 && device->vk_info.KHR_spirv_1_4)
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
         * - 64-bit atomics. Only buffer atomics are required for SM 6.6.
         * - Strict IsHelperInvocation(). The emulated path might have some edge cases here,
         *   no reason not to require it.
         * - 8-bit integers. Widely supported, even on older targets. Can be emulated if need be.
         * - WaveSize attribute, requiredSubgroupSizeStages + FullSubgroups feature is required.
         * - RayPayload attribute (purely metadata in DXIL land, irrelevant for us).
         */
        if (device->d3d12_caps.max_shader_model == D3D_SHADER_MODEL_6_5 &&
                device->device_info.compute_shader_derivatives_features_nv.computeDerivativeGroupLinear &&
                device->device_info.shader_atomic_int64_features.shaderBufferInt64Atomics &&
                device->device_info.demote_features.shaderDemoteToHelperInvocation &&
                device->device_info.float16_int8_features.shaderInt8 &&
                device->device_info.subgroup_size_control_features.computeFullSubgroups &&
                device->device_info.subgroup_size_control_features.subgroupSizeControl &&
                (device->device_info.subgroup_size_control_properties.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT))
        {
            INFO("Enabling support for SM 6.6.\n");
            device->d3d12_caps.max_shader_model = D3D_SHADER_MODEL_6_6;
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
        case VKD3D_APPLICATION_FEATURE_OVERRIDE_PROMOTE_DXR_TO_ULTIMATE:
            if (device->d3d12_caps.options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0)
            {
                device->d3d12_caps.options7.MeshShaderTier = D3D12_MESH_SHADER_TIER_1;
                device->d3d12_caps.options7.SamplerFeedbackTier = D3D12_SAMPLER_FEEDBACK_TIER_1_0;
                INFO("DXR enabled. Application also requires Mesh/Sampler feedback to be exposed (but unused). "
                     "Enabling these features automatically.\n");
            }
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
        const char* string;
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
    d3d12_device_caps_init_feature_level(device);

    d3d12_device_caps_override(device);
    d3d12_device_caps_override_application(device);
}

static void vkd3d_init_shader_extensions(struct d3d12_device *device)
{
    device->vk_info.shader_extension_count = 0;
    if (device->vk_info.EXT_shader_demote_to_helper_invocation)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_SPV_EXT_DEMOTE_TO_HELPER_INVOCATION;
    }

    if (device->device_info.shader_integer_dot_product_features.shaderIntegerDotProduct)
    {
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_SPV_KHR_INTEGER_DOT_PRODUCT;
    }

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

    if (device->device_info.scalar_block_layout_features.scalarBlockLayout)
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

    if (device->d3d12_caps.options4.Native16BitShaderOpsSupported &&
            (device->device_info.driver_properties.driverID == VK_DRIVER_ID_MESA_RADV ||
                    (vkd3d_config_flags & VKD3D_CONFIG_FLAG_FORCE_NATIVE_FP16)))
    {
        /* Native FP16 is buggy on NV for now. */
        device->vk_info.shader_extensions[device->vk_info.shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_MIN_PRECISION_IS_NATIVE_16BIT;
    }
}

static void vkd3d_compute_shader_interface_key(struct d3d12_device *device)
{
    /* This key needs to hold all state which could potentially affect shader compilation.
     * We may generate different SPIR-V based on the bindless state flags.
     * The bindless states are affected by various flags. */
    unsigned int i;
    uint64_t key;

    key = hash_fnv1_init();

    /* Technically, any changes in vkd3d-shader will be reflected in the vkd3d-proton Git hash,
     * but it is useful to be able to modify the internal revision while developing since
     * we have no mechanism for emitting dirty Git revisions. */
    key = hash_fnv1_iterate_u64(key, vkd3d_shader_get_revision());
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

    device->shader_interface_key = key;
}

static bool d3d12_device_supports_feature_level(struct d3d12_device *device, D3D_FEATURE_LEVEL feature_level)
{
    return feature_level <= device->d3d12_caps.max_feature_level;
}

extern CONST_VTBL struct ID3D12DeviceExtVtbl d3d12_device_vkd3d_ext_vtbl;

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
        device->ID3D12Device_iface.lpVtbl = &d3d12_device_vtbl;
#else
    device->ID3D12Device_iface.lpVtbl = &d3d12_device_vtbl;
#endif

    device->refcount = 1;

    vkd3d_instance_incref(device->vkd3d_instance = instance);
    device->vk_info = instance->vk_info;
    device->signal_event = instance->signal_event;

    device->adapter_luid = create_info->adapter_luid;
    device->removed_reason = S_OK;

    device->vk_device = VK_NULL_HANDLE;

    if ((rc = pthread_mutex_init(&device->mutex, NULL)))
    {
        ERR("Failed to initialize mutex, error %d.\n", rc);
        hr = hresult_from_errno(rc);
        goto out_free_instance;
    }
    
    device->ID3D12DeviceExt_iface.lpVtbl = &d3d12_device_vkd3d_ext_vtbl;

    if (FAILED(hr = vkd3d_create_vk_device(device, create_info)))
        goto out_free_mutex;

    if (FAILED(hr = vkd3d_private_store_init(&device->private_store)))
        goto out_free_vk_resources;

    if (FAILED(hr = vkd3d_memory_allocator_init(&device->memory_allocator, device)))
        goto out_free_private_store;

    if (FAILED(hr = vkd3d_init_format_info(device)))
        goto out_free_memory_allocator;

    if (FAILED(hr = vkd3d_memory_info_init(&device->memory_info, device)))
        goto out_cleanup_format_info;

    if (FAILED(hr = vkd3d_bindless_state_init(&device->bindless_state, device)))
        goto out_cleanup_memory_info;

    if (FAILED(hr = vkd3d_view_map_init(&device->sampler_map)))
        goto out_cleanup_bindless_state;

    if (FAILED(hr = vkd3d_sampler_state_init(&device->sampler_state, device)))
        goto out_cleanup_view_map;

    if (FAILED(hr = vkd3d_meta_ops_init(&device->meta_ops, device)))
        goto out_cleanup_sampler_state;

    if (FAILED(hr = vkd3d_shader_debug_ring_init(&device->debug_ring, device)))
        goto out_cleanup_meta_ops;

#ifdef VKD3D_ENABLE_BREADCRUMBS
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
        if (FAILED(hr = vkd3d_breadcrumb_tracer_init(&device->breadcrumb_tracer, device)))
            goto out_cleanup_debug_ring;
#endif

    if (vkd3d_descriptor_debug_active_qa_checks())
    {
        if (FAILED(hr = vkd3d_descriptor_debug_alloc_global_info(&device->descriptor_qa_global_info,
                VKD3D_DESCRIPTOR_DEBUG_DEFAULT_NUM_COOKIES, device)))
            goto out_cleanup_breadcrumb_tracer;
    }

    if ((device->parent = create_info->parent))
        IUnknown_AddRef(device->parent);

    d3d12_device_caps_init(device);

    vkd3d_init_shader_extensions(device);
    vkd3d_compute_shader_interface_key(device);

    /* Make sure all extensions and shader interface keys are computed. */
    if (FAILED(hr = vkd3d_pipeline_library_init_disk_cache(&device->disk_cache, device)))
        goto out_cleanup_descriptor_qa_global_info;

#ifdef VKD3D_ENABLE_RENDERDOC
    if (vkd3d_renderdoc_active() && vkd3d_renderdoc_global_capture_enabled())
        vkd3d_renderdoc_begin_capture(device->vkd3d_instance->vk_instance);
#endif

    return S_OK;

out_cleanup_descriptor_qa_global_info:
    vkd3d_descriptor_debug_free_global_info(device->descriptor_qa_global_info, device);
out_cleanup_breadcrumb_tracer:
#ifdef VKD3D_ENABLE_BREADCRUMBS
    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_BREADCRUMBS)
        vkd3d_breadcrumb_tracer_cleanup(&device->breadcrumb_tracer, device);
out_cleanup_debug_ring:
#endif
    vkd3d_shader_debug_ring_cleanup(&device->debug_ring, device);
out_cleanup_meta_ops:
    vkd3d_meta_ops_cleanup(&device->meta_ops, device);
out_cleanup_sampler_state:
    vkd3d_sampler_state_cleanup(&device->sampler_state, device);
out_cleanup_view_map:
    vkd3d_view_map_destroy(&device->sampler_map, device);
out_cleanup_bindless_state:
    vkd3d_bindless_state_cleanup(&device->bindless_state, device);
out_cleanup_memory_info:
    vkd3d_memory_info_cleanup(&device->memory_info, device);
out_cleanup_format_info:
    vkd3d_cleanup_format_info(device);
out_free_memory_allocator:
    vkd3d_memory_allocator_cleanup(&device->memory_allocator, device);
out_free_private_store:
    vkd3d_private_store_destroy(&device->private_store);
out_free_vk_resources:
    vk_procs = &device->vk_procs;
    VK_CALL(vkDestroyDevice(device->vk_device, NULL));
out_free_instance:
    vkd3d_instance_decref(device->vkd3d_instance);
out_free_mutex:
    pthread_mutex_destroy(&device->mutex);
    return hr;
}

bool d3d12_device_validate_shader_meta(struct d3d12_device *device, const struct vkd3d_shader_meta *meta)
{
    /* TODO: Add more as required. */
    if ((meta->flags & VKD3D_SHADER_META_FLAG_USES_NATIVE_16BIT_OPERATIONS) &&
            !device->d3d12_caps.options4.Native16BitShaderOpsSupported)
    {
        WARN("Attempting to use 16-bit operations in shader %016"PRIx64", but this is not supported.", meta->hash);
        return false;
    }

    if (meta->cs_required_wave_size)
    {
        const struct vkd3d_physical_device_info *info = &device->device_info;
        if (!info->subgroup_size_control_features.subgroupSizeControl ||
                !info->subgroup_size_control_features.computeFullSubgroups ||
                !(info->subgroup_size_control_properties.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT))
        {
            ERR("Required subgroup size control features are not supported for SM 6.6 WaveSize.\n");
            return E_INVALIDARG;
        }

        if (meta->cs_required_wave_size < info->subgroup_size_control_properties.minSubgroupSize ||
                meta->cs_required_wave_size > info->subgroup_size_control_properties.maxSubgroupSize)
        {
            ERR("Requested WaveSize %u, but supported range is [%u, %u].\n",
                    meta->cs_required_wave_size,
                    info->subgroup_size_control_properties.minSubgroupSize,
                    info->subgroup_size_control_properties.maxSubgroupSize);
            return E_INVALIDARG;
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

    if (!(object = vkd3d_calloc(1, sizeof(*object))))
    {
        pthread_mutex_unlock(&d3d12_device_map_mutex);
        return E_OUTOFMEMORY;
    }

    if (FAILED(hr = d3d12_device_init(object, instance, create_info)))
    {
        vkd3d_free(object);
        pthread_mutex_unlock(&d3d12_device_map_mutex);
        return hr;
    }

    if (!d3d12_device_supports_feature_level(object, create_info->minimum_feature_level))
    {
        WARN("Feature level %#x is not supported.\n", create_info->minimum_feature_level);
        d3d12_device_destroy(object);
        vkd3d_free(object);
        pthread_mutex_unlock(&d3d12_device_map_mutex);
        return E_INVALIDARG;
    }

    TRACE("Created device %p.\n", object);

    d3d12_add_device_singleton(object, create_info->adapter_luid);

    pthread_mutex_unlock(&d3d12_device_map_mutex);

    *device = object;

    return S_OK;
}

void d3d12_device_mark_as_removed(struct d3d12_device *device, HRESULT reason,
        const char *message, ...)
{
    va_list args;

    va_start(args, message);
    WARN("Device %p is lost (reason %#x, \"%s\").\n",
            device, reason, vkd3d_dbg_vsprintf(message, args));
    va_end(args);

    device->removed_reason = reason;
}

HRESULT vkd3d_create_thread(struct vkd3d_instance *instance,
        PFN_vkd3d_thread thread_main, void *data, union vkd3d_thread_handle *thread)
{
    HRESULT hr = S_OK;
    int rc;

    if (instance->create_thread)
    {
        if (!(thread->handle = instance->create_thread(thread_main, data)))
        {
            ERR("Failed to create thread.\n");
            hr = E_FAIL;
        }
    }
    else
    {
        if ((rc = pthread_create(&thread->pthread, NULL, thread_main, data)))
        {
            ERR("Failed to create thread, error %d.\n", rc);
            hr = hresult_from_errno(rc);
        }
    }

    return hr;
}

HRESULT vkd3d_join_thread(struct vkd3d_instance *instance, union vkd3d_thread_handle *thread)
{
    HRESULT hr = S_OK;
    int rc;

    if (instance->join_thread)
    {
        if (FAILED(hr = instance->join_thread(thread->handle)))
            ERR("Failed to join thread, hr %#x.\n", hr);
    }
    else
    {
        if ((rc = pthread_join(thread->pthread, NULL)))
        {
            ERR("Failed to join thread, error %d.\n", rc);
            hr = hresult_from_errno(rc);
        }
    }

    return hr;
}

VKD3D_EXPORT IUnknown *vkd3d_get_device_parent(ID3D12Device *device)
{
    struct d3d12_device *d3d12_device = impl_from_ID3D12Device((d3d12_device_iface *)device);

    return d3d12_device->parent;
}

VKD3D_EXPORT VkDevice vkd3d_get_vk_device(ID3D12Device *device)
{
    struct d3d12_device *d3d12_device = impl_from_ID3D12Device((d3d12_device_iface *)device);

    return d3d12_device->vk_device;
}

VKD3D_EXPORT VkPhysicalDevice vkd3d_get_vk_physical_device(ID3D12Device *device)
{
    struct d3d12_device *d3d12_device = impl_from_ID3D12Device((d3d12_device_iface *)device);

    return d3d12_device->vk_physical_device;
}

VKD3D_EXPORT struct vkd3d_instance *vkd3d_instance_from_device(ID3D12Device *device)
{
    struct d3d12_device *d3d12_device = impl_from_ID3D12Device((d3d12_device_iface *)device);

    return d3d12_device->vkd3d_instance;
}
