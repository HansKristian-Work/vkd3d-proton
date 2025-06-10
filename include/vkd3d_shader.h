/*
 * Copyright 2017-2019 Józef Kucia for CodeWeavers
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

#ifndef __VKD3D_SHADER_H
#define __VKD3D_SHADER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <hashmap.h>
#include <vkd3d_types.h>
#include <vkd3d_d3d12.h>
#include <vkd3d.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

enum vkd3d_shader_compiler_option
{
    VKD3D_SHADER_STRIP_DEBUG = 0x00000001,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_COMPILER_OPTION),
};

enum vkd3d_shader_visibility
{
    VKD3D_SHADER_VISIBILITY_ALL = 0,
    VKD3D_SHADER_VISIBILITY_VERTEX = 1,
    VKD3D_SHADER_VISIBILITY_HULL = 2,
    VKD3D_SHADER_VISIBILITY_DOMAIN = 3,
    VKD3D_SHADER_VISIBILITY_GEOMETRY = 4,
    VKD3D_SHADER_VISIBILITY_PIXEL = 5,
    VKD3D_SHADER_VISIBILITY_AMPLIFICATION = 6,
    VKD3D_SHADER_VISIBILITY_MESH = 7,

    VKD3D_SHADER_VISIBILITY_COMPUTE = 1000000000,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_VISIBILITY),
};

typedef uint64_t vkd3d_shader_hash_t;

enum vkd3d_shader_meta_flags
{
    VKD3D_SHADER_META_FLAG_REPLACED = 1 << 0,
    VKD3D_SHADER_META_FLAG_USES_SUBGROUP_OPERATIONS = 1 << 1,
    VKD3D_SHADER_META_FLAG_USES_NATIVE_16BIT_OPERATIONS = 1 << 2,
    VKD3D_SHADER_META_FLAG_USES_FP64 = 1 << 3,
    VKD3D_SHADER_META_FLAG_USES_INT64 = 1 << 4,
    VKD3D_SHADER_META_FLAG_USES_STENCIL_EXPORT = 1 << 5,
    VKD3D_SHADER_META_FLAG_USES_FRAGMENT_FULLY_COVERED = 1 << 6,
    VKD3D_SHADER_META_FLAG_USES_SHADER_VIEWPORT_INDEX_LAYER = 1 << 7,
    VKD3D_SHADER_META_FLAG_USES_SPARSE_RESIDENCY = 1 << 8,
    VKD3D_SHADER_META_FLAG_USES_INT64_ATOMICS = 1 << 9,
    VKD3D_SHADER_META_FLAG_USES_INT64_ATOMICS_IMAGE = 1 << 10,
    VKD3D_SHADER_META_FLAG_USES_FRAGMENT_BARYCENTRIC = 1 << 11,
    VKD3D_SHADER_META_FLAG_USES_SAMPLE_RATE_SHADING = 1 << 12,
    VKD3D_SHADER_META_FLAG_USES_RASTERIZER_ORDERED_VIEWS = 1 << 13,
    VKD3D_SHADER_META_FLAG_EMITS_LINES = 1 << 14,
    VKD3D_SHADER_META_FLAG_EMITS_TRIANGLES = 1 << 15,
    VKD3D_SHADER_META_FLAG_FORCE_COMPUTE_BARRIER_AFTER_DISPATCH = 1 << 16,
    VKD3D_SHADER_META_FLAG_EXPORTS_SAMPLE_MASK = 1 << 17,
    VKD3D_SHADER_META_FLAG_FORCE_PRE_RASTERIZATION_BEFORE_DISPATCH = 1 << 18,
    VKD3D_SHADER_META_FLAG_FORCE_GRAPHICS_BEFORE_DISPATCH = 1 << 19,
    VKD3D_SHADER_META_FLAG_FORCE_COMPUTE_BARRIER_BEFORE_DISPATCH = 1 << 20,
    VKD3D_SHADER_META_FLAG_USES_DEPTH_STENCIL_WRITE = 1 << 21,
    VKD3D_SHADER_META_FLAG_DISABLE_OPTIMIZATIONS = 1 << 22,
    VKD3D_SHADER_META_FLAG_POINT_MODE_TESSELLATION = 1 << 23,
    VKD3D_SHADER_META_FLAG_USES_COOPERATIVE_MATRIX = 1 << 24,
    VKD3D_SHADER_META_FLAG_USES_COOPERATIVE_MATRIX_FP8 = 1 << 25,
};

struct vkd3d_shader_meta
{
    vkd3d_shader_hash_t hash;
    unsigned int cs_workgroup_size[3]; /* Only contains valid data if uses_subgroup_size is true. */
    unsigned int patch_vertex_count; /* Relevant for HS. May be 0, in which case the patch vertex count is not known. */
    uint8_t cs_wave_size_min; /* If non-zero, minimum or required subgroup size. */
    uint8_t cs_wave_size_max; /* If non-zero, maximum subgroup size. */
    uint8_t cs_wave_size_preferred; /* If non-zero, preferred subgroup size. */
    uint8_t gs_input_topology; /* VkPrimitiveTopology */
    uint32_t flags; /* vkd3d_shader_meta_flags */
};
STATIC_ASSERT(sizeof(struct vkd3d_shader_meta) == 32);

struct vkd3d_shader_code
{
    const void *code;
    size_t size;
    struct vkd3d_shader_meta meta;
};

struct vkd3d_shader_code_debug
{
    const char *debug_entry_point_name;
};

/* Scans OpCapabilities. */
void vkd3d_shader_extract_feature_meta(struct vkd3d_shader_code *code);

vkd3d_shader_hash_t vkd3d_shader_hash(const struct vkd3d_shader_code *shader);

enum vkd3d_shader_descriptor_type
{
    VKD3D_SHADER_DESCRIPTOR_TYPE_UNKNOWN,
    VKD3D_SHADER_DESCRIPTOR_TYPE_CBV,     /* cb# */
    VKD3D_SHADER_DESCRIPTOR_TYPE_SRV,     /* t#  */
    VKD3D_SHADER_DESCRIPTOR_TYPE_UAV,     /* u#  */
    VKD3D_SHADER_DESCRIPTOR_TYPE_SAMPLER, /* s#  */

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_DESCRIPTOR_TYPE),
};

struct vkd3d_shader_descriptor_binding
{
    unsigned int set;
    unsigned int binding;
};

enum vkd3d_shader_binding_flag
{
    VKD3D_SHADER_BINDING_FLAG_BUFFER     = 0x00000001,
    VKD3D_SHADER_BINDING_FLAG_IMAGE      = 0x00000002,
    VKD3D_SHADER_BINDING_FLAG_AUX_BUFFER = 0x00000004,
    VKD3D_SHADER_BINDING_FLAG_BINDLESS   = 0x00000008,
    VKD3D_SHADER_BINDING_FLAG_RAW_VA     = 0x00000010,
    VKD3D_SHADER_BINDING_FLAG_RAW_SSBO   = 0x00000020,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_BINDING_FLAG),
};

