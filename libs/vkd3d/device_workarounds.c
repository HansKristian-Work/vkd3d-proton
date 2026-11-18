/*
 * Copyright 2026 Hans-Kristian Arntzen for Valve Corporation
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
#include "vkd3d_platform.h"

bool vkd3d_debug_control_is_test_suite(void);

enum vkd3d_application_feature_override
{
    VKD3D_APPLICATION_FEATURE_OVERRIDE_NONE = 0,
    VKD3D_APPLICATION_FEATURE_NO_DEFAULT_DXR_ON_DECK_AND_FRAME = 1 << 0,
    VKD3D_APPLICATION_FEATURE_LIMIT_DXR_1_0 = 1 << 1,
    VKD3D_APPLICATION_FEATURE_DISABLE_NV_REFLEX = 1 << 2,
    VKD3D_APPLICATION_FEATURE_MESH_SHADER_WITHOUT_BARYCENTRICS = 1 << 3,
    VKD3D_APPLICATION_FEATURE_DISABLE_ANTI_LAG = 1 << 4,
    VKD3D_APPLICATION_FEATURE_RDNA1_COMPATIBILITY = 1 << 5,
    VKD3D_APPLICATION_FEATURE_ASSUMES_STRICT_BYTE_ADDRESS_WRAP = 1 << 6,
    VKD3D_APPLICATION_FEATURE_BROKEN_WAVE128_REQUESTS = 1 << 7,
    VKD3D_APPLICATION_FEATURE_REQUIRES_MIN16_DENORMS = 1 << 8,
    VKD3D_APPLICATION_FEATURE_ATTEMPTS_COMPLEX_MULTIVIEW = 1 << 9,
};

static enum vkd3d_application_feature_override vkd3d_application_feature_override;
typedef uint32_t vkd3d_application_feature_override_flags;
union vkd3d_config_flags vkd3d_config_flags;
struct vkd3d_shader_quirk_info vkd3d_shader_quirk_info_template;

struct vkd3d_instance_application_meta
{
    enum vkd3d_string_compare_mode mode;
    const char *name;
    union vkd3d_config_flags global_flags_add;
    union vkd3d_config_flags global_flags_remove;
    vkd3d_application_feature_override_flags override;
    const char *comment;
};

struct vkd3d_shader_quirk_meta
{
    enum vkd3d_string_compare_mode mode;
    const char *name;
    const struct vkd3d_shader_quirk_info *info;
    const char *comment;
};

/* These shaders clamp the wave size to 32, but misses this in a few places of course ... */
static const struct vkd3d_shader_quirk_hash re_engine_hashes[] = {
    {
        "PersistentClusterCulling", 0,
        VKD3D_SHADER_QUIRK_CLAMP_WAVE_SIZE_TO_THREAD_GROUP32 | VKD3D_SHADER_QUIRK_ENABLE_FAIR_SCHEDULING
    },
    {
        "PersistentShadowClusterCulling", 0,
        VKD3D_SHADER_QUIRK_CLAMP_WAVE_SIZE_TO_THREAD_GROUP32 | VKD3D_SHADER_QUIRK_ENABLE_FAIR_SCHEDULING
    },
};

static const struct vkd3d_shader_quirk_info re_engine_quirks = {
    re_engine_hashes, ARRAY_SIZE(re_engine_hashes), 0,
};
#include "vkd3d_proton_app_workarounds.h"

struct vkd3d_application_database_entry
{
    const char *appid;
    enum vkd3d_string_compare_mode mode;
    const char *name;
    enum vkd3d_application_engine_class engine_class;
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
};
#include "vkd3d_proton_app_db.h"

static uint32_t vkd3d_application_version;

uint32_t vkd3d_get_instance_application_version(void)
{
    return vkd3d_application_version;
}

static enum vkd3d_application_engine_class parse_engine_class(const char *str)
{
    if (strcmp(str, "UnrealEngine") == 0)
        return VKD3D_APPLICATION_ENGINE_CLASS_UNREAL_ENGINE;
    else if (strcmp(str, "REEngine") == 0)
        return VKD3D_APPLICATION_ENGINE_CLASS_RE_ENGINE;
    else
        return VKD3D_APPLICATION_ENGINE_CLASS_UNKNOWN;
}

/* See vkd3d-proton-app-db-example.txt for how the format looks.
 * It's super basic and does the minimal thing needed for engine detection.
 */

