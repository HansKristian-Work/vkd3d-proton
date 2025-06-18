/*
 * Copyright 2016-2017 Józef Kucia for CodeWeavers
 * Copyright 2020-2021 Philip Rebohle for Valve Corporation
 * Copyright 2020-2021 Joshua Ashton for Valve Corporation
 * Copyright 2020-2021 Hans-Kristian Arntzen for Valve Corporation
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
#include "d3d12_crosstest.h"

void test_create_compute_pipeline_state(void)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_state_desc;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pipeline_state;
    ID3D12Device *device, *tmp_device;
    ULONG refcount;
    HRESULT hr;

#include "shaders/pso/headers/cs_create_pso.h"

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = NULL;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    memset(&pipeline_state_desc, 0, sizeof(pipeline_state_desc));
    pipeline_state_desc.pRootSignature = root_signature;
    pipeline_state_desc.CS = cs_create_pso_dxbc;
    pipeline_state_desc.NodeMask = 0;
    pipeline_state_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    hr = ID3D12Device_CreateComputePipelineState(device, &pipeline_state_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == S_OK, "Failed to create compute pipeline, hr %#x.\n", hr);

    refcount = get_refcount(root_signature);
    ok(refcount == 1, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12PipelineState_GetDevice(pipeline_state, &IID_ID3D12Device, (void **)&tmp_device);
    ok(hr == S_OK, "Failed to get device, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 4, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(pipeline_state, &IID_ID3D12Object, true);
    check_interface(pipeline_state, &IID_ID3D12DeviceChild, true);
    check_interface(pipeline_state, &IID_ID3D12Pageable, true);
    check_interface(pipeline_state, &IID_ID3D12PipelineState, true);

    refcount = ID3D12PipelineState_Release(pipeline_state);
    ok(!refcount, "ID3D12PipelineState has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12RootSignature_Release(root_signature);
    ok(!refcount, "ID3D12RootSignature has %u references left.\n", (unsigned int)refcount);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_integer_blending_pipeline_state(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pso;
    D3D12_BLEND_DESC *blend;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

#include "shaders/pso/headers/ps_integer_blending.h"
#include "shaders/pso/headers/ps_integer_blending_no_rt.h"

    struct test
    {
        HRESULT hr;
        const D3D12_SHADER_BYTECODE *ps;
        UINT8 write_mask;
    };
    static const struct test tests[] =
    {
        { S_OK, &ps_integer_blending_no_rt_dxbc, D3D12_COLOR_WRITE_ENABLE_ALL },
        { E_INVALIDARG, &ps_integer_blending_dxbc, 0 },
        { E_INVALIDARG, &ps_integer_blending_dxbc, D3D12_COLOR_WRITE_ENABLE_ALL },
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = NULL;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);
        init_pipeline_state_desc(&pso_desc, root_signature, DXGI_FORMAT_R32_UINT, NULL, tests[i].ps, NULL);
        blend = &pso_desc.BlendState;
        blend->IndependentBlendEnable = false;
        blend->RenderTarget[0].BlendEnable = true;
        blend->RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend->RenderTarget[0].DestBlend = D3D12_BLEND_DEST_ALPHA;
        blend->RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend->RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend->RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blend->RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend->RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
                &IID_ID3D12PipelineState, (void **)&pso);
        ok(hr == tests[i].hr, "Unexpected hr %#x.\n", hr);
        if (SUCCEEDED(hr))
            ID3D12PipelineState_Release(pso);
    }
    vkd3d_test_set_context(NULL);
    ID3D12RootSignature_Release(root_signature);
    ID3D12Device_Release(device);
}

void test_create_graphics_pipeline_state(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pipeline_state;
    ID3D12Device *device, *tmp_device;
    D3D12_BLEND_DESC *blend;
    ULONG refcount;
    unsigned int i;
    HRESULT hr;

    static const D3D12_SO_DECLARATION_ENTRY so_declaration[] =
    {
        {0, "SV_Position", 0, 0, 4, 0},
    };
    static const unsigned int strides[] = {16};

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = NULL;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    init_pipeline_state_desc(&pso_desc, root_signature, DXGI_FORMAT_R8G8B8A8_UNORM, NULL, NULL, NULL);
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    refcount = get_refcount(root_signature);
    ok(refcount == 1, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12PipelineState_GetDevice(pipeline_state, &IID_ID3D12Device, (void **)&tmp_device);
    ok(hr == S_OK, "Failed to get device, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 4, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(pipeline_state, &IID_ID3D12Object, true);
    check_interface(pipeline_state, &IID_ID3D12DeviceChild, true);
    check_interface(pipeline_state, &IID_ID3D12Pageable, true);
    check_interface(pipeline_state, &IID_ID3D12PipelineState, true);

    refcount = ID3D12PipelineState_Release(pipeline_state);
    ok(!refcount, "ID3D12PipelineState has %u references left.\n", (unsigned int)refcount);

    blend = &pso_desc.BlendState;
    blend->IndependentBlendEnable = false;
    blend->RenderTarget[0].BlendEnable = true;
    blend->RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_COLOR;
    blend->RenderTarget[0].DestBlend = D3D12_BLEND_DEST_COLOR;
    blend->RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend->RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
    blend->RenderTarget[0].DestBlendAlpha = D3D12_BLEND_DEST_ALPHA;
    blend->RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend->RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);
    ID3D12PipelineState_Release(pipeline_state);

    /* Only one of BlendEnable or LogicOpEnable can be set to true. */
    blend->IndependentBlendEnable = false;
    blend->RenderTarget[0].BlendEnable = true;
    blend->RenderTarget[0].LogicOpEnable = true;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R32_UINT;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    blend->IndependentBlendEnable = false;
    blend->RenderTarget[0].BlendEnable = false;
    blend->RenderTarget[0].LogicOpEnable = true;

    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) &&
            options.OutputMergerLogicOp)
    {
        hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
                &IID_ID3D12PipelineState, (void **) &pipeline_state);
        ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);
        ID3D12PipelineState_Release(pipeline_state);
    }

    /* IndependentBlendEnable must be set to false when logic operations are enabled. */
    blend->IndependentBlendEnable = true;
    blend->RenderTarget[0].LogicOpEnable = true;
    for (i = 1; i < ARRAY_SIZE(blend->RenderTarget); ++i)
        blend->RenderTarget[i] = blend->RenderTarget[0];
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* DSVFormat = DXGI_FORMAT_UNKNOWN */
    memset(blend, 0, sizeof(*blend));
    pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ID3D12PipelineState_Release(pipeline_state);

    /* Invalid DSVFormat */
    pso_desc.DSVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.DepthStencilState.DepthEnable = true;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == S_OK, "Got unexpected hr %#x.\n", hr);
    ID3D12PipelineState_Release(pipeline_state);

    /* Inactive render targets formats must be set to DXGI_FORMAT_UNKNOWN. */
    init_pipeline_state_desc(&pso_desc, root_signature, DXGI_FORMAT_R8G8B8A8_UNORM, NULL, NULL, NULL);
    pso_desc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    /* Stream output without D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT. */
    init_pipeline_state_desc(&pso_desc, root_signature, DXGI_FORMAT_R8G8B8A8_UNORM, NULL, NULL, NULL);
    pso_desc.StreamOutput.NumEntries = ARRAY_SIZE(so_declaration);
    pso_desc.StreamOutput.pSODeclaration = so_declaration;
    pso_desc.StreamOutput.pBufferStrides = strides;
    pso_desc.StreamOutput.NumStrides = ARRAY_SIZE(strides);
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    refcount = ID3D12RootSignature_Release(root_signature);
    ok(!refcount, "ID3D12RootSignature has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_create_pipeline_state(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pipeline_state;
    ID3D12Device2 *device2;
    ID3D12Device *device;
    unsigned int i;
    ULONG refcount;
    HRESULT hr;

#include "shaders/pso/headers/cs_create_pso.h"
#include "shaders/pso/headers/ps_create_pso.h"
#include "shaders/pso/headers/vs_create_pso.h"

    static const union d3d12_root_signature_subobject root_signature_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
        NULL, /* fill in dynamically */
    }};

    static const union d3d12_shader_bytecode_subobject vs_subobject = {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, { vs_create_pso_code_dxbc, sizeof(vs_create_pso_code_dxbc) } }};
    static const union d3d12_shader_bytecode_subobject ps_subobject = {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_create_pso_code_dxbc, sizeof(ps_create_pso_code_dxbc) } }};
    static const union d3d12_shader_bytecode_subobject cs_subobject = {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, { cs_create_pso_code_dxbc, sizeof(cs_create_pso_code_dxbc) } }};

    static const D3D12_SO_DECLARATION_ENTRY so_entries[] =
    {
        { 0, "SV_POSITION", 0, 0, 4, 0 },
    };

    static const UINT so_strides[] = { 16u };

    static const union d3d12_stream_output_subobject stream_output_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT,
        { so_entries, ARRAY_SIZE(so_entries),
            so_strides, ARRAY_SIZE(so_strides),
            D3D12_SO_NO_RASTERIZED_STREAM },
    }};

    static const union d3d12_blend_subobject blend_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,
        { FALSE, TRUE,
            {{ FALSE, FALSE,
                D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                D3D12_LOGIC_OP_NOOP, 0xF }},
        }
    }};

    static const union d3d12_sample_mask_subobject sample_mask_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK,
        0xFFFFFFFFu
    }};

    static const union d3d12_rasterizer_subobject rasterizer_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
        { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_BACK,
            FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, 0,
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF },
    }};

    static const union d3d12_depth_stencil_subobject depth_stencil_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL,
        { TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS_EQUAL, TRUE, 0xFF, 0xFF,
            { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_INCR, D3D12_COMPARISON_FUNC_EQUAL },
            { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_INCR, D3D12_COMPARISON_FUNC_EQUAL } },
    }};

    static const D3D12_INPUT_ELEMENT_DESC input_elements[] =
    {
        { "POS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    static const union d3d12_input_layout_subobject input_layout_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT,
        { input_elements, ARRAY_SIZE(input_elements) },
    }};

    static const union d3d12_ib_strip_cut_value_subobject ib_strip_cut_value_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE,
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF,
    }};

    static const union d3d12_primitive_topology_subobject primitive_topology_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    }};

    static const union d3d12_render_target_formats_subobject render_target_formats_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
        { { DXGI_FORMAT_R8G8B8A8_UNORM }, 1 },
    }};

    static const union d3d12_depth_stencil_format_subobject depth_stencil_format_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,
        DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
    }};

    static const union d3d12_sample_desc_subobject sample_desc_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,
        { 1, 0 },
    }};

    static const union d3d12_node_mask_subobject node_mask_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK,
        0x0,
    }};

    static const union d3d12_cached_pso_subobject cached_pso_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO,
        { NULL, 0 },
    }};

    static const union d3d12_flags_subobject flags_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS,
        D3D12_PIPELINE_STATE_FLAG_NONE,
    }};

    static const union d3d12_depth_stencil1_subobject depth_stencil1_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1,
        { TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_LESS_EQUAL, TRUE, 0xFF, 0xFF,
            { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_INCR, D3D12_COMPARISON_FUNC_EQUAL },
            { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_INCR, D3D12_COMPARISON_FUNC_EQUAL } },
    }};

    static const union d3d12_view_instancing_subobject view_instancing_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING,
        { 0, NULL, D3D12_VIEW_INSTANCING_FLAG_NONE },
    }};

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject vertex_shader;
        union d3d12_shader_bytecode_subobject pixel_shader;
        union d3d12_blend_subobject blend;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_rasterizer_subobject rasterizer;
        union d3d12_depth_stencil1_subobject depth_stencil;
        union d3d12_input_layout_subobject input_layout;
        union d3d12_ib_strip_cut_value_subobject strip_cut;
        union d3d12_primitive_topology_subobject primitive_topology;
        union d3d12_render_target_formats_subobject render_target_formats;
        union d3d12_depth_stencil_format_subobject depth_stencil_format;
        union d3d12_sample_desc_subobject sample_desc;
        union d3d12_node_mask_subobject node_mask;
        union d3d12_cached_pso_subobject cached_pso;
        union d3d12_flags_subobject flags;
        union d3d12_view_instancing_subobject view_instancing;
    }
    pipeline_desc_1 =
    {
        root_signature_subobject,
        vs_subobject,
        ps_subobject,
        blend_subobject,
        sample_mask_subobject,
        rasterizer_subobject,
        depth_stencil1_subobject,
        input_layout_subobject,
        ib_strip_cut_value_subobject,
        primitive_topology_subobject,
        render_target_formats_subobject,
        depth_stencil_format_subobject,
        sample_desc_subobject,
        node_mask_subobject,
        cached_pso_subobject,
        flags_subobject,
        view_instancing_subobject,
    };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject compute_shader;
    }
    pipeline_desc_2 =
    {
        root_signature_subobject, cs_subobject,
    };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject vertex_shader;
        union d3d12_stream_output_subobject stream_output;
        union d3d12_input_layout_subobject input_layout;
    }
    pipeline_desc_3 =
    {
        root_signature_subobject, vs_subobject, stream_output_subobject,
        input_layout_subobject,
    };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
    }
    pipeline_desc_4 =
    {
        root_signature_subobject,
    };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject cs;
        union d3d12_shader_bytecode_subobject vs;
    }
    pipeline_desc_5 =
    {
        root_signature_subobject, cs_subobject, vs_subobject,
    };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject cs;
        union d3d12_shader_bytecode_subobject ps;
        union d3d12_rasterizer_subobject rasterizer;
    }
    pipeline_desc_6 =
    {
        root_signature_subobject, cs_subobject, ps_subobject,
        rasterizer_subobject,
    };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_depth_stencil_subobject depth_stencil;
        union d3d12_depth_stencil_format_subobject depth_stencil_format;
        union d3d12_input_layout_subobject input_layout;
        union d3d12_shader_bytecode_subobject vertex_shader;
    }
    pipeline_desc_7 =
    {
        root_signature_subobject, depth_stencil_subobject, depth_stencil_format_subobject,
        input_layout_subobject, vs_subobject,
    };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject cs;
        union d3d12_shader_bytecode_subobject cs2;
    }
    pipeline_desc_8 =
    {
        root_signature_subobject, cs_subobject, cs_subobject,
    };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject vs;
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE extra_type;
    }
    pipeline_desc_9 =
    {
        root_signature_subobject, vs_subobject,
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL
    };

    struct
    {
        D3D12_PIPELINE_STATE_STREAM_DESC stream_desc;
        HRESULT expected_result;
    }
    tests[] = {
        { { sizeof(pipeline_desc_1), &pipeline_desc_1 }, S_OK },
        { { sizeof(pipeline_desc_2), &pipeline_desc_2 }, S_OK },
        { { sizeof(pipeline_desc_3), &pipeline_desc_3 }, S_OK },
        { { sizeof(pipeline_desc_4), &pipeline_desc_4 }, E_INVALIDARG },
        { { sizeof(pipeline_desc_5), &pipeline_desc_5 }, E_INVALIDARG },
        { { sizeof(pipeline_desc_6), &pipeline_desc_6 }, S_OK },
        { { sizeof(pipeline_desc_7), &pipeline_desc_7 }, S_OK },
        { { sizeof(pipeline_desc_8), &pipeline_desc_8 }, E_INVALIDARG },
        { { sizeof(pipeline_desc_9), &pipeline_desc_9 }, E_INVALIDARG },
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    if (ID3D12Device_QueryInterface(device, &IID_ID3D12Device2, (void **)&device2))
    {
        skip("ID3D12Device2 not supported..\n");
        ID3D12Device_Release(device);
        return;
    }

    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = NULL;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT |
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        union d3d12_root_signature_subobject *rs_subobject;
        vkd3d_test_set_context("Test %u", i);

        /* Assign root signature. To keep things simple, assume
         * that the root signature is always the first element
         * in each pipeline stream */
        rs_subobject = tests[i].stream_desc.pPipelineStateSubobjectStream;

        if (rs_subobject && rs_subobject->type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE)
            rs_subobject->root_signature = root_signature;

        hr = ID3D12Device2_CreatePipelineState(device2, &tests[i].stream_desc, &IID_ID3D12PipelineState, (void **)&pipeline_state);
        ok(hr == tests[i].expected_result, "Got unexpected return value %#x.\n", hr);

        if (hr == S_OK)
        {
            refcount = ID3D12PipelineState_Release(pipeline_state);
            ok(!refcount, "ID3D12PipelineState has %u references left.\n", (unsigned int)refcount);
        }
    }

    refcount = ID3D12RootSignature_Release(root_signature);
    ok(!refcount, "ID3D12RootSignature has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12Device2_Release(device2);
    ok(refcount == 1, "ID3D12Device2 has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_shader_interstage_interface(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *vb;

#include "shaders/pso/headers/ps_interstage.h"
#include "shaders/pso/headers/vs_interstage.h"

    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"SV_POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD",    0, DXGI_FORMAT_R32G32_FLOAT, 0,  8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD",    1, DXGI_FORMAT_R32_FLOAT,    0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD",    2, DXGI_FORMAT_R32_UINT,     0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD",    3, DXGI_FORMAT_R32_UINT,     0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD",    4, DXGI_FORMAT_R32_FLOAT,    0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const struct
    {
        struct vec2 position;
        struct vec2 t0;
        float t1;
        unsigned int t2;
        unsigned int t3;
        float t4;
    }
    quad[] =
    {
        {{-1.0f, -1.0f}, {3.0f, 5.0f}, 5.0f, 2, 6, 7.0f},
        {{-1.0f,  1.0f}, {3.0f, 5.0f}, 5.0f, 2, 6, 7.0f},
        {{ 1.0f, -1.0f}, {3.0f, 5.0f}, 5.0f, 2, 6, 7.0f},
        {{ 1.0f,  1.0f}, {3.0f, 5.0f}, 5.0f, 2, 6, 7.0f},
    };
    static const struct vec4 expected_result = {10.0f, 8.0f, 7.0f, 3.0f};

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    context.root_signature = create_empty_root_signature(context.device,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);
    context.pipeline_state = create_pipeline_state(context.device, context.root_signature,
            desc.rt_format, &vs_interstage_dxbc, &ps_interstage_dxbc, &input_layout);

    vb = create_upload_buffer(context.device, sizeof(quad), quad);

    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.StrideInBytes = sizeof(*quad);
    vbv.SizeInBytes = sizeof(quad);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &expected_result, 0);

    ID3D12Resource_Release(vb);
    destroy_test_context(&context);
}

