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

void test_vrs(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList5 *command_list;
    bool additional_shading_rates_supported;
    ID3D12PipelineState *pipeline_state = NULL;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int i;
    HRESULT hr;

#include "shaders/vrs/headers/vrs.h"

    static const struct
    {
        D3D12_SHADING_RATE shading_rate;
        D3D12_SHADING_RATE_COMBINER combiners[2];
        unsigned int expected_color;
        bool additional_shading_rate;
    }
    tests[] =
    {
        {D3D12_SHADING_RATE_1X1, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000101},
        {D3D12_SHADING_RATE_1X2, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000201},
        {D3D12_SHADING_RATE_2X1, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000102},
        {D3D12_SHADING_RATE_2X2, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000202},
        {D3D12_SHADING_RATE_2X4, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000402, true},
        {D3D12_SHADING_RATE_4X2, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000204, true},
        {D3D12_SHADING_RATE_4X4, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000404, true},
    };

    memset(&desc, 0, sizeof(desc));
    if (!init_test_context(&context, &desc))
        return;

    if (!is_vrs_tier1_supported(context.device, &additional_shading_rates_supported))
    {
        skip("VariableRateShading not supported.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList5, (void **)&command_list);
    ok(hr == S_OK, "Couldn't get GraphicsCommandList5, hr %#x.\n", (int)hr);
    ID3D12GraphicsCommandList5_Release(command_list);

    queue = context.queue;

    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, &vrs_dxbc, NULL);

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", (int)hr);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        if (!additional_shading_rates_supported)
        {
            skip("Skipped test %u, AdditionalShadingRates not supported.\n", i);
            continue;
        }

        ID3D12GraphicsCommandList5_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList5_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList5_SetPipelineState(command_list, pipeline_state);
        ID3D12GraphicsCommandList5_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList5_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList5_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList5_RSSetShadingRate(command_list, tests[i].shading_rate, tests[i].combiners);
        ID3D12GraphicsCommandList5_DrawInstanced(command_list, 3, 1, 0, 0);
        transition_resource_state(context.list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, queue, context.list, tests[i].expected_color, 0);

        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    vkd3d_test_set_context(NULL);

    if (pipeline_state)
        ID3D12PipelineState_Release(pipeline_state);

    destroy_test_context(&context);
}

void test_vrs_dxil(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList5 *command_list;
    ID3D12PipelineState *pipeline_state = NULL;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int i;
    HRESULT hr;

#include "shaders/vrs/headers/vrs_vs.h"
#include "shaders/vrs/headers/vrs_ps.h"

    static const struct
    {
        D3D12_SHADING_RATE shading_rate;
        D3D12_SHADING_RATE_COMBINER combiners[2];
        unsigned int expected_color;
    }
    tests[] =
    {
        {D3D12_SHADING_RATE_1X1, {D3D12_SHADING_RATE_COMBINER_MAX, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000100},
        {D3D12_SHADING_RATE_1X2, {D3D12_SHADING_RATE_COMBINER_MAX, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000100},
        {D3D12_SHADING_RATE_2X1, {D3D12_SHADING_RATE_COMBINER_MAX, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000101},
        {D3D12_SHADING_RATE_2X2, {D3D12_SHADING_RATE_COMBINER_MAX, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000101},
        {D3D12_SHADING_RATE_1X1, {D3D12_SHADING_RATE_COMBINER_MIN, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000000},
        {D3D12_SHADING_RATE_1X2, {D3D12_SHADING_RATE_COMBINER_MIN, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000100},
        {D3D12_SHADING_RATE_2X1, {D3D12_SHADING_RATE_COMBINER_MIN, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000000},
        {D3D12_SHADING_RATE_2X2, {D3D12_SHADING_RATE_COMBINER_MIN, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000100},
        {D3D12_SHADING_RATE_1X1, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000000},
        {D3D12_SHADING_RATE_1X2, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000100},
        {D3D12_SHADING_RATE_2X1, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000001},
        {D3D12_SHADING_RATE_2X2, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000101},
        {D3D12_SHADING_RATE_1X1, {D3D12_SHADING_RATE_COMBINER_OVERRIDE, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000100},
        {D3D12_SHADING_RATE_1X2, {D3D12_SHADING_RATE_COMBINER_OVERRIDE, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000100},
        {D3D12_SHADING_RATE_2X1, {D3D12_SHADING_RATE_COMBINER_OVERRIDE, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000100},
        {D3D12_SHADING_RATE_2X2, {D3D12_SHADING_RATE_COMBINER_OVERRIDE, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000100},
    };

    memset(&desc, 0, sizeof(desc));
    if (!init_test_context(&context, &desc))
        return;

    if (!context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    if (!is_vrs_tier2_supported(context.device))
    {
        skip("VariableRateShading TIER_2 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList5, (void **)&command_list);
    ok(hr == S_OK, "Couldn't get GraphicsCommandList5, hr %#x.\n", (int)hr);
    ID3D12GraphicsCommandList5_Release(command_list);

    queue = context.queue;

    init_pipeline_state_desc_dxil(&pso_desc, context.root_signature,
            context.render_target_desc.Format, &vrs_vs_dxil, &vrs_ps_dxil, NULL);

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", (int)hr);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        ID3D12GraphicsCommandList5_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList5_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList5_SetPipelineState(command_list, pipeline_state);
        ID3D12GraphicsCommandList5_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList5_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList5_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList5_RSSetShadingRate(command_list, tests[i].shading_rate, tests[i].combiners);
        ID3D12GraphicsCommandList5_DrawInstanced(command_list, 3, 1, 0, 0);
        transition_resource_state(context.list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, queue, context.list, tests[i].expected_color, 0);

        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    vkd3d_test_set_context(NULL);

    if (pipeline_state)
        ID3D12PipelineState_Release(pipeline_state);

    destroy_test_context(&context);
}

void test_vrs_image(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12PipelineState *pipeline_state = NULL;
    ID3D12GraphicsCommandList5 *command_list;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int i;
    HRESULT hr;

#include "shaders/vrs/headers/vrs_image.h"

    static const struct
    {
        D3D12_SHADING_RATE base_shading_rate;
        D3D12_SHADING_RATE tex_shading_rate;
        D3D12_SHADING_RATE_COMBINER combiners[2];
        unsigned int expected_color;
    }
    tests[] =
    {
        {D3D12_SHADING_RATE_1X1, D3D12_SHADING_RATE_1X2, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_OVERRIDE}, 0x00000201},
        {D3D12_SHADING_RATE_1X2, D3D12_SHADING_RATE_2X1, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_OVERRIDE}, 0x00000102},
        {D3D12_SHADING_RATE_2X1, D3D12_SHADING_RATE_2X2, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_OVERRIDE}, 0x00000202},
        {D3D12_SHADING_RATE_2X2, D3D12_SHADING_RATE_1X1, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_OVERRIDE}, 0x00000101},
        {D3D12_SHADING_RATE_1X1, D3D12_SHADING_RATE_1X2, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000101},
        {D3D12_SHADING_RATE_1X2, D3D12_SHADING_RATE_2X1, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000201},
        {D3D12_SHADING_RATE_2X1, D3D12_SHADING_RATE_2X2, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000102},
        {D3D12_SHADING_RATE_2X2, D3D12_SHADING_RATE_1X1, {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH}, 0x00000202},
    };

    memset(&desc, 0, sizeof(desc));
    if (!init_test_context(&context, &desc))
        return;

    if (!is_vrs_tier2_supported(context.device))
    {
        skip("VariableRateShading TIER_2 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList5, (void **)&command_list);
    ok(hr == S_OK, "Couldn't get GraphicsCommandList5, hr %#x.\n", (int)hr);
    ID3D12GraphicsCommandList5_Release(command_list);

    queue = context.queue;

    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, &vrs_image_dxbc, NULL);

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", (int)hr);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        #define TEX_WIDTH (4u)
        #define TEX_HEIGHT (4u)

        ID3D12Resource *texture;
        uint8_t shading_rate_data[TEX_WIDTH * TEX_HEIGHT] =
        {
            tests[i].tex_shading_rate, tests[i].tex_shading_rate, tests[i].tex_shading_rate, tests[i].tex_shading_rate,
            tests[i].tex_shading_rate, tests[i].tex_shading_rate, tests[i].tex_shading_rate, tests[i].tex_shading_rate,
            tests[i].tex_shading_rate, tests[i].tex_shading_rate, tests[i].tex_shading_rate, tests[i].tex_shading_rate,
            tests[i].tex_shading_rate, tests[i].tex_shading_rate, tests[i].tex_shading_rate, tests[i].tex_shading_rate,
        };
        D3D12_SUBRESOURCE_DATA tex_data = { shading_rate_data, TEX_WIDTH, TEX_WIDTH * TEX_HEIGHT };

        vkd3d_test_set_context("Test %u", i);

        /* Docs say RTV usage is not allowed, yet it works, and D3D12 layers don't complain.
         * Simultaneous access is also not checked, but we'll only consider doing that when we see this behavior in the wild.
         * Dead Space (2023) hits this scenario. */
        texture = create_default_texture2d(context.device, 4, 4, 1, 1, DXGI_FORMAT_R8_UINT,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
        upload_texture_data(texture, &tex_data, 1, queue, context.list);
        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, texture,
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE);

        ID3D12GraphicsCommandList5_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList5_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList5_SetPipelineState(command_list, pipeline_state);
        ID3D12GraphicsCommandList5_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList5_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList5_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList5_RSSetShadingRate(command_list, tests[i].base_shading_rate, tests[i].combiners);
        ID3D12GraphicsCommandList5_RSSetShadingRateImage(command_list, NULL);
        ID3D12GraphicsCommandList5_RSSetShadingRateImage(command_list, texture);
        ID3D12GraphicsCommandList5_DrawInstanced(command_list, 3, 1, 0, 0);
        transition_resource_state(context.list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, queue, context.list, tests[i].expected_color, 0);

        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12Resource_Release(texture);

        #undef TEX_WIDTH
        #undef TEX_HEIGHT
    }
    vkd3d_test_set_context(NULL);

    if (pipeline_state)
        ID3D12PipelineState_Release(pipeline_state);

    destroy_test_context(&context);
}

void test_vrs_depth_write(bool use_dxil)
{
    static const D3D12_SHADING_RATE_COMBINER combiners[2] =
    {
        D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
        D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList5 *command_list;
    struct depth_stencil_resource ds;
    struct resource_readback rb_color;
    struct resource_readback rb_depth;
    struct test_context_desc desc;
    const float black[4] = { 0 };
    struct test_context context;
    ID3D12PipelineState *pso[2];
    uint32_t i, x, y;
    HRESULT hr;

#include "shaders/vrs/headers/vrs_depth_vs.h"
#include "shaders/vrs/headers/vrs_depth_ps.h"
#include "shaders/vrs/headers/vrs_depth_ps_fixed.h"

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.rt_width = 64;
    desc.rt_height = 2;
    desc.rt_format = DXGI_FORMAT_R32_UINT;
    if (!init_test_context(&context, &desc))
        return;

    if (!is_vrs_tier1_supported(context.device, NULL))
    {
        skip("VariableRateShading TIER_1 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    init_depth_stencil(&ds, context.device, 64, 2, 1, 1, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, NULL);

    ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList5, (void **)&command_list);

    init_pipeline_state_desc(&pso_desc, context.root_signature,
        context.render_target_desc.Format,
        use_dxil ? &vrs_depth_vs_dxil : &vrs_depth_vs_dxbc,
        use_dxil ? &vrs_depth_ps_dxil : &vrs_depth_ps_dxbc,
        NULL);
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.DepthStencilState.DepthEnable = TRUE;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso[0]);
    ok(SUCCEEDED(hr), "Failed to create pipeline, hr #%x.\n", (int)hr);

    pso_desc.PS = use_dxil ? vrs_depth_ps_fixed_dxil : vrs_depth_ps_fixed_dxbc;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso[1]);
    ok(SUCCEEDED(hr), "Failed to create pipeline, hr #%x.\n", (int)hr);

    for (i = 0; i < ARRAY_SIZE(pso); i++)
    {
        vkd3d_test_set_context("Test %u", i);
        ID3D12GraphicsCommandList5_ClearDepthStencilView(command_list, ds.dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, NULL);
        ID3D12GraphicsCommandList5_ClearRenderTargetView(command_list, context.rtv, black, 0, NULL);
        ID3D12GraphicsCommandList5_OMSetRenderTargets(command_list, 1, &context.rtv, false, &ds.dsv_handle);
        ID3D12GraphicsCommandList5_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList5_SetPipelineState(command_list, pso[i]);
        ID3D12GraphicsCommandList5_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList5_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList5_RSSetScissorRects(command_list, 1, &context.scissor_rect);

        ID3D12GraphicsCommandList5_RSSetShadingRate(command_list, D3D12_SHADING_RATE_2X2, combiners);
        ID3D12GraphicsCommandList5_DrawInstanced(command_list, 3, 1, 0, 0);

        transition_resource_state(context.list, ds.texture, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_texture_readback_with_command_list(context.render_target, 0, &rb_color, context.queue, context.list);
        reset_command_list(context.list, context.allocator);
        get_texture_readback_with_command_list(ds.texture, 0, &rb_depth, context.queue, context.list);
        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, ds.texture, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        for (y = 0; y < 2; y++)
        {
            for (x = 0; x < 64; x++)
            {
                uint32_t expected_color;
                float expected_depth;
                uint32_t color;
                float depth;
                bool is_vrs;

                color = get_readback_uint(&rb_color, x, y, 0);
                depth = get_readback_float(&rb_depth, x, y);
                is_vrs = i == 1;

                expected_depth = ((float)x + 0.5f) / 64.0f;

                /* Writing to SV_Depth forces 1x1 rate, even when hardware would support that in Vulkan.
                 * However, this only seems to happen on NV. Both AMD and WARP are bugged in different ways. */
                /* https://microsoft.github.io/DirectX-Specs/d3d/VariableRateShading.html#export-of-depth-and-stencil specifies 1x1 rate. */
                if (is_vrs)
                    expected_color = (uint32_t)(((float)(x / 2) + 0.5f) * 64.0f / 32.0f);
                else
                    expected_color = x;

                ok(compare_float(expected_depth, depth, 1), "%u, %u: Expected %f, got %f\n", x, y, expected_depth, depth);
                ok(color == expected_color, "%u, %u: Expected %u, got %u\n", x, y, expected_color, color);
            }
        }

        release_resource_readback(&rb_color);
        release_resource_readback(&rb_depth);
    }
    vkd3d_test_set_context(NULL);

    for (i = 0; i < ARRAY_SIZE(pso); i++)
        ID3D12PipelineState_Release(pso[i]);
    ID3D12GraphicsCommandList5_Release(command_list);
    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_vrs_depth_write_dxbc(void)
{
    test_vrs_depth_write(false);
}

void test_vrs_depth_write_dxil(void)
{
    test_vrs_depth_write(true);
}

void test_vrs_clip_distance(void)
{
    static const D3D12_SHADING_RATE_COMBINER combiners[2] =
    {
        D3D12_SHADING_RATE_COMBINER_OVERRIDE,
        D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList5 *command_list;
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    const float black[4] = { 0 };
    struct resource_readback rb;
    struct test_context context;
    float root_constant_data[3];
    uint32_t x, y;
    HRESULT hr;

#include "shaders/vrs/headers/vrs_clip_distance_vs.h"
#include "shaders/vrs/headers/vrs_clip_distance_ps.h"

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    desc.rt_width = 4;
    desc.rt_height = 4;
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    if (!init_test_context(&context, &desc))
        return;

    if (!is_vrs_tier1_supported(context.device, NULL))
    {
        skip("VariableRateShading TIER_1 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    init_depth_stencil(&ds, context.device, 4, 4, 1, 1, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, NULL);

    ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList5, (void **)&command_list);

    context.root_signature = create_32bit_constants_root_signature(context.device, 0, 3, D3D12_SHADER_VISIBILITY_VERTEX);

    init_pipeline_state_desc(&pso_desc, context.root_signature,
        context.render_target_desc.Format, &vrs_clip_distance_vs_dxil, &vrs_clip_distance_ps_dxil, NULL);
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.DepthStencilState.DepthEnable = TRUE;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create pipeline, hr #%x.\n", (int)hr);

    ID3D12GraphicsCommandList5_ClearDepthStencilView(command_list, ds.dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, NULL);
    ID3D12GraphicsCommandList5_ClearRenderTargetView(command_list, context.rtv, black, 0, NULL);
    ID3D12GraphicsCommandList5_OMSetRenderTargets(command_list, 1, &context.rtv, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList5_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList5_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList5_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList5_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList5_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    root_constant_data[0] = 1.0f;
    root_constant_data[1] = 1.0f;
    root_constant_data[2] = -1.0f / 16.0f;

    ID3D12GraphicsCommandList5_SetGraphicsRoot32BitConstants(command_list, 0, 3, root_constant_data, 0);

    ID3D12GraphicsCommandList5_RSSetShadingRate(command_list, D3D12_SHADING_RATE_1X1, combiners);
    ID3D12GraphicsCommandList5_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, context.queue, context.list);
    reset_command_list(context.list, context.allocator);

    for (y = 0; y < 4; y++)
    {
        for (x = 0; x < 4; x++)
        {
            const struct vec4 *value = get_readback_vec4(&rb, x, y);
            struct vec4 expected = {0};
            float clip_x, clip_y;
            float clip_distance;

            /* Coverage is evaluated at full rate for clip distance. */
            clip_x = ((float)x + 0.5f) / 2.0f - 1.0f;
            clip_y = ((float)y + 0.5f) / 2.0f - 1.0f;
            clip_distance = root_constant_data[2] + root_constant_data[0] * clip_x + root_constant_data[1] * clip_y;

            if (clip_distance >= 0.0f)
            {
                clip_x = (float)(x | 1) / 2.0f - 1.0f;
                clip_y = (float)(y | 1) / 2.0f - 1.0f;

                /* The varying is evaluated at coarse rate. We can evaluate a negative clip distance here. */
                clip_distance = root_constant_data[2] + root_constant_data[0] * clip_x + root_constant_data[1] * clip_y;

                expected.x = clip_distance;
                expected.y = (float)(x | 1);
                expected.z = (float)(y | 1);
                expected.w = expected.x;
            }

            ok(compare_vec4(value, &expected, 256), "%u, %u: Expected (%f, %f, %f, %f), got (%f, %f, %f, %f)\n", x, y,
                     expected.x, expected.y, expected.z, expected.w,
                     value->x, value->y, value->z, value->w);
        }
    }

    release_resource_readback(&rb);

    ID3D12GraphicsCommandList5_Release(command_list);
    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