enum vkd3d_shader_parameter_type
{
    VKD3D_SHADER_PARAMETER_TYPE_UNKNOWN,
    VKD3D_SHADER_PARAMETER_TYPE_IMMEDIATE_CONSTANT,
    VKD3D_SHADER_PARAMETER_TYPE_SPECIALIZATION_CONSTANT,
};

enum vkd3d_shader_parameter_data_type
{
    VKD3D_SHADER_PARAMETER_DATA_TYPE_UNKNOWN,
    VKD3D_SHADER_PARAMETER_DATA_TYPE_UINT32,
};

enum vkd3d_shader_parameter_name
{
    VKD3D_SHADER_PARAMETER_NAME_UNKNOWN,
    VKD3D_SHADER_PARAMETER_NAME_RASTERIZER_SAMPLE_COUNT,
};

struct vkd3d_shader_parameter_immediate_constant
{
    union
    {
        uint32_t u32;
    };
};

struct vkd3d_shader_parameter_specialization_constant
{
    uint32_t id;
};

struct vkd3d_shader_parameter
{
    enum vkd3d_shader_parameter_name name;
    enum vkd3d_shader_parameter_type type;
    enum vkd3d_shader_parameter_data_type data_type;
    union
    {
        struct vkd3d_shader_parameter_immediate_constant immediate_constant;
        struct vkd3d_shader_parameter_specialization_constant specialization_constant;
    };
};

#define VKD3D_SHADER_DESCRIPTOR_RANGE_UNBOUNDED (~0u)

struct vkd3d_shader_resource_binding
{
    enum vkd3d_shader_descriptor_type type;
    unsigned int register_space;
    unsigned int register_index;
    unsigned int register_count;
    unsigned int descriptor_table;
    unsigned int descriptor_offset;
    enum vkd3d_shader_visibility shader_visibility;
    unsigned int flags; /* vkd3d_shader_binding_flags */

    struct vkd3d_shader_descriptor_binding binding;
};

#define VKD3D_DUMMY_SAMPLER_INDEX ~0u

struct vkd3d_shader_push_constant_buffer
{
    unsigned int register_space;
    unsigned int register_index;
    enum vkd3d_shader_visibility shader_visibility;

    unsigned int offset; /* in bytes */
    unsigned int size;   /* in bytes */
};

struct vkd3d_shader_descriptor_table_buffer
{
    unsigned int offset; /* in bytes */
    unsigned int count;  /* number of tables */
};

enum vkd3d_shader_interface_flag
{
    VKD3D_SHADER_INTERFACE_PUSH_CONSTANTS_AS_UNIFORM_BUFFER    = 0x00000001u,
    VKD3D_SHADER_INTERFACE_BINDLESS_CBV_AS_STORAGE_BUFFER      = 0x00000002u,
    VKD3D_SHADER_INTERFACE_SSBO_OFFSET_BUFFER                  = 0x00000004u,
    VKD3D_SHADER_INTERFACE_TYPED_OFFSET_BUFFER                 = 0x00000008u,
    VKD3D_SHADER_INTERFACE_DESCRIPTOR_QA_BUFFER                = 0x00000010u,
    /* In this model, use descriptor_size_cbv_srv_uav as array stride for raw VA buffer. */
    VKD3D_SHADER_INTERFACE_RAW_VA_ALIAS_DESCRIPTOR_BUFFER      = 0x00000020u,
    VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER               = 0x00000040u,
    VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER_FULL          = 0x00000080u,
    VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER_FLUSH_NAN     = 0x00000100u,
    VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER_EXPECT_ASSUME = 0x00000200u,
    VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER_SYNC          = 0x00000400u,
    VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER_SYNC_COMPUTE  = 0x00000800u,
};

struct vkd3d_shader_stage_io_entry
{
    const char *semantic_name;
    unsigned int semantic_index;
    unsigned int vk_location;
    unsigned int vk_component;
    unsigned int vk_flags;
};

struct vkd3d_shader_stage_io_map
{
    struct vkd3d_shader_stage_io_entry *entries;
    size_t entries_size;
    size_t entry_count;
};

struct vkd3d_shader_stage_io_entry *vkd3d_shader_stage_io_map_append(struct vkd3d_shader_stage_io_map *map,
        const char *semantic_name, unsigned int semantic_index);
const struct vkd3d_shader_stage_io_entry *vkd3d_shader_stage_io_map_find(const struct vkd3d_shader_stage_io_map *map,
        const char *semantic_name, unsigned int semantic_index);
void vkd3d_shader_stage_io_map_free(struct vkd3d_shader_stage_io_map *map);

struct vkd3d_shader_interface_info
{
    unsigned int flags; /* vkd3d_shader_interface_flags */
    unsigned int min_ssbo_alignment;

    struct vkd3d_shader_descriptor_table_buffer descriptor_tables;
    const struct vkd3d_shader_resource_binding *bindings;
    unsigned int binding_count;

    const struct vkd3d_shader_push_constant_buffer *push_constant_buffers;
    unsigned int push_constant_buffer_count;

    /* Ignored unless VKD3D_SHADER_INTERFACE_PUSH_CONSTANTS_AS_UNIFORM_BUFFER is set */
    const struct vkd3d_shader_descriptor_binding *push_constant_ubo_binding;
    /* Ignored unless VKD3D_SHADER_INTERFACE_SSBO_OFFSET_BUFFER or TYPED_OFFSET_BUFFER is set */
    const struct vkd3d_shader_descriptor_binding *offset_buffer_binding;

#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
    /* Ignored unless VKD3D_SHADER_INTERFACE_{DESCRIPTOR,INSTRUCTION}_QA_BUFFER is set. */
    const struct vkd3d_shader_descriptor_binding *descriptor_qa_payload_binding;
    /* Ignored unless VKD3D_SHADER_INTERFACE_{DESCRIPTOR,INSTRUCTION}_QA_BUFFER is set. */
    const struct vkd3d_shader_descriptor_binding *descriptor_qa_control_binding;
#endif

    const struct vkd3d_shader_stage_io_map *stage_input_map;
    struct vkd3d_shader_stage_io_map *stage_output_map;

    VkShaderStageFlagBits stage;

    const struct vkd3d_shader_transform_feedback_info *xfb_info;

    /* Used for either VKD3D_SHADER_INTERFACE_RAW_VA_ALIAS_DESCRIPTOR_BUFFER or local root signatures. */
    uint32_t descriptor_size_cbv_srv_uav;
    uint32_t descriptor_size_sampler;
};