void test_shader_input_output_components(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2];
    ID3D12Resource *uint_render_target;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *vb;
    unsigned int i;
    HRESULT hr;

#include "shaders/pso/headers/vs_shader_io_1.h"
#include "shaders/pso/headers/vs_shader_io_2.h"
#include "shaders/pso/headers/vs_shader_io_5.h"
#include "shaders/pso/headers/ps_shader_io_1.h"
#include "shaders/pso/headers/ps_shader_io_2.h"
#include "shaders/pso/headers/ps_shader_io_3.h"
#include "shaders/pso/headers/ps_shader_io_4.h"
#include "shaders/pso/headers/ps_shader_io_5.h"

    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"UINT",     0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const struct
    {
        struct vec4 position;
        struct uvec4 u;
        struct vec4 t0;
        struct vec4 t1;
        struct vec4 t2;
    }
    quad[] =
    {
        {{-1.0f, -1.0f}, {1, 2, 3, 4}, {3.0f, 3.0f, 8.0f, 4.0f}, {9.0f, 5.0f, 3.0f, 1.0f}, {7.0f, 2.0f, 5.0f}},
        {{-1.0f,  1.0f}, {1, 2, 3, 4}, {3.0f, 3.0f, 8.0f, 4.0f}, {9.0f, 5.0f, 3.0f, 1.0f}, {7.0f, 2.0f, 5.0f}},
        {{ 1.0f, -1.0f}, {1, 2, 3, 4}, {3.0f, 3.0f, 8.0f, 4.0f}, {9.0f, 5.0f, 3.0f, 1.0f}, {7.0f, 2.0f, 5.0f}},
        {{ 1.0f,  1.0f}, {1, 2, 3, 4}, {3.0f, 3.0f, 8.0f, 4.0f}, {9.0f, 5.0f, 3.0f, 1.0f}, {7.0f, 2.0f, 5.0f}},
    };
    static const struct
    {
        const D3D12_SHADER_BYTECODE *vs;
        const D3D12_SHADER_BYTECODE *ps;
        const struct vec4 expected_vec4;
        const struct uvec4 expected_uvec4;
    }
    tests[] =
    {
        {&vs_shader_io_1_dxbc, &ps_shader_io_1_dxbc, {1.0f, 2.0f, 3.0f, 0.00f}, {0xdeadbeef, 0, 2, 3}},
        {&vs_shader_io_2_dxbc, &ps_shader_io_2_dxbc, {6.0f, 4.0f, 7.0f, 8.00f}, {         9, 5, 0, 1}},
        {&vs_shader_io_2_dxbc, &ps_shader_io_3_dxbc, {3.0f, 8.0f, 7.0f, 7.00f}, {         9, 0, 0, 1}},
        {&vs_shader_io_2_dxbc, &ps_shader_io_4_dxbc, {0.0f, 1.0f, 0.0f, 1.00f}, {         0, 1, 0, 0}},
        {&vs_shader_io_5_dxbc, &ps_shader_io_5_dxbc, {0.0f, 1.0f, 0.0f, 0.25f}, {         0, 1, 0, 0}},
    };

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.rt_descriptor_count = 2;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    context.root_signature = create_empty_root_signature(context.device,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);
    init_pipeline_state_desc(&pso_desc, context.root_signature, desc.rt_format, NULL, NULL, &input_layout);
    pso_desc.NumRenderTargets = 2;
    pso_desc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_UINT;

    rtvs[0] = context.rtv;
    rtvs[1] = get_cpu_rtv_handle(&context, context.rtv_heap, 1);
    desc.rt_format = pso_desc.RTVFormats[1];
    create_render_target(&context, &desc, &uint_render_target, &rtvs[1]);

    vb = create_upload_buffer(context.device, sizeof(quad), quad);

    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.StrideInBytes = sizeof(*quad);
    vbv.SizeInBytes = sizeof(quad);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        pso_desc.VS = *tests[i].vs;
        pso_desc.PS = *tests[i].ps;
        hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
                &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
        ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

        if (i)
        {
            reset_command_list(command_list, context.allocator);
            transition_resource_state(command_list, context.render_target,
                    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            transition_resource_state(command_list, uint_render_target,
                    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }

        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 2, &context.rtv, true, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 1, 0, 0);

        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_vec4(context.render_target, 0, queue, command_list, &tests[i].expected_vec4, 0);
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, uint_render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uvec4(uint_render_target, 0, queue, command_list, &tests[i].expected_uvec4);

        ID3D12PipelineState_Release(context.pipeline_state);
        context.pipeline_state = NULL;
    }
    vkd3d_test_set_context(NULL);

    ID3D12Resource_Release(vb);
    ID3D12Resource_Release(uint_render_target);
    destroy_test_context(&context);
}

void test_append_aligned_element(void)
{
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    D3D12_VERTEX_BUFFER_VIEW vbv[6];
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    ID3D12Resource *vb[3];
    unsigned int color;

#include "shaders/pso/headers/ps_append_aligned_element.h"
#include "shaders/pso/headers/vs_append_aligned_element.h"

    /* Semantic names are case-insensitive. */
    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"CoLoR",    2, DXGI_FORMAT_R32G32_FLOAT,       1, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"ColoR",    3, DXGI_FORMAT_R32G32_FLOAT,       5, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"ColoR",    0, DXGI_FORMAT_R32G32_FLOAT,       5, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"cOLOr",    1, DXGI_FORMAT_R32G32_FLOAT,       1, D3D12_APPEND_ALIGNED_ELEMENT,
                D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    };
    static const struct
    {
        struct vec4 position;
    }
    stream0[] =
    {
        {{-1.0f, -1.0f, 0.0f, 1.0f}},
        {{-1.0f,  1.0f, 0.0f, 1.0f}},
        {{-0.5f, -1.0f, 0.0f, 1.0f}},
        {{-0.5f,  1.0f, 0.0f, 1.0f}},
    };
    static const struct
    {
        struct vec2 color2;
        struct vec2 color1;
    }
    stream1[] =
    {
        {{0.5f, 0.5f}, {0.0f, 1.0f}},
        {{0.5f, 0.5f}, {0.0f, 1.0f}},
        {{0.5f, 0.5f}, {1.0f, 1.0f}},
        {{0.5f, 0.5f}, {1.0f, 1.0f}},
    };
    static const struct
    {
        struct vec2 color3;
        struct vec2 color0;
    }
    stream2[] =
    {
        {{0.5f, 0.5f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f}, {0.0f, 1.0f}},
        {{0.5f, 0.5f}, {0.0f, 0.0f}},
        {{0.5f, 0.5f}, {1.0f, 0.0f}},
    };
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};

    memset(&desc, 0, sizeof(desc));
    desc.rt_width = 640;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    context.root_signature = create_empty_root_signature(context.device,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);
    context.pipeline_state = create_pipeline_state(context.device, context.root_signature,
            context.render_target_desc.Format, &vs_append_aligned_element_dxbc,
            &ps_append_aligned_element_dxbc, &input_layout);

    memset(vbv, 0, sizeof(vbv));
    vb[0] = create_upload_buffer(context.device, sizeof(stream0), stream0);
    vbv[0].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb[0]);
    vbv[0].StrideInBytes = sizeof(*stream0);
    vbv[0].SizeInBytes = sizeof(stream0);

    vb[1] = create_upload_buffer(context.device, sizeof(stream1), stream1);
    vbv[1].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb[1]);
    vbv[1].StrideInBytes = sizeof(*stream1);
    vbv[1].SizeInBytes = sizeof(stream1);

    vb[2] = create_upload_buffer(context.device, sizeof(stream2), stream2);
    vbv[5].BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb[2]);
    vbv[5].StrideInBytes = sizeof(*stream2);
    vbv[5].SizeInBytes = sizeof(stream2);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, ARRAY_SIZE(vbv), vbv);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 4, 4, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    color = get_readback_uint(&rb, 80, 16, 0);
    ok(compare_color(color, 0xff0000ff, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_readback_uint(&rb, 240, 16, 0);
    ok(compare_color(color, 0xff00ff00, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_readback_uint(&rb, 400, 16, 0);
    ok(compare_color(color, 0xffff0000, 1), "Got unexpected color 0x%08x.\n", color);
    color = get_readback_uint(&rb, 560, 16, 0);
    ok(compare_color(color, 0xffff00ff, 1), "Got unexpected color 0x%08x.\n", color);
    release_resource_readback(&rb);

    ID3D12Resource_Release(vb[2]);
    ID3D12Resource_Release(vb[1]);
    ID3D12Resource_Release(vb[0]);
    destroy_test_context(&context);
}

void test_blend_factor(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int i;
    HRESULT hr;

    static const struct
    {
        float blend_factor[4];
        unsigned int expected_color;
    }
    tests[] =
    {
        {{0.0f, 0.0f, 0.0f, 0.0f}, 0xffffffff},
        {{0.0f, 1.0f, 0.0f, 1.0f}, 0xffffffff},
        {{0.5f, 0.5f, 0.5f, 0.5f}, 0xff80ff80},
        {{1.0f, 1.0f, 1.0f, 1.0f}, 0xff00ff00},
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, NULL, NULL);
    pso_desc.BlendState.RenderTarget[0].BlendEnable = true;
    pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_BLEND_FACTOR;
    pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_BLEND_FACTOR;
    pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_BLEND_FACTOR;
    pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_BLEND_FACTOR;
    pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_OMSetBlendFactor(command_list, tests[i].blend_factor);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, queue, command_list, tests[i].expected_color, 1);

        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    vkd3d_test_set_context(NULL);

    destroy_test_context(&context);
}

static void test_dual_source_blending(bool use_dxil)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int i;
    HRESULT hr;

#include "shaders/pso/headers/ps_dual_source.h"
#include "shaders/pso/headers/ps_dual_source_3rt.h"

    const D3D12_SHADER_BYTECODE ps = use_dxil ? ps_dual_source_dxil : ps_dual_source_dxbc;
    const D3D12_SHADER_BYTECODE ps_3rt = use_dxil ? ps_dual_source_3rt_dxil : ps_dual_source_3rt_dxbc;

    static const struct
    {
        struct
        {
            struct vec4 c0;
            struct vec4 c1;
        } constants;
        unsigned int expected_color;
    }
    tests[] =
    {
        {{{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}}, 0x00000000},
        {{{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}, 0xff0000ff},
        {{{1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}}, 0xff0000ff},
        {{{1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 0.0f}}, 0xffffffff},
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    command_list = context.list;
    queue = context.queue;

    context.root_signature = create_32bit_constants_root_signature(context.device,
            0, sizeof(tests->constants) / sizeof(uint32_t), D3D12_SHADER_VISIBILITY_PIXEL);

    if (use_dxil)
    {
        init_pipeline_state_desc_dxil(&pso_desc, context.root_signature,
                context.render_target_desc.Format, NULL, &ps, NULL);
    }
    else
    {
        init_pipeline_state_desc(&pso_desc, context.root_signature,
                context.render_target_desc.Format, NULL, &ps, NULL);
    }

    pso_desc.BlendState.RenderTarget[0].BlendEnable = true;
    pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_COLOR;
    pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_SRC1_COLOR;
    pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
    pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_SRC1_ALPHA;
    pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

    pso_desc.NumRenderTargets = 2;
    pso_desc.RTVFormats[1] = pso_desc.RTVFormats[0];
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == E_INVALIDARG, "Unexpected result, hr %#x.\n", hr);

    /* Write mask of 0 is not enough. */
    pso_desc.BlendState.IndependentBlendEnable = TRUE;
    pso_desc.BlendState.RenderTarget[1].RenderTargetWriteMask = 0;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
        &IID_ID3D12PipelineState, (void**)&context.pipeline_state);
    ok(hr == E_INVALIDARG, "Unexpected result, hr %#x.\n", hr);

    /* This appears to be allowed however. */
    pso_desc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
    pso_desc.BlendState.IndependentBlendEnable = FALSE;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
        &IID_ID3D12PipelineState, (void**)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);
    ID3D12PipelineState_Release(context.pipeline_state);

    /* >2 RTs is also allowed as long as we keep using NULL format. */
    pso_desc.NumRenderTargets = 3;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
        &IID_ID3D12PipelineState, (void**)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);
    ID3D12PipelineState_Release(context.pipeline_state);

    /* This is still allowed. We need to only consider RTs with IOSIG entry apparently ... */
    pso_desc.RTVFormats[2] = pso_desc.RTVFormats[0];
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
        &IID_ID3D12PipelineState, (void**)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);
    ID3D12PipelineState_Release(context.pipeline_state);

    /* If we try to write to o2 however, this must fail. */
    pso_desc.PS = ps_3rt;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
        &IID_ID3D12PipelineState, (void**)&context.pipeline_state);
    ok(hr == E_INVALIDARG, "Unexpected result, hr %#x.\n", hr);

    /* Writing to unused RTs is allowed if they aren't bound to the PSO. */
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
        &IID_ID3D12PipelineState, (void**)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);
    ID3D12PipelineState_Release(context.pipeline_state);

    pso_desc.PS = ps;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 8, &tests[i].constants, 0);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, queue, command_list, tests[i].expected_color, 1);

        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    vkd3d_test_set_context(NULL);

    ID3D12PipelineState_Release(context.pipeline_state);
    pso_desc.BlendState.IndependentBlendEnable = true;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12PipelineState_Release(context.pipeline_state);
    context.pipeline_state = NULL;

    pso_desc.BlendState.RenderTarget[1] = pso_desc.BlendState.RenderTarget[0];
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12PipelineState_Release(context.pipeline_state);
    context.pipeline_state = NULL;

    pso_desc.BlendState.RenderTarget[1].DestBlendAlpha = D3D12_BLEND_SRC_ALPHA;
    pso_desc.BlendState.RenderTarget[1].DestBlend = D3D12_BLEND_SRC_COLOR;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12PipelineState_Release(context.pipeline_state);
    context.pipeline_state = NULL;

    pso_desc.NumRenderTargets = 2;
    pso_desc.RTVFormats[1] = pso_desc.RTVFormats[0];
    pso_desc.BlendState.IndependentBlendEnable = false;
    pso_desc.BlendState.RenderTarget[0].BlendEnable = true;
    pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_COLOR;
    pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_SRC1_COLOR;
    pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
    pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_SRC1_ALPHA;
    pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12PipelineState_Release(context.pipeline_state);
    context.pipeline_state = NULL;

    destroy_test_context(&context);
}

void test_dual_source_blending_dxbc(void)
{
    test_dual_source_blending(false);
}

void test_dual_source_blending_dxil(void)
{
    test_dual_source_blending(true);
}

void test_primitive_restart(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    D3D12_INDEX_BUFFER_VIEW ibv;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    unsigned int index_count;
    ID3D12Resource *ib, *vb;
    size_t buffer_size;
    unsigned int i;
    D3D12_BOX box;
    HRESULT hr;
    void *ptr;

#include "shaders/pso/headers/vs_primitive_restart.h"

    static const struct
    {
        int8_t x, y;
    }
    quad[] =
    {
        {-1, -1},
        {-1,  1},
        { 1, -1},
        { 1,  1},
    };
    static const D3D12_INPUT_ELEMENT_DESC layout_desc[] =
    {
        {"position", 0, DXGI_FORMAT_R8G8_SINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    static const uint16_t indices16[] = {0, 1, 2, 3};
    static const uint32_t indices[] = {0, 1, 2, 3};
    static const uint16_t indices16_max[] = {0, 1, 2, 0xffff};
    static const uint32_t indices_max16[] = {0, 1, 2, 0xffff};
    static const uint16_t indices16_restart[] = {0, 1, 2, 0xffff, 2, 1, 3};
    static const uint32_t indices_restart[] = {0, 1, 2, 0xffffffff, 2, 1, 3};
    static const struct
    {
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE strip_cut_value;
        DXGI_FORMAT ib_format;
        const void *indices;
        size_t indices_size;
        unsigned int last_index;
        bool full_quad;
        bool is_todo;
    }
    tests[] =
    {
        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED, DXGI_FORMAT_R16_UINT,     indices16,     sizeof(indices16), 0x0003, true},
        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED, DXGI_FORMAT_R16_UINT, indices16_max, sizeof(indices16_max), 0xffff, true},
        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED, DXGI_FORMAT_R32_UINT,       indices,       sizeof(indices), 0x0003, true},
        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED, DXGI_FORMAT_R32_UINT, indices_max16, sizeof(indices_max16), 0xffff, true},

        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF, DXGI_FORMAT_R16_UINT,     indices16,     sizeof(indices16), 0x0003, true},
        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF, DXGI_FORMAT_R16_UINT, indices16_max, sizeof(indices16_max), 0xffff, false},
        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF, DXGI_FORMAT_R16_UINT, indices16_restart, sizeof(indices16_restart), 0x0003, true},
        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF, DXGI_FORMAT_R32_UINT,       indices,       sizeof(indices), 0x0003, true},
        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF, DXGI_FORMAT_R32_UINT, indices_max16, sizeof(indices_max16), 0xffff, false, true},

        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF, DXGI_FORMAT_R16_UINT,     indices16,     sizeof(indices16), 0x0003, true},
        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF, DXGI_FORMAT_R16_UINT, indices16_max, sizeof(indices16_max), 0xffff, true, true},
        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF, DXGI_FORMAT_R32_UINT,       indices,       sizeof(indices), 0x0003, true},
        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF, DXGI_FORMAT_R32_UINT, indices_max16, sizeof(indices_max16), 0xffff, true},
        {D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF, DXGI_FORMAT_R32_UINT, indices_restart, sizeof(indices_restart), 0x0003, true},
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    context.root_signature = create_empty_root_signature(context.device,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    input_layout.pInputElementDescs = layout_desc;
    input_layout.NumElements = ARRAY_SIZE(layout_desc);
    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, &vs_primitive_restart_dxbc, NULL, &input_layout);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        buffer_size = (tests[i].last_index + 1) * sizeof(*quad);

        vb = create_upload_buffer(context.device, buffer_size, NULL);
        vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
        vbv.StrideInBytes = sizeof(*quad);
        vbv.SizeInBytes = buffer_size;

        pso_desc.IBStripCutValue = tests[i].strip_cut_value;
        hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
                &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
        ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

        ibv.Format = tests[i].ib_format;
        ib = create_upload_buffer(context.device, tests[i].indices_size, tests[i].indices);
        ibv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(ib);
        ibv.SizeInBytes = tests[i].indices_size;
        index_count = tests[i].indices_size / format_size(ibv.Format);

        hr = ID3D12Resource_Map(vb, 0, NULL, &ptr);
        ok(hr == S_OK, "Failed to map buffer, hr %#x.\n", hr);
        memcpy(ptr, quad, (ARRAY_SIZE(quad) - 1) * sizeof(*quad));
        memcpy((BYTE *)ptr + tests[i].last_index * sizeof(*quad), &quad[ARRAY_SIZE(quad) - 1], sizeof(*quad));
        ID3D12Resource_Unmap(vb, 0, NULL);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D12GraphicsCommandList_IASetVertexBuffers(command_list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_IASetIndexBuffer(command_list, &ibv);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_DrawIndexedInstanced(command_list, index_count, 1, 0, 0, 0);

        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
        if (tests[i].full_quad)
        {
            todo_if(tests[i].is_todo)
            check_readback_data_uint(&rb, NULL, 0xff00ff00, 0);
        }
        else
        {
            set_box(&box, 16, 0, 0, 32, 10, 1);
            todo_if(tests[i].is_todo)
            check_readback_data_uint(&rb, &box, 0xffffffff, 0);
            set_box(&box, 0, 16, 0, 16, 32, 1);
            check_readback_data_uint(&rb, &box, 0xff00ff00, 0);
        }
        release_resource_readback(&rb);

        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12Resource_Release(ib);
        ID3D12Resource_Release(vb);
        ID3D12PipelineState_Release(context.pipeline_state);
        context.pipeline_state = NULL;
    }
    vkd3d_test_set_context(NULL);

    destroy_test_context(&context);
}

