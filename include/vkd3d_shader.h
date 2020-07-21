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
#include <vkd3d_types.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

enum vkd3d_shader_structure_type
{
    /* 1.2 */
    VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO,
    VKD3D_SHADER_STRUCTURE_TYPE_INTERFACE_INFO,
    VKD3D_SHADER_STRUCTURE_TYPE_SCAN_INFO,
    VKD3D_SHADER_STRUCTURE_TYPE_SPIRV_DOMAIN_SHADER_TARGET_INFO,
    VKD3D_SHADER_STRUCTURE_TYPE_SPIRV_TARGET_INFO,
    VKD3D_SHADER_STRUCTURE_TYPE_TRANSFORM_FEEDBACK_INFO,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_STRUCTURE_TYPE),
};

enum vkd3d_shader_compile_option_name
{
    VKD3D_SHADER_COMPILE_OPTION_STRIP_DEBUG = 0x00000001,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_COMPILE_OPTION_NAME),
};

struct vkd3d_shader_compile_option
{
    enum vkd3d_shader_compile_option_name name;
    unsigned int value;
};

enum vkd3d_shader_visibility
{
    VKD3D_SHADER_VISIBILITY_ALL = 0,
    VKD3D_SHADER_VISIBILITY_VERTEX = 1,
    VKD3D_SHADER_VISIBILITY_HULL = 2,
    VKD3D_SHADER_VISIBILITY_DOMAIN = 3,
    VKD3D_SHADER_VISIBILITY_GEOMETRY = 4,
    VKD3D_SHADER_VISIBILITY_PIXEL = 5,

    VKD3D_SHADER_VISIBILITY_COMPUTE = 1000000000,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_VISIBILITY),
};

struct vkd3d_shader_code
{
    const void *code;
    size_t size;
};

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
    VKD3D_SHADER_BINDING_FLAG_BUFFER = 0x00000001,
    VKD3D_SHADER_BINDING_FLAG_IMAGE  = 0x00000002,

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
    } u;
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
    } u;
};

struct vkd3d_shader_resource_binding
{
    enum vkd3d_shader_descriptor_type type;
    unsigned int register_space;
    unsigned int register_index;
    enum vkd3d_shader_visibility shader_visibility;
    unsigned int flags; /* vkd3d_shader_binding_flag */

    struct vkd3d_shader_descriptor_binding binding;
};

#define VKD3D_DUMMY_SAMPLER_INDEX ~0u

struct vkd3d_shader_combined_resource_sampler
{
    unsigned int resource_index;
    unsigned int sampler_index;
    enum vkd3d_shader_visibility shader_visibility;
    unsigned int flags; /* vkd3d_shader_binding_flag */

    struct vkd3d_shader_descriptor_binding binding;
};

struct vkd3d_shader_uav_counter_binding
{
    unsigned int register_space;
    unsigned int register_index; /* u# */
    enum vkd3d_shader_visibility shader_visibility;

    struct vkd3d_shader_descriptor_binding binding;
    unsigned int offset;
};

struct vkd3d_shader_push_constant_buffer
{
    unsigned int register_space;
    unsigned int register_index;
    enum vkd3d_shader_visibility shader_visibility;

    unsigned int offset; /* in bytes */
    unsigned int size;   /* in bytes */
};

/* Extends vkd3d_shader_compile_info. */
struct vkd3d_shader_interface_info
{
    enum vkd3d_shader_structure_type type;
    const void *next;

    const struct vkd3d_shader_resource_binding *bindings;
    unsigned int binding_count;

    const struct vkd3d_shader_push_constant_buffer *push_constant_buffers;
    unsigned int push_constant_buffer_count;

    const struct vkd3d_shader_combined_resource_sampler *combined_samplers;
    unsigned int combined_sampler_count;

    const struct vkd3d_shader_uav_counter_binding *uav_counters;
    unsigned int uav_counter_count;
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

/* Extends vkd3d_shader_interface_info. */
struct vkd3d_shader_transform_feedback_info
{
    enum vkd3d_shader_structure_type type;
    const void *next;

