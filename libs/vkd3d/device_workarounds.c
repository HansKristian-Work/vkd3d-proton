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
};
static const struct vkd3d_instance_application_meta application_override[] = {
    /* MSVC fails to compile empty array. */
    { VKD3D_STRING_COMPARE_EXACT, "GravityMark.exe", VKD3D_CONFIG_FLAG_STATIC(FORCE_MINIMUM_SUBGROUP_SIZE) },
    /* Halo Infinite (1240440).
     * Game relies on NON_ZEROED committed UAVs to be cleared to zero on allocation.
     * This works okay with zerovram on first game boot, but not later, since this memory is guaranteed to be recycled.
     * Game also relies on indirectly modifying CBV root descriptors, which means we are forced to rely on RAW_VA_CBV.
     * It also relies on multi-dispatch indirect with state updates which is ... ye.
     * Need another config flag to workaround that as well.
     * Poor loading times and performance with ReBar on some devices.
     */
    { VKD3D_STRING_COMPARE_EXACT, "HaloInfinite.exe",
            VKD3D_CONFIG_FLAG_INIT_STATIC(
                .FORCE_RAW_VA_CBV = 1, .USE_HOST_IMPORT_FALLBACK = 1,
                .PREALLOCATE_SRV_MIP_CLAMPS = 1,
                .NO_UPLOAD_HVV = 1) },
    /* (1182900) Workaround amdgpu kernel bug with host memory import and concurrent submissions. */
    { VKD3D_STRING_COMPARE_EXACT, "APlagueTaleRequiem_x64.exe",
            VKD3D_CONFIG_FLAG_INIT_STATIC(.USE_HOST_IMPORT_FALLBACK = 1, .DISABLE_UAV_COMPRESSION = 1) },
    /* Shadow of the Tomb Raider (750920).
     * Invariant workarounds actually cause more issues than they resolve on NV.
     * RADV already has workarounds by default.
     * FIXME: The proper workaround will be a workaround which force-emits mul + add + precise. The vertex shaders
     * are broken enough that normal invariance is not enough. */
    { VKD3D_STRING_COMPARE_EXACT, "SOTTR.exe",
        VKD3D_CONFIG_FLAG_INIT_STATIC(.FORCE_NO_INVARIANT_POSITION = 1, .REQUIRE_INPUT_ATTACHMENTS = 1) },
    /* Elden Ring (1245620).
     * Game is really churny on committed memory allocations, and does not use NOT_ZEROED. Clearing works causes bubbles.
     * It seems to work just fine however to skip the clears. */
    { VKD3D_STRING_COMPARE_EXACT, "eldenring.exe",
            VKD3D_CONFIG_FLAG_INIT_STATIC(
                .MEMORY_ALLOCATOR_SKIP_CLEAR = 1, .PIPELINE_LIBRARY_IGNORE_MISMATCH_DRIVER = 1,
                .RECYCLE_COMMAND_POOLS = 1) },
    /* Serious Sam 4 (257420).
     * Invariant workarounds cause graphical glitches when rendering foliage on NV. */
    { VKD3D_STRING_COMPARE_EXACT, "Sam4.exe",
        VKD3D_CONFIG_FLAG_INIT_STATIC(.FORCE_NO_INVARIANT_POSITION = 1, .SMALL_VRAM_REBAR = 1) },
    /* Cyberpunk 2077 (1091500). For whatever reason, anti-lag is always used if it is supported (impossible to disable),
     * leading to bad performance in some cases. Currently only affects Proton-GE which ships amdxc64.dll shim by default.
     * The workaround is obsolete how however, since Mesa does not enable anti-lag by default,
     * and it will not be enabled by default until it's confirmed to be rock solid. */
    { VKD3D_STRING_COMPARE_EXACT, "Cyberpunk2077.exe", VKD3D_CONFIG_FLAG_STATIC(ALLOW_SBT_COLLECTION) },
    /* Control (870780). Control fails to detect DXR if 1.1 is exposed. */
    { VKD3D_STRING_COMPARE_EXACT, "Control_DX12.exe", VKD3D_CONFIG_FLAGS_NONE, VKD3D_CONFIG_FLAGS_NONE, VKD3D_APPLICATION_FEATURE_LIMIT_DXR_1_0 },
    /* Hellblade: Senua's Sacrifice (414340). Enables RT by default if supported which is ... jarring and particularly jarring on Deck. */
    { VKD3D_STRING_COMPARE_EXACT, "HellbladeGame-Win64-Shipping.exe", VKD3D_CONFIG_FLAGS_NONE, VKD3D_CONFIG_FLAGS_NONE, VKD3D_APPLICATION_FEATURE_NO_DEFAULT_DXR_ON_DECK_AND_FRAME },
    /* PARANOID (946920). Similar concern with DXR. Add default UE configs. Also requires non-native FP16 somehow. */
    { VKD3D_STRING_COMPARE_EXACT, "Paranoid-Win64-Shipping.exe", VKD3D_CONFIG_FLAG_INIT_STATIC(.SMALL_VRAM_REBAR = 1, .NO_STAGGERED_SUBMIT = 1), VKD3D_CONFIG_FLAGS_NONE,
            VKD3D_APPLICATION_FEATURE_NO_DEFAULT_DXR_ON_DECK_AND_FRAME },
    /* Lost Judgment (2058190) */
    { VKD3D_STRING_COMPARE_EXACT, "LostJudgment.exe", VKD3D_CONFIG_FLAG_STATIC(FORCE_INITIAL_TRANSITION) },
    /* Marvel's Spider-Man Remastered (1817070). DCC stores causes glitches when RT is enabled with RADV. */
    { VKD3D_STRING_COMPARE_EXACT, "Spider-Man.exe", VKD3D_CONFIG_FLAG_INIT_STATIC(.FORCE_INITIAL_TRANSITION = 1, .DISABLE_UAV_COMPRESSION = 1) },
    /* Marvel’s Spider-Man: Miles Morales (1817190) */
    { VKD3D_STRING_COMPARE_EXACT, "MilesMorales.exe", VKD3D_CONFIG_FLAG_STATIC(FORCE_INITIAL_TRANSITION) },
    /* Deus Ex: Mankind United (337000) */
    { VKD3D_STRING_COMPARE_EXACT, "DXMD.exe", VKD3D_CONFIG_FLAG_STATIC(FORCE_INITIAL_TRANSITION) },
    /* Dead Space (2023) (1693980) */
    { VKD3D_STRING_COMPARE_EXACT, "Dead Space.exe", VKD3D_CONFIG_FLAG_STATIC(FORCE_DEDICATED_IMAGE_ALLOCATION) },
    /* Witcher 3 (2023) (292030). Misses ALLOW_REBUILD when querying for RTAS sizes. */
    { VKD3D_STRING_COMPARE_EXACT, "witcher3.exe", VKD3D_CONFIG_FLAG_INIT_STATIC(
            .DISABLE_SIMULTANEOUS_UAV_COMPRESSION = 1, .RTAS_ALLOW_BLAS_REBUILD_SIZES = 1) },
    /* Age of Wonders 4 (1669000). Extremely stuttery performance with ReBAR. */
    { VKD3D_STRING_COMPARE_EXACT, "AOW4.exe", VKD3D_CONFIG_FLAG_STATIC(NO_UPLOAD_HVV) },
    /* Red Dead Redemption (2668510). Inconsistent performance with ReBAR at cutscenes of the game. */
    { VKD3D_STRING_COMPARE_EXACT, "RDR.exe", VKD3D_CONFIG_FLAG_STATIC(NO_UPLOAD_HVV) },
    /* Starfield (1716740) */
    { VKD3D_STRING_COMPARE_EXACT, "Starfield.exe",
            VKD3D_CONFIG_FLAG_INIT_STATIC(.HUGE_NV_DGC_BUFFERS = 1, .REJECT_PADDED_SMALL_RESOURCE_ALIGNMENT = 1) },
    /* Persona 3 Reload (2161700). Enables RT by default on Deck and does not run acceptably for a verified title. */
    { VKD3D_STRING_COMPARE_EXACT, "P3R.exe", VKD3D_CONFIG_FLAGS_NONE, VKD3D_CONFIG_FLAGS_NONE, VKD3D_APPLICATION_FEATURE_NO_DEFAULT_DXR_ON_DECK_AND_FRAME },
    /* Basically never bothers doing initial transitions.
     * GPU hang observed on RDNA1 cards at least during intro cutscene.
     * Game does not use UAV barrier between ClearUAV and GDeflate shader.
     * NVIDIA does not hit that particular hazard since it uses metacommand, but ClearUAV barrier
     * still works around sync issues. */
    { VKD3D_STRING_COMPARE_STARTS_WITH, "ffxvi", VKD3D_CONFIG_FLAG_STATIC(FORCE_INITIAL_TRANSITION) },
    /* World of Warcraft retail. Broken MSAA code where it renders to multi-sampled target with single sampled PSO. */
    /* Descriptor type mismatches causes GPU hangs in a ray query shader without 64 byte descriptors */
    { VKD3D_STRING_COMPARE_EXACT, "Wow.exe", VKD3D_CONFIG_FLAG_INIT_STATIC(.FORCE_DYNAMIC_MSAA = 1, .AVOID_IMAGE_BUFFER_ALIASING = 1, .DESCRIPTOR_HEAP = 1) },
    /* The Last of Us Part I (1888930). Submits hundreds of command buffers per frame.
     * Some of the lighting shaders are extremely sensitive to tiling layouts, and using thin tiling for 3D UAVs has profound
     * performance effects. */
    { VKD3D_STRING_COMPARE_STARTS_WITH, "tlou-i",
            VKD3D_CONFIG_FLAG_INIT_STATIC(.NO_STAGGERED_SUBMIT = 1, .PREFER_THIN_UAV_TILING = 1) },
    /* Skull and Bones (2853730). Seems to require unsupported dcomp when reflex is enabled for some reason *shrug */
    { VKD3D_STRING_COMPARE_EXACT, "skullandbones.exe", VKD3D_CONFIG_FLAGS_NONE, VKD3D_CONFIG_FLAGS_NONE, VKD3D_APPLICATION_FEATURE_DISABLE_NV_REFLEX },
    /* Star Wars Outlaws (2842040). Attempt to workaround a possible NV driver bug. */
    { VKD3D_STRING_COMPARE_EXACT, "Outlaws.exe", VKD3D_CONFIG_FLAG_STATIC(ONE_TIME_SUBMIT) },
    { VKD3D_STRING_COMPARE_EXACT, "Outlaws_Plus.exe", VKD3D_CONFIG_FLAG_STATIC(ONE_TIME_SUBMIT) },
    /* FFVII Rebirth (2909400).
     * Game can destroy PSOs while they are in-flight.
     * Also, add no-staggered since this is a UE title without the common workaround,
     * although that only seems to matter when FSR/DLSS injectors are used. */
    { VKD3D_STRING_COMPARE_EXACT, "ff7rebirth_.exe",
            VKD3D_CONFIG_FLAG_INIT_STATIC(.RETAIN_PSOS = 1, .NO_STAGGERED_SUBMIT = 1), VKD3D_CONFIG_FLAGS_NONE,
            VKD3D_APPLICATION_FEATURE_MESH_SHADER_WITHOUT_BARYCENTRICS },
    /* REANIMAL (2129530). Game can destroy PSOs while they are in-flight on loading screen.
     * Smells very similar to FFVII Rebirth.
     * It also has bugs with FSR3 being destroyed while in flight (but that case is automatically covered already). */
    { VKD3D_STRING_COMPARE_EXACT, "REANIMAL.exe",
            VKD3D_CONFIG_FLAG_INIT_STATIC(.RETAIN_PSOS = 1, .NO_STAGGERED_SUBMIT = 1) },
    /* There aren't many games that use mesh shaders outside of UE5 Nanite fallbacks.
     * UE5 is broken w.r.t. feature checks, so we have to do opt-in instead :( */
    { VKD3D_STRING_COMPARE_EXACT, "AlanWake2.exe", VKD3D_CONFIG_FLAGS_NONE, VKD3D_CONFIG_FLAGS_NONE, VKD3D_APPLICATION_FEATURE_MESH_SHADER_WITHOUT_BARYCENTRICS },
    /* Monster Hunter Wilds (2246340).
     * There is an impossible amdgpu bug with PRT sparse.
     * No upload HVV as a performance opt since it's very CPU intensive, and there's no obvious GPU uplift from this. */
    { VKD3D_STRING_COMPARE_EXACT, "MonsterHunterWilds.exe",
        VKD3D_CONFIG_FLAG_INIT_STATIC(.SKIP_NULL_SPARSE_TILES = 1, .NO_UPLOAD_HVV = 1) },
    /* Wreckfest 2 (1203190). Aliases block-compressed textures with color images on the
     * same heap and expects image data to be interpreted consistently. */
    { VKD3D_STRING_COMPARE_EXACT, "Wreckfest2.exe", VKD3D_CONFIG_FLAG_STATIC(PLACED_TEXTURE_ALIASING) },
    /* Eve online. Uses DGC with CBV updates. Kinda questionable exename ... */
    { VKD3D_STRING_COMPARE_EXACT, "exefile.exe", VKD3D_CONFIG_FLAG_STATIC(FORCE_RAW_VA_CBV) },
    /* Unreal Engine catch-all. ReBAR is a massive uplift on RX 7600 for example in Wukong.
     * AMD windows drivers also seem to have some kind of general app-opt for UE titles.
     * Use no-staggered-submit by default on UE. We've only observed issues in Wukong here, but
     * unless we see proof that UE titles want staggered,
     * we'll disable for now to be defensive and de-risk any large scale regressions. */
    { VKD3D_STRING_COMPARE_ENDS_WITH, "-Win64-Shipping.exe",
            VKD3D_CONFIG_FLAG_INIT_STATIC(.SMALL_VRAM_REBAR = 1, .NO_STAGGERED_SUBMIT = 1) },
    /* Borderlands 4. Also UE, but uses different name. */
    { VKD3D_STRING_COMPARE_EXACT, "Borderlands4.exe",
            VKD3D_CONFIG_FLAG_INIT_STATIC(.SMALL_VRAM_REBAR = 1, .NO_STAGGERED_SUBMIT = 1) },
    /* New halo is UE, but doesn't use standard UE naming. */
    { VKD3D_STRING_COMPARE_EXACT, "HaloCampaignEvolved.exe",
            VKD3D_CONFIG_FLAG_INIT_STATIC(.SMALL_VRAM_REBAR = 1, .NO_STAGGERED_SUBMIT = 1) },
    /* Rise of the Tomb Raider. Game renders and samples a texture at the same time */
    { VKD3D_STRING_COMPARE_EXACT, "ROTTR.exe", VKD3D_CONFIG_FLAG_STATIC(DISABLE_COLOR_COMPRESSION) },
    /* Death Stranding (Director's Cut and original). Massive CPU overhead due to reading from HVV in certain scenarios. */
    /* EGS alias as well. */
    { VKD3D_STRING_COMPARE_EXACT, "ds.exe", VKD3D_CONFIG_FLAG_STATIC(NO_UPLOAD_HVV) },
    { VKD3D_STRING_COMPARE_EXACT, "DeathStranding.exe", VKD3D_CONFIG_FLAG_STATIC(NO_UPLOAD_HVV) },
    /* AC: Valhalla (2208920). Very ugly use-after-free in some cases. The main culprit seems a sparse resource. */
    { VKD3D_STRING_COMPARE_EXACT, "ACValhalla.exe", VKD3D_CONFIG_FLAG_STATIC(DEFER_RESOURCE_DESTRUCTION) },
    /* Guardians of the Galaxy: Tries to use root descriptors with indirect rendering if it detects an Nvidia GPU. */
    { VKD3D_STRING_COMPARE_EXACT, "gotg.exe", VKD3D_CONFIG_FLAG_STATIC(FORCE_RAW_VA_CBV) },
    /* Crimson Desert (3321460).
     * Game advertises being able to run on RDNA1, but when we don't expose some RDNA2+ features,
     * it just exits on startup. It seems to rely on unstable barycentrics, which we can implement on older AMD,
     * and VRS can be nooped. Recent game update has broken raw buffer <-> image aliasing. */
    { VKD3D_STRING_COMPARE_EXACT, "CrimsonDesert.exe", VKD3D_CONFIG_FLAG_STATIC(AVOID_IMAGE_BUFFER_ALIASING), VKD3D_CONFIG_FLAGS_NONE,
        VKD3D_APPLICATION_FEATURE_RDNA1_COMPATIBILITY },
    /* Ark Ascended (2399830). Very broken FSR3 usage where the entire thing is freed. We can retain the resources automatically
     * but descriptor heap is not named, so we cannot auto-detect. Similar story for the PSOs. */
    { VKD3D_STRING_COMPARE_EXACT, "ArkAscended.exe",
        VKD3D_CONFIG_FLAG_INIT_STATIC(.RETAIN_PSOS = 1) },
    /* Forza Horizon 6 (2483190).
     * Completely broken case where it writes a texture descriptor and reads it as a buffer.
     * With 32b embedded model on RDNA3/4, this causes a GPU hang.
     * Lots of jank is needed to make this work:
     * Co-siting buffers and images was attempted, but we ran into HW bugs.
     * The only reasonable solution is to completely firewall images and raw buffers from each other by
     * forcing 64b descriptors on heap or rely on 64b drirc workaround in RADV for DB path. */
    { VKD3D_STRING_COMPARE_EXACT, "forzahorizon6.exe", VKD3D_CONFIG_FLAG_INIT_STATIC(
        .AVOID_IMAGE_BUFFER_ALIASING = 1, .NO_STAGGERED_SUBMIT = 1) },
    /* SCP: Secret Laboratory (700330). DCC stores causes glitches with RADV. */
    { VKD3D_STRING_COMPARE_EXACT, "SCPSL.exe", VKD3D_CONFIG_FLAG_INIT_STATIC(.DISABLE_UAV_COMPRESSION = 1) },
    /* PRAGMATA (3357650) */
    { VKD3D_STRING_COMPARE_STARTS_WITH, "PRAGMATA", VKD3D_CONFIG_FLAGS_NONE, VKD3D_CONFIG_FLAGS_NONE,
        VKD3D_APPLICATION_FEATURE_ASSUMES_STRICT_BYTE_ADDRESS_WRAP },
    { VKD3D_STRING_COMPARE_EXACT, "GoWEDay-Steam.exe", VKD3D_CONFIG_FLAG_INIT_STATIC(.NO_STAGGERED_SUBMIT = 1) },
    /* Assetto Corsa EVO (3058630) */
    { VKD3D_STRING_COMPARE_EXACT, "AssettoCorsaEVO.exe", VKD3D_CONFIG_FLAG_INIT_STATIC(.FORCE_RAW_VA_CBV = 1) },
    /* World of Warcraft Classic */
    /* Like in retail WoW, descriptor type mismatches causes GPU hangs in a ray query shader without 64 byte descriptors */
    { VKD3D_STRING_COMPARE_EXACT, "WoWClassic.exe", VKD3D_CONFIG_FLAG_INIT_STATIC(.AVOID_IMAGE_BUFFER_ALIASING = 1, .DESCRIPTOR_HEAP = 1) },
    /* Teardown.
     * Creates command signatures that modify vertex/index buffer views and root CBVs, which
     * cannot be implemented without device generated commands. Silently dropping the state
     * template makes those ExecuteIndirect draws disappear, and the game handles a failed
     * CreateCommandSignature by issuing the draws from the CPU instead. */
    { VKD3D_STRING_COMPARE_EXACT, "teardown.exe", VKD3D_CONFIG_FLAG_INIT_STATIC(.FAIL_UNSUPPORTED_STATE_TEMPLATE = 1) },
    { VKD3D_STRING_COMPARE_NEVER, NULL },
};