void test_create_pipeline_with_null_root_signature(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_desc;
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute_desc;
    ID3D12PipelineState *pipeline_state = NULL;
    ID3D12RootSignature *root_signature = NULL;
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Resource *uav_buffer;
    ID3D12CommandQueue *queue;
    HRESULT hr;
    UINT value;

    static const float green[] = { 0.0f, 1.0f, 0.0f, 1.0f };

#include "shaders/pso/headers/cs_null_root_signature.h"
#include "shaders/pso/headers/ps_null_root_signature.h"
#include "shaders/pso/headers/vs_null_root_signature.h"

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    
    command_list = context.list;
    queue = context.queue;

    uav_buffer = create_default_buffer(context.device, 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    hr = ID3D12Device_CreateRootSignature(context.device, 0, cs_null_root_signature_code_dxbc,
            sizeof(cs_null_root_signature_code_dxbc), &IID_ID3D12RootSignature, (void**)&root_signature);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    memset(&compute_desc, 0, sizeof(compute_desc));
    compute_desc.CS = cs_null_root_signature_dxbc;
    hr = ID3D12Device_CreateComputePipelineState(context.device, &compute_desc, &IID_ID3D12PipelineState, (void**)&pipeline_state);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    if (root_signature && pipeline_state)
    {
        ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_state);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list, 0, ID3D12Resource_GetGPUVirtualAddress(uav_buffer));
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(command_list, 1, 1, 0);
        ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);
    }

    transition_resource_state(command_list, uav_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(uav_buffer, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);

    value = get_readback_uint(&rb, 0, 0, 0);
    ok(value == 1, "Got unexpected readback value %u.\n", value);

    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    if (root_signature)
        ID3D12RootSignature_Release(root_signature);
    if (pipeline_state)
        ID3D12PipelineState_Release(pipeline_state);

    ID3D12Resource_Release(uav_buffer);

    hr = ID3D12Device_CreateRootSignature(context.device, 0, vs_null_root_signature_code_dxbc,
            sizeof(vs_null_root_signature_code_dxbc), &IID_ID3D12RootSignature, (void**)&root_signature);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    memset(&graphics_desc, 0, sizeof(graphics_desc));
    graphics_desc.VS = vs_null_root_signature_dxbc;
    graphics_desc.PS = ps_null_root_signature_dxbc;
    graphics_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    graphics_desc.SampleMask = 0xffffffffu;
    graphics_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    graphics_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    graphics_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphics_desc.NumRenderTargets = 1;
    graphics_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    graphics_desc.SampleDesc.Count = 1;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &graphics_desc, &IID_ID3D12PipelineState, (void**)&pipeline_state);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    if (root_signature && pipeline_state)
    {
        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, green, 0);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    }

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    if (root_signature)
        ID3D12RootSignature_Release(root_signature);
    if (pipeline_state)
        ID3D12PipelineState_Release(pipeline_state);

    destroy_test_context(&context);
}

void test_mismatching_pso_stages(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    struct test_context_desc context_desc;
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pipeline;
    struct test_context context;
    HRESULT hr;

#include "shaders/pso/headers/vs_create_pso.h"
#include "shaders/pso/headers/ps_create_pso.h"

    static const D3D12_INPUT_ELEMENT_DESC input_elements[] =
    {
        { "POS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_render_target = true;
    context_desc.no_root_signature = true;
    if (!init_test_context(&context, &context_desc))
        return;

    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = NULL;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    create_root_signature(context.device, &root_signature_desc, &root_signature);

    memset(&pso_desc, 0, sizeof(pso_desc));
    pso_desc.VS = vs_create_pso_dxbc;
    pso_desc.PS = ps_create_pso_dxbc;
    pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.pRootSignature = root_signature;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.InputLayout.NumElements = ARRAY_SIZE(input_elements);
    pso_desc.InputLayout.pInputElementDescs = input_elements;
    pso_desc.SampleDesc.Count = 1;
    pso_desc.SampleDesc.Quality = 0;
    pso_desc.SampleMask = ~0u;

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pipeline);
    ok(SUCCEEDED(hr), "Unexpected hr #%x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12PipelineState_Release(pipeline);

    pso_desc.PS = vs_create_pso_dxbc;

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pipeline);
    ok(hr == E_INVALIDARG, "Unexpected hr #%x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12PipelineState_Release(pipeline);

    ID3D12RootSignature_Release(root_signature);
    destroy_test_context(&context);
}

void test_pipeline_no_ps_nonzero_rts(void)
{
    const FLOAT white[] = { 100.0f, 100.0f, 100.0f, 100.0f };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct depth_stencil_resource ds;
    D3D12_INPUT_LAYOUT_DESC layout;
    D3D12_INPUT_ELEMENT_DESC elem;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    ID3D12DescriptorHeap *rtv;
    ID3D12Resource *vbo;
    ID3D12Resource *rt;
    D3D12_VIEWPORT vp;
    D3D12_RECT sci;

    static const FLOAT vbo_data[] =
    {
        -1.0f, -1.0f, 0.5f, 1.0f,
        +3.0f, -1.0f, 0.5f, 1.0f,
        -1.0f, +3.0f, 0.5f, 1.0f,
    };

#include "shaders/pso/headers/vs_no_ps.h"

    layout.NumElements = 1;
    layout.pInputElementDescs = &elem;
    memset(&elem, 0, sizeof(elem));
    elem.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    elem.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    elem.SemanticName = "A";

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    desc.no_render_target = true;

    if (!init_test_context(&context, &desc))
        return;

    init_depth_stencil(&ds, context.device, 1, 1, 1, 1, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, NULL);
    rt = create_default_texture2d(context.device, 1, 1, 1, 1, DXGI_FORMAT_R32_FLOAT,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

    rtv = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    init_pipeline_state_desc(&pso, context.root_signature, DXGI_FORMAT_R8G8B8A8_UNORM, &vs_no_ps_dxbc, NULL, &layout);
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.PS.BytecodeLength = 0;
    pso.PS.pShaderBytecode = NULL;

    rtv_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv);

    ID3D12Device_CreateGraphicsPipelineState(context.device, &pso, &IID_ID3D12PipelineState, (void**)&context.pipeline_state);
    ID3D12Device_CreateRenderTargetView(context.device, rt, NULL, rtv_handle);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, rtv_handle, white, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, ds.dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    set_viewport(&vp, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
    set_rect(&sci, 0, 0, 1, 1);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &rtv_handle, TRUE, &ds.dsv_handle);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    vbo = create_upload_buffer(context.device, sizeof(vbo_data), vbo_data);
    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vbo);
    vbv.SizeInBytes = sizeof(vbo_data);
    vbv.StrideInBytes = 16;
    ID3D12GraphicsCommandList_IASetVertexBuffers(context.list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(context.list, ds.texture, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);

    /* Verify depth buffer was written to. */
    check_sub_resource_float(ds.texture, 0, context.queue, context.list, 0.5f, 0);
    reset_command_list(context.list, context.allocator);
    /* Verify that the invalid R32_FLOAT RTV was just ignored. */
    check_sub_resource_float(rt, 0, context.queue, context.list, 100.0f, 0);

    ID3D12Resource_Release(rt);
    ID3D12Resource_Release(vbo);
    ID3D12DescriptorHeap_Release(rtv);
    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_topology_triangle_fan(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS15 options15;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_INPUT_LAYOUT_DESC input_layout_desc;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    struct test_context context;
    ID3D12Resource *vbo;
    HRESULT hr;

#include "shaders/pso/headers/ps_triangle_fan.h"
#include "shaders/pso/headers/vs_triangle_fan.h"

    static const D3D12_INPUT_ELEMENT_DESC input_elements[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    static const float black[] = { 0.0f, 0.0f, 0.0f, 0.0f };

    static const float vertex_data[6][2] =
    {
        {  0.0f,  0.0f },
        { -1.0f,  1.0f },
        {  1.0f,  1.0f },
        {  1.0f, -1.0f },
        { -1.0f, -1.0f },
        { -1.0f,  1.0f },
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_root_signature = true;

    if (!init_test_context(&context, &desc))
        return;

    memset(&options15, 0, sizeof(options15));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS15, &options15, sizeof(options15));

    if (!options15.TriangleFanSupported)
    {
        skip("Triangle fan topology not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    vbo = create_upload_buffer(context.device, sizeof(vertex_data), vertex_data);
    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vbo);
    vbv.SizeInBytes = sizeof(vertex_data);
    vbv.StrideInBytes = 2 * sizeof(float);

    memset(&input_layout_desc, 0, sizeof(input_layout_desc));
    input_layout_desc.pInputElementDescs = input_elements;
    input_layout_desc.NumElements = ARRAY_SIZE(input_elements);

    /* Enable plain additive blending. If this was to be rendered with an
     * incorrect topology, we'd observe overdraw or missing coverage. */
    init_pipeline_state_desc(&pso_desc, context.root_signature,
            DXGI_FORMAT_R8G8B8A8_UNORM, &vs_triangle_fan_dxbc, &ps_triangle_fan_dxbc,
            &input_layout_desc);
    pso_desc.BlendState.RenderTarget[0].BlendEnable = true;
    pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, black, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLEFAN);
    ID3D12GraphicsCommandList_IASetVertexBuffers(context.list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 6, 1, 0, 0);

    transition_resource_state(context.list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, context.queue, context.list, 0x80808080u, 1);

    ID3D12Resource_Release(vbo);

    destroy_test_context(&context);
}

void test_dynamic_depth_bias(void)
{
    ID3D12PipelineState *pso_with_bias, *pso_no_bias, *pso_static_bias;
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16;
    ID3D12GraphicsCommandList9 *command_list9;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle;
    D3D12_QUERY_HEAP_DESC query_heap_desc;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12Resource *ds, *query_buffer;
    ID3D12DescriptorHeap *dsv_heap;
    struct test_context_desc desc;
    D3D12_ROOT_PARAMETER rs_param;
    const uint64_t *readback_data;
    struct test_context context;
    ID3D12QueryHeap *query_heap;
    D3D12_VIEWPORT viewport;
    ID3D12Device2 *device2;
    D3D12_RECT scissor;
    unsigned int i;
    HRESULT hr;

    struct
    {
        float depth;
        float slope;
    } push_args;

#include "shaders/pso/headers/vs_dynamic_depth_bias.h"

    static const union d3d12_root_signature_subobject root_signature_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
        NULL, /* fill in dynamically */
    } };

    static const union d3d12_shader_bytecode_subobject vs_subobject = { { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,
            { vs_dynamic_depth_bias_code_dxbc, sizeof(vs_dynamic_depth_bias_code_dxbc) } } };

    static const union d3d12_sample_mask_subobject sample_mask_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK,
        0xFFFFFFFFu
    } };

    static const union d3d12_blend_subobject blend_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,
        { FALSE, FALSE },
    } };

    static const union d3d12_rasterizer_subobject rasterizer_with_bias_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
        { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE,
            FALSE, 1, 0.0f, 1.0f, TRUE, FALSE, FALSE, 0,
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF },
    } };

    static const union d3d12_rasterizer_subobject rasterizer_no_bias_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
        { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE,
            FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, 0,
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF },
    } };

    static const union d3d12_depth_stencil_subobject depth_stencil_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL,
        { TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_GREATER, FALSE, 0x00, 0x00,
            { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS },
            { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS } },
    } };

    static const union d3d12_input_layout_subobject input_layout_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT,
        { NULL, 0 },
    } };

    static const union d3d12_ib_strip_cut_value_subobject ib_strip_cut_value_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE,
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
    } };

    static const union d3d12_primitive_topology_subobject primitive_topology_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    } };

    static const union d3d12_render_target_formats_subobject render_target_formats_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
        { { DXGI_FORMAT_UNKNOWN }, 0 },
    } };

    static const union d3d12_depth_stencil_format_subobject depth_stencil_format_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,
        DXGI_FORMAT_D32_FLOAT,
    } };

    static const union d3d12_sample_desc_subobject sample_desc_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,
        { 1, 0 },
    } };

    static const union d3d12_flags_subobject flags_dynamic_bias_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS,
        D3D12_PIPELINE_STATE_FLAG_DYNAMIC_DEPTH_BIAS,
    } };

    static const union d3d12_flags_subobject flags_static_bias_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS,
        D3D12_PIPELINE_STATE_FLAG_NONE,
    } };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject vertex_shader;
        union d3d12_blend_subobject blend;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_rasterizer_subobject rasterizer;
        union d3d12_depth_stencil_subobject depth_stencil;
        union d3d12_input_layout_subobject input_layout;
        union d3d12_ib_strip_cut_value_subobject strip_cut;
        union d3d12_primitive_topology_subobject primitive_topology;
        union d3d12_render_target_formats_subobject render_target_formats;
        union d3d12_depth_stencil_format_subobject depth_stencil_format;
        union d3d12_sample_desc_subobject sample_desc;
        union d3d12_flags_subobject flags_desc;
    }
    pso_desc =
    {
        root_signature_subobject,
        vs_subobject,
        blend_subobject,
        sample_mask_subobject,
        rasterizer_with_bias_subobject,
        depth_stencil_subobject,
        input_layout_subobject,
        ib_strip_cut_value_subobject,
        primitive_topology_subobject,
        render_target_formats_subobject,
        depth_stencil_format_subobject,
        sample_desc_subobject,
        flags_dynamic_bias_subobject,
    };

    const D3D12_PIPELINE_STATE_STREAM_DESC pso_stream = { sizeof(pso_desc), &pso_desc };

    struct
    {
        uint64_t expected;
        bool broken_on_amd;
        bool is_todo;
        bool test_skipped;
    }
    tests[] =
    {
        { 1 },
        { 1, false, true },
        { 1 },
        { 0 },
        { 1 },
        { 1 },
        { 0 },
        { 1, true },
        { 1, false, true, true },
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;

    if (!init_test_context(&context, &desc))
        return;

    memset(&options16, 0, sizeof(options16));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS16, &options16, sizeof(options16));

    if (!options16.DynamicDepthBiasSupported)
    {
        skip("DynamicDepthBiasSupported not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device2, (void**)&device2)))
    {
        skip("ID3D12Device2 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList9, (void**)&command_list9)))
    {
        skip("ID3D12GraphicsCommandList9 not supported.\n");
        ID3D12Device2_Release(device2);
        destroy_test_context(&context);
        return;
    }

    memset(&rs_param, 0, sizeof(rs_param));
    rs_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_param.Constants.Num32BitValues = sizeof(push_args) / sizeof(uint32_t);
    rs_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &rs_param;
    hr = create_root_signature(context.device, &rs_desc, &context.root_signature);

    pso_desc.root_signature.root_signature = context.root_signature;

    hr = ID3D12Device2_CreatePipelineState(device2, &pso_stream, &IID_ID3D12PipelineState, (void**)&pso_with_bias);
    ok(hr == S_OK, "Failed to create pipeline state, hr %#x.\n", hr);

    pso_desc.rasterizer = rasterizer_no_bias_subobject;

    hr = ID3D12Device2_CreatePipelineState(device2, &pso_stream, &IID_ID3D12PipelineState, (void**)&pso_no_bias);
    ok(hr == S_OK, "Failed to create pipeline state, hr %#x.\n", hr);

    pso_desc.rasterizer = rasterizer_with_bias_subobject;
    pso_desc.flags_desc = flags_static_bias_subobject;

    hr = ID3D12Device2_CreatePipelineState(device2, &pso_stream, &IID_ID3D12PipelineState, (void**)&pso_static_bias);
    ok(hr == S_OK, "Failed to create pipeline state, hr %#x.\n", hr);

    dsv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
    ds = create_default_texture2d(context.device, 4, 4, 1, 1, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    memset(&dsv_desc, 0, sizeof(dsv_desc));
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsv_heap);
    ID3D12Device_CreateDepthStencilView(context.device, ds, &dsv_desc, dsv_handle);

    memset(&query_heap_desc, 0, sizeof(query_heap_desc));
    query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    query_heap_desc.Count = ARRAY_SIZE(tests);

    hr = ID3D12Device_CreateQueryHeap(context.device, &query_heap_desc, &IID_ID3D12QueryHeap, (void**)&query_heap);
    ok(hr == S_OK, "Failed to create query heap, hr %#x.\n", hr);

    query_buffer = create_readback_buffer(context.device, sizeof(uint64_t) * query_heap_desc.Count);

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = 4.0f;
    viewport.Height = 4.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    scissor.left = 0;
    scissor.top = 0;
    scissor.right = 4;
    scissor.bottom = 4;

    ID3D12GraphicsCommandList9_OMSetRenderTargets(command_list9, 0, NULL, false, &dsv_handle);
    ID3D12GraphicsCommandList9_SetGraphicsRootSignature(command_list9, context.root_signature);
    ID3D12GraphicsCommandList9_IASetPrimitiveTopology(command_list9, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList9_RSSetViewports(command_list9, 1, &viewport);
    ID3D12GraphicsCommandList9_RSSetScissorRects(command_list9, 1, &scissor);

    /* We start with a positive depth bias, so the first draw should pass */
    ID3D12GraphicsCommandList9_ClearDepthStencilView(command_list9, dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0, 0, NULL);
    ID3D12GraphicsCommandList9_SetPipelineState(command_list9, pso_with_bias);

    push_args.depth = 0.5f;
    push_args.slope = 0.0f;
    ID3D12GraphicsCommandList9_SetGraphicsRoot32BitConstants(command_list9, 0, sizeof(push_args) / sizeof(uint32_t), &push_args, 0);

    ID3D12GraphicsCommandList9_BeginQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
    ID3D12GraphicsCommandList9_DrawInstanced(command_list9, 3, 1, 0, 0);
    ID3D12GraphicsCommandList9_EndQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);

    /* Changing the dynamic depth bias and then rebinding the same pipeline does
     * not override the dynamic depth bias, even though the spec says it should. */
    ID3D12GraphicsCommandList9_RSSetDepthBias(command_list9, 2.0f, 0.0f, 0.0f);
    ID3D12GraphicsCommandList9_SetPipelineState(command_list9, pso_with_bias);

    ID3D12GraphicsCommandList9_BeginQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 1);
    ID3D12GraphicsCommandList9_DrawInstanced(command_list9, 3, 1, 0, 0);
    ID3D12GraphicsCommandList9_EndQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 1);

    /* Binding a different pipeline does override any dynamically set depth bias,
     * so the second draw should fail. */
    ID3D12GraphicsCommandList9_ClearDepthStencilView(command_list9, dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0, 0, NULL);
    ID3D12GraphicsCommandList9_SetPipelineState(command_list9, pso_no_bias);
    ID3D12GraphicsCommandList9_RSSetDepthBias(command_list9, 1.0f, 0.0f, 0.0f);

    ID3D12GraphicsCommandList9_BeginQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 2);
    ID3D12GraphicsCommandList9_DrawInstanced(command_list9, 3, 1, 0, 0);
    ID3D12GraphicsCommandList9_EndQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 2);

    ID3D12GraphicsCommandList9_RSSetDepthBias(command_list9, 2.0f, 0.0f, 0.0f);
    ID3D12GraphicsCommandList9_SetPipelineState(command_list9, pso_with_bias);

    ID3D12GraphicsCommandList9_BeginQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 3);
    ID3D12GraphicsCommandList9_DrawInstanced(command_list9, 3, 1, 0, 0);
    ID3D12GraphicsCommandList9_EndQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 3);

    /* Ensure that slope-scaled depth bias and depth bias clamp also apply */
    ID3D12GraphicsCommandList9_ClearDepthStencilView(command_list9, dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, NULL);

    push_args.depth = 0.5f;
    push_args.slope = 0.25f;
    ID3D12GraphicsCommandList9_SetGraphicsRoot32BitConstants(command_list9, 0, sizeof(push_args) / sizeof(uint32_t), &push_args, 0);

    ID3D12GraphicsCommandList9_RSSetDepthBias(command_list9, 0.0f, 0.0f, 0.0f);
    ID3D12GraphicsCommandList9_DrawInstanced(command_list9, 3, 1, 0, 0);
    ID3D12GraphicsCommandList9_RSSetDepthBias(command_list9, 0.0f, 0.0001f, 0.01f);

    ID3D12GraphicsCommandList9_BeginQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 4);
    ID3D12GraphicsCommandList9_DrawInstanced(command_list9, 3, 1, 0, 0);
    ID3D12GraphicsCommandList9_EndQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 4);

    /* Reset clamp, this should still pass */
    ID3D12GraphicsCommandList9_RSSetDepthBias(command_list9, 0.0f, 0.0f, 0.01f);

    ID3D12GraphicsCommandList9_BeginQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 5);
    ID3D12GraphicsCommandList9_DrawInstanced(command_list9, 3, 1, 0, 0);
    ID3D12GraphicsCommandList9_EndQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 5);

    /* Reset everything to zero, this should now fail */
    ID3D12GraphicsCommandList9_RSSetDepthBias(command_list9, 0.0f, 0.0f, 0.0f);

    ID3D12GraphicsCommandList9_BeginQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 6);
    ID3D12GraphicsCommandList9_DrawInstanced(command_list9, 3, 1, 0, 0);
    ID3D12GraphicsCommandList9_EndQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 6);

    /* Ensure that binding a pipeline without the dynamic depth bias flag ignores
     * any previously set dynamic depth bias. This is broken on AMD native. */
    ID3D12GraphicsCommandList9_ClearDepthStencilView(command_list9, dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0, 0, NULL);
    ID3D12GraphicsCommandList9_RSSetDepthBias(command_list9, 0.0f, 0.0f, 0.0f);
    ID3D12GraphicsCommandList9_SetPipelineState(command_list9, pso_static_bias);

    push_args.depth = 0.5f;
    push_args.slope = 0.0f;
    ID3D12GraphicsCommandList9_SetGraphicsRoot32BitConstants(command_list9, 0, sizeof(push_args) / sizeof(uint32_t), &push_args, 0);

    ID3D12GraphicsCommandList9_BeginQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 7);
    ID3D12GraphicsCommandList9_DrawInstanced(command_list9, 3, 1, 0, 0);
    ID3D12GraphicsCommandList9_EndQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 7);