    const struct vkd3d_shader_transform_feedback_element *elements;
    unsigned int element_count;
    const unsigned int *buffer_strides;
    unsigned int buffer_stride_count;
};

enum vkd3d_shader_source_type
{
    VKD3D_SHADER_SOURCE_NONE,
    VKD3D_SHADER_SOURCE_DXBC_TPF,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_SOURCE_TYPE),
};

enum vkd3d_shader_target_type
{
    VKD3D_SHADER_TARGET_NONE,
    VKD3D_SHADER_TARGET_SPIRV_BINARY,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_TARGET_TYPE),
};

struct vkd3d_shader_compile_info
{
    enum vkd3d_shader_structure_type type;
    const void *next;

    struct vkd3d_shader_code source;

    enum vkd3d_shader_source_type source_type;
    enum vkd3d_shader_target_type target_type;

    const struct vkd3d_shader_compile_option *options;
    unsigned int option_count;
};

enum vkd3d_shader_spirv_environment
{
    VKD3D_SHADER_SPIRV_ENVIRONMENT_NONE,
    VKD3D_SHADER_SPIRV_ENVIRONMENT_OPENGL_4_5,
    VKD3D_SHADER_SPIRV_ENVIRONMENT_VULKAN_1_0, /* default target */

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_SPIRV_ENVIRONMENT),
};

enum vkd3d_shader_spirv_extension
{
    VKD3D_SHADER_SPIRV_EXTENSION_NONE,
    VKD3D_SHADER_SPIRV_EXTENSION_EXT_DEMOTE_TO_HELPER_INVOCATION,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_SPIRV_EXTENSION),
};

/* Extends vkd3d_shader_compile_info. */
struct vkd3d_shader_spirv_target_info
{
    enum vkd3d_shader_structure_type type;
    const void *next;

    const char *entry_point; /* "main" if NULL. */

    enum vkd3d_shader_spirv_environment environment;

    const enum vkd3d_shader_spirv_extension *extensions;
    unsigned int extension_count;

    const struct vkd3d_shader_parameter *parameters;
    unsigned int parameter_count;

    bool dual_source_blending;
    const unsigned int *output_swizzles;
    unsigned int output_swizzle_count;
};

enum vkd3d_shader_tessellator_output_primitive
{
    VKD3D_SHADER_TESSELLATOR_OUTPUT_POINT        = 0x1,
    VKD3D_SHADER_TESSELLATOR_OUTPUT_LINE         = 0x2,
    VKD3D_SHADER_TESSELLATOR_OUTPUT_TRIANGLE_CW  = 0x3,
    VKD3D_SHADER_TESSELLATOR_OUTPUT_TRIANGLE_CCW = 0x4,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_TESSELLATOR_OUTPUT_PRIMITIVE),
};

enum vkd3d_shader_tessellator_partitioning
{
    VKD3D_SHADER_TESSELLATOR_PARTITIONING_INTEGER         = 0x1,
    VKD3D_SHADER_TESSELLATOR_PARTITIONING_POW2            = 0x2,
    VKD3D_SHADER_TESSELLATOR_PARTITIONING_FRACTIONAL_ODD  = 0x3,
    VKD3D_SHADER_TESSELLATOR_PARTITIONING_FRACTIONAL_EVEN = 0x4,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_TESSELLATOR_PARTITIONING),
};

/* Extends vkd3d_shader_spirv_target_info. */
struct vkd3d_shader_spirv_domain_shader_target_info
{
    enum vkd3d_shader_structure_type type;
    const void *next;

    enum vkd3d_shader_tessellator_output_primitive output_primitive;
    enum vkd3d_shader_tessellator_partitioning partitioning;
};