static enum vkd3d_application_engine_class vkd3d_get_engine_version_from_db(
        const char *exe_name, const char *match_appid, uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    enum vkd3d_application_engine_class engine_class = VKD3D_APPLICATION_ENGINE_CLASS_UNKNOWN;
    char env[VKD3D_PATH_MAX];
    FILE *file = NULL;
    char line[1024];

    if (!vkd3d_get_env_var("VKD3D_APP_DB_PATH", env, sizeof(env)))
        goto end;

    INFO("Loading VKD3D_APP_DB_PATH from \"%s\"\n", env);

    file = fopen(env, "r");
    if (!file)
    {
        ERR("Failed to open \"%s\"\n", env);
        goto end;
    }

    if (!fgets(line, sizeof(line), file) || strcmp(line, "{vkd3d-proton-app-db v1}\n") != 0)
    {
        ERR("Database is corrupt.\n");
        goto end;
    }

    while (fgets(line, sizeof(line), file))
    {
        char *endp = NULL;
        char *tokens[6];
        bool has_match;
        unsigned int i;

        if (line[0] == '#' || line[0] == '\n')
            continue;

        memset(tokens, 0, sizeof(tokens));

#ifdef _MSC_VER
#define strtok_r strtok_s
#endif

        tokens[0] = strtok_r(line, ",", &endp);
        for (i = 1; i < ARRAY_SIZE(tokens) && tokens[i - 1]; i++)
            tokens[i] = strtok_r(NULL, ",", &endp);

        /* Need at least appid, exe, engine. */
        if (!tokens[0] || !tokens[1] || !tokens[2])
        {
            ERR("Invalid line found {%s} {%s} {%s}\n",
                    tokens[0] ? tokens[0] : "null",
                    tokens[1] ? tokens[1] : "null",
                    tokens[2] ? tokens[2] : "null");
            continue;
        }

        has_match = match_appid[0] != '\0' && strcmp(tokens[0], match_appid) == 0;

        if (!has_match)
        {
            enum vkd3d_string_compare_mode mode = VKD3D_STRING_COMPARE_EXACT;
            const char *parsed_exe = tokens[1];

            if (parsed_exe[0] == '=')
            {
                parsed_exe++;
            }
            else if (parsed_exe[0] == '<')
            {
                mode = VKD3D_STRING_COMPARE_STARTS_WITH;
                parsed_exe++;
            }
            else if (parsed_exe[0] == '>')
            {
                mode = VKD3D_STRING_COMPARE_ENDS_WITH;
                parsed_exe++;
            }
            else if (parsed_exe[0] == '?')
            {
                mode = VKD3D_STRING_COMPARE_CONTAINS;
                parsed_exe++;
            }
            else if (parsed_exe[0] == '@')
            {
                mode = VKD3D_STRING_COMPARE_HASH_EQUAL;
                parsed_exe++;
            }

            has_match = vkd3d_string_compare(mode, exe_name, parsed_exe);
        }

        if (!has_match)
            continue;

        engine_class = parse_engine_class(tokens[2]);

        if (tokens[3])
            *major = strtoul(tokens[3], NULL, 0);
        if (tokens[4])
            *minor = strtoul(tokens[4], NULL, 0);
        if (tokens[5])
            *patch = strtoul(tokens[5], NULL, 0);

        break;
    }

    if (engine_class == VKD3D_APPLICATION_ENGINE_CLASS_UNREAL_ENGINE)
        INFO("Detected UnrealEngine from database, version %u.%u.%u\n", *major, *minor, *patch);
    else if (engine_class == VKD3D_APPLICATION_ENGINE_CLASS_RE_ENGINE)
        INFO("Detected REEngine from database, version %u.%u.%u\n", *major, *minor, *patch);

end:
    if (file)
        fclose(file);

    return engine_class;
}

static enum vkd3d_application_engine_class vkd3d_get_engine_version_from_static_db(
        const char *exe_name, const char *appid, uint32_t *major, uint32_t *minor, uint32_t *patch)
{
    enum vkd3d_application_engine_class engine_class = VKD3D_APPLICATION_ENGINE_CLASS_UNKNOWN;
    unsigned int i;

    INFO("Parsing static application database hash: %s\n", application_database_hash);

    for (i = 0; i < ARRAY_SIZE(application_database_entries); i++)
    {
        const struct vkd3d_application_database_entry *entry = &application_database_entries[i];
        bool match = false;

        if ((entry->appid && strcmp(appid, entry->appid) == 0) ||
             vkd3d_string_compare(entry->mode, exe_name, entry->name))
            match = true;

        if (match)
        {
            engine_class = entry->engine_class;
            *major = entry->major;
            *minor = entry->minor;
            *patch = entry->patch;
            break;
        }
    }

    if (engine_class == VKD3D_APPLICATION_ENGINE_CLASS_UNREAL_ENGINE)
        INFO("Detected UnrealEngine from static database, version %u.%u.%u\n", *major, *minor, *patch);
    else if (engine_class == VKD3D_APPLICATION_ENGINE_CLASS_RE_ENGINE)
        INFO("Detected REEngine from static database, version %u.%u.%u\n", *major, *minor, *patch);

    return engine_class;
}