#if 0
    /* Spec says that calling RSSetDepthBias with a pipeline that does not enable
     * dynamic depth bias is undefined behaviour and might lead to device reset,
     * so do not test that here, however both AMD and Nvidia drivers are robust
     * here and will in fact apply the depth bias set via RSSetDepthBias. */
    ID3D12GraphicsCommandList9_RSSetDepthBias(command_list9, 2.0f, 0.0f, 0.0f);

    ID3D12GraphicsCommandList9_BeginQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 8);
    ID3D12GraphicsCommandList9_DrawInstanced(command_list9, 3, 1, 0, 0);
    ID3D12GraphicsCommandList9_EndQuery(command_list9, query_heap, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 8);

    tests[8].test_skipped = false;
#endif

    /* Resolve and check occlusion query data */
    ID3D12GraphicsCommandList9_ResolveQueryData(command_list9, query_heap,
            D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0, query_heap_desc.Count, query_buffer, 0);

    hr = ID3D12GraphicsCommandList9_Close(command_list9);
    ok(hr == S_OK, "Failed to close command list, hr %#x.\n", hr);

    exec_command_list(context.queue, context.list);
    wait_queue_idle(context.device, context.queue);

    hr = ID3D12Resource_Map(query_buffer, 0, NULL, (void**)&readback_data);
    ok(hr == S_OK, "Failed to map readback buffer, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        if (tests[i].test_skipped)
            continue;

        todo_if(tests[i].is_todo)
        bug_if(tests[i].broken_on_amd && is_amd_windows_device(context.device))
        ok(readback_data[i] == tests[i].expected, "Got %"PRIu64", expected %"PRIu64".\n",
                readback_data[i], tests[i].expected);
    }

    ID3D12QueryHeap_Release(query_heap);
    ID3D12DescriptorHeap_Release(dsv_heap);

    ID3D12Resource_Release(query_buffer);
    ID3D12Resource_Release(ds);

    ID3D12PipelineState_Release(pso_with_bias);
    ID3D12PipelineState_Release(pso_no_bias);
    ID3D12PipelineState_Release(pso_static_bias);

    ID3D12GraphicsCommandList9_Release(command_list9);
    ID3D12Device2_Release(device2);
    destroy_test_context(&context);
}

