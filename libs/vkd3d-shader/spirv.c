/*
 * Copyright 2017 Józef Kucia for CodeWeavers
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

#include "vkd3d_shader_private.h"
#include "rbtree.h"

#include <stdio.h>
#include "spirv/1.0/spirv.h"
#include "spirv/1.0/GLSL.std.450.h"
#ifdef HAVE_SPIRV_TOOLS
# include "spirv-tools/libspirv.h"
#endif /* HAVE_SPIRV_TOOLS */

#ifdef HAVE_SPIRV_TOOLS

static void vkd3d_spirv_dump(const struct vkd3d_shader_code *spirv)
{
    const static uint32_t options
            = SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES | SPV_BINARY_TO_TEXT_OPTION_INDENT;
    spv_diagnostic diagnostic = NULL;
    spv_text text = NULL;
    spv_context context;
    spv_result_t ret;

    context = spvContextCreate(SPV_ENV_VULKAN_1_0);

    if (!(ret = spvBinaryToText(context, spirv->code, spirv->size / sizeof(uint32_t),
            options, &text, &diagnostic)))
    {
        const char *str, *current = text->str;
        while ((str = strchr(current, '\n')))
        {
            TRACE("%.*s\n", (int)(str - current), current);
            current = str + 1;
        }
    }
    else
    {
        TRACE("Failed to convert SPIR-V binary to text, ret %d.\n", ret);
        TRACE("Diagnostic message: %s.\n", debugstr_a(diagnostic->error));
    }

    spvTextDestroy(text);
    spvDiagnosticDestroy(diagnostic);
    spvContextDestroy(context);
}

static void vkd3d_spirv_validate(const struct vkd3d_shader_code *spirv)
{
    spv_diagnostic diagnostic = NULL;
    spv_context context;
    spv_result_t ret;

    context = spvContextCreate(SPV_ENV_VULKAN_1_0);

    if ((ret = spvValidateBinary(context, spirv->code, spirv->size / sizeof(uint32_t),
            &diagnostic)))
    {
        TRACE("Failed to validate SPIR-V binary, ret %d.\n", ret);
        TRACE("Diagnostic message: %s.\n", debugstr_a(diagnostic->error));
    }

    spvDiagnosticDestroy(diagnostic);
    spvContextDestroy(context);
}

#else

static void vkd3d_spirv_dump(const struct vkd3d_shader_code *spirv) {}
static void vkd3d_spirv_validate(const struct vkd3d_shader_code *spirv) {}

#endif /* HAVE_SPIRV_TOOLS */

struct vkd3d_spirv_stream
{
    uint32_t *words;
    size_t capacity;
    size_t word_count;
};

static void vkd3d_spirv_stream_init(struct vkd3d_spirv_stream *stream)
{
    stream->capacity = 256;
    if (!(stream->words = vkd3d_calloc(stream->capacity, sizeof(*stream->words))))
        stream->capacity = 0;
    stream->word_count = 0;
}

static void vkd3d_spirv_stream_free(struct vkd3d_spirv_stream *stream)
{
    vkd3d_free(stream->words);
}

static bool vkd3d_spirv_stream_append(struct vkd3d_spirv_stream *dst_stream,
        const struct vkd3d_spirv_stream *src_stream)
{
    if (!vkd3d_array_reserve((void **)&dst_stream->words, &dst_stream->capacity,
            dst_stream->word_count + src_stream->word_count, sizeof(*dst_stream->words)))
        return false;

    assert(dst_stream->word_count + src_stream->word_count <= dst_stream->capacity);
    memcpy(&dst_stream->words[dst_stream->word_count], src_stream->words,
            src_stream->word_count * sizeof(*src_stream->words));
    dst_stream->word_count += src_stream->word_count;
    return true;
}

struct vkd3d_spirv_builder
{
    uint64_t capability_mask;
    uint32_t ext_instr_set_glsl_450;
    SpvExecutionModel execution_model;

    uint32_t current_id;
    uint32_t main_function_id;
    uint32_t type_id[VKD3D_TYPE_COUNT][VKD3D_VEC4_SIZE];

    struct vkd3d_spirv_stream debug_stream; /* debug instructions */
    struct vkd3d_spirv_stream annotation_stream; /* decoration instructions */
    struct vkd3d_spirv_stream global_stream; /* types, constants, global variables */
    struct vkd3d_spirv_stream function_stream; /* function definitions */

    union
    {
        struct
        {
            uint32_t local_size[3];
        } compute;
    } u;

    /* entry point interface */
    uint32_t *iface;
    size_t iface_capacity;
    size_t iface_element_count;
};

static void vkd3d_spirv_enable_capability(struct vkd3d_spirv_builder *builder,
        SpvCapability cap)
{
    assert(cap < sizeof(builder->capability_mask) * CHAR_BIT);
    builder->capability_mask |= 1ull << cap;
}

static uint32_t vkd3d_spirv_get_glsl_std_450_instr_set(struct vkd3d_spirv_builder *builder)
{
    if (!builder->ext_instr_set_glsl_450)
        builder->ext_instr_set_glsl_450 = builder->current_id++;

    return builder->ext_instr_set_glsl_450;
}

static void vkd3d_spirv_add_iface_variable(struct vkd3d_spirv_builder *builder,
        uint32_t id)
{
    if (!vkd3d_array_reserve((void **)&builder->iface, &builder->iface_capacity,
            builder->iface_element_count + 1, sizeof(*builder->iface)))
        return;

    builder->iface[builder->iface_element_count++] = id;
}

static void vkd3d_spirv_set_execution_model(struct vkd3d_spirv_builder *builder,
        SpvExecutionModel model)
{
    builder->execution_model = model;

    switch (model)
    {
        case SpvExecutionModelVertex:
        case SpvExecutionModelFragment:
        case SpvExecutionModelGLCompute:
            vkd3d_spirv_enable_capability(builder, SpvCapabilityShader);
            break;
        case SpvExecutionModelTessellationControl:
        case SpvExecutionModelTessellationEvaluation:
            vkd3d_spirv_enable_capability(builder, SpvCapabilityTessellation);
            break;
        case SpvExecutionModelGeometry:
            vkd3d_spirv_enable_capability(builder, SpvCapabilityGeometry);
            break;
        default:
            ERR("Unhandled execution model %#x.\n", model);
    }
}

static void vkd3d_spirv_set_local_size(struct vkd3d_spirv_builder *builder,
        unsigned int x, unsigned int y, unsigned int z)
{
    assert(builder->execution_model == SpvExecutionModelGLCompute);

    builder->u.compute.local_size[0] = x;
    builder->u.compute.local_size[1] = y;
    builder->u.compute.local_size[2] = z;
}

static uint32_t vkd3d_spirv_opcode_word(SpvOp op, unsigned int word_count)
{
    assert(!(op & ~SpvOpCodeMask));
    return (word_count << SpvWordCountShift) | op;
}

static void vkd3d_spirv_build_word(struct vkd3d_spirv_stream *stream, uint32_t word)
{
    if (!vkd3d_array_reserve((void **)&stream->words, &stream->capacity,
            stream->word_count + 1, sizeof(*stream->words)))
        return;

    stream->words[stream->word_count++] = word;
}

static unsigned int vkd3d_spirv_string_word_count(const char *str)
{
    return align(strlen(str) + 1, sizeof(uint32_t)) / sizeof(uint32_t);
}

static void vkd3d_spirv_build_string(struct vkd3d_spirv_stream *stream,
        const char *str, unsigned int word_count)
{
    unsigned int word_idx, i;
    const char *ptr = str;

    for (word_idx = 0; word_idx < word_count; ++word_idx)
    {
        uint32_t word = 0;
        for (i = 0; i < sizeof(uint32_t) && *ptr; ++i)
            word |= (uint32_t)*ptr++ << (8 * i);
        vkd3d_spirv_build_word(stream, word);
    }
}

/*
 * vkd3d_spirv_build_op[1-3][v]()
 * vkd3d_spirv_build_op_[t][r][1-3][v]()
 *
 * t   - result type
 * r   - result id
 * 1-3 - the number of operands
 * v   - variable number of operands
 */
static void vkd3d_spirv_build_op(struct vkd3d_spirv_stream *stream, SpvOp op)
{
    vkd3d_spirv_build_word(stream, vkd3d_spirv_opcode_word(op, 1));
}

static void vkd3d_spirv_build_op1(struct vkd3d_spirv_stream *stream,
        SpvOp op, uint32_t operand)
{
    vkd3d_spirv_build_word(stream, vkd3d_spirv_opcode_word(op, 2));
    vkd3d_spirv_build_word(stream, operand);
}

static void vkd3d_spirv_build_op1v(struct vkd3d_spirv_stream *stream,
        SpvOp op, uint32_t operand0, const uint32_t *operands, unsigned int operand_count)
{
    unsigned int i;
    vkd3d_spirv_build_word(stream, vkd3d_spirv_opcode_word(op, 2 + operand_count));
    vkd3d_spirv_build_word(stream, operand0);
    for (i = 0; i < operand_count; ++i)
        vkd3d_spirv_build_word(stream, operands[i]);
}

