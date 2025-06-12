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

void test_mesh_shader_create_pipeline(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7;
    ID3D12PipelineState *pipeline_state;
    ID3D12RootSignature *root_signature;
    ID3D12Device2 *device2;
    ID3D12Device *device;
    HRESULT hr;

#include "shaders/mesh_shader/headers/ms_empty.h"
#include "shaders/mesh_shader/headers/ms_nontrivial.h"
#include "shaders/mesh_shader/headers/ps_dummy.h"
#include "shaders/mesh_shader/headers/vs_dummy.h"

    static const union d3d12_shader_bytecode_subobject vs_subobject = {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, { vs_dummy_code_dxil, sizeof(vs_dummy_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_subobject = {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_empty_code_dxil, sizeof(ms_empty_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ps_subobject = {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_dummy_code_dxil, sizeof(ps_dummy_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_nontrivial_subobject = {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_nontrivial_code_dxil, sizeof(ms_nontrivial_code_dxil) } }};

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
        { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_BACK,
            FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, 0,
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF },
    }};

    static const union d3d12_sample_desc_subobject sample_desc_subobject =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,
        { 1, 0 },
    }};

    struct {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject ms;
    } ms_only_pipeline_desc;

    struct {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject ms;
        union d3d12_shader_bytecode_subobject vs;
        union d3d12_primitive_topology_subobject primitive_topology;
        union d3d12_input_layout_subobject input_layout;
        union d3d12_rasterizer_subobject rasterizer;
    } ms_vs_pipeline_desc;

    struct {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject ms;
        union d3d12_shader_bytecode_subobject ps;
        union d3d12_sample_desc_subobject sample_desc;
        union d3d12_rasterizer_subobject rasterizer;
    } ms_ps_pipeline_desc;

    struct {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject ms;
        union d3d12_primitive_topology_subobject primitive_topology;
        union d3d12_input_layout_subobject input_layout;
    } ms_ia_pipeline_desc;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    memset(&options7, 0, sizeof(options7));
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
    ok(SUCCEEDED(hr), "OPTIONS7 is not supported by runtime.\n");

    if (options7.MeshShaderTier < D3D12_MESH_SHADER_TIER_1)
    {
        skip("Mesh shaders not supported by device.\n");
        ID3D12Device_Release(device);
        return;
    }

    hr = ID3D12Device_QueryInterface(device, &IID_ID3D12Device2, (void **)&device2);
    ok(SUCCEEDED(hr), "Failed to query ID3D12Device2.\n");

    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = NULL;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    /* On the most basic level, only a root signature and mesh shader are required */
    ms_only_pipeline_desc.root_signature = root_signature_subobject;
    ms_only_pipeline_desc.root_signature.root_signature = root_signature;
    ms_only_pipeline_desc.ms = ms_subobject;
    hr = create_pipeline_state_from_stream(device2, &ms_only_pipeline_desc, &pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create pipeline, hr %#x.\n", hr);
    ID3D12PipelineState_Release(pipeline_state);

    /* Mixing mesh shaders and any part of the old geometry pipelines is not allowed */
    ms_vs_pipeline_desc.root_signature = root_signature_subobject;
    ms_vs_pipeline_desc.root_signature.root_signature = root_signature;
    ms_vs_pipeline_desc.ms = ms_subobject;
    ms_vs_pipeline_desc.vs = vs_subobject;
    ms_vs_pipeline_desc.primitive_topology = primitive_topology_subobject;
    ms_vs_pipeline_desc.input_layout = input_layout_subobject;
    ms_vs_pipeline_desc.rasterizer = rasterizer_subobject;

    hr = create_pipeline_state_from_stream(device2, &ms_vs_pipeline_desc, &pipeline_state);
    ok(hr == E_INVALIDARG, "Unexpected result for pipeline creation, hr %#x.\n", hr);

    /* Pixel shaders require a non-trivial mesh shader */
    ms_ps_pipeline_desc.root_signature = root_signature_subobject;
    ms_ps_pipeline_desc.root_signature.root_signature = root_signature;
    ms_ps_pipeline_desc.ms = ms_subobject;
    ms_ps_pipeline_desc.ps = ps_subobject;
    ms_ps_pipeline_desc.sample_desc = sample_desc_subobject;
    ms_ps_pipeline_desc.rasterizer = rasterizer_subobject;

    hr = create_pipeline_state_from_stream(device2, &ms_ps_pipeline_desc, &pipeline_state);
    todo ok(hr == E_INVALIDARG, "Unexpected result for pipeline creation, hr %#x.\n", hr);

    if (SUCCEEDED(hr))
        ID3D12PipelineState_Release(pipeline_state);

    ms_ps_pipeline_desc.ms = ms_nontrivial_subobject;

    hr = create_pipeline_state_from_stream(device2, &ms_ps_pipeline_desc, &pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create pipeline, hr %#x.\n", hr);
    ID3D12PipelineState_Release(pipeline_state);

    /* Input assembly and primitive topology are ignored */
    ms_ia_pipeline_desc.root_signature = root_signature_subobject;
    ms_ia_pipeline_desc.root_signature.root_signature = root_signature;
    ms_ia_pipeline_desc.ms = ms_subobject;
    ms_ia_pipeline_desc.primitive_topology = primitive_topology_subobject;
    ms_ia_pipeline_desc.input_layout = input_layout_subobject;

    hr = create_pipeline_state_from_stream(device2, &ms_ia_pipeline_desc, &pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create pipeline, hr %#x.\n", hr);
    ID3D12PipelineState_Release(pipeline_state);

    ID3D12RootSignature_Release(root_signature);
    ID3D12Device2_Release(device2);
    ID3D12Device_Release(device);
}

void test_mesh_shader_rendering(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12Resource *srv_resource, *uav_resource;
    D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9;
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7;
    ID3D12GraphicsCommandList6 *command_list6;
    D3D12_ROOT_PARAMETER root_parameters[3];
    D3D12_HEAP_PROPERTIES heap_properties;
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pipeline_state;
    D3D12_RESOURCE_DESC resource_desc;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12Device2 *device2;
    float clip_distance;
    unsigned int i;
    HRESULT hr;

#include "shaders/mesh_shader/headers/ms_clip_distance.h"
#include "shaders/mesh_shader/headers/ms_cull_primitive.h"
#include "shaders/mesh_shader/headers/ms_culling.h"
#include "shaders/mesh_shader/headers/ms_derivatives.h"
#include "shaders/mesh_shader/headers/ms_interface_matching.h"
#include "shaders/mesh_shader/headers/ms_simple.h"
#include "shaders/mesh_shader/headers/ms_system_values.h"
#include "shaders/mesh_shader/headers/ps_culling.h"
#include "shaders/mesh_shader/headers/ps_derivatives.h"
#include "shaders/mesh_shader/headers/ps_green.h"
#include "shaders/mesh_shader/headers/ps_interface_matching.h"
#include "shaders/mesh_shader/headers/ps_system_values.h"

    static const union d3d12_shader_bytecode_subobject ps_simple_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_green_code_dxil, sizeof(ps_green_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ps_interface_matching_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_interface_matching_code_dxil, sizeof(ps_interface_matching_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ps_culling_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_culling_code_dxil, sizeof(ps_culling_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ps_system_values_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_system_values_code_dxil, sizeof(ps_system_values_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ps_derivatives_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_derivatives_code_dxil, sizeof(ps_derivatives_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_simple_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_simple_code_dxil, sizeof(ms_simple_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_interface_matching_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_interface_matching_code_dxil, sizeof(ms_interface_matching_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_clip_distance_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_clip_distance_code_dxil, sizeof(ms_clip_distance_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_culling_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_culling_code_dxil, sizeof(ms_culling_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_cull_primitive_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_cull_primitive_code_dxil, sizeof(ms_cull_primitive_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_system_values_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_system_values_code_dxil, sizeof(ms_system_values_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_derivatives_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_derivatives_code_dxil, sizeof(ms_derivatives_code_dxil) } }};

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

    static const union d3d12_render_target_formats_subobject render_target_subobject_rgba8 =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
        { { DXGI_FORMAT_R8G8B8A8_UNORM }, 1 },
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

    static const union d3d12_render_target_formats_subobject render_target_subobject_none =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
        { {0}, 0 },
    }};

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const uint32_t clear_buffer_data[] = { 0, 0, 0, 0 };
    static const uint32_t matching_buffer_data[] = { 0xdeadbeef, 0x3, 0x3, 1 };
    static const uint32_t cull_buffer_data[] = { 0xdeadbeef, 0x12345678, 0x00000000, 0xffffffff };
    static const uint32_t prim_id_data[] = { 0x02010212, 0x00020010, 0x00000001, 0x00000000 };
    static const uint32_t rt_colors[] = { 0xff000000, 0xff0000ff, 0xff00ff00, 0xff00ffff,
                                          0xffff0000, 0xffff00ff, 0xffffff00, 0xffffffff };

    struct {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject ms;
        union d3d12_shader_bytecode_subobject ps;
        union d3d12_rasterizer_subobject rasterizer;
        union d3d12_sample_desc_subobject sample_desc;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_render_target_formats_subobject render_targets;
        union d3d12_blend_subobject blend;
    } pipeline_desc;

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.rt_array_size = 8;
    desc.no_root_signature = true;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;

    memset(&options7, 0, sizeof(options7));
    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
    ok(SUCCEEDED(hr), "OPTIONS7 is not supported by runtime.\n");

    memset(&options9, 0, sizeof(options9));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS9, &options9, sizeof(options9));

    if (options7.MeshShaderTier < D3D12_MESH_SHADER_TIER_1)
    {
        skip("Mesh shaders not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device2, (void **)&device2);
    ok(SUCCEEDED(hr), "Failed to query ID3D12Device2.\n");
    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList6, (void **)&command_list6);
    ok(SUCCEEDED(hr), "Failed to query ID3D12GraphicsCommandList6.\n");

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Width = 16;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = ID3D12Device2_CreateCommittedResource(device2, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&srv_resource);
    ok(SUCCEEDED(hr), "Failed to create SRV resource, hr %#x.\n", hr);

    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    hr = ID3D12Device2_CreateCommittedResource(device2, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&uav_resource);
    ok(SUCCEEDED(hr), "Failed to create UAV resource, hr %#x.\n", hr);

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = 3;
    root_signature_desc.pParameters = root_parameters;

    memset(&root_parameters, 0, sizeof(root_parameters));
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].Descriptor.ShaderRegister = 1;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[2].Constants.ShaderRegister = 2;
    root_parameters[2].Constants.Num32BitValues = 1;
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    hr = create_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    /* Test rendering a simple quad using mesh shaders */
    pipeline_desc.root_signature.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
    pipeline_desc.root_signature.root_signature = root_signature;
    pipeline_desc.ms = ms_simple_subobject;
    pipeline_desc.ps = ps_simple_subobject;
    pipeline_desc.rasterizer = rasterizer_subobject;
    pipeline_desc.sample_desc = sample_desc_subobject;
    pipeline_desc.sample_mask = sample_mask_subobject;
    pipeline_desc.render_targets = render_target_subobject_rgba8;
    pipeline_desc.blend = blend_subobject;

    hr = create_pipeline_state_from_stream(device2, &pipeline_desc, &pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    ID3D12GraphicsCommandList6_ClearRenderTargetView(command_list6, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList6_OMSetRenderTargets(command_list6, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList6_SetGraphicsRootSignature(command_list6, root_signature);
    ID3D12GraphicsCommandList6_SetPipelineState(command_list6, pipeline_state);
    ID3D12GraphicsCommandList6_RSSetViewports(command_list6, 1, &context.viewport);
    ID3D12GraphicsCommandList6_RSSetScissorRects(command_list6, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList6_DispatchMesh(command_list6, 1, 1, 1);
    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, context.queue, context.list, 0xff00ff00, 0);
    ID3D12PipelineState_Release(pipeline_state);

    /* Test basic interface matching between mesh and pixel shaders.
     * This is currently broken on AMD D3D12 drivers */
    pipeline_desc.ms = ms_interface_matching_subobject;
    pipeline_desc.ps = ps_interface_matching_subobject;

    hr = create_pipeline_state_from_stream(device2, &pipeline_desc, &pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    reset_command_list(context.list, context.allocator);
    upload_buffer_data(uav_resource, 0, sizeof(clear_buffer_data), clear_buffer_data, context.queue, context.list);

    reset_command_list(context.list, context.allocator);
    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition_resource_state(context.list, uav_resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12GraphicsCommandList6_ClearRenderTargetView(command_list6, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList6_OMSetRenderTargets(command_list6, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList6_SetGraphicsRootSignature(command_list6, root_signature);
    ID3D12GraphicsCommandList6_SetGraphicsRootUnorderedAccessView(command_list6, 1, ID3D12Resource_GetGPUVirtualAddress(uav_resource));
    ID3D12GraphicsCommandList6_SetPipelineState(command_list6, pipeline_state);
    ID3D12GraphicsCommandList6_RSSetViewports(command_list6, 1, &context.viewport);
    ID3D12GraphicsCommandList6_RSSetScissorRects(command_list6, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList6_DispatchMesh(command_list6, 1, 1, 1);
    transition_resource_state(context.list, uav_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(uav_resource, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(matching_buffer_data); i++)
    {
        UINT value = get_readback_uint(&rb, i, 0, 0);
        ok(value == matching_buffer_data[i], "Readback value at %u is: %u\n", i, value);
    }

    release_resource_readback(&rb);

    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(context.render_target, 0, context.queue, context.list, 0xff00ff00, 0);
    ID3D12PipelineState_Release(pipeline_state);

    /* Test shared memory and dynamic primitive counts, as well as multiple dispatches. */
    pipeline_desc.render_targets = render_target_subobject_none;
    pipeline_desc.ms = ms_culling_subobject;
    pipeline_desc.ps = ps_culling_subobject;

    hr = create_pipeline_state_from_stream(device2, &pipeline_desc, &pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    reset_command_list(context.list, context.allocator);
    upload_buffer_data(srv_resource, 0, sizeof(cull_buffer_data), cull_buffer_data, context.queue, context.list);

    reset_command_list(context.list, context.allocator);
    transition_resource_state(context.list, uav_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(uav_resource, 0, sizeof(clear_buffer_data), clear_buffer_data, context.queue, context.list);

    reset_command_list(context.list, context.allocator);
    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition_resource_state(context.list, srv_resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition_resource_state(context.list, uav_resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12GraphicsCommandList6_OMSetRenderTargets(command_list6, 0, NULL, false, NULL);
    ID3D12GraphicsCommandList6_SetGraphicsRootSignature(command_list6, root_signature);
    ID3D12GraphicsCommandList6_SetGraphicsRootShaderResourceView(command_list6, 0, ID3D12Resource_GetGPUVirtualAddress(srv_resource));
    ID3D12GraphicsCommandList6_SetGraphicsRootUnorderedAccessView(command_list6, 1, ID3D12Resource_GetGPUVirtualAddress(uav_resource));
    ID3D12GraphicsCommandList6_SetPipelineState(command_list6, pipeline_state);
    ID3D12GraphicsCommandList6_RSSetViewports(command_list6, 1, &context.viewport);
    ID3D12GraphicsCommandList6_RSSetScissorRects(command_list6, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList6_DispatchMesh(command_list6, 4, 1, 1);
    transition_resource_state(context.list, uav_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(uav_resource, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(cull_buffer_data); i++)
    {
        UINT value = get_readback_uint(&rb, i, 0, 0);
        ok(value == cull_buffer_data[i], "Readback value at %u is: %u\n", i, value);
    }

    release_resource_readback(&rb);
    ID3D12PipelineState_Release(pipeline_state);

    /* Test SV_CullPrimitive */
    pipeline_desc.ms = ms_cull_primitive_subobject;
    pipeline_desc.ps = ps_culling_subobject;
    hr = create_pipeline_state_from_stream(device2, &pipeline_desc, &pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    reset_command_list(context.list, context.allocator);
    transition_resource_state(context.list, uav_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(uav_resource, 0, sizeof(clear_buffer_data), clear_buffer_data, context.queue, context.list);

    reset_command_list(context.list, context.allocator);
    transition_resource_state(context.list, srv_resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition_resource_state(context.list, uav_resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12GraphicsCommandList6_OMSetRenderTargets(command_list6, 0, NULL, false, NULL);
    ID3D12GraphicsCommandList6_SetGraphicsRootSignature(command_list6, root_signature);
    ID3D12GraphicsCommandList6_SetGraphicsRootShaderResourceView(command_list6, 0, ID3D12Resource_GetGPUVirtualAddress(srv_resource));
    ID3D12GraphicsCommandList6_SetGraphicsRootUnorderedAccessView(command_list6, 1, ID3D12Resource_GetGPUVirtualAddress(uav_resource));
    ID3D12GraphicsCommandList6_SetPipelineState(command_list6, pipeline_state);
    ID3D12GraphicsCommandList6_RSSetViewports(command_list6, 1, &context.viewport);
    ID3D12GraphicsCommandList6_RSSetScissorRects(command_list6, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList6_DispatchMesh(command_list6, 4, 1, 1);
    transition_resource_state(context.list, uav_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(uav_resource, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(cull_buffer_data); i++)
    {
        UINT value = get_readback_uint(&rb, i, 0, 0);
        ok(value == cull_buffer_data[i], "Readback value at %u is: %u\n", i, value);
    }

    release_resource_readback(&rb);
    ID3D12PipelineState_Release(pipeline_state);

    /* Test SV_ClipDistance with mesh shaders */
    pipeline_desc.render_targets = render_target_subobject_rgba8;
    pipeline_desc.ms = ms_clip_distance_subobject;
    pipeline_desc.ps = ps_simple_subobject;

    hr = create_pipeline_state_from_stream(device2, &pipeline_desc, &pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    for (i = 0; i < 2; i++)
    {
        clip_distance = i ? 5.0f : -1.0f;

        reset_command_list(context.list, context.allocator);

        if (i)
            transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12GraphicsCommandList6_ClearRenderTargetView(command_list6, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList6_OMSetRenderTargets(command_list6, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList6_SetGraphicsRootSignature(command_list6, root_signature);
        ID3D12GraphicsCommandList6_SetGraphicsRoot32BitConstant(command_list6, 2, float_bits_to_uint32(clip_distance), 0);
        ID3D12GraphicsCommandList6_SetPipelineState(command_list6, pipeline_state);
        ID3D12GraphicsCommandList6_RSSetViewports(command_list6, 1, &context.viewport);
        ID3D12GraphicsCommandList6_RSSetScissorRects(command_list6, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList6_DispatchMesh(command_list6, 4, 1, 1);
        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, context.queue, context.list,
                i ? 0xff00ff00u : 0xffffffffu, 0u);
    }

    ID3D12PipelineState_Release(pipeline_state);

    /* Test distributing primitives to multiple array layers, as well as SV_PRIMITIVEID. */
    pipeline_desc.ms = ms_system_values_subobject;
    pipeline_desc.ps = ps_system_values_subobject;

    hr = create_pipeline_state_from_stream(device2, &pipeline_desc, &pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    reset_command_list(context.list, context.allocator);
    transition_resource_state(context.list, uav_resource, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(uav_resource, 0, sizeof(clear_buffer_data), clear_buffer_data, context.queue, context.list);

    reset_command_list(context.list, context.allocator);
    transition_resource_state(context.list, uav_resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList6_ClearRenderTargetView(command_list6, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList6_OMSetRenderTargets(command_list6, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList6_SetGraphicsRootSignature(command_list6, root_signature);
    ID3D12GraphicsCommandList6_SetGraphicsRootUnorderedAccessView(command_list6, 1, ID3D12Resource_GetGPUVirtualAddress(uav_resource));
    ID3D12GraphicsCommandList6_SetPipelineState(command_list6, pipeline_state);
    ID3D12GraphicsCommandList6_RSSetViewports(command_list6, 1, &context.viewport);
    ID3D12GraphicsCommandList6_RSSetScissorRects(command_list6, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList6_DispatchMesh(command_list6, 1, 1, 1);
    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(context.list, uav_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    for (i = 0; i < ARRAY_SIZE(rt_colors); i++)
    {
        check_sub_resource_uint(context.render_target, i, context.queue, context.list, rt_colors[i], 0);
        reset_command_list(context.list, context.allocator);
    }

    get_buffer_readback_with_command_list(uav_resource, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(prim_id_data); i++)
    {
        UINT value = get_readback_uint(&rb, i, 0, 0);
        ok(value == prim_id_data[i], "Readback value at %u is: %u\n", i, value);
    }

    release_resource_readback(&rb);
    ID3D12PipelineState_Release(pipeline_state);

    if (options9.DerivativesInMeshAndAmplificationShadersSupported)
    {
        pipeline_desc.ms = ms_derivatives_subobject;
        pipeline_desc.ps = ps_derivatives_subobject;

        hr = create_pipeline_state_from_stream(device2, &pipeline_desc, &pipeline_state);
        ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12GraphicsCommandList6_ClearRenderTargetView(command_list6, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList6_OMSetRenderTargets(command_list6, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList6_SetGraphicsRootSignature(command_list6, root_signature);
        ID3D12GraphicsCommandList6_SetGraphicsRootUnorderedAccessView(command_list6, 1, ID3D12Resource_GetGPUVirtualAddress(uav_resource));
        ID3D12GraphicsCommandList6_SetPipelineState(command_list6, pipeline_state);
        ID3D12GraphicsCommandList6_RSSetViewports(command_list6, 1, &context.viewport);
        ID3D12GraphicsCommandList6_RSSetScissorRects(command_list6, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList6_DispatchMesh(command_list6, 1, 1, 1);

        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, context.queue, context.list, 0x24000011, 1);

        ID3D12PipelineState_Release(pipeline_state);
    }
    else
    {
        skip("Derivatives in mesh and amplification shaders not supported.\n");
    }

    ID3D12Resource_Release(srv_resource);
    ID3D12Resource_Release(uav_resource);
    ID3D12RootSignature_Release(root_signature);
    ID3D12GraphicsCommandList6_Release(command_list6);
    ID3D12Device2_Release(device2);
    destroy_test_context(&context);
}

void test_mesh_shader_execute_indirect(void)
{
    D3D12_COMMAND_SIGNATURE_DESC command_signature_desc;
    D3D12_INDIRECT_ARGUMENT_DESC indirect_argument_desc;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7;
    ID3D12GraphicsCommandList6 *command_list6;
    ID3D12CommandSignature *command_signature;
    D3D12_HEAP_PROPERTIES heap_properties;
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pipeline_state;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *indirect_buffer;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12Device2 *device2;
    unsigned int i;
    HRESULT hr;

#include "shaders/mesh_shader/headers/ms_execute_indirect.h"
#include "shaders/mesh_shader/headers/ps_green.h"

    static const union d3d12_shader_bytecode_subobject ps_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_green_code_dxil, sizeof(ps_green_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_execute_indirect_code_dxil, sizeof(ms_execute_indirect_code_dxil) } }};

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
        { { DXGI_FORMAT_R8G8B8A8_UNORM }, 1 },
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

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const struct
    {
        uint32_t data[4];
        bool indirect_count;
        uint32_t expected;
    }
    tests[] =
    {
        {{0,0,0,0}, false, 0xffffffffu},
        {{0,1,1,0}, false, 0xffffffffu},
        {{1,1,1,0}, false, 0xff00ff00u},
        {{1,1,1,0}, true,  0xffffffffu},
        {{0,0,0,1}, true,  0xffffffffu},
        {{1,1,1,1}, true,  0xff00ff00u},
    };

    struct {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject ms;
        union d3d12_shader_bytecode_subobject ps;
        union d3d12_rasterizer_subobject rasterizer;
        union d3d12_sample_desc_subobject sample_desc;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_render_target_formats_subobject render_targets;
        union d3d12_blend_subobject blend;
    } pipeline_desc;

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.no_root_signature = true;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;

    memset(&options7, 0, sizeof(options7));
    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
    ok(SUCCEEDED(hr), "OPTIONS7 is not supported by runtime.\n");

    if (options7.MeshShaderTier < D3D12_MESH_SHADER_TIER_1)
    {
        skip("Mesh shaders not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device2, (void **)&device2);
    ok(SUCCEEDED(hr), "Failed to query ID3D12Device2.\n");
    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList6, (void **)&command_list6);
    ok(SUCCEEDED(hr), "Failed to query ID3D12GraphicsCommandList6.\n");

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Width = 16;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = ID3D12Device2_CreateCommittedResource(device2, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&indirect_buffer);
    ok(SUCCEEDED(hr), "Failed to create SRV resource, hr %#x.\n", hr);

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = 0;
    root_signature_desc.pParameters = NULL;

    hr = create_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    memset(&command_signature_desc, 0, sizeof(command_signature_desc));
    command_signature_desc.ByteStride = 12;
    command_signature_desc.NumArgumentDescs = 1;
    command_signature_desc.pArgumentDescs = &indirect_argument_desc;

    memset(&indirect_argument_desc, 0, sizeof(indirect_argument_desc));
    indirect_argument_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

    hr = ID3D12Device2_CreateCommandSignature(device2, &command_signature_desc, NULL, &IID_ID3D12CommandSignature, (void **)&command_signature);
    ok(SUCCEEDED(hr), "Failed to create command signature, hr %#x.\n", hr);

    pipeline_desc.root_signature.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
    pipeline_desc.root_signature.root_signature = root_signature;
    pipeline_desc.ms = ms_subobject;
    pipeline_desc.ps = ps_subobject;
    pipeline_desc.rasterizer = rasterizer_subobject;
    pipeline_desc.sample_desc = sample_desc_subobject;
    pipeline_desc.sample_mask = sample_mask_subobject;
    pipeline_desc.render_targets = render_target_subobject;
    pipeline_desc.blend = blend_subobject;

    hr = create_pipeline_state_from_stream(device2, &pipeline_desc, &pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        upload_buffer_data(indirect_buffer, 0, sizeof(tests[i].data), tests[i].data, context.queue, context.list);

        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, indirect_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        ID3D12GraphicsCommandList6_ClearRenderTargetView(command_list6, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList6_OMSetRenderTargets(command_list6, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList6_SetGraphicsRootSignature(command_list6, root_signature);
        ID3D12GraphicsCommandList6_SetPipelineState(command_list6, pipeline_state);
        ID3D12GraphicsCommandList6_RSSetViewports(command_list6, 1, &context.viewport);
        ID3D12GraphicsCommandList6_RSSetScissorRects(command_list6, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList6_ExecuteIndirect(command_list6, command_signature, 1, indirect_buffer, 0,
                tests[i].indirect_count ? indirect_buffer : NULL, tests[i].indirect_count ? 12 : 0);
        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, context.queue, context.list, tests[i].expected, 0);

        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        transition_resource_state(context.list, indirect_buffer, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);
    }

    ID3D12PipelineState_Release(pipeline_state);
    ID3D12CommandSignature_Release(command_signature);
    ID3D12Resource_Release(indirect_buffer);
    ID3D12RootSignature_Release(root_signature);
    ID3D12GraphicsCommandList6_Release(command_list6);
    ID3D12Device2_Release(device2);
    destroy_test_context(&context);
}

void test_mesh_shader_execute_indirect_state(void)
{
    D3D12_INDIRECT_ARGUMENT_DESC indirect_argument_desc[3];
    D3D12_COMMAND_SIGNATURE_DESC command_signature_desc;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7;
    ID3D12GraphicsCommandList6 *command_list6;
    ID3D12CommandSignature *command_signature;
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pipeline_state;
    ID3D12Resource *indirect_buffer;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    D3D12_VIEWPORT viewport;
    ID3D12Resource *output;
    ID3D12Device2 *device2;
    D3D12_RECT scissor;
    unsigned int i, j;
    HRESULT hr;

#include "shaders/mesh_shader/headers/ms_execute_indirect_state.h"
#include "shaders/mesh_shader/headers/ps_execute_indirect_state.h"
    static const union d3d12_shader_bytecode_subobject ps_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_execute_indirect_state_code_dxil, sizeof(ps_execute_indirect_state_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_execute_indirect_state_code_dxil, sizeof(ms_execute_indirect_state_code_dxil) } }};

    static const union d3d12_rasterizer_subobject rasterizer_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
        { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE,
            TRUE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, 0,
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF },
    } };

    struct test_data
    {
        uint32_t prims;
        uint32_t va[2];
        uint32_t mesh_groups[3];
        uint32_t indirect_count;
    };

    struct
    {
        struct test_data data;
        bool indirect_count;
    }
    tests[] =
    {
        {{5, {0, 0}, {0, 3, 4}, 5}, false},
        {{4, {0, 0}, {4, 3, 4}, 6}, false},
        {{3, {0, 0}, {1, 0, 0}, 2}, false},
        {{5, {0, 0}, {2, 3, 8}, 8}, true},
        {{4, {0, 0}, {4, 3, 6}, 3}, true},
        {{3, {0, 0}, {1, 3, 0}, 0}, true},
        {{6, {0, 0}, {2, 3, 4}, 8}, true},
    };

    struct {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject ms;
        union d3d12_shader_bytecode_subobject ps;
        union d3d12_rasterizer_subobject rasterizer;
    } pipeline_desc;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_root_signature = true;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;

    memset(&options7, 0, sizeof(options7));
    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
    ok(SUCCEEDED(hr), "OPTIONS7 is not supported by runtime.\n");

    if (options7.MeshShaderTier < D3D12_MESH_SHADER_TIER_1)
    {
        skip("Mesh shaders not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device2, (void **)&device2);
    ok(SUCCEEDED(hr), "Failed to query ID3D12Device2.\n");
    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList6, (void **)&command_list6);
    ok(SUCCEEDED(hr), "Failed to query ID3D12GraphicsCommandList6.\n");

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    memset(root_parameters, 0, sizeof(root_parameters));
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_MESH;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[0].Constants.Num32BitValues = 1;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;

    hr = create_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    memset(&command_signature_desc, 0, sizeof(command_signature_desc));
    command_signature_desc.ByteStride = sizeof(struct test_data);
    command_signature_desc.NumArgumentDescs = ARRAY_SIZE(indirect_argument_desc);
    command_signature_desc.pArgumentDescs = indirect_argument_desc;

    memset(indirect_argument_desc, 0, sizeof(indirect_argument_desc));
    indirect_argument_desc[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    indirect_argument_desc[0].Constant.Num32BitValuesToSet = 1;
    indirect_argument_desc[0].Constant.RootParameterIndex = 0;
    indirect_argument_desc[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
    indirect_argument_desc[1].UnorderedAccessView.RootParameterIndex = 1;
    indirect_argument_desc[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

    hr = ID3D12Device2_CreateCommandSignature(device2, &command_signature_desc, root_signature, &IID_ID3D12CommandSignature, (void **)&command_signature);
    ok(SUCCEEDED(hr) || hr == E_NOTIMPL, "Failed to create command signature, hr %#x.\n", hr);
    if (FAILED(hr))
        command_signature = NULL;
    if (hr == E_NOTIMPL)
        skip("DGC is likely not implemented. Skipping test.\n");

    pipeline_desc.root_signature.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
    pipeline_desc.root_signature.root_signature = root_signature;
    pipeline_desc.ms = ms_subobject;
    pipeline_desc.ps = ps_subobject;
    pipeline_desc.rasterizer = rasterizer_subobject;

    hr = create_pipeline_state_from_stream(device2, &pipeline_desc, &pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    output = create_default_buffer(context.device, sizeof(tests) * sizeof(uint32_t), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    {
        struct test_data test_data[128];
        memset(test_data, 0, sizeof(test_data));
        for (i = 0; i < ARRAY_SIZE(tests); i++)
            test_data[i] = tests[i].data;

        for (i = 0; i < ARRAY_SIZE(test_data); i++)
        {
            D3D12_GPU_VIRTUAL_ADDRESS va = ID3D12Resource_GetGPUVirtualAddress(output) + i * sizeof(uint32_t);
            memcpy(test_data[i].va, &va, sizeof(va));
        }

        indirect_buffer = create_upload_buffer(context.device, sizeof(test_data), test_data);
    }

    if (command_signature)
    {
        for (i = 0; i < ARRAY_SIZE(tests); i++)
        {
            ID3D12GraphicsCommandList6_SetGraphicsRootSignature(command_list6, root_signature);
            ID3D12GraphicsCommandList6_SetPipelineState(command_list6, pipeline_state);
            set_viewport(&viewport, 0, 0, 1, 1, 0, 1);
            set_rect(&scissor, 0, 0, 1, 1);
            ID3D12GraphicsCommandList6_RSSetViewports(command_list6, 1, &viewport);
            ID3D12GraphicsCommandList6_RSSetScissorRects(command_list6, 1, &scissor);

            ID3D12GraphicsCommandList6_ExecuteIndirect(command_list6, command_signature,
                tests[i].indirect_count ? 64 : tests[i].data.indirect_count,
                indirect_buffer, sizeof(struct test_data) * i,
                tests[i].indirect_count ? indirect_buffer : NULL,
                tests[i].indirect_count ? (sizeof(struct test_data) * i + offsetof(struct test_data, indirect_count)) : 0);
        }
    }

    transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    if (command_signature)
    {
        uint32_t expected[128] = { 0 };
        for (i = 0; i < ARRAY_SIZE(tests); i++)
            for (j = 0; j < tests[i].data.indirect_count; j++)
                expected[i + j] += (tests[i + j].data.prims + 1) * tests[i + j].data.mesh_groups[0] * tests[i + j].data.mesh_groups[1] * tests[i + j].data.mesh_groups[2];

        for (i = 0; i < ARRAY_SIZE(tests); i++)
            ok(get_readback_uint(&rb, i, 0, 0) == expected[i], "Index %u: expected %u, got %u.\n", i, expected[i], get_readback_uint(&rb, i, 0, 0));
    }

    release_resource_readback(&rb);

    ID3D12Resource_Release(output);
    ID3D12PipelineState_Release(pipeline_state);
    if (command_signature)
        ID3D12CommandSignature_Release(command_signature);
    ID3D12Resource_Release(indirect_buffer);
    ID3D12RootSignature_Release(root_signature);
    ID3D12GraphicsCommandList6_Release(command_list6);
    ID3D12Device2_Release(device2);
    destroy_test_context(&context);
}

void test_amplification_shader(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12Resource *srv_resource, *uav_resource;
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7;
    ID3D12GraphicsCommandList6 *command_list6;
    D3D12_ROOT_PARAMETER root_parameters[2];
    D3D12_HEAP_PROPERTIES heap_properties;
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pipeline_state;
    D3D12_RESOURCE_DESC resource_desc;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12Device2 *device2;
    unsigned int i;
    HRESULT hr;

#include "shaders/mesh_shader/headers/as_simple.h"
#include "shaders/mesh_shader/headers/as_multi_workgroup.h"
#include "shaders/mesh_shader/headers/ms_payload.h"
#include "shaders/mesh_shader/headers/ms_multi_workgroup.h"
#include "shaders/mesh_shader/headers/ps_color.h"

    static const union d3d12_shader_bytecode_subobject as_simple_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, { as_simple_code_dxil, sizeof(as_simple_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject as_multi_workgroup_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, { as_multi_workgroup_code_dxil, sizeof(as_multi_workgroup_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_simple_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_payload_code_dxil, sizeof(ms_payload_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_multi_workgroup_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_multi_workgroup_code_dxil, sizeof(ms_multi_workgroup_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ps_simple_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_color_code_dxil, sizeof(ps_color_code_dxil) } }};

    static const union d3d12_shader_bytecode_subobject ps_none_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { NULL, 0 } }};

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

    static const union d3d12_render_target_formats_subobject render_target_subobject_rgba8 =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
        { { DXGI_FORMAT_R8G8B8A8_UNORM }, 1 },
    }};

    static const union d3d12_render_target_formats_subobject render_target_subobject_none =
    {{
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
        { {0}, 0 },
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

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const uint32_t clear_buffer_data[] = { 0, 0, 0, 0 };
    static const uint32_t workgroup_counts[] = { 1, 7, 0, 32 };
    static const uint32_t workgroup_masks[] = { 0x1, 0x7f, 0x0, 0xffffffffu };

    struct {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject as;
        union d3d12_shader_bytecode_subobject ms;
        union d3d12_shader_bytecode_subobject ps;
        union d3d12_rasterizer_subobject rasterizer;
        union d3d12_sample_desc_subobject sample_desc;
        union d3d12_sample_mask_subobject sample_mask;
        union d3d12_render_target_formats_subobject render_targets;
        union d3d12_blend_subobject blend;
    } pipeline_desc;

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.no_root_signature = true;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;

    memset(&options7, 0, sizeof(options7));
    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
    ok(SUCCEEDED(hr), "OPTIONS7 is not supported by runtime.\n");

    if (options7.MeshShaderTier < D3D12_MESH_SHADER_TIER_1)
    {
        skip("Mesh shaders not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device2, (void **)&device2);
    ok(SUCCEEDED(hr), "Failed to query ID3D12Device2.\n");
    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList6, (void **)&command_list6);
    ok(SUCCEEDED(hr), "Failed to query ID3D12GraphicsCommandList6.\n");

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Width = 16;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = ID3D12Device2_CreateCommittedResource(device2, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&srv_resource);
    ok(SUCCEEDED(hr), "Failed to create SRV resource, hr %#x.\n", hr);

    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    hr = ID3D12Device2_CreateCommittedResource(device2, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&uav_resource);
    ok(SUCCEEDED(hr), "Failed to create UAV resource, hr %#x.\n", hr);

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = 2;
    root_signature_desc.pParameters = root_parameters;

    memset(&root_parameters, 0, sizeof(root_parameters));
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].Descriptor.ShaderRegister = 1;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    hr = create_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    /* Test forwarding a simple payload from an amplification shader */
    pipeline_desc.root_signature.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
    pipeline_desc.root_signature.root_signature = root_signature;
    pipeline_desc.as = as_simple_subobject;
    pipeline_desc.ms = ms_simple_subobject;
    pipeline_desc.ps = ps_simple_subobject;
    pipeline_desc.rasterizer = rasterizer_subobject;
    pipeline_desc.sample_desc = sample_desc_subobject;
    pipeline_desc.sample_mask = sample_mask_subobject;
    pipeline_desc.render_targets = render_target_subobject_rgba8;
    pipeline_desc.blend = blend_subobject;

    hr = create_pipeline_state_from_stream(device2, &pipeline_desc, &pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    ID3D12GraphicsCommandList6_ClearRenderTargetView(command_list6, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList6_OMSetRenderTargets(command_list6, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList6_SetGraphicsRootSignature(command_list6, root_signature);
    ID3D12GraphicsCommandList6_SetPipelineState(command_list6, pipeline_state);
    ID3D12GraphicsCommandList6_RSSetViewports(command_list6, 1, &context.viewport);
    ID3D12GraphicsCommandList6_RSSetScissorRects(command_list6, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList6_DispatchMesh(command_list6, 1, 1, 1);
    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, context.queue, context.list, 0xff00ff00, 0);
    ID3D12PipelineState_Release(pipeline_state);

    /* Test dispatching multiple mesh shader workgroups from an amplification shader */
    pipeline_desc.as = as_multi_workgroup_subobject;
    pipeline_desc.ms = ms_multi_workgroup_subobject;
    pipeline_desc.ps = ps_none_subobject;
    pipeline_desc.render_targets = render_target_subobject_none;

    hr = create_pipeline_state_from_stream(device2, &pipeline_desc, &pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    reset_command_list(context.list, context.allocator);
    upload_buffer_data(uav_resource, 0, sizeof(clear_buffer_data), clear_buffer_data, context.queue, context.list);

    reset_command_list(context.list, context.allocator);
    upload_buffer_data(srv_resource, 0, sizeof(workgroup_counts), workgroup_counts, context.queue, context.list);

    reset_command_list(context.list, context.allocator);
    transition_resource_state(context.list, srv_resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition_resource_state(context.list, uav_resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12GraphicsCommandList6_OMSetRenderTargets(command_list6, 0, NULL, false, NULL);
    ID3D12GraphicsCommandList6_SetGraphicsRootSignature(command_list6, root_signature);
    ID3D12GraphicsCommandList6_SetPipelineState(command_list6, pipeline_state);
    ID3D12GraphicsCommandList6_SetGraphicsRootShaderResourceView(command_list6, 0, ID3D12Resource_GetGPUVirtualAddress(srv_resource));
    ID3D12GraphicsCommandList6_SetGraphicsRootUnorderedAccessView(command_list6, 1, ID3D12Resource_GetGPUVirtualAddress(uav_resource));
    ID3D12GraphicsCommandList6_RSSetViewports(command_list6, 1, &context.viewport);
    ID3D12GraphicsCommandList6_RSSetScissorRects(command_list6, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList6_DispatchMesh(command_list6, 4, 1, 1);

    transition_resource_state(context.list, uav_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(uav_resource, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(workgroup_masks); i++)
    {
        UINT value = get_readback_uint(&rb, i, 0, 0);
        ok(value == workgroup_masks[i], "Readback value at %u is: %u\n", i, value);
    }

    release_resource_readback(&rb);
    ID3D12PipelineState_Release(pipeline_state);

    ID3D12Resource_Release(srv_resource);
    ID3D12Resource_Release(uav_resource);
    ID3D12RootSignature_Release(root_signature);
    ID3D12GraphicsCommandList6_Release(command_list6);
    ID3D12Device2_Release(device2);
    destroy_test_context(&context);
}

void test_amplification_shader_execute_indirect_state(void)
{
    D3D12_INDIRECT_ARGUMENT_DESC indirect_argument_desc[3];
    D3D12_COMMAND_SIGNATURE_DESC command_signature_desc;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7;
    ID3D12GraphicsCommandList6 *command_list6;
    ID3D12CommandSignature *command_signature;
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pipeline_state;
    ID3D12Resource *indirect_buffer;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    D3D12_VIEWPORT viewport;
    ID3D12Resource *output;
    ID3D12Device2 *device2;
    D3D12_RECT scissor;
    unsigned int i, j;
    HRESULT hr;

#include "shaders/mesh_shader/headers/as_execute_indirect_state.h"
#include "shaders/mesh_shader/headers/ms_execute_indirect_state_payload.h"
#include "shaders/mesh_shader/headers/ps_execute_indirect_state.h"

    static const union d3d12_shader_bytecode_subobject as_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, { as_execute_indirect_state_code_dxil, sizeof(as_execute_indirect_state_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ms_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, { ms_execute_indirect_state_payload_code_dxil, sizeof(ms_execute_indirect_state_payload_code_dxil) } }};
    static const union d3d12_shader_bytecode_subobject ps_subobject =
            {{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, { ps_execute_indirect_state_code_dxil, sizeof(ps_execute_indirect_state_code_dxil) } }};

    static const union d3d12_rasterizer_subobject rasterizer_subobject =
    { {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,
        { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_NONE,
            TRUE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, 0,
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF },
    } };

    struct test_data
    {
        uint32_t mesh_groups[3];
        uint32_t prims;
        D3D12_GPU_VIRTUAL_ADDRESS va;
        uint32_t tasks[3];
        uint32_t indirect_count;
    };

    struct
    {
        struct test_data data;
        uint32_t uav_offset;
        bool indirect_count;
    }
    tests[] =
    {
        {
            {{5, 6, 7}, 3, 0, {2, 3, 4}, 2},
            0, false,
        },
        {
            {{2, 9, 4}, 11, 0, {1, 3, 9}, 0}, /* Empty EI */
            0, false,
        },

        {
            {{5, 6, 7}, 3, 0, {2, 3, 4}, 2},
            2 * sizeof(uint32_t), true,
        },
        {
            {{2, 9, 4}, 11, 0, {1, 3, 9}, 0}, /* Empty indirect count EI */
            2 * sizeof(uint32_t), true,
        },
    };

    struct {
        union d3d12_root_signature_subobject root_signature;
        union d3d12_shader_bytecode_subobject ms;
        union d3d12_shader_bytecode_subobject ps;
        union d3d12_shader_bytecode_subobject as;
        union d3d12_rasterizer_subobject rasterizer;
    } pipeline_desc;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_root_signature = true;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;

    memset(&options7, 0, sizeof(options7));
    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7));
    ok(SUCCEEDED(hr), "OPTIONS7 is not supported by runtime.\n");

    if (options7.MeshShaderTier < D3D12_MESH_SHADER_TIER_1)
    {
        skip("Mesh shaders not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device2, (void **)&device2);
    ok(SUCCEEDED(hr), "Failed to query ID3D12Device2.\n");
    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList6, (void **)&command_list6);
    ok(SUCCEEDED(hr), "Failed to query ID3D12GraphicsCommandList6.\n");

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    memset(root_parameters, 0, sizeof(root_parameters));
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_AMPLIFICATION;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[0].Constants.Num32BitValues = 4;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;

    hr = create_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    memset(&command_signature_desc, 0, sizeof(command_signature_desc));
    command_signature_desc.ByteStride = sizeof(tests[0].data);
    command_signature_desc.NumArgumentDescs = ARRAY_SIZE(indirect_argument_desc);
    command_signature_desc.pArgumentDescs = indirect_argument_desc;

    memset(indirect_argument_desc, 0, sizeof(indirect_argument_desc));
    indirect_argument_desc[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    indirect_argument_desc[0].Constant.Num32BitValuesToSet = 4;
    indirect_argument_desc[0].Constant.RootParameterIndex = 0;
    indirect_argument_desc[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
    indirect_argument_desc[1].UnorderedAccessView.RootParameterIndex = 1;
    indirect_argument_desc[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

    hr = ID3D12Device2_CreateCommandSignature(device2, &command_signature_desc, root_signature, &IID_ID3D12CommandSignature, (void **)&command_signature);
    ok(SUCCEEDED(hr) || hr == E_NOTIMPL, "Failed to create command signature, hr %#x.\n", hr);
    if (FAILED(hr))
        command_signature = NULL;
    if (hr == E_NOTIMPL)
        skip("DGC is likely not implemented. Skipping test.\n");

    pipeline_desc.root_signature.type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
    pipeline_desc.root_signature.root_signature = root_signature;
    pipeline_desc.ms = ms_subobject;
    pipeline_desc.ps = ps_subobject;
    pipeline_desc.as = as_subobject;
    pipeline_desc.rasterizer = rasterizer_subobject;

    hr = create_pipeline_state_from_stream(device2, &pipeline_desc, &pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    output = create_default_buffer(context.device, sizeof(tests) * sizeof(uint32_t), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    {
        struct test_data test_data[128];
        for (i = 0; i < ARRAY_SIZE(tests); i++)
        {
            D3D12_GPU_VIRTUAL_ADDRESS va = ID3D12Resource_GetGPUVirtualAddress(output) + tests[i].uav_offset;
            test_data[i] = tests[i].data;
            memcpy(&test_data[i].va, &va, sizeof(va));
        }
        indirect_buffer = create_upload_buffer(context.device, sizeof(test_data), test_data);
    }

    if (command_signature)
    {
        for (i = 0; i < ARRAY_SIZE(tests); i++)
        {
            ID3D12GraphicsCommandList6_SetGraphicsRootSignature(command_list6, root_signature);
            ID3D12GraphicsCommandList6_SetPipelineState(command_list6, pipeline_state);
            set_viewport(&viewport, 0, 0, 1, 1, 0, 1);
            set_rect(&scissor, 0, 0, 1, 1);
            ID3D12GraphicsCommandList6_RSSetViewports(command_list6, 1, &viewport);
            ID3D12GraphicsCommandList6_RSSetScissorRects(command_list6, 1, &scissor);
            ID3D12GraphicsCommandList6_ExecuteIndirect(command_list6, command_signature,
                tests[i].indirect_count ? 64 : tests[i].data.indirect_count,
                indirect_buffer, sizeof(tests[i].data) * i,
                tests[i].indirect_count ? indirect_buffer : NULL,
                tests[i].indirect_count ? (sizeof(tests[i].data) * i + offsetof(struct test_data, indirect_count)) : 0);
        }
    }

    transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    if (command_signature)
    {
        for (i = 0; i < ARRAY_SIZE(tests); i++)
        {
            uint32_t num_tasks, num_groups_per_task, expected;
            expected = 0;

            for (j = 0; j < tests[i].data.indirect_count; j++)
            {
                num_tasks = tests[i + j].data.tasks[0] * tests[i + j].data.tasks[1] * tests[i + j].data.tasks[2];
                num_groups_per_task = tests[i + j].data.mesh_groups[0] * tests[i + j].data.mesh_groups[1] * tests[i + j].data.mesh_groups[2];
                expected += num_tasks; /* every task increments counter */
                expected += num_tasks * num_groups_per_task; /* every mesh group increments counter. */
                expected += num_tasks * num_groups_per_task * tests[i + j].data.prims; /* Every primitive increments counter. */
            }

            if (tests[i].data.indirect_count)
            {
                ok(get_readback_uint(&rb, tests[i].uav_offset / sizeof(uint32_t), 0, 0) == expected,
                        "Test %u: expected %u, got %u.\n", i, expected, get_readback_uint(&rb, tests[i].uav_offset / sizeof(uint32_t), 0, 0));
            }
            else
            {
                ok(get_readback_uint(&rb, i, 0, 0) == expected, "Test %u: expected %u, got %u.\n",
                        i, expected, get_readback_uint(&rb, i, 0, 0));
            }
        }
    }

    release_resource_readback(&rb);

    ID3D12Resource_Release(output);
    ID3D12PipelineState_Release(pipeline_state);
    if (command_signature)
        ID3D12CommandSignature_Release(command_signature);
    ID3D12Resource_Release(indirect_buffer);
    ID3D12RootSignature_Release(root_signature);
    ID3D12GraphicsCommandList6_Release(command_list6);
    ID3D12Device2_Release(device2);
    destroy_test_context(&context);
}