void test_dynamic_index_strip_cut(void)
{
    ID3D12PipelineState *pso_strip_cut_disabled_dynamic, *pso_strip_cut_enabled_dynamic;
    ID3D12PipelineState *pso_strip_cut_disabled_static, *pso_strip_cut_enabled_static;
    ID3D12Resource *index_buffer_16, *index_buffer_32, *uav_buffer;
    D3D12_FEATURE_DATA_D3D12_OPTIONS15 options15;
    ID3D12GraphicsCommandList9 *command_list9;
    D3D12_INDEX_BUFFER_VIEW ibv16, ibv32;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[2];
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Device2 *device2;
    unsigned int i;
    HRESULT hr;

#include "shaders/pso/headers/vs_index_strip_cut.h"

    static const union d3d12_root_signature_subobject root_signature_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
        NULL, /* fill in dynamically */
    } };

    static const union d3d12_shader_bytecode_subobject vs_subobject = { { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,
        { vs_index_strip_cut_code_dxbc, sizeof(vs_index_strip_cut_code_dxbc) } } };

    static const union d3d12_sample_mask_subobject sample_mask_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK,
        0xFFFFFFFFu
    } };

    static const union d3d12_blend_subobject blend_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,
        { FALSE, FALSE },
    } };

    static const union d3d12_rasterizer_subobject rasterizer_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
        { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE,
            FALSE, 1, 0.0f, 1.0f, TRUE, FALSE, FALSE, 0,
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF },
    } };

    static const union d3d12_depth_stencil_subobject depth_stencil_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL,
        { FALSE, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_ALWAYS, FALSE, 0x00, 0x00,
            { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS },
            { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS } },
    }};

    static const union d3d12_input_layout_subobject input_layout_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT,
        { NULL, 0 },
    } };

    static const union d3d12_ib_strip_cut_value_subobject ib_strip_cut_disabled_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE,
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
    } };

    static const union d3d12_ib_strip_cut_value_subobject ib_strip_cut_enabled_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE,
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF,
    } };

    static const union d3d12_primitive_topology_subobject primitive_topology_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    } };

    static const union d3d12_render_target_formats_subobject render_target_formats_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
        { { DXGI_FORMAT_UNKNOWN }, 0 },
    } };

    static const union d3d12_depth_stencil_format_subobject depth_stencil_format_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,
        DXGI_FORMAT_UNKNOWN,
    } };

    static const union d3d12_sample_desc_subobject sample_desc_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,
        { 1, 0 },
    } };

    static const union d3d12_flags_subobject flags_dynamic_strip_cut_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS,
        D3D12_PIPELINE_STATE_FLAG_DYNAMIC_INDEX_BUFFER_STRIP_CUT,
    } };

    static const union d3d12_flags_subobject flags_static_strip_cut_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS,
        D3D12_PIPELINE_STATE_FLAG_NONE,
    } };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject vertex_shader;
        union d3d12_blend_subobject blend;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_rasterizer_subobject rasterizer;
        union d3d12_depth_stencil_subobject depth_stencil;
        union d3d12_input_layout_subobject input_layout;
        union d3d12_ib_strip_cut_value_subobject strip_cut;
        union d3d12_primitive_topology_subobject primitive_topology;
        union d3d12_render_target_formats_subobject render_target_formats;
        union d3d12_depth_stencil_format_subobject depth_stencil_format;
        union d3d12_sample_desc_subobject sample_desc;
        union d3d12_flags_subobject flags_desc;
    }
    pso_desc =
    {
        root_signature_subobject,
        vs_subobject,
        blend_subobject,
        sample_mask_subobject,
        rasterizer_subobject,
        depth_stencil_subobject,
        input_layout_subobject,
        ib_strip_cut_disabled_subobject,
        primitive_topology_subobject,
        render_target_formats_subobject,
        depth_stencil_format_subobject,
        sample_desc_subobject,
        flags_static_strip_cut_subobject,
    };

    const D3D12_PIPELINE_STATE_STREAM_DESC pso_stream = { sizeof(pso_desc), &pso_desc };

    const struct
    {
        ID3D12PipelineState **pipeline;
        uint32_t index_buffer_bits;
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE strip_cut_value;
        bool set_dynamically;
        uint32_t expected;
    }
    tests[] =
    {
        /* Test binding dynamic state pipeline to a fresh command list */
        { &pso_strip_cut_enabled_dynamic,   16, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF,      false, 0 },
        /* Test IASetIndexBufferStripCutValue behaviour */
        { &pso_strip_cut_disabled_dynamic,  16, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,    false, 1 },
        { NULL,                             16, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF,      true,  0 },
        { NULL,                             32, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF,  true,  0 },
        { NULL,                             32, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,    true,  1 },
        /* Test that re-binding the same PSO reapplies static PSO state. */
        { &pso_strip_cut_disabled_dynamic,  16, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF,      true,  0 },
        { &pso_strip_cut_disabled_dynamic,  16, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,    false, 1 },
        /* Test that disabling strip cut dynamically works as well */
        { &pso_strip_cut_enabled_dynamic,   16, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,    true,  1 },
        /* Test switching between static and dynamic pipelines */
        { &pso_strip_cut_enabled_static,    16, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF,      false, 0 },
        { &pso_strip_cut_disabled_dynamic,  16, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,    false, 1 },
        { &pso_strip_cut_disabled_static,   16, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,    false, 1 },
        { &pso_strip_cut_enabled_dynamic,   16, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF,      false, 0 },
    };

    static const uint16_t index16_data[] = { 0u, 1u, 2u, 0xffffu,     3u, 4u, 5u };
    static const uint32_t index32_data[] = { 0u, 1u, 2u, 0xffffffffu, 3u, 4u, 5u };

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;

    if (!init_test_context(&context, &desc))
        return;

    if (is_amd_windows_device(context.device))
    {
        skip("Dynamic index buffer strip cut is broken on AMD (native).\n");
        destroy_test_context(&context);
        return;
    }

    memset(&options15, 0, sizeof(options15));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS15, &options15, sizeof(options15));

    if (!options15.DynamicIndexBufferStripCutSupported)
    {
        skip("DynamicIndexBufferStripCutSupported not supported.\n");
        destroy_test_context(&context);
        return;
    }
    if (FAILED(ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device2, (void**)&device2)))
    {
        skip("ID3D12Device2 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList9, (void**)&command_list9)))
    {
        skip("ID3D12GraphicsCommandList9 not supported.\n");
        ID3D12Device2_Release(device2);
        destroy_test_context(&context);
        return;
    }

    memset(&rs_params, 0, sizeof(rs_params));
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rs_params[0].Descriptor.ShaderRegister = 0u;
    rs_params[0].Descriptor.RegisterSpace = 0u;
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_params[1].Constants.ShaderRegister = 0u;
    rs_params[1].Constants.RegisterSpace = 0u;
    rs_params[1].Constants.Num32BitValues = 2u;
    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    hr = create_root_signature(context.device, &rs_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    pso_desc.root_signature.root_signature = context.root_signature;

    /* Create pipelines with all possible permutations of dynamic/static strip
     * cut values and strip cut being statically enabled/disabled, so that we
     * can test behaviour of switching between states. */
    pso_desc.strip_cut = ib_strip_cut_disabled_subobject;
    pso_desc.flags_desc = flags_static_strip_cut_subobject;
    hr = ID3D12Device2_CreatePipelineState(device2, &pso_stream, &IID_ID3D12PipelineState, (void **)&pso_strip_cut_disabled_static);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    pso_desc.flags_desc = flags_dynamic_strip_cut_subobject;
    hr = ID3D12Device2_CreatePipelineState(device2, &pso_stream, &IID_ID3D12PipelineState, (void **)&pso_strip_cut_disabled_dynamic);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    pso_desc.strip_cut = ib_strip_cut_enabled_subobject;
    pso_desc.flags_desc = flags_static_strip_cut_subobject;
    hr = ID3D12Device2_CreatePipelineState(device2, &pso_stream, &IID_ID3D12PipelineState, (void **)&pso_strip_cut_enabled_static);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    pso_desc.flags_desc = flags_dynamic_strip_cut_subobject;
    hr = ID3D12Device2_CreatePipelineState(device2, &pso_stream, &IID_ID3D12PipelineState, (void **)&pso_strip_cut_enabled_dynamic);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    index_buffer_16 = create_upload_buffer(context.device, sizeof(index16_data), index16_data);
    index_buffer_32 = create_upload_buffer(context.device, sizeof(index32_data), index32_data);

    uav_buffer = create_default_buffer(context.device, ARRAY_SIZE(tests) * sizeof(uint32_t),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ibv16.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(index_buffer_16);
    ibv16.SizeInBytes = sizeof(index16_data);
    ibv16.Format = DXGI_FORMAT_R16_UINT;

    ibv32.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(index_buffer_32);
    ibv32.SizeInBytes = sizeof(index32_data);
    ibv32.Format = DXGI_FORMAT_R32_UINT;

    ID3D12GraphicsCommandList9_SetGraphicsRootSignature(command_list9, context.root_signature);
    ID3D12GraphicsCommandList9_SetGraphicsRootUnorderedAccessView(command_list9,
            0, ID3D12Resource_GetGPUVirtualAddress(uav_buffer));
    ID3D12GraphicsCommandList9_IASetPrimitiveTopology(command_list9, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList9_RSSetViewports(command_list9, 1, &context.viewport);
    ID3D12GraphicsCommandList9_RSSetScissorRects(command_list9, 1, &context.scissor_rect);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        if (tests[i].pipeline)
            ID3D12GraphicsCommandList9_SetPipelineState(command_list9, *tests[i].pipeline);

        ID3D12GraphicsCommandList9_SetGraphicsRoot32BitConstant(command_list9, 1, i, 0);
        ID3D12GraphicsCommandList9_SetGraphicsRoot32BitConstant(command_list9, 1, tests[i].index_buffer_bits == 32 ? 0xffffffffu : 0xffffu, 1);
        ID3D12GraphicsCommandList9_IASetIndexBuffer(command_list9, tests[i].index_buffer_bits == 32 ? &ibv32 : &ibv16);

        if (tests[i].set_dynamically)
            ID3D12GraphicsCommandList9_IASetIndexBufferStripCutValue(command_list9, tests[i].strip_cut_value);

        ID3D12GraphicsCommandList9_DrawIndexedInstanced(command_list9,
                ARRAY_SIZE(index16_data), 1, 0, 0, 0);
    }

    transition_resource_state(context.list, uav_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(uav_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        uint32_t value = get_readback_uint(&rb, i, 0, 0);
        ok(tests[i].expected == value, "Got %u, expected %u at %u.\n", value, tests[i].expected, i);
    }

    release_resource_readback(&rb);

    ID3D12Resource_Release(uav_buffer);
    ID3D12Resource_Release(index_buffer_16);
    ID3D12Resource_Release(index_buffer_32);

    ID3D12PipelineState_Release(pso_strip_cut_disabled_static);
    ID3D12PipelineState_Release(pso_strip_cut_disabled_dynamic);
    ID3D12PipelineState_Release(pso_strip_cut_enabled_static);
    ID3D12PipelineState_Release(pso_strip_cut_enabled_dynamic);

    ID3D12GraphicsCommandList9_Release(command_list9);
    ID3D12Device2_Release(device2);
    destroy_test_context(&context);
}

void test_line_rasterization(void)
{
    const FLOAT white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    D3D12_FEATURE_DATA_D3D12_OPTIONS19 options19;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12DescriptorHeap *rtv_heap;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12PipelineState *pso;
    D3D12_VIEWPORT viewport;
    ID3D12Device2 *device2;
    unsigned int i, x, y;
    ID3D12Resource *rt;
    D3D12_RECT scissor;
    uint16_t coverage;
    bool has_alpha;
    HRESULT hr;

#include "shaders/pso/headers/ps_line_raster.h"
#include "shaders/pso/headers/vs_line_raster.h"

    static const union d3d12_root_signature_subobject root_signature_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
        NULL, /* fill in dynamically */
    } };

    static const union d3d12_shader_bytecode_subobject vs_subobject = { { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, { vs_line_raster_code_dxbc, sizeof(vs_line_raster_code_dxbc) } } };
    static const union d3d12_shader_bytecode_subobject ps_subobject = { { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_line_raster_code_dxbc, sizeof(ps_line_raster_code_dxbc) } } };

    static const union d3d12_sample_mask_subobject sample_mask_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK,
        0xFFFFFFFFu
    } };

    static const union d3d12_blend_subobject blend_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,
        { FALSE, TRUE, { { TRUE, FALSE,
            D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
            D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD,
            D3D12_LOGIC_OP_NOOP, 0xfu } } },
    } };

    static const union d3d12_rasterizer_subobject rasterizer_plain_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
        { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE,
            FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, 0,
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF },
    } };

    static const union d3d12_rasterizer2_subobject rasterizer_desc2_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2,
        { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE,
            FALSE, 0, 0.0f, 0.0f, TRUE,
            D3D12_LINE_RASTERIZATION_MODE_ALIASED, 0,
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF },
    } };

    static const union d3d12_depth_stencil_subobject depth_stencil_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL,
        { FALSE, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_ALWAYS, FALSE, 0x00, 0x00,
            { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS },
            { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS } },
    } };

    static const union d3d12_input_layout_subobject input_layout_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT,
        { NULL, 0 },
    } };

    static const union d3d12_ib_strip_cut_value_subobject ib_strip_cut_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE,
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
    } };

    static const union d3d12_primitive_topology_subobject primitive_topology_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
    } };

    static const union d3d12_render_target_formats_subobject render_target_formats_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
        { { DXGI_FORMAT_R8_UNORM }, 1 },
    } };

    static const union d3d12_depth_stencil_format_subobject depth_stencil_format_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,
        DXGI_FORMAT_UNKNOWN,
    } };

    static const union d3d12_sample_desc_subobject sample_desc_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,
        { 1, 0 },
    } };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject vertex_shader;
        union d3d12_shader_bytecode_subobject pixel_shader;
        union d3d12_blend_subobject blend;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_rasterizer_subobject rasterizer;
        union d3d12_depth_stencil_subobject depth_stencil;
        union d3d12_input_layout_subobject input_layout;
        union d3d12_ib_strip_cut_value_subobject strip_cut;
        union d3d12_primitive_topology_subobject primitive_topology;
        union d3d12_render_target_formats_subobject render_target_formats;
        union d3d12_depth_stencil_format_subobject depth_stencil_format;
        union d3d12_sample_desc_subobject sample_desc;
    }
    pso_rs_plain_desc =
    {
        root_signature_subobject,
        vs_subobject,
        ps_subobject,
        blend_subobject,
        sample_mask_subobject,
        rasterizer_plain_subobject,
        depth_stencil_subobject,
        input_layout_subobject,
        ib_strip_cut_subobject,
        primitive_topology_subobject,
        render_target_formats_subobject,
        depth_stencil_format_subobject,
        sample_desc_subobject,
    };

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject vertex_shader;
        union d3d12_shader_bytecode_subobject pixel_shader;
        union d3d12_blend_subobject blend;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_rasterizer2_subobject rasterizer;
        union d3d12_depth_stencil_subobject depth_stencil;
        union d3d12_input_layout_subobject input_layout;
        union d3d12_ib_strip_cut_value_subobject strip_cut;
        union d3d12_primitive_topology_subobject primitive_topology;
        union d3d12_render_target_formats_subobject render_target_formats;
        union d3d12_depth_stencil_format_subobject depth_stencil_format;
        union d3d12_sample_desc_subobject sample_desc;
    }
    pso_rs_desc2_desc =
    {
        root_signature_subobject,
        vs_subobject,
        ps_subobject,
        blend_subobject,
        sample_mask_subobject,
        rasterizer_desc2_subobject,
        depth_stencil_subobject,
        input_layout_subobject,
        ib_strip_cut_subobject,
        primitive_topology_subobject,
        render_target_formats_subobject,
        depth_stencil_format_subobject,
        sample_desc_subobject,
    };

    const D3D12_PIPELINE_STATE_STREAM_DESC pso_rs_plain_stream = { sizeof(pso_rs_plain_desc), &pso_rs_plain_desc };
    const D3D12_PIPELINE_STATE_STREAM_DESC pso_rs_desc2_stream = { sizeof(pso_rs_desc2_desc), &pso_rs_desc2_desc };

    struct
    {
        const D3D12_PIPELINE_STATE_STREAM_DESC *pso_stream;
        uint16_t min_coverage;
        uint16_t max_coverage;
        bool expected_alpha;
        bool antialiased_line_enable;
        bool multisample_enable;
        D3D12_LINE_RASTERIZATION_MODE line_raster_mode;
    }
    tests[] =
    {
        /* AMD and Nvidia behave differently w.r.t. line rasterization */
        { &pso_rs_plain_stream, 0x03c0u, 0x03c0u, false, false, false },
        { &pso_rs_plain_stream, 0x13c8u, 0x17e8u, true,  true,  false },
        { &pso_rs_plain_stream, 0x03c0u, 0x17e8u, false, false, true  },
        { &pso_rs_plain_stream, 0x03c0u, 0x17e8u, false, true,  true  },
        /* Explicit line rasterization states last, we skip all tests if these are unsupported. */
        { &pso_rs_desc2_stream, 0x03c0u, 0x03c0u, false, false, false, D3D12_LINE_RASTERIZATION_MODE_ALIASED },
        { &pso_rs_desc2_stream, 0x03c0u, 0x03c0u, false, false, false, D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_NARROW },
        { &pso_rs_desc2_stream, 0x03c0u, 0x17e8u, false, false, false, D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE },
        { &pso_rs_desc2_stream, 0x13c8u, 0x17e8u, true,  false, false, D3D12_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED },
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;

    if (!init_test_context(&context, &desc))
        return;

    if (FAILED(ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device2, (void**)&device2)))
    {
        skip("ID3D12Device2 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    memset(&options19, 0, sizeof(options19));
    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS19, &options19, sizeof(options19));
    if (FAILED(hr))
    {
        skip("OPTIONS19 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    rt = create_default_texture2d(context.device, 4, 4, 1, 1, DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    rtv_handle = get_cpu_rtv_handle(&context, rtv_heap, 0);

    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Format = DXGI_FORMAT_R8_UNORM;
    ID3D12Device_CreateRenderTargetView(context.device, rt, &rtv_desc, rtv_handle);

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = 4.0f;
    viewport.Height = 4.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    scissor.left = 0;
    scissor.top = 0;
    scissor.right = 4;
    scissor.bottom = 4;

    pso_rs_plain_desc.root_signature.root_signature = context.root_signature;
    pso_rs_desc2_desc.root_signature.root_signature = context.root_signature;

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        if (tests[i].pso_stream == &pso_rs_desc2_stream && !options19.RasterizerDesc2Supported)
        {
            skip("D3D12_RASTERIZER_DESC2 not supported.\n");
            break;
        }

        if (tests[i].line_raster_mode == D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_NARROW &&
                !options19.NarrowQuadrilateralLinesSupported)
        {
            skip("D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_NARROW not supported.\n");
            continue;
        }

        pso_rs_plain_desc.rasterizer.rasterizer_desc.AntialiasedLineEnable = tests[i].antialiased_line_enable;
        pso_rs_plain_desc.rasterizer.rasterizer_desc.MultisampleEnable = tests[i].multisample_enable;

        pso_rs_desc2_desc.rasterizer.rasterizer_desc.LineRasterizationMode = tests[i].line_raster_mode;

        hr = ID3D12Device2_CreatePipelineState(device2, tests[i].pso_stream, &IID_ID3D12PipelineState, (void**)&pso);
        ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

        transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &rtv_handle, false, NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, rtv_handle, white, 0, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 2, 1, 0, 0);

        transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_texture_readback_with_command_list(rt, 0, &rb, context.queue, context.list);

        has_alpha = 0;
        coverage = 0;

        for (y = 0; y < 4; y++)
        {
            for (x = 0; x < 4; x++)
            {
                uint8_t pixel = get_readback_uint8(&rb, x, y);
                has_alpha |= pixel && pixel < 0xff;

                if (pixel < 0xff)
                    coverage |= 1u << (4 * y + x);
            }
        }

        ok(!(tests[i].min_coverage & ~coverage) && !(coverage & ~tests[i].max_coverage),
                "Got coverage %#x, expected range is %#x - %#x.\n", coverage, tests[i].min_coverage, tests[i].max_coverage);

        bug_if(tests[i].expected_alpha && is_amd_windows_device(context.device))
        ok(has_alpha == tests[i].expected_alpha, "Got alpha %u, expected %u.\n", has_alpha, tests[i].expected_alpha);

        release_resource_readback(&rb);
        reset_command_list(context.list, context.allocator);

        ID3D12PipelineState_Release(pso);
    }

    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12Resource_Release(rt);
    ID3D12Device2_Release(device2);
    destroy_test_context(&context);
}

void test_coverage_export_atoc(bool use_dxil)
{
    ID3D12PipelineState *pso_count_samples, *pso;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12RootSignature *rs_count_samples, *rs;
    ID3D12DescriptorHeap *rtv_heap, *srv_heap;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv, rtv_ms;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    struct test_context_desc context_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_DESCRIPTOR_RANGE rs_srv_range;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_ROOT_PARAMETER rs_param;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Resource *rt_ms, *rt;
    D3D12_VIEWPORT viewport;
    unsigned int i, x, y;
    D3D12_RECT scissor;
    HRESULT hr;

    static const FLOAT black[] = { 0.0f, 0.0f, 0.0f, 0.0f };

#include "shaders/pso/headers/ps_atoc.h"
#include "shaders/pso/headers/ps_count_samples.h"
#include "shaders/pso/headers/ps_export_coverage.h"

    const D3D12_SHADER_BYTECODE ps_coverage = use_dxil ? ps_export_coverage_dxil : ps_export_coverage_dxbc;
    const D3D12_SHADER_BYTECODE ps_atoc = use_dxil ? ps_atoc_dxil : ps_atoc_dxbc;

    const struct
    {
        const D3D12_SHADER_BYTECODE *ps;
        union
        {
            uint32_t u;
            float f;
        } shader_arg;
        BOOL atoc;
        uint32_t min_coverage;
        uint32_t max_coverage;
    }
    tests[] =
    {
        /* Plain AToC enable, don't be super strict, just ensure that
         * coverage is roughly proportional to the exported alpha value. */
        { &ps_atoc, { .f = 0.0f }, true, 0, 0 },
        { &ps_atoc, { .f = 0.5f }, true, 1, 3 },
        { &ps_atoc, { .f = 1.0f }, true, 4, 4 },

        /* Plain coverage export without AToC, just ensure that written
         * sample counts match that of the mask exactly. */
        { &ps_coverage, { .u = 0x0 }, false, 0, 0 },
        { &ps_coverage, { .u = 0x8 }, false, 1, 1 },
        { &ps_coverage, { .u = 0x5 }, false, 2, 2 },
        { &ps_coverage, { .u = 0xb }, false, 3, 3 },
        { &ps_coverage, { .u = 0xf }, false, 4, 4 },

        /* Coverage export with AToC. Alpha is always 0 with this shader,
         * just ensure that writes still happen regardless. */
        { &ps_coverage, { .u = 0x0 }, true, 0, 0 },
        { &ps_coverage, { .u = 0x2 }, true, 1, 1 },
        { &ps_coverage, { .u = 0x9 }, true, 2, 2 },
        { &ps_coverage, { .u = 0xd }, true, 3, 3 },
        { &ps_coverage, { .u = 0xf }, true, 4, 4 },
    };

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_render_target = true;
    context_desc.no_root_signature = true;
    if (!init_test_context(&context, &context_desc))
        return;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &rs_param;

    memset(&rs_param, 0, sizeof(rs_param));
    rs_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_param.Constants.Num32BitValues = 1;
    rs_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    hr = create_root_signature(context.device, &rs_desc, &rs);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    memset(&rs_srv_range, 0, sizeof(rs_srv_range));
    rs_srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rs_srv_range.NumDescriptors = 1;

    memset(&rs_param, 0, sizeof(rs_param));
    rs_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_param.DescriptorTable.NumDescriptorRanges = 1;
    rs_param.DescriptorTable.pDescriptorRanges = &rs_srv_range;
    rs_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    hr = create_root_signature(context.device, &rs_desc, &rs_count_samples);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Width = 4;
    resource_desc.Height = 4;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8_UINT;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL, &IID_ID3D12Resource, (void **)&rt);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    resource_desc.SampleDesc.Count = 4;
    resource_desc.Format = DXGI_FORMAT_R8_UNORM;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, NULL, &IID_ID3D12Resource, (void **)&rt_ms);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2);

    rtv = get_cpu_rtv_handle(&context, rtv_heap, 0);
    rtv_ms = get_cpu_rtv_handle(&context, rtv_heap, 1);

    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Format = DXGI_FORMAT_R8_UINT;
    rtv_desc.Texture2D.MipSlice = 0;
    rtv_desc.Texture2D.PlaneSlice = 0;
    ID3D12Device_CreateRenderTargetView(context.device, rt, &rtv_desc, rtv);

    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
    rtv_desc.Format = DXGI_FORMAT_R8_UNORM;
    ID3D12Device_CreateRenderTargetView(context.device, rt_ms, &rtv_desc, rtv_ms);

    srv_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    srv_desc.Format = DXGI_FORMAT_R8_UNORM;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    ID3D12Device_CreateShaderResourceView(context.device, rt_ms, &srv_desc,
            get_cpu_descriptor_handle(&context, srv_heap, 0));

    init_pipeline_state_desc(&pso_desc, rs_count_samples, DXGI_FORMAT_R8_UINT, NULL, &ps_count_samples_dxbc, NULL);
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pso_count_samples);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = 4.0f;
    viewport.Height = 4.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    scissor.left = 0;
    scissor.top = 0;
    scissor.right = 4;
    scissor.bottom = 4;

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        transition_resource_state(context.list, rt_ms, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        if (use_dxil)
            init_pipeline_state_desc_dxil(&pso_desc, rs, DXGI_FORMAT_R8_UNORM, NULL, tests[i].ps, NULL);
        else
            init_pipeline_state_desc(&pso_desc, rs, DXGI_FORMAT_R8_UNORM, NULL, tests[i].ps, NULL);
        pso_desc.SampleDesc.Count = 4;
        pso_desc.BlendState.AlphaToCoverageEnable = tests[i].atoc;
        hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
                &IID_ID3D12PipelineState, (void **)&pso);
        ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &scissor);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &rtv_ms, false, NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, rtv_ms, black, 0, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, rs);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstant(context.list, 0, tests[i].shader_arg.u, 0);
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

        transition_resource_state(context.list, rt_ms, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &rtv, false, NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, rtv, black, 0, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, rs_count_samples);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso_count_samples);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &srv_heap);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(context.list, 0,
                get_gpu_descriptor_handle(&context, srv_heap, 0));
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

        transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_texture_readback_with_command_list(rt, 0, &rb, context.queue, context.list);

        for (y = 0; y < 4; y++)
        {
            for (x = 0; x < 4; x++)
            {
                uint32_t count = get_readback_uint8(&rb, x, y);

                ok(count >= tests[i].min_coverage && count <= tests[i].max_coverage,
                        "Unexpected number of covered samples %u, expected %u - %u.\n",
                        count, tests[i].min_coverage, tests[i].max_coverage);
            }
        }

        release_resource_readback(&rb);
        ID3D12PipelineState_Release(pso);

        reset_command_list(context.list, context.allocator);
    }

    ID3D12PipelineState_Release(pso_count_samples);

    ID3D12Resource_Release(rt);
    ID3D12Resource_Release(rt_ms);

    /* Test coverage export behaviour with single sample. */
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Format = DXGI_FORMAT_R8_UNORM;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&rt);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Format = DXGI_FORMAT_R8_UNORM;
    rtv_desc.Texture2D.MipSlice = 0;
    rtv_desc.Texture2D.PlaneSlice = 0;
    ID3D12Device_CreateRenderTargetView(context.device, rt, &rtv_desc, rtv);

    vkd3d_test_set_context("Single sample export");

    if (use_dxil)
        init_pipeline_state_desc_dxil(&pso_desc, rs, DXGI_FORMAT_R8_UNORM, NULL, &ps_coverage, NULL);
    else
        init_pipeline_state_desc(&pso_desc, rs, DXGI_FORMAT_R8_UNORM, NULL, &ps_coverage, NULL);

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pso);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &scissor);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &rtv, false, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, rtv, black, 0, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, rs);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstant(context.list, 0, 0x0, 0);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint8(rt, 0, context.queue, context.list, 0u, 0);

    ID3D12PipelineState_Release(pso);
    ID3D12Resource_Release(rt);

    ID3D12DescriptorHeap_Release(srv_heap);
    ID3D12DescriptorHeap_Release(rtv_heap);

    ID3D12RootSignature_Release(rs);
    ID3D12RootSignature_Release(rs_count_samples);

    destroy_test_context(&context);
}