/* root signature 1.0 */
enum vkd3d_shader_filter
{
    VKD3D_SHADER_FILTER_MIN_MAG_MIP_POINT                          = 0x000,
    VKD3D_SHADER_FILTER_MIN_MAG_POINT_MIP_LINEAR                   = 0x001,
    VKD3D_SHADER_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT             = 0x004,
    VKD3D_SHADER_FILTER_MIN_POINT_MAG_MIP_LINEAR                   = 0x005,
    VKD3D_SHADER_FILTER_MIN_LINEAR_MAG_MIP_POINT                   = 0x010,
    VKD3D_SHADER_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR            = 0x011,
    VKD3D_SHADER_FILTER_MIN_MAG_LINEAR_MIP_POINT                   = 0x014,
    VKD3D_SHADER_FILTER_MIN_MAG_MIP_LINEAR                         = 0x015,
    VKD3D_SHADER_FILTER_ANISOTROPIC                                = 0x055,
    VKD3D_SHADER_FILTER_COMPARISON_MIN_MAG_MIP_POINT               = 0x080,
    VKD3D_SHADER_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR        = 0x081,
    VKD3D_SHADER_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT  = 0x084,
    VKD3D_SHADER_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR        = 0x085,
    VKD3D_SHADER_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT        = 0x090,
    VKD3D_SHADER_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x091,
    VKD3D_SHADER_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT        = 0x094,
    VKD3D_SHADER_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR              = 0x095,
    VKD3D_SHADER_FILTER_COMPARISON_ANISOTROPIC                     = 0x0d5,
    VKD3D_SHADER_FILTER_MINIMUM_MIN_MAG_MIP_POINT                  = 0x100,
    VKD3D_SHADER_FILTER_MINIMUM_MIN_MAG_POINT_MIP_LINEAR           = 0x101,
    VKD3D_SHADER_FILTER_MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT     = 0x104,
    VKD3D_SHADER_FILTER_MINIMUM_MIN_POINT_MAG_MIP_LINEAR           = 0x105,
    VKD3D_SHADER_FILTER_MINIMUM_MIN_LINEAR_MAG_MIP_POINT           = 0x110,
    VKD3D_SHADER_FILTER_MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR    = 0x111,
    VKD3D_SHADER_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT           = 0x114,
    VKD3D_SHADER_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR                 = 0x115,
    VKD3D_SHADER_FILTER_MINIMUM_ANISOTROPIC                        = 0x155,
    VKD3D_SHADER_FILTER_MAXIMUM_MIN_MAG_MIP_POINT                  = 0x180,
    VKD3D_SHADER_FILTER_MAXIMUM_MIN_MAG_POINT_MIP_LINEAR           = 0x181,
    VKD3D_SHADER_FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT     = 0x184,
    VKD3D_SHADER_FILTER_MAXIMUM_MIN_POINT_MAG_MIP_LINEAR           = 0x185,
    VKD3D_SHADER_FILTER_MAXIMUM_MIN_LINEAR_MAG_MIP_POINT           = 0x190,
    VKD3D_SHADER_FILTER_MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR    = 0x191,
    VKD3D_SHADER_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT           = 0x194,
    VKD3D_SHADER_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR                 = 0x195,
    VKD3D_SHADER_FILTER_MAXIMUM_ANISOTROPIC                        = 0x1d5,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_FILTER),
};

enum vkd3d_shader_texture_address_mode
{
    VKD3D_SHADER_TEXTURE_ADDRESS_MODE_WRAP        = 0x1,
    VKD3D_SHADER_TEXTURE_ADDRESS_MODE_MIRROR      = 0x2,
    VKD3D_SHADER_TEXTURE_ADDRESS_MODE_CLAMP       = 0x3,
    VKD3D_SHADER_TEXTURE_ADDRESS_MODE_BORDER      = 0x4,
    VKD3D_SHADER_TEXTURE_ADDRESS_MODE_MIRROR_ONCE = 0x5,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_TEXTURE_ADDRESS_MODE),
};

enum vkd3d_shader_comparison_func
{
    VKD3D_SHADER_COMPARISON_FUNC_NEVER         = 0x1,
    VKD3D_SHADER_COMPARISON_FUNC_LESS          = 0x2,
    VKD3D_SHADER_COMPARISON_FUNC_EQUAL         = 0x3,
    VKD3D_SHADER_COMPARISON_FUNC_LESS_EQUAL    = 0x4,
    VKD3D_SHADER_COMPARISON_FUNC_GREATER       = 0x5,
    VKD3D_SHADER_COMPARISON_FUNC_NOT_EQUAL     = 0x6,
    VKD3D_SHADER_COMPARISON_FUNC_GREATER_EQUAL = 0x7,
    VKD3D_SHADER_COMPARISON_FUNC_ALWAYS        = 0x8,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_COMPARISON_FUNC),
};