void vkd3d_instance_apply_application_workarounds(void)
{
    uint32_t engine_major = 0, engine_minor = 0, engine_patch = 0;
    enum vkd3d_application_engine_class engine_class;
    char app[VKD3D_PATH_MAX];
    char appid[32];
    size_t i;

    if (!vkd3d_get_program_name(app))
        return;

    if (vkd3d_get_env_var("SteamAppID", appid, sizeof(appid)))
        INFO("Detected SteamAppID %s\n", appid);
    else
        appid[0] = '\0';

    INFO("Application DB JSON hash: %s\n", application_override_db_hash);

    engine_class = vkd3d_get_engine_version_from_db(app, appid, &engine_major, &engine_minor, &engine_patch);

    if (engine_class == VKD3D_APPLICATION_ENGINE_CLASS_UNKNOWN)
        engine_class = vkd3d_get_engine_version_from_static_db(app, appid, &engine_major, &engine_minor, &engine_patch);

    if (engine_class == VKD3D_APPLICATION_ENGINE_CLASS_UNKNOWN)
    {
        if ((engine_class = vkd3d_get_engine_version(&engine_major, &engine_minor, &engine_patch)))
        {
            switch (engine_class)
            {
                case VKD3D_APPLICATION_ENGINE_CLASS_UNREAL_ENGINE:
                    if (engine_major == 0)
                        INFO("Detected Unreal Engine through dubious means, version is unknown.\n");
                    else
                        INFO("Detected Unreal Engine %u.%u.%u.\n", engine_major, engine_minor, engine_patch);

                    if (engine_major == 5)
                        vkd3d_application_version = VKD3D_APPLICATION_VERSION_ENGINE_UNREAL_ENGINE_5;
                    else if (engine_major == 4)
                        vkd3d_application_version = VKD3D_APPLICATION_VERSION_ENGINE_UNREAL_ENGINE_4;
                    else if (engine_major == 0)
                        vkd3d_application_version = VKD3D_APPLICATION_VERSION_ENGINE_UNREAL_ENGINE_UNKNOWN;
                    break;

                case VKD3D_APPLICATION_ENGINE_CLASS_RE_ENGINE:
                    INFO("Detected RE Engine\n");
                    vkd3d_application_version = VKD3D_APPLICATION_VERSION_ENGINE_RE_ENGINE;
                    break;

                default:
                    break;
            }
        }
        else if (strstr(app, "Win64-Shipping.exe"))
        {
            engine_class = VKD3D_APPLICATION_ENGINE_CLASS_UNREAL_ENGINE;
            INFO("Detected Unreal Engine by means of .exe name detection. Version unknown.\n");
            vkd3d_application_version = VKD3D_APPLICATION_VERSION_ENGINE_UNREAL_ENGINE_UNKNOWN;
        }
    }

    /* If we don't have any application specific patterns we hit, engage default engine workarounds. */

    for (i = 0; i < ARRAY_SIZE(application_override); i++)
    {
        const struct vkd3d_instance_application_meta *meta = &application_override[i];

        bool match;
        if (meta->mode == VKD3D_STRING_COMPARE_APPID)
            match = vkd3d_string_compare(VKD3D_STRING_COMPARE_EXACT, appid, meta->name);
        else
            match = vkd3d_string_compare(meta->mode, app, meta->name);

        if (match)
        {
            vkd3d_config_flag_global_add(meta->global_flags_add);
            vkd3d_config_flag_global_remove(meta->global_flags_remove);
            INFO("Detected game %s, adding %u configs, removing %u configs.\n",
                 app, vkd3d_config_flag_popcount(meta->global_flags_add),
                 vkd3d_config_flag_popcount(meta->global_flags_remove));
            vkd3d_application_feature_override = meta->override;
            if (meta->comment)
                INFO("%s\n", meta->comment);
            break;
        }
    }

    if (i == ARRAY_SIZE(application_override) && engine_class == VKD3D_APPLICATION_ENGINE_CLASS_UNREAL_ENGINE)
    {
		/* Unreal Engine catch-all. ReBAR is a massive uplift on RX 7600 for example in Wukong.
		 * AMD windows drivers also seem to have some kind of general app-opt for UE titles.
		 * Use no-staggered-submit by default on UE. We've only observed issues in Wukong here, but
		 * unless we see proof that UE titles want staggered,
		 * we'll disable for now to be defensive and de-risk any large scale regressions. */
        INFO("Applying default Unreal Engine workarounds.\n");
        vkd3d_config_flag_global_add(VKD3D_CONFIG_FLAG_INIT(.SMALL_VRAM_REBAR = 1, .NO_STAGGERED_SUBMIT = 1));
    }

    if (engine_class == VKD3D_APPLICATION_ENGINE_CLASS_UNREAL_ENGINE && (engine_major == 5 || engine_major == 0))
    {
        /* UE5 is broken and requests wave128, yet the shader doesn't actually support that.
         * Relevant for Turnip. It should be safe to enable this for UE4 too in case that hits the unknown path. */
        vkd3d_application_feature_override |= VKD3D_APPLICATION_FEATURE_BROKEN_WAVE128_REQUESTS;

        if (engine_major == 5)
        {
            /* This is not an app bug, but we have to disable a minor optimization
             * which assumes no unsigned wrap for raw buffer addresses. */
            vkd3d_application_feature_override |= VKD3D_APPLICATION_FEATURE_ASSUMES_STRICT_BYTE_ADDRESS_WRAP;
        }
    }

    for (i = 0; i < ARRAY_SIZE(application_shader_quirks); i++)
    {
        const struct vkd3d_shader_quirk_meta *meta = &application_shader_quirks[i];

        bool match;
        if (meta->mode == VKD3D_STRING_COMPARE_APPID)
            match = vkd3d_string_compare(VKD3D_STRING_COMPARE_EXACT, appid, meta->name);
        else
            match = vkd3d_string_compare(meta->mode, app, meta->name);

        if (match)
        {
            vkd3d_shader_quirk_info_template = *meta->info;
            INFO("Detected game %s, adding shader quirks for specific shaders.\n", app);
            if (meta->comment)
                INFO("%s\n", meta->comment);
            break;
        }
    }

    if (i == ARRAY_SIZE(application_shader_quirks) && engine_class == VKD3D_APPLICATION_ENGINE_CLASS_RE_ENGINE)
    {
        INFO("Applying default RE Engine shader quirks.\n");
        vkd3d_shader_quirk_info_template = re_engine_quirks;
    }

    if (VKD3D_CONFIG_FLAG_IS_SET(ADRENO_7XX_EXTRA_COMPAT))
    {
        /* Enable slower paths to remain more compatible with spec requirements on Adreno 7xx.
         * Used for debugging to isolate if a game hits these cases.
         */
        vkd3d_application_feature_override |= VKD3D_APPLICATION_FEATURE_ASSUMES_STRICT_BYTE_ADDRESS_WRAP;
        vkd3d_application_feature_override |= VKD3D_APPLICATION_FEATURE_REQUIRES_MIN16_DENORMS;
        vkd3d_shader_quirk_info_template.global_quirks |= VKD3D_SHADER_QUIRK_FORCE_DENORM_LEGACY_FP16_CONVERSIONS;

        /* Probably useful thing to have. */
        vkd3d_application_feature_override |= VKD3D_APPLICATION_FEATURE_BROKEN_WAVE128_REQUESTS;
    }
}