struct vkd3d_shader_descriptor_table
{
    uint32_t table_index;
    uint32_t binding_count;
    struct vkd3d_shader_resource_binding *first_binding;
};

struct vkd3d_shader_root_constant
{
    uint32_t constant_index;
    uint32_t constant_count;
};

struct vkd3d_shader_root_descriptor
{
    struct vkd3d_shader_resource_binding *binding;
    uint32_t raw_va_root_descriptor_index;
};

struct vkd3d_shader_root_parameter
{
    D3D12_ROOT_PARAMETER_TYPE parameter_type;
    union
    {
        struct vkd3d_shader_root_constant constant;
        struct vkd3d_shader_root_descriptor descriptor;
        struct vkd3d_shader_descriptor_table descriptor_table;
    };
};

struct vkd3d_shader_interface_local_info
{
    const struct vkd3d_shader_root_parameter *local_root_parameters;
    unsigned int local_root_parameter_count;
    const struct vkd3d_shader_push_constant_buffer *shader_record_constant_buffers;
    unsigned int shader_record_buffer_count;
    const struct vkd3d_shader_resource_binding *bindings;
    unsigned int binding_count;
};

struct vkd3d_shader_transform_feedback_element
{
    unsigned int stream_index;
    const char *semantic_name;
    unsigned int semantic_index;
    uint8_t component_index;
    uint8_t component_count;
    uint8_t output_slot;
};

struct vkd3d_shader_transform_feedback_info
{
    const struct vkd3d_shader_transform_feedback_element *elements;
    unsigned int element_count;
    const unsigned int *buffer_strides;
    unsigned int buffer_stride_count;
};

enum vkd3d_shader_target
{
    VKD3D_SHADER_TARGET_NONE,
    VKD3D_SHADER_TARGET_SPIRV_VULKAN_1_0, /* default target */

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_TARGET),
};

enum vkd3d_shader_target_extension
{
    VKD3D_SHADER_TARGET_EXTENSION_NONE,

    VKD3D_SHADER_TARGET_EXTENSION_SPV_EXT_DEMOTE_TO_HELPER_INVOCATION,
    VKD3D_SHADER_TARGET_EXTENSION_READ_STORAGE_IMAGE_WITHOUT_FORMAT,
    VKD3D_SHADER_TARGET_EXTENSION_SPV_KHR_INTEGER_DOT_PRODUCT,
    VKD3D_SHADER_TARGET_EXTENSION_RAY_TRACING_PRIMITIVE_CULLING,
    VKD3D_SHADER_TARGET_EXTENSION_SCALAR_BLOCK_LAYOUT,

    /* When using scalar block layout with a vec3 array on a byte address buffer,
     * there is diverging behavior across hardware.
     * On AMD, robustness is checked per component, which means we can implement ByteAddressBuffer
     * without further hackery. On NVIDIA, robustness does not seem to work this way, so it's either
     * all in range, or all out of range. We can implement structured buffer vectorization of vec3,
     * but not byte address buffer. */
    VKD3D_SHADER_TARGET_EXTENSION_ASSUME_PER_COMPONENT_SSBO_ROBUSTNESS,
    VKD3D_SHADER_TARGET_EXTENSION_BARYCENTRIC_KHR,
    VKD3D_SHADER_TARGET_EXTENSION_MIN_PRECISION_IS_NATIVE_16BIT,
    VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP16_DENORM_PRESERVE,
    VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP32_DENORM_FLUSH,
    VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP64_DENORM_PRESERVE,
    VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP16_INF_NAN_PRESERVE,
    VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP32_INF_NAN_PRESERVE,
    VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_FP64_INF_NAN_PRESERVE,
    VKD3D_SHADER_TARGET_EXTENSION_SUPPORT_SUBGROUP_PARTITIONED_NV,
    VKD3D_SHADER_TARGET_EXTENSION_COMPUTE_SHADER_DERIVATIVES_NV,
    VKD3D_SHADER_TARGET_EXTENSION_COMPUTE_SHADER_DERIVATIVES_KHR,
    VKD3D_SHADER_TARGET_EXTENSION_QUAD_CONTROL_RECONVERGENCE,
    VKD3D_SHADER_TARGET_EXTENSION_RAW_ACCESS_CHAINS_NV,
    VKD3D_SHADER_TARGET_EXTENSION_OPACITY_MICROMAP,
    VKD3D_SHADER_TARGET_EXTENSION_WMMA_FP8,
    VKD3D_SHADER_TARGET_EXTENSION_NV_COOPMAT2_CONVERSIONS,
    VKD3D_SHADER_TARGET_EXTENSION_COUNT,
};

enum vkd3d_shader_quirk
{
    /* If sample or sample_b is used in control flow, force LOD 0.0 (which game should expect anyway).
     * Works around specific, questionable shaders which rely on this to give sensible results,
     * since LOD can become garbage on certain implementations, and even on native drivers
     * the result is implementation defined.
     * Outside of making this edge case well-defined in Vulkan or hacking driver compilers,
     * this is the pragmatic solution.
     * Hoisting gradients is not possible in all cases,
     * and would not be worth it until it's a widespread problem. */
    VKD3D_SHADER_QUIRK_FORCE_EXPLICIT_LOD_IN_CONTROL_FLOW = (1 << 0),

    /* After every write to group shared memory, force a memory barrier.
     * This works around buggy games which forget to use barrier(). */
    VKD3D_SHADER_QUIRK_FORCE_TGSM_BARRIERS = (1 << 1),

    /* For Position builtins in Output storage class, emit Invariant decoration.
     * Normally, games have to emit Precise math for position, but if they forget ... */
    VKD3D_SHADER_QUIRK_INVARIANT_POSITION = (1 << 2),

    /* Forces NoContract on every expression that can take it. */
    VKD3D_SHADER_QUIRK_FORCE_NOCONTRACT_MATH = (1 << 3),

    /* Clamp tessellation factors to a "reasonable" value.
     * Different flags for different values is a little jank,
     * but also avoids having to pass down side-channel float values.
     * Also makes it easier to do app profiles with existing quirk structures. */
    VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_32 = (1 << 4),
    VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_16 = (1 << 5),
    VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_12 = (1 << 6),
    VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_8 = (1 << 7),
    VKD3D_SHADER_QUIRK_LIMIT_TESS_FACTORS_4 = (1 << 8),

    /* Force lane count query to return 1.
     * Can be used to disable buggy subgroup logic that checks for subgroup sizes. */
    VKD3D_SHADER_QUIRK_FORCE_SUBGROUP_SIZE_1 = (1 << 9),

    /* Enforce a subgroup size of 32 or less. Can be used to work around
     * issues in shaders that are buggy with large subgroups. */
    VKD3D_SHADER_QUIRK_FORCE_MAX_WAVE32 = (1 << 10),