static void vkd3d_spirv_build_op2v(struct vkd3d_spirv_stream *stream,
        SpvOp op, uint32_t operand0, uint32_t operand1,
        const uint32_t *operands, unsigned int operand_count)
{
    unsigned int i;
    vkd3d_spirv_build_word(stream, vkd3d_spirv_opcode_word(op, 3 + operand_count));
    vkd3d_spirv_build_word(stream, operand0);
    vkd3d_spirv_build_word(stream, operand1);
    for (i = 0; i < operand_count; ++i)
        vkd3d_spirv_build_word(stream, operands[i]);
}

static void vkd3d_spirv_build_op2(struct vkd3d_spirv_stream *stream,
        SpvOp op, uint32_t operand0, uint32_t operand1)
{
    return vkd3d_spirv_build_op2v(stream, op, operand0, operand1, NULL, 0);
}

static void vkd3d_spirv_build_op3(struct vkd3d_spirv_stream *stream,
        SpvOp op, uint32_t operand0, uint32_t operand1, uint32_t operand2)
{
    return vkd3d_spirv_build_op2v(stream, op, operand0, operand1, &operand2, 1);
}

static uint32_t vkd3d_spirv_build_op_rv(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream, SpvOp op,
        const uint32_t *operands, unsigned int operand_count)
{
    uint32_t result_id = builder->current_id++;
    vkd3d_spirv_build_op1v(stream, op, result_id, operands, operand_count);
    return result_id;
}

static uint32_t vkd3d_spirv_build_op_r(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream, SpvOp op)
{
    return vkd3d_spirv_build_op_rv(builder, stream, op, NULL, 0);
}

static uint32_t vkd3d_spirv_build_op_r1(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream, SpvOp op, uint32_t operand0)
{
    return vkd3d_spirv_build_op_rv(builder, stream, op, &operand0, 1);
}

static uint32_t vkd3d_spirv_build_op_r2(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream, SpvOp op, uint32_t operand0, uint32_t operand1)
{
    uint32_t operands[] = {operand0, operand1};
    return vkd3d_spirv_build_op_rv(builder, stream, op, operands, ARRAY_SIZE(operands));
}

static uint32_t vkd3d_spirv_build_op_r1v(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream, SpvOp op, uint32_t operand0,
        const uint32_t *operands, unsigned int operand_count)
{
    uint32_t result_id = builder->current_id++;
    vkd3d_spirv_build_op2v(stream, op, result_id, operand0, operands, operand_count);
    return result_id;
}

static uint32_t vkd3d_spirv_build_op_trv(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream, SpvOp op, uint32_t result_type,
        const uint32_t *operands, unsigned int operand_count)
{
    uint32_t result_id = builder->current_id++;
    vkd3d_spirv_build_op2v(stream, op, result_type, result_id, operands, operand_count);
    return result_id;
}

static uint32_t vkd3d_spirv_build_op_tr(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream, SpvOp op, uint32_t result_type)
{
    return vkd3d_spirv_build_op_trv(builder, stream, op, result_type, NULL, 0);
}

static uint32_t vkd3d_spirv_build_op_tr1(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream, SpvOp op, uint32_t result_type,
        uint32_t operand0)
{
    return vkd3d_spirv_build_op_trv(builder, stream, op, result_type, &operand0, 1);
}

static uint32_t vkd3d_spirv_build_op_tr2(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream, SpvOp op, uint32_t result_type,
        uint32_t operand0, uint32_t operand1)
{
    uint32_t operands[] = {operand0, operand1};
    return vkd3d_spirv_build_op_trv(builder, stream, op, result_type,
            operands, ARRAY_SIZE(operands));
}

static uint32_t vkd3d_spirv_build_op_tr1v(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream, SpvOp op, uint32_t result_type,
        uint32_t operand0, const uint32_t *operands, unsigned int operand_count)
{
    uint32_t result_id = builder->current_id++;
    unsigned int i;
    vkd3d_spirv_build_word(stream, vkd3d_spirv_opcode_word(op, 4 + operand_count));
    vkd3d_spirv_build_word(stream, result_type);
    vkd3d_spirv_build_word(stream, result_id);
    vkd3d_spirv_build_word(stream, operand0);
    for (i = 0; i < operand_count; ++i)
        vkd3d_spirv_build_word(stream, operands[i]);
    return result_id;
}

static uint32_t vkd3d_spirv_build_op_tr2v(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream, SpvOp op, uint32_t result_type,
        uint32_t operand0, uint32_t operand1, const uint32_t *operands, unsigned int operand_count)
{
    uint32_t result_id = builder->current_id++;
    unsigned int i;
    vkd3d_spirv_build_word(stream, vkd3d_spirv_opcode_word(op, 5 + operand_count));
    vkd3d_spirv_build_word(stream, result_type);
    vkd3d_spirv_build_word(stream, result_id);
    vkd3d_spirv_build_word(stream, operand0);
    vkd3d_spirv_build_word(stream, operand1);
    for (i = 0; i < operand_count; ++i)
        vkd3d_spirv_build_word(stream, operands[i]);
    return result_id;
}

static void vkd3d_spirv_build_op_capability(struct vkd3d_spirv_stream *stream,
        SpvCapability cap)
{
    vkd3d_spirv_build_op1(stream, SpvOpCapability, cap);
}

static void vkd3d_spirv_build_op_ext_inst_import(struct vkd3d_spirv_stream *stream,
        uint32_t result_id, const char *name)
{
    unsigned int name_size = vkd3d_spirv_string_word_count(name);
    vkd3d_spirv_build_word(stream, vkd3d_spirv_opcode_word(SpvOpExtInstImport, 2 + name_size));
    vkd3d_spirv_build_word(stream, result_id);
    vkd3d_spirv_build_string(stream, name, name_size);
}

static uint32_t vkd3d_spirv_build_op_ext_inst(struct vkd3d_spirv_builder *builder,
        uint32_t result_type, uint32_t inst_set, uint32_t inst_number,
        unsigned int operand_count, uint32_t *operands)
{
    return vkd3d_spirv_build_op_tr2v(builder, &builder->function_stream,
            SpvOpExtInst, result_type, inst_set, inst_number, operands, operand_count);
}

static void vkd3d_spirv_build_op_memory_model(struct vkd3d_spirv_stream *stream,
        SpvAddressingModel addressing_model, SpvMemoryModel memory_model)
{
    vkd3d_spirv_build_op2(stream, SpvOpMemoryModel, addressing_model, memory_model);
}

static void vkd3d_spirv_build_op_entry_point(struct vkd3d_spirv_stream *stream,
        SpvExecutionModel model, uint32_t function_id, const char *name,
        uint32_t *interface_list, unsigned int interface_size)
{
    unsigned int i, name_size = vkd3d_spirv_string_word_count(name);
    vkd3d_spirv_build_word(stream, vkd3d_spirv_opcode_word(SpvOpEntryPoint, 3 + name_size + interface_size));
    vkd3d_spirv_build_word(stream, model);
    vkd3d_spirv_build_word(stream, function_id);
    vkd3d_spirv_build_string(stream, name, name_size);
    for (i = 0; i < interface_size; ++i)
        vkd3d_spirv_build_word(stream, interface_list[i]);
}

static void vkd3d_spirv_build_op_execution_mode(struct vkd3d_spirv_stream *stream,
        uint32_t entry_point, SpvExecutionMode mode, uint32_t *literals, unsigned int literal_count)
{
    vkd3d_spirv_build_op2v(stream, SpvOpExecutionMode, entry_point, mode, literals, literal_count);
}

static void vkd3d_spirv_build_op_name(struct vkd3d_spirv_builder *builder,
        uint32_t id, const char *name)
{
    unsigned int name_size = vkd3d_spirv_string_word_count(name);
    struct vkd3d_spirv_stream *stream = &builder->debug_stream;
    vkd3d_spirv_build_word(stream, vkd3d_spirv_opcode_word(SpvOpName, 2 + name_size));
    vkd3d_spirv_build_word(stream, id);
    vkd3d_spirv_build_string(stream, name, name_size);
}

static void vkd3d_spirv_build_op_decorate(struct vkd3d_spirv_builder *builder,
        uint32_t target_id, SpvDecoration decoration,
        uint32_t *literals, uint32_t literal_count)
{
    vkd3d_spirv_build_op2v(&builder->annotation_stream,
            SpvOpDecorate, target_id, decoration, literals, literal_count);
}

static void vkd3d_spirv_build_op_decorate1(struct vkd3d_spirv_builder *builder,
        uint32_t target_id, SpvDecoration decoration, uint32_t operand0)
{
    return vkd3d_spirv_build_op_decorate(builder, target_id, decoration, &operand0, 1);
}

static uint32_t vkd3d_spirv_build_op_type_void(struct vkd3d_spirv_builder *builder)
{
    return vkd3d_spirv_build_op_r(builder, &builder->global_stream, SpvOpTypeVoid);
}

static uint32_t vkd3d_spirv_build_op_type_float(struct vkd3d_spirv_builder *builder,
        uint32_t width)
{
    return vkd3d_spirv_build_op_r1(builder, &builder->global_stream, SpvOpTypeFloat, width);
}

