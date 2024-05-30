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
    ok(hr == S_OK, "Couldn't get GraphicsCommandList5, hr %#x.\n", hr);
    ID3D12GraphicsCommandList5_Release(command_list);

    queue = context.queue;

    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, &vrs_dxbc, NULL);

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);

        if (!additional_shading_rates_supported)
        {
            skip("Skipped test %u, AdditionalShadingRates not supported.\n");
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
    ok(hr == S_OK, "Couldn't get GraphicsCommandList5, hr %#x.\n", hr);
    ID3D12GraphicsCommandList5_Release(command_list);

    queue = context.queue;

    init_pipeline_state_desc_dxil(&pso_desc, context.root_signature,
            context.render_target_desc.Format, &vrs_vs_dxil, &vrs_ps_dxil, NULL);

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

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
    ok(hr == S_OK, "Couldn't get GraphicsCommandList5, hr %#x.\n", hr);
    ID3D12GraphicsCommandList5_Release(command_list);

    queue = context.queue;

    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, &vrs_image_dxbc, NULL);

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

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