    /* For shaders which are bugged when you opt-in to 16-bit. */
    VKD3D_SHADER_QUIRK_FORCE_MIN16_AS_32BIT = (1 << 11),

    /* Driver workaround hackery. Try to rewrite weird Grads to plain Bias. */
    VKD3D_SHADER_QUIRK_REWRITE_GRAD_TO_BIAS = (1 << 12),

    /* Driver workarounds. Force loops to not be unrolled with SPIR-V control masks. */
    VKD3D_SHADER_QUIRK_FORCE_LOOP = (1 << 13),

    /* Requests META_FLAG_FORCE_COMPUTE_BARRIER_{AFTER,BEFORE}_DISPATCH to be set in shader meta. */
    VKD3D_SHADER_QUIRK_FORCE_COMPUTE_BARRIER = (1 << 14),
    VKD3D_SHADER_QUIRK_FORCE_PRE_COMPUTE_BARRIER = (1 << 15),

    /* Range check every descriptor heap access with dynamic index and robustness check it. */
    VKD3D_SHADER_QUIRK_DESCRIPTOR_HEAP_ROBUSTNESS = (1 << 16),

    /* Requests META_FLAG_FORCE_PRE_RASTERIZATION_BEFORE_DISPATCH to be set in shader meta. */
    VKD3D_SHADER_QUIRK_FORCE_PRE_RASTERIZATION_BARRIER = (1 << 17),
    /* Requests META_FLAG_FORCE_GRAPHICS_BEFORE_DISPATCH to be set in shader meta. */
    VKD3D_SHADER_QUIRK_FORCE_GRAPHICS_BARRIER = (1 << 18),

    /* VK_PIPELINE_CREATE_DISABLE_OPTIMIZATIONS. For driver workarounds where optimizations break stuff. */
    VKD3D_SHADER_QUIRK_DISABLE_OPTIMIZATIONS = (1 << 19),

    /* Forces NoContract on every expression that can take it (only applies to VS).
     * Useful as a global quirk when games are shipping broken code. */
    VKD3D_SHADER_QUIRK_FORCE_NOCONTRACT_MATH_VS = (1 << 20),

    /* Works around cases where app is relying on coherency between threads in a workgroup,
     * but forgets to use Device memory barriers properly. */
    VKD3D_SHADER_QUIRK_FORCE_DEVICE_MEMORY_BARRIER_THREAD_GROUP_COHERENCY = (1 << 21),

    /* Extremely specific workaround for cubemap importance sampling pass.
     * The lowest res mips may contain garbage. */
    VKD3D_SHADER_QUIRK_ASSUME_BROKEN_SUB_8x8_CUBE_MIPS = (1 << 22),

    /* When an alloca array forwards loads to a CBV, a game might attempt to access the private array OOB.
     * This can lead to page faults. Clamps the input index to the private array size
     * as the simplest possible workaround. */
    VKD3D_SHADER_QUIRK_FORCE_ROBUST_PHYSICAL_CBV_LOAD_FORWARDING = (1 << 23),

    /* Do more aggressive analysis of when nonuniform may be missing from shaders.
     * Not done by default since it's overly conservative. */
    VKD3D_SHADER_QUIRK_AGGRESSIVE_NONUNIFORM = (1 << 24)
};

struct vkd3d_shader_quirk_hash
{
    vkd3d_shader_hash_t shader_hash;
    uint32_t quirks;
};

struct vkd3d_shader_quirk_info
{
    const struct vkd3d_shader_quirk_hash *hashes;
    unsigned int num_hashes;
    uint32_t default_quirks;

    /* Quirks which are ORed in with the other masks (including default_quirks).
     * Used mostly for additional overrides from VKD3D_CONFIG. */
    uint32_t global_quirks;
};

struct vkd3d_shader_compile_arguments
{
    enum vkd3d_shader_target target;

    unsigned int target_extension_count;
    const enum vkd3d_shader_target_extension *target_extensions;

    unsigned int parameter_count;
    const struct vkd3d_shader_parameter *parameters;

    bool dual_source_blending;
    const unsigned int *output_swizzles;
    unsigned int output_swizzle_count;

    uint32_t min_subgroup_size;
    uint32_t max_subgroup_size;
    bool promote_wave_size_heuristics;

    const struct vkd3d_shader_quirk_info *quirks;
    /* Only non-zero when enabled by vkd3d_config */
    VkDriverId driver_id;
    uint32_t driver_version;
};

enum vkd3d_tessellator_output_primitive
{
    VKD3D_TESSELLATOR_OUTPUT_POINT        = 1,
    VKD3D_TESSELLATOR_OUTPUT_LINE         = 2,
    VKD3D_TESSELLATOR_OUTPUT_TRIANGLE_CW  = 3,
    VKD3D_TESSELLATOR_OUTPUT_TRIANGLE_CCW = 4,
};

enum vkd3d_tessellator_partitioning
{
    VKD3D_TESSELLATOR_PARTITIONING_INTEGER         = 1,
    VKD3D_TESSELLATOR_PARTITIONING_POW2            = 2,
    VKD3D_TESSELLATOR_PARTITIONING_FRACTIONAL_ODD  = 3,
    VKD3D_TESSELLATOR_PARTITIONING_FRACTIONAL_EVEN = 4,
};

/* root signature 1.0 */
enum vkd3d_filter
{
    VKD3D_FILTER_MIN_MAG_MIP_POINT = 0x0,
    VKD3D_FILTER_MIN_MAG_POINT_MIP_LINEAR = 0x1,
    VKD3D_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x4,
    VKD3D_FILTER_MIN_POINT_MAG_MIP_LINEAR = 0x5,
    VKD3D_FILTER_MIN_LINEAR_MAG_MIP_POINT = 0x10,
    VKD3D_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x11,
    VKD3D_FILTER_MIN_MAG_LINEAR_MIP_POINT = 0x14,
    VKD3D_FILTER_MIN_MAG_MIP_LINEAR = 0x15,
    VKD3D_FILTER_ANISOTROPIC = 0x55,
    VKD3D_FILTER_COMPARISON_MIN_MAG_MIP_POINT = 0x80,
    VKD3D_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR = 0x81,
    VKD3D_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x84,
    VKD3D_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR = 0x85,
    VKD3D_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT = 0x90,
    VKD3D_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x91,
    VKD3D_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT = 0x94,
    VKD3D_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR = 0x95,
    VKD3D_FILTER_COMPARISON_ANISOTROPIC = 0xd5,
    VKD3D_FILTER_MINIMUM_MIN_MAG_MIP_POINT = 0x100,
    VKD3D_FILTER_MINIMUM_MIN_MAG_POINT_MIP_LINEAR = 0x101,
    VKD3D_FILTER_MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x104,
    VKD3D_FILTER_MINIMUM_MIN_POINT_MAG_MIP_LINEAR = 0x105,
    VKD3D_FILTER_MINIMUM_MIN_LINEAR_MAG_MIP_POINT = 0x110,
    VKD3D_FILTER_MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x111,
    VKD3D_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT = 0x114,
    VKD3D_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR = 0x115,
    VKD3D_FILTER_MINIMUM_ANISOTROPIC = 0x155,
    VKD3D_FILTER_MAXIMUM_MIN_MAG_MIP_POINT = 0x180,
    VKD3D_FILTER_MAXIMUM_MIN_MAG_POINT_MIP_LINEAR = 0x181,
    VKD3D_FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT = 0x184,
    VKD3D_FILTER_MAXIMUM_MIN_POINT_MAG_MIP_LINEAR = 0x185,
    VKD3D_FILTER_MAXIMUM_MIN_LINEAR_MAG_MIP_POINT = 0x190,
    VKD3D_FILTER_MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x191,
    VKD3D_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT = 0x194,
    VKD3D_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR = 0x195,
    VKD3D_FILTER_MAXIMUM_ANISOTROPIC = 0x1d5,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_FILTER),
};

