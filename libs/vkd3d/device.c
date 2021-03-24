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

#ifdef VKD3D_ENABLE_RENDERDOC
#include "vkd3d_renderdoc.h"
#endif

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
#include "vkd3d_descriptor_debug.h"
#endif

struct vkd3d_struct
{
    enum vkd3d_structure_type type;
    const void *next;
};

#define vkd3d_find_struct(c, t) vkd3d_find_struct_(c, VKD3D_STRUCTURE_TYPE_##t)
static const void *vkd3d_find_struct_(const struct vkd3d_struct *chain,
        enum vkd3d_structure_type type)
{
    while (chain)
    {
        if (chain->type == type)
            return chain;

        chain = chain->next;
    }

    return NULL;
}

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
    VK_EXTENSION(KHR_SPIRV_1_4, KHR_spirv_1_4),
    VK_EXTENSION(KHR_SHADER_FLOAT_CONTROLS, KHR_shader_float_controls),
    VK_EXTENSION(KHR_FRAGMENT_SHADING_RATE, KHR_fragment_shading_rate),
    VK_EXTENSION(KHR_CREATE_RENDERPASS_2, KHR_create_renderpass2),
    /* EXT extensions */
    VK_EXTENSION(EXT_CALIBRATED_TIMESTAMPS, EXT_calibrated_timestamps),
    VK_EXTENSION(EXT_CONDITIONAL_RENDERING, EXT_conditional_rendering),
    VK_EXTENSION(EXT_CONSERVATIVE_RASTERIZATION, EXT_conservative_rasterization),
    VK_EXTENSION(EXT_CUSTOM_BORDER_COLOR, EXT_custom_border_color),
    VK_EXTENSION(EXT_DEPTH_CLIP_ENABLE, EXT_depth_clip_enable),
    VK_EXTENSION(EXT_DESCRIPTOR_INDEXING, EXT_descriptor_indexing),
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
    VK_EXTENSION(EXT_EXTERNAL_MEMORY_HOST, EXT_external_memory_host),
    VK_EXTENSION(EXT_4444_FORMATS, EXT_4444_formats),
    /* AMD extensions */
    VK_EXTENSION(AMD_BUFFER_MARKER, AMD_buffer_marker),
    VK_EXTENSION(AMD_SHADER_CORE_PROPERTIES, AMD_shader_core_properties),
    VK_EXTENSION(AMD_SHADER_CORE_PROPERTIES_2, AMD_shader_core_properties2),
    /* NV extensions */
    VK_EXTENSION(NV_SHADER_SM_BUILTINS, NV_shader_sm_builtins),
    /* VALVE extensions */
    VK_EXTENSION(VALVE_MUTABLE_DESCRIPTOR_TYPE, VALVE_mutable_descriptor_type),
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
    const char *disabled_extensions;

    if (!(disabled_extensions = getenv("VKD3D_DISABLE_EXTENSIONS")))
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