void test_coverage_export_atoc_dxbc(void)
{
    test_coverage_export_atoc(false);
}

void test_coverage_export_atoc_dxil(void)
{
    test_coverage_export_atoc(true);
}

void test_view_instancing(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc_simple;
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3;
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7;
    D3D12_COMMAND_SIGNATURE_DESC command_desc;
    ID3D12GraphicsCommandList6* command_list6;
    ID3D12GraphicsCommandList1 *command_list1;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_INDIRECT_ARGUMENT_DESC command_arg;
    D3D12_FEATURE_DATA_SHADER_MODEL model;
    struct test_context_desc context_desc;
    ID3D12CommandSignature *command_sig;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12Resource *ibo, *arg_buffer;
    ID3D12PipelineState *pso, *pso2;
    struct resource_readback rb;
    D3D12_INDEX_BUFFER_VIEW ibv;
    D3D12_ROOT_PARAMETER rs_arg;
    struct test_context context;
    D3D12_VIEWPORT viewports[4];
    unsigned int i, j, x, y;
    ID3D12Device2 *device2;
    uint32_t got, expected;
    D3D12_RECT scissors[4];
    uint32_t view_mask;
    HRESULT hr;

    FLOAT clear_value[] = { 65535.0f, 0.0f, 0.0f, 0.0f };

#include "shaders/pso/headers/ps_view_id_passthrough.h"
#include "shaders/pso/headers/ms_view_id_passthrough.h"
#include "shaders/pso/headers/vs_view_id_passthrough.h"
#include "shaders/pso/headers/vs_multiview_export_layer_viewport.h"
#include "shaders/pso/headers/gs_multiview_export_layer_viewport.h"

    static const union d3d12_shader_bytecode_subobject ps_view_id_passthrough_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_view_id_passthrough_code_dxil, sizeof(ps_view_id_passthrough_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject vs_view_id_passthrough_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, { vs_view_id_passthrough_code_dxil, sizeof(vs_view_id_passthrough_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_view_id_passthrough_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_view_id_passthrough_code_dxil, sizeof(ms_view_id_passthrough_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject vs_multiview_export_layer_viewport_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, { vs_multiview_export_layer_viewport_code_dxil, sizeof(vs_multiview_export_layer_viewport_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject gs_multiview_export_layer_viewport_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, { gs_multiview_export_layer_viewport_code_dxil, sizeof(gs_multiview_export_layer_viewport_code_dxil) } }};

    static const uint32_t index_data[] = { 0, 1, 2 };
    static const uint32_t indirect_draw_data[] = { 3, 1, 0, 0 };

    static const union d3d12_root_signature_subobject root_signature_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
        NULL, /* fill in dynamically */
    }};

    static const union d3d12_input_layout_subobject input_layout_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT,
        { NULL, 0 },
    } };

    static const union d3d12_primitive_topology_subobject primitive_topology_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    }};

    static const union d3d12_rasterizer_subobject rasterizer_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
        { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE,
            TRUE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, 0,
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF },
    }};

    static const union d3d12_sample_desc_subobject sample_desc_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,
        { 1, 0 },
    }};

    static const union d3d12_sample_mask_subobject sample_mask_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK,
        0xFFFFFFFFu
    }};

    static const union d3d12_render_target_formats_subobject render_target_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
        { { DXGI_FORMAT_R32_UINT }, 1 },
    }};

    static const union d3d12_blend_subobject blend_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,
        { FALSE, TRUE,
            {{ FALSE, FALSE,
                D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL }},
        }
    }};

    static const D3D12_VIEW_INSTANCE_LOCATION view_instance_locations_simple[] =
    {
        { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 },
    };

    static const D3D12_VIEW_INSTANCE_LOCATION view_instance_locations_shuffle[] =
    {
        { 0, 2 }, { 0, 0 }, { 0, 3 }, { 0, 1 },
    };

    static const D3D12_VIEW_INSTANCE_LOCATION view_instance_locations_viewports[] =
    {
        { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 },
    };

    static const D3D12_VIEW_INSTANCE_LOCATION view_instance_locations_mixed[] =
    {
        { 2, 1 }, { 3, 0 }, { 1, 3 },
    };

    static const D3D12_VIEW_INSTANCE_LOCATION view_instance_locations_export[] =
    {
        { 1, 1 }, { 1, 2 },
    };

    static const union d3d12_view_instancing_subobject view_instancing_simple_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING,
        { ARRAY_SIZE(view_instance_locations_simple), view_instance_locations_simple, 0 }
    }};

    static const union d3d12_view_instancing_subobject view_instancing_simple_mask_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING,
        { ARRAY_SIZE(view_instance_locations_simple), view_instance_locations_simple,
        D3D12_VIEW_INSTANCING_FLAG_ENABLE_VIEW_INSTANCE_MASKING }
    }};

    static const union d3d12_view_instancing_subobject view_instancing_shuffle_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING,
        { ARRAY_SIZE(view_instance_locations_shuffle), view_instance_locations_shuffle, 0 }
    }};

    static const union d3d12_view_instancing_subobject view_instancing_viewports_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING,
        { ARRAY_SIZE(view_instance_locations_viewports), view_instance_locations_viewports, 0 }
    }};

    static const union d3d12_view_instancing_subobject view_instancing_mixed_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING,
        { ARRAY_SIZE(view_instance_locations_mixed), view_instance_locations_mixed, 0 }
    }};

    static const union d3d12_view_instancing_subobject view_instancing_export_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING,
        { ARRAY_SIZE(view_instance_locations_export), view_instance_locations_export, 0 }
    }};

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_input_layout_subobject input_layout;
        union d3d12_primitive_topology_subobject primitive_topology;
        union d3d12_shader_bytecode_subobject vs;
        union d3d12_shader_bytecode_subobject ps;
        union d3d12_rasterizer_subobject rasterizer;
        union d3d12_sample_desc_subobject sample_desc;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_render_target_formats_subobject render_targets;
        union d3d12_blend_subobject blend;
        union d3d12_view_instancing_subobject view_instancing;
    } pso_vs_ps_desc;

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_input_layout_subobject input_layout;
        union d3d12_primitive_topology_subobject primitive_topology;
        union d3d12_shader_bytecode_subobject vs;
        union d3d12_shader_bytecode_subobject gs;
        union d3d12_shader_bytecode_subobject ps;
        union d3d12_rasterizer_subobject rasterizer;
        union d3d12_sample_desc_subobject sample_desc;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_render_target_formats_subobject render_targets;
        union d3d12_blend_subobject blend;
        union d3d12_view_instancing_subobject view_instancing;
    } pso_vs_gs_ps_desc;

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject ms;
        union d3d12_shader_bytecode_subobject ps;
        union d3d12_rasterizer_subobject rasterizer;
        union d3d12_sample_desc_subobject sample_desc;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_blend_subobject blend;
        union d3d12_render_target_formats_subobject render_targets;
        union d3d12_view_instancing_subobject view_instancing;
    } pso_ms_ps_desc;

    struct shader_args
    {
        uint32_t layer;
        uint32_t viewport;
    } shader_args;

    static const uint32_t view_mask_tests[] =
    {
        0x0,
        0x2,
        0xb,
        0xf,
    };

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_root_signature = true;
    context_desc.rt_format = DXGI_FORMAT_R32_UINT;
    context_desc.rt_array_size = 4u;
    if (!init_test_context(&context, &context_desc))
        return;

    if (is_nvidia_windows_device(context.device))
    {
        /* NV is currently too broken to run any of these tests */
        skip("Skipping view instancing tests due to known issues.\n");
        destroy_test_context(&context);
        return;
    }

    model.HighestShaderModel = D3D_SHADER_MODEL_6_1;
    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
        model.HighestShaderModel < D3D_SHADER_MODEL_6_1)
    {
        skip("Shader model 6.1 is not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_arg, 0, sizeof(rs_arg));
    rs_arg.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_arg.Constants.Num32BitValues = sizeof(shader_args) / sizeof(uint32_t);
    rs_arg.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &rs_arg;

    hr = create_root_signature(context.device, &rs_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    /* Shaders must accept SV_ViewID even if view instancing is not supported,
     * or disabled for the PSO. */
    init_pipeline_state_desc_dxil(&pso_desc_simple, context.root_signature, DXGI_FORMAT_R32_UINT,
            &vs_view_id_passthrough_dxil, &ps_view_id_passthrough_dxil, NULL);
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device,
            &pso_desc_simple, &IID_ID3D12PipelineState, (void **)&pso);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_value, 0, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, context.queue, context.list, 0, 0);
    reset_command_list(context.list, context.allocator);

    ID3D12PipelineState_Release(pso);

    memset(&options, 0, sizeof(options));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    memset(&options3, 0, sizeof(options3));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3));
    memset(&options7, 0, sizeof(options7));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));

    if (!options3.ViewInstancingTier)
    {
        skip("View instancing not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    ibo = create_upload_buffer(context.device, sizeof(index_data), index_data);

    /* Device2 is guaranteed to be provided if the feature is exposed, don't try to be robust here. */
    hr = ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device2, (void**)&device2);
    ok(hr == S_OK, "ID3D12Device2 not supported.\n");
    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList1, (void**)&command_list1);
    ok(hr == S_OK, "ID3D12GraphicsCommandList1 not supported.\n");

    /* Test basic multiviww behaviour with view index => layer index */
    memset(&pso_vs_ps_desc, 0, sizeof(pso_vs_ps_desc));
    pso_vs_ps_desc.root_signature = root_signature_subobject;
    pso_vs_ps_desc.root_signature.root_signature = context.root_signature;
    pso_vs_ps_desc.input_layout = input_layout_subobject;
    pso_vs_ps_desc.primitive_topology = primitive_topology_subobject;
    pso_vs_ps_desc.vs = vs_view_id_passthrough_subobject;
    pso_vs_ps_desc.ps = ps_view_id_passthrough_subobject;
    pso_vs_ps_desc.rasterizer = rasterizer_subobject;
    pso_vs_ps_desc.sample_mask = sample_mask_subobject;
    pso_vs_ps_desc.sample_desc = sample_desc_subobject;
    pso_vs_ps_desc.blend = blend_subobject;
    pso_vs_ps_desc.render_targets = render_target_subobject;
    pso_vs_ps_desc.view_instancing = view_instancing_simple_subobject;

    hr = create_pipeline_state_from_stream(device2, &pso_vs_ps_desc, &pso);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    /* View masking is disabled in PSO and must be ignored */
    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_value, 0, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList1_SetViewInstanceMask(command_list1, 0);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    for (i = 0; i < ARRAY_SIZE(view_instance_locations_simple); i++)
    {
        vkd3d_test_set_context("layer %u", i);

        /* NV somehow manages to mix up 1 and 3 */
        check_sub_resource_uint(context.render_target, i, context.queue, context.list, i, 0);
        reset_command_list(context.list, context.allocator);
    }

    /* Test ExecuteIndirect */
    arg_buffer = create_upload_buffer(context.device, sizeof(indirect_draw_data), indirect_draw_data);

    memset(&command_arg, 0, sizeof(command_arg));
    command_arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    memset(&command_desc, 0, sizeof(command_desc));
    command_desc.ByteStride = 16;
    command_desc.NumArgumentDescs = 1;
    command_desc.pArgumentDescs = &command_arg;

    hr = ID3D12Device_CreateCommandSignature(context.device, &command_desc, NULL, &IID_ID3D12CommandSignature, (void**)&command_sig);
    ok(hr == S_OK, "Failed to create command signature, hr %#x.\n", hr);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    /* View masking is disabled in PSO and must be ignored */
    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_value, 0, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_ExecuteIndirect(context.list, command_sig, 1, arg_buffer, 0, NULL, 0);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    for (i = 0; i < ARRAY_SIZE(view_instance_locations_simple); i++)
    {
        vkd3d_test_set_context("layer %u", i);

        check_sub_resource_uint(context.render_target, i, context.queue, context.list, i, 0);
        reset_command_list(context.list, context.allocator);
    }

    ID3D12Resource_Release(arg_buffer);
    ID3D12CommandSignature_Release(command_sig);

    /* Test basic multiview with view mask */
    pso_vs_ps_desc.view_instancing = view_instancing_simple_mask_subobject;

    hr = create_pipeline_state_from_stream(device2, &pso_vs_ps_desc, &pso2);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(view_mask_tests); i++)
    {
        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_value, 0, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso2);
        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList1_SetViewInstanceMask(command_list1, view_mask_tests[i]);
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        for (j = 0; j < ARRAY_SIZE(view_instance_locations_simple); j++)
        {
            vkd3d_test_set_context("view mask %#x, layer %u", view_mask_tests[i], j);
            expected = (view_mask_tests[i] & (1u << j)) ? j : 0xffff;

            check_sub_resource_uint(context.render_target, j, context.queue, context.list, expected, 0);
            reset_command_list(context.list, context.allocator);
        }
    }

    /* Behaviour with invalid view masks is inconsistent, WARP ignores the second call whereas
     * AMD native masks the last four bits. Accept either behaviour. */
    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_value, 0, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso2);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList1_SetViewInstanceMask(command_list1, 0x3);
    ID3D12GraphicsCommandList1_SetViewInstanceMask(command_list1, 0xfc);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    view_mask = 0;

    for (i = 0; i < ARRAY_SIZE(view_instance_locations_simple); i++)
    {
        vkd3d_test_set_context("layer %u", i);

        get_texture_readback_with_command_list(context.render_target, i, &rb, context.queue, context.list);

        if (get_readback_uint(&rb, 0, 0, 0) != 0xffff)
            view_mask |= 1u << i;

        release_resource_readback(&rb);

        reset_command_list(context.list, context.allocator);
    }

    ok(view_mask == 0x3 || view_mask == 0xc, "Unexpected view mask %#x.\n", view_mask);

    /* Binding an unmasked PSO does not affect the dynamic view mask. Native
     * NV drivers will crash in SetViewInstanceMask if no PSO is bound. */
    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList1_SetViewInstanceMask(command_list1, 0x1);

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_value, 0, NULL);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_value, 0, NULL);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso2);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    for (i = 0; i < ARRAY_SIZE(view_instance_locations_simple); i++)
    {
        vkd3d_test_set_context("layer %u", i);
        check_sub_resource_uint(context.render_target, i, context.queue, context.list, i ? 0xffff : 0, 0);
        reset_command_list(context.list, context.allocator);
    }

    ID3D12PipelineState_Release(pso2);
    ID3D12PipelineState_Release(pso);

    /* Test shuffling layers */
    pso_vs_ps_desc.view_instancing = view_instancing_shuffle_subobject;

    hr = create_pipeline_state_from_stream(device2, &pso_vs_ps_desc, &pso);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_value, 0, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    for (i = 0; i < ARRAY_SIZE(view_instance_locations_shuffle); i++)
    {
        vkd3d_test_set_context("layer %u", view_instance_locations_shuffle[i].RenderTargetArrayIndex);

        /* NV once again mixes up views 1 and 3 here */
        check_sub_resource_uint(context.render_target, view_instance_locations_shuffle[i].RenderTargetArrayIndex, context.queue, context.list, i, 0);
        reset_command_list(context.list, context.allocator);
    }

    ID3D12PipelineState_Release(pso);

    /* Test single layer with multiple viewports. Also use the opportunity to test indexed draws. */
    pso_vs_ps_desc.view_instancing = view_instancing_viewports_subobject;

    hr = create_pipeline_state_from_stream(device2, &pso_vs_ps_desc, &pso);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(viewports); i++)
    {
        scissors[i].left = (i & 1) * 2;
        scissors[i].top = (i & 2);
        scissors[i].right = scissors[i].left + 2;
        scissors[i].bottom = scissors[i].top + 2;

        viewports[i].TopLeftX = (float)scissors[i].left;
        viewports[i].TopLeftY = (float)scissors[i].top;
        viewports[i].Width = 2.0f;
        viewports[i].Height = 2.0f;
        viewports[i].MinDepth = 0.0f;
        viewports[i].MaxDepth = 1.0f;
    }

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ibv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(ibo);
    ibv.Format = DXGI_FORMAT_R32_UINT;
    ibv.SizeInBytes = sizeof(index_data);

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_value, 0, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, ARRAY_SIZE(viewports), viewports);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, ARRAY_SIZE(scissors), scissors);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_IASetIndexBuffer(context.list, &ibv);
    ID3D12GraphicsCommandList_DrawIndexedInstanced(context.list, 3, 1, 0, 0, 0);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, context.queue, context.list);
    reset_command_list(context.list, context.allocator);

    for (y = 0; y < 4; y++)
    {
        for (x = 0; x < 4; x++)
        {
            got = get_readback_uint(&rb, x, y, 0);
            expected = (x / 2) + 2 * (y / 2);

            ok(got == expected, "Got %#x, expected %#x at (%u,%u).\n", got, expected, x, y);
        }
    }

    release_resource_readback(&rb);

    for (i = 1; i < 4; i++)
    {
        check_sub_resource_uint(context.render_target, i, context.queue, context.list, 0xffff, 0);
        reset_command_list(context.list, context.allocator);
    }

    ID3D12PipelineState_Release(pso);

    /* Test a mixture of layer and viewport indices */
    pso_vs_ps_desc.view_instancing = view_instancing_mixed_subobject;

    hr = create_pipeline_state_from_stream(device2, &pso_vs_ps_desc, &pso);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_value, 0, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, ARRAY_SIZE(viewports), viewports);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, ARRAY_SIZE(scissors), scissors);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    for (i = 0; i < ARRAY_SIZE(view_instance_locations_mixed); i++)
    {
        const D3D12_RECT* sci = &scissors[view_instance_locations_mixed[i].ViewportArrayIndex];

        get_texture_readback_with_command_list(context.render_target,
            view_instance_locations_mixed[i].RenderTargetArrayIndex, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        for (y = 0; y < 4; y++)
        {
            for (x = 0; x < 4; x++)
            {
                got = get_readback_uint(&rb, x, y, 0);
                expected = (x >= (unsigned int)sci->left && x < (unsigned int)sci->right &&
                        y >= (unsigned int)sci->top && y < (unsigned int)sci->bottom) ? i : 0xffff;

                /* This is once again all over the place on NV */
                ok(got == expected, "Got %#x, expected %#x at (%u,%u).\n", got, expected, x, y);
            }
        }

        release_resource_readback(&rb);
    }

    check_sub_resource_uint(context.render_target, 2, context.queue, context.list, 0xffff, 0);
    reset_command_list(context.list, context.allocator);

    ID3D12PipelineState_Release(pso);

    if (options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation)
    {
        /* Test layer/viewport bias. Simple positive numbers for now. */
        pso_vs_ps_desc.view_instancing = view_instancing_export_subobject;
        pso_vs_ps_desc.vs = vs_multiview_export_layer_viewport_subobject;

        hr = create_pipeline_state_from_stream(device2, &pso_vs_ps_desc, &pso);
        ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

        for (shader_args.layer = 0; shader_args.layer <= 1; shader_args.layer++)
        {
            for (shader_args.viewport = 0; shader_args.viewport <= 2; shader_args.viewport++)
            {
                transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

                ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
                ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_value, 0, NULL);
                ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
                ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
                ID3D12GraphicsCommandList_RSSetViewports(context.list, ARRAY_SIZE(viewports), viewports);
                ID3D12GraphicsCommandList_RSSetScissorRects(context.list, ARRAY_SIZE(scissors), scissors);
                ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(context.list, 0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
                ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

                transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

                for (i = 0; i < ARRAY_SIZE(view_instance_locations_export); i++)
                {
                    const D3D12_RECT* sci = &scissors[view_instance_locations_export[i].ViewportArrayIndex + shader_args.viewport];

                    get_texture_readback_with_command_list(context.render_target,
                        view_instance_locations_export[i].RenderTargetArrayIndex + shader_args.layer, &rb, context.queue, context.list);
                    reset_command_list(context.list, context.allocator);

                    for (y = 0; y < 4; y++)
                    {
                        for (x = 0; x < 4; x++)
                        {
                            got = get_readback_uint(&rb, x, y, 0);
                            expected = (shader_args.layer << 8) | (shader_args.viewport << 16);

                            if (x < (unsigned int)sci->left || x >= (unsigned int)sci->right ||
                                y < (unsigned int)sci->top || y >= (unsigned int)sci->bottom)
                            {
                                expected = 0xffff;
                            }

                            /* AMD does not respect the layer/viewport bias correctly if per-view layer indices
                             * are sequential. */
                            bug_if(is_amd_windows_device(context.device) && got != expected)
                            ok(got == expected, "Got %#x, expected %#x at (%u,%u).\n", got, expected, x, y);
                        }
                    }

                    release_resource_readback(&rb);
                }
            }
        }

        ID3D12PipelineState_Release(pso);
    }
    else
    {
        skip("VS layer/viewport index not supported.\n");
    }

    /* Test layer/viewport biasing from GS, with VS propagating the offsets. */
    memset(&pso_vs_gs_ps_desc, 0, sizeof(pso_vs_gs_ps_desc));
    pso_vs_gs_ps_desc.root_signature = root_signature_subobject;
    pso_vs_gs_ps_desc.root_signature.root_signature = context.root_signature;
    pso_vs_gs_ps_desc.input_layout = input_layout_subobject;
    pso_vs_gs_ps_desc.primitive_topology = primitive_topology_subobject;
    pso_vs_gs_ps_desc.vs = vs_multiview_export_layer_viewport_subobject;
    pso_vs_gs_ps_desc.gs = gs_multiview_export_layer_viewport_subobject;
    pso_vs_gs_ps_desc.ps = ps_view_id_passthrough_subobject;
    pso_vs_gs_ps_desc.rasterizer = rasterizer_subobject;
    pso_vs_gs_ps_desc.sample_mask = sample_mask_subobject;
    pso_vs_gs_ps_desc.sample_desc = sample_desc_subobject;
    pso_vs_gs_ps_desc.blend = blend_subobject;
    pso_vs_gs_ps_desc.render_targets = render_target_subobject;
    pso_vs_gs_ps_desc.view_instancing = view_instancing_export_subobject;

    hr = create_pipeline_state_from_stream(device2, &pso_vs_gs_ps_desc, &pso);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    for (shader_args.layer = 0; shader_args.layer <= 1; shader_args.layer++)
    {
        for (shader_args.viewport = 0; shader_args.viewport <= 2; shader_args.viewport++)
        {
            transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

            ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
            ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_value, 0, NULL);
            ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
            ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
            ID3D12GraphicsCommandList_RSSetViewports(context.list, ARRAY_SIZE(viewports), viewports);
            ID3D12GraphicsCommandList_RSSetScissorRects(context.list, ARRAY_SIZE(scissors), scissors);
            ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(context.list, 0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
            ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

            transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

            for (i = 0; i < ARRAY_SIZE(view_instance_locations_export); i++)
            {
                const D3D12_RECT* sci = &scissors[view_instance_locations_export[i].ViewportArrayIndex + shader_args.viewport];

                get_texture_readback_with_command_list(context.render_target,
                    view_instance_locations_export[i].RenderTargetArrayIndex + shader_args.layer, &rb, context.queue, context.list);
                reset_command_list(context.list, context.allocator);

                for (y = 0; y < 4; y++)
                {
                    for (x = 0; x < 4; x++)
                    {
                        uint32_t color = get_readback_uint(&rb, x, y, 0);
                        uint32_t expected = i | (shader_args.layer << 8) | (shader_args.viewport << 16);

                        if (x < (unsigned int)sci->left || x >= (unsigned int)sci->right ||
                            y < (unsigned int)sci->top || y >= (unsigned int)sci->bottom)
                            expected = 0xffff;

                        ok(color == expected, "Got %#x, expected %#x at (%u,%u).\n", color, expected, x, y);
                    }
                }

                release_resource_readback(&rb);
            }
        }
    }

    ID3D12PipelineState_Release(pso);

    if (options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1)
    {
        hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList6, (void**)&command_list6);
        ok(hr == S_OK, "ID3D12GraphicsCommandList6 not supported.\n");

        memset(&pso_ms_ps_desc, 0, sizeof(pso_ms_ps_desc));
        pso_ms_ps_desc.root_signature = root_signature_subobject;
        pso_ms_ps_desc.root_signature.root_signature = context.root_signature;
        pso_ms_ps_desc.ms = ms_view_id_passthrough_subobject;
        pso_ms_ps_desc.ps = ps_view_id_passthrough_subobject;
        pso_ms_ps_desc.rasterizer = rasterizer_subobject;
        pso_ms_ps_desc.sample_mask = sample_mask_subobject;
        pso_ms_ps_desc.sample_desc = sample_desc_subobject;
        pso_ms_ps_desc.render_targets = render_target_subobject;
        pso_ms_ps_desc.blend = blend_subobject;
        pso_ms_ps_desc.view_instancing = view_instancing_simple_subobject;

        hr = create_pipeline_state_from_stream(device2, &pso_ms_ps_desc, &pso);
        ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_value, 0, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList6_DispatchMesh(command_list6, 1, 1, 1);

        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        for (i = 0; i < ARRAY_SIZE(view_instance_locations_simple); i++)
        {
            vkd3d_test_set_context("layer %u", i);

            /* AMD only renders first layer, NV renders nothing */
            bug_if(is_amd_windows_device(context.device))
            check_sub_resource_uint(context.render_target, i, context.queue, context.list, i, 0);

            reset_command_list(context.list, context.allocator);
        }

        ID3D12PipelineState_Release(pso);

        ID3D12GraphicsCommandList6_Release(command_list6);
    }
    else
    {
        skip("Mesh shaders not supported by device.\n");
    }

    ID3D12Resource_Release(ibo);
    ID3D12GraphicsCommandList1_Release(command_list1);
    ID3D12Device2_Release(device2);
    destroy_test_context(&context);
}