enum vkd3d_shader_static_border_colour
{
    VKD3D_SHADER_STATIC_BORDER_COLOUR_TRANSPARENT_BLACK = 0x0,
    VKD3D_SHADER_STATIC_BORDER_COLOUR_OPAQUE_BLACK      = 0x1,
    VKD3D_SHADER_STATIC_BORDER_COLOUR_OPAQUE_WHITE      = 0x2,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_STATIC_BORDER_COLOUR),
};

struct vkd3d_shader_static_sampler_desc
{
    enum vkd3d_shader_filter filter;
    enum vkd3d_shader_texture_address_mode address_u;
    enum vkd3d_shader_texture_address_mode address_v;
    enum vkd3d_shader_texture_address_mode address_w;
    float mip_lod_bias;
    unsigned int max_anisotropy;
    enum vkd3d_shader_comparison_func comparison_func;
    enum vkd3d_shader_static_border_colour border_colour;
    float min_lod;
    float max_lod;
    unsigned int shader_register;
    unsigned int register_space;
    enum vkd3d_shader_visibility shader_visibility;
};

enum vkd3d_shader_descriptor_range_type
{
    VKD3D_SHADER_DESCRIPTOR_RANGE_TYPE_SRV     = 0x0,
    VKD3D_SHADER_DESCRIPTOR_RANGE_TYPE_UAV     = 0x1,
    VKD3D_SHADER_DESCRIPTOR_RANGE_TYPE_CBV     = 0x2,
    VKD3D_SHADER_DESCRIPTOR_RANGE_TYPE_SAMPLER = 0x3,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_DESCRIPTOR_RANGE_TYPE),
};

struct vkd3d_shader_descriptor_range
{
    enum vkd3d_shader_descriptor_range_type range_type;
    unsigned int descriptor_count;
    unsigned int base_shader_register;
    unsigned int register_space;
    unsigned int descriptor_table_offset;
};

struct vkd3d_shader_root_descriptor_table
{
    unsigned int descriptor_range_count;
    const struct vkd3d_shader_descriptor_range *descriptor_ranges;
};

struct vkd3d_shader_root_constants
{
    unsigned int shader_register;
    unsigned int register_space;
    unsigned int value_count;
};

struct vkd3d_shader_root_descriptor
{
    unsigned int shader_register;
    unsigned int register_space;
};

enum vkd3d_shader_root_parameter_type
{
    VKD3D_SHADER_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE = 0x0,
    VKD3D_SHADER_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS  = 0x1,
    VKD3D_SHADER_ROOT_PARAMETER_TYPE_CBV              = 0x2,
    VKD3D_SHADER_ROOT_PARAMETER_TYPE_SRV              = 0x3,
    VKD3D_SHADER_ROOT_PARAMETER_TYPE_UAV              = 0x4,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_ROOT_PARAMETER_TYPE),
};

struct vkd3d_shader_root_parameter
{
    enum vkd3d_shader_root_parameter_type parameter_type;
    union
    {
        struct vkd3d_shader_root_descriptor_table descriptor_table;
        struct vkd3d_shader_root_constants constants;
        struct vkd3d_shader_root_descriptor descriptor;
    } u;
    enum vkd3d_shader_visibility shader_visibility;
};

enum vkd3d_shader_root_signature_flags
{
    VKD3D_SHADER_ROOT_SIGNATURE_FLAG_NONE                               = 0x00,
    VKD3D_SHADER_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT = 0x01,
    VKD3D_SHADER_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS     = 0x02,
    VKD3D_SHADER_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS       = 0x04,
    VKD3D_SHADER_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS     = 0x08,
    VKD3D_SHADER_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS   = 0x10,
    VKD3D_SHADER_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS      = 0x20,
    VKD3D_SHADER_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT                = 0x40,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_ROOT_SIGNATURE_FLAGS),
};

struct vkd3d_shader_root_signature_desc
{
    unsigned int parameter_count;
    const struct vkd3d_shader_root_parameter *parameters;
    unsigned int static_sampler_count;
    const struct vkd3d_shader_static_sampler_desc *static_samplers;
    enum vkd3d_shader_root_signature_flags flags;
};