void vkd3d_instance_deduce_config_flags_from_environment(void)
{
    char env[VKD3D_PATH_MAX];

    if (vkd3d_get_env_var("VKD3D_SHADER_OVERRIDE", env, sizeof(env)) ||
            vkd3d_get_env_var("VKD3D_SHADER_DUMP_PATH", env, sizeof(env)) ||
            vkd3d_get_env_var("VKD3D_QA_HASHES", env, sizeof(env)))
    {
        INFO("VKD3D_SHADER_OVERRIDE, VKD3D_SHADER_DUMP_PATH or VKD3D_QA_HASHES is used, pipeline_library_ignore_spirv option is enforced.\n");
        vkd3d_config_flag_global_add(VKD3D_CONFIG_FLAG(PIPELINE_LIBRARY_IGNORE_SPIRV));
    }

    if (vkd3d_get_env_var("FOSSILIZE", env, sizeof(env)) && strcmp(env, "1") == 0 &&
            vkd3d_get_env_var("FOSSILIZE_DUMP_PATH", env, sizeof(env)))
    {
        INFO("Fossilize is enabled. pipeline_library_ignore_spirv option is enforced.\n");
        vkd3d_config_flag_global_add(VKD3D_CONFIG_FLAG(PIPELINE_LIBRARY_IGNORE_SPIRV));
    }

    vkd3d_get_env_var("VKD3D_SHADER_CACHE_PATH", env, sizeof(env));
    if (strcmp(env, "0") == 0)
    {
        INFO("Shader cache is explicitly disabled, relying solely on application caches.\n");
        vkd3d_config_flag_global_add(VKD3D_CONFIG_FLAG(PIPELINE_LIBRARY_APP_CACHE_ONLY));
    }

    /* If we're using a global shader cache, it's meaningless to use PSO caches. */
    if (!VKD3D_CONFIG_FLAG_IS_SET(PIPELINE_LIBRARY_APP_CACHE_ONLY))
    {
        INFO("shader_cache is used, global_pipeline_cache is enforced.\n");
        vkd3d_config_flag_global_add(VKD3D_CONFIG_FLAG(GLOBAL_PIPELINE_CACHE));
    }

    /* Normally, we would use VK_EXT_tooling_info for this, but we don't observe layers across the winevulkan layer.
     * The global env-var on the other hand, does ... */
    if (vkd3d_get_env_var("ENABLE_VULKAN_RENDERDOC_CAPTURE", env, sizeof(env)) &&
            strcmp(env, "1") == 0)
    {
        INFO("RenderDoc capture is enabled. Forcing HOST CACHED memory types and disabling pipeline caching completely.\n");
        vkd3d_config_flag_global_add(VKD3D_CONFIG_FLAG_INIT(
                    .FORCE_HOST_CACHED = 1,
                    .PIPELINE_LIBRARY_APP_CACHE_ONLY = 1,
                    .GLOBAL_PIPELINE_CACHE = 1,
                    .PIPELINE_LIBRARY_NO_SERIALIZE_SPIRV = 1,
                    .PIPELINE_LIBRARY_IGNORE_SPIRV = 1,
                    .DEBUG_UTILS = 1,
                    .EXTENDED_DEBUG_UTILS = 1));
    }

    if (vkd3d_get_env_var("MESA_VK_TRACE", env, sizeof(env)) && strcmp(env, "rgp") == 0)
    {
        INFO("Thread trace is enabled. Forcing debug utils to be enabled for labels.\n");
        /* Disable caching so we can get full debug information when emitting labels. */
        vkd3d_config_flag_global_add(VKD3D_CONFIG_FLAG_INIT(
                    .DEBUG_UTILS = 1,
                    .GLOBAL_PIPELINE_CACHE = 1,
                    .PIPELINE_LIBRARY_APP_CACHE_ONLY = 1,
                    .PIPELINE_LIBRARY_NO_SERIALIZE_SPIRV = 1,
                    .PIPELINE_LIBRARY_IGNORE_SPIRV = 1));
    }
}