void test_shader_io_mismatch(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3;
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7;
    struct test_context_desc context_desc;
    ID3D12RootSignature *root_signature;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct test_context context;
    ID3D12PipelineState *pso;
    UINT supported_features;
    ID3D12Device2 *device2;
    HRESULT hr, expected;
    unsigned int i;

#include "shaders/pso/headers/ms_mismatch.h"
#include "shaders/pso/headers/ms_mismatch_primid.h"
#include "shaders/pso/headers/ms_mismatch_min16float.h"
#include "shaders/pso/headers/vs_mismatch.h"
#include "shaders/pso/headers/vs_mismatch_min16float.h"
#include "shaders/pso/headers/gs_mismatch_ref.h"
#include "shaders/pso/headers/gs_mismatch_so_1.h"
#include "shaders/pso/headers/gs_mismatch_so_2.h"
#include "shaders/pso/headers/gs_mismatch_1.h"
#include "shaders/pso/headers/gs_mismatch_2.h"
#include "shaders/pso/headers/gs_mismatch_3.h"
#include "shaders/pso/headers/gs_mismatch_4.h"
#include "shaders/pso/headers/gs_mismatch_5.h"
#include "shaders/pso/headers/gs_mismatch_primid.h"
#include "shaders/pso/headers/hs_mismatch_ref.h"
#include "shaders/pso/headers/hs_mismatch_1.h"
#include "shaders/pso/headers/hs_mismatch_2.h"
#include "shaders/pso/headers/hs_mismatch_3.h"
#include "shaders/pso/headers/hs_mismatch_4.h"
#include "shaders/pso/headers/hs_mismatch_5.h"
#include "shaders/pso/headers/ds_mismatch_ref.h"
#include "shaders/pso/headers/ds_mismatch_1.h"
#include "shaders/pso/headers/ds_mismatch_2.h"
#include "shaders/pso/headers/ds_mismatch_3.h"
#include "shaders/pso/headers/ds_mismatch_4.h"
#include "shaders/pso/headers/ds_mismatch_5.h"
#include "shaders/pso/headers/ds_mismatch_6.h"
#include "shaders/pso/headers/ps_mismatch_ref.h"
#include "shaders/pso/headers/ps_mismatch_so_1.h"
#include "shaders/pso/headers/ps_mismatch_so_2.h"
#include "shaders/pso/headers/ps_mismatch_1.h"
#include "shaders/pso/headers/ps_mismatch_2.h"
#include "shaders/pso/headers/ps_mismatch_3.h"
#include "shaders/pso/headers/ps_mismatch_4.h"
#include "shaders/pso/headers/ps_mismatch_5.h"
#include "shaders/pso/headers/ps_mismatch_6.h"
#include "shaders/pso/headers/ps_mismatch_7.h"
#include "shaders/pso/headers/ps_mismatch_8.h"
#include "shaders/pso/headers/ps_mismatch_9.h"
#include "shaders/pso/headers/ps_mismatch_10.h"
#include "shaders/pso/headers/ps_mismatch_sv_1.h"
#include "shaders/pso/headers/ps_mismatch_sv_2.h"
#include "shaders/pso/headers/ps_mismatch_sv_3.h"
#include "shaders/pso/headers/ps_mismatch_sv_4.h"
#include "shaders/pso/headers/ps_mismatch_sv_5.h"
#include "shaders/pso/headers/ps_mismatch_sv_6.h"
#include "shaders/pso/headers/ps_mismatch_min16float.h"

#define FEATURE_BARYCENTRICS  (1 << 0)

    static const union d3d12_root_signature_subobject root_signature_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
        NULL, /* fill in dynamically */
    }};

    static const union d3d12_rasterizer_subobject rasterizer_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
        { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE,
            TRUE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, 0,
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF },
    }};

    static const union d3d12_sample_desc_subobject sample_desc_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,
        { 1, 0 },
    }};

    static const union d3d12_sample_mask_subobject sample_mask_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK,
        0xFFFFFFFFu
    }};

    static const union d3d12_render_target_formats_subobject render_target_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
        { { DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32A32_UINT }, 3 },
    }};

    static const union d3d12_blend_subobject blend_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,
        { FALSE, TRUE,
            {{ FALSE, FALSE,
                D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
                D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL }},
        }
    }};

    struct
    {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject ms;
        union d3d12_shader_bytecode_subobject ps;
        union d3d12_rasterizer_subobject rasterizer;
        union d3d12_sample_desc_subobject sample_desc;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_blend_subobject blend;
        union d3d12_render_target_formats_subobject render_targets;
    } pso_ms_desc;

    static const struct
    {
        const D3D12_SHADER_BYTECODE *ms;
        const D3D12_SHADER_BYTECODE *vs;
        const D3D12_SHADER_BYTECODE *hs;
        const D3D12_SHADER_BYTECODE *ds;
        const D3D12_SHADER_BYTECODE *gs;
        const D3D12_SHADER_BYTECODE *ps;
        bool vs_should_compile;
        bool ms_should_compile;
        UINT required_features;
    }
    tests[] =
    {
        /* Basic MS/VS -> PS */
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_ref_dxil, true, true },
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_1_dxil, true,  false },
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_2_dxil, false, false },
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_3_dxil, false, true  },
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_4_dxil, false, false },
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_5_dxil, false, false },
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_6_dxil, false, false },
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_7_dxil, true,  true  },
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_8_dxil, false, false },
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_9_dxil, true, false },
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_10_dxil, true,  true  },

        /* HS -> DS */
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_ref_dxil, true  },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_1_dxil,   &ds_mismatch_1_dxil,   NULL, &ps_mismatch_ref_dxil, true  },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_1_dxil,   &ds_mismatch_ref_dxil, NULL, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_1_dxil,   NULL, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_2_dxil,   NULL, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_1_dxil,   &ds_mismatch_3_dxil,   NULL, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_4_dxil,   NULL, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_1_dxil,   &ds_mismatch_5_dxil,   NULL, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_6_dxil,   NULL, &ps_mismatch_ref_dxil, false },

        /* VS -> HS */
        { NULL, &vs_mismatch_dxil, &hs_mismatch_2_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_ref_dxil, true  },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_3_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_ref_dxil, true  },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_4_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_5_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_ref_dxil, false },

        /* DS -> PS */
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_1_dxil, true  },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_2_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_3_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_4_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_5_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_6_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_7_dxil, true  },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_8_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_9_dxil, true  },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, NULL, &ps_mismatch_10_dxil, true  },

        /* GS -> PS */
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_ref_dxil, &ps_mismatch_ref_dxil, true },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_ref_dxil, &ps_mismatch_1_dxil, true  },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_ref_dxil, &ps_mismatch_2_dxil, false },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_ref_dxil, &ps_mismatch_3_dxil, false },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_ref_dxil, &ps_mismatch_4_dxil, false },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_ref_dxil, &ps_mismatch_5_dxil, false },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_ref_dxil, &ps_mismatch_6_dxil, false },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_ref_dxil, &ps_mismatch_7_dxil, true  },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_ref_dxil, &ps_mismatch_8_dxil, false },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_ref_dxil, &ps_mismatch_9_dxil, true  },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_ref_dxil, &ps_mismatch_10_dxil, true  },

        /* VS -> GS */
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_1_dxil, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_2_dxil, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_3_dxil, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_4_dxil, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_5_dxil, &ps_mismatch_ref_dxil, true  },

        /* DS -> GS */
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, &gs_mismatch_ref_dxil, &ps_mismatch_ref_dxil, true },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, &gs_mismatch_1_dxil, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, &gs_mismatch_2_dxil, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, &gs_mismatch_3_dxil, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, &gs_mismatch_4_dxil, &ps_mismatch_ref_dxil, false },
        { NULL, &vs_mismatch_dxil, &hs_mismatch_ref_dxil, &ds_mismatch_ref_dxil, &gs_mismatch_5_dxil, &ps_mismatch_ref_dxil, true  },

        /* Test some system value behaviour */
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_sv_1_dxil, true,  true,  FEATURE_BARYCENTRICS },
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_sv_2_dxil, false, true,  FEATURE_BARYCENTRICS },

        /* Test primitive ID behaviour */
        { NULL, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_sv_3_dxil, true  },
        { NULL, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_sv_4_dxil, true  },
        { NULL, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_sv_5_dxil, false },
        { NULL, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_sv_6_dxil, true  },

        { &ms_mismatch_primid_dxil, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_primid_dxil, &ps_mismatch_sv_3_dxil, true,  true  },
        { &ms_mismatch_primid_dxil, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_primid_dxil, &ps_mismatch_sv_4_dxil, false, true  },
        { &ms_mismatch_primid_dxil, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_primid_dxil, &ps_mismatch_sv_5_dxil, true,  true  },
        { &ms_mismatch_primid_dxil, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_primid_dxil, &ps_mismatch_sv_6_dxil, false, true  },

        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_ref_dxbc, &ps_mismatch_sv_3_dxil, false },
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, &gs_mismatch_ref_dxbc, &ps_mismatch_sv_4_dxil, false },

        /* DXBC ends up with the same results, but uses slightly different signatures */
        { NULL, &vs_mismatch_dxbc, NULL, NULL, NULL, &ps_mismatch_sv_3_dxbc, true  },
        { NULL, &vs_mismatch_dxbc, NULL, NULL, NULL, &ps_mismatch_sv_4_dxbc, true  },
        { NULL, &vs_mismatch_dxbc, NULL, NULL, NULL, &ps_mismatch_sv_5_dxbc, false },
        { NULL, &vs_mismatch_dxbc, NULL, NULL, NULL, &ps_mismatch_sv_6_dxbc, true  },

        { NULL, &vs_mismatch_dxbc, NULL, NULL, &gs_mismatch_primid_dxbc, &ps_mismatch_sv_3_dxbc, true  },
        { NULL, &vs_mismatch_dxbc, NULL, NULL, &gs_mismatch_primid_dxbc, &ps_mismatch_sv_4_dxbc, false },
        { NULL, &vs_mismatch_dxbc, NULL, NULL, &gs_mismatch_primid_dxbc, &ps_mismatch_sv_5_dxbc, true  },
        { NULL, &vs_mismatch_dxbc, NULL, NULL, &gs_mismatch_primid_dxbc, &ps_mismatch_sv_6_dxbc, false },

        { NULL, &vs_mismatch_dxbc, NULL, NULL, &gs_mismatch_ref_dxbc, &ps_mismatch_sv_3_dxbc, false },
        { NULL, &vs_mismatch_dxbc, NULL, NULL, &gs_mismatch_ref_dxbc, &ps_mismatch_sv_4_dxbc, false },

        /* min16float matching, mostly relevant for SM5.0 */
        { &ms_mismatch_min16float_dxil, &vs_mismatch_min16float_dxil, NULL, NULL, NULL, &ps_mismatch_min16float_dxil, true, true },
        { &ms_mismatch_min16float_dxil, &vs_mismatch_min16float_dxil, NULL, NULL, NULL, &ps_mismatch_ref_dxil, false, false },
        { &ms_mismatch_dxil, &vs_mismatch_dxil, NULL, NULL, NULL, &ps_mismatch_min16float_dxil, false, false },

        { NULL, &vs_mismatch_min16float_dxbc, NULL, NULL, NULL, &ps_mismatch_min16float_dxbc, true, true },
        { NULL, &vs_mismatch_min16float_dxbc, NULL, NULL, NULL, &ps_mismatch_ref_dxbc, false, false },
        { NULL, &vs_mismatch_dxbc, NULL, NULL, NULL, &ps_mismatch_min16float_dxbc, false, false },
    };

    VKD3D_UNUSED static const uint32_t so_strides[] = { 64, 64 };

    static const D3D12_SO_DECLARATION_ENTRY so_basic[] =
    {
        { 0, "SV_POSITION", 0, 0, 4, 0 },
    };

    static const D3D12_SO_DECLARATION_ENTRY so_multi_stream[] =
    {
        { 0, "SV_POSITION", 0, 0, 4, 0 },
        { 0, "ARG", 0, 0, 3, 0 },
        { 0, "ARG", 2, 0, 4, 0 },
        { 1, "ARG", 1, 0, 2, 1 },
    };

    VKD3D_UNUSED static const struct
    {
        const D3D12_SHADER_BYTECODE *vs;
        const D3D12_SHADER_BYTECODE *gs;
        const D3D12_SHADER_BYTECODE *ps;
        UINT so_entry_count;
        const D3D12_SO_DECLARATION_ENTRY *so_entries;
        UINT rasterized_stream;
        bool should_compile;
    }
    so_tests[] =
    {
        { &vs_mismatch_dxil, &gs_mismatch_ref_dxil, NULL,                   ARRAY_SIZE(so_basic), so_basic, D3D12_SO_NO_RASTERIZED_STREAM, true },
        { &vs_mismatch_dxil, &gs_mismatch_ref_dxil, &ps_mismatch_ref_dxil,  ARRAY_SIZE(so_basic), so_basic, D3D12_SO_NO_RASTERIZED_STREAM, true },
        { &vs_mismatch_dxil, &gs_mismatch_ref_dxil, &ps_mismatch_ref_dxil,  ARRAY_SIZE(so_basic), so_basic, 0, true },
        { &vs_mismatch_dxil, &gs_mismatch_ref_dxil, &ps_mismatch_ref_dxil,  ARRAY_SIZE(so_basic), so_basic, 0, true },
        { &vs_mismatch_dxil, &gs_mismatch_ref_dxil, &ps_mismatch_3_dxil,    ARRAY_SIZE(so_basic), so_basic, D3D12_SO_NO_RASTERIZED_STREAM, true },
        { &vs_mismatch_dxil, &gs_mismatch_ref_dxil, &ps_mismatch_3_dxil,    ARRAY_SIZE(so_basic), so_basic, 0, false },

        { &vs_mismatch_dxil, &gs_mismatch_so_1_dxil, &ps_mismatch_ref_dxil,  ARRAY_SIZE(so_multi_stream), so_multi_stream, D3D12_SO_NO_RASTERIZED_STREAM, true },
        { &vs_mismatch_dxil, &gs_mismatch_so_1_dxil, &ps_mismatch_ref_dxil,  ARRAY_SIZE(so_multi_stream), so_multi_stream, 0, false },
        { &vs_mismatch_dxil, &gs_mismatch_so_1_dxil, &ps_mismatch_ref_dxil,  ARRAY_SIZE(so_multi_stream), so_multi_stream, 1, false },
        { &vs_mismatch_dxil, &gs_mismatch_so_1_dxil, &ps_mismatch_3_dxil,    ARRAY_SIZE(so_multi_stream), so_multi_stream, 0, true  },
        { &vs_mismatch_dxil, &gs_mismatch_so_1_dxil, &ps_mismatch_3_dxil,    ARRAY_SIZE(so_multi_stream), so_multi_stream, 1, true  },
        { &vs_mismatch_dxil, &gs_mismatch_so_1_dxil, &ps_mismatch_so_1_dxil, ARRAY_SIZE(so_multi_stream), so_multi_stream, 0, false },
        { &vs_mismatch_dxil, &gs_mismatch_so_1_dxil, &ps_mismatch_so_1_dxil, ARRAY_SIZE(so_multi_stream), so_multi_stream, 1, false },

        { &vs_mismatch_dxil, &gs_mismatch_so_2_dxil, &ps_mismatch_so_2_dxil, ARRAY_SIZE(so_multi_stream), so_multi_stream, 0, false },
    };

    static const D3D12_INPUT_ELEMENT_DESC input_descs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "FROG", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_render_target = true;
    context_desc.no_root_signature = true;
    if (!init_test_context(&context, &context_desc))
        return;

    memset(&options3, 0, sizeof(options3));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3));
    memset(&options7, 0, sizeof(options7));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));

    supported_features = 0;

    if (options3.BarycentricsSupported)
        supported_features |= FEATURE_BARYCENTRICS;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;

    create_root_signature(context.device, &rs_desc, &root_signature);

    /* Test input layout compatibility */
    init_pipeline_state_desc(&pso_desc, root_signature, DXGI_FORMAT_UNKNOWN,
            &vs_mismatch_dxil, &ps_mismatch_ref_dxil, NULL);
    pso_desc.NumRenderTargets = render_target_subobject.render_target_formats.NumRenderTargets;

    for (i = 0; i < pso_desc.NumRenderTargets; i++)
        pso_desc.RTVFormats[i] = render_target_subobject.render_target_formats.RTFormats[i];

    vkd3d_mute_validation_message("08733", "Testing out of spec behavior with stage IO");

    for (i = 0; i < ARRAY_SIZE(input_descs); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        pso_desc.InputLayout.NumElements = 1;
        pso_desc.InputLayout.pInputElementDescs = &input_descs[i];

        /* Types aren't validated, but fail if a semantic is not provided */
        expected = i > 1 ? E_INVALIDARG : S_OK;

        hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        ok(hr == expected, "Got hr %#x, expected %#x.\n", hr, expected);

        if (SUCCEEDED(hr))
            ID3D12PipelineState_Release(pso);
    }

    vkd3d_unmute_validation_message("08733");

    /* Test general shader interface compatibility */
    pso_desc.InputLayout.NumElements = 1;
    pso_desc.InputLayout.pInputElementDescs = &input_descs[0];

    vkd3d_mute_validation_message("07754", "Testing out of spec behavior with stage IO");

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        if (!tests[i].vs)
            continue;

        if ((supported_features & tests[i].required_features) != tests[i].required_features)
        {
            skip("Features %#x not supported.\n", tests[i].required_features);
            continue;
        }

        expected = tests[i].vs_should_compile ? S_OK : E_INVALIDARG;

        memset(&pso_desc.HS, 0, sizeof(pso_desc.HS));
        memset(&pso_desc.DS, 0, sizeof(pso_desc.DS));
        memset(&pso_desc.GS, 0, sizeof(pso_desc.GS));
        memset(&pso_desc.PS, 0, sizeof(pso_desc.PS));

        pso_desc.VS = *tests[i].vs;

        if (tests[i].hs)
        {
            pso_desc.HS = *tests[i].hs;
            pso_desc.DS = *tests[i].ds;

            pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
        }
        else
            pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        if (tests[i].gs)
            pso_desc.GS = *tests[i].gs;

        if (tests[i].ps)
            pso_desc.PS = *tests[i].ps;

        hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        ok(hr == expected, "Got hr %#x, expected %#x.\n", hr, expected);

        if (SUCCEEDED(hr))
            ID3D12PipelineState_Release(pso);
    }

    vkd3d_unmute_validation_message("07754");

    /* Test some stream output scenarios with or without rasterized stream */
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    memset(&pso_desc.HS, 0, sizeof(pso_desc.HS));
    memset(&pso_desc.DS, 0, sizeof(pso_desc.DS));

    /* TODO Fix DXBC/DXIL translation bugs that currently lead to
     * mesa asserts with these tests */