static uint32_t vkd3d_spirv_build_op_type_int(struct vkd3d_spirv_builder *builder,
        uint32_t width, uint32_t signedness)
{
    return vkd3d_spirv_build_op_r2(builder, &builder->global_stream, SpvOpTypeInt, width, signedness);
}

static uint32_t vkd3d_spirv_build_op_type_vector(struct vkd3d_spirv_builder *builder,
        uint32_t component_type, uint32_t component_count)
{
    return vkd3d_spirv_build_op_r2(builder, &builder->global_stream,
            SpvOpTypeVector, component_type, component_count);
}

static uint32_t vkd3d_spirv_build_op_type_array(struct vkd3d_spirv_builder *builder,
        uint32_t element_type, uint32_t length_id)
{
    return vkd3d_spirv_build_op_r2(builder, &builder->global_stream,
            SpvOpTypeArray, element_type, length_id);
}

static uint32_t vkd3d_spirv_build_op_type_struct(struct vkd3d_spirv_builder *builder,
        uint32_t *members, unsigned int member_count)
{
    return vkd3d_spirv_build_op_rv(builder, &builder->global_stream,
            SpvOpTypeStruct, members, member_count);
}

static uint32_t vkd3d_spirv_build_op_type_function(struct vkd3d_spirv_builder *builder,
        uint32_t return_type, uint32_t *param_types, unsigned int param_count)
{
    return vkd3d_spirv_build_op_r1v(builder, &builder->global_stream,
            SpvOpTypeFunction, return_type, param_types, param_count);
}

static uint32_t vkd3d_spirv_build_op_type_pointer(struct vkd3d_spirv_builder *builder,
        uint32_t storage_class, uint32_t type_id)
{
    return vkd3d_spirv_build_op_r2(builder, &builder->global_stream,
            SpvOpTypePointer, storage_class, type_id);
}

/* Types larger than 32-bits are not supported. */
static uint32_t vkd3d_spirv_build_op_constant(struct vkd3d_spirv_builder *builder,
        uint32_t result_type, const uint32_t value)
{
    return vkd3d_spirv_build_op_tr1(builder, &builder->global_stream,
            SpvOpConstant, result_type, value);
}

static uint32_t vkd3d_spirv_build_op_constant_composite(struct vkd3d_spirv_builder *builder,
        uint32_t result_type, const uint32_t* constituents, unsigned int constituent_count)
{
    return vkd3d_spirv_build_op_trv(builder, &builder->global_stream,
            SpvOpConstantComposite, result_type, constituents, constituent_count);
}

/* Initializers are not supported. */
static uint32_t vkd3d_spirv_build_op_variable(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream, uint32_t type_id, uint32_t storage_class)
{
    return vkd3d_spirv_build_op_tr1(builder, stream, SpvOpVariable, type_id, storage_class);
}

static uint32_t vkd3d_spirv_build_op_function(struct vkd3d_spirv_builder *builder,
        uint32_t result_type, uint32_t function_control, uint32_t function_type)
{
    return vkd3d_spirv_build_op_tr2(builder, &builder->function_stream,
            SpvOpFunction, result_type, function_control, function_type);
}

static void vkd3d_spirv_build_op_function_end(struct vkd3d_spirv_builder *builder)
{
    vkd3d_spirv_build_op(&builder->function_stream, SpvOpFunctionEnd);
}

static uint32_t vkd3d_spirv_build_op_label(struct vkd3d_spirv_builder *builder)
{
    return vkd3d_spirv_build_op_r(builder, &builder->function_stream, SpvOpLabel);
}

static uint32_t vkd3d_spirv_build_op_undef(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream, uint32_t type_id)
{
    return vkd3d_spirv_build_op_tr(builder, stream, SpvOpUndef, type_id);
}

static uint32_t vkd3d_spirv_build_op_access_chain(struct vkd3d_spirv_builder *builder,
        uint32_t result_type, uint32_t base_id, uint32_t *indexes, uint32_t index_count)
{
    return vkd3d_spirv_build_op_tr1v(builder, &builder->function_stream,
            SpvOpAccessChain, result_type, base_id, indexes, index_count);
}

static uint32_t vkd3d_spirv_build_op_in_bounds_access_chain(struct vkd3d_spirv_builder *builder,
        uint32_t result_type, uint32_t base_id, uint32_t *indexes, uint32_t index_count)
{
    return vkd3d_spirv_build_op_tr1v(builder, &builder->function_stream,
            SpvOpInBoundsAccessChain, result_type, base_id, indexes, index_count);
}

static uint32_t vkd3d_spirv_build_op_vector_shuffle(struct vkd3d_spirv_builder *builder,
        uint32_t result_type, uint32_t vector1_id, uint32_t vector2_id,
        uint32_t *components, uint32_t component_count)
{
    return vkd3d_spirv_build_op_tr2v(builder, &builder->function_stream, SpvOpVectorShuffle,
            result_type, vector1_id, vector2_id, components, component_count);
}

static uint32_t vkd3d_spirv_build_op_load(struct vkd3d_spirv_builder *builder,
        uint32_t result_type, uint32_t pointer_id, uint32_t memory_access)
{
    if (!memory_access)
        return vkd3d_spirv_build_op_tr1(builder, &builder->function_stream, SpvOpLoad,
                result_type, pointer_id);
    else
        return vkd3d_spirv_build_op_tr2(builder, &builder->function_stream, SpvOpLoad,
                result_type, pointer_id, memory_access);
}

static void vkd3d_spirv_build_op_store(struct vkd3d_spirv_builder *builder,
        uint32_t pointer_id, uint32_t object_id, uint32_t memory_access)
{
    if (!memory_access)
        return vkd3d_spirv_build_op2(&builder->function_stream, SpvOpStore,
                pointer_id, object_id);
    else
        return vkd3d_spirv_build_op3(&builder->function_stream, SpvOpStore,
                pointer_id, object_id, memory_access);
}

static void vkd3d_spirv_build_op_return(struct vkd3d_spirv_builder *builder)
{
    vkd3d_spirv_build_op(&builder->function_stream, SpvOpReturn);
}

static uint32_t vkd3d_spirv_build_op_and(struct vkd3d_spirv_builder *builder,
        uint32_t result_type, uint32_t operand0, uint32_t operand1)
{
    return vkd3d_spirv_build_op_tr2(builder, &builder->function_stream,
            SpvOpBitwiseAnd, result_type, operand0, operand1);
}

static uint32_t vkd3d_spirv_build_op_bitcast(struct vkd3d_spirv_builder *builder,
        uint32_t result_type, uint32_t operand)
{
    return vkd3d_spirv_build_op_tr1(builder, &builder->function_stream,
            SpvOpBitcast, result_type, operand);
}

static uint32_t vkd3d_spirv_get_type_id(struct vkd3d_spirv_builder *builder,
        enum vkd3d_component_type component_type, unsigned int component_count)
{
    uint32_t id, scalar_id;

    assert(0 < component_count && component_count <= VKD3D_VEC4_SIZE);

    if (!(id = builder->type_id[component_type][component_count - 1]))
    {
        if (component_count == 1)
        {
            switch (component_type)
            {
                case VKD3D_TYPE_VOID:
                    id = vkd3d_spirv_build_op_type_void(builder);
                    break;
                case VKD3D_TYPE_FLOAT:
                    id = vkd3d_spirv_build_op_type_float(builder, 32);
                    break;
                case VKD3D_TYPE_INT:
                case VKD3D_TYPE_UINT:
                    id = vkd3d_spirv_build_op_type_int(builder, 32, component_type == VKD3D_TYPE_INT);
                    break;
                default:
                    FIXME("Unhandled component type %#x.\n", component_type);
                    id = 0;
            }
        }
        else
        {
            assert(component_type != VKD3D_TYPE_VOID);
            scalar_id = vkd3d_spirv_get_type_id(builder, component_type, 1);
            id = vkd3d_spirv_build_op_type_vector(builder, scalar_id, component_count);
        }

        builder->type_id[component_type][component_count - 1] = id;
    }

    return id;
}

static void vkd3d_spirv_builder_init(struct vkd3d_spirv_builder *builder)
{
    uint32_t void_id, function_type_id;

    vkd3d_spirv_stream_init(&builder->debug_stream);
    vkd3d_spirv_stream_init(&builder->annotation_stream);
    vkd3d_spirv_stream_init(&builder->global_stream);
    vkd3d_spirv_stream_init(&builder->function_stream);

    builder->current_id = 1;

    void_id = vkd3d_spirv_get_type_id(builder, VKD3D_TYPE_VOID, 1);
    function_type_id = vkd3d_spirv_build_op_type_function(builder, void_id, NULL, 0);

    builder->main_function_id = vkd3d_spirv_build_op_function(builder, void_id,
            SpvFunctionControlMaskNone, function_type_id);
    vkd3d_spirv_build_op_name(builder, builder->main_function_id, "main");
    vkd3d_spirv_build_op_label(builder);
}

static void vkd3d_spirv_builder_free(struct vkd3d_spirv_builder *builder)
{
    vkd3d_spirv_stream_free(&builder->debug_stream);
    vkd3d_spirv_stream_free(&builder->annotation_stream);
    vkd3d_spirv_stream_free(&builder->global_stream);
    vkd3d_spirv_stream_free(&builder->function_stream);

    vkd3d_free(builder->iface);
}