enum vkd3d_texture_address_mode
{
    VKD3D_TEXTURE_ADDRESS_MODE_WRAP = 1,
    VKD3D_TEXTURE_ADDRESS_MODE_MIRROR = 2,
    VKD3D_TEXTURE_ADDRESS_MODE_CLAMP = 3,
    VKD3D_TEXTURE_ADDRESS_MODE_BORDER = 4,
    VKD3D_TEXTURE_ADDRESS_MODE_MIRROR_ONCE = 5,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_TEXTURE_ADDRESS_MODE),
};

enum vkd3d_comparison_func
{
    VKD3D_COMPARISON_FUNC_NEVER = 1,
    VKD3D_COMPARISON_FUNC_LESS = 2,
    VKD3D_COMPARISON_FUNC_EQUAL = 3,
    VKD3D_COMPARISON_FUNC_LESS_EQUAL = 4,
    VKD3D_COMPARISON_FUNC_GREATER = 5,
    VKD3D_COMPARISON_FUNC_NOT_EQUAL = 6,
    VKD3D_COMPARISON_FUNC_GREATER_EQUAL = 7,
    VKD3D_COMPARISON_FUNC_ALWAYS = 8,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_COMPARISON_FUNC),
};

enum vkd3d_static_border_color
{
    VKD3D_STATIC_BORDER_COLOR_TRANSPARENT_BLACK = 0,
    VKD3D_STATIC_BORDER_COLOR_OPAQUE_BLACK = 1,
    VKD3D_STATIC_BORDER_COLOR_OPAQUE_WHITE = 2,
    VKD3D_STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT = 3,
    VKD3D_STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT = 4,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_STATIC_BORDER_COLOR),
};

enum vkd3d_sampler_flags
{
    VKD3D_SAMPLER_FLAG_NONE = 0,
    VKD3D_SAMPLER_FLAG_UINT_BORDER_COLOR = 0x1,
    VKD3D_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES = 0x2,
};

struct vkd3d_static_sampler_desc
{
    enum vkd3d_filter filter;
    enum vkd3d_texture_address_mode address_u;
    enum vkd3d_texture_address_mode address_v;
    enum vkd3d_texture_address_mode address_w;
    float mip_lod_bias;
    unsigned int max_anisotropy;
    enum vkd3d_comparison_func comparison_func;
    enum vkd3d_static_border_color border_color;
    float min_lod;
    float max_lod;
    unsigned int shader_register;
    unsigned int register_space;
    enum vkd3d_shader_visibility shader_visibility;
};

struct vkd3d_static_sampler_desc1
{
    enum vkd3d_filter filter;
    enum vkd3d_texture_address_mode address_u;
    enum vkd3d_texture_address_mode address_v;
    enum vkd3d_texture_address_mode address_w;
    float mip_lod_bias;
    unsigned int max_anisotropy;
    enum vkd3d_comparison_func comparison_func;
    enum vkd3d_static_border_color border_color;
    float min_lod;
    float max_lod;
    unsigned int shader_register;
    unsigned int register_space;
    enum vkd3d_shader_visibility shader_visibility;
    unsigned int flags; /* vkd3d_sampler_flags */
};

enum vkd3d_descriptor_range_type
{
    VKD3D_DESCRIPTOR_RANGE_TYPE_SRV = 0,
    VKD3D_DESCRIPTOR_RANGE_TYPE_UAV = 1,
    VKD3D_DESCRIPTOR_RANGE_TYPE_CBV = 2,
    VKD3D_DESCRIPTOR_RANGE_TYPE_SAMPLER = 3,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_DESCRIPTOR_RANGE_TYPE),
};

struct vkd3d_descriptor_range
{
    enum vkd3d_descriptor_range_type range_type;
    unsigned int descriptor_count;
    unsigned int base_shader_register;
    unsigned int register_space;
    unsigned int descriptor_table_offset;
};

struct vkd3d_root_descriptor_table
{
    unsigned int descriptor_range_count;
    const struct vkd3d_descriptor_range *descriptor_ranges;
};

struct vkd3d_root_constants
{
    unsigned int shader_register;
    unsigned int register_space;
    unsigned int value_count;
};

struct vkd3d_root_descriptor
{
    unsigned int shader_register;
    unsigned int register_space;
};

enum vkd3d_root_parameter_type
{
    VKD3D_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE = 0,
    VKD3D_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS = 1,
    VKD3D_ROOT_PARAMETER_TYPE_CBV = 2,
    VKD3D_ROOT_PARAMETER_TYPE_SRV = 3,
    VKD3D_ROOT_PARAMETER_TYPE_UAV = 4,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_ROOT_PARAMETER_TYPE),
};

struct vkd3d_root_parameter
{
    enum vkd3d_root_parameter_type parameter_type;
    union
    {
        struct vkd3d_root_descriptor_table descriptor_table;
        struct vkd3d_root_constants constants;
        struct vkd3d_root_descriptor descriptor;
    };
    enum vkd3d_shader_visibility shader_visibility;
};

enum vkd3d_root_signature_flags
{
    VKD3D_ROOT_SIGNATURE_FLAG_NONE = 0x0,
    VKD3D_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT = 0x1,
    VKD3D_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS = 0x2,
    VKD3D_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS = 0x4,
    VKD3D_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS = 0x8,
    VKD3D_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS = 0x10,
    VKD3D_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS = 0x20,
    VKD3D_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT = 0x40,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_ROOT_SIGNATURE_FLAGS),
};