/* root signature 1.1 */
enum vkd3d_shader_root_descriptor_flags
{
    VKD3D_SHADER_ROOT_DESCRIPTOR_FLAG_NONE                             = 0x0,
    VKD3D_SHADER_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE                    = 0x2,
    VKD3D_SHADER_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE = 0x4,
    VKD3D_SHADER_ROOT_DESCRIPTOR_FLAG_DATA_STATIC                      = 0x8,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_ROOT_DESCRIPTOR_FLAGS),
};

enum vkd3d_shader_descriptor_range_flags
{
    VKD3D_SHADER_DESCRIPTOR_RANGE_FLAG_NONE                             = 0x0,
    VKD3D_SHADER_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE             = 0x1,
    VKD3D_SHADER_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE                    = 0x2,
    VKD3D_SHADER_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE = 0x4,
    VKD3D_SHADER_DESCRIPTOR_RANGE_FLAG_DATA_STATIC                      = 0x8,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_DESCRIPTOR_RANGE_FLAGS),
};

struct vkd3d_shader_descriptor_range1
{
    enum vkd3d_shader_descriptor_range_type range_type;
    unsigned int descriptor_count;
    unsigned int base_shader_register;
    unsigned int register_space;
    enum vkd3d_shader_descriptor_range_flags flags;
    unsigned int descriptor_table_offset;
};

struct vkd3d_shader_root_descriptor_table1
{
    unsigned int descriptor_range_count;
    const struct vkd3d_shader_descriptor_range1 *descriptor_ranges;
};

struct vkd3d_shader_root_descriptor1
{
    unsigned int shader_register;
    unsigned int register_space;
    enum vkd3d_shader_root_descriptor_flags flags;
};

struct vkd3d_shader_root_parameter1
{
    enum vkd3d_shader_root_parameter_type parameter_type;
    union
    {
        struct vkd3d_shader_root_descriptor_table1 descriptor_table;
        struct vkd3d_shader_root_constants constants;
        struct vkd3d_shader_root_descriptor1 descriptor;
    } u;
    enum vkd3d_shader_visibility shader_visibility;
};

struct vkd3d_root_signature_desc1
{
    unsigned int parameter_count;
    const struct vkd3d_shader_root_parameter1 *parameters;
    unsigned int static_sampler_count;
    const struct vkd3d_shader_static_sampler_desc *static_samplers;
    enum vkd3d_shader_root_signature_flags flags;
};

enum vkd3d_shader_root_signature_version
{
    VKD3D_SHADER_ROOT_SIGNATURE_VERSION_1_0 = 0x1,
    VKD3D_SHADER_ROOT_SIGNATURE_VERSION_1_1 = 0x2,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_ROOT_SIGNATURE_VERSION),
};

struct vkd3d_versioned_root_signature_desc
{
    enum vkd3d_shader_root_signature_version version;
    union
    {
        struct vkd3d_shader_root_signature_desc v_1_0;
        struct vkd3d_root_signature_desc1 v_1_1;
    } u;
};

enum vkd3d_shader_resource_type
{
    VKD3D_SHADER_RESOURCE_NONE              = 0x0,
    VKD3D_SHADER_RESOURCE_BUFFER            = 0x1,
    VKD3D_SHADER_RESOURCE_TEXTURE_1D        = 0x2,
    VKD3D_SHADER_RESOURCE_TEXTURE_2D        = 0x3,
    VKD3D_SHADER_RESOURCE_TEXTURE_2DMS      = 0x4,
    VKD3D_SHADER_RESOURCE_TEXTURE_3D        = 0x5,
    VKD3D_SHADER_RESOURCE_TEXTURE_CUBE      = 0x6,
    VKD3D_SHADER_RESOURCE_TEXTURE_1DARRAY   = 0x7,
    VKD3D_SHADER_RESOURCE_TEXTURE_2DARRAY   = 0x8,
    VKD3D_SHADER_RESOURCE_TEXTURE_2DMSARRAY = 0x9,
    VKD3D_SHADER_RESOURCE_TEXTURE_CUBEARRAY = 0xa,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_RESOURCE_TYPE),
};

enum vkd3d_shader_resource_data_type
{
    VKD3D_SHADER_RESOURCE_DATA_UNORM = 0x1,
    VKD3D_SHADER_RESOURCE_DATA_SNORM = 0x2,
    VKD3D_SHADER_RESOURCE_DATA_INT   = 0x3,
    VKD3D_SHADER_RESOURCE_DATA_UINT  = 0x4,
    VKD3D_SHADER_RESOURCE_DATA_FLOAT = 0x5,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_RESOURCE_DATA_TYPE),
};