static void vkd3d_spirv_build_execution_mode_declarations(struct vkd3d_spirv_builder *builder,
        struct vkd3d_spirv_stream *stream)
{
    if (builder->execution_model == SpvExecutionModelGLCompute)
    {
        vkd3d_spirv_build_op_execution_mode(stream, builder->main_function_id, SpvExecutionModeLocalSize,
                builder->u.compute.local_size, ARRAY_SIZE(builder->u.compute.local_size));
    }
}

static bool vkd3d_spirv_compile_module(struct vkd3d_spirv_builder *builder,
        struct vkd3d_shader_code *spirv)
{
    uint64_t capability_mask = builder->capability_mask;
    struct vkd3d_spirv_stream stream;
    uint32_t *code;
    unsigned int i;
    size_t size;

    vkd3d_spirv_build_op_function_end(builder);

    vkd3d_spirv_stream_init(&stream);

    vkd3d_spirv_build_word(&stream, SpvMagicNumber);
    vkd3d_spirv_build_word(&stream, SpvVersion);
    vkd3d_spirv_build_word(&stream, 0); /* generator */
    vkd3d_spirv_build_word(&stream, builder->current_id); /* bound */
    vkd3d_spirv_build_word(&stream, 0); /* schema, reserved */

    for (i = 0; capability_mask; ++i)
    {
        if (capability_mask & 1)
            vkd3d_spirv_build_op_capability(&stream, i);
        capability_mask >>= 1;
    }

    if (builder->ext_instr_set_glsl_450)
        vkd3d_spirv_build_op_ext_inst_import(&stream, builder->ext_instr_set_glsl_450, "GLSL.std.450");

    vkd3d_spirv_build_op_memory_model(&stream, SpvAddressingModelLogical, SpvMemoryModelGLSL450);
    vkd3d_spirv_build_op_entry_point(&stream, builder->execution_model, builder->main_function_id,
            "main", builder->iface, builder->iface_element_count);

    vkd3d_spirv_build_execution_mode_declarations(builder, &stream);

    vkd3d_spirv_stream_append(&stream, &builder->debug_stream);
    vkd3d_spirv_stream_append(&stream, &builder->annotation_stream);
    vkd3d_spirv_stream_append(&stream, &builder->global_stream);
    vkd3d_spirv_stream_append(&stream, &builder->function_stream);

    if (!(code = vkd3d_calloc(stream.word_count, sizeof(*code))))
    {
        vkd3d_spirv_stream_free(&stream);
        return false;
    }

    size = stream.word_count * sizeof(*code);
    memcpy(code, stream.words, size);
    vkd3d_spirv_stream_free(&stream);

    spirv->code = code;
    spirv->size = size;

    return true;
}

struct vkd3d_symbol_pointer_type
{
    uint32_t type_id;
    SpvStorageClass storage_class;
};

struct vkd3d_symbol_constant
{
    unsigned int component_count;
    enum vkd3d_component_type component_type;
    union
    {
        float f[VKD3D_VEC4_SIZE];
        uint32_t u[VKD3D_VEC4_SIZE];
    } value;
};

struct vkd3d_symbol_register
{
    enum vkd3d_shader_register_type type;
    unsigned int idx;
};

struct vkd3d_symbol
{
    struct rb_entry entry;

    enum
    {
        VKD3D_SYMBOL_POINTER_TYPE,
        VKD3D_SYMBOL_CONSTANT,
        VKD3D_SYMBOL_REGISTER,
    } type;

    union
    {
        struct vkd3d_symbol_pointer_type pointer_type;
        struct vkd3d_symbol_constant constant;
        struct vkd3d_symbol_register reg;
    } key;

    uint32_t id;
    union
    {
        SpvStorageClass storage_class;
    } info;
};

static int vkd3d_symbol_compare(const void *key, const struct rb_entry *entry)
{
    const struct vkd3d_symbol *a = key;
    const struct vkd3d_symbol *b = RB_ENTRY_VALUE(entry, const struct vkd3d_symbol, entry);

    if (a->type != b->type)
        return a->type - b->type;
    return memcmp(&a->key, &b->key, sizeof(a->key));
}

static void vkd3d_symbol_free(struct rb_entry *entry, void *context)
{
    struct vkd3d_symbol *s = RB_ENTRY_VALUE(entry, struct vkd3d_symbol, entry);

    vkd3d_free(s);
}

static void vkd3d_symbol_make_pointer_type(struct vkd3d_symbol *symbol,
        uint32_t type_id, SpvStorageClass storage_class)
{
    symbol->type = VKD3D_SYMBOL_POINTER_TYPE;
    memset(&symbol->key, 0, sizeof(symbol->key));
    symbol->key.pointer_type.type_id = type_id;
    symbol->key.pointer_type.storage_class = storage_class;
}

static void vkd3d_symbol_make_constant(struct vkd3d_symbol *symbol,
        enum vkd3d_component_type component_type, unsigned int component_count, const uint32_t *values)
{
    symbol->type = VKD3D_SYMBOL_CONSTANT;
    memset(&symbol->key, 0, sizeof(symbol->key));
    symbol->key.constant.component_type = component_type;
    symbol->key.constant.component_count = component_count;
    memcpy(symbol->key.constant.value.u, values, sizeof(*values) * component_count);
}

static void vkd3d_symbol_make_register(struct vkd3d_symbol *symbol,
        const struct vkd3d_shader_register *reg)
{
    symbol->type = VKD3D_SYMBOL_REGISTER;
    memset(&symbol->key, 0, sizeof(symbol->key));
    symbol->key.reg.type = reg->type;
    symbol->key.reg.idx = reg->idx[0].offset;
}

static struct vkd3d_symbol *vkd3d_symbol_dup(const struct vkd3d_symbol *symbol)
{
    struct vkd3d_symbol *s;

    if (!(s = vkd3d_malloc(sizeof(*s))))
        return NULL;

    return memcpy(s, symbol, sizeof(*s));
}

struct vkd3d_dxbc_compiler
{
    struct vkd3d_spirv_builder spirv_builder;

    uint32_t options;

    struct rb_tree symbol_table;
    uint32_t temp_id;
    unsigned int temp_count;
};

struct vkd3d_dxbc_compiler *vkd3d_dxbc_compiler_create(const struct vkd3d_shader_version *shader_version,
        uint32_t compiler_options)
{
    struct vkd3d_dxbc_compiler *compiler;

    if (!(compiler = vkd3d_malloc(sizeof(*compiler))))
        return NULL;

    memset(compiler, 0, sizeof(*compiler));
    vkd3d_spirv_builder_init(&compiler->spirv_builder);
    compiler->options = compiler_options;

    rb_init(&compiler->symbol_table, vkd3d_symbol_compare);

    switch (shader_version->type)
    {
        case VKD3D_SHADER_TYPE_VERTEX:
            vkd3d_spirv_set_execution_model(&compiler->spirv_builder, SpvExecutionModelVertex);
            break;
        case VKD3D_SHADER_TYPE_HULL:
            vkd3d_spirv_set_execution_model(&compiler->spirv_builder, SpvExecutionModelTessellationControl);
            break;
        case VKD3D_SHADER_TYPE_DOMAIN:
            vkd3d_spirv_set_execution_model(&compiler->spirv_builder, SpvExecutionModelTessellationEvaluation);
            break;
        case VKD3D_SHADER_TYPE_GEOMETRY:
            vkd3d_spirv_set_execution_model(&compiler->spirv_builder, SpvExecutionModelGeometry);
            break;
        case VKD3D_SHADER_TYPE_PIXEL:
            vkd3d_spirv_set_execution_model(&compiler->spirv_builder, SpvExecutionModelFragment);
            break;
        case VKD3D_SHADER_TYPE_COMPUTE:
            vkd3d_spirv_set_execution_model(&compiler->spirv_builder, SpvExecutionModelGLCompute);
            break;
        default:
            ERR("Invalid shader type %#x.\n", shader_version->type);
    }

    return compiler;
}

static void vkd3d_dxbc_compiler_put_symbol(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_symbol *symbol)
{
    struct vkd3d_symbol *s;

    s = vkd3d_symbol_dup(symbol);
    if (rb_put(&compiler->symbol_table, s, &s->entry) == -1)
    {
        ERR("Failed to insert symbol entry.\n");
        vkd3d_free(s);
    }
}

static uint32_t vkd3d_dxbc_compiler_get_pointer_type(struct vkd3d_dxbc_compiler *compiler,
        uint32_t type_id, SpvStorageClass storage_class)
{
    struct vkd3d_symbol pointer_type;
    struct rb_entry *entry;

    vkd3d_symbol_make_pointer_type(&pointer_type, type_id, storage_class);
    if ((entry = rb_get(&compiler->symbol_table, &pointer_type)))
        return RB_ENTRY_VALUE(entry, struct vkd3d_symbol, entry)->id;

    pointer_type.id = vkd3d_spirv_build_op_type_pointer(&compiler->spirv_builder, storage_class, type_id);
    vkd3d_dxbc_compiler_put_symbol(compiler, &pointer_type);
    return pointer_type.id;
}