struct vkd3d_root_signature_desc
{
    unsigned int parameter_count;
    const struct vkd3d_root_parameter *parameters;
    unsigned int static_sampler_count;
    const struct vkd3d_static_sampler_desc *static_samplers;
    enum vkd3d_root_signature_flags flags;
};

/* root signature 1.1 */
enum vkd3d_root_descriptor_flags
{
    VKD3D_ROOT_DESCRIPTOR_FLAG_NONE = 0x0,
    VKD3D_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE = 0x2,
    VKD3D_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE = 0x4,
    VKD3D_ROOT_DESCRIPTOR_FLAG_DATA_STATIC = 0x8,
};

enum vkd3d_descriptor_range_flags
{
    VKD3D_DESCRIPTOR_RANGE_FLAG_NONE = 0x0,
    VKD3D_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE = 0x1,
    VKD3D_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE = 0x2,
    VKD3D_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE = 0x4,
    VKD3D_DESCRIPTOR_RANGE_FLAG_DATA_STATIC = 0x8,
    VKD3D_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_STATIC_KEEPING_BUFFER_BOUNDS_CHECKS = 0x10000
};

struct vkd3d_descriptor_range1
{
    enum vkd3d_descriptor_range_type range_type;
    unsigned int descriptor_count;
    unsigned int base_shader_register;
    unsigned int register_space;
    enum vkd3d_descriptor_range_flags flags;
    unsigned int descriptor_table_offset;
};

struct vkd3d_root_descriptor_table1
{
    unsigned int descriptor_range_count;
    const struct vkd3d_descriptor_range1 *descriptor_ranges;
};

struct vkd3d_root_descriptor1
{
    unsigned int shader_register;
    unsigned int register_space;
    enum vkd3d_root_descriptor_flags flags;
};

struct vkd3d_root_parameter1
{
    enum vkd3d_root_parameter_type parameter_type;
    union
    {
        struct vkd3d_root_descriptor_table1 descriptor_table;
        struct vkd3d_root_constants constants;
        struct vkd3d_root_descriptor1 descriptor;
    };
    enum vkd3d_shader_visibility shader_visibility;
};

struct vkd3d_root_signature_desc1
{
    unsigned int parameter_count;
    const struct vkd3d_root_parameter1 *parameters;
    unsigned int static_sampler_count;
    const struct vkd3d_static_sampler_desc *static_samplers;
    enum vkd3d_root_signature_flags flags;
};

struct vkd3d_root_signature_desc2
{
    unsigned int parameter_count;
    const struct vkd3d_root_parameter1 *parameters;
    unsigned int static_sampler_count;
    const struct vkd3d_static_sampler_desc1 *static_samplers;
    enum vkd3d_root_signature_flags flags;
};

enum vkd3d_root_signature_version
{
    VKD3D_ROOT_SIGNATURE_VERSION_1_0 = 0x1,
    VKD3D_ROOT_SIGNATURE_VERSION_1_1 = 0x2,
    VKD3D_ROOT_SIGNATURE_VERSION_1_2 = 0x3,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_ROOT_SIGNATURE_VERSION),
};

#define VKD3D_ROOT_SIGNATURE_VERSION_COUNT 3
static inline unsigned vkd3d_root_signature_version_to_other_index(
        enum vkd3d_root_signature_version version)
{
    assert(version >= VKD3D_ROOT_SIGNATURE_VERSION_1_0 &&
            version <= VKD3D_ROOT_SIGNATURE_VERSION_1_2);
    return version - VKD3D_ROOT_SIGNATURE_VERSION_1_0;
}

static inline bool vkd3d_root_signature_version_is_supported(enum vkd3d_root_signature_version version)
{
    return version >= VKD3D_ROOT_SIGNATURE_VERSION_1_0 && version <= VKD3D_ROOT_SIGNATURE_VERSION_1_2;
}

struct vkd3d_versioned_root_signature_desc
{
    enum vkd3d_root_signature_version version;
    union
    {
        struct vkd3d_root_signature_desc v_1_0;
        struct vkd3d_root_signature_desc1 v_1_1;
        struct vkd3d_root_signature_desc2 v_1_2;
    };
};

enum vkd3d_shader_uav_flag
{
    VKD3D_SHADER_UAV_FLAG_READ_ACCESS     = 0x00000001,
    VKD3D_SHADER_UAV_FLAG_ATOMIC_COUNTER  = 0x00000002,
    VKD3D_SHADER_UAV_FLAG_ATOMIC_ACCESS   = 0x00000004,
    VKD3D_SHADER_UAV_FLAG_WRITE_ACCESS    = 0x00000008,
};

struct vkd3d_shader_scan_info
{
    struct hash_map register_map;
    bool use_vocp;

    bool early_fragment_tests;
    bool has_side_effects;
    bool needs_late_zs;
    bool discards;
    bool has_uav_counter;
    bool declares_globally_coherent_uav;
    bool requires_thread_group_uav_coherency;
    bool requires_rov;
    unsigned int patch_vertex_count;
};

enum vkd3d_component_type
{
    VKD3D_TYPE_VOID    = 0,
    VKD3D_TYPE_UINT    = 1,
    VKD3D_TYPE_INT     = 2,
    VKD3D_TYPE_FLOAT   = 3,
    VKD3D_TYPE_BOOL,
    VKD3D_TYPE_DOUBLE,
    VKD3D_TYPE_COUNT,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_COMPONENT_TYPE),
};

enum vkd3d_sysval_semantic
{
    VKD3D_SV_NONE                      = 0,
    VKD3D_SV_POSITION                  = 1,
    VKD3D_SV_CLIP_DISTANCE             = 2,
    VKD3D_SV_CULL_DISTANCE             = 3,
    VKD3D_SV_RENDER_TARGET_ARRAY_INDEX = 4,
    VKD3D_SV_VIEWPORT_ARRAY_INDEX      = 5,
    VKD3D_SV_VERTEX_ID                 = 6,
    VKD3D_SV_PRIMITIVE_ID              = 7,
    VKD3D_SV_INSTANCE_ID               = 8,
    VKD3D_SV_IS_FRONT_FACE             = 9,
    VKD3D_SV_SAMPLE_INDEX              = 10,
    VKD3D_SV_TESS_FACTOR_QUADEDGE      = 11,
    VKD3D_SV_TESS_FACTOR_QUADINT       = 12,
    VKD3D_SV_TESS_FACTOR_TRIEDGE       = 13,
    VKD3D_SV_TESS_FACTOR_TRIINT        = 14,
    VKD3D_SV_TESS_FACTOR_LINEDET       = 15,
    VKD3D_SV_TESS_FACTOR_LINEDEN       = 16,
    VKD3D_SV_BARYCENTRICS              = 23,
    VKD3D_SV_SHADING_RATE              = 24,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SYSVAL_SEMANTIC),
};