static HRESULT vkd3d_init_instance_caps(struct vkd3d_instance *instance,
        const struct vkd3d_instance_create_info *create_info,
        uint32_t *instance_extension_count, bool **user_extension_supported)
{
    const struct vkd3d_vk_global_procs *vk_procs = &instance->vk_global_procs;
    const struct vkd3d_optional_instance_extensions_info *optional_extensions;
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

    optional_extensions = vkd3d_find_struct(create_info->next, OPTIONAL_INSTANCE_EXTENSIONS_INFO);
    if (optional_extensions && optional_extensions->extension_count)
    {
        if (!(*user_extension_supported = vkd3d_calloc(optional_extensions->extension_count, sizeof(bool))))
        {
            vkd3d_free(vk_extensions);
            return E_OUTOFMEMORY;
        }
    }
    else
    {
        *user_extension_supported = NULL;
    }

    *instance_extension_count = vkd3d_check_extensions(vk_extensions, count, NULL, 0,
            optional_instance_extensions, ARRAY_SIZE(optional_instance_extensions),
            create_info->instance_extensions, create_info->instance_extension_count,
            optional_extensions ? optional_extensions->extensions : NULL,
            optional_extensions ? optional_extensions->extension_count : 0,
            *user_extension_supported, vulkan_info, "instance");

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
     */
    unsigned int i;
    static const uint32_t ignored_ids[] = {
        0xc05b3a9du,
        0x2864340eu,
        0xbfcfaec2u,
        0x96f03c1cu,
        0x8189c842u,
        0x3d492883u,
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

uint64_t vkd3d_config_flags = 0;

struct vkd3d_instance_application_meta
{
    const char *name;
    uint64_t global_flags_add;
    uint64_t global_flags_remove;
};
static const struct vkd3d_instance_application_meta application_override[] = {
    /* MSVC fails to compile empty array. */
    { NULL, 0, 0 }
};

static void vkd3d_instance_apply_application_workarounds(void)
{
    char app[VKD3D_PATH_MAX];
    size_t i;
    if (!vkd3d_get_program_name(app))
        return;

    for (i = 0; i < ARRAY_SIZE(application_override); i++)
    {
        if (application_override[i].name && !strcmp(app, application_override[i].name))
        {
            vkd3d_config_flags |= application_override[i].global_flags_add;
            vkd3d_config_flags &= ~application_override[i].global_flags_remove;
            INFO("Detected game %s, adding config 0x%"PRIx64", removing masks 0x%"PRIx64".\n",
                 app, application_override[i].global_flags_add, application_override[i].global_flags_remove);
            break;
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
    {"multi_queue", VKD3D_CONFIG_FLAG_MULTI_QUEUE},
};

static void vkd3d_config_flags_init_once(void)
{
    const char *config;

    config = getenv("VKD3D_CONFIG");
    vkd3d_config_flags = vkd3d_parse_debug_options(config, vkd3d_config_options, ARRAY_SIZE(vkd3d_config_options));

    if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_SKIP_APPLICATION_WORKAROUNDS))
        vkd3d_instance_apply_application_workarounds();

#ifdef VKD3D_ENABLE_RENDERDOC
    /* Enable debug utils by default for Renderdoc builds */
    TRACE("Renderdoc build implies VKD3D_CONFIG_FLAG_DEBUG_UTILS.\n");
    vkd3d_config_flags |= VKD3D_CONFIG_FLAG_DEBUG_UTILS;
#endif

    if (vkd3d_config_flags)
        INFO("VKD3D_CONFIG='%s'.\n", config ? config : "");
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
    const struct vkd3d_optional_instance_extensions_info *optional_extensions;
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

    if (FAILED(hr = vkd3d_init_instance_caps(instance, create_info,
            &extension_count, &user_extension_supported)))
    {
        if (instance->libvulkan)
            vkd3d_dlclose(instance->libvulkan);
        return hr;
    }

    if (vk_global_procs->vkEnumerateInstanceVersion)
        vk_global_procs->vkEnumerateInstanceVersion(&loader_version);

    if (loader_version < VKD3D_MIN_API_VERSION)
    {
        ERR("Vulkan %u.%u not supported by loader.\n",
                VK_VERSION_MAJOR(VKD3D_MIN_API_VERSION),
                VK_VERSION_MINOR(VKD3D_MIN_API_VERSION));
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

    optional_extensions = vkd3d_find_struct(create_info->next, OPTIONAL_INSTANCE_EXTENSIONS_INFO);

    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pNext = NULL;
    instance_info.flags = 0;
    instance_info.pApplicationInfo = &application_info;
    instance_info.enabledLayerCount = 0;
    instance_info.ppEnabledLayerNames = NULL;
    instance_info.enabledExtensionCount = vkd3d_enable_extensions(extensions, NULL, 0,
            optional_instance_extensions, ARRAY_SIZE(optional_instance_extensions),
            create_info->instance_extensions, create_info->instance_extension_count,
            optional_extensions ? optional_extensions->extensions : NULL,
            optional_extensions ? optional_extensions->extension_count : 0,
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
            ERR("Failed to enumerate instance layers, will not use VK_LAYER_KHRONOS_validation!\n");
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
    if (create_info->type != VKD3D_STRUCTURE_TYPE_INSTANCE_CREATE_INFO)
    {
        WARN("Invalid structure type %#x.\n", create_info->type);
        return E_INVALIDARG;
    }

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

bool d3d12_device_supports_variable_shading_rate_tier_1(struct d3d12_device *device)
{
    const struct vkd3d_physical_device_info *info = &device->device_info;

    return info->fragment_shading_rate_features.pipelineFragmentShadingRate &&
            (device->vk_info.device_limits.framebufferColorSampleCounts & VK_SAMPLE_COUNT_2_BIT);
}

bool d3d12_device_supports_ray_tracing_tier_1_0(const struct d3d12_device *device)
{
    return device->device_info.acceleration_structure_features.accelerationStructure &&
            device->device_info.ray_tracing_pipeline_features.rayTracingPipeline &&
            device->d3d12_caps.options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
}

static D3D12_VARIABLE_SHADING_RATE_TIER d3d12_device_determine_variable_shading_rate_tier(struct d3d12_device *device)
{
    if (!d3d12_device_supports_variable_shading_rate_tier_1(device))
        return D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;

    /* TODO: TIER_2
        - impl RSSetShadingRateImage
        - prop fragmentShadingRateNonTrivialCombinerOp
        - feat primitiveFragmentShadingRate
            - DXIL bringup for PrimitiveShadingRateKHR built-in
        - feat attachmentFragmentShadingRate
    */

    return D3D12_VARIABLE_SHADING_RATE_TIER_1;
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
        vk_prepend_struct(&info->properties2, &info->subgroup_size_control_properties);
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

    VK_CALL(vkGetPhysicalDeviceFeatures2(device->vk_physical_device, &info->features2));
    VK_CALL(vkGetPhysicalDeviceProperties2(device->vk_physical_device, &info->properties2));
}

static void vkd3d_trace_physical_device_properties(const VkPhysicalDeviceProperties *properties)
{
    TRACE("Device name: %s.\n", properties->deviceName);
    TRACE("Vendor ID: %#x, Device ID: %#x.\n", properties->vendorID, properties->deviceID);
    TRACE("Driver version: %#x (%u.%u.%u, %u.%u.%u.%u).\n", properties->driverVersion,
            VK_VERSION_MAJOR(properties->driverVersion),
            VK_VERSION_MINOR(properties->driverVersion),
            VK_VERSION_PATCH(properties->driverVersion),
            (properties->driverVersion >> 22),
            (properties->driverVersion >> 14) & 0xff,
            (properties->driverVersion >> 6)  & 0xff,
            (properties->driverVersion >> 0)  & 0x3f);
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
        uint32_t *device_extension_count, bool **user_extension_supported)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance->vk_procs;
    const struct vkd3d_optional_device_extensions_info *optional_extensions;
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

    optional_extensions = vkd3d_find_struct(create_info->next, OPTIONAL_DEVICE_EXTENSIONS_INFO);
    if (optional_extensions && optional_extensions->extension_count)
    {
        if (!(*user_extension_supported = vkd3d_calloc(optional_extensions->extension_count, sizeof(bool))))
        {
            vkd3d_free(vk_extensions);
            return E_OUTOFMEMORY;
        }
    }
    else
    {
        *user_extension_supported = NULL;
    }

    *device_extension_count = vkd3d_check_extensions(vk_extensions, count, NULL, 0,
            optional_device_extensions, ARRAY_SIZE(optional_device_extensions),
            create_info->device_extensions, create_info->device_extension_count,
            optional_extensions ? optional_extensions->extensions : NULL,
            optional_extensions ? optional_extensions->extension_count : 0,
            *user_extension_supported, vulkan_info, "device");

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

    /* Shader extensions. */
    vulkan_info->shader_extension_count = 0;
    if (vulkan_info->EXT_shader_demote_to_helper_invocation)
    {
        vulkan_info->shader_extensions[vulkan_info->shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_SPV_EXT_DEMOTE_TO_HELPER_INVOCATION;
    }

    if (physical_device_info->features2.features.shaderStorageImageReadWithoutFormat)
    {
        vulkan_info->shader_extensions[vulkan_info->shader_extension_count++] =
                VKD3D_SHADER_TARGET_EXTENSION_READ_STORAGE_IMAGE_WITHOUT_FORMAT;
    }

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

    if (!vulkan_info->KHR_create_renderpass2)
    {
        ERR("KHR_create_renderpass2 is not supported by this implementation. This is required for correct operation.\n");
        return E_INVALIDARG;
    }

    if (vulkan_info->KHR_fragment_shading_rate)
        physical_device_info->additional_shading_rates_supported = d3d12_device_determine_additional_shading_rates_supported(device);

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
    const char *filter;
    uint32_t count;
    unsigned int i;
    VkResult vr;

    filter = getenv("VKD3D_FILTER_DEVICE_NAME");

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

        if (filter && !strstr(device_properties.deviceName, filter))
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

        for (i = 0; i < queue_family->queue_count; i++)
        {
            if (queue_family->queues[i])
                vkd3d_queue_destroy(queue_family->queues[i], device);
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

        if (queue_info->family_index[i] != VK_QUEUE_FAMILY_IGNORED)
        {
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
    single_queue = !(vkd3d_config_flags & VKD3D_CONFIG_FLAG_MULTI_QUEUE);

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
    const struct vkd3d_optional_device_extensions_info *optional_extensions;
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

    if (FAILED(hr = vkd3d_init_device_extensions(device, create_info,
            &extension_count, &user_extension_supported)))
        return hr;

    vkd3d_physical_device_info_init(&device->device_info, device);

    if (FAILED(hr = vkd3d_init_device_caps(device, create_info, &device->device_info)))
        return hr;

    if (!(extensions = vkd3d_calloc(extension_count, sizeof(*extensions))))
    {
        vkd3d_free(user_extension_supported);
        return E_OUTOFMEMORY;
    }

    optional_extensions = vkd3d_find_struct(create_info->next, OPTIONAL_DEVICE_EXTENSIONS_INFO);

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
            create_info->device_extensions, create_info->device_extension_count,
            optional_extensions ? optional_extensions->extensions : NULL,
            optional_extensions ? optional_extensions->extension_count : 0,
            user_extension_supported, &device->vk_info);
    device_info.ppEnabledExtensionNames = extensions;
    device_info.pEnabledFeatures = &device->device_info.features2.features;
    vkd3d_free(user_extension_supported);

    vr = VK_CALL(vkCreateDevice(physical_device, &device_info, NULL, &vk_device));
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
            return;
        }
    }
}

static HRESULT d3d12_device_create_scratch_buffer(struct d3d12_device *device, VkDeviceSize size, struct vkd3d_scratch_buffer *scratch)
{
    struct vkd3d_allocate_heap_memory_info alloc_info;
    HRESULT hr;

    TRACE("device %p, size %llu, scratch %p.\n", device, size, scratch);

    memset(&alloc_info, 0, sizeof(alloc_info));
    alloc_info.heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    alloc_info.heap_desc.SizeInBytes = size;
    alloc_info.heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    alloc_info.heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

    if (FAILED(hr = vkd3d_allocate_heap_memory(device, &device->memory_allocator,
            &alloc_info, &scratch->allocation)))
        return hr;

    scratch->offset = 0;
    return S_OK;
}

static void d3d12_device_destroy_scratch_buffer(struct d3d12_device *device, const struct vkd3d_scratch_buffer *scratch)
{
    TRACE("device %p, scratch %p.\n", device, scratch);

    vkd3d_free_memory(device, &device->memory_allocator, &scratch->allocation);
}

HRESULT d3d12_device_get_scratch_buffer(struct d3d12_device *device, VkDeviceSize min_size, struct vkd3d_scratch_buffer *scratch)
{
    if (min_size > VKD3D_SCRATCH_BUFFER_SIZE)
        return d3d12_device_create_scratch_buffer(device, min_size, scratch);

    pthread_mutex_lock(&device->mutex);

    if (device->scratch_buffer_count)
    {
        *scratch = device->scratch_buffers[--device->scratch_buffer_count];
        scratch->offset = 0;
        pthread_mutex_unlock(&device->mutex);
        return S_OK;
    }
    else
    {
        pthread_mutex_unlock(&device->mutex);
        return d3d12_device_create_scratch_buffer(device, VKD3D_SCRATCH_BUFFER_SIZE, scratch);
    }
}

void d3d12_device_return_scratch_buffer(struct d3d12_device *device, const struct vkd3d_scratch_buffer *scratch)
{
    pthread_mutex_lock(&device->mutex);

    if (scratch->allocation.resource.size == VKD3D_SCRATCH_BUFFER_SIZE &&
            device->scratch_buffer_count < VKD3D_SCRATCH_BUFFER_COUNT)
    {
        device->scratch_buffers[device->scratch_buffer_count++] = *scratch;
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

        case VKD3D_QUERY_TYPE_INDEX_RT_SERIALIZE_SIZE:
            pool_info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR;
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
static inline struct d3d12_device *impl_from_ID3D12Device(d3d12_device_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_device, ID3D12Device_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_QueryInterface(d3d12_device_iface *iface,
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
    size_t i;

    for (i = 0; i < device->scratch_buffer_count; i++)
        d3d12_device_destroy_scratch_buffer(device, &device->scratch_buffers[i]);

    for (i = 0; i < device->query_pool_count; i++)
        d3d12_device_destroy_query_pool(device, &device->query_pools[i]);

    vkd3d_free(device->descriptor_heap_gpu_vas);

    vkd3d_private_store_destroy(&device->private_store);

    vkd3d_cleanup_format_info(device);
    vkd3d_shader_debug_ring_cleanup(&device->debug_ring, device);
    vkd3d_sampler_state_cleanup(&device->sampler_state, device);
    vkd3d_view_map_destroy(&device->sampler_map, device);
    vkd3d_meta_ops_cleanup(&device->meta_ops, device);
    vkd3d_bindless_state_cleanup(&device->bindless_state, device);
    vkd3d_destroy_null_resources(&device->null_resources, device);
    vkd3d_render_pass_cache_cleanup(&device->render_pass_cache, device);
    d3d12_device_destroy_vkd3d_queues(device);
    vkd3d_memory_allocator_cleanup(&device->memory_allocator, device);
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
    struct d3d12_command_allocator *object;
    HRESULT hr;

    TRACE("iface %p, type %#x, riid %s, command_allocator %p.\n",
            iface, type, debugstr_guid(riid), command_allocator);

    if (FAILED(hr = d3d12_command_allocator_create(device, type, &object)))
        return hr;

    return return_interface(&object->ID3D12CommandAllocator_iface, &IID_ID3D12CommandAllocator,
            riid, command_allocator);
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

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommandList(d3d12_device_iface *iface,
        UINT node_mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *command_allocator,
        ID3D12PipelineState *initial_pipeline_state, REFIID riid, void **command_list)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_command_allocator *allocator;
    struct d3d12_command_list *object;
    HRESULT hr;

    TRACE("iface %p, node_mask 0x%08x, type %#x, command_allocator %p, "
            "initial_pipeline_state %p, riid %s, command_list %p.\n",
            iface, node_mask, type, command_allocator,
            initial_pipeline_state, debugstr_guid(riid), command_list);

    if (!(allocator = unsafe_impl_from_ID3D12CommandAllocator(command_allocator)))
    {
        WARN("Command allocator is NULL.\n");
        return E_INVALIDARG;
    }

    if (allocator->type != type)
    {
        WARN("Command list types do not match (allocator %#x, list %#x).\n",
                allocator->type, type);
        return E_INVALIDARG;
    }

    if (FAILED(hr = d3d12_command_list_create(device, node_mask, type, command_allocator,
            initial_pipeline_state, &object)))
        return hr;

    return return_interface(&object->ID3D12GraphicsCommandList_iface,
            &IID_ID3D12GraphicsCommandList, riid, command_list);
}

/* Direct3D feature levels restrict which formats can be optionally supported. */
static void vkd3d_restrict_format_support_for_feature_level(D3D12_FEATURE_DATA_FORMAT_SUPPORT *format_support)
{
    static const D3D12_FEATURE_DATA_FORMAT_SUPPORT blacklisted_format_features[] =
    {
        {DXGI_FORMAT_B8G8R8A8_TYPELESS, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW,
                D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE},
        {DXGI_FORMAT_B8G8R8A8_UNORM,    D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW,
                D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE},
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(blacklisted_format_features); ++i)
    {
        if (blacklisted_format_features[i].Format == format_support->Format)
        {
            format_support->Support1 &= ~blacklisted_format_features[i].Support1;
            format_support->Support2 &= ~blacklisted_format_features[i].Support2;
            break;
        }
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
            const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
            D3D12_FEATURE_DATA_FORMAT_SUPPORT *data = feature_data;
            VkFormatFeatureFlagBits image_features;
            const struct vkd3d_format *format;
            VkFormatProperties properties;

            if (feature_data_size != sizeof(*data))
            {
                WARN("Invalid size %u.\n", feature_data_size);
                return E_INVALIDARG;
            }

            data->Support1 = D3D12_FORMAT_SUPPORT1_NONE;
            data->Support2 = D3D12_FORMAT_SUPPORT2_NONE;
            if (!(format = vkd3d_get_format(device, data->Format, false)))
                format = vkd3d_get_format(device, data->Format, true);
            if (!format)
            {
                FIXME("Unhandled format %#x.\n", data->Format);
                return E_INVALIDARG;
            }

            VK_CALL(vkGetPhysicalDeviceFormatProperties(device->vk_physical_device, format->vk_format, &properties));
            image_features = properties.linearTilingFeatures | properties.optimalTilingFeatures;

            if (properties.bufferFeatures)
                data->Support1 |= D3D12_FORMAT_SUPPORT1_BUFFER;
            if (properties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)
                data->Support1 |= D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER;
            if (data->Format == DXGI_FORMAT_R16_UINT || data->Format == DXGI_FORMAT_R32_UINT)
                data->Support1 |= D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER;
            if (image_features)
                data->Support1 |= D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURE2D
                        | D3D12_FORMAT_SUPPORT1_TEXTURE3D | D3D12_FORMAT_SUPPORT1_TEXTURECUBE;
            if (image_features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
            {
                data->Support1 |= D3D12_FORMAT_SUPPORT1_SHADER_LOAD | D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD
                        | D3D12_FORMAT_SUPPORT1_SHADER_GATHER;
                if (image_features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
                {
                    data->Support1 |= D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE
                            | D3D12_FORMAT_SUPPORT1_MIP;
                }
                if (format->vk_aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
                    data->Support1 |= D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON
                            | D3D12_FORMAT_SUPPORT1_SHADER_GATHER_COMPARISON;
            }
            if (image_features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
                data->Support1 |= D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET;
            if (image_features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
                data->Support1 |= D3D12_FORMAT_SUPPORT1_BLENDABLE;
            if (image_features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                data->Support1 |= D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL;
            if (image_features & VK_FORMAT_FEATURE_BLIT_SRC_BIT)
                data->Support1 |= D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE;
            if (image_features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
                data->Support1 |= D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;

            if (image_features & VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT)
                data->Support2 |= D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD
                        | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS
                        | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE
                        | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE
                        | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX
                        | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX;

            vkd3d_restrict_format_support_for_feature_level(data);

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

    switch (descriptor_heap_type)
    {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            return sizeof(struct d3d12_desc);

        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
            return sizeof(struct d3d12_rtv_desc);

        default:
            FIXME("Unhandled type %#x.\n", descriptor_heap_type);
            return 0;
    }
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

    d3d12_desc_create_cbv(d3d12_desc_from_cpu_handle(descriptor), device, desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateShaderResourceView(d3d12_device_iface *iface,
        ID3D12Resource *resource, const D3D12_SHADER_RESOURCE_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, resource %p, desc %p, descriptor %#lx.\n",
            iface, resource, desc, descriptor.ptr);

    d3d12_desc_create_srv(d3d12_desc_from_cpu_handle(descriptor),
            device, unsafe_impl_from_ID3D12Resource(resource), desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateUnorderedAccessView(d3d12_device_iface *iface,
        ID3D12Resource *resource, ID3D12Resource *counter_resource,
        const D3D12_UNORDERED_ACCESS_VIEW_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, resource %p, counter_resource %p, desc %p, descriptor %#lx.\n",
            iface, resource, counter_resource, desc, descriptor.ptr);

    d3d12_desc_create_uav(d3d12_desc_from_cpu_handle(descriptor),
            device, unsafe_impl_from_ID3D12Resource(resource),
            unsafe_impl_from_ID3D12Resource(counter_resource), desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateRenderTargetView(d3d12_device_iface *iface,
        ID3D12Resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    TRACE("iface %p, resource %p, desc %p, descriptor %#lx.\n",
            iface, resource, desc, descriptor.ptr);

    d3d12_rtv_desc_create_rtv(d3d12_rtv_desc_from_cpu_handle(descriptor),
            impl_from_ID3D12Device(iface), unsafe_impl_from_ID3D12Resource(resource), desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateDepthStencilView(d3d12_device_iface *iface,
        ID3D12Resource *resource, const D3D12_DEPTH_STENCIL_VIEW_DESC *desc,
        D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    TRACE("iface %p, resource %p, desc %p, descriptor %#lx.\n",
            iface, resource, desc, descriptor.ptr);

    d3d12_rtv_desc_create_dsv(d3d12_rtv_desc_from_cpu_handle(descriptor),
            impl_from_ID3D12Device(iface), unsafe_impl_from_ID3D12Resource(resource), desc);
}

static void STDMETHODCALLTYPE d3d12_device_CreateSampler(d3d12_device_iface *iface,
        const D3D12_SAMPLER_DESC *desc, D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);

    TRACE("iface %p, desc %p, descriptor %#lx.\n", iface, desc, descriptor.ptr);

    d3d12_desc_create_sampler(d3d12_desc_from_cpu_handle(descriptor), device, desc);
}

static inline D3D12_CPU_DESCRIPTOR_HANDLE d3d12_advance_cpu_descriptor_handle(D3D12_CPU_DESCRIPTOR_HANDLE handle,
        unsigned int increment, unsigned int units)
{
    handle.ptr += increment * units;
    return handle;
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
                d3d12_desc_copy(d3d12_desc_from_cpu_handle(dst),
                        d3d12_desc_from_cpu_handle(src), copy_count,
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
    TRACE("iface %p, descriptor_count %u, dst_descriptor_range_offset %#lx, "
            "src_descriptor_range_offset %#lx, descriptor_heap_type %#x.\n",
            iface, descriptor_count, dst_descriptor_range_offset.ptr, src_descriptor_range_offset.ptr,
            descriptor_heap_type);

    d3d12_device_copy_descriptors(impl_from_ID3D12Device(iface),
            1, &dst_descriptor_range_offset, &descriptor_count,
            1, &src_descriptor_range_offset, &descriptor_count,
            descriptor_heap_type);
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

static HRESULT STDMETHODCALLTYPE d3d12_device_CreatePlacedResource(d3d12_device_iface *iface,
        ID3D12Heap *heap, UINT64 heap_offset,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value, REFIID iid, void **resource)
{
    struct d3d12_heap *heap_object = unsafe_impl_from_ID3D12Heap(heap);
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_resource *object;
    HRESULT hr;

    TRACE("iface %p, heap %p, heap_offset %#"PRIx64", desc %p, initial_state %#x, "
            "optimized_clear_value %p, iid %s, resource %p.\n",
            iface, heap, heap_offset, desc, initial_state,
            optimized_clear_value, debugstr_guid(iid), resource);

    if (FAILED(hr = d3d12_resource_create_placed(device, desc, heap_object,
            heap_offset, initial_state, optimized_clear_value, &object)))
        return hr;

    return return_interface(&object->ID3D12Resource_iface, &IID_ID3D12Resource, iid, resource);
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
    FIXME("iface %p, object %p, attributes %p, access %#x, name %s, handle %p stub!\n",
            iface, object, attributes, access, debugstr_w(name), handle);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_OpenSharedHandle(d3d12_device_iface *iface,
        HANDLE handle, REFIID riid, void **object)
{
    FIXME("iface %p, handle %p, riid %s, object %p stub!\n",
            iface, handle, debugstr_guid(riid), object);

    return E_NOTIMPL;
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
    struct d3d12_fence *object;
    HRESULT hr;

    TRACE("iface %p, intial_value %#"PRIx64", flags %#x, riid %s, fence %p.\n",
            iface, initial_value, flags, debugstr_guid(riid), fence);

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

static void STDMETHODCALLTYPE d3d12_device_GetCopyableFootprints(d3d12_device_iface *iface,
        const D3D12_RESOURCE_DESC *desc, UINT first_sub_resource, UINT sub_resource_count,
        UINT64 base_offset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT *layouts,
        UINT *row_counts, UINT64 *row_sizes, UINT64 *total_bytes)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    static const struct vkd3d_format vkd3d_format_unknown
            = {DXGI_FORMAT_UNKNOWN, VK_FORMAT_UNDEFINED, 1, 1, 1, 1, 0};

    unsigned int i, sub_resource_idx, miplevel_idx, row_count, row_size, row_pitch;
    unsigned int width, height, depth, array_size;
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
    if (total_bytes)
        *total_bytes = ~(uint64_t)0;

    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        format = &vkd3d_format_unknown;
    }
    else if (!(format = vkd3d_format_from_d3d12_resource_desc(device, desc, 0)))
    {
        WARN("Invalid format %#x.\n", desc->Format);
        return;
    }

    if (FAILED(d3d12_resource_validate_desc(desc, device)))
    {
        WARN("Invalid resource desc.\n");
        return;
    }

    array_size = d3d12_resource_desc_get_layer_count(desc);

    if (first_sub_resource >= desc->MipLevels * array_size
            || sub_resource_count > desc->MipLevels * array_size - first_sub_resource)
    {
        WARN("Invalid sub-resource range %u-%u for resource.\n", first_sub_resource, sub_resource_count);
        return;
    }

    offset = 0;
    total = 0;
    for (i = 0; i < sub_resource_count; ++i)
    {
        sub_resource_idx = first_sub_resource + i;
        miplevel_idx = sub_resource_idx % desc->MipLevels;
        width = align(d3d12_resource_desc_get_width(desc, miplevel_idx), format->block_width);
        height = align(d3d12_resource_desc_get_height(desc, miplevel_idx), format->block_height);
        depth = d3d12_resource_desc_get_depth(desc, miplevel_idx);
        row_count = height / format->block_height;
        row_size = (width / format->block_width) * format->byte_count * format->block_byte_count;
        row_pitch = align(row_size, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

        if (layouts)
        {
            layouts[i].Offset = base_offset + offset;
            layouts[i].Footprint.Format = desc->Format;
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
        size = max(0, depth - 1) * align(size, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) + size;

        total = offset + size;
        offset = align(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    }
    if (total_bytes)
        *total_bytes = total;
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
        const D3D12_COMMAND_SIGNATURE_DESC *desc, ID3D12RootSignature *root_signature,
        REFIID iid, void **command_signature)
{
    struct d3d12_device *device = impl_from_ID3D12Device(iface);
    struct d3d12_command_signature *object;
    HRESULT hr;

    TRACE("iface %p, desc %p, root_signature %p, iid %s, command_signature %p.\n",
            iface, desc, root_signature, debugstr_guid(iid), command_signature);

    if (FAILED(hr = d3d12_command_signature_create(device, desc, &object)))
        return hr;

    return return_interface(&object->ID3D12CommandSignature_iface,
            &IID_ID3D12CommandSignature, iid, command_signature);
}

static void STDMETHODCALLTYPE d3d12_device_GetResourceTiling(d3d12_device_iface *iface,
        ID3D12Resource *resource, UINT *tile_count, D3D12_PACKED_MIP_INFO *packed_mip_info,
        D3D12_TILE_SHAPE *tile_shape, UINT *tiling_count, UINT first_tiling,
        D3D12_SUBRESOURCE_TILING *tilings)
{
    struct d3d12_sparse_info *sparse = &unsafe_impl_from_ID3D12Resource(resource)->sparse;
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

    max_tiling_count = sparse->tiling_count - min(first_tiling, sparse->tiling_count);
    max_tiling_count = min(max_tiling_count, *tiling_count);

    for (i = 0; i < max_tiling_count; i++)
        tilings[i] = sparse->tilings[first_tiling + i];

    *tiling_count = max_tiling_count;
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
    struct d3d12_pipeline_library* pipeline_library;
    HRESULT hr;

    TRACE("iface %p, blob %p, blob_size %lu, iid %s, lib %p.\n",
            iface, blob, blob_size, debugstr_guid(iid), lib);

    if (FAILED(hr = d3d12_pipeline_library_create(device, blob, blob_size, &pipeline_library)))
        return hr;

    return return_interface(&pipeline_library->ID3D12PipelineLibrary_iface,
            &IID_ID3D12PipelineLibrary, iid, lib);
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
    struct d3d12_command_list *object;
    HRESULT hr;

    TRACE("iface %p, node_mask 0x%08x, type %#x, flags %#x, riid %s, command_list %p.\n",
            iface, node_mask, type, flags, debugstr_guid(riid), command_list);

    if (FAILED(hr = d3d12_command_list_create(device, node_mask, type, NULL, NULL, &object)))
        return hr;

    return return_interface(&object->ID3D12GraphicsCommandList_iface,
            &IID_ID3D12GraphicsCommandList, riid, command_list);
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateProtectedResourceSession(d3d12_device_iface *iface,
        const D3D12_PROTECTED_RESOURCE_SESSION_DESC *desc, REFIID iid, void **session)
{
    FIXME("iface %p, desc %p, iid %s, session %p stub!\n",
            iface, desc, debugstr_guid(iid), session);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_device_CreateCommittedResource1(d3d12_device_iface *iface,
        const D3D12_HEAP_PROPERTIES *heap_properties, D3D12_HEAP_FLAGS heap_flags,
        const D3D12_RESOURCE_DESC *desc, D3D12_RESOURCE_STATES initial_state,
        const D3D12_CLEAR_VALUE *optimized_clear_value,
        ID3D12ProtectedResourceSession *protected_session,
        REFIID iid, void **resource)
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
            heap_flags, initial_state, optimized_clear_value, &object)))
    {
        *resource = NULL;
        return hr;
    }

    return return_interface(&object->ID3D12Resource_iface, &IID_ID3D12Resource, iid, resource);
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
    HRESULT hr;

    TRACE("iface %p, desc %p, initial_state %#x, optimized_clear_value %p, protected_session %p, iid %s, resource %p.\n",
            iface, desc, initial_state, optimized_clear_value, protected_session, debugstr_guid(iid), resource);

    if (protected_session)
        FIXME("Ignoring protected session %p.\n", protected_session);

    if (FAILED(hr = d3d12_resource_create_reserved(device, desc,
            initial_state, optimized_clear_value, &object)))
        return hr;

    return return_interface(&object->ID3D12Resource_iface, &IID_ID3D12Resource, iid, resource);
}

static D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE d3d12_device_GetResourceAllocationInfo1(d3d12_device_iface *iface,
        D3D12_RESOURCE_ALLOCATION_INFO *info, UINT visible_mask, UINT count, const D3D12_RESOURCE_DESC *resource_descs,
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
        const D3D12_RESOURCE_DESC *desc = &resource_descs[i];
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

    if (FAILED(hr = d3d12_state_object_create(device, desc, &state)))
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

    if (!vkd3d_acceleration_structure_convert_inputs(&build_info, desc))
    {
        ERR("Failed to convert inputs.\n");
        memset(info, 0, sizeof(*info));
        return;
    }

    if (build_info.build_info.mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR)
        FIXME("MODE_UPDATE_KHR in PrebuildInfo?\n");

    memset(&size_info, 0, sizeof(size_info));
    size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

    VK_CALL(vkGetAccelerationStructureBuildSizesKHR(device->vk_device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info.build_info,
            build_info.primitive_counts, &size_info));

    vkd3d_acceleration_structure_build_info_cleanup(&build_info);

    info->ResultDataMaxSizeInBytes = size_info.accelerationStructureSize;
    info->ScratchDataSizeInBytes = size_info.buildScratchSize;
    info->UpdateScratchDataSizeInBytes = size_info.updateScratchSize;
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

static CONST_VTBL struct ID3D12Device6Vtbl d3d12_device_vtbl =
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
            !device->queue_families[VKD3D_QUEUE_FAMILY_SPARSE_BINDING]->queue_count)
        return D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;

    if (!features->shaderResourceResidency || !features->shaderResourceMinLod ||
            sparse_properties->residencyAlignedMipSize ||
            !sparse_properties->residencyNonResidentStrict ||
            !minmax_properties->filterMinmaxSingleComponentFormats)
        return D3D12_TILED_RESOURCES_TIER_1;

    return D3D12_TILED_RESOURCES_TIER_2;
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

static D3D12_RAYTRACING_TIER d3d12_device_determine_ray_tracing_tier(struct d3d12_device *device)
{
    const struct vkd3d_vk_instance_procs *vk_procs = &device->vkd3d_instance->vk_procs;
    const struct vkd3d_physical_device_info *info = &device->device_info;
    D3D12_RAYTRACING_TIER tier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    VkFormatProperties properties;
    bool supports_vbo_formats;
    unsigned int i;

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
        supports_vbo_formats = true;
        for (i = 0; i < ARRAY_SIZE(required_vbo_formats); i++)
        {
            VK_CALL(vkGetPhysicalDeviceFormatProperties(device->vk_physical_device,
                    required_vbo_formats[i],
                    &properties));

            if (!(properties.bufferFeatures & VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR))
            {
                supports_vbo_formats = false;
                INFO("Vulkan format %u is not supported for RTAS VBO, cannot support DXR tier 1.0.\n", required_vbo_formats[i]);
            }
        }

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

    return tier;
}

static D3D12_RESOURCE_HEAP_TIER d3d12_device_determine_heap_tier(struct d3d12_device *device)
{
    const VkPhysicalDeviceLimits *limits = &device->device_info.properties2.properties.limits;
    const VkPhysicalDeviceMemoryProperties *mem_properties = &device->memory_properties;
    const struct vkd3d_memory_info *mem_info = &device->memory_info;
    uint32_t i, host_visible_types = 0;

    for (i = 0; i < mem_properties->memoryTypeCount; i++)
    {
        if (mem_properties->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            host_visible_types |= 1u << i;
    }

    // Heap Tier 2 requires us to be able to create a heap that supports all resource
    // categories at the same time, except RT/DS textures on UPLOAD/READBACK heaps.
    if (limits->bufferImageGranularity > D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT ||
            !(mem_info->buffer_type_mask & mem_info->sampled_type_mask & mem_info->rt_ds_type_mask) ||
            !(mem_info->buffer_type_mask & mem_info->sampled_type_mask & host_visible_types))
        return D3D12_RESOURCE_HEAP_TIER_1;

    return D3D12_RESOURCE_HEAP_TIER_2;
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
    options->TypedUAVLoadAdditionalFormats =
            features->shaderStorageImageExtendedFormats &&
            features->shaderStorageImageReadWithoutFormat;
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
    /* Does spirv-dxil support this? */
    options1->Int64ShaderOps = FALSE;

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
            D3D12_COMMAND_LIST_SUPPORT_FLAG_COMPUTE | D3D12_COMMAND_LIST_SUPPORT_FLAG_COPY;
    /* Currently not supported */
    options3->ViewInstancingTier = D3D12_VIEW_INSTANCING_TIER_NOT_SUPPORTED;
    /* Currently not supported */
    options3->BarycentricsSupported = FALSE;
}

static void d3d12_device_caps_init_feature_options4(struct d3d12_device *device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 *options4 = &device->d3d12_caps.options4;

    /* Requires changes to format compatibility */
    options4->MSAA64KBAlignedTextureSupported = FALSE;
    /* Shared resources not supported */
    options4->SharedResourceCompatibilityTier = D3D12_SHARED_RESOURCE_COMPATIBILITY_TIER_0;
    options4->Native16BitShaderOpsSupported = device->device_info.float16_int8_features.shaderFloat16;
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

    /* Currently not supported, requires TIER_2 shading rate */
    options6->PerPrimitiveShadingRateSupportedWithViewportIndexing = FALSE;
    options6->ShadingRateImageTileSize = 0;
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

    if (device->api_version >= VK_API_VERSION_1_1 &&
        physical_device_info->subgroup_properties.subgroupSize >= 4 &&
        (physical_device_info->subgroup_properties.supportedOperations & required) == required &&
        (physical_device_info->subgroup_properties.supportedStages & required_stages) == required_stages)
    {
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
        if (device->device_info.float16_int8_features.shaderFloat16 &&
                device->device_info.storage_16bit_features.storageBuffer16BitAccess &&
                device->device_info.subgroup_extended_types_features.shaderSubgroupExtendedTypes)
        {
            /* These features are required by FidelityFX SSSR demo. */
            /* Technically we need storageInputOutput16 as well, but
             * we can probably work around it on devices which don't support it. */
            device->d3d12_caps.max_shader_model = D3D_SHADER_MODEL_6_2;
            TRACE("Enabling support for SM 6.2.\n");
        }

        /* SM 6.3 adds:
         * https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model-6.3
         * Ray tracing (lib_6_3 multi entry point targets).
         */
        if (device->d3d12_caps.max_shader_model == D3D_SHADER_MODEL_6_2 &&
            device->device_info.ray_tracing_pipeline_features.rayTracingPipeline &&
            device->vk_info.KHR_spirv_1_4)
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
    }
    else
    {
        device->d3d12_caps.max_shader_model = D3D_SHADER_MODEL_5_1;
        TRACE("Enabling support for SM 5.1.\n");
    }
}

static void d3d12_device_caps_override(struct d3d12_device *device)
{
    D3D_FEATURE_LEVEL fl_override = (D3D_FEATURE_LEVEL)0;
    struct d3d12_caps *caps = &device->d3d12_caps;
    const char* fl_string;
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

    if (!(fl_string = getenv("VKD3D_FEATURE_LEVEL")))
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
        caps->options3.WriteBufferImmediateSupportFlags |= D3D12_COMMAND_LIST_SUPPORT_FLAG_BUNDLE;
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
    d3d12_device_caps_init_feature_level(device);

    d3d12_device_caps_override(device);
}

static bool d3d12_device_supports_feature_level(struct d3d12_device *device, D3D_FEATURE_LEVEL feature_level)
{
    return feature_level <= device->d3d12_caps.max_feature_level;
}

struct d3d12_device *unsafe_impl_from_ID3D12Device(d3d12_device_iface *iface)
{
    if (!iface)
        return NULL;

#ifdef VKD3D_ENABLE_PROFILING
    assert(iface->lpVtbl == &d3d12_device_vtbl ||
           iface->lpVtbl == &d3d12_device_vtbl_profiled);
#else
    assert(iface->lpVtbl == &d3d12_device_vtbl);
#endif

    return impl_from_ID3D12Device(iface);
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

    if (FAILED(hr = vkd3d_init_null_resources(&device->null_resources, device)))
        goto out_cleanup_format_info;

    if (FAILED(hr = vkd3d_bindless_state_init(&device->bindless_state, device)))
        goto out_destroy_null_resources;

    if (FAILED(hr = vkd3d_view_map_init(&device->sampler_map)))
        goto out_cleanup_bindless_state;

    if (FAILED(hr = vkd3d_sampler_state_init(&device->sampler_state, device)))
        goto out_cleanup_view_map;

    if (FAILED(hr = vkd3d_meta_ops_init(&device->meta_ops, device)))
        goto out_cleanup_sampler_state;

    if (FAILED(hr = vkd3d_shader_debug_ring_init(&device->debug_ring, device)))
        goto out_cleanup_meta_ops;

    vkd3d_render_pass_cache_init(&device->render_pass_cache);

    if ((device->parent = create_info->parent))
        IUnknown_AddRef(device->parent);

    d3d12_device_caps_init(device);
    return S_OK;

out_cleanup_meta_ops:
    vkd3d_meta_ops_cleanup(&device->meta_ops, device);
out_cleanup_sampler_state:
    vkd3d_sampler_state_cleanup(&device->sampler_state, device);
out_cleanup_view_map:
    vkd3d_view_map_destroy(&device->sampler_map, device);
out_cleanup_bindless_state:
    vkd3d_bindless_state_cleanup(&device->bindless_state, device);
out_destroy_null_resources:
    vkd3d_destroy_null_resources(&device->null_resources, device);
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