enum vkd3d_shader_descriptor_info_flag
{
    VKD3D_SHADER_DESCRIPTOR_INFO_FLAG_UAV_COUNTER             = 0x00000001,
    VKD3D_SHADER_DESCRIPTOR_INFO_FLAG_UAV_READ                = 0x00000002,
    VKD3D_SHADER_DESCRIPTOR_INFO_FLAG_SAMPLER_COMPARISON_MODE = 0x00000004,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_DESCRIPTOR_INFO_FLAG),
};

struct vkd3d_shader_descriptor_info
{
    enum vkd3d_shader_descriptor_type type;
    unsigned int register_space;
    unsigned int register_index;
    enum vkd3d_shader_resource_type resource_type;
    enum vkd3d_shader_resource_data_type resource_data_type;
    unsigned int flags; /* vkd3d_shader_descriptor_info_flag */
    unsigned int count;
};

struct vkd3d_shader_scan_info
{
    enum vkd3d_shader_structure_type type;
    void *next;

    struct vkd3d_shader_descriptor_info *descriptors;
    unsigned int descriptor_count;
};

enum vkd3d_shader_component_type
{
    VKD3D_SHADER_COMPONENT_VOID     = 0x0,
    VKD3D_SHADER_COMPONENT_UINT     = 0x1,
    VKD3D_SHADER_COMPONENT_INT      = 0x2,
    VKD3D_SHADER_COMPONENT_FLOAT    = 0x3,
    VKD3D_SHADER_COMPONENT_BOOL     = 0x4,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_COMPONENT_TYPE),
};

enum vkd3d_shader_sysval_semantic
{
    VKD3D_SHADER_SV_NONE                      = 0x00,
    VKD3D_SHADER_SV_POSITION                  = 0x01,
    VKD3D_SHADER_SV_CLIP_DISTANCE             = 0x02,
    VKD3D_SHADER_SV_CULL_DISTANCE             = 0x03,
    VKD3D_SHADER_SV_RENDER_TARGET_ARRAY_INDEX = 0x04,
    VKD3D_SHADER_SV_VIEWPORT_ARRAY_INDEX      = 0x05,
    VKD3D_SHADER_SV_VERTEX_ID                 = 0x06,
    VKD3D_SHADER_SV_PRIMITIVE_ID              = 0x07,
    VKD3D_SHADER_SV_INSTANCE_ID               = 0x08,
    VKD3D_SHADER_SV_IS_FRONT_FACE             = 0x09,
    VKD3D_SHADER_SV_SAMPLE_INDEX              = 0x0a,
    VKD3D_SHADER_SV_TESS_FACTOR_QUADEDGE      = 0x0b,
    VKD3D_SHADER_SV_TESS_FACTOR_QUADINT       = 0x0c,
    VKD3D_SHADER_SV_TESS_FACTOR_TRIEDGE       = 0x0d,
    VKD3D_SHADER_SV_TESS_FACTOR_TRIINT        = 0x0e,
    VKD3D_SHADER_SV_TESS_FACTOR_LINEDET       = 0x0f,
    VKD3D_SHADER_SV_TESS_FACTOR_LINEDEN       = 0x10,