enum vkd3d_shader_minimum_precision
{
    VKD3D_SHADER_MINIMUM_PRECISION_NONE      = 0,
    VKD3D_SHADER_MINIMUM_PRECISION_FLOAT_16  = 1,
    VKD3D_SHADER_MINIMUM_PRECISION_FLOAT_8_2 = 2,
    VKD3D_SHADER_MINIMUM_PRECISION_INT_16    = 4,
    VKD3D_SHADER_MINIMUM_PRECISION_UINT_16   = 5,
};

struct vkd3d_shader_signature_element
{
    const char *semantic_name;
    unsigned int semantic_index;
    unsigned int stream_index;
    enum vkd3d_sysval_semantic sysval_semantic;
    enum vkd3d_component_type component_type;
    unsigned int register_index;
    unsigned int mask;
    enum vkd3d_shader_minimum_precision min_precision;
};

struct vkd3d_shader_signature
{
    struct vkd3d_shader_signature_element *elements;
    unsigned int element_count;
};

/* swizzle bits fields: wwzzyyxx */
#define VKD3D_SWIZZLE_X (0u)
#define VKD3D_SWIZZLE_Y (1u)
#define VKD3D_SWIZZLE_Z (2u)
#define VKD3D_SWIZZLE_W (3u)

#define VKD3D_SWIZZLE_MASK (0x3u)
#define VKD3D_SWIZZLE_SHIFT(idx) (2u * (idx))

#define VKD3D_SWIZZLE(x, y, z, w) \
        (((x & VKD3D_SWIZZLE_MASK) << VKD3D_SWIZZLE_SHIFT(0)) \
        | ((y & VKD3D_SWIZZLE_MASK) << VKD3D_SWIZZLE_SHIFT(1)) \
        | ((z & VKD3D_SWIZZLE_MASK) << VKD3D_SWIZZLE_SHIFT(2)) \
        | ((w & VKD3D_SWIZZLE_MASK) << VKD3D_SWIZZLE_SHIFT(3)))

#define VKD3D_NO_SWIZZLE \
        VKD3D_SWIZZLE(VKD3D_SWIZZLE_X, VKD3D_SWIZZLE_Y, VKD3D_SWIZZLE_Z, VKD3D_SWIZZLE_W)

#ifndef VKD3D_SHADER_NO_PROTOTYPES

int vkd3d_shader_compile_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv, struct vkd3d_shader_code_debug *spirv_debug,
        unsigned int compiler_options,
        const struct vkd3d_shader_interface_info *shader_interface_info,
        const struct vkd3d_shader_compile_arguments *compile_args);
void vkd3d_shader_free_shader_code(struct vkd3d_shader_code *code);
void vkd3d_shader_free_shader_code_debug(struct vkd3d_shader_code_debug *code);

int vkd3d_shader_parse_root_signature(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *root_signature,
        vkd3d_shader_hash_t *compatibility_hash);
int vkd3d_shader_parse_root_signature_raw(const char *data, unsigned int data_size,
        struct vkd3d_versioned_root_signature_desc *desc,
        vkd3d_shader_hash_t *compatibility_hash);
void vkd3d_shader_free_root_signature(struct vkd3d_versioned_root_signature_desc *root_signature);

/* FIXME: Add support for returning error messages (ID3DBlob). */
int vkd3d_shader_serialize_root_signature(const struct vkd3d_versioned_root_signature_desc *root_signature,
        struct vkd3d_shader_code *dxbc);

int vkd3d_shader_convert_root_signature(struct vkd3d_versioned_root_signature_desc *dst,
        enum vkd3d_root_signature_version version, const struct vkd3d_versioned_root_signature_desc *src);

int vkd3d_shader_scan_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_scan_info *scan_info);

int vkd3d_shader_parse_input_signature(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_signature *signature);
int vkd3d_shader_parse_output_signature(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_signature *signature);
int vkd3d_shader_parse_patch_constant_signature(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_signature *signature);
struct vkd3d_shader_signature_element *vkd3d_shader_find_signature_element(
        const struct vkd3d_shader_signature *signature, const char *semantic_name,
        unsigned int semantic_index, unsigned int stream_index);
void vkd3d_shader_free_shader_signature(struct vkd3d_shader_signature *signature);

enum vkd3d_shader_node_launch_type
{
    VKD3D_SHADER_NODE_LAUNCH_TYPE_INVALID = 0,
    VKD3D_SHADER_NODE_LAUNCH_TYPE_BROADCASTING = 1,
    VKD3D_SHADER_NODE_LAUNCH_TYPE_COALESCING = 2,
    VKD3D_SHADER_NODE_LAUNCH_TYPE_THREAD = 3
};

/* For emulation path. */
struct vkd3d_shader_node_input_push_signature
{
    VkDeviceAddress node_payload_bda;
    VkDeviceAddress node_linear_offset_bda;
    VkDeviceAddress node_total_nodes_bda;
    VkDeviceAddress node_payload_stride_or_offsets_bda;
    VkDeviceAddress node_payload_output_bda;
    VkDeviceAddress node_payload_output_atomic_bda;
    VkDeviceAddress local_root_signature_bda;
    uint32_t node_payload_output_offset;
    uint32_t node_payload_output_stride;
    uint32_t node_remaining_recursion_levels;
};

struct vkd3d_shader_node_input_data
{
    const char *node_id; /* This is often same as entry point name, but does not have to be. */
    uint32_t payload_stride; /* If 0, there is no input payload, i.e. EmptyNode. */
    enum vkd3d_shader_node_launch_type launch_type;
    uint32_t node_array_index;
    uint32_t dispatch_grid_offset;
    uint32_t dispatch_grid_type_bits;
    uint32_t dispatch_grid_components;
    uint32_t broadcast_grid[3]; /* For broadcast nodes. */
    uint32_t thread_group_size_spec_id[3];
    uint32_t recursion_factor; /* [NodeMaxRecursionDepth] */
    uint32_t coalesce_factor;
    const char *node_share_input_id;
    uint32_t node_share_input_array_index;
    uint32_t local_root_arguments_table_index;
    uint32_t is_indirect_bda_stride_program_entry_spec_id;
    uint32_t is_entry_point_spec_id;
    uint32_t is_static_broadcast_node_spec_id;
    uint32_t dispatch_grid_is_upper_bound_spec_id;
    bool dispatch_grid_is_upper_bound; /* [NodeMaxDispatchGrid] if true. */
    bool node_track_rw_input_sharing; /* Payload is tagged with [NodeTrackRWInputSharing]. */
    bool is_program_entry; /* [NodeIsProgramEntry] */
};

struct vkd3d_shader_node_output_data
{
    const char *node_id;
    uint32_t node_array_index;
    uint32_t node_array_size; /* If UINT32_MAX, it's unbounded. */
    uint32_t node_index_spec_constant_id;
    uint32_t max_records;
    bool sparse_array;
    /* We get the rest of the information from the target entry point's input data.
     * Output data is only for determining linkage. */
};