static uint32_t vkd3d_dxbc_compiler_get_constant(struct vkd3d_dxbc_compiler *compiler,
        enum vkd3d_component_type component_type, unsigned int component_count, const uint32_t *values)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    uint32_t type_id = vkd3d_spirv_get_type_id(builder, component_type, component_count);
    uint32_t component_ids[VKD3D_VEC4_SIZE];
    struct vkd3d_symbol constant;
    struct rb_entry *entry;
    unsigned int i;

    assert(0 < component_count && component_count <= VKD3D_VEC4_SIZE);

    vkd3d_symbol_make_constant(&constant, component_type, component_count, values);
    if ((entry = rb_get(&compiler->symbol_table, &constant)))
        return RB_ENTRY_VALUE(entry, struct vkd3d_symbol, entry)->id;

    switch (component_type)
    {
        case VKD3D_TYPE_UINT:
        case VKD3D_TYPE_INT:
        case VKD3D_TYPE_FLOAT:
            break;
        default:
            FIXME("Unhandled component_type %#x.\n", component_type);
            return vkd3d_spirv_build_op_undef(builder, &builder->global_stream, type_id);
    }

    if (component_count == 1)
    {
        constant.id = vkd3d_spirv_build_op_constant(builder, type_id, *values);
    }
    else
    {
        for (i = 0; i < component_count; ++i)
            component_ids[i] = vkd3d_dxbc_compiler_get_constant(compiler, component_type, 1, &values[i]);
        constant.id = vkd3d_spirv_build_op_constant_composite(builder, type_id, component_ids, component_count);
    }

    vkd3d_dxbc_compiler_put_symbol(compiler, &constant);

    return constant.id;
}

static uint32_t vkd3d_dxbc_compiler_get_constant_uint(struct vkd3d_dxbc_compiler *compiler,
        uint32_t value)
{
    return vkd3d_dxbc_compiler_get_constant(compiler, VKD3D_TYPE_UINT, 1, &value);
}

static bool vkd3d_dxbc_compiler_get_register_name(char *buffer,const struct vkd3d_shader_register *reg)
{
    switch (reg->type)
    {
        case VKD3DSPR_CONSTBUFFER:
            sprintf(buffer, "cb%u_%u", reg->idx[0].offset, reg->idx[1].offset);
            break;
        case VKD3DSPR_INPUT:
            sprintf(buffer, "v%u", reg->idx[0].offset);
            break;
        case VKD3DSPR_OUTPUT:
        case VKD3DSPR_COLOROUT:
            sprintf(buffer, "o%u", reg->idx[0].offset);
            break;
        default:
            FIXME("Unhandled register %#x.\n", reg->type);
            sprintf(buffer, "unrecognized_%#x", reg->type);
            return false;
    }

    return true;
}

static void vkd3d_dxbc_compiler_emit_register_debug_name(struct vkd3d_spirv_builder *builder,
        uint32_t id, const struct vkd3d_shader_register *reg)
{
    char debug_name[256];
    if (vkd3d_dxbc_compiler_get_register_name(debug_name, reg))
        vkd3d_spirv_build_op_name(builder, id, debug_name);
}

static uint32_t vkd3d_dxbc_compiler_emit_variable(struct vkd3d_dxbc_compiler *compiler,
        struct vkd3d_spirv_stream *stream, SpvStorageClass storage_class,
        enum vkd3d_component_type component_type, unsigned int component_count)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    uint32_t type_id, ptr_type_id;

    type_id = vkd3d_spirv_get_type_id(builder, component_type, component_count);
    ptr_type_id = vkd3d_dxbc_compiler_get_pointer_type(compiler, type_id, storage_class);
    return vkd3d_spirv_build_op_variable(builder, stream, ptr_type_id, storage_class);
}

static uint32_t vkd3d_dxbc_compiler_emit_undef(struct vkd3d_dxbc_compiler *compiler,
        struct vkd3d_spirv_stream *stream, const struct vkd3d_shader_register *reg)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    uint32_t type_id, ptr_type_id;

    type_id = vkd3d_spirv_get_type_id(builder, VKD3D_TYPE_FLOAT, VKD3D_VEC4_SIZE);
    ptr_type_id = vkd3d_dxbc_compiler_get_pointer_type(compiler, type_id, SpvStorageClassPrivate);
    return vkd3d_spirv_build_op_undef(builder, stream, ptr_type_id);
}

struct vkd3d_dxbc_register_info
{
    uint32_t id;
    SpvStorageClass storage_class;
};

static void vkd3d_dxbc_compiler_get_register_info(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_register *reg, struct vkd3d_dxbc_register_info *register_info)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    struct vkd3d_symbol reg_symbol, *symbol;
    struct rb_entry *entry;

    assert(reg->type != VKD3DSPR_IMMCONST);

    if (reg->idx[0].rel_addr || reg->idx[1].rel_addr)
        FIXME("Relative indexing not implemented.\n");

    if (reg->type == VKD3DSPR_TEMP)
    {
        assert(reg->idx[0].offset < compiler->temp_count);
        register_info->id = compiler->temp_id + reg->idx[0].offset;
        register_info->storage_class = SpvStorageClassFunction;
        return;
    }

    vkd3d_symbol_make_register(&reg_symbol, reg);
    entry = rb_get(&compiler->symbol_table, &reg_symbol);
    assert(entry);
    symbol = RB_ENTRY_VALUE(entry, struct vkd3d_symbol, entry);
    register_info->id = symbol->id;
    register_info->storage_class = symbol->info.storage_class;

    if (reg->type == VKD3DSPR_CONSTBUFFER)
    {
        uint32_t type_id, ptr_type_id;
        uint32_t indexes[] =
        {
            vkd3d_dxbc_compiler_get_constant_uint(compiler, 0),
            vkd3d_dxbc_compiler_get_constant_uint(compiler, reg->idx[1].offset),
        };

        type_id = vkd3d_spirv_get_type_id(builder, VKD3D_TYPE_FLOAT, VKD3D_VEC4_SIZE);
        ptr_type_id = vkd3d_dxbc_compiler_get_pointer_type(compiler,
                type_id, register_info->storage_class);
        register_info->id = vkd3d_spirv_build_op_access_chain(builder, ptr_type_id,
                register_info->id, indexes, ARRAY_SIZE(indexes));
    }
}

static uint32_t vkd3d_dxbc_compiler_get_register_id(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_register *reg)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    struct vkd3d_dxbc_register_info register_info;
    unsigned int component_count;

    switch (reg->type)
    {
        case VKD3DSPR_TEMP:
        case VKD3DSPR_INPUT:
        case VKD3DSPR_OUTPUT:
        case VKD3DSPR_COLOROUT:
        case VKD3DSPR_CONSTBUFFER:
            vkd3d_dxbc_compiler_get_register_info(compiler, reg, &register_info);
            return register_info.id;
        case VKD3DSPR_IMMCONST:
            if (reg->immconst_type == VKD3D_IMMCONST_SCALAR)
            {
                component_count = 1;
            }
            else if (reg->immconst_type == VKD3D_IMMCONST_VEC4)
            {
                component_count = VKD3D_VEC4_SIZE;
            }
            else
            {
                FIXME("Unhandled immconst type %#x.\n", reg->immconst_type);
                component_count = 1;
            }
            return vkd3d_dxbc_compiler_get_constant(compiler,
                    vkd3d_component_type_from_data_type(reg->data_type),
                    component_count, reg->u.immconst_data);
        default:
            FIXME("Unhandled register type %#x.\n", reg->type);
            return vkd3d_dxbc_compiler_emit_undef(compiler, &builder->global_stream, reg);
    }
}

static uint32_t vkd3d_dxbc_compiler_emit_load_constant(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_register *reg, DWORD swizzle, DWORD write_mask)
{
    unsigned int component_count = vkd3d_write_mask_component_count(write_mask);
    uint32_t values[VKD3D_VEC4_SIZE];
    unsigned int i, j;

    assert(reg->type == VKD3DSPR_IMMCONST);

    if (component_count == 1 || component_count == VKD3D_VEC4_SIZE)
    {
        assert(component_count != 1 || reg->immconst_type == VKD3D_IMMCONST_SCALAR);
        return vkd3d_dxbc_compiler_get_register_id(compiler, reg);
    }

    assert(reg->immconst_type == VKD3D_IMMCONST_VEC4);

    for (i = 0, j = 0; i < VKD3D_VEC4_SIZE; ++i)
    {
        if (write_mask & (VKD3DSP_WRITEMASK_0 << i))
            values[j++] = reg->u.immconst_data[vkd3d_swizzle_get_component(swizzle, i)];
    }

    return vkd3d_dxbc_compiler_get_constant(compiler,
            vkd3d_component_type_from_data_type(reg->data_type), component_count, values);
}