#if 0
    for (i = 0; i < ARRAY_SIZE(so_tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        expected = so_tests[i].should_compile ? S_OK : E_INVALIDARG;

        memset(&pso_desc.PS, 0, sizeof(pso_desc.PS));

        pso_desc.VS = *so_tests[i].vs;
        pso_desc.GS = *so_tests[i].gs;

        if (so_tests[i].ps)
            pso_desc.PS = *so_tests[i].ps;

        pso_desc.StreamOutput.RasterizedStream = so_tests[i].rasterized_stream;
        pso_desc.StreamOutput.NumEntries = so_tests[i].so_entry_count;
        pso_desc.StreamOutput.pSODeclaration = so_tests[i].so_entries;
        pso_desc.StreamOutput.NumStrides = ARRAY_SIZE(so_strides);
        pso_desc.StreamOutput.pBufferStrides = so_strides;

        hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
        ok(hr == expected, "Got hr %#x, expected %#x.\n", hr, expected);

        if (SUCCEEDED(hr))
            ID3D12PipelineState_Release(pso);
    }
#endif

    ID3D12RootSignature_Release(root_signature);

    /* Run mesh shader tests */
    if (options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1)
    {
        rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        create_root_signature(context.device, &rs_desc, &root_signature);

        hr = ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device2, (void**)&device2);

        memset(&pso_ms_desc, 0, sizeof(pso_ms_desc));
        pso_ms_desc.root_signature = root_signature_subobject;
        pso_ms_desc.root_signature.root_signature = root_signature;
        pso_ms_desc.ms.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
        pso_ms_desc.ps.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
        pso_ms_desc.rasterizer = rasterizer_subobject;
        pso_ms_desc.sample_desc = sample_desc_subobject;
        pso_ms_desc.sample_mask = sample_mask_subobject;
        pso_ms_desc.blend = blend_subobject;
        pso_ms_desc.render_targets = render_target_subobject;

        for (i = 0; i < ARRAY_SIZE(tests); i++)
        {
            vkd3d_test_set_context("Test %u", i);

            if (!tests[i].ms)
                continue;

            if ((supported_features & tests[i].required_features) != tests[i].required_features)
            {
                skip("Features %#x not supported.\n", tests[i].required_features);
                continue;
            }

            expected = tests[i].ms_should_compile ? S_OK : E_INVALIDARG;

            memset(&pso_ms_desc.ps.shader_bytecode, 0, sizeof(pso_ms_desc.ps.shader_bytecode));
            pso_ms_desc.ms.shader_bytecode = *tests[i].ms;

            if (tests[i].ps)
                pso_ms_desc.ps.shader_bytecode = *tests[i].ps;

            hr = create_pipeline_state_from_stream(device2, &pso_ms_desc, &pso);
            ok(hr == expected, "Got hr %#x, expected %#x.\n", hr, expected);

            if (SUCCEEDED(hr))
                ID3D12PipelineState_Release(pso);
        }

        ID3D12Device2_Release(device2);
        ID3D12RootSignature_Release(root_signature);
    }

    destroy_test_context(&context);

#undef FEATURE_VRS_TIER_2
#undef FEATURE_BARYCENTRICS
}

void test_gs_topology_mismatch(bool dxil)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    struct test_context_desc context_desc;
    struct test_context context;
    ID3D12PipelineState *pso;
    HRESULT hr, expected;
    unsigned int i, j;

#include "shaders/pso/headers/vs_topology.h"
#include "shaders/pso/headers/hs_topology_point.h"
#include "shaders/pso/headers/hs_topology_line.h"
#include "shaders/pso/headers/hs_topology_triangle.h"
#include "shaders/pso/headers/ds_topology_line.h"
#include "shaders/pso/headers/ds_topology_triangle.h"
#include "shaders/pso/headers/gs_topology_point.h"
#include "shaders/pso/headers/gs_topology_line.h"
#include "shaders/pso/headers/gs_topology_line_adj.h"
#include "shaders/pso/headers/gs_topology_triangle.h"
#include "shaders/pso/headers/gs_topology_triangle_adj.h"

    static const D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_types[] =
    {
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT,
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    };

    static const struct
    {
        D3D12_PRIMITIVE_TOPOLOGY_TYPE topology;
        const D3D12_SHADER_BYTECODE *dxbc;
        const D3D12_SHADER_BYTECODE *dxil;
        bool uses_adjacency;
    }
    geometry_shaders[] =
    {
        { D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT,    &gs_topology_point_dxbc,        &gs_topology_point_dxil,        false },
        { D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,     &gs_topology_line_dxbc,         &gs_topology_line_dxil,         false },
        { D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,     &gs_topology_line_adj_dxbc,     &gs_topology_line_adj_dxil,     true  },
        { D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, &gs_topology_triangle_dxbc,     &gs_topology_triangle_dxil,     false },
        { D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, &gs_topology_triangle_adj_dxbc, &gs_topology_triangle_adj_dxil, true  },
    };

    static const struct
    {
        D3D12_PRIMITIVE_TOPOLOGY_TYPE topology;
        const D3D12_SHADER_BYTECODE *hs_dxbc;
        const D3D12_SHADER_BYTECODE *hs_dxil;
        const D3D12_SHADER_BYTECODE *ds_dxbc;
        const D3D12_SHADER_BYTECODE *ds_dxil;
    }
    tess_shaders[] =
    {
        { D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT,    &hs_topology_point_dxbc,        &hs_topology_point_dxil,
                                                  &ds_topology_triangle_dxbc,     &ds_topology_triangle_dxil },
        { D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,     &hs_topology_line_dxbc,         &hs_topology_line_dxil,
                                                  &ds_topology_line_dxbc,         &ds_topology_line_dxil },
        { D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, &hs_topology_triangle_dxbc,     &hs_topology_triangle_dxil,
                                                  &ds_topology_triangle_dxbc,     &ds_topology_triangle_dxil },
    };

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_render_target = true;
    context_desc.no_root_signature = true;
    if (!init_test_context(&context, &context_desc))
        return;

    context.root_signature = create_empty_root_signature(context.device, 0u);

    /* GS input topology is validated against the primitive topology type */
    init_pipeline_state_desc(&pso_desc, context.root_signature, DXGI_FORMAT_UNKNOWN, NULL, NULL, NULL);
    pso_desc.VS = dxil ? vs_topology_dxil : vs_topology_dxbc;
    pso_desc.PS = shader_bytecode(NULL, 0u);

    for (i = 0; i < ARRAY_SIZE(topology_types); i++)
    {
        pso_desc.PrimitiveTopologyType = topology_types[i];

        for (j = 0; j < ARRAY_SIZE(geometry_shaders); j++)
        {
            vkd3d_test_set_context("Test %u,%u", i, j);
            pso_desc.GS = dxil ? *geometry_shaders[j].dxil : *geometry_shaders[j].dxbc;

            hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&pso);
            expected = geometry_shaders[j].topology == topology_types[i] ? S_OK : E_INVALIDARG;

            ok(hr == expected, "Got hr %#x, expected %#x.\n", hr, expected);

            if (hr == S_OK)
                ID3D12PipelineState_Release(pso);
        }
    }

    /* If the pipeline has tessellation shaders, GS input topology is validated
     * against the DS output topology, and adjacency is not allowed. */
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

    if (!dxil)
        vkd3d_mute_validation_message("08737", "See vkd3d-proton issue 2378");

    for (i = 0; i < ARRAY_SIZE(tess_shaders); i++)
    {
        pso_desc.HS = dxil ? *tess_shaders[i].hs_dxil : *tess_shaders[i].hs_dxbc;
        pso_desc.DS = dxil ? *tess_shaders[i].ds_dxil : *tess_shaders[i].ds_dxbc;

        for (j = 0; j < ARRAY_SIZE(geometry_shaders); j++)
        {
            vkd3d_test_set_context("Test %u,%u", i, j);
            pso_desc.GS = dxil ? *geometry_shaders[j].dxil : *geometry_shaders[j].dxbc;

            hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&pso);

            expected = (geometry_shaders[j].topology == tess_shaders[i].topology &&
                    !geometry_shaders[j].uses_adjacency) ? S_OK : E_INVALIDARG;

            ok(hr == expected, "Got hr %#x, expected %#x.\n", hr, expected);

            if (hr == S_OK)
                ID3D12PipelineState_Release(pso);
        }
    }

    if (!dxil)
        vkd3d_unmute_validation_message("08737");

    destroy_test_context(&context);
}

void test_gs_topology_mismatch_dxbc(void)
{
    test_gs_topology_mismatch(false);
}

void test_gs_topology_mismatch_dxil(void)
{
    test_gs_topology_mismatch(true);
}