void vkd3d_instance_apply_global_shader_quirks(void)
{
    unsigned int level;
    char env[64];

    struct override
    {
        union vkd3d_config_flags config;
        vkd3d_shader_quirks_t quirk;
        bool negative;
    };

    static const struct override overrides[] =
    {
        { VKD3D_CONFIG_FLAG_STATIC(FORCE_NO_INVARIANT_POSITION), VKD3D_SHADER_QUIRK_INVARIANT_POSITION, true },
        { VKD3D_CONFIG_FLAG_STATIC(DESCRIPTOR_HEAP_ROBUSTNESS), VKD3D_SHADER_QUIRK_DESCRIPTOR_HEAP_ROBUSTNESS, false },
        { VKD3D_CONFIG_FLAG_STATIC(LEGACY_STRUCTURIZER), VKD3D_SHADER_QUIRK_LEGACY_STRUCTURIZER, false },
    };
    bool eq_test;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(overrides); i++)
    {
        eq_test = !overrides[i].negative;
        if (vkd3d_config_flag_is_set(overrides[i].config) == eq_test)
            vkd3d_shader_quirk_info_template.global_quirks |= overrides[i].quirk;
    }

    if (vkd3d_get_env_var("VKD3D_LIMIT_TESS_FACTORS", env, sizeof(env)))
    {
        static const struct
        {
            unsigned int level;
            vkd3d_shader_quirks_t quirk;
        } mapping[] = {
            { 4, VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_4 },
            { 8, VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_8 },
            { 12, VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_12 },
            { 16, VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_16 },
            { 32, VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_32 },
        };

        /* Override what any app profile did. */
        vkd3d_shader_quirk_info_template.global_quirks &= ~(VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_4 |
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
                vkd3d_shader_quirk_info_template.global_quirks |= mapping[i].quirk;
                break;
            }
        }
    }
}

static bool d3d12_device_is_steam_deck(const struct d3d12_device *device)
{
    return device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_MESA_RADV &&
            device->device_info.properties2.properties.vendorID == VKD3D_VENDOR_ID_AMD &&
            (device->device_info.properties2.properties.deviceID == 0x163f ||
             device->device_info.properties2.properties.deviceID == 0x1435);
}