    VKD3D_FORCE_32_BIT_ENUM(VKD3D_SHADER_SYSVAL_SEMANTIC),
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
    enum vkd3d_shader_sysval_semantic sysval_semantic;
    enum vkd3d_shader_component_type component_type;
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
#define VKD3D_SHADER_SWIZZLE_X (0u)
#define VKD3D_SHADER_SWIZZLE_Y (1u)
#define VKD3D_SHADER_SWIZZLE_Z (2u)
#define VKD3D_SHADER_SWIZZLE_W (3u)

#define VKD3D_SHADER_SWIZZLE_MASK (0x3u)
#define VKD3D_SHADER_SWIZZLE_SHIFT(idx) (2u * (idx))

#define VKD3D_SHADER_SWIZZLE(x, y, z, w) \
        (((x & VKD3D_SHADER_SWIZZLE_MASK) << VKD3D_SHADER_SWIZZLE_SHIFT(0)) \
        | ((y & VKD3D_SHADER_SWIZZLE_MASK) << VKD3D_SHADER_SWIZZLE_SHIFT(1)) \
        | ((z & VKD3D_SHADER_SWIZZLE_MASK) << VKD3D_SHADER_SWIZZLE_SHIFT(2)) \
        | ((w & VKD3D_SHADER_SWIZZLE_MASK) << VKD3D_SHADER_SWIZZLE_SHIFT(3)))

#define VKD3D_SHADER_NO_SWIZZLE \
        VKD3D_SHADER_SWIZZLE(VKD3D_SHADER_SWIZZLE_X, VKD3D_SHADER_SWIZZLE_Y, \
        VKD3D_SHADER_SWIZZLE_Z, VKD3D_SHADER_SWIZZLE_W)

#ifndef VKD3D_SHADER_NO_PROTOTYPES

int vkd3d_shader_compile(const struct vkd3d_shader_compile_info *compile_info, struct vkd3d_shader_code *out);
void vkd3d_shader_free_shader_code(struct vkd3d_shader_code *code);

int vkd3d_shader_parse_root_signature(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *root_signature);
void vkd3d_shader_free_root_signature(struct vkd3d_versioned_root_signature_desc *root_signature);

/* FIXME: Add support for returning error messages (ID3DBlob). */
int vkd3d_shader_serialize_root_signature(const struct vkd3d_versioned_root_signature_desc *root_signature,
        struct vkd3d_shader_code *dxbc);

int vkd3d_shader_convert_root_signature(struct vkd3d_versioned_root_signature_desc *dst,
        enum vkd3d_shader_root_signature_version version, const struct vkd3d_versioned_root_signature_desc *src);

int vkd3d_shader_scan_dxbc(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_scan_info *scan_info);
void vkd3d_shader_free_scan_info(struct vkd3d_shader_scan_info *scan_info);

int vkd3d_shader_parse_input_signature(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_signature *signature);
struct vkd3d_shader_signature_element *vkd3d_shader_find_signature_element(
        const struct vkd3d_shader_signature *signature, const char *semantic_name,
        unsigned int semantic_index, unsigned int stream_index);
void vkd3d_shader_free_shader_signature(struct vkd3d_shader_signature *signature);

#endif  /* VKD3D_SHADER_NO_PROTOTYPES */

/*
 * Function pointer typedefs for vkd3d-shader functions.
 */
typedef int (*PFN_vkd3d_shader_compile)(const struct vkd3d_shader_compile_info *compile_info,
        struct vkd3d_shader_code *out);
typedef void (*PFN_vkd3d_shader_free_shader_code)(struct vkd3d_shader_code *code);

typedef int (*PFN_vkd3d_shader_parse_root_signature)(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_versioned_root_signature_desc *root_signature);
typedef void (*PFN_vkd3d_shader_free_root_signature)(struct vkd3d_versioned_root_signature_desc *root_signature);

typedef int (*PFN_vkd3d_shader_serialize_root_signature)(
        const struct vkd3d_versioned_root_signature_desc *root_signature, struct vkd3d_shader_code *dxbc);

typedef int (*PFN_vkd3d_shader_convert_root_signature)(struct vkd3d_versioned_root_signature_desc *dst,
        enum vkd3d_shader_root_signature_version version, const struct vkd3d_versioned_root_signature_desc *src);

typedef int (*PFN_vkd3d_shader_scan_dxbc)(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_scan_info *scan_info);
typedef void (*PFN_vkd3d_shader_free_scan_info)(struct vkd3d_shader_scan_info *scan_info);

typedef int (*PFN_vkd3d_shader_parse_input_signature)(const struct vkd3d_shader_code *dxbc,
        struct vkd3d_shader_signature *signature);
typedef struct vkd3d_shader_signature_element * (*PFN_vkd3d_shader_find_signature_element)(
        const struct vkd3d_shader_signature *signature, const char *semantic_name,
        unsigned int semantic_index, unsigned int stream_index);
typedef void (*PFN_vkd3d_shader_free_shader_signature)(struct vkd3d_shader_signature *signature);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif  /* __VKD3D_SHADER_H */