struct vkd3d_shader_quirk_meta
{
    enum vkd3d_string_compare_mode mode;
    const char *name;
    const struct vkd3d_shader_quirk_info *info;
};

static const struct vkd3d_shader_quirk_hash ue4_hashes[] = {
    { NULL, 0x08a323ee81c1e393ull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
    { NULL, 0x75dcbd76ee898815ull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
    { NULL, 0x6c37b5a66059b751ull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
    { NULL, 0xaf6d07d7b56a3effull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
    { NULL, 0xa48ead2a618e12d8ull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
    { NULL, 0xebfd864995d3fc07ull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
    { NULL, 0xcca7b582db60199cull, VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW },
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
    { NULL, 0xbf0af7db6a7fb86bull, VKD3D_SHADER_QUIRK_FORCE_NOCONTRACT_MATH },
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
    { NULL, 0xa100b53736f9c1bfull, VKD3D_SHADER_QUIRK_FORCE_SUBGROUP_SIZE_1 },
    /* RE2 and RE7 */
    { NULL, 0x1c4c8782b75c498bull, VKD3D_SHADER_QUIRK_FORCE_SUBGROUP_SIZE_1 },
    /* Temporary driver workaround for RADV. See https://gitlab.freedesktop.org/mesa/mesa/-/issues/9852. */
    /* This shader trips on Mesa 23.0.3. */
    { NULL, 0xdb1593ced60da3f1ull, VKD3D_SHADER_QUIRK_REWRITE_GRAD_TO_BIAS },
    /* This shader hangs on Mesa main. */
    { NULL, 0x5784e9e2f7a76819ull, VKD3D_SHADER_QUIRK_REWRITE_GRAD_TO_BIAS },
    /* This shader hangs on RDNA1 */
    { NULL, 0x7b3cec4ba6d32cacull, VKD3D_SHADER_QUIRK_REWRITE_GRAD_TO_BIAS },
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
    { NULL, 0xd892f8024f52d3ca, VKD3D_SHADER_QUIRK_FORCE_NOCONTRACT_MATH },
};

static const struct vkd3d_shader_quirk_info mhr_quirks = {
    mhr_hashes, ARRAY_SIZE(mhr_hashes), 0,
};

static const struct vkd3d_shader_quirk_hash witcher3_hashes[] = {
    /* In DXR path, the game will write VBO data in a CS which is then followed
     * by a VS -> tess -> geom pass that writes out data to a UAV in the GS.
     * There appears to be missing synchronization here by game (no UAV -> VBO barrier) and
     * forcing barriers fixes a ton of glitches on both NV and RADV. */
    { NULL, 0x2c16686e5d9b04a8, VKD3D_SHADER_QUIRK_FORCE_COMPUTE_BARRIER },
};

static const struct vkd3d_shader_quirk_info witcher3_quirks = {
    witcher3_hashes, ARRAY_SIZE(witcher3_hashes), 0,
};

static const struct vkd3d_shader_quirk_info heap_robustness_quirks = {
    NULL, 0, VKD3D_SHADER_QUIRK_DESCRIPTOR_HEAP_ROBUSTNESS,
};

static const struct vkd3d_shader_quirk_info forza6_quirks = {
    NULL, 0,
    /* Tons of OOB access in RT, even for sampler heap.
     * Also, lots of missed nonuniformEXT in RT, so force that ... */
    VKD3D_SHADER_QUIRK_DESCRIPTOR_HEAP_ROBUSTNESS | VKD3D_SHADER_QUIRK_FORCE_NONUNIFORM_RT,
};

static const struct vkd3d_shader_quirk_hash ac_mirage_hashes[] = {
    /* There is a write-after-read hazard.
     * Index buffer is being read from, and there is a compute shader afterwards
     * that writes to that index buffer without a barrier. */
    { NULL, 0x0cb130fa374982e3, VKD3D_SHADER_QUIRK_FORCE_PRE_RASTERIZATION_BARRIER },
};

static const struct vkd3d_shader_quirk_info ac_mirage_quirks = {
    ac_mirage_hashes, ARRAY_SIZE(ac_mirage_hashes), 0,
};

static const struct vkd3d_shader_quirk_hash ffxvi_hashes[] = {
    /* On RADV 24.1.6 RDNA3, we seem to be plagued with a compiler bug/hardware quirk.
     * It works on main, but only by chance.
     * https://gitlab.freedesktop.org/mesa/mesa/-/issues/11738. */
    { NULL, 0xa98606e01cdd5924, VKD3D_SHADER_QUIRK_DISABLE_OPTIMIZATIONS },
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
    { NULL, 0x74b8eaf23e3d166c, VKD3D_SHADER_QUIRK_ASSUME_BROKEN_SUB_8x8_CUBE_MIPS },
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
    { NULL, 0xe6cb9c843fa1bd18, VKD3D_SHADER_QUIRK_FORCE_PRE_COMPUTE_BARRIER },
    /* 1.003 update. Hash changed, but didn't fix the bug. */
    { NULL, 0xf047c7f2f4f32111, VKD3D_SHADER_QUIRK_FORCE_PRE_COMPUTE_BARRIER },
};

static const struct vkd3d_shader_quirk_info rebirth_quirks = {
    rebirth_hashes, ARRAY_SIZE(rebirth_hashes), 0,
};

/* Game misses a transition from color to resource before FSR3.
 * The shader hash is FSR3-PREPARE-INPUTS. */
static const struct vkd3d_shader_quirk_hash satisfactory_hashes[] = {
    { NULL, 0x1bc3c90cfe16ad1e, VKD3D_SHADER_QUIRK_FORCE_GRAPHICS_BARRIER },
};

static const struct vkd3d_shader_quirk_info satisfactory_quirks = {
    satisfactory_hashes, ARRAY_SIZE(satisfactory_hashes), 0,
};

static const struct vkd3d_shader_quirk_hash deadspace_hashes[] = {
    /* Shader calculates derivatives in non-uniform control flow,
     * leading to NaN pixels on Nvidia GPUs. */
    { NULL, 0x8b981fdafe14b649, VKD3D_SHADER_QUIRK_HOIST_DERIVATIVES },
};

static const struct vkd3d_shader_quirk_info deadspace_quirks = {
    deadspace_hashes, ARRAY_SIZE(deadspace_hashes), 0,
};

static const struct vkd3d_shader_quirk_hash death_stranding_hashes[] = {
    /* Game forgets to transition RENDER_TARGET to PIXEL_SHADER_RESOURCE. */
    { NULL, 0x014fa51aaa3f3139, VKD3D_SHADER_QUIRK_FORCE_GRAPHICS_BARRIER_BEFORE_RENDER_PASS },
};

static const struct vkd3d_shader_quirk_info death_stranding_quirks = {
    death_stranding_hashes, ARRAY_SIZE(death_stranding_hashes), 0,
};

static const struct vkd3d_shader_quirk_hash wuthering_waves_hashes[] = {
    /* LightGridInjectionCS. Forgets to UAV barrier after ClearCS. */
    { "LightGridInjectionCS", 0, VKD3D_SHADER_QUIRK_FORCE_PRE_COMPUTE_BARRIER },
};

static const struct vkd3d_shader_quirk_info wuthering_waves_quirks = {
    wuthering_waves_hashes, ARRAY_SIZE(wuthering_waves_hashes), 0,
};

static const struct vkd3d_shader_quirk_info dune_quirks = {
    NULL, 0, VKD3D_SHADER_QUIRK_FIXUP_LOOP_HEADER_UNDEF_PHIS,
};

static const struct vkd3d_shader_quirk_hash bl4_hashes[] = {
    /* See Mesa issue 13981. Impossible looking HW bug on RDNA2 specifically
     * caused by NSA image_sample_d. */
    { NULL, 0x3b9937c41027ca73, VKD3D_SHADER_QUIRK_DISABLE_OPTIMIZATIONS },
    { NULL, 0x0bf58981278d2126, VKD3D_SHADER_QUIRK_DISABLE_OPTIMIZATIONS },
};

static const struct vkd3d_shader_quirk_info bl4_quirks = {
    bl4_hashes, ARRAY_SIZE(bl4_hashes), 0,
};

static const struct vkd3d_shader_quirk_hash control_hashes[] = {
    /* A closest hit shader is doing x / sqrt(dot(x, x)) where X is 0.
     * It's fetching positions from a buffer from SBT root descriptors, so
     * this doesn't 100% prove a game bug, but it's overwhelmingly likely. */
    { NULL, 0xdb22fce4505969f2, VKD3D_SHADER_QUIRK_FIXUP_RSQRT_INF_NAN },
};

static const struct vkd3d_shader_quirk_info control_quirks = {
    control_hashes, ARRAY_SIZE(control_hashes), 0,
};

static const struct vkd3d_shader_quirk_hash rottr_hashes[] = {
    /* Game forgets to transition depth to pixel shader resource. */
    { NULL, 0x7d1af7d0c7d63856, VKD3D_SHADER_QUIRK_FORCE_GRAPHICS_BARRIER_BEFORE_RENDER_PASS },
};

static const struct vkd3d_shader_quirk_info rottr_quirks = {
    rottr_hashes, ARRAY_SIZE(rottr_hashes), 0,
};

static const struct vkd3d_shader_quirk_hash hfw_hashes[] = {
    /* Classic case of a clear CS that is followed up by overwriting it without proper barrier. */
    { NULL, 0x548a3de5dc3828ef, VKD3D_SHADER_QUIRK_FORCE_COMPUTE_BARRIER },
};

static const struct vkd3d_shader_quirk_info hfw_quirks = {
    hfw_hashes, ARRAY_SIZE(hfw_hashes), 0,
};

/* Misses some sync with depth in SSDM_FillHolesDepthDisplacementPS */
static const struct vkd3d_shader_quirk_hash crimson_desert_hashes[] = {
    { "SSDM_FillHolesDepthDisplacementPS", 0, VKD3D_SHADER_QUIRK_FORCE_GRAPHICS_BARRIER_BEFORE_RENDER_PASS },
};

static const struct vkd3d_shader_quirk_info crimson_desert_quirks = {
    crimson_desert_hashes, ARRAY_SIZE(crimson_desert_hashes), VKD3D_SHADER_QUIRK_ROBUST_COMPUTE_QUAD_BROADCAST,
};

/* Some vertex shaders use precise fma, while others don't.
 * Just forcing fma even for precise works around it and invariant gl_Position
 * takes care of the rest.
 * Works around some depth invariance issues when rendering the baby's eyeballs
 * in intro cutscenes. */
static const struct vkd3d_shader_quirk_info ds2_quirks = {
    NULL, 0, VKD3D_SHADER_QUIRK_PRECISE_FMA,
};

static const struct vkd3d_shader_quirk_hash spiderman2_hashes[] = {
    { NULL, 0x324071d329f05ccc, VKD3D_SHADER_QUIRK_FORCE_COMPUTE_BARRIER },
};

static const struct vkd3d_shader_quirk_info spiderman2_quirks = {
    spiderman2_hashes, ARRAY_SIZE(spiderman2_hashes), 0,
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

static const struct vkd3d_shader_quirk_hash wow_hashes[] = {
    /* In addition to needing 64 byte descriptors, this ray query shader causes a heap OOB as well. */
    { NULL, 0xef2f620cbce5630c, VKD3D_SHADER_QUIRK_DESCRIPTOR_HEAP_ROBUSTNESS },
};

static const struct vkd3d_shader_quirk_info wow_quirks = {
    wow_hashes, ARRAY_SIZE(wow_hashes), 0,
};

/* Misses a required RTV -> PIXEL_SHADER_RESOURCE barrier.
 * The shader is called Opaque_PS and is used all over the place,
 * so need to narrow it down to hashes.
 * The draw can sometimes appear in the middle of a render pass, so need a harder barrier.
 */
static const struct vkd3d_shader_quirk_hash gotg_hashes[] = {
    { NULL, 0x1f29288d6150a04e, VKD3D_SHADER_QUIRK_FORCE_GRAPHICS_BARRIER_BEFORE_DRAW },
};

static const struct vkd3d_shader_quirk_info gotg_quirks = {
    gotg_hashes, ARRAY_SIZE(gotg_hashes), 0,
};

static const struct vkd3d_shader_quirk_hash sottr_hashes[] = {
    { NULL, 0x8227f72cbc13591d, VKD3D_SHADER_QUIRK_FORCE_FEEDBACK_LOOP },
};

static const struct vkd3d_shader_quirk_info sottr_quirks = {
    sottr_hashes, ARRAY_SIZE(sottr_hashes), 0,
};

static const struct vkd3d_shader_quirk_hash wow_classic_hashes[] = {
    /* The same ray query shader from retail WoW causes a heap OOB here as well. */
    { NULL, 0xef2f620cbce5630c, VKD3D_SHADER_QUIRK_DESCRIPTOR_HEAP_ROBUSTNESS },
};

static const struct vkd3d_shader_quirk_info wow_classic_quirks = {
    wow_classic_hashes, ARRAY_SIZE(wow_classic_hashes), 0,
};

static const struct vkd3d_shader_quirk_info empire_of_the_ants_quirks = {
    NULL, 0, VKD3D_SHADER_QUIRK_CLAMP_WAVE_SIZE_TO_THREAD_GROUP32,
};

static const struct vkd3d_shader_quirk_hash first_light_hashes[] = {
    { "MeshCalcDest_CS", 0, VKD3D_SHADER_QUIRK_CLAMP_WAVE_SIZE_TO_THREAD_GROUP32 },
    { "MeshBinning_CS", 0, VKD3D_SHADER_QUIRK_CLAMP_WAVE_SIZE_TO_THREAD_GROUP32 },
    { "MeshCount_CS", 0, VKD3D_SHADER_QUIRK_CLAMP_WAVE_SIZE_TO_THREAD_GROUP32 },
    { "MeshProcessGG_CS", 0, VKD3D_SHADER_QUIRK_CLAMP_WAVE_SIZE_TO_THREAD_GROUP32 },
};

static const struct vkd3d_shader_quirk_info first_light_quirks = {
    first_light_hashes, ARRAY_SIZE(first_light_hashes), 0,
};

static const struct vkd3d_shader_quirk_hash plague_tale_resonance_hashes[] = {
    { "ch_spawn", 0, VKD3D_SHADER_QUIRK_DESCRIPTOR_HEAP_ROBUSTNESS },
    { "ch_update", 0, VKD3D_SHADER_QUIRK_DESCRIPTOR_HEAP_ROBUSTNESS },
};

static const struct vkd3d_shader_quirk_info plague_tale_resonance_robustness_quirks = {
    plague_tale_resonance_hashes, ARRAY_SIZE(plague_tale_resonance_hashes), 0,
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
    /* Wuthering Waves */
    { VKD3D_STRING_COMPARE_EXACT, "Client-Win64-Shipping.exe", &wuthering_waves_quirks },
    /* Dead Space (2023) */
    { VKD3D_STRING_COMPARE_ENDS_WITH, "Dead Space.exe", &deadspace_quirks },
    /* Death Stranding  */
    { VKD3D_STRING_COMPARE_EXACT, "ds.exe", &death_stranding_quirks },
    { VKD3D_STRING_COMPARE_EXACT, "DeathStranding.exe", &death_stranding_quirks },
    { VKD3D_STRING_COMPARE_EXACT, "3DMarkPortRoyal.exe", &heap_robustness_quirks },
    /* Dune: Awakening (1172710) */
    { VKD3D_STRING_COMPARE_STARTS_WITH, "DuneSandbox", &dune_quirks },
    /* Borderlands 4 (1285190) */
    { VKD3D_STRING_COMPARE_EXACT, "Borderlands4.exe", &bl4_quirks },
    /* Control (870780). */
    { VKD3D_STRING_COMPARE_EXACT, "Control_DX12.exe", &control_quirks },
	/* Rise of the Tomb Raider */
    { VKD3D_STRING_COMPARE_EXACT, "ROTTR.exe", &rottr_quirks },
    /* Horizon Forbidden West (2420110). */
    { VKD3D_STRING_COMPARE_EXACT, "HorizonForbiddenWest.exe", &hfw_quirks },
    /* Crimson Desert (3321460) */
    { VKD3D_STRING_COMPARE_EXACT, "CrimsonDesert.exe", &crimson_desert_quirks },
    /* Death Stranding 2 (3280350) */
    { VKD3D_STRING_COMPARE_EXACT, "DS2.exe", &ds2_quirks },
	/* Spider Man 2 (2651280) */
    { VKD3D_STRING_COMPARE_EXACT, "Spider-Man2.exe", &spiderman2_quirks },
    /* PRAGMATA (3357650) */
    { VKD3D_STRING_COMPARE_STARTS_WITH, "PRAGMATA", &re_engine_quirks },
    /* Forza Horizon 6 (2483190). */
    { VKD3D_STRING_COMPARE_EXACT, "forzahorizon6.exe", &forza6_quirks },
    /* World of Warcraft */
    { VKD3D_STRING_COMPARE_EXACT, "Wow.exe", &wow_quirks },
    /* Guardians of the Galaxy */
    { VKD3D_STRING_COMPARE_EXACT, "gotg.exe", &gotg_quirks },
    /* Shadow of the Tomb Raider */
    { VKD3D_STRING_COMPARE_EXACT, "SOTTR.exe", &sottr_quirks },
    /* MSVC fails to compile empty array. */
    /* World of Warcraft Classic */
    { VKD3D_STRING_COMPARE_EXACT, "WoWClassic.exe", &wow_classic_quirks },
    /* Plague Tale Resonance (2713000). */
    { VKD3D_STRING_COMPARE_EXACT, "Resonance.exe", &plague_tale_resonance_robustness_quirks },
    /* Empire of the Ants (2287330). With Terrain Tessellation on, some shaders
     * seem to assume Wave32 by mistake leading to GPU timeout. */
    { VKD3D_STRING_COMPARE_EXACT, "Empire-Win64-Shipping.exe", &empire_of_the_ants_quirks },
    /* 007: First Light (3768760) */
    { VKD3D_STRING_COMPARE_EXACT, "007FirstLight.exe", &first_light_quirks },
    /* Resident Evil Requiem (3764200) */
    { VKD3D_STRING_COMPARE_EXACT, "re9.exe", &re_engine_quirks },
    /* Monster Hunter Stories 3 (2852190) */
    { VKD3D_STRING_COMPARE_EXACT, "MONSTER_HUNTER_STORIES_3_TWISTED_REFLECTION.exe", &re_engine_quirks },
    /* Onimusha: Way of the Sword DEMO (3974650) */
    { VKD3D_STRING_COMPARE_EXACT, "OnimushaWotS_Demo.exe", &re_engine_quirks },
    /* Onimusha: Way of the Sword (2638890) */
    { VKD3D_STRING_COMPARE_EXACT, "OnimushaWotS.exe", &re_engine_quirks },
    /* Dragon's Dogma 2 (2054970) */
    { VKD3D_STRING_COMPARE_EXACT, "DD2.exe", &re_engine_quirks },
    /* Unreal Engine 4 */
    { VKD3D_STRING_COMPARE_ENDS_WITH, "-Shipping.exe", &ue4_quirks },
    { VKD3D_STRING_COMPARE_NEVER, NULL, NULL },
};

void vkd3d_instance_apply_application_workarounds(void)
{
    uint32_t ue_major = 0, ue_minor = 0, ue_patch = 0;
    char app[VKD3D_PATH_MAX];
    bool is_unreal = false;
    size_t i;

    if (!vkd3d_get_program_name(app))
        return;

    if (vkd3d_get_ue_version(&ue_major, &ue_minor, &ue_patch))
    {
        is_unreal = true;
        INFO("Detected Unreal Engine %u.%u.%u.\n", ue_major, ue_minor, ue_patch);
    }

    /* If we don't have any application specific patterns we hit, engage default engine workarounds. */

    for (i = 0; i < ARRAY_SIZE(application_override); i++)
    {
        if (vkd3d_string_compare(application_override[i].mode, app, application_override[i].name))
        {
            vkd3d_config_flag_global_add(application_override[i].global_flags_add);
            vkd3d_config_flag_global_remove(application_override[i].global_flags_remove);
            INFO("Detected game %s, adding %u configs, removing %u configs.\n",
                 app, vkd3d_config_flag_popcount(application_override[i].global_flags_add),
                 vkd3d_config_flag_popcount(application_override[i].global_flags_remove));
            vkd3d_application_feature_override = application_override[i].override;
            break;
        }
    }

    if (i == ARRAY_SIZE(application_override))
    {
        if (is_unreal)
        {
            INFO("Applying default Unreal Engine workarounds.\n");
            vkd3d_config_flag_global_add(VKD3D_CONFIG_FLAG_INIT(.SMALL_VRAM_REBAR = 1, .NO_STAGGERED_SUBMIT = 1));
        }
    }

    for (i = 0; i < ARRAY_SIZE(application_shader_quirks); i++)
    {
        if (vkd3d_string_compare(application_shader_quirks[i].mode, app, application_shader_quirks[i].name))
        {
            vkd3d_shader_quirk_info_template = *application_shader_quirks[i].info;
            INFO("Detected game %s, adding shader quirks for specific shaders.\n", app);
            break;
        }
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

    /* RADV_THREAD_TRACE_xxx are deprecated and will be removed at some point. */
    if (vkd3d_get_env_var("RADV_THREAD_TRACE", env, sizeof(env)) ||
            vkd3d_get_env_var("RADV_THREAD_TRACE_TRIGGER", env, sizeof(env)) ||
            (vkd3d_get_env_var("MESA_VK_TRACE", env, sizeof(env)) &&
                strcmp(env, "rgp") == 0))
    {
        INFO("RADV thread trace is enabled. Forcing debug utils to be enabled for labels.\n");
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
        uint32_t quirk;
        bool negative;
    };

    static const struct override overrides[] =
    {
        { VKD3D_CONFIG_FLAG_STATIC(FORCE_NO_INVARIANT_POSITION), VKD3D_SHADER_QUIRK_INVARIANT_POSITION, true },
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
            uint32_t quirk;
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

        if (!vkd3d_debug_control_is_test_suite() &&
            info->vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY)
        {
            /* Float controls2 is broken where we need it to work. Keep using bad QuantizeToFP16 path
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