static bool d3d12_device_is_steam_frame(const struct d3d12_device *device)
{
    return device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_MESA_TURNIP &&
           device->device_info.properties2.properties.vendorID == VKD3D_VENDOR_ID_QUALCOMM &&
           device->device_info.properties2.properties.deviceID == 0x43051401;
}

static bool d3d12_device_is_steam_deck_or_frame(const struct d3d12_device *device)
{
    return d3d12_device_is_steam_deck(device) || d3d12_device_is_steam_frame(device);
}

void d3d12_device_caps_override_application(struct d3d12_device *device)
{
    /* Some games rely on certain features to be exposed before they let the primary feature
     * be exposed. */
    if (vkd3d_application_feature_override & VKD3D_APPLICATION_FEATURE_NO_DEFAULT_DXR_ON_DECK_AND_FRAME)
    {
        /* For games which automatically enable RT even on Deck or Frame, leading to very poor performance by default. */
        if (d3d12_device_is_steam_deck_or_frame(device) && !VKD3D_CONFIG_FLAG_IS_SET(DXR))
        {
            INFO("Disabling automatic enablement of DXR on Deck/Frame.\n");
            device->d3d12_caps.options5.RaytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
        }
    }

    if (vkd3d_application_feature_override & VKD3D_APPLICATION_FEATURE_LIMIT_DXR_1_0)
    {
        if (device->d3d12_caps.options5.RaytracingTier > D3D12_RAYTRACING_TIER_1_0)
        {
            INFO("Limiting reported DXR tier to 1.0.\n");
            device->d3d12_caps.options5.RaytracingTier = D3D12_RAYTRACING_TIER_1_0;
        }
    }

    if (vkd3d_application_feature_override & VKD3D_APPLICATION_FEATURE_DISABLE_NV_REFLEX)
    {
        INFO("Disabling NV reflex.\n");
        device->vk_info.NV_low_latency2 = false;
    }

    if (vkd3d_application_feature_override & VKD3D_APPLICATION_FEATURE_DISABLE_ANTI_LAG)
    {
        INFO("Disabling AMD anti-lag.\n");
        device->vk_info.AMD_anti_lag = false;
        device->device_info.anti_lag_amd.antiLag = VK_FALSE;
    }
}