static uint32_t vkd3d_dxbc_compiler_emit_load_scalar(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_register *reg, DWORD swizzle, DWORD write_mask)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    uint32_t type_id, ptr_type_id, indexes[1], chain_id, val_id;
    struct vkd3d_dxbc_register_info reg_info;
    unsigned int component_idx;

    assert(reg->type != VKD3DSPR_IMMCONST);
    assert(vkd3d_write_mask_component_count(write_mask) == 1);

    component_idx = vkd3d_write_mask_get_component_idx(write_mask);
    component_idx = vkd3d_swizzle_get_component(swizzle, component_idx);

    vkd3d_dxbc_compiler_get_register_info(compiler, reg, &reg_info);

    type_id = vkd3d_spirv_get_type_id(builder, VKD3D_TYPE_FLOAT, 1);
    ptr_type_id = vkd3d_dxbc_compiler_get_pointer_type(compiler, type_id, reg_info.storage_class);
    indexes[0] = vkd3d_dxbc_compiler_get_constant_uint(compiler, component_idx);
    chain_id = vkd3d_spirv_build_op_in_bounds_access_chain(builder,
            ptr_type_id, reg_info.id, indexes, ARRAY_SIZE(indexes));

    val_id = vkd3d_spirv_build_op_load(builder, type_id, chain_id, SpvMemoryAccessMaskNone);

    if (reg->data_type != VKD3D_DATA_FLOAT)
    {
        type_id = vkd3d_spirv_get_type_id(builder,
                vkd3d_component_type_from_data_type(reg->data_type), 1);
        val_id = vkd3d_spirv_build_op_bitcast(builder, type_id, val_id);
    }

    return val_id;
}

static uint32_t vkd3d_dxbc_compiler_emit_load_reg(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_register *reg, DWORD swizzle, DWORD write_mask)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    uint32_t src_id, type_id, val_id, components[VKD3D_VEC4_SIZE];
    unsigned int i, component_idx, component_count;

    if (reg->type == VKD3DSPR_IMMCONST)
        return vkd3d_dxbc_compiler_emit_load_constant(compiler, reg, swizzle, write_mask);

    component_count = vkd3d_write_mask_component_count(write_mask);
    if (component_count == 1)
        return vkd3d_dxbc_compiler_emit_load_scalar(compiler, reg, swizzle, write_mask);

    src_id = vkd3d_dxbc_compiler_get_register_id(compiler, reg);
    type_id = vkd3d_spirv_get_type_id(builder, VKD3D_TYPE_FLOAT, VKD3D_VEC4_SIZE);
    val_id = vkd3d_spirv_build_op_load(builder, type_id, src_id, SpvMemoryAccessMaskNone);

    if (component_count != VKD3D_VEC4_SIZE || swizzle != VKD3DSP_NOSWIZZLE)
    {
        type_id = vkd3d_spirv_get_type_id(builder, VKD3D_TYPE_FLOAT, component_count);

        for (i = 0, component_idx = 0; i < ARRAY_SIZE(components); ++i)
        {
            if (write_mask & (VKD3DSP_WRITEMASK_0 << i))
                components[component_idx++] = vkd3d_swizzle_get_component(swizzle, i);
        }

        val_id = vkd3d_spirv_build_op_vector_shuffle(builder,
                type_id, val_id, val_id, components, component_count);
    }

    if (reg->data_type != VKD3D_DATA_FLOAT)
    {
        type_id = vkd3d_spirv_get_type_id(builder,
                vkd3d_component_type_from_data_type(reg->data_type), component_count);
        val_id = vkd3d_spirv_build_op_bitcast(builder, type_id, val_id);
    }

    return val_id;
}

static void vkd3d_dxbc_compiler_emit_store_scalar(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_register *reg, DWORD write_mask, uint32_t val_id)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    uint32_t type_id, ptr_type_id, chain_id, index[1];
    struct vkd3d_dxbc_register_info reg_info;
    unsigned int component_idx;

    assert(reg->type != VKD3DSPR_IMMCONST);

    vkd3d_dxbc_compiler_get_register_info(compiler, reg, &reg_info);

    type_id = vkd3d_spirv_get_type_id(builder, VKD3D_TYPE_FLOAT, 1);
    ptr_type_id = vkd3d_dxbc_compiler_get_pointer_type(compiler,
            type_id, reg_info.storage_class);
    component_idx = vkd3d_write_mask_get_component_idx(write_mask);
    index[0] = vkd3d_dxbc_compiler_get_constant_uint(compiler, component_idx);
    chain_id = vkd3d_spirv_build_op_in_bounds_access_chain(builder,
            ptr_type_id, reg_info.id, index, ARRAY_SIZE(index));

    vkd3d_spirv_build_op_store(builder, chain_id, val_id, SpvMemoryAccessMaskNone);
}

static void vkd3d_dxbc_compiler_emit_store_reg(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_register *reg, DWORD write_mask, uint32_t val_id)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    unsigned int i, component_idx, component_count;
    uint32_t components[VKD3D_VEC4_SIZE];
    uint32_t reg_id;

    assert(reg->type != VKD3DSPR_IMMCONST);

    component_count = vkd3d_write_mask_component_count(write_mask);

    if (reg->data_type != VKD3D_DATA_FLOAT)
    {
        uint32_t type_id = vkd3d_spirv_get_type_id(builder, VKD3D_TYPE_FLOAT, component_count);
        val_id = vkd3d_spirv_build_op_bitcast(builder, type_id, val_id);
    }

    if (component_count == 1)
        return vkd3d_dxbc_compiler_emit_store_scalar(compiler, reg, write_mask, val_id);

    if (component_count != VKD3D_VEC4_SIZE)
    {
        uint32_t type_id, reg_val_id;

        type_id = vkd3d_spirv_get_type_id(builder, VKD3D_TYPE_FLOAT, VKD3D_VEC4_SIZE);

        reg_val_id = vkd3d_dxbc_compiler_emit_load_reg(compiler,
                reg, VKD3DSP_NOSWIZZLE, VKD3DSP_WRITEMASK_ALL);

        for (i = 0, component_idx = 0; i < ARRAY_SIZE(components); ++i)
        {
            if (write_mask & (VKD3DSP_WRITEMASK_0 << i))
                components[i] = VKD3D_VEC4_SIZE + component_idx++;
            else
                components[i] = i;
        }

        val_id = vkd3d_spirv_build_op_vector_shuffle(builder,
                type_id, reg_val_id, val_id, components, ARRAY_SIZE(components));
    }

    reg_id = vkd3d_dxbc_compiler_get_register_id(compiler, reg);
    vkd3d_spirv_build_op_store(builder, reg_id, val_id, SpvMemoryAccessMaskNone);
}

static void vkd3d_dxbc_compiler_decorate_sysval(struct vkd3d_spirv_builder *builder,
        uint32_t target_id, enum vkd3d_shader_input_sysval_semantic sysval)
{
    SpvBuiltIn builtin;

    switch (sysval)
    {
        case VKD3D_SIV_POSITION:
            builtin = SpvBuiltInPosition;
            break;
        case VKD3D_SIV_VERTEX_ID:
            builtin = SpvBuiltInVertexIndex;
            break;
        default:
            FIXME("Unhandled semantic %#x.\n", sysval);
            return;
    }

    vkd3d_spirv_build_op_decorate1(builder, target_id, SpvDecorationBuiltIn, builtin);
}

static void vkd3d_dxbc_compiler_emit_input(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_dst_param *dst, enum vkd3d_shader_input_sysval_semantic sysval)
{
    enum vkd3d_component_type component_type = vkd3d_component_type_for_semantic(sysval);
    unsigned int component_count = vkd3d_write_mask_component_count(dst->write_mask);
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    struct vkd3d_symbol reg_symbol;
    SpvStorageClass storage_class;
    uint32_t val_id, var_id;

    storage_class = SpvStorageClassInput;
    var_id = vkd3d_dxbc_compiler_emit_variable(compiler, &builder->global_stream,
            storage_class, component_type, component_count);
    vkd3d_spirv_add_iface_variable(builder, var_id);
    if (sysval)
        vkd3d_dxbc_compiler_decorate_sysval(builder, var_id, sysval);
    else
        vkd3d_spirv_build_op_decorate1(builder, var_id, SpvDecorationLocation, dst->reg.idx[0].offset);

    if (component_type != VKD3D_TYPE_FLOAT)
    {
        uint32_t float_type_id = vkd3d_spirv_get_type_id(builder, VKD3D_TYPE_FLOAT, component_count);
        assert(component_count == 1);
        val_id = vkd3d_spirv_build_op_bitcast(builder, float_type_id, var_id);
    }
    else if (component_count != 4)
    {
        uint32_t type_id = vkd3d_spirv_get_type_id(builder, component_type, component_count);
        val_id = vkd3d_spirv_build_op_load(builder, type_id, var_id, SpvMemoryAccessMaskNone);
    }

    /* FIXME: handle multiple inputs packed into a single register */
    if (component_count != 4 || component_type != VKD3D_TYPE_FLOAT)
    {
        storage_class = SpvStorageClassPrivate;
        var_id = vkd3d_dxbc_compiler_emit_variable(compiler, &builder->global_stream,
                storage_class, VKD3D_TYPE_FLOAT, VKD3D_VEC4_SIZE);
    }

    vkd3d_symbol_make_register(&reg_symbol, &dst->reg);
    reg_symbol.id = var_id;
    reg_symbol.info.storage_class = storage_class;
    vkd3d_dxbc_compiler_put_symbol(compiler, &reg_symbol);

    vkd3d_dxbc_compiler_emit_register_debug_name(builder, var_id, &dst->reg);

    if (component_count != 4 || component_type != VKD3D_TYPE_FLOAT)
        vkd3d_dxbc_compiler_emit_store_reg(compiler, &dst->reg, dst->write_mask, val_id);
}