/* For DXR, use special purpose entry points since there's a lot of special purpose reflection required. */
struct vkd3d_shader_library_entry_point
{
    unsigned int identifier;
    VkShaderStageFlagBits stage;

    uint32_t pipeline_variant_index;
    uint32_t stage_index;

    /* For implementing the API, since it uses WCHAR despite C++ identifiers being ASCII ... */
    WCHAR *mangled_entry_point;
    WCHAR *plain_entry_point;

    /* Used only for interfacing with dxil-spirv. */
    char *real_entry_point;
    char *debug_entry_point;

    struct vkd3d_shader_node_input_data *node_input;
    struct vkd3d_shader_node_output_data *node_outputs;
    size_t node_outputs_size;
    size_t node_outputs_count;
};

enum vkd3d_shader_subobject_kind
{
    /* Matches DXIL for simplicity. */
    VKD3D_SHADER_SUBOBJECT_KIND_STATE_OBJECT_CONFIG = 0,
    VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE = 1,
    VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE = 2,
    VKD3D_SHADER_SUBOBJECT_KIND_SUBOBJECT_TO_EXPORTS_ASSOCIATION = 8,
    VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_SHADER_CONFIG = 9,
    VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG = 10,
    VKD3D_SHADER_SUBOBJECT_KIND_HIT_GROUP = 11,
    VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG1 = 12,
};

struct vkd3d_shader_library_subobject
{
    enum vkd3d_shader_subobject_kind kind;

    /* All const pointers here point directly to the DXBC blob,
     * so they do not need to be freed.
     * Fortunately for us, the C strings are zero-terminated in the blob itself. */

    /* In the blob, ASCII is used as identifier, where API uses wide strings, sigh ...
     * We need to dup this name for deferred COLLECTIONS, so use wchar instead. */
    WCHAR *name;

    /* If true, any pointers below are just borrowed. */
    bool borrowed_payloads;

    union
    {
        D3D12_RAYTRACING_PIPELINE_CONFIG1 pipeline_config;
        D3D12_RAYTRACING_SHADER_CONFIG shader_config;
        D3D12_STATE_OBJECT_CONFIG object_config;

        /* Duped strings because API wants wide strings for no good reason. */
        D3D12_HIT_GROUP_DESC hit_group;
        D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION association;

        struct
        {
            /* Duped because of deferred COLLECTIONS. */
            void *data;
            size_t size;
        } payload;
    } data;
};

int vkd3d_shader_dxil_append_library_entry_points_and_subobjects(
        const D3D12_DXIL_LIBRARY_DESC *library_desc,
        unsigned int identifier,
        struct vkd3d_shader_library_entry_point **entry_points,
        size_t *entry_point_size, size_t *entry_point_count,
        struct vkd3d_shader_library_subobject **subobjects,
        size_t *subobjects_size, size_t *subobjects_count);

void vkd3d_shader_dxil_free_library_entry_points(struct vkd3d_shader_library_entry_point *entry_points, size_t count);
void vkd3d_shader_dxil_free_library_subobjects(struct vkd3d_shader_library_subobject *subobjects, size_t count);

int vkd3d_shader_dxil_find_global_root_signature_subobject(const void *dxbc, size_t size,
        struct vkd3d_shader_code *code);

/* export may be a mangled or demangled name.
 * If RTPSO requests the demangled name, it will likely be demangled, otherwise we forward the mangled name directly.
 * demangled_export is always a demangled name, for debug purposes. Can be NULL. */
int vkd3d_shader_compile_dxil_export(const struct vkd3d_shader_code *dxil,
        const char *export, const char *demangled_export,
        struct vkd3d_shader_code *spirv,
        struct vkd3d_shader_code_debug *spirv_debug,
        const struct vkd3d_shader_interface_info *shader_interface_info,
        const struct vkd3d_shader_interface_local_info *shader_interface_local_info,
        const struct vkd3d_shader_compile_arguments *compiler_args);

uint32_t vkd3d_shader_compile_arguments_select_quirks(
        const struct vkd3d_shader_compile_arguments *args, vkd3d_shader_hash_t hash);

uint64_t vkd3d_shader_get_revision(void);

#endif  /* VKD3D_SHADER_NO_PROTOTYPES */

/*
 * Function pointer typedefs for vkd3d-shader functions.
 */
typedef int (*PFN_vkd3d_shader_compile_dxbc)(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_code *spirv, struct vkd3d_shader_code_debug *spirv_debug,
        unsigned int compiler_options,
        const struct vkd3d_shader_interface_info *shader_interface_info,
        const struct vkd3d_shader_compile_arguments *compile_args);
typedef void (*PFN_vkd3d_shader_free_shader_code)(struct vkd3d_shader_code *code);

typedef int (*PFN_vkd3d_shader_parse_root_signature)(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *root_signature,
        vkd3d_shader_hash_t *compatibility_hash);
typedef void (*PFN_vkd3d_shader_free_root_signature)(struct vkd3d_versioned_root_signature_desc *root_signature);

typedef int (*PFN_vkd3d_shader_serialize_root_signature)(
        const struct vkd3d_versioned_root_signature_desc *root_signature, struct vkd3d_shader_code *dxbc);

typedef int (*PFN_vkd3d_shader_convert_root_signature)(struct vkd3d_versioned_root_signature_desc *dst,
        enum vkd3d_root_signature_version version, const struct vkd3d_versioned_root_signature_desc *src);

typedef int (*PFN_vkd3d_shader_scan_dxbc)(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_scan_info *scan_info);

typedef int (*PFN_vkd3d_shader_parse_input_signature)(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_signature *signature);
typedef struct vkd3d_shader_signature_element * (*PFN_vkd3d_shader_find_signature_element)(
        const struct vkd3d_shader_signature *signature, const char *semantic_name,
        unsigned int semantic_index, unsigned int stream_index);
typedef void (*PFN_vkd3d_shader_free_shader_signature)(struct vkd3d_shader_signature *signature);

int vkd3d_shader_parse_root_signature_v_1_0(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *desc,
        vkd3d_shader_hash_t *compatibility_hash);
int vkd3d_shader_parse_root_signature_v_1_2(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *out_desc,
        vkd3d_shader_hash_t *compatibility_hash);
int vkd3d_shader_parse_root_signature_v_1_2_from_raw_payload(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *out_desc,
        vkd3d_shader_hash_t *compatibility_hash);

vkd3d_shader_hash_t vkd3d_root_signature_v_1_2_compute_layout_compat_hash(
        const struct vkd3d_root_signature_desc2 *desc);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* __VKD3D_SHADER_H */