void vkd3d_physical_device_info_apply_workarounds(struct vkd3d_physical_device_info *info,
        struct d3d12_device *device)
{
    /* A performance workaround for NV.
     * The 16 byte offset is a lie, as that is only actually required when we
     * use vectorized load-stores. When we emit vectorized load-store ops,
     * the storage buffer must be aligned properly, so this is fine in practice
     * and is a nice speed boost.
     * See vkd3d_init_shader_extensions. Need to add VKD3D_SHADER_TARGET_EXTENSION_SHADER_IMPLIED_SSBO_ALIGNMENT.
     */
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
    if (!(vkd3d_application_feature_override & VKD3D_APPLICATION_FEATURE_MESH_SHADER_WITHOUT_BARYCENTRICS) &&
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

    if (!VKD3D_CONFIG_FLAG_IS_SET(SKIP_DRIVER_WORKAROUNDS))
    {
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

        if (info->vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY &&
            info->properties2.properties.driverVersion <= VKD3D_DRIVER_VERSION_MAKE_NV(595, 0, 0))
        {
            WARN("Disabling present_id2, wait2 and timing on pre-595 NV drivers.\n");
            device->device_info.swapchain_maintenance1_features.swapchainMaintenance1 = VK_FALSE;
            device->vk_info.EXT_present_timing = false;
            device->vk_info.KHR_present_id2 = false;
            device->vk_info.KHR_present_wait2 = false;
            device->device_info.present_id2_features.presentId2 = VK_FALSE;
            device->device_info.present_wait2_features.presentWait2 = VK_FALSE;
            device->device_info.present_timing_features.presentTiming = VK_FALSE;
            device->device_info.present_timing_features.presentAtAbsoluteTime = VK_FALSE;
            device->device_info.present_timing_features.presentAtRelativeTime = VK_FALSE;
        }

        if (info->vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY)
        {
            /* Float controls2 is broken where we need it to work. Keep using slow quantize path
             * until driver works. */
            WARN("Disabling shader_float_controls2 on NV drivers due to buggy implementation.\n");
            device->device_info.float_controls2_features.shaderFloatControls2 = VK_FALSE;
            device->vk_info.KHR_shader_float_controls2 = false;
        }
    }
}

bool vkd3d_driver_id_wraps_ssbo_32bit_before_robustness(VkDriverId driver_id)
{
    /* In Vulkan, it's more or less UB when accessing a logical array outside its bounds.
     * Based on this language, it seems like implementations are allowed to wrap the address space
     * beyond 4G (and negative offsets) as long as <4G is the maximum descriptor size.
     * From OpAccessChain in SPIR-V spec:
     * " - if indexing into a vector, array, or matrix, with the result type being a logical pointer type,
     * behavior is undefined if not in bounds."
     */

    /* By default, just assume it's fine as-is. We can evaluate later if we should take (slightly) slower path by default.
     * Only one game is known to be affected by this. */
    if (!(vkd3d_application_feature_override & VKD3D_APPLICATION_FEATURE_ASSUMES_STRICT_BYTE_ADDRESS_WRAP) &&
        !vkd3d_debug_control_is_test_suite())
        return true;

    switch (driver_id)
    {
        case VK_DRIVER_ID_AMD_OPEN_SOURCE:
        case VK_DRIVER_ID_AMD_PROPRIETARY:
        case VK_DRIVER_ID_MESA_RADV:
        case VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA:
        case VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS:
        case VK_DRIVER_ID_MESA_NVK:
        case VK_DRIVER_ID_NVIDIA_PROPRIETARY:
            /* The major desktop vendors all seem to have same behavior.
             * NVIDIA is somewhat surprising given how they do robustness, but tests don't lie.
             * Quirky behavior given how OpAccessChain is defined, but it's actually the most convenient interpretation,
             * and maps well to D3D12. */
            return true;

        case VK_DRIVER_ID_MESA_TURNIP:
        case VK_DRIVER_ID_QUALCOMM_PROPRIETARY:
            /* Known to implement SSBOs as pseudo-texel buffers, which will not have the desired wrapping behavior. */
            return false;

        default:
            /* Conservative, assume it might not. */
            return false;
    }
}

bool d3d12_device_allow_emulated_vrs_tier_2(struct d3d12_device *device)
{
    return (vkd3d_application_feature_override & VKD3D_APPLICATION_FEATURE_RDNA1_COMPATIBILITY) &&
            device->device_info.properties2.properties.vendorID == VKD3D_VENDOR_ID_AMD &&
            device->device_info.vulkan_1_3_properties.minSubgroupSize == 32;
}

bool d3d12_device_allow_emulated_barycentrics(struct d3d12_device* device)
{
    return (vkd3d_application_feature_override & VKD3D_APPLICATION_FEATURE_RDNA1_COMPATIBILITY) &&
            device->device_info.properties2.properties.vendorID == VKD3D_VENDOR_ID_AMD &&
            device->vk_info.AMD_shader_explicit_vertex_parameter &&
            device->device_info.vulkan_1_3_properties.minSubgroupSize == 32;
}

bool vkd3d_application_has_broken_wave128(void)
{
    return (vkd3d_application_feature_override & VKD3D_APPLICATION_FEATURE_BROKEN_WAVE128_REQUESTS) != 0;
}

bool vkd3d_application_requires_min16_denorms(void)
{
    return (vkd3d_application_feature_override & VKD3D_APPLICATION_FEATURE_REQUIRES_MIN16_DENORMS) != 0;
}

bool vkd3d_application_attempts_complex_multiview(void)
{
    return (vkd3d_application_feature_override & VKD3D_APPLICATION_FEATURE_ATTEMPTS_COMPLEX_MULTIVIEW) != 0;
}

VKD3D_DEBUG_CONTROL_BEHAVIOR_FLAGS vkd3d_debug_control_get_behavior_flags(void);

void d3d12_device_init_workarounds(struct d3d12_device *device)
{
    uint32_t major, minor, patch;

    /* Have a local copy of this since we may need to apply per-device workarounds in shader compiler. */
    device->workarounds.quirks = vkd3d_shader_quirk_info_template;

    /* If we're faking VRS tier 1, we need to just nop out everything about primitive shading rate. */
    if (d3d12_device_allow_emulated_vrs_tier_2(device) &&
        !d3d12_device_supports_variable_shading_rate_tier_1(device))
    {
        device->workarounds.quirks.global_quirks |= VKD3D_SHADER_QUIRK_IGNORE_PRIMITIVE_SHADING_RATE;
    }

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
            /* If the GPU can take advantage of tiling, we should aim for suspend resume properly.
             * Treat this as a performance workaround (it kinda is, since it'll slow down CPU recording speed as a result). */
            device->workarounds.tiler_suspend_resume = true;
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

    /* For testing purposes, allow us to exercise all code paths on all GPUs. */
    if (vkd3d_debug_control_get_behavior_flags() & VKD3D_DEBUG_CONTROL_BEHAVIOR_ENABLE_TILER_SYNC)
        device->workarounds.tiler_renderpass_barriers = true;
    else if (vkd3d_debug_control_get_behavior_flags() & VKD3D_DEBUG_CONTROL_BEHAVIOR_DISABLE_TILER_SYNC)
        device->workarounds.tiler_renderpass_barriers = false;
    if (vkd3d_debug_control_get_behavior_flags() & VKD3D_DEBUG_CONTROL_BEHAVIOR_ENABLE_SUSPEND_RESUME)
        device->workarounds.tiler_suspend_resume = true;
    if (vkd3d_debug_control_get_behavior_flags() & VKD3D_DEBUG_CONTROL_BEHAVIOR_DISABLE_SUSPEND_RESUME)
        device->workarounds.tiler_suspend_resume = false;

    /* Having to split render passes when there is a mismatch in load-store ops is unfortunate.
     * Be spec correct by default, and go a bit out of spec if we know the drivers are sensible. */
    switch (device->device_info.vulkan_1_2_properties.driverID)
    {
        case VK_DRIVER_ID_MESA_TURNIP:
        case VK_DRIVER_ID_MESA_RADV:
        case VK_DRIVER_ID_MESA_NVK:
        case VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA:
        case VK_DRIVER_ID_NVIDIA_PROPRIETARY:
            /* Currently only relevant on Turnip really, since that's where we enable suspend-resume by default. */
            device->workarounds.tiler_suspend_resume_relax_load_store_op = device->workarounds.tiler_suspend_resume;
            break;

        default:
            break;
    }

    if (VKD3D_CONFIG_FLAG_IS_SET(SKIP_DRIVER_WORKAROUNDS))
        return;

    if (device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_MESA_RADV &&
        device->device_info.properties2.properties.driverVersion < VK_MAKE_VERSION(26, 2, 0))
    {
        /* Fixed in RADV 26.2+. */
        if (device->device_info.properties2.properties.limits.maxImageDimension1D < 32768 &&
            device->device_info.compute_shader_derivatives_features_khr.computeDerivativeGroupQuads)
        {
            WARN("Disabling computeDerivativeGroupQuads on pre-RDNA4 HW due to buggy emulation.\n");
            device->device_info.compute_shader_derivatives_features_khr.computeDerivativeGroupQuads = VK_FALSE;
        }
    }

    if (device->device_info.properties2.properties.vendorID == 0x1002)
    {
        if (vkd3d_get_linux_kernel_version(&major, &minor, &patch))
        {
            uint32_t ver;

            /* 6.10 amdgpu kernel changes the clear vram code to do background clears instead
             * of on-demand clearing. This seems to have bugs, and we have been able to observe
             * non-zeroed VRAM coming from the affected kernels.
             * This workaround needs to be in place until we have confirmed a fix in upstream kernel. */
            INFO("Detected Linux kernel version %u.%u.%u\n", major, minor, patch);

            ver = major * 1000000 + minor * 1000 + patch;

            /* Fixed in kernel 6.15.9 and 6.16+. */
            if (ver >= 6010000 && ver < 6015009)
            {
                INFO("AMDGPU broken kernel detected. Enabling manual memory clearing path.\n");
                device->workarounds.amdgpu_broken_clearvram = true;
            }
        }

        if (device->device_info.vulkan_1_2_properties.driverID != VK_DRIVER_ID_MESA_RADV ||
            device->device_info.properties2.properties.driverVersion < VK_MAKE_VERSION(26, 2, 0))
        {
            /* Current AMD GPUs have a bug where NULL pages which are read through SMEM unit
             * does not understand PRT, leading to GPU hangs on a bogus page fault instead of returning the correct 0 value.
             * Worked around properly in Mesa 26.2+. */
            device->workarounds.amdgpu_broken_null_tile_mapping = true;
            INFO("Broken NULL PRT with SMEM detected, per-game workarounds may apply.\n");
        }

        /* Works around a weird GPU hang on RDNA4.
         * See mesa issue https://gitlab.freedesktop.org/mesa/mesa/-/issues/14812.
         * Observed in both RE2 and RE8, so this is likely a uarch specific issue.
         * The game doesn't seem to actually write useful subsampling here anyway.
         * Use large 1D texture support as a sentinel for RDNA4.
         * RDNA3 reports 16k. */
        if (device->device_info.properties2.properties.limits.maxImageDimension1D >= 32768 &&
            !vkd3d_debug_control_is_test_suite() &&
            device->device_info.properties2.properties.driverVersion < VK_MAKE_VERSION(26, 1, 0))
        {
            INFO("Nooping SV_ShadingRate on RDNA4 due to unknown HW quirk causing hangs.\n");
            device->workarounds.quirks.global_quirks |= VKD3D_SHADER_QUIRK_IGNORE_PRIMITIVE_SHADING_RATE;
        }
    }
}