static uint32_t vkd3d_dxbc_compiler_emit_output(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_dst_param *dst, enum vkd3d_shader_input_sysval_semantic sysval)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    const struct vkd3d_shader_register *reg = &dst->reg;
    struct vkd3d_symbol reg_symbol;
    SpvStorageClass storage_class;
    uint32_t id;

    storage_class = SpvStorageClassOutput;

    id = vkd3d_dxbc_compiler_emit_variable(compiler, &builder->global_stream,
            storage_class, VKD3D_TYPE_FLOAT, VKD3D_VEC4_SIZE);
    vkd3d_spirv_add_iface_variable(builder, id);
    if (sysval)
        vkd3d_dxbc_compiler_decorate_sysval(builder, id, sysval);
    else
        vkd3d_spirv_build_op_decorate1(builder, id, SpvDecorationLocation, reg->idx[0].offset);

    vkd3d_dxbc_compiler_emit_register_debug_name(builder, id, reg);

    vkd3d_symbol_make_register(&reg_symbol, &dst->reg);
    reg_symbol.id = id;
    reg_symbol.info.storage_class = storage_class;
    vkd3d_dxbc_compiler_put_symbol(compiler, &reg_symbol);

    return id;
}

static void vkd3d_dxbc_compiler_emit_dcl_temps(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    uint32_t type_id, ptr_type_id, id;
    char debug_name[100];
    unsigned int i;

    type_id = vkd3d_spirv_get_type_id(builder, VKD3D_TYPE_FLOAT, VKD3D_VEC4_SIZE);
    ptr_type_id = vkd3d_dxbc_compiler_get_pointer_type(compiler, type_id, SpvStorageClassFunction);

    assert(!compiler->temp_count);
    compiler->temp_count = instruction->declaration.count;
    for (i = 0; i < compiler->temp_count; ++i)
    {
        id = vkd3d_spirv_build_op_variable(builder, &builder->function_stream,
                ptr_type_id, SpvStorageClassFunction);
        if (!i)
            compiler->temp_id = id;
        assert(id == compiler->temp_id + i);

        sprintf(debug_name, "r%u", i);
        vkd3d_spirv_build_op_name(builder, id, debug_name);
    }
}

static void vkd3d_dxbc_compiler_emit_dcl_constant_buffer(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    uint32_t vec4_id, array_type_id, length_id, struct_id, pointer_type_id, var_id;
    const struct vkd3d_shader_src_param *src = &instruction->declaration.src;
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    struct vkd3d_symbol reg_symbol;
    unsigned int cb_idx, cb_size;
    char debug_name[100];

    assert(!(instruction->flags & ~VKD3DSI_INDEXED_DYNAMIC));

    if (instruction->flags & VKD3DSI_INDEXED_DYNAMIC)
        vkd3d_spirv_enable_capability(builder, SpvCapabilityUniformBufferArrayDynamicIndexing);

    cb_idx = src->reg.idx[0].offset;
    cb_size = src->reg.idx[1].offset;

    vec4_id = vkd3d_spirv_get_type_id(builder, VKD3D_TYPE_FLOAT, VKD3D_VEC4_SIZE);
    length_id = vkd3d_dxbc_compiler_get_constant_uint(compiler, cb_size);
    array_type_id = vkd3d_spirv_build_op_type_array(builder, vec4_id, length_id);
    vkd3d_spirv_build_op_decorate1(builder, array_type_id, SpvDecorationArrayStride, 16);

    struct_id = vkd3d_spirv_build_op_type_struct(builder, &array_type_id, 1);
    vkd3d_spirv_build_op_decorate(builder, struct_id, SpvDecorationBlock, NULL, 0);
    sprintf(debug_name, "cb%u_struct", cb_size);
    vkd3d_spirv_build_op_name(builder, struct_id, debug_name);

    pointer_type_id = vkd3d_spirv_build_op_type_pointer(builder, SpvStorageClassUniform, struct_id);
    var_id = vkd3d_spirv_build_op_variable(builder, &builder->global_stream,
            pointer_type_id, SpvStorageClassUniform);

    vkd3d_spirv_build_op_decorate1(builder, var_id, SpvDecorationDescriptorSet, 0);
    vkd3d_spirv_build_op_decorate1(builder, var_id, SpvDecorationBinding, cb_idx);

    vkd3d_dxbc_compiler_emit_register_debug_name(builder, var_id, &src->reg);

    vkd3d_symbol_make_register(&reg_symbol, &src->reg);
    reg_symbol.id = var_id;
    reg_symbol.info.storage_class = SpvStorageClassUniform;
    vkd3d_dxbc_compiler_put_symbol(compiler, &reg_symbol);
}

static void vkd3d_dxbc_compiler_emit_dcl_input(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    vkd3d_dxbc_compiler_emit_input(compiler, &instruction->declaration.dst, VKD3D_SIV_NONE);
}

static void vkd3d_dxbc_compiler_emit_dcl_input_ps(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    vkd3d_dxbc_compiler_emit_input(compiler, &instruction->declaration.dst, VKD3D_SIV_NONE);
    if (instruction->flags != VKD3DSIM_LINEAR)
        FIXME("Unhandled interpolation mode %#x.\n", instruction->flags);
}

static void vkd3d_dxbc_compiler_emit_dcl_input_sgv(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    vkd3d_dxbc_compiler_emit_input(compiler, &instruction->declaration.dst,
            instruction->declaration.register_semantic.sysval_semantic);
}

static void vkd3d_dxbc_compiler_emit_dcl_output(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    vkd3d_dxbc_compiler_emit_output(compiler, &instruction->declaration.dst, VKD3D_SIV_NONE);
}

static void vkd3d_dxbc_compiler_emit_dcl_output_siv(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    vkd3d_dxbc_compiler_emit_output(compiler, &instruction->declaration.dst,
            instruction->declaration.register_semantic.sysval_semantic);
}

static void vkd3d_dxbc_compiler_emit_dcl_thread_group(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    const struct vkd3d_shader_thread_group_size *group_size = &instruction->declaration.thread_group_size;
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;

    vkd3d_spirv_set_local_size(builder, group_size->x, group_size->y, group_size->z);
}

static SpvOp vkd3d_dxbc_compiler_map_alu_instruction(const struct vkd3d_shader_instruction *instruction)
{
    static const struct
    {
        enum VKD3D_SHADER_INSTRUCTION_HANDLER handler_idx;
        SpvOp spirv_op;
    }
    alu_ops[] =
    {
        {VKD3DSIH_ADD,  SpvOpFAdd},
        {VKD3DSIH_AND,  SpvOpBitwiseAnd},
        {VKD3DSIH_DIV,  SpvOpFDiv},
        {VKD3DSIH_MUL,  SpvOpFMul},
        {VKD3DSIH_UTOF, SpvOpConvertUToF},
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(alu_ops); ++i)
    {
        if (alu_ops[i].handler_idx == instruction->handler_idx)
            return alu_ops[i].spirv_op;
    }

    return SpvOpMax;
}

static void vkd3d_dxbc_compiler_emit_alu_instruction(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    const struct vkd3d_shader_dst_param *dst = instruction->dst;
    const struct vkd3d_shader_src_param *src = instruction->src;
    uint32_t src_ids[VKD3D_DXBC_MAX_SOURCE_COUNT];
    unsigned int component_count;
    uint32_t type_id, val_id;
    unsigned int i;
    SpvOp op;

    op = vkd3d_dxbc_compiler_map_alu_instruction(instruction);
    if (op == SpvOpMax)
    {
        FIXME("Unhandled ALU instruction %#x.\n", instruction->handler_idx);
        return;
    }

    assert(instruction->dst_count == 1);
    assert(instruction->src_count <= VKD3D_DXBC_MAX_SOURCE_COUNT);

    component_count = vkd3d_write_mask_component_count(dst->write_mask);
    type_id = vkd3d_spirv_get_type_id(builder,
            vkd3d_component_type_from_data_type(dst->reg.data_type), component_count);

    for (i = 0; i < instruction->src_count; ++i)
        src_ids[i] = vkd3d_dxbc_compiler_emit_load_reg(compiler,
                &src[i].reg, src[i].swizzle, dst->write_mask);

    val_id = vkd3d_spirv_build_op_trv(builder, &builder->function_stream, op, type_id,
            src_ids, instruction->src_count);

    vkd3d_dxbc_compiler_emit_store_reg(compiler, &dst->reg, dst->write_mask, val_id);
}

static enum GLSLstd450 vkd3d_dxbc_compiler_map_ext_glsl_instruction(
        const struct vkd3d_shader_instruction *instruction)
{
    static const struct
    {
        enum VKD3D_SHADER_INSTRUCTION_HANDLER handler_idx;
        enum GLSLstd450 glsl_inst;
    }
    glsl_insts[] =
    {
        {VKD3DSIH_MAD, GLSLstd450Fma},
        {VKD3DSIH_RSQ, GLSLstd450InverseSqrt},
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(glsl_insts); ++i)
    {
        if (glsl_insts[i].handler_idx == instruction->handler_idx)
            return glsl_insts[i].glsl_inst;
    }

    return GLSLstd450Bad;
}

