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

    static const DWORD dxbc_code[] =
    {
#if 0
        [numthreads(1, 1, 1)]
        void main() { }
#endif
        0x43425844, 0x1acc3ad0, 0x71c7b057, 0xc72c4306, 0xf432cb57, 0x00000001, 0x00000074, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000020, 0x00050050, 0x00000008, 0x0100086a,
        0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x0100003e,
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

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    memset(&pipeline_state_desc, 0, sizeof(pipeline_state_desc));
    pipeline_state_desc.pRootSignature = root_signature;
    pipeline_state_desc.CS = shader_bytecode(dxbc_code, sizeof(dxbc_code));
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

    static const DWORD ps_code[] =
    {
#if 0
        uint main() : SV_Target
        {
            return 10;
        }
#endif
        0x43425844, 0x9f26b611, 0xc59570a7, 0x9b327871, 0xb1015fc6, 0x00000001, 0x000000a8, 0x00000003,
        0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
        0x00000e01, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x00000030, 0x00000050, 0x0000000c,
        0x0100086a, 0x03000065, 0x00102012, 0x00000000, 0x05000036, 0x00102012, 0x00000000, 0x00004001,
        0x0000000a, 0x0100003e,
    };

    static const DWORD ps_code_no_rt[] =
    {
#if 0
        void main()
        {
        }
#endif
        0x43425844, 0x499d4ed5, 0xbbe2842c, 0x179313ee, 0xde5cd5d9, 0x00000001, 0x00000064, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000010, 0x00000050, 0x00000004, 0x0100086a,
        0x0100003e,
    };

    static const D3D12_SHADER_BYTECODE ps = { ps_code, sizeof(ps_code) };
    static const D3D12_SHADER_BYTECODE ps_no_rt = { ps_code_no_rt, sizeof(ps_code_no_rt) };

    struct test
    {
        HRESULT hr;
        const D3D12_SHADER_BYTECODE *ps;
        UINT8 write_mask;
    };
    static const struct test tests[] =
    {
        { S_OK, &ps_no_rt, D3D12_COLOR_WRITE_ENABLE_ALL },
        { E_INVALIDARG, &ps, 0 },
        { E_INVALIDARG, &ps, D3D12_COLOR_WRITE_ENABLE_ALL },
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

    static const DWORD cs_code[] =
    {
#if 0
        [numthreads(1, 1, 1)]
        void main() { }
#endif
        0x43425844, 0x1acc3ad0, 0x71c7b057, 0xc72c4306, 0xf432cb57, 0x00000001, 0x00000074, 0x00000003,
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000020, 0x00050050, 0x00000008, 0x0100086a,
        0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x0100003e,
    };

    static const DWORD vs_code[] =
    {
#if 0
        float4 main(float4 pos : POS) : SV_POSITION {
                return pos;
        }
#endif
        0x43425844, 0xd0f999d3, 0x5250b8b9, 0x32f55488, 0x0498c795, 0x00000001, 0x000000d4, 0x00000003,
        0x0000002c, 0x00000058, 0x0000008c, 0x4e475349, 0x00000024, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00534f50, 0x4e47534f, 0x0000002c,
        0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f,
        0x505f5653, 0x5449534f, 0x004e4f49, 0x58454853, 0x00000040, 0x00010050, 0x00000010, 0x0100086a,
        0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x05000036,
        0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };

    static const DWORD ps_code[] =
    {
#if 0
        float4 main() : SV_TARGET {
                return float4(1.0f, 1.0f, 1.0f, 1.0f);
        }
#endif
        0x43425844, 0x29b14cf3, 0xb991cf90, 0x9e455ffc, 0x4675b046, 0x00000001, 0x000000b4, 0x00000003,
        0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x58454853, 0x0000003c, 0x00000050, 0x0000000f,
        0x0100086a, 0x03000065, 0x001020f2, 0x00000000, 0x08000036, 0x001020f2, 0x00000000, 0x00004002,
        0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x0100003e,
    };

    static const union d3d12_root_signature_subobject root_signature_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
        NULL, /* fill in dynamically */
    }};

    static const union d3d12_shader_bytecode_subobject vs_subobject = {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, { vs_code, sizeof(vs_code) } }};
    static const union d3d12_shader_bytecode_subobject ps_subobject = {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_code, sizeof(ps_code) } }};
    static const union d3d12_shader_bytecode_subobject cs_subobject = {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, { cs_code, sizeof(cs_code) } }};

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

    static const DWORD vs_code[] =
    {
#if 0
        struct vertex
        {
            float4 position : SV_Position;
            float2 t0 : TEXCOORD0;
            nointerpolation float t1 : TEXCOORD1;
            uint t2 : TEXCOORD2;
            uint t3 : TEXCOORD3;
            float t4 : TEXCOORD4;
        };

        void main(in vertex vin, out vertex vout)
        {
            vout = vin;
        }
#endif
        0x43425844, 0x561ea178, 0x7b8f454c, 0x69091b4f, 0xf28d9a01, 0x00000001, 0x000002c0, 0x00000003,
        0x0000002c, 0x000000e4, 0x0000019c, 0x4e475349, 0x000000b0, 0x00000006, 0x00000008, 0x00000098,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x000000a4, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000303, 0x000000a4, 0x00000001, 0x00000000, 0x00000003, 0x00000002,
        0x00000101, 0x000000a4, 0x00000002, 0x00000000, 0x00000001, 0x00000003, 0x00000101, 0x000000a4,
        0x00000003, 0x00000000, 0x00000001, 0x00000004, 0x00000101, 0x000000a4, 0x00000004, 0x00000000,
        0x00000003, 0x00000005, 0x00000101, 0x505f5653, 0x7469736f, 0x006e6f69, 0x43584554, 0x44524f4f,
        0xababab00, 0x4e47534f, 0x000000b0, 0x00000006, 0x00000008, 0x00000098, 0x00000000, 0x00000001,
        0x00000003, 0x00000000, 0x0000000f, 0x000000a4, 0x00000000, 0x00000000, 0x00000003, 0x00000001,
        0x00000c03, 0x000000a4, 0x00000004, 0x00000000, 0x00000003, 0x00000001, 0x00000b04, 0x000000a4,
        0x00000001, 0x00000000, 0x00000003, 0x00000002, 0x00000e01, 0x000000a4, 0x00000002, 0x00000000,
        0x00000001, 0x00000002, 0x00000d02, 0x000000a4, 0x00000003, 0x00000000, 0x00000001, 0x00000002,
        0x00000b04, 0x505f5653, 0x7469736f, 0x006e6f69, 0x43584554, 0x44524f4f, 0xababab00, 0x58454853,
        0x0000011c, 0x00010050, 0x00000047, 0x0100086a, 0x0300005f, 0x001010f2, 0x00000000, 0x0300005f,
        0x00101032, 0x00000001, 0x0300005f, 0x00101012, 0x00000002, 0x0300005f, 0x00101012, 0x00000003,
        0x0300005f, 0x00101012, 0x00000004, 0x0300005f, 0x00101012, 0x00000005, 0x04000067, 0x001020f2,
        0x00000000, 0x00000001, 0x03000065, 0x00102032, 0x00000001, 0x03000065, 0x00102042, 0x00000001,
        0x03000065, 0x00102012, 0x00000002, 0x03000065, 0x00102022, 0x00000002, 0x03000065, 0x00102042,
        0x00000002, 0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x05000036, 0x00102032,
        0x00000001, 0x00101046, 0x00000001, 0x05000036, 0x00102042, 0x00000001, 0x0010100a, 0x00000005,
        0x05000036, 0x00102012, 0x00000002, 0x0010100a, 0x00000002, 0x05000036, 0x00102022, 0x00000002,
        0x0010100a, 0x00000003, 0x05000036, 0x00102042, 0x00000002, 0x0010100a, 0x00000004, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs = {vs_code, sizeof(vs_code)};
    static const DWORD ps_code[] =
    {
#if 0
        void main(float4 position : SV_Position, float2 t0 : TEXCOORD0,
                nointerpolation float t1 : TEXCOORD1, uint t2 : TEXCOORD2,
                uint t3 : TEXCOORD3, float t4 : TEXCOORD4, out float4 o : SV_Target)
        {
            o.x = t0.y + t1;
            o.y = t2 + t3;
            o.z = t4;
            o.w = t0.x;
        }
#endif
        0x43425844, 0x21076b15, 0x493d36f1, 0x0cd125d6, 0x1e92c724, 0x00000001, 0x000001e0, 0x00000003,
        0x0000002c, 0x000000e4, 0x00000118, 0x4e475349, 0x000000b0, 0x00000006, 0x00000008, 0x00000098,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x000000a4, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000303, 0x000000a4, 0x00000004, 0x00000000, 0x00000003, 0x00000001,
        0x00000404, 0x000000a4, 0x00000001, 0x00000000, 0x00000003, 0x00000002, 0x00000101, 0x000000a4,
        0x00000002, 0x00000000, 0x00000001, 0x00000002, 0x00000202, 0x000000a4, 0x00000003, 0x00000000,
        0x00000001, 0x00000002, 0x00000404, 0x505f5653, 0x7469736f, 0x006e6f69, 0x43584554, 0x44524f4f,
        0xababab00, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000,
        0x00000003, 0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x000000c0,
        0x00000050, 0x00000030, 0x0100086a, 0x03001062, 0x00101032, 0x00000001, 0x03001062, 0x00101042,
        0x00000001, 0x03000862, 0x00101012, 0x00000002, 0x03000862, 0x00101022, 0x00000002, 0x03000862,
        0x00101042, 0x00000002, 0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000001, 0x0700001e,
        0x00100012, 0x00000000, 0x0010101a, 0x00000002, 0x0010102a, 0x00000002, 0x05000056, 0x00102022,
        0x00000000, 0x0010000a, 0x00000000, 0x07000000, 0x00102012, 0x00000000, 0x0010101a, 0x00000001,
        0x0010100a, 0x00000002, 0x05000036, 0x001020c2, 0x00000000, 0x001012a6, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
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
    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, desc.rt_format, &vs, &ps, &input_layout);

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

    static const DWORD vs1_code[] =
    {
#if 0
        void main(float4 in_position : POSITION, uint4 in_uint : UINT,
                out float4 out_position : SV_POSITION, out uint out_uint : UINT,
                out float3 out_float : FLOAT)
        {
            out_position = in_position;
            out_uint = in_uint.y;
            out_float = float3(1, 2, 3);
        }
#endif
        0x43425844, 0x0521bc60, 0xd39733a4, 0x1522eea3, 0x0c741ea3, 0x00000001, 0x0000018c, 0x00000003,
        0x0000002c, 0x0000007c, 0x000000ec, 0x4e475349, 0x00000048, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00000041, 0x00000000, 0x00000000,
        0x00000001, 0x00000001, 0x0000020f, 0x49534f50, 0x4e4f4954, 0x4e495500, 0xabab0054, 0x4e47534f,
        0x00000068, 0x00000003, 0x00000008, 0x00000050, 0x00000000, 0x00000001, 0x00000003, 0x00000000,
        0x0000000f, 0x0000005c, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000e01, 0x00000061,
        0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x00000807, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x544e4955, 0x4f4c4600, 0xab005441, 0x58454853, 0x00000098, 0x00010050, 0x00000026, 0x0100086a,
        0x0300005f, 0x001010f2, 0x00000000, 0x0300005f, 0x00101022, 0x00000001, 0x04000067, 0x001020f2,
        0x00000000, 0x00000001, 0x03000065, 0x00102012, 0x00000001, 0x03000065, 0x00102072, 0x00000002,
        0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x05000036, 0x00102012, 0x00000001,
        0x0010101a, 0x00000001, 0x08000036, 0x00102072, 0x00000002, 0x00004002, 0x3f800000, 0x40000000,
        0x40400000, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs1 = {vs1_code, sizeof(vs1_code)};
    static const DWORD ps1_code[] =
    {
#if 0
        void main(float4 position : SV_POSITION, uint in_uint : UINT,
                float3 in_float : FLOAT, out float4 out_float : SV_TARGET0,
                out uint4 out_uint : SV_TARGET1)
        {
            out_float.x = position.w;
            out_float.y = in_uint;
            out_float.z = in_float.z;
            out_float.w = 0;
            out_uint.x = 0xdeadbeef;
            out_uint.y = 0;
            out_uint.z = in_uint;
            out_uint.w = in_float.z;
        }
#endif
        0x43425844, 0x762dbf5e, 0x2cc83972, 0x60c7aa48, 0xbca6118a, 0x00000001, 0x000001d4, 0x00000003,
        0x0000002c, 0x0000009c, 0x000000e8, 0x4e475349, 0x00000068, 0x00000003, 0x00000008, 0x00000050,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000080f, 0x0000005c, 0x00000000, 0x00000000,
        0x00000001, 0x00000001, 0x00000101, 0x00000061, 0x00000000, 0x00000000, 0x00000003, 0x00000002,
        0x00000407, 0x505f5653, 0x5449534f, 0x004e4f49, 0x544e4955, 0x4f4c4600, 0xab005441, 0x4e47534f,
        0x00000044, 0x00000002, 0x00000008, 0x00000038, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x00000038, 0x00000001, 0x00000000, 0x00000001, 0x00000001, 0x0000000f, 0x545f5653,
        0x45475241, 0xabab0054, 0x52444853, 0x000000e4, 0x00000040, 0x00000039, 0x04002064, 0x00101082,
        0x00000000, 0x00000001, 0x03000862, 0x00101012, 0x00000001, 0x03001062, 0x00101042, 0x00000002,
        0x03000065, 0x001020f2, 0x00000000, 0x03000065, 0x001020f2, 0x00000001, 0x05000056, 0x00102022,
        0x00000000, 0x0010100a, 0x00000001, 0x05000036, 0x00102012, 0x00000000, 0x0010103a, 0x00000000,
        0x05000036, 0x00102042, 0x00000000, 0x0010102a, 0x00000002, 0x05000036, 0x00102082, 0x00000000,
        0x00004001, 0x00000000, 0x0500001c, 0x00102082, 0x00000001, 0x0010102a, 0x00000002, 0x08000036,
        0x00102032, 0x00000001, 0x00004002, 0xdeadbeef, 0x00000000, 0x00000000, 0x00000000, 0x05000036,
        0x00102042, 0x00000001, 0x0010100a, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps1 = {ps1_code, sizeof(ps1_code)};
    static const DWORD vs2_code[] =
    {
#if 0
        void main(float4 in_position : POSITION,
                float4 in_texcoord0 : TEXCOORD0, float4 in_texcoord1 : TEXCOORD1,
                float4 in_texcoord2 : TEXCOORD2,
                out float4 position : Sv_Position,
                out float2 texcoord0 : TEXCOORD0, out float2 texcoord1 : TEXCOORD1,
                out float4 texcoord2 : TEXCOORD2, out float3 texcoord3 : TEXCOORD3)
        {
            position = in_position;
            texcoord0 = in_texcoord0.yx;
            texcoord1 = in_texcoord0.wz;
            texcoord2 = in_texcoord1;
            texcoord3 = in_texcoord2.yzx;
        }
#endif
        0x43425844, 0x6721613b, 0xb997c7e4, 0x8bc3df4d, 0x813c93b9, 0x00000001, 0x00000224, 0x00000003,
        0x0000002c, 0x000000b0, 0x00000150, 0x4e475349, 0x0000007c, 0x00000004, 0x00000008, 0x00000068,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00000071, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x00000071, 0x00000001, 0x00000000, 0x00000003, 0x00000002,
        0x00000f0f, 0x00000071, 0x00000002, 0x00000000, 0x00000003, 0x00000003, 0x0000070f, 0x49534f50,
        0x4e4f4954, 0x58455400, 0x524f4f43, 0xabab0044, 0x4e47534f, 0x00000098, 0x00000005, 0x00000008,
        0x00000080, 0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x0000008c, 0x00000000,
        0x00000000, 0x00000003, 0x00000001, 0x00000c03, 0x0000008c, 0x00000001, 0x00000000, 0x00000003,
        0x00000001, 0x0000030c, 0x0000008c, 0x00000002, 0x00000000, 0x00000003, 0x00000002, 0x0000000f,
        0x0000008c, 0x00000003, 0x00000000, 0x00000003, 0x00000003, 0x00000807, 0x505f7653, 0x7469736f,
        0x006e6f69, 0x43584554, 0x44524f4f, 0xababab00, 0x52444853, 0x000000cc, 0x00010040, 0x00000033,
        0x0300005f, 0x001010f2, 0x00000000, 0x0300005f, 0x001010f2, 0x00000001, 0x0300005f, 0x001010f2,
        0x00000002, 0x0300005f, 0x00101072, 0x00000003, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x03000065, 0x00102032, 0x00000001, 0x03000065, 0x001020c2, 0x00000001, 0x03000065, 0x001020f2,
        0x00000002, 0x03000065, 0x00102072, 0x00000003, 0x05000036, 0x001020f2, 0x00000000, 0x00101e46,
        0x00000000, 0x05000036, 0x001020f2, 0x00000001, 0x00101b16, 0x00000001, 0x05000036, 0x001020f2,
        0x00000002, 0x00101e46, 0x00000002, 0x05000036, 0x00102072, 0x00000003, 0x00101496, 0x00000003,
        0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs2 = {vs2_code, sizeof(vs2_code)};
    static const DWORD ps2_code[] =
    {
#if 0
        void main(float4 position : Sv_Position,
                float2 texcoord0 : TEXCOORD0, float2 texcoord1 : TEXCOORD1,
                float4 texcoord2 : TEXCOORD2, float3 texcoord3 : TEXCOORD3,
                out float4 target0 : Sv_Target0, out uint4 target1 : SV_Target1)
        {
            target0.x = texcoord0.x + texcoord0.y;
            target0.y = texcoord1.x;
            target0.z = texcoord3.z;
            target0.w = texcoord1.y;

            target1.x = texcoord2.x;
            target1.y = texcoord2.y;
            target1.w = texcoord2.w;
            target1.z = 0;
        }
#endif
        0x43425844, 0xa6c0df60, 0x5bf34683, 0xa0093595, 0x98cca724, 0x00000001, 0x000001e8, 0x00000003,
        0x0000002c, 0x000000cc, 0x00000120, 0x4e475349, 0x00000098, 0x00000005, 0x00000008, 0x00000080,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x0000008c, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000303, 0x0000008c, 0x00000001, 0x00000000, 0x00000003, 0x00000001,
        0x00000c0c, 0x0000008c, 0x00000002, 0x00000000, 0x00000003, 0x00000002, 0x00000b0f, 0x0000008c,
        0x00000003, 0x00000000, 0x00000003, 0x00000003, 0x00000407, 0x505f7653, 0x7469736f, 0x006e6f69,
        0x43584554, 0x44524f4f, 0xababab00, 0x4e47534f, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x0000000f, 0x00000042, 0x00000001, 0x00000000,
        0x00000001, 0x00000001, 0x0000000f, 0x545f7653, 0x65677261, 0x56530074, 0x7261545f, 0x00746567,
        0x52444853, 0x000000c0, 0x00000040, 0x00000030, 0x03001062, 0x00101032, 0x00000001, 0x03001062,
        0x001010c2, 0x00000001, 0x03001062, 0x001010b2, 0x00000002, 0x03001062, 0x00101042, 0x00000003,
        0x03000065, 0x001020f2, 0x00000000, 0x03000065, 0x001020f2, 0x00000001, 0x07000000, 0x00102012,
        0x00000000, 0x0010101a, 0x00000001, 0x0010100a, 0x00000001, 0x05000036, 0x001020a2, 0x00000000,
        0x00101ea6, 0x00000001, 0x05000036, 0x00102042, 0x00000000, 0x0010102a, 0x00000003, 0x0500001c,
        0x001020b2, 0x00000001, 0x00101c46, 0x00000002, 0x05000036, 0x00102042, 0x00000001, 0x00004001,
        0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps2 = {ps2_code, sizeof(ps2_code)};
    static const DWORD ps3_code[] =
    {
#if 0
        void main(float4 position : Sv_Position,
                float2 texcoord0 : TEXCOORD0, float2 texcoord1 : TEXCOORD1,
                float4 texcoord2 : TEXCOORD2, float3 texcoord3 : TEXCOORD3,
                out float4 target0 : Sv_Target0, out uint4 target1 : SV_Target1)
        {
            target0.x = texcoord0.x;
            target0.y = texcoord1.y;
            target0.z = texcoord3.z;
            target0.w = texcoord3.z;

            target1.x = texcoord2.x;
            target1.y = 0;
            target1.w = texcoord2.w;
            target1.z = 0;
        }
#endif
        0x43425844, 0x2df3a11d, 0x885fc859, 0x332d922b, 0xf8e01020, 0x00000001, 0x000001d8, 0x00000003,
        0x0000002c, 0x000000cc, 0x00000120, 0x4e475349, 0x00000098, 0x00000005, 0x00000008, 0x00000080,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x0000008c, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000103, 0x0000008c, 0x00000001, 0x00000000, 0x00000003, 0x00000001,
        0x0000080c, 0x0000008c, 0x00000002, 0x00000000, 0x00000003, 0x00000002, 0x0000090f, 0x0000008c,
        0x00000003, 0x00000000, 0x00000003, 0x00000003, 0x00000407, 0x505f7653, 0x7469736f, 0x006e6f69,
        0x43584554, 0x44524f4f, 0xababab00, 0x4e47534f, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x0000000f, 0x00000042, 0x00000001, 0x00000000,
        0x00000001, 0x00000001, 0x0000000f, 0x545f7653, 0x65677261, 0x56530074, 0x7261545f, 0x00746567,
        0x52444853, 0x000000b0, 0x00000040, 0x0000002c, 0x03001062, 0x00101012, 0x00000001, 0x03001062,
        0x00101082, 0x00000001, 0x03001062, 0x00101092, 0x00000002, 0x03001062, 0x00101042, 0x00000003,
        0x03000065, 0x001020f2, 0x00000000, 0x03000065, 0x001020f2, 0x00000001, 0x05000036, 0x00102032,
        0x00000000, 0x001010c6, 0x00000001, 0x05000036, 0x001020c2, 0x00000000, 0x00101aa6, 0x00000003,
        0x0500001c, 0x00102092, 0x00000001, 0x00101c06, 0x00000002, 0x08000036, 0x00102062, 0x00000001,
        0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps3 = {ps3_code, sizeof(ps3_code)};
    /* position.xyw */
    static const DWORD ps4_code[] =
    {
#if 0
        void main(float4 position : Sv_Position,
                float2 texcoord0 : TEXCOORD0, float2 texcoord1 : TEXCOORD1,
                float4 texcoord2 : TEXCOORD2, float3 texcoord3 : TEXCOORD3,
                out float4 target0 : Sv_Target0, out uint4 target1 : SV_Target1)
        {
            if (all(position.xy < float2(64, 64)))
                target0 = float4(0, 1, 0, 1);
            else
                target0 = float4(0, 0, 0, 0);

            target1.xyzw = 0;
            target1.y = position.w;
        }
#endif
        0x43425844, 0x6dac90a1, 0x518a6b0a, 0x393cc320, 0x5f6fbe5e, 0x00000001, 0x00000204, 0x00000003,
        0x0000002c, 0x000000cc, 0x00000120, 0x4e475349, 0x00000098, 0x00000005, 0x00000008, 0x00000080,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x00000b0f, 0x0000008c, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000003, 0x0000008c, 0x00000001, 0x00000000, 0x00000003, 0x00000001,
        0x0000000c, 0x0000008c, 0x00000002, 0x00000000, 0x00000003, 0x00000002, 0x0000000f, 0x0000008c,
        0x00000003, 0x00000000, 0x00000003, 0x00000003, 0x00000007, 0x505f7653, 0x7469736f, 0x006e6f69,
        0x43584554, 0x44524f4f, 0xababab00, 0x4e47534f, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x0000000f, 0x00000042, 0x00000001, 0x00000000,
        0x00000001, 0x00000001, 0x0000000f, 0x545f7653, 0x65677261, 0x56530074, 0x7261545f, 0x00746567,
        0x52444853, 0x000000dc, 0x00000040, 0x00000037, 0x04002064, 0x001010b2, 0x00000000, 0x00000001,
        0x03000065, 0x001020f2, 0x00000000, 0x03000065, 0x001020f2, 0x00000001, 0x02000068, 0x00000001,
        0x0a000031, 0x00100032, 0x00000000, 0x00101046, 0x00000000, 0x00004002, 0x42800000, 0x42800000,
        0x00000000, 0x00000000, 0x07000001, 0x00100012, 0x00000000, 0x0010001a, 0x00000000, 0x0010000a,
        0x00000000, 0x0a000001, 0x001020f2, 0x00000000, 0x00100006, 0x00000000, 0x00004002, 0x00000000,
        0x3f800000, 0x00000000, 0x3f800000, 0x0500001c, 0x00102022, 0x00000001, 0x0010103a, 0x00000000,
        0x08000036, 0x001020d2, 0x00000001, 0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps4 = {ps4_code, sizeof(ps4_code)};
#if 0
    struct ps_data
    {
        float4 position : SV_Position;
        float4 color : COLOR;
        float3 color1 : COLOR1;
        float color2 : COLOR2;
    };

    ps_data vs_main(float4 position : POSITION)
    {
        ps_data o;
        o.position = position;
        o.color = float4(0, 1, 0, 1);
        o.color1 = (float3)0.5;
        o.color2 = 0.25;
        return o;
    }

    float4 ps_main(ps_data i) : SV_Target
    {
        return float4(i.color.rgb, i.color2);
    }
#endif
    static const DWORD vs5_code[] =
    {
        0x43425844, 0xc3e1b9fc, 0xb99e43ef, 0x9a2a6dfc, 0xad719e68, 0x00000001, 0x00000190, 0x00000003,
        0x0000002c, 0x00000060, 0x000000e4, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000007c, 0x00000004, 0x00000008, 0x00000068, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x00000074, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x0000000f,
        0x00000074, 0x00000001, 0x00000000, 0x00000003, 0x00000002, 0x00000807, 0x00000074, 0x00000002,
        0x00000000, 0x00000003, 0x00000002, 0x00000708, 0x505f5653, 0x7469736f, 0x006e6f69, 0x4f4c4f43,
        0xabab0052, 0x58454853, 0x000000a4, 0x00010050, 0x00000029, 0x0100086a, 0x0300005f, 0x001010f2,
        0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000001,
        0x03000065, 0x00102072, 0x00000002, 0x03000065, 0x00102082, 0x00000002, 0x05000036, 0x001020f2,
        0x00000000, 0x00101e46, 0x00000000, 0x08000036, 0x001020f2, 0x00000001, 0x00004002, 0x00000000,
        0x3f800000, 0x00000000, 0x3f800000, 0x08000036, 0x001020f2, 0x00000002, 0x00004002, 0x3f000000,
        0x3f000000, 0x3f000000, 0x3e800000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs5 = {vs5_code, sizeof(vs5_code)};
    static const DWORD ps5_code[] =
    {
        0x43425844, 0x285bf397, 0xbc07e078, 0xc4e528e3, 0x74efea4d, 0x00000001, 0x00000148, 0x00000003,
        0x0000002c, 0x000000b0, 0x000000e4, 0x4e475349, 0x0000007c, 0x00000004, 0x00000008, 0x00000068,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x00000074, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x0000070f, 0x00000074, 0x00000001, 0x00000000, 0x00000003, 0x00000002,
        0x00000007, 0x00000074, 0x00000002, 0x00000000, 0x00000003, 0x00000002, 0x00000808, 0x505f5653,
        0x7469736f, 0x006e6f69, 0x4f4c4f43, 0xabab0052, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008,
        0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x0000000f, 0x545f5653, 0x65677261,
        0xabab0074, 0x58454853, 0x0000005c, 0x00000050, 0x00000017, 0x0100086a, 0x03001062, 0x00101072,
        0x00000001, 0x03001062, 0x00101082, 0x00000002, 0x03000065, 0x001020f2, 0x00000000, 0x05000036,
        0x00102072, 0x00000000, 0x00101246, 0x00000001, 0x05000036, 0x00102082, 0x00000000, 0x0010103a,
        0x00000002, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps5 = {ps5_code, sizeof(ps5_code)};
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
        {&vs1, &ps1, {1.0f, 2.0f, 3.0f, 0.00f}, {0xdeadbeef, 0, 2, 3}},
        {&vs2, &ps2, {6.0f, 4.0f, 7.0f, 8.00f}, {         9, 5, 0, 1}},
        {&vs2, &ps3, {3.0f, 8.0f, 7.0f, 7.00f}, {         9, 0, 0, 1}},
        {&vs2, &ps4, {0.0f, 1.0f, 0.0f, 1.00f}, {         0, 1, 0, 0}},
        {&vs5, &ps5, {0.0f, 1.0f, 0.0f, 0.25f}, {         0, 1, 0, 0}},
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
    static const DWORD vs_code[] =
    {
#if 0
        struct vs_in
        {
            float4 position : POSITION;
            float2 color_xy : COLOR0;
            float2 color_zw : COLOR1;
            unsigned int instance_id : SV_INSTANCEID;
        };

        struct vs_out
        {
            float4 position : SV_POSITION;
            float2 color_xy : COLOR0;
            float2 color_zw : COLOR1;
        };

        struct vs_out main(struct vs_in i)
        {
            struct vs_out o;

            o.position = i.position;
            o.position.x += i.instance_id * 0.5;
            o.color_xy = i.color_xy;
            o.color_zw = i.color_zw;

            return o;
        }
#endif
        0x43425844, 0x52e3bf46, 0x6300403d, 0x624cffe4, 0xa4fc0013, 0x00000001, 0x00000214, 0x00000003,
        0x0000002c, 0x000000bc, 0x00000128, 0x4e475349, 0x00000088, 0x00000004, 0x00000008, 0x00000068,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00000071, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000303, 0x00000071, 0x00000001, 0x00000000, 0x00000003, 0x00000002,
        0x00000303, 0x00000077, 0x00000000, 0x00000008, 0x00000001, 0x00000003, 0x00000101, 0x49534f50,
        0x4e4f4954, 0x4c4f4300, 0x5300524f, 0x4e495f56, 0x4e415453, 0x44494543, 0xababab00, 0x4e47534f,
        0x00000064, 0x00000003, 0x00000008, 0x00000050, 0x00000000, 0x00000001, 0x00000003, 0x00000000,
        0x0000000f, 0x0000005c, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x00000c03, 0x0000005c,
        0x00000001, 0x00000000, 0x00000003, 0x00000001, 0x0000030c, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4f4c4f43, 0xabab0052, 0x52444853, 0x000000e4, 0x00010040, 0x00000039, 0x0300005f, 0x001010f2,
        0x00000000, 0x0300005f, 0x00101032, 0x00000001, 0x0300005f, 0x00101032, 0x00000002, 0x04000060,
        0x00101012, 0x00000003, 0x00000008, 0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x03000065,
        0x00102032, 0x00000001, 0x03000065, 0x001020c2, 0x00000001, 0x02000068, 0x00000001, 0x05000056,
        0x00100012, 0x00000000, 0x0010100a, 0x00000003, 0x09000032, 0x00102012, 0x00000000, 0x0010000a,
        0x00000000, 0x00004001, 0x3f000000, 0x0010100a, 0x00000000, 0x05000036, 0x001020e2, 0x00000000,
        0x00101e56, 0x00000000, 0x05000036, 0x00102032, 0x00000001, 0x00101046, 0x00000001, 0x05000036,
        0x001020c2, 0x00000001, 0x00101406, 0x00000002, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs = {vs_code, sizeof(vs_code)};
    static const DWORD ps_code[] =
    {
#if 0
        struct vs_out
        {
            float4 position : SV_POSITION;
            float2 color_xy : COLOR0;
            float2 color_zw : COLOR1;
        };

        float4 main(struct vs_out i) : SV_TARGET
        {
            return float4(i.color_xy.xy, i.color_zw.xy);
        }
#endif
        0x43425844, 0x64e48a09, 0xaa484d46, 0xe40a6e78, 0x9885edf3, 0x00000001, 0x00000118, 0x00000003,
        0x0000002c, 0x00000098, 0x000000cc, 0x4e475349, 0x00000064, 0x00000003, 0x00000008, 0x00000050,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x0000005c, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000303, 0x0000005c, 0x00000001, 0x00000000, 0x00000003, 0x00000001,
        0x00000c0c, 0x505f5653, 0x5449534f, 0x004e4f49, 0x4f4c4f43, 0xabab0052, 0x4e47534f, 0x0000002c,
        0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x0000000f,
        0x545f5653, 0x45475241, 0xabab0054, 0x52444853, 0x00000044, 0x00000040, 0x00000011, 0x03001062,
        0x00101032, 0x00000001, 0x03001062, 0x001010c2, 0x00000001, 0x03000065, 0x001020f2, 0x00000000,
        0x05000036, 0x001020f2, 0x00000000, 0x00101e46, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
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
    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, &vs, &ps, &input_layout);

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
#if 0
    float4 c0;
    float4 c1;

    void main(out float4 o0 : SV_Target0, out float4 o1 : SV_Target1)
    {
        o0 = c0;
        o1 = c1;
    }
#endif
    static const DWORD ps_code_dxbc[] =
    {
        0x43425844, 0x823a4ecf, 0xd409abf6, 0xe76697ab, 0x9b53c9a5, 0x00000001, 0x000000f8, 0x00000003,
        0x0000002c, 0x0000003c, 0x00000088, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x00000044, 0x00000002, 0x00000008, 0x00000038, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x00000038, 0x00000001, 0x00000000, 0x00000003, 0x00000001, 0x0000000f, 0x545f5653,
        0x65677261, 0xabab0074, 0x58454853, 0x00000068, 0x00000050, 0x0000001a, 0x0100086a, 0x04000059,
        0x00208e46, 0x00000000, 0x00000002, 0x03000065, 0x001020f2, 0x00000000, 0x03000065, 0x001020f2,
        0x00000001, 0x06000036, 0x001020f2, 0x00000000, 0x00208e46, 0x00000000, 0x00000000, 0x06000036,
        0x001020f2, 0x00000001, 0x00208e46, 0x00000000, 0x00000001, 0x0100003e,
    };
    static const BYTE ps_code_dxil[] =
    {
        0x44, 0x58, 0x42, 0x43, 0xe9, 0x7d, 0xe2, 0x80, 0x9c, 0x32, 0xb1, 0xce, 0x8a, 0xdf, 0x9d, 0xe5, 0xda, 0xda, 0x37, 0x87, 0x01, 0x00, 0x00, 0x00, 0x5e, 0x07, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x34, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00, 0xae, 0x00, 0x00, 0x00, 0x2e, 0x01, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x53, 0x56, 0x5f, 0x54, 0x61, 0x72, 0x67, 0x65, 0x74, 0x00, 0x50, 0x53, 0x56, 0x30, 0x78, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x44, 0x10, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x44, 0x10, 0x03, 0x00, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x28, 0x06, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x8a, 0x01, 0x00, 0x00, 0x44, 0x58,
        0x49, 0x4c, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x06, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x81, 0x01, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91, 0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x14,
        0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0xa4, 0x10, 0x32, 0x14, 0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x52, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48,
        0x0e, 0x90, 0x91, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x29, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x1b, 0x88, 0xe0, 0xff, 0xff, 0xff,
        0xff, 0x07, 0x40, 0xda, 0x60, 0x08, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x12, 0x50, 0x01, 0x00, 0x00, 0x00, 0x49, 0x18, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42, 0x20, 0x00,
        0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x32, 0x22, 0x48, 0x09, 0x20, 0x64, 0x85, 0x04, 0x93, 0x22, 0xa4, 0x84, 0x04, 0x93, 0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c,
        0x8a, 0x8c, 0x0b, 0x84, 0xa4, 0x4c, 0x10, 0x54, 0x33, 0x00, 0xc3, 0x08, 0x04, 0x30, 0x13, 0x42, 0x06, 0xa7, 0xc0, 0x0e, 0xef, 0x20, 0x0e, 0xe1, 0xc0, 0x0e, 0xf3, 0x80, 0x84, 0x10, 0x48, 0x8c,
        0x00, 0x94, 0x80, 0x50, 0x99, 0x23, 0x00, 0x83, 0x39, 0x02, 0xa4, 0x18, 0xe4, 0x9c, 0x83, 0x00, 0xa5, 0xa3, 0x86, 0xcb, 0x9f, 0xb0, 0x87, 0x90, 0x7c, 0x6e, 0xa3, 0x8a, 0x95, 0x98, 0xfc, 0xe2,
        0xb6, 0x11, 0x01, 0x00, 0x00, 0x84, 0xee, 0x19, 0x2e, 0x7f, 0xc2, 0x1e, 0x42, 0xf2, 0x43, 0xa0, 0x19, 0x16, 0x02, 0x05, 0xac, 0x10, 0xeb, 0xb4, 0x43, 0x6e, 0x8e, 0x20, 0x28, 0x46, 0x3b, 0xe8,
        0x1c, 0x48, 0x71, 0x20, 0x20, 0x05, 0x0e, 0x00, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d,
        0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07,
        0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60,
        0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76,
        0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x3a, 0x0f, 0x44, 0x90, 0x21, 0x23, 0x25, 0x40, 0x00, 0x3a, 0x00, 0x60,
        0xc8, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x90, 0x27, 0x01, 0x02, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x21, 0x8f, 0x03, 0x04, 0x80, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x43, 0x9e, 0x08, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb2, 0x40, 0x0d, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x14, 0x19, 0x11,
        0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x42, 0x85, 0x30, 0x02, 0x40, 0xa7, 0x04, 0x46, 0x00, 0x0a, 0xa1, 0x18, 0x0a, 0xa8, 0x0c, 0xca, 0xa1, 0x24, 0x0a, 0x84, 0x50, 0x49, 0x14,
        0x08, 0x8d, 0x19, 0x00, 0x12, 0x33, 0x00, 0x44, 0xc7, 0x12, 0x00, 0x04, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x5d, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0xc4, 0x88, 0x0c,
        0x6f, 0xec, 0xed, 0x4d, 0x0c, 0x44, 0x06, 0x26, 0x26, 0xc7, 0x05, 0xa6, 0xc6, 0x05, 0x06, 0x66, 0x43, 0x10, 0x4c, 0x10, 0x8e, 0x63, 0x82, 0x70, 0x20, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x1c, 0xc9,
        0x06, 0x61, 0x30, 0x28, 0xc0, 0xcd, 0x4d, 0x10, 0x0e, 0x65, 0xc3, 0x80, 0x24, 0xc4, 0x04, 0x61, 0x90, 0x88, 0x90, 0x1c, 0xb1, 0xbd, 0x89, 0x85, 0xb1, 0xcd, 0x4d, 0x10, 0x8e, 0x65, 0x03, 0x42,
        0x2c, 0x0c, 0x41, 0x0c, 0x0d, 0xb0, 0x21, 0x70, 0x36, 0x10, 0x00, 0xf0, 0x00, 0x13, 0x04, 0x61, 0xa2, 0x30, 0x06, 0x33, 0x41, 0x38, 0x98, 0x09, 0xc2, 0xd1, 0x4c, 0x10, 0x0e, 0x67, 0x83, 0x91,
        0x48, 0x13, 0x41, 0x55, 0x14, 0xc6, 0x62, 0x26, 0x08, 0xc7, 0xb3, 0xc1, 0x48, 0xae, 0x09, 0xa3, 0xaa, 0x0d, 0x43, 0x63, 0x65, 0x1b, 0x06, 0x22, 0xd2, 0x26, 0x08, 0x46, 0xb0, 0x01, 0xd8, 0x30,
        0x10, 0x5d, 0xb7, 0x21, 0xf0, 0x36, 0x0c, 0x03, 0xf7, 0x4d, 0x10, 0x28, 0x6a, 0x43, 0x10, 0x06, 0x24, 0xda, 0xc2, 0xd2, 0xdc, 0x98, 0x4c, 0x59, 0x7d, 0x51, 0x85, 0xc9, 0x9d, 0x95, 0xd1, 0x4d,
        0x10, 0x10, 0x68, 0x82, 0x80, 0x44, 0x1b, 0x02, 0x62, 0x82, 0x80, 0x18, 0x13, 0x04, 0xa4, 0xd8, 0xb0, 0x10, 0x64, 0x50, 0x06, 0x66, 0x70, 0x06, 0x68, 0x30, 0xa4, 0x01, 0x81, 0x06, 0xc0, 0x86,
        0x60, 0xd8, 0xb0, 0x0c, 0x64, 0x50, 0x06, 0x66, 0xb0, 0x06, 0x68, 0x30, 0xa4, 0xc1, 0x80, 0x06, 0xc0, 0x06, 0x41, 0x0d, 0xd8, 0x60, 0xc3, 0x00, 0xb4, 0x01, 0xb0, 0xa1, 0xe0, 0xc6, 0xc0, 0x0d,
        0x20, 0xa0, 0x0a, 0x1b, 0x9b, 0x5d, 0x9b, 0x4b, 0x1a, 0x59, 0x99, 0x1b, 0xdd, 0x94, 0x20, 0xa8, 0x42, 0x86, 0xe7, 0x62, 0x57, 0x26, 0x37, 0x97, 0xf6, 0xe6, 0x36, 0x25, 0x20, 0x9a, 0x90, 0xe1,
        0xb9, 0xd8, 0x85, 0xb1, 0xd9, 0x95, 0xc9, 0x4d, 0x09, 0x8c, 0x3a, 0x64, 0x78, 0x2e, 0x73, 0x68, 0x61, 0x64, 0x65, 0x72, 0x4d, 0x6f, 0x64, 0x65, 0x6c, 0x53, 0x82, 0xa4, 0x0c, 0x19, 0x9e, 0x8b,
        0x5c, 0xd9, 0xdc, 0x5b, 0x9d, 0xdc, 0x58, 0xd9, 0xdc, 0x94, 0xe0, 0xa9, 0x44, 0x86, 0xe7, 0x42, 0x97, 0x07, 0x57, 0x16, 0xe4, 0xe6, 0xf6, 0x46, 0x17, 0x46, 0x97, 0xf6, 0xe6, 0x36, 0x37, 0x45,
        0xd0, 0xbe, 0x3a, 0x64, 0x78, 0x2e, 0x76, 0x69, 0x65, 0x77, 0x49, 0x64, 0x53, 0x74, 0x61, 0x74, 0x65, 0x53, 0x82, 0x30, 0xa8, 0x43, 0x86, 0xe7, 0x52, 0xe6, 0x46, 0x27, 0x97, 0x07, 0xf5, 0x96,
        0xe6, 0x46, 0x37, 0x37, 0x25, 0x70, 0x03, 0x00, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88, 0x43, 0x38,
        0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c,
        0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48, 0x87, 0x70,
        0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10,
        0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0,
        0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78, 0x87, 0x77,
        0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4,
        0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x43,
        0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x8c, 0xcc, 0x21, 0x07, 0x7c, 0x70, 0x03, 0x74,
        0x60, 0x07, 0x37, 0x90, 0x87, 0x72, 0x98, 0x87, 0x77, 0xa8, 0x07, 0x79, 0x18, 0x87, 0x72, 0x70, 0x83, 0x70, 0xa0, 0x07, 0x7a, 0x90, 0x87, 0x74, 0x10, 0x87, 0x7a, 0xa0, 0x87, 0x72, 0x00, 0x00,
        0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x05, 0x80, 0x90, 0x8e, 0xec, 0xb7, 0x38, 0xcc, 0x9e, 0xdb, 0xc0, 0x36, 0x5c, 0xbe, 0xf3, 0xf8, 0x42, 0x40, 0x15, 0x05, 0x11, 0x95,
        0x0e, 0x30, 0x94, 0x84, 0x01, 0x08, 0x98, 0x5f, 0xdc, 0xb6, 0x11, 0x48, 0xc3, 0xe5, 0x3b, 0x8f, 0x2f, 0x44, 0x04, 0x30, 0x11, 0x21, 0xd0, 0x0c, 0x0b, 0x61, 0x02, 0xd5, 0x70, 0xf9, 0xce, 0xe3,
        0x4b, 0x93, 0x13, 0x11, 0x28, 0x35, 0x3d, 0xd4, 0xe4, 0x17, 0xb7, 0x6d, 0x01, 0x04, 0x03, 0x20, 0x0d, 0x00, 0x61, 0x20, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c, 0x10, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x74, 0x4a, 0xa1, 0xec, 0x4a, 0x8e, 0x50, 0x11, 0x94, 0x40, 0x19, 0x10, 0x1c, 0x01, 0x00, 0x00, 0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0x10, 0x61, 0x05, 0x41,
        0x51, 0xc1, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x38, 0xda, 0x11, 0x54, 0xa3, 0x09, 0x01, 0x30, 0x9a, 0x20, 0x04, 0xa3, 0x09, 0x83, 0x30, 0x9a, 0x40, 0x0c, 0x23, 0x06, 0x09, 0x00, 0x82, 0x60,
        0x90, 0x80, 0x41, 0xa3, 0x69, 0x1c, 0x31, 0x62, 0x90, 0x00, 0x20, 0x08, 0x06, 0x09, 0x18, 0x34, 0x9a, 0x96, 0x0c, 0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0x90, 0x80, 0x41, 0xa3, 0x69, 0x8a, 0x30,
        0x62, 0x90, 0x00, 0x20, 0x08, 0x06, 0x09, 0x18, 0x34, 0x9a, 0x86, 0x04, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xe0, 0x7c, 0x8c, 0xb1, 0x8d, 0x26, 0x04, 0xc0, 0x68, 0x82, 0x10, 0x8c, 0x26, 0x0c,
        0xc2, 0x68, 0x02, 0x31, 0x8c, 0x18, 0x24, 0x00, 0x08, 0x82, 0x41, 0x52, 0x06, 0x12, 0x18, 0x7c, 0x61, 0x40, 0x8c, 0x18, 0x24, 0x00, 0x08, 0x82, 0x41, 0x52, 0x06, 0x12, 0x18, 0x7c, 0xce, 0x30,
        0x62, 0x90, 0x00, 0x20, 0x08, 0x06, 0x49, 0x19, 0x48, 0x60, 0xf0, 0x3d, 0xc2, 0x88, 0x41, 0x02, 0x80, 0x20, 0x18, 0x24, 0x65, 0x20, 0x81, 0xc1, 0xd7, 0x04, 0x18, 0x0e, 0x04, 0x00, 0x05, 0x00,
        0x00, 0x00, 0xc5, 0x01, 0x91, 0x8e, 0xec, 0xb7, 0x38, 0xcc, 0x9e, 0x7f, 0xc7, 0xe2, 0xba, 0xd9, 0x5c, 0x96, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
#if 0
    float4 c0;
    float4 c1;

    void main(out float4 o0 : SV_Target0, out float4 o1 : SV_Target1, out float4 o2 : SV_Target2)
    {
        o0 = c0;
        o1 = c1;
        o2 = 1.0.xxxx;
    }
#endif
    static const BYTE ps_code_3rt_dxil[] =
    {
        0x44, 0x58, 0x42, 0x43, 0x71, 0x86, 0x12, 0x42, 0x18, 0x38, 0x41, 0x3c, 0x58, 0xfb, 0x1e, 0xe6, 0x52, 0xe5, 0x85, 0xd5, 0x01, 0x00, 0x00, 0x00,
        0x24, 0x0d, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x5c, 0x00, 0x00, 0x00, 0xd8, 0x00, 0x00, 0x00,
        0x80, 0x01, 0x00, 0x00, 0x10, 0x07, 0x00, 0x00, 0x2c, 0x07, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31,
        0x74, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x40, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x68, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x56, 0x5f, 0x54, 0x61, 0x72, 0x67, 0x65, 0x74, 0x00, 0x00, 0x00,
        0x50, 0x53, 0x56, 0x30, 0xa0, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x44, 0x10, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x44, 0x10, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x02, 0x44, 0x10, 0x03, 0x00, 0x00, 0x00,
        0x53, 0x54, 0x41, 0x54, 0x88, 0x05, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x62, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00,
        0x10, 0x00, 0x00, 0x00, 0x70, 0x05, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x59, 0x01, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91, 0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c,
        0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x14, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0xa4, 0x10, 0x32, 0x14, 0x38, 0x08, 0x18, 0x4b,
        0x0a, 0x32, 0x52, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x91, 0x22, 0xc4, 0x50,
        0x41, 0x51, 0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x29, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x1b, 0x88, 0xe0, 0xff,
        0xff, 0xff, 0xff, 0x07, 0x40, 0xda, 0x60, 0x08, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x12, 0x50, 0x01, 0x00, 0x00, 0x00, 0x49, 0x18, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42, 0x20, 0x00, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x32, 0x22, 0x48, 0x09,
        0x20, 0x64, 0x85, 0x04, 0x93, 0x22, 0xa4, 0x84, 0x04, 0x93, 0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x8a, 0x8c, 0x0b, 0x84, 0xa4, 0x4c,
        0x10, 0x54, 0x23, 0x00, 0x25, 0x00, 0x14, 0xe6, 0x08, 0xc0, 0x60, 0x8e, 0x00, 0x99, 0x01, 0x28, 0x06, 0x18, 0x63, 0x90, 0x42, 0xe6, 0xa8, 0xe1,
        0xf2, 0x27, 0xec, 0x21, 0x24, 0x9f, 0xdb, 0xa8, 0x62, 0x25, 0x26, 0xbf, 0xb8, 0x6d, 0x44, 0x94, 0x52, 0x0a, 0x91, 0x7b, 0x86, 0xcb, 0x9f, 0xb0,
        0x87, 0x90, 0xfc, 0x10, 0x68, 0x86, 0x85, 0x40, 0x41, 0x2a, 0x04, 0x1a, 0x6a, 0xd0, 0x9a, 0x23, 0x08, 0x8a, 0xa1, 0x06, 0x19, 0xa3, 0x91, 0x1b,
        0x08, 0x18, 0x46, 0x20, 0x8a, 0x99, 0x10, 0x32, 0x38, 0x05, 0x76, 0x78, 0x07, 0x71, 0x08, 0x07, 0x76, 0x98, 0x07, 0x24, 0xc4, 0x48, 0x32, 0x05,
        0x06, 0x00, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50,
        0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0,
        0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0,
        0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10,
        0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0,
        0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e,
        0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x06, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0c, 0x79, 0x16, 0x20, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xf2, 0x38, 0x40, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x90, 0x05, 0x02, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x14, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47,
        0xc6, 0x04, 0x43, 0x1a, 0x25, 0x30, 0x02, 0x50, 0x0c, 0x05, 0x54, 0x06, 0xe5, 0x50, 0x12, 0x05, 0x52, 0x1e, 0x45, 0x50, 0x30, 0x85, 0x41, 0xa4,
        0x24, 0x0a, 0x64, 0x04, 0xa0, 0x10, 0xa8, 0xd5, 0x00, 0xc9, 0x19, 0x00, 0x9a, 0x33, 0x00, 0x44, 0xc7, 0x12, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00,
        0x79, 0x18, 0x00, 0x00, 0x74, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0x44, 0x8f, 0x0c, 0x6f, 0xec, 0xed, 0x4d, 0x0c, 0x24,
        0xc6, 0xe5, 0xc6, 0x45, 0x46, 0x26, 0x46, 0xc6, 0x85, 0x06, 0x06, 0x04, 0xa5, 0x0c, 0x86, 0x66, 0xc6, 0x8c, 0x26, 0x2c, 0x46, 0x26, 0x65, 0x43,
        0x10, 0x4c, 0x10, 0x06, 0x62, 0x82, 0x30, 0x14, 0x1b, 0x84, 0x81, 0xd8, 0x20, 0x10, 0x04, 0x05, 0xb8, 0xb9, 0x09, 0xc2, 0x60, 0x6c, 0x18, 0x0e,
        0x84, 0x98, 0x20, 0x4c, 0x16, 0x11, 0x92, 0x23, 0xb6, 0x37, 0xb1, 0x30, 0xb6, 0xb9, 0x09, 0xc2, 0x70, 0x6c, 0x40, 0x08, 0x65, 0x21, 0x88, 0x81,
        0x01, 0x36, 0x04, 0xcd, 0x06, 0x02, 0x00, 0x1c, 0x60, 0x82, 0x20, 0x55, 0x14, 0xc6, 0x60, 0x26, 0x08, 0x03, 0x32, 0x41, 0x18, 0x92, 0x09, 0xc2,
        0xa0, 0x4c, 0x10, 0x1a, 0x6a, 0x03, 0x82, 0x44, 0x12, 0x31, 0x51, 0x54, 0x45, 0x61, 0x2c, 0x66, 0x82, 0x30, 0x2c, 0x1b, 0x10, 0xe4, 0x92, 0xb0,
        0x89, 0xa2, 0xaa, 0x0d, 0x03, 0x63, 0x65, 0x1b, 0x06, 0x02, 0xd2, 0x26, 0x08, 0x02, 0xb0, 0x01, 0xd8, 0x30, 0x10, 0x5d, 0xb7, 0x21, 0xf0, 0x36,
        0x0c, 0x03, 0xf7, 0x4d, 0x10, 0xa8, 0x6b, 0x43, 0x10, 0x06, 0x24, 0xda, 0xc2, 0xd2, 0xdc, 0x98, 0x4c, 0x59, 0x7d, 0x51, 0x85, 0xc9, 0x9d, 0x95,
        0xd1, 0x4d, 0x10, 0x08, 0x68, 0x82, 0x40, 0x44, 0x1b, 0x02, 0x62, 0x82, 0x40, 0x48, 0x13, 0x04, 0x62, 0x9a, 0x20, 0x0c, 0xcc, 0x06, 0x41, 0x52,
        0x83, 0x0d, 0x0b, 0x41, 0x06, 0x65, 0x60, 0x06, 0x67, 0x80, 0x06, 0x43, 0x1a, 0x10, 0x68, 0xb0, 0x06, 0x1b, 0x82, 0x61, 0xc3, 0x32, 0x90, 0x41,
        0x19, 0x98, 0x41, 0x1b, 0xa0, 0xc1, 0x90, 0x06, 0x03, 0x1a, 0xac, 0xc1, 0x04, 0x61, 0x68, 0x36, 0x04, 0x6f, 0xb0, 0x61, 0x79, 0x03, 0x32, 0x28,
        0x03, 0x33, 0x80, 0x03, 0x34, 0x18, 0xd2, 0xe0, 0x0d, 0xd0, 0x60, 0x0d, 0x36, 0x0c, 0x6c, 0xe0, 0x06, 0x71, 0xb0, 0x61, 0x00, 0xe4, 0x00, 0xd8,
        0x50, 0x70, 0x63, 0x30, 0x07, 0x0f, 0xc0, 0x22, 0xcd, 0x6d, 0x8e, 0x6e, 0x6e, 0x82, 0x30, 0x38, 0x34, 0xe6, 0xd2, 0xce, 0xbe, 0xe6, 0xe8, 0x26,
        0x08, 0xc3, 0xb3, 0x81, 0xa8, 0x03, 0x3b, 0xb8, 0x03, 0x3c, 0xa8, 0xc2, 0xc6, 0x66, 0xd7, 0xe6, 0x92, 0x46, 0x56, 0xe6, 0x46, 0x37, 0x25, 0x08,
        0xaa, 0x90, 0xe1, 0xb9, 0xd8, 0x95, 0xc9, 0xcd, 0xa5, 0xbd, 0xb9, 0x4d, 0x09, 0x88, 0x26, 0x64, 0x78, 0x2e, 0x76, 0x61, 0x6c, 0x76, 0x65, 0x72,
        0x53, 0x82, 0xa2, 0x0e, 0x19, 0x9e, 0xcb, 0x1c, 0x5a, 0x18, 0x59, 0x99, 0x5c, 0xd3, 0x1b, 0x59, 0x19, 0xdb, 0x94, 0x00, 0x29, 0x43, 0x86, 0xe7,
        0x22, 0x57, 0x36, 0xf7, 0x56, 0x27, 0x37, 0x56, 0x36, 0x37, 0x25, 0x70, 0x2a, 0x91, 0xe1, 0xb9, 0xd0, 0xe5, 0xc1, 0x95, 0x05, 0xb9, 0xb9, 0xbd,
        0xd1, 0x85, 0xd1, 0xa5, 0xbd, 0xb9, 0xcd, 0x4d, 0x11, 0xb4, 0xaf, 0x0e, 0x19, 0x9e, 0x8b, 0x5d, 0x5a, 0xd9, 0x5d, 0x12, 0xd9, 0x14, 0x5d, 0x18,
        0x5d, 0xd9, 0x94, 0x20, 0x0c, 0xea, 0x90, 0xe1, 0xb9, 0x94, 0xb9, 0xd1, 0xc9, 0xe5, 0x41, 0xbd, 0xa5, 0xb9, 0xd1, 0xcd, 0x4d, 0x09, 0xe6, 0xa0,
        0x0b, 0x19, 0x9e, 0xcb, 0xd8, 0x5b, 0x9d, 0x1b, 0x5d, 0x99, 0xdc, 0xdc, 0x94, 0x00, 0x0f, 0x00, 0x79, 0x18, 0x00, 0x00, 0x49, 0x00, 0x00, 0x00,
        0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73,
        0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30,
        0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b,
        0x08, 0x07, 0x79, 0x48, 0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e,
        0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61,
        0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76,
        0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76,
        0xf8, 0x05, 0x76, 0x78, 0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e,
        0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61,
        0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8,
        0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x8c, 0xc8,
        0x21, 0x07, 0x7c, 0x70, 0x03, 0x72, 0x10, 0x87, 0x73, 0x70, 0x03, 0x7b, 0x08, 0x07, 0x79, 0x60, 0x87, 0x70, 0xc8, 0x87, 0x77, 0xa8, 0x07, 0x7a,
        0x00, 0x00, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x26, 0xb0, 0x0d, 0x97, 0xef, 0x3c, 0xbe, 0x10, 0x50, 0x45, 0x41, 0x44,
        0xa5, 0x03, 0x0c, 0x25, 0x61, 0x00, 0x02, 0xe6, 0x17, 0xb7, 0x6d, 0x03, 0xd2, 0x70, 0xf9, 0xce, 0xe3, 0x0b, 0x11, 0x01, 0x4c, 0x44, 0x08, 0x34,
        0xc3, 0x42, 0x58, 0x40, 0x35, 0x5c, 0xbe, 0xf3, 0xf8, 0xd2, 0xe4, 0x44, 0x04, 0x4a, 0x4d, 0x0f, 0x35, 0xf9, 0xc5, 0x6d, 0x1b, 0x00, 0xc1, 0x00,
        0x48, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x41, 0x53, 0x48, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xcc, 0x54, 0x7e, 0xc9,
        0xba, 0xab, 0x79, 0x37, 0x5b, 0x08, 0xa0, 0x69, 0x4c, 0xcc, 0x08, 0x23, 0x44, 0x58, 0x49, 0x4c, 0xf0, 0x05, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00,
        0x7c, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0xd8, 0x05, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde,
        0x21, 0x0c, 0x00, 0x00, 0x73, 0x01, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91,
        0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x14, 0x45, 0x02,
        0x42, 0x92, 0x0b, 0x42, 0xa4, 0x10, 0x32, 0x14, 0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x52, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5,
        0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x91, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x29, 0x46, 0x06,
        0x51, 0x18, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x1b, 0x88, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0xda, 0x60, 0x08, 0xff, 0xff, 0xff, 0xff,
        0x3f, 0x00, 0x12, 0x50, 0x01, 0x00, 0x00, 0x00, 0x49, 0x18, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42, 0x20, 0x00, 0x00, 0x00,
        0x89, 0x20, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x32, 0x22, 0x48, 0x09, 0x20, 0x64, 0x85, 0x04, 0x93, 0x22, 0xa4, 0x84, 0x04, 0x93, 0x22, 0xe3,
        0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x8a, 0x8c, 0x0b, 0x84, 0xa4, 0x4c, 0x10, 0x54, 0x23, 0x00, 0x25, 0x00, 0x14, 0xe6, 0x08, 0xc0, 0x60, 0x8e,
        0x00, 0x99, 0x01, 0x28, 0x06, 0x18, 0x63, 0x90, 0x42, 0xe6, 0xa8, 0xe1, 0xf2, 0x27, 0xec, 0x21, 0x24, 0x9f, 0xdb, 0xa8, 0x62, 0x25, 0x26, 0xbf,
        0xb8, 0x6d, 0x44, 0x94, 0x52, 0x0a, 0x91, 0x7b, 0x86, 0xcb, 0x9f, 0xb0, 0x87, 0x90, 0xfc, 0x10, 0x68, 0x86, 0x85, 0x40, 0x41, 0x2a, 0x04, 0x1a,
        0x6a, 0xd0, 0x9a, 0x23, 0x08, 0x8a, 0xa1, 0x06, 0x19, 0xa3, 0x91, 0x1b, 0x08, 0x18, 0x46, 0x20, 0x8a, 0x99, 0x10, 0x32, 0x38, 0x05, 0x76, 0x78,
        0x07, 0x71, 0x08, 0x07, 0x76, 0x98, 0x07, 0x24, 0xc4, 0x48, 0x32, 0x05, 0x06, 0x00, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87,
        0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30,
        0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20,
        0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90,
        0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20,
        0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60,
        0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86,
        0x3c, 0x06, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x79, 0x16, 0x20, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x18, 0xf2, 0x38, 0x40, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x05, 0x02, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
        0x32, 0x1e, 0x98, 0x14, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x1a, 0x25, 0x30, 0x02, 0x50, 0x0e, 0xc5, 0x50, 0x40,
        0x65, 0x50, 0x1e, 0x45, 0x40, 0xa4, 0x24, 0x0a, 0x64, 0x04, 0xa0, 0x10, 0x68, 0xce, 0x00, 0x10, 0x1d, 0x4b, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00,
        0x79, 0x18, 0x00, 0x00, 0x4f, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0x44, 0x8f, 0x0c, 0x6f, 0xec, 0xed, 0x4d, 0x0c, 0x24,
        0xc6, 0xe5, 0xc6, 0x45, 0x46, 0x26, 0x46, 0xc6, 0x85, 0x06, 0x06, 0x04, 0xa5, 0x0c, 0x86, 0x66, 0xc6, 0x8c, 0x26, 0x2c, 0x46, 0x26, 0x65, 0x43,
        0x10, 0x4c, 0x10, 0x06, 0x62, 0x82, 0x30, 0x14, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x0c, 0xc6, 0x06, 0x61, 0x30, 0x28, 0xc0, 0xcd, 0x4d, 0x10, 0x86,
        0x63, 0xc3, 0x80, 0x24, 0xc4, 0x04, 0x61, 0x82, 0x08, 0x4c, 0x10, 0x06, 0x64, 0x03, 0x42, 0x2c, 0x0c, 0x41, 0x0c, 0x0d, 0xb0, 0x21, 0x70, 0x36,
        0x10, 0x00, 0xf0, 0x00, 0x13, 0x04, 0x2a, 0xda, 0x10, 0x44, 0x13, 0x04, 0x01, 0x20, 0xd1, 0x16, 0x96, 0xe6, 0xc6, 0x64, 0xca, 0xea, 0x8b, 0x2a,
        0x4c, 0xee, 0xac, 0x8c, 0x6e, 0x82, 0x40, 0x30, 0x13, 0x04, 0xa2, 0xd9, 0x10, 0x10, 0x13, 0x04, 0xc2, 0x99, 0x20, 0x10, 0xcf, 0x04, 0x61, 0x48,
        0x26, 0x08, 0x83, 0xb2, 0x41, 0xd8, 0xb8, 0x0d, 0x0b, 0x51, 0x59, 0x17, 0x96, 0x0d, 0x1a, 0x91, 0x75, 0x1b, 0x82, 0x61, 0xc3, 0x32, 0x54, 0xd6,
        0xf5, 0x65, 0x83, 0x36, 0x64, 0xdd, 0x04, 0x61, 0x58, 0x36, 0x04, 0x61, 0xb0, 0x61, 0x09, 0x83, 0xca, 0xba, 0xc4, 0x20, 0x1b, 0xb4, 0x30, 0xc8,
        0xba, 0x0d, 0x83, 0x07, 0x06, 0x63, 0xb0, 0x61, 0x00, 0xc8, 0x00, 0xd8, 0x50, 0x4c, 0x54, 0x19, 0x40, 0x40, 0x15, 0x36, 0x36, 0xbb, 0x36, 0x97,
        0x34, 0xb2, 0x32, 0x37, 0xba, 0x29, 0x41, 0x50, 0x85, 0x0c, 0xcf, 0xc5, 0xae, 0x4c, 0x6e, 0x2e, 0xed, 0xcd, 0x6d, 0x4a, 0x40, 0x34, 0x21, 0xc3,
        0x73, 0xb1, 0x0b, 0x63, 0xb3, 0x2b, 0x93, 0x9b, 0x12, 0x18, 0x75, 0xc8, 0xf0, 0x5c, 0xe6, 0xd0, 0xc2, 0xc8, 0xca, 0xe4, 0x9a, 0xde, 0xc8, 0xca,
        0xd8, 0xa6, 0x04, 0x49, 0x19, 0x32, 0x3c, 0x17, 0xb9, 0xb2, 0xb9, 0xb7, 0x3a, 0xb9, 0xb1, 0xb2, 0xb9, 0x29, 0xc1, 0x53, 0x87, 0x0c, 0xcf, 0xc5,
        0x2e, 0xad, 0xec, 0x2e, 0x89, 0x6c, 0x8a, 0x2e, 0x8c, 0xae, 0x6c, 0x4a, 0x10, 0xd5, 0x21, 0xc3, 0x73, 0x29, 0x73, 0xa3, 0x93, 0xcb, 0x83, 0x7a,
        0x4b, 0x73, 0xa3, 0x9b, 0x9b, 0x12, 0x94, 0x01, 0x00, 0x00, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x49, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c,
        0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6,
        0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43,
        0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48,
        0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e,
        0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89,
        0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68,
        0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78,
        0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5,
        0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c,
        0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43,
        0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x8c, 0xc8, 0x21, 0x07, 0x7c, 0x70,
        0x03, 0x72, 0x10, 0x87, 0x73, 0x70, 0x03, 0x7b, 0x08, 0x07, 0x79, 0x60, 0x87, 0x70, 0xc8, 0x87, 0x77, 0xa8, 0x07, 0x7a, 0x00, 0x00, 0x00, 0x00,
        0x71, 0x20, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x26, 0xb0, 0x0d, 0x97, 0xef, 0x3c, 0xbe, 0x10, 0x50, 0x45, 0x41, 0x44, 0xa5, 0x03, 0x0c, 0x25,
        0x61, 0x00, 0x02, 0xe6, 0x17, 0xb7, 0x6d, 0x03, 0xd2, 0x70, 0xf9, 0xce, 0xe3, 0x0b, 0x11, 0x01, 0x4c, 0x44, 0x08, 0x34, 0xc3, 0x42, 0x58, 0x40,
        0x35, 0x5c, 0xbe, 0xf3, 0xf8, 0xd2, 0xe4, 0x44, 0x04, 0x4a, 0x4d, 0x0f, 0x35, 0xf9, 0xc5, 0x6d, 0x1b, 0x00, 0xc1, 0x00, 0x48, 0x03, 0x00, 0x00,
        0x61, 0x20, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x34, 0x4a, 0xa1, 0xec,
        0x4a, 0x8e, 0x48, 0x11, 0x94, 0x40, 0x19, 0x50, 0x1b, 0x01, 0xa0, 0x32, 0x46, 0x00, 0x82, 0x20, 0x88, 0x7f, 0x00, 0x00, 0x23, 0x06, 0x09, 0x00,
        0x82, 0x60, 0xe0, 0x5c, 0x46, 0x51, 0x55, 0xc2, 0x88, 0xc1, 0x01, 0x80, 0x20, 0x18, 0x2c, 0x19, 0x12, 0x58, 0xa3, 0x09, 0x01, 0x30, 0x9a, 0x20,
        0x04, 0xa3, 0x09, 0x83, 0x30, 0x9a, 0x40, 0x0c, 0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0x60, 0x7c, 0xce, 0xb6, 0x49, 0xc4, 0x88, 0x41, 0x02, 0x80,
        0x20, 0x18, 0x18, 0x9f, 0xb3, 0x6d, 0xca, 0x30, 0x62, 0x90, 0x00, 0x20, 0x08, 0x06, 0xc6, 0xe7, 0x6c, 0xdb, 0x22, 0x8c, 0x18, 0x24, 0x00, 0x08,
        0x82, 0x81, 0xf1, 0x39, 0xdb, 0x96, 0x04, 0x23, 0x06, 0x07, 0x00, 0x82, 0x60, 0xb0, 0x78, 0x8d, 0xc1, 0x8d, 0x26, 0x04, 0xc0, 0x68, 0x82, 0x10,
        0x8c, 0x26, 0x0c, 0xc2, 0x68, 0x02, 0x31, 0x8c, 0x18, 0x24, 0x00, 0x08, 0x82, 0x81, 0x41, 0x06, 0x53, 0x18, 0x80, 0xc1, 0x45, 0x8c, 0x18, 0x24,
        0x00, 0x08, 0x82, 0x81, 0x41, 0x06, 0x53, 0x18, 0x80, 0xc1, 0x33, 0x8c, 0x18, 0x24, 0x00, 0x08, 0x82, 0x81, 0x41, 0x06, 0x53, 0x18, 0x80, 0x01,
        0x24, 0x8c, 0x18, 0x24, 0x00, 0x08, 0x82, 0x81, 0x41, 0x06, 0x53, 0x18, 0x80, 0x81, 0x13, 0x8c, 0x18, 0x24, 0x00, 0x08, 0x82, 0x81, 0x41, 0x06,
        0x93, 0x06, 0x06, 0x17, 0x33, 0x62, 0x90, 0x00, 0x20, 0x08, 0x06, 0x06, 0x19, 0x4c, 0x1a, 0x18, 0x3c, 0xcc, 0x88, 0x41, 0x02, 0x80, 0x20, 0x18,
        0x18, 0x64, 0x30, 0x69, 0x60, 0x00, 0x31, 0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0x60, 0x90, 0xc1, 0xa4, 0x81, 0x81, 0xc3, 0x20, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    static const DWORD ps_code_3rt_dxbc[] =
    {
        0x43425844, 0xe1e2c26b, 0x10d9607c, 0x4a0f0786, 0xc368f603, 0x00000001, 0x0000013c, 0x00000003,
        0x0000002c, 0x0000003c, 0x000000a0, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x0000005c, 0x00000003, 0x00000008, 0x00000050, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x00000050, 0x00000001, 0x00000000, 0x00000003, 0x00000001, 0x0000000f, 0x00000050,
        0x00000002, 0x00000000, 0x00000003, 0x00000002, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074,
        0x58454853, 0x00000094, 0x00000050, 0x00000025, 0x0100086a, 0x04000059, 0x00208e46, 0x00000000,
        0x00000002, 0x03000065, 0x001020f2, 0x00000000, 0x03000065, 0x001020f2, 0x00000001, 0x03000065,
        0x001020f2, 0x00000002, 0x06000036, 0x001020f2, 0x00000000, 0x00208e46, 0x00000000, 0x00000000,
        0x06000036, 0x001020f2, 0x00000001, 0x00208e46, 0x00000000, 0x00000001, 0x08000036, 0x001020f2,
        0x00000002, 0x00004002, 0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x0100003e,
    };
    const D3D12_SHADER_BYTECODE ps = {
        use_dxil ? (const void*)ps_code_dxil : (const void*)ps_code_dxbc,
        use_dxil ? sizeof(ps_code_dxil) : sizeof(ps_code_dxbc)
    };
    const D3D12_SHADER_BYTECODE ps_3rt = {
        use_dxil ? (const void*)ps_code_3rt_dxil : (const void*)ps_code_3rt_dxbc,
        use_dxil ? sizeof(ps_code_3rt_dxil) : sizeof(ps_code_3rt_dxbc)
    };
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

    static const DWORD vs_code[] =
    {
#if 0
        float4 main(int4 p : POSITION) : SV_Position
        {
            return p;
        }
#endif
        0x43425844, 0x3fd50ab1, 0x580a1d14, 0x28f5f602, 0xd1083e3a, 0x00000001, 0x000000d8, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000002, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x7469736f, 0x006e6f69, 0x52444853, 0x0000003c, 0x00010040,
        0x0000000f, 0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x0500002b, 0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs = {vs_code, sizeof(vs_code)};
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
            context.render_target_desc.Format, &vs, NULL, &input_layout);

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

#if 0
    #define rs_text "UAV(u0), RootConstants(num32BitConstants=1, b0)"

    RWStructuredBuffer<uint> buffer : register(u0);

    cbuffer info_t : register(b0)
    {
            uint value;
    };

    [RootSignature(rs_text)]
    [numthreads(1,1,1)]
    void main()
    {
            buffer[0] = value;
    }
#endif
    static const DWORD cs_code[] =
    {
        0x43425844, 0x3032c9e9, 0x035e534c, 0x1513248b, 0xdb9ceb89, 0x00000001, 0x00000110, 0x00000004,
        0x00000030, 0x00000040, 0x00000050, 0x000000c0, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
        0x4e47534f, 0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000068, 0x00050050, 0x0000001a,
        0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0400009e, 0x0011e000, 0x00000000,
        0x00000004, 0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x0a0000a8, 0x0011e012, 0x00000000,
        0x00004001, 0x00000000, 0x00004001, 0x00000000, 0x0020800a, 0x00000000, 0x00000000, 0x0100003e,
        0x30535452, 0x00000048, 0x00000002, 0x00000002, 0x00000018, 0x00000000, 0x00000048, 0x00000000,
        0x00000004, 0x00000000, 0x00000030, 0x00000001, 0x00000000, 0x0000003c, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000001,
    };

#if 0
    #define rs_text "RootConstants(num32BitConstants=4, b0)"

    [RootSignature(rs_text)]
    float4 main(uint vid : SV_VERTEXID) : SV_POSITION
    {
            return float4(
                    -1.0f + 4.0f * float(vid % 2),
                    -1.0f + 2.0f * float(vid & 2),
                    0.0f, 1.0f);
    }
#endif
    static const DWORD vs_code[] =
    {
        0x43425844, 0x7d259eed, 0xd7246bd4, 0xeb5e0efc, 0x9e31fb00, 0x00000001, 0x000001b4, 0x00000004,
        0x00000030, 0x00000064, 0x00000098, 0x0000017c, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008,
        0x00000020, 0x00000000, 0x00000006, 0x00000001, 0x00000000, 0x00000101, 0x565f5653, 0x45545245,
        0x00444958, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001,
        0x00000003, 0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x58454853, 0x000000dc,
        0x00010050, 0x00000037, 0x0100086a, 0x04000060, 0x00101012, 0x00000000, 0x00000006, 0x04000067,
        0x001020f2, 0x00000000, 0x00000001, 0x02000068, 0x00000001, 0x0a000001, 0x00100032, 0x00000000,
        0x00101006, 0x00000000, 0x00004002, 0x00000001, 0x00000002, 0x00000000, 0x00000000, 0x05000056,
        0x00100032, 0x00000000, 0x00100046, 0x00000000, 0x09000032, 0x00102012, 0x00000000, 0x0010000a,
        0x00000000, 0x00004001, 0x40800000, 0x00004001, 0xbf800000, 0x09000032, 0x00102022, 0x00000000,
        0x0010001a, 0x00000000, 0x00004001, 0x40000000, 0x00004001, 0xbf800000, 0x08000036, 0x001020c2,
        0x00000000, 0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x3f800000, 0x0100003e, 0x30535452,
        0x00000030, 0x00000002, 0x00000001, 0x00000018, 0x00000000, 0x00000030, 0x00000000, 0x00000001,
        0x00000000, 0x00000024, 0x00000000, 0x00000000, 0x00000004,
    };

#if 0
    #define rs_text "RootConstants(num32BitConstants=4, b0)"

    cbuffer info_t : register(b0)
    {
            float4 color;
    };

    [RootSignature(rs_text)]
    float4 main() : SV_TARGET0
    {
            return color;
    }
#endif
    static const DWORD ps_code[] =
    {
        0x43425844, 0x8f5e3ee2, 0x0d8ffa83, 0x9d34e012, 0x6e287df7, 0x00000001, 0x000000f8, 0x00000004,
        0x00000030, 0x00000040, 0x00000074, 0x000000c0, 0x4e475349, 0x00000008, 0x00000000, 0x00000008,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x58454853, 0x00000044, 0x00000050,
        0x00000011, 0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x03000065, 0x001020f2,
        0x00000000, 0x06000036, 0x001020f2, 0x00000000, 0x00208e46, 0x00000000, 0x00000000, 0x0100003e,
        0x30535452, 0x00000030, 0x00000002, 0x00000001, 0x00000018, 0x00000000, 0x00000030, 0x00000000,
        0x00000001, 0x00000000, 0x00000024, 0x00000000, 0x00000000, 0x00000004,
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    
    command_list = context.list;
    queue = context.queue;

    uav_buffer = create_default_buffer(context.device, 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    hr = ID3D12Device_CreateRootSignature(context.device, 0, cs_code, sizeof(cs_code), &IID_ID3D12RootSignature, (void**)&root_signature);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    memset(&compute_desc, 0, sizeof(compute_desc));
    compute_desc.CS = shader_bytecode(cs_code, sizeof(cs_code));
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

    hr = ID3D12Device_CreateRootSignature(context.device, 0, vs_code, sizeof(vs_code), &IID_ID3D12RootSignature, (void**)&root_signature);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    memset(&graphics_desc, 0, sizeof(graphics_desc));
    graphics_desc.VS = shader_bytecode(vs_code, sizeof(vs_code));
    graphics_desc.PS = shader_bytecode(ps_code, sizeof(ps_code));
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

    static const DWORD vs_code[] =
    {
#if 0
        float4 main(float4 pos : POS) : SV_POSITION {
                return pos;
        }
#endif
        0x43425844, 0xd0f999d3, 0x5250b8b9, 0x32f55488, 0x0498c795, 0x00000001, 0x000000d4, 0x00000003,
        0x0000002c, 0x00000058, 0x0000008c, 0x4e475349, 0x00000024, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00534f50, 0x4e47534f, 0x0000002c,
        0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f,
        0x505f5653, 0x5449534f, 0x004e4f49, 0x58454853, 0x00000040, 0x00010050, 0x00000010, 0x0100086a,
        0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x05000036,
        0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };

    static const DWORD ps_code[] =
    {
#if 0
        float4 main() : SV_TARGET {
                return float4(1.0f, 1.0f, 1.0f, 1.0f);
        }
#endif
        0x43425844, 0x29b14cf3, 0xb991cf90, 0x9e455ffc, 0x4675b046, 0x00000001, 0x000000b4, 0x00000003,
        0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x58454853, 0x0000003c, 0x00000050, 0x0000000f,
        0x0100086a, 0x03000065, 0x001020f2, 0x00000000, 0x08000036, 0x001020f2, 0x00000000, 0x00004002,
        0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x0100003e,
    };

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
    pso_desc.VS.pShaderBytecode = vs_code;
    pso_desc.VS.BytecodeLength = sizeof(vs_code);
    pso_desc.PS.pShaderBytecode = ps_code;
    pso_desc.PS.BytecodeLength = sizeof(ps_code);
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

    pso_desc.PS.pShaderBytecode = vs_code;
    pso_desc.PS.BytecodeLength = sizeof(vs_code);

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

    static const DWORD vs_code[] =
    {
#if 0
        float4 main(float4 a : A) : SV_Position
        {
                return a;
        }
#endif
        0x43425844, 0xecd820c8, 0x89ee4b40, 0xb73efa73, 0x4ed91573, 0x00000001, 0x000000d4, 0x00000003,
        0x0000002c, 0x00000058, 0x0000008c, 0x4e475349, 0x00000024, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0xabab0041, 0x4e47534f, 0x0000002c,
        0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f,
        0x505f5653, 0x7469736f, 0x006e6f69, 0x58454853, 0x00000040, 0x00010050, 0x00000010, 0x0100086a,
        0x0300005f, 0x001010f2, 0x00000000, 0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x05000036,
        0x001020f2, 0x00000000, 0x00101e46, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs = SHADER_BYTECODE(vs_code);

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

    init_pipeline_state_desc(&pso, context.root_signature, DXGI_FORMAT_R8G8B8A8_UNORM, &vs, NULL, &layout);
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

    static const D3D12_INPUT_ELEMENT_DESC input_elements[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

#if 0
    float4 main(in float2 pos : POSITION) : SV_POSITION
    {
            return float4(pos, 0.0f, 1.0f);
    }
#endif
    static const DWORD vs_code[] =
    {
        0x43425844, 0x5f60e33d, 0xceaefe52, 0xf0605a1b, 0x6174c3a2, 0x00000001, 0x000000fc, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000303, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x58454853, 0x00000060, 0x00010050,
        0x00000018, 0x0100086a, 0x0300005f, 0x00101032, 0x00000000, 0x04000067, 0x001020f2, 0x00000000,
        0x00000001, 0x05000036, 0x00102032, 0x00000000, 0x00101046, 0x00000000, 0x08000036, 0x001020c2,
        0x00000000, 0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x3f800000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE vs_bytecode = { &vs_code, sizeof(vs_code) };

#if 0
    float4 main() : SV_TARGET
    {
            return 0.5f.xxxx;
    }
#endif
    static const DWORD ps_code[] =
    {
        0x43425844, 0x47e44c76, 0x613ff931, 0xe271e2f3, 0x54e2f1a9, 0x00000001, 0x000000b4, 0x00000003,
        0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f,
        0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x58454853, 0x0000003c, 0x00000050, 0x0000000f,
        0x0100086a, 0x03000065, 0x001020f2, 0x00000000, 0x08000036, 0x001020f2, 0x00000000, 0x00004002,
        0x3f000000, 0x3f000000, 0x3f000000, 0x3f000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps_bytecode = { &ps_code, sizeof(ps_code) };

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
            DXGI_FORMAT_R8G8B8A8_UNORM, &vs_bytecode, &ps_bytecode,
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

#if 0
    SamplerState s : register(s0);
    Texture2D<float> t : register(t0);

    cbuffer params : register(b0)
    {
            float depth;
            float slope;
    };

    float4 main(in uint id : SV_VERTEXID) : SV_POSITION
    {
            float2 coords = float2((id << 1) & 2, id & 2);
            return float4(coords * float2(2, -2) + float2(-1, 1), depth + slope * coords.y, 1);
    }
#endif
    static const DWORD vs_code[] =
    {
        0x43425844, 0x9430038e, 0x95fbf076, 0xc92d448d, 0xaa5babfb, 0x00000001, 0x000001bc, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000006, 0x00000001, 0x00000000, 0x00000101, 0x565f5653, 0x45545245, 0x00444958,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x58454853, 0x00000120, 0x00010050,
        0x00000048, 0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x04000060, 0x00101012,
        0x00000000, 0x00000006, 0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x02000068, 0x00000001,
        0x0b00008c, 0x00100012, 0x00000000, 0x00004001, 0x00000001, 0x00004001, 0x00000001, 0x0010100a,
        0x00000000, 0x00004001, 0x00000000, 0x07000001, 0x00100042, 0x00000000, 0x0010100a, 0x00000000,
        0x00004001, 0x00000002, 0x05000056, 0x00100032, 0x00000000, 0x00100086, 0x00000000, 0x0f000032,
        0x00102032, 0x00000000, 0x00100046, 0x00000000, 0x00004002, 0x40000000, 0xc0000000, 0x00000000,
        0x00000000, 0x00004002, 0xbf800000, 0x3f800000, 0x00000000, 0x00000000, 0x0b000032, 0x00102042,
        0x00000000, 0x0020801a, 0x00000000, 0x00000000, 0x0010001a, 0x00000000, 0x0020800a, 0x00000000,
        0x00000000, 0x05000036, 0x00102082, 0x00000000, 0x00004001, 0x3f800000, 0x0100003e
    };

    static const union d3d12_root_signature_subobject root_signature_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
        NULL, /* fill in dynamically */
    } };

    static const union d3d12_shader_bytecode_subobject vs_subobject = { { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, { vs_code, sizeof(vs_code) } } };

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