static void vkd3d_dxbc_compiler_emit_ext_glsl_instruction(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    const struct vkd3d_shader_dst_param *dst = instruction->dst;
    const struct vkd3d_shader_src_param *src = instruction->src;
    uint32_t src_id[VKD3D_DXBC_MAX_SOURCE_COUNT];
    uint32_t instr_set_id, type_id, val_id;
    unsigned int component_count;
    enum GLSLstd450 glsl_inst;
    unsigned int i;

    glsl_inst = vkd3d_dxbc_compiler_map_ext_glsl_instruction(instruction);
    if (glsl_inst == GLSLstd450Bad)
    {
        FIXME("Unhandled GLSLstd450 instruction %#x.\n", instruction->handler_idx);
        return;
    }

    instr_set_id = vkd3d_spirv_get_glsl_std_450_instr_set(builder);

    assert(instruction->dst_count == 1);
    assert(instruction->src_count <= VKD3D_DXBC_MAX_SOURCE_COUNT);

    component_count = vkd3d_write_mask_component_count(dst->write_mask);
    type_id = vkd3d_spirv_get_type_id(builder,
            vkd3d_component_type_from_data_type(dst->reg.data_type), component_count);

    for (i = 0; i < instruction->src_count; ++i)
        src_id[i] = vkd3d_dxbc_compiler_emit_load_reg(compiler,
                &src[i].reg, src[i].swizzle, dst->write_mask);

    val_id = vkd3d_spirv_build_op_ext_inst(builder, type_id,
            instr_set_id, glsl_inst, instruction->src_count, src_id);

    vkd3d_dxbc_compiler_emit_store_reg(compiler, &dst->reg, dst->write_mask, val_id);
}

static void vkd3d_dxbc_compiler_emit_mov(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    const struct vkd3d_shader_dst_param *dst = &instruction->dst[0];
    const struct vkd3d_shader_src_param *src = &instruction->src[0];
    uint32_t val_id;

    val_id = vkd3d_dxbc_compiler_emit_load_reg(compiler, &src->reg, src->swizzle, dst->write_mask);
    vkd3d_dxbc_compiler_emit_store_reg(compiler, &dst->reg, dst->write_mask, val_id);
}

static void vkd3d_dxbc_compiler_emit_dot(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    const struct vkd3d_shader_dst_param *dst = instruction->dst;
    const struct vkd3d_shader_src_param *src = instruction->src;
    uint32_t type_id, val_id, src_ids[2];
    DWORD write_mask;
    unsigned int i;

    assert(vkd3d_write_mask_component_count(dst->write_mask) == 1);

    if (instruction->handler_idx == VKD3DSIH_DP4)
        write_mask = VKD3DSP_WRITEMASK_ALL;
    else if (instruction->handler_idx == VKD3DSIH_DP3)
        write_mask = VKD3DSP_WRITEMASK_0 | VKD3DSP_WRITEMASK_1 | VKD3DSP_WRITEMASK_2;
    else
        write_mask = VKD3DSP_WRITEMASK_0 | VKD3DSP_WRITEMASK_1;

    assert(instruction->src_count == ARRAY_SIZE(src_ids));
    for (i = 0; i < instruction->src_count; ++i)
        src_ids[i] = vkd3d_dxbc_compiler_emit_load_reg(compiler,
                &src[i].reg, src[i].swizzle, write_mask);

    type_id = vkd3d_spirv_get_type_id(builder,
            vkd3d_component_type_from_data_type(dst->reg.data_type), 1);

    val_id = vkd3d_spirv_build_op_tr2(builder, &builder->function_stream,
            SpvOpDot, type_id, src_ids[0], src_ids[1]);

    vkd3d_dxbc_compiler_emit_store_reg(compiler, &dst->reg, dst->write_mask, val_id);
}

static void vkd3d_dxbc_compiler_emit_bitfield_instruction(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    struct vkd3d_spirv_builder *builder = &compiler->spirv_builder;
    const struct vkd3d_shader_dst_param *dst = instruction->dst;
    const struct vkd3d_shader_src_param *src = instruction->src;
    uint32_t src_ids[4], result_id, type_id, mask_id;
    unsigned int i, j, src_count;
    DWORD write_mask;
    SpvOp op;

    src_count = instruction->src_count;
    assert(2 <= src_count && src_count <= ARRAY_SIZE(src_ids));

    type_id = vkd3d_spirv_get_type_id(builder, VKD3D_TYPE_UINT, 1);
    mask_id = vkd3d_dxbc_compiler_get_constant_uint(compiler, 0x1f);

    op = SpvOpBitFieldInsert;

    for (i = 0; i < VKD3D_VEC4_SIZE; ++i)
    {
        if (!(write_mask = dst->write_mask & (VKD3DSP_WRITEMASK_0 << i)))
            continue;

        for (j = 0; j < src_count; ++j)
            src_ids[src_count - j - 1] = vkd3d_dxbc_compiler_emit_load_reg(compiler,
                    &src[j].reg, src[j].swizzle, write_mask);

        for (j = src_count - 2; j < src_count; ++j)
        {
            if (instruction->src[src_count - j - 1].reg.type != VKD3DSPR_IMMCONST)
                src_ids[j] = vkd3d_spirv_build_op_and(builder, type_id, src_ids[j], mask_id);
        }

        result_id = vkd3d_spirv_build_op_trv(builder, &builder->function_stream,
                op, type_id, src_ids, src_count);

        vkd3d_dxbc_compiler_emit_store_reg(compiler,
                &dst->reg, write_mask, result_id);
    }
}

static void vkd3d_dxbc_compiler_emit_return(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    vkd3d_spirv_build_op_return(&compiler->spirv_builder);
}

void vkd3d_dxbc_compiler_handle_instruction(struct vkd3d_dxbc_compiler *compiler,
        const struct vkd3d_shader_instruction *instruction)
{
    switch (instruction->handler_idx)
    {
        case VKD3DSIH_DCL_TEMPS:
            vkd3d_dxbc_compiler_emit_dcl_temps(compiler, instruction);
            break;
        case VKD3DSIH_DCL_CONSTANT_BUFFER:
            vkd3d_dxbc_compiler_emit_dcl_constant_buffer(compiler, instruction);
            break;
        case VKD3DSIH_DCL_INPUT:
            vkd3d_dxbc_compiler_emit_dcl_input(compiler, instruction);
            break;
        case VKD3DSIH_DCL_INPUT_PS:
            vkd3d_dxbc_compiler_emit_dcl_input_ps(compiler, instruction);
            break;
        case VKD3DSIH_DCL_INPUT_SGV:
            vkd3d_dxbc_compiler_emit_dcl_input_sgv(compiler, instruction);
            break;
        case VKD3DSIH_DCL_OUTPUT:
            vkd3d_dxbc_compiler_emit_dcl_output(compiler, instruction);
            break;
        case VKD3DSIH_DCL_OUTPUT_SIV:
            vkd3d_dxbc_compiler_emit_dcl_output_siv(compiler, instruction);
            break;
        case VKD3DSIH_DCL_THREAD_GROUP:
            vkd3d_dxbc_compiler_emit_dcl_thread_group(compiler, instruction);
            break;
        case VKD3DSIH_MOV:
            vkd3d_dxbc_compiler_emit_mov(compiler, instruction);
            break;
        case VKD3DSIH_ADD:
        case VKD3DSIH_AND:
        case VKD3DSIH_DIV:
        case VKD3DSIH_MUL:
        case VKD3DSIH_UTOF:
            vkd3d_dxbc_compiler_emit_alu_instruction(compiler, instruction);
            break;
        case VKD3DSIH_MAD:
        case VKD3DSIH_RSQ:
            vkd3d_dxbc_compiler_emit_ext_glsl_instruction(compiler, instruction);
            break;
        case VKD3DSIH_DP4:
        case VKD3DSIH_DP3:
        case VKD3DSIH_DP2:
            vkd3d_dxbc_compiler_emit_dot(compiler, instruction);
            break;
        case VKD3DSIH_BFI:
            vkd3d_dxbc_compiler_emit_bitfield_instruction(compiler, instruction);
            break;
        case VKD3DSIH_RET:
            vkd3d_dxbc_compiler_emit_return(compiler, instruction);
            break;
        default:
            FIXME("Unhandled instruction %#x.\n", instruction->handler_idx);
    }
}

bool vkd3d_dxbc_compiler_generate_spirv(struct vkd3d_dxbc_compiler *compiler,
        struct vkd3d_shader_code *spirv)
{
    if (!vkd3d_spirv_compile_module(&compiler->spirv_builder, spirv))
        return false;

    if (TRACE_ON())
    {
        vkd3d_spirv_dump(spirv);
        vkd3d_spirv_validate(spirv);
    }

    return true;
}

void vkd3d_dxbc_compiler_destroy(struct vkd3d_dxbc_compiler *compiler)
{
    vkd3d_spirv_builder_free(&compiler->spirv_builder);

    rb_destroy(&compiler->symbol_table, vkd3d_symbol_free, NULL);

    vkd3d_free(compiler);
}
