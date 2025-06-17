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

void test_srgb_unorm_mismatch_usage_aliasing(void)
{
    D3D12_RESOURCE_ALLOCATION_INFO unorm_alloc_info;
    D3D12_RESOURCE_ALLOCATION_INFO srgb_alloc_info;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    static float white[] = { 1, 1, 1, 1 };
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    D3D12_HEAP_DESC heap_desc;
    ID3D12Resource *unorm;
    ID3D12Resource *srgb;
    D3D12_VIEWPORT vp;
    unsigned int x, y;
    ID3D12Heap *heap;
    D3D12_RECT sci;
    HRESULT hr;

#include "shaders/render_target/headers/srgb_unorm_mismatch_vs.h"
#include "shaders/render_target/headers/srgb_unorm_mismatch_ps.h"

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;

    if (!init_test_context(&context, &desc))
        return;

    memset(&rs_desc, 0, sizeof(rs_desc));
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    init_pipeline_state_desc(&pso_desc, context.root_signature, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            &srgb_unorm_mismatch_vs_dxbc, &srgb_unorm_mismatch_ps_dxbc, NULL);
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create PSO, hr #%x\n", hr);

    /* Triages an AC: Shadows bug where these resources are aliased on top of a heap and game expects this to "just werk". */

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Width = 264;
    resource_desc.Height = 264;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.MipLevels = 1;
    resource_desc.DepthOrArraySize = 1;

    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    unorm_alloc_info = ID3D12Device_GetResourceAllocationInfo(context.device, 0, 1, &resource_desc);

    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    srgb_alloc_info = ID3D12Device_GetResourceAllocationInfo(context.device, 0, 1, &resource_desc);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Flags = D3D12_HEAP_FLAG_CREATE_NOT_ZEROED | D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.SizeInBytes = 64 * 1024 + max(unorm_alloc_info.SizeInBytes, srgb_alloc_info.SizeInBytes);
    hr = ID3D12Device_CreateHeap(context.device, &heap_desc, &IID_ID3D12Heap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create heap, hr #%x\n", hr);

    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    hr = ID3D12Device_CreatePlacedResource(context.device, heap, 64 * 1024, &resource_desc, D3D12_RESOURCE_STATE_COPY_SOURCE, NULL, &IID_ID3D12Resource, (void **)&unorm);
    ok(SUCCEEDED(hr), "Failed to create placed resource, hr #%x\n", hr);

    /* AC: Shadows does not add this UAV flag, but D3D12 has requirements about aliasing if all things match,
     * and we should be using VK_IMAGE_CREATE_ALIAS_BIT for all placed resoures, which makes this test pass on RDNA4.
     * This should work as expected on all GPUs more or less ... */
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    hr = ID3D12Device_CreatePlacedResource(context.device, heap, 64 * 1024, &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&srgb);
    ok(SUCCEEDED(hr), "Failed to create placed resource, hr #%x\n", hr);

    context.rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    context.rtv = get_cpu_rtv_handle(&context, context.rtv_heap, 0);
    ID3D12Device_CreateRenderTargetView(context.device, srgb, NULL, context.rtv);

    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    set_viewport(&vp, 0, 0, resource_desc.Width, resource_desc.Height, 0, 1);
    set_rect(&sci, 0, 0, resource_desc.Width, resource_desc.Height);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

    transition_resource_state(context.list, srgb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    /* Massive UB. This will only work if the layout of the image matches. */
    vkd3d_mute_validation_message("09600", "Image layout tracking does not work across VK_IMAGE_CREATE_ALIAS_BIT.");
    get_texture_readback_with_command_list(unorm, 0, &rb, context.queue, context.list);
    vkd3d_unmute_validation_message("09600");

    for (y = 0; y < resource_desc.Height; y++)
    {
        for (x = 0; x < resource_desc.Width; x++)
        {
            uint32_t readback, readback_x, readback_y;
            readback = get_readback_uint(&rb, x, y, 0);
            readback_x = (readback >> 0) & 0xff;
            readback_y = (readback >> 8) & 0xff;

            ok(readback_x == min(x, 255) && readback_y == min(y, 255), "%u, %u: Expected %u, %u, got %u, %u\n", x, y, min(x, 255), min(y, 255), readback_x, readback_y);
        }
    }

    release_resource_readback(&rb);
    ID3D12Heap_Release(heap);
    ID3D12Resource_Release(srgb);
    ID3D12Resource_Release(unorm);
    destroy_test_context(&context);
}

void test_unbound_rtv_rendering(void)
{
    static const struct vec4 red = { 1.0f, 0.0f, 0.0f, 1.0f };
    static const float white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE rt_handle;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *fp32_rt;
    HRESULT hr;

#include "shaders/render_target/headers/ps_unbound_rtv.h"

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.rt_width = 32;
    desc.rt_height = 32;
    desc.rt_descriptor_count = 2;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    fp32_rt = create_default_texture2d(context.device, 32, 32,
            1, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

    rt_handle = context.rtv;
    rt_handle.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    ID3D12Device_CreateRenderTargetView(context.device, fp32_rt, NULL, rt_handle);

    /* Apparently, rendering to an NULL RTV is fine. D3D12 validation does not complain about this case at all. */
    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, &ps_unbound_rtv_dxbc, NULL);
    pso_desc.NumRenderTargets = 2;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    pso_desc.RTVFormats[1] = DXGI_FORMAT_R32_FLOAT;
    pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0xf;
    pso_desc.BlendState.RenderTarget[1].RenderTargetWriteMask = 0xf;
    pso_desc.DepthStencilState.DepthEnable = false;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rt_handle, white, 0, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.5f, 0.5f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    /* First, render to both RTs, but then only render to 1 RT. */
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 2, &context.rtv, true, NULL);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, fp32_rt,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &red, 0);
    reset_command_list(command_list, context.allocator);
    check_sub_resource_float(fp32_rt, 0, queue, command_list, 0.5f, 0);
    ID3D12Resource_Release(fp32_rt);
    destroy_test_context(&context);
}

void test_unknown_rtv_format(void)
{
    static const struct vec4 vec4_white = {1.0f, 1.0f, 1.0f, 1.0f};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    struct vec4 expected_vec4 = {0.0f, 0.0f, 0.0f, 1.0f};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3];
    ID3D12Resource *render_targets[2];
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int i;
    HRESULT hr;

#include "shaders/render_target/headers/ps_unknown_rtv.h"

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.rt_descriptor_count = 16;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    init_depth_stencil(&ds, context.device, 32, 32, 1, 1, DXGI_FORMAT_D32_FLOAT, 0, NULL);

    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, &ps_unknown_rtv_dxbc, NULL);
    pso_desc.NumRenderTargets = ARRAY_SIZE(rtvs);
    for (i = 0; i < ARRAY_SIZE(rtvs); ++i)
        pso_desc.RTVFormats[i] = desc.rt_format;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    rtvs[0] = get_cpu_rtv_handle(&context, context.rtv_heap, 0);
    rtvs[1] = get_cpu_rtv_handle(&context, context.rtv_heap, 1);
    rtvs[2] = get_cpu_rtv_handle(&context, context.rtv_heap, 2);
    create_render_target(&context, &desc, &render_targets[0], &rtvs[1]);
    create_render_target(&context, &desc, &render_targets[1], &rtvs[2]);

    for (i = 0; i < ARRAY_SIZE(rtvs); ++i)
        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtvs[i], white, 0, NULL);

    /* NULL RTV */
    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    rtv_desc.Texture2D.MipSlice = 0;
    rtv_desc.Texture2D.PlaneSlice = 0;
    ID3D12Device_CreateRenderTargetView(context.device, NULL, &rtv_desc,
            get_cpu_rtv_handle(&context, context.rtv_heap, 0));

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, ARRAY_SIZE(rtvs), rtvs, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.5f, 0.5f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, render_targets[0],
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, render_targets[1],
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &vec4_white, 0);
    reset_command_list(command_list, context.allocator);
    expected_vec4.x = 2.0f;
    check_sub_resource_vec4(render_targets[0], 0, queue, command_list, &expected_vec4, 0);
    reset_command_list(command_list, context.allocator);
    expected_vec4.x = 3.0f;
    check_sub_resource_vec4(render_targets[1], 0, queue, command_list, &expected_vec4, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(ds.texture, 0, queue, command_list, 0.5f, 1);

    for (i = 0; i < ARRAY_SIZE(render_targets); ++i)
        ID3D12Resource_Release(render_targets[i]);
    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_unknown_dsv_format(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    struct depth_stencil_resource ds;
    D3D12_CLEAR_VALUE clear_value;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

#include "shaders/render_target/headers/ps_color.h"

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const struct vec4 green = {0.0f, 1.0f, 0.0f, 1.0f};
    static const struct vec4 red = {1.0f, 0.0f, 0.0f, 1.0f};

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    clear_value.Format = DXGI_FORMAT_D32_FLOAT;
    clear_value.DepthStencil.Depth = 0.5f;
    clear_value.DepthStencil.Stencil = 0;
    init_depth_stencil(&ds, context.device, 32, 32, 1, 1, DXGI_FORMAT_D32_FLOAT, 0, &clear_value);

    context.root_signature = create_32bit_constants_root_signature(context.device,
            0, 4, D3D12_SHADER_VISIBILITY_PIXEL);

    /* DSVFormat = DXGI_FORMAT_UNKNOWN and D3D12_DEPTH_WRITE_MASK_ZERO */
    init_pipeline_state_desc(&pso_desc, context.root_signature, desc.rt_format, NULL, &ps_color_dxbc, NULL);
    pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &green.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.5f, 0.5f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &red.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 1.0f, 1.0f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 0.0f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.55f, 0.55f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(ds.texture, 0, queue, command_list, 0.5f, 1);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &green, 0);

    /* DSVFormat = DXGI_FORMAT_UNKNOWN and no DSV */
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &red.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 0.0f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &green.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.5f, 0.5f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &green, 0);

    /* DSVFormat = DXGI_FORMAT_UNKNOWN and D3D12_COMPARISON_FUNC_ALWAYS */
    ID3D12PipelineState_Release(context.pipeline_state);
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &red.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.0f, 0.0f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &green.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.6f, 0.6f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &green, 0);

    /* DSVFormat = DXGI_FORMAT_UNKNOWN and depth write */
    ID3D12PipelineState_Release(context.pipeline_state);
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, NULL);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &red.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 1.0f, 1.0f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &green.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.6f, 0.6f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &green, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, ds.texture,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_float(ds.texture, 0, queue, command_list, 1.0f, 1);

    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_depth_stencil_test_no_dsv(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

#include "shaders/render_target/headers/ps_color.h"

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const struct vec4 red = {1.0f, 0.0f, 0.0f, 1.0f};
    static const struct vec4 green = { 0.0f, 1.0f, 0.0f, 1.0f };
    static const struct vec4 blue = { 0.0f, 0.0f, 1.0f, 1.0f };

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.no_root_signature = true;
    desc.rt_width = 32;
    desc.rt_height = 32;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    init_depth_stencil(&ds, context.device, 32, 32, 1, 1, DXGI_FORMAT_D32_FLOAT, 0, NULL);

    context.root_signature = create_32bit_constants_root_signature(context.device,
              0, 4, D3D12_SHADER_VISIBILITY_PIXEL);

    init_pipeline_state_desc(&pso_desc, context.root_signature, desc.rt_format, NULL, &ps_color_dxbc, NULL);
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle, D3D12_CLEAR_FLAG_DEPTH,
            1.0f, 0, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, &ds.dsv_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &green.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.5f, 0.5f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &red.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.9f, 0.9f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    /* Now, dynamically disable the depth attachment. */
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);

    /* Native behavior seems to be that depth test is just disabled entirely here.
     * This last draw is the color we should get on NV at least.
     * D3D12 validation layers report errors here of course,
     * but Metro Exodus relies on depth testing on DSV NULL apparently. */
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 0, 4, &blue.x, 0);
    set_viewport(&context.viewport, 0.0f, 0.0f, 32.0f, 32.0f, 0.55f, 0.55f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
              D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    /* vkd3d-proton just skips the draw call in this situation.
     * At least test that we don't crash. */
    check_sub_resource_vec4(context.render_target, 0, queue, command_list, &blue, 0);

    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_render_a8_dxbc(void)
{
    static const float black[] = {0.0f, 0.0f, 0.0f, 0.0f};
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;

#include "shaders/render_target/headers/ps_render_a8.h"

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_A8_UNORM;
    desc.ps = &ps_render_a8_dxbc;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, black, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint8(context.render_target, 0, queue, command_list, 0xff, 0);

    destroy_test_context(&context);
}

void test_render_a8_dxil(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    static const float black[] = {0.0f, 0.0f, 0.0f, 0.0f};
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    HRESULT hr;

#include "shaders/render_target/headers/ps_render_a8.h"

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_A8_UNORM;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;

    if (!context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    device = context.device;
    command_list = context.list;
    queue = context.queue;

    init_pipeline_state_desc_dxil(&pso_desc, context.root_signature, 0, NULL, NULL, NULL);
    pso_desc.RTVFormats[0] = DXGI_FORMAT_A8_UNORM;
    pso_desc.NumRenderTargets = 1;
    pso_desc.PS = ps_render_a8_dxil;

    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, black, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint8(context.render_target, 0, queue, command_list, 0xff, 0);

    destroy_test_context(&context);
}

void test_multisample_rendering(void)
{
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList *command_list;
    ID3D12PipelineState *ms_pipeline_state;
    D3D12_CPU_DESCRIPTOR_HANDLE ms_rtv;
    ID3D12Resource *ms_render_target;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    uint32_t sample;
    unsigned int i;
    HRESULT hr;

#include "shaders/render_target/headers/ps_multisample_color.h"
#include "shaders/render_target/headers/ps_multisample_resolve.h"

    static const unsigned int expected_colors[] = {0xff0000ff, 0xff00ff00, 0xffff0000, 0xff000000};

    if (use_warp_device)
    {
        skip("Sample shading tests fail on WARP.\n");
        return;
    }

    memset(&desc, 0, sizeof(desc));
    desc.rt_width = desc.rt_height = 32;
    desc.rt_descriptor_count = 2;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    context.root_signature = create_texture_root_signature(context.device,
            D3D12_SHADER_VISIBILITY_PIXEL, 2, 0);

    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, &ps_multisample_resolve_dxbc, NULL);
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    pso_desc.PS = ps_multisample_color_dxbc;
    pso_desc.SampleDesc.Count = 4;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&ms_pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);

    ms_rtv = get_cpu_rtv_handle(&context, context.rtv_heap, 1);
    desc.sample_desc.Count = 4;
    create_render_target(&context, &desc, &ms_render_target, &ms_rtv);

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    ID3D12Device_CreateShaderResourceView(context.device, ms_render_target, NULL,
            get_cpu_descriptor_handle(&context, heap, 0));

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, ms_rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &ms_rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, ms_pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            get_gpu_descriptor_handle(&context, heap, 0));
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, ms_render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    ID3D12GraphicsCommandList_ResolveSubresource(command_list,
            context.render_target, 0, ms_render_target, 0, context.render_target_desc.Format);
    transition_resource_state(command_list, ms_render_target,
            D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff404040, 2);

    for (i = 0; i < ARRAY_SIZE(expected_colors); ++i)
    {
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
                get_gpu_descriptor_handle(&context, heap, 0));
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 1, 1, &desc.rt_width, 1);

        sample = i;
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 1, 1, &sample, 0);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, queue, command_list, expected_colors[i], 0);
    }

    /* Multisampled PSO on single-sampled RT. Native drivers are
     * robust here and only render sample 0. */
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, ms_pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            get_gpu_descriptor_handle(&context, heap, 0));
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, expected_colors[0], 0);

    ID3D12PipelineState_Release(ms_pipeline_state);

    /* Single-sampled PSO on multisampled RT. Behaviour is not well
     * defined here, but happens to work on native. */
    pso_desc.SampleDesc.Count = 1;
    vkd3d_set_out_of_spec_test_behavior(VKD3D_DEBUG_CONTROL_OUT_OF_SPEC_BEHAVIOR_SAMPLE_COUNT_MISMATCH, TRUE);
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&ms_pipeline_state);
    ok(hr == S_OK, "Failed to create pipeline, hr %#x.\n", hr);
    vkd3d_set_out_of_spec_test_behavior(VKD3D_DEBUG_CONTROL_OUT_OF_SPEC_BEHAVIOR_SAMPLE_COUNT_MISMATCH, FALSE);

    reset_command_list(command_list, context.allocator);

    transition_resource_state(command_list, ms_render_target,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, ms_rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &ms_rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, ms_pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, ms_render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    for (i = 0; i < ARRAY_SIZE(expected_colors); ++i)
    {
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
                get_gpu_descriptor_handle(&context, heap, 0));
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 1, 1, &desc.rt_width, 1);

        sample = i;
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 1, 1, &sample, 0);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        hr = ID3D12GraphicsCommandList_Close(command_list);
        ok(hr == S_OK, "Failed to close command list, hr %#x.\n", hr);

        exec_command_list(queue, command_list);
        wait_queue_idle(context.device, queue);

        reset_command_list(command_list, context.allocator);
    }

    ID3D12DescriptorHeap_Release(heap);
    ID3D12Resource_Release(ms_render_target);
    ID3D12PipelineState_Release(ms_pipeline_state);
    destroy_test_context(&context);
}

void test_rendering_no_attachments_layers(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS options;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER root_param;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    unsigned int layer, x, y;
    ID3D12Resource *buffer;
    const struct vec4 *val;
    D3D12_VIEWPORT vp;
    D3D12_RECT sci;
    HRESULT hr;

#include "shaders/render_target/headers/ps_no_attachments.h"
#include "shaders/render_target/headers/vs_no_attachments.h"

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) ||
            !options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation)
    {
        skip("Cannot render layers from VS. Skipping.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&root_param, 0, sizeof(root_param));
    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &root_param;
    root_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    buffer = create_default_buffer(context.device, 64 * 1024,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    init_pipeline_state_desc(&pso_desc, context.root_signature, DXGI_FORMAT_UNKNOWN,
            &vs_no_attachments_dxbc, &ps_no_attachments_dxbc, NULL);
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void**)&context.pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create PSO, hr #%x.\n", hr);

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 0, NULL, FALSE, NULL);

    set_viewport(&vp, 10000.0f, 12000.0f, 4.0f, 4.0f, 0.0f, 1.0f);
    set_rect(&sci, 10000, 12000, 10004, 12004);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(buffer));
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 16, 0, 0);

    transition_resource_state(context.list, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(buffer, DXGI_FORMAT_R32G32B32A32_FLOAT, &rb, context.queue, context.list);

    for (layer = 0; layer < 16; layer++)
    {
        for (y = 0; y < 4; y++)
        {
            for (x = 0; x < 4; x++)
            {
                val = get_readback_vec4(&rb, layer * 16 + y * 4 + x, 0);
                ok(val->x == 10000.5f + (float)x, "%u, %u, %u: Unexpected value (x) = %f.\n", x, y, layer, val->x);
                ok(val->y == 12000.5f + (float)y, "%u, %u, %u: Unexpected value (y) = %f.\n", x, y, layer, val->y);
                ok(val->z == (float)layer, "%u, %u, %u: Unexpected value (z) = %f.\n", x, y, layer, val->z);
                ok(val->w == 0.0f, "%u, %u, %u: Unexpected value (w) = %f.\n", x, y, layer, val->w);
            }
        }
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(buffer);
    destroy_test_context(&context);
}

void test_renderpass_validation(void)
{
    D3D12_RENDER_PASS_RENDER_TARGET_DESC rtv_infos[2];
    D3D12_FEATURE_DATA_D3D12_OPTIONS18 options18;
    ID3D12GraphicsCommandList4 *command_list4;
    D3D12_HEAP_PROPERTIES heap_properties;
    ID3D12CommandAllocator *allocator;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12DescriptorHeap *rtv_heap;
    ID3D12Resource *rt[4], *ds;
    ID3D12Device* device;
    unsigned int i;
    HRESULT hr;

    static const FLOAT black[] = { 0.0f, 0.0f, 0.0f, 0.0f };

    if (!(device = create_device()))
        return;

    memset(&options18, 0, sizeof(options18));
    ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS18, &options18, sizeof(options18));

    if (!options18.RenderPassesValid)
    {
        skip("Render passes not supported.\n");
        ID3D12Device_Release(device);
        return;
    }

    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)&allocator);
    ok(hr == S_OK, "Failed to create command allocator, hr %#x.\n", hr);

    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);

    if (FAILED(hr))
    {
        skip("ID3D12GraphicsCommandList4 not supported.\n");
        ID3D12Device_Release(device);
        return;
    }

    rtv_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, ARRAY_SIZE(rt));

    /* Create dummy resources to bind as render targets */
    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.Width = 4;
    resource_desc.Height = 4;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, 0, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&rt[0]);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    resource_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, 0, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&rt[1]);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    resource_desc.Width = 16;
    resource_desc.Height = 16;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, 0, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&rt[2]);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    resource_desc.SampleDesc.Count = 4;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, 0, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&rt[3]);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    resource_desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_properties, 0, &resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, NULL, &IID_ID3D12Resource, (void**)&ds);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(rt); i++)
        ID3D12Device_CreateRenderTargetView(device, rt[i], NULL, get_cpu_handle(device, rtv_heap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, i));

    /* Test whether beginning a render pass with no attachments is allowed */
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 0, NULL, NULL, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(SUCCEEDED(hr), "Got hr %#x, expected S_OK.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    /* Test that binding a simple RTV works */
    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);

    memset(rtv_infos, 0, sizeof(rtv_infos));

    for (i = 0; i < ARRAY_SIZE(rtv_infos); i++)
    {
        rtv_infos[i].cpuDescriptor = get_cpu_handle(device, rtv_heap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, i);
        rtv_infos[i].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
        rtv_infos[i].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    }

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(SUCCEEDED(hr), "Got hr %#x, expected S_OK.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    /* Test that not matching BeginRenderPass and EndRenderPass fails */
    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, 0);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(FAILED(hr), "Got hr %#x, expected E_FAIL.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(FAILED(hr), "Got hr %#x, expected E_INVALIDARG.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(FAILED(hr), "Got hr %#x, expected E_INVALIDARG.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(FAILED(hr), "Got hr %#x, expected E_INVALIDARG.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    /* Test consecutive render pass validation with suspend/resume flags */
    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS | D3D12_RENDER_PASS_FLAG_RESUMING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    /* Changing the render target order is explicitly allowed */
    for (i = 0; i < 2; i++)
        rtv_infos[i].cpuDescriptor = get_cpu_handle(device, rtv_heap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1 - i);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_RESUMING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    /* This is invalid, but not checked by the runtime */
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_RESUMING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(SUCCEEDED(hr), "Got hr %#x, expected S_OK.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    /* Changing render targets in any way is not permited for suspend/resume within the same command list */
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_RESUMING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    todo ok(FAILED(hr), "Got hr %#x, expected E_INVALIDARG.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    /* Unlike resume after resume, suspend after suspend is checked by the runtime */
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    todo ok(FAILED(hr), "Got hr %#x, expected E_INVALIDARG.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    /* Test that executing certain commands during a render pass fails */
    ID3D12CommandAllocator_Reset(allocator);
    ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator, NULL, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 2, rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_ClearRenderTargetView(command_list4, get_cpu_handle(device, rtv_heap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 0), black, 0, NULL);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);
    hr = ID3D12GraphicsCommandList4_Close(command_list4);
    ok(FAILED(hr), "Got hr %#x, expected E_FAIL.\n", hr);
    ID3D12GraphicsCommandList4_Release(command_list4);

    /* Barriers inside render passes, GPU work occuring between render passes with
     * PRESERVE_LOCAL or suspend/resume flags are not validated by the runtime. */

    ID3D12CommandAllocator_Release(allocator);

    for (i = 0; i < ARRAY_SIZE(rt); i++)
        ID3D12Resource_Release(rt[i]);

    ID3D12Resource_Release(ds);

    ID3D12DescriptorHeap_Release(rtv_heap);

    ID3D12Device_Release(device);
}

void test_renderpass_rendering(void)
{
    D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS rtv_resolves[5];
    D3D12_RENDER_PASS_RENDER_TARGET_DESC rtv_infos[3];
    D3D12_RENDER_PASS_DEPTH_STENCIL_DESC dsv_info;
    D3D12_FEATURE_DATA_D3D12_OPTIONS18 options18;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12GraphicsCommandList4 *command_list4;
    ID3D12DescriptorHeap *rtv_heap, *dsv_heap;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    ID3D12RootSignature *root_signature;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12PipelineState *pso, *pso_ms;
    ID3D12Resource *rt, *rt_ms, *ds;
    struct test_context_desc desc;
    D3D12_ROOT_PARAMETER rs_param;
    struct resource_readback rb;
    struct test_context context;
    D3D12_VIEWPORT viewport;
    unsigned int i, x, y;
    D3D12_RECT scissor;
    HRESULT hr;

    static const float black[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static const float white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float red[]   = { 1.0f, 0.0f, 0.0f, 1.0f };
    static const float green[] = { 0.0f, 1.0f, 0.0f, 1.0f };
    static const float blue[]  = { 0.0f, 0.0f, 1.0f, 1.0f };

#include "shaders/render_target/headers/ps_renderpass.h"

    struct
    {
        float depth;
        uint32_t colors[3];
        uint32_t sample_mask;
    } shader_args;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    desc.no_root_signature = true;
    desc.no_pipeline = true;
    if (!init_test_context(&context, &desc))
        return;

    memset(&options18, 0, sizeof(options18));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS18, &options18, sizeof(options18));

    if (!options18.RenderPassesValid)
    {
        skip("Render passes not supported.\n");
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);

    if (FAILED(hr))
    {
        skip("ID3D12GraphicsCommandList4 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = 4;
    resource_desc.Height = 4;
    resource_desc.DepthOrArraySize = 3;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.SampleDesc.Count = 1;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties,
            D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
            NULL, &IID_ID3D12Resource, (void**)&rt);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    resource_desc.Alignment = D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.SampleDesc.Count = 4;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties,
            D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
            NULL, &IID_ID3D12Resource, (void**)&rt_ms);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties,
            D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            NULL, &IID_ID3D12Resource, (void**)&ds);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 8);
    dsv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

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

    memset(&rs_param, 0, sizeof(rs_param));
    rs_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_param.Constants.Num32BitValues = sizeof(shader_args) / sizeof(uint32_t);
    rs_param.Constants.ShaderRegister = 0;
    rs_param.Constants.RegisterSpace = 0;
    rs_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &rs_param;
    create_root_signature(context.device, &rs_desc, &root_signature);

    init_pipeline_state_desc(&pso_desc, root_signature, 0, NULL, &ps_renderpass_dxbc, NULL);
    pso_desc.NumRenderTargets = 3;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0xf;
    pso_desc.BlendState.RenderTarget[1].RenderTargetWriteMask = 0xf;
    pso_desc.BlendState.RenderTarget[2].RenderTargetWriteMask = 0xf;
    pso_desc.DepthStencilState.DepthEnable = true;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso_desc.DepthStencilState.StencilEnable = TRUE;
    pso_desc.DepthStencilState.StencilWriteMask = 0xff;
    pso_desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso_desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    pso_desc.DepthStencilState.BackFace = pso_desc.DepthStencilState.FrontFace;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pso);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    pso_desc.SampleDesc.Count = 4;
    pso_desc.SampleMask = 0xf;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&pso_ms);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    for (i = 0; i < 3; i++)
    {
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtv_desc.Texture2DArray.MipSlice = 0;
        rtv_desc.Texture2DArray.FirstArraySlice = i;
        rtv_desc.Texture2DArray.ArraySize = 1;
        ID3D12Device_CreateRenderTargetView(context.device, rt, &rtv_desc,
                get_cpu_rtv_handle(&context, rtv_heap, i));

        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
        rtv_desc.Texture2DMSArray.FirstArraySlice = i;
        rtv_desc.Texture2DMSArray.ArraySize = 1;
        ID3D12Device_CreateRenderTargetView(context.device, rt_ms, &rtv_desc,
                get_cpu_rtv_handle(&context, rtv_heap, i + 4));
    }

    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv_desc.Texture2DArray.MipSlice = 0;
    rtv_desc.Texture2DArray.FirstArraySlice = 0;
    rtv_desc.Texture2DArray.ArraySize = 3;
    ID3D12Device_CreateRenderTargetView(context.device, rt, &rtv_desc,
            get_cpu_rtv_handle(&context, rtv_heap, 3));

    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
    rtv_desc.Texture2DMSArray.FirstArraySlice = 0;
    rtv_desc.Texture2DMSArray.ArraySize = 3;
    ID3D12Device_CreateRenderTargetView(context.device, rt_ms, &rtv_desc,
            get_cpu_rtv_handle(&context, rtv_heap, 7));

    memset(&dsv_desc, 0, sizeof(dsv_desc));
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Texture2D.MipSlice = 0;
    ID3D12Device_CreateDepthStencilView(context.device, ds, &dsv_desc,
            get_cpu_dsv_handle(&context, dsv_heap, 0));

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

    /* Initialize all render target subresources */
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, get_cpu_rtv_handle(&context, rtv_heap, 3), white, 0, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, get_cpu_rtv_handle(&context, rtv_heap, 7), white, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, get_cpu_dsv_handle(&context, dsv_heap, 0),
            D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0xff, 0, NULL);

    /* Test basic clear and preserve load ops */
    memset(rtv_infos, 0, sizeof(rtv_infos));

    for (i = 0; i < ARRAY_SIZE(rtv_infos); i++)
    {
        rtv_infos[i].cpuDescriptor = get_cpu_rtv_handle(&context, rtv_heap, i);
        rtv_infos[i].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
        rtv_infos[i].BeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtv_infos[i].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    }

    rtv_infos[0].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    memcpy(rtv_infos[0].BeginningAccess.Clear.ClearValue.Color, green, sizeof(green));

    rtv_infos[2].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    memcpy(rtv_infos[2].BeginningAccess.Clear.ClearValue.Color, red, sizeof(red));

    memset(&dsv_info, 0, sizeof(dsv_info));
    dsv_info.cpuDescriptor = get_cpu_dsv_handle(&context, dsv_heap, 0);
    dsv_info.DepthBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    dsv_info.DepthBeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    dsv_info.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = 0.5f;
    dsv_info.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    dsv_info.StencilBeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    dsv_info.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = 0x55;
    dsv_info.DepthEndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    dsv_info.StencilEndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, &dsv_info, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(rt, 0, context.queue, context.list, 0xff00ff00u, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 1, context.queue, context.list, 0xffffffffu, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 2, context.queue, context.list, 0xff0000ffu, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_float(ds, 0, context.queue, context.list, 0.5f, 0.0f);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint8(ds, 1, context.queue, context.list, 0x55, 0);
    reset_command_list(context.list, context.allocator);

    /* Test rendering with a render pass */
    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    for (i = 0; i < ARRAY_SIZE(rtv_infos); i++)
    {
        rtv_infos[i].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
        memcpy(&rtv_infos[i].BeginningAccess.Clear.ClearValue.Color, &black, sizeof(black));
    }

    dsv_info.DepthBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    dsv_info.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = 0.0f;
    dsv_info.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    dsv_info.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = 0x00;

    shader_args.depth = 1.0f;
    shader_args.colors[0] = 0xff0000ff;
    shader_args.colors[1] = 0xff00ff00;
    shader_args.colors[2] = 0xffff0000;
    shader_args.sample_mask = ~0u;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, &dsv_info, 0);
    ID3D12GraphicsCommandList4_IASetPrimitiveTopology(command_list4, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList4_RSSetViewports(command_list4, 1, &viewport);
    ID3D12GraphicsCommandList4_RSSetScissorRects(command_list4, 1, &scissor);
    ID3D12GraphicsCommandList4_OMSetStencilRef(command_list4, 0xff);
    ID3D12GraphicsCommandList4_SetGraphicsRootSignature(command_list4, root_signature);
    ID3D12GraphicsCommandList4_SetPipelineState(command_list4, pso);
    ID3D12GraphicsCommandList4_SetGraphicsRoot32BitConstants(command_list4,
            0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
    ID3D12GraphicsCommandList4_DrawInstanced(command_list4, 3, 1, 0, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    /* Ensure that render targets are unbound after the render pass */
    shader_args.depth = 0.5f;
    shader_args.colors[0] = 0xdeadbeef;
    shader_args.colors[1] = 0xdeadbeef;
    shader_args.colors[2] = 0xdeadbeef;

    ID3D12GraphicsCommandList4_SetGraphicsRoot32BitConstants(command_list4,
            0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
    ID3D12GraphicsCommandList4_DrawInstanced(command_list4, 3, 1, 0, 0);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(rt, 0, context.queue, context.list, 0xff0000ff, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 1, context.queue, context.list, 0xff00ff00, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 2, context.queue, context.list, 0xffff0000, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_float(ds, 0, context.queue, context.list, 1.0f, 0.0f);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint8(ds, 1, context.queue, context.list, 0xff, 0);
    reset_command_list(context.list, context.allocator);

    /* Test clearing depth and stencil aspects indiviually */
    dsv_info.DepthBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    dsv_info.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = 0.5f;
    dsv_info.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
    dsv_info.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = 0x00;

    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, &dsv_info, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_float(ds, 0, context.queue, context.list, 0.5f, 0.0f);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint8(ds, 1, context.queue, context.list, 0xff, 0);
    reset_command_list(context.list, context.allocator);

    dsv_info.DepthBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
    dsv_info.DepthBeginningAccess.Clear.ClearValue.DepthStencil.Depth = 1.0f;
    dsv_info.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    dsv_info.StencilBeginningAccess.Clear.ClearValue.DepthStencil.Stencil = 0x40;

    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, &dsv_info, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_resource_state(context.list, ds, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_float(ds, 0, context.queue, context.list, 0.5f, 0.0f);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint8(ds, 1, context.queue, context.list, 0x40, 0);
    reset_command_list(context.list, context.allocator);

    /* Test clear behaviour with SUSPEND/RESUME flags */
    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    rtv_infos[0].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    memcpy(&rtv_infos[0].BeginningAccess.Clear.ClearValue.Color, &blue, sizeof(green));

    shader_args.colors[0] = 0xff00ff00u;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS);
    ID3D12GraphicsCommandList4_IASetPrimitiveTopology(command_list4, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList4_RSSetViewports(command_list4, 1, &viewport);
    ID3D12GraphicsCommandList4_RSSetScissorRects(command_list4, 1, &scissor);
    ID3D12GraphicsCommandList4_OMSetStencilRef(command_list4, 0xff);
    ID3D12GraphicsCommandList4_SetGraphicsRootSignature(command_list4, root_signature);
    ID3D12GraphicsCommandList4_SetPipelineState(command_list4, pso);
    ID3D12GraphicsCommandList4_SetGraphicsRoot32BitConstants(command_list4,
            0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
    ID3D12GraphicsCommandList4_DrawInstanced(command_list4, 3, 1, 0, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, D3D12_RENDER_PASS_FLAG_RESUMING_PASS);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(rt, 0, context.queue, context.list, 0xff00ff00, 0);
    reset_command_list(context.list, context.allocator);

    /* Test bind-to-rasterizer behaviour of different beginning access types */
    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    rtv_infos[0].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    rtv_infos[0].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    memcpy(rtv_infos[0].BeginningAccess.Clear.ClearValue.Color, white, sizeof(white));
    rtv_infos[1].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    rtv_infos[1].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE_LOCAL_RENDER;
    memcpy(rtv_infos[1].BeginningAccess.Clear.ClearValue.Color, white, sizeof(white));
    rtv_infos[2].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    rtv_infos[2].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE_LOCAL_SRV;
    memcpy(rtv_infos[2].BeginningAccess.Clear.ClearValue.Color, white, sizeof(white));

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_sub_resource_state(context.list, rt, 2, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    ID3D12GraphicsCommandList4_IASetPrimitiveTopology(command_list4, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList4_RSSetViewports(command_list4, 1, &viewport);
    ID3D12GraphicsCommandList4_RSSetScissorRects(command_list4, 1, &scissor);
    ID3D12GraphicsCommandList4_OMSetStencilRef(command_list4, 0xff);
    ID3D12GraphicsCommandList4_SetGraphicsRootSignature(command_list4, root_signature);
    ID3D12GraphicsCommandList4_SetPipelineState(command_list4, pso);

    rtv_infos[0].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
    rtv_infos[0].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
    rtv_infos[1].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_RENDER;
    rtv_infos[1].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE_LOCAL_SRV;
    rtv_infos[2].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_SRV;
    rtv_infos[2].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD;

    shader_args.colors[0] = 0xff00ff00u;
    shader_args.colors[1] = 0xdeadbeefu;
    shader_args.colors[2] = 0xdeadbeefu;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_SetGraphicsRoot32BitConstants(command_list4,
            0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
    ID3D12GraphicsCommandList4_DrawInstanced(command_list4, 3, 1, 0, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_sub_resource_state(context.list, rt, 1, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition_sub_resource_state(context.list, rt, 2, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    rtv_infos[0].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
    rtv_infos[0].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    rtv_infos[1].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE_LOCAL_SRV;
    rtv_infos[1].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    rtv_infos[2].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
    rtv_infos[2].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;

    shader_args.colors[0] = 0xffff00ffu;
    shader_args.colors[1] = 0xffff0000u;
    shader_args.colors[2] = 0xdeadbeefu;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_SetGraphicsRoot32BitConstants(command_list4,
            0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
    ID3D12GraphicsCommandList4_DrawInstanced(command_list4, 3, 1, 0, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_sub_resource_state(context.list, rt, 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_sub_resource_state(context.list, rt, 1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_sub_resource_state(context.list, rt, 2, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(rt, 0, context.queue, context.list, 0xffff00ff, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 1, context.queue, context.list, 0xff00ff00, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 2, context.queue, context.list, 0xffff0000, 0);
    reset_command_list(context.list, context.allocator);

    /* Test render pass resolves */
    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);

    for (i = 0; i < ARRAY_SIZE(rtv_infos); i++)
    {
        rtv_infos[i].cpuDescriptor = get_cpu_rtv_handle(&context, rtv_heap, 4 + i);
        rtv_infos[i].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_DISCARD;
        rtv_infos[i].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
        rtv_infos[i].EndingAccess.Resolve.pSrcResource = rt_ms;
        rtv_infos[i].EndingAccess.Resolve.pDstResource = rt;
        rtv_infos[i].EndingAccess.Resolve.SubresourceCount = 1;
        rtv_infos[i].EndingAccess.Resolve.pSubresourceParameters = &rtv_resolves[i];
        rtv_infos[i].EndingAccess.Resolve.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtv_infos[i].EndingAccess.Resolve.PreserveResolveSource = TRUE;

        rtv_resolves[i].SrcSubresource = i;
        rtv_resolves[i].DstSubresource = i;
        rtv_resolves[i].DstX = 0;
        rtv_resolves[i].DstY = 0;
        rtv_resolves[i].SrcRect.left = 0;
        rtv_resolves[i].SrcRect.top = 0;
        rtv_resolves[i].SrcRect.right = 4;
        rtv_resolves[i].SrcRect.bottom = 4;
    }

    rtv_infos[0].EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_MIN;
    rtv_infos[1].EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_MAX;
    rtv_infos[2].EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_AVERAGE;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, ARRAY_SIZE(rtv_infos), rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_IASetPrimitiveTopology(command_list4, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList4_RSSetViewports(command_list4, 1, &viewport);
    ID3D12GraphicsCommandList4_RSSetScissorRects(command_list4, 1, &scissor);
    ID3D12GraphicsCommandList4_SetGraphicsRootSignature(command_list4, root_signature);
    ID3D12GraphicsCommandList4_SetPipelineState(command_list4, pso_ms);

    for (i = 0; i < 4; i++)
    {
        shader_args.depth = (float)i / 3.0f;
        shader_args.colors[0] = (85 * i) << 0;
        shader_args.colors[1] = (85 * i) << 8;
        shader_args.colors[2] = (85 * i) << 16;
        shader_args.sample_mask = 1u << (i ^ 1);

        ID3D12GraphicsCommandList4_OMSetStencilRef(command_list4, 16 * i + 8);
        ID3D12GraphicsCommandList4_SetGraphicsRoot32BitConstants(command_list4,
                0, sizeof(shader_args) / sizeof(uint32_t), &shader_args, 0);
        ID3D12GraphicsCommandList4_DrawInstanced(command_list4, 3, 1, 0, 0);
    }

    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(rt, 0, context.queue, context.list, 0x00000000, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 1, context.queue, context.list, 0x0000ff00, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(rt, 2, context.queue, context.list, 0x00800000, 1);
    reset_command_list(context.list, context.allocator);

    /* Test per-subresource resolve areas */
    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, get_cpu_rtv_handle(&context, rtv_heap, 3), white, 0, NULL);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_DEST);

    for (i = 0; i < 3; i++)
    {
        rtv_resolves[i].SrcSubresource = i;
        rtv_resolves[i].DstSubresource = i;
        rtv_resolves[i].DstX = i == 1 ? 1 : 0;
        rtv_resolves[i].DstY = i == 1 ? 2 : 0;
        rtv_resolves[i].SrcRect.left = i == 2 ? 2 : 0;
        rtv_resolves[i].SrcRect.top = i == 2 ? 1 : 0;
        rtv_resolves[i].SrcRect.right = rtv_resolves[i].SrcRect.left + 2;
        rtv_resolves[i].SrcRect.bottom = rtv_resolves[i].SrcRect.top + 2;
    }

    rtv_infos[0].cpuDescriptor = get_cpu_rtv_handle(&context, rtv_heap, 7);
    rtv_infos[0].BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE;
    rtv_infos[0].EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
    rtv_infos[0].EndingAccess.Resolve.pSrcResource = rt_ms;
    rtv_infos[0].EndingAccess.Resolve.pDstResource = rt;
    rtv_infos[0].EndingAccess.Resolve.SubresourceCount = 3;
    rtv_infos[0].EndingAccess.Resolve.pSubresourceParameters = &rtv_resolves[0];
    rtv_infos[0].EndingAccess.Resolve.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtv_infos[0].EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_AVERAGE;
    rtv_infos[0].EndingAccess.Resolve.PreserveResolveSource = TRUE;

    ID3D12GraphicsCommandList4_BeginRenderPass(command_list4, 1, rtv_infos, NULL, 0);
    ID3D12GraphicsCommandList4_EndRenderPass(command_list4);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    for (i = 0; i < 3; i++)
    {
        uint32_t multiplier = 1u << (8 * i);

        get_texture_readback_with_command_list(rt, i, &rb, context.queue, context.list);

        for (x = 0; x < 4; x++)
        {
            for (y = 0; y < 4; y++)
            {
                uint32_t got = get_readback_uint(&rb, x, y, 0);
                bool inside = x >= rtv_resolves[i].DstX && y >= rtv_resolves[i].DstY &&
                        x < rtv_resolves[i].DstX + rtv_resolves[i].SrcRect.right - rtv_resolves[i].SrcRect.left &&
                        y < rtv_resolves[i].DstY + rtv_resolves[i].SrcRect.bottom - rtv_resolves[i].SrcRect.top;

                if (inside)
                {
                    ok(got >= (0x7f * multiplier) && got <= (0x81 * multiplier),
                            "Got %#x, expected %#x at (%u,%u,%u).\n", got, 0x80 * multiplier, x, y, i);
                }
                else
                    ok(got == 0xffffffffu, "Got %#x, expected %#x at (%u,%u,%u).\n", got, 0xffffffffu, x, y, i);
            }
        }

        release_resource_readback(&rb);
        reset_command_list(context.list, context.allocator);
    }

    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12DescriptorHeap_Release(dsv_heap);

    ID3D12Resource_Release(rt);
    ID3D12Resource_Release(ds);
    ID3D12Resource_Release(rt_ms);

    ID3D12PipelineState_Release(pso);
    ID3D12PipelineState_Release(pso_ms);
    ID3D12RootSignature_Release(root_signature);

    ID3D12GraphicsCommandList4_Release(command_list4);

    destroy_test_context(&context);
}

void test_scissor_clamping(void)
{
    const float black[4] = { 1.0f / 255.0f, 1.0f / 255.0, 1.0f / 255.0f, 1.0f / 255.0f };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    struct test_context_desc context_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE desc[3];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12DescriptorHeap *rtv_heap;
    struct test_context context;
    ID3D12Resource *highres[2];
    ID3D12Resource *lowres;
    D3D12_VIEWPORT vp;
    D3D12_RECT sci;
    unsigned int i;
    HRESULT hr;

#include "shaders/render_target/headers/vs_flat_color.h"
#include "shaders/render_target/headers/ps_flat_color.h"

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_render_target = true;
    context_desc.no_pipeline = true;

    if (!init_test_context(&context, &context_desc))
        return;

    memset(&rs_desc, 0, sizeof(rs_desc));
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    init_pipeline_state_desc(&pso_desc, context.root_signature, DXGI_FORMAT_R8G8B8A8_UNORM, &vs_flat_color_dxbc, &ps_flat_color_dxbc, NULL);
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create PSO, hr #%x.\n", hr);

    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3);
    highres[0] = create_default_texture2d(context.device, 1920, 1080, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    highres[1] = create_default_texture2d(context.device, 1920, 1080, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    lowres = create_default_texture2d(context.device, 480, 270, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);

    for (i = 0; i < 3; i++)
        desc[i] = get_cpu_rtv_handle(&context, rtv_heap, i);

    ID3D12Device_CreateRenderTargetView(context.device, highres[0], NULL, desc[0]);
    ID3D12Device_CreateRenderTargetView(context.device, lowres, NULL, desc[1]);
    ID3D12Device_CreateRenderTargetView(context.device, highres[1], NULL, desc[2]);

    set_rect(&sci, 0, 0, 1920, 1080);
    set_viewport(&vp, 0, 0, 1920, 1080, 0, 1);

    /* Try clearing out of bounds too. Does not trigger validation error either >_< */
    for (i = 0; i < 3; i++)
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, desc[i], black, 1, &sci);

    transition_resource_state(context.list, highres[0], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(context.list, highres[1], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(context.list, lowres, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(highres[0], 0, context.queue, context.list, 0x01010101, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(highres[1], 0, context.queue, context.list, 0x01010101, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(lowres, 0, context.queue, context.list, 0x01010101, 0);
    reset_command_list(context.list, context.allocator);

    transition_resource_state(context.list, highres[0], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition_resource_state(context.list, highres[1], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition_resource_state(context.list, lowres, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    /* When rendering low-res target, it will have scissor out of bounds. There is no validation error when doing so. */
    for (i = 0; i < 3; i++)
    {
        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &desc[i], TRUE, NULL);
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);
    }

    transition_resource_state(context.list, highres[0], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(context.list, highres[1], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(context.list, lowres, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    check_sub_resource_uint(highres[0], 0, context.queue, context.list, ~0u, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(highres[1], 0, context.queue, context.list, ~0u, 0);
    reset_command_list(context.list, context.allocator);
    check_sub_resource_uint(lowres, 0, context.queue, context.list, ~0u, 0);
    reset_command_list(context.list, context.allocator);

    ID3D12Resource_Release(highres[0]);
    ID3D12Resource_Release(highres[1]);
    ID3D12Resource_Release(lowres);
    ID3D12DescriptorHeap_Release(rtv_heap);
    destroy_test_context(&context);
}

void test_mismatching_rtv_dsv_size(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12DescriptorHeap *rtv_heap, *dsv_heap;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2], dsv;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    struct test_context_desc context_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *rt[3], *ds[3];
    unsigned int i, j, x, y, w, h;
    struct resource_readback rb;
    struct test_context context;
    D3D12_VIEWPORT viewport;
    RECT scissor;
    HRESULT hr;

#include "shaders/render_target/headers/ps_rt_mismatch.h"

    static const FLOAT red[] = { 1.0f, 0.0, 0.0f, 1.0f };

    static const uint32_t rt_ds_size[][2] =
    {
        { 4, 4 },
        { 6, 2 },
        { 8, 8 },
    };

    struct
    {
        uint32_t rt_count;
        uint32_t rtvs[2];
        uint32_t dsv;
        bool expected;
    }
    tests[] =
    {
        /* Mismatched RTV-DSV size */
        { 1, { 0, 0 }, 0 },
        { 1, { 0, 0 }, 1 },
        { 1, { 0, 0 }, 2 },
        { 1, { 1, 0 }, 0 },
        { 1, { 1, 0 }, 1 },
        { 1, { 1, 0 }, 2 },
        { 1, { 2, 0 }, 0 },
        { 1, { 2, 0 }, 1 },
        { 1, { 2, 0 }, 2 },

        /* Mismatched RTV sizes with no DSV */
        { 2, { 0, 1 }, ~0u },
        { 2, { 0, 2 }, ~0u },
        { 2, { 1, 0 }, ~0u },
        { 2, { 1, 2 }, ~0u },
        { 2, { 2, 0 }, ~0u },
        { 2, { 2, 1 }, ~0u },

        /* Mismatched RTV sizes with max size dsv */
        { 2, { 0, 1 }, 2 },
        { 2, { 0, 2 }, 2 },
        { 2, { 1, 0 }, 2 },
        { 2, { 1, 2 }, 2 },
        { 2, { 2, 0 }, 2 },
        { 2, { 2, 1 }, 2 },
    };

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_render_target = true;
    context_desc.no_pipeline = true;

    if (!init_test_context(&context, &context_desc))
        return;

    memset(&rs_desc, 0, sizeof(rs_desc));

    hr = create_root_signature(context.device, &rs_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    init_pipeline_state_desc(&pso_desc, context.root_signature, 0, NULL, &ps_rt_mismatch_dxbc, NULL);
    pso_desc.DepthStencilState.DepthEnable = TRUE;
    pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pso_desc.NumRenderTargets = ARRAY_SIZE(rtvs);
    for (i = 0; i < ARRAY_SIZE(rtvs); i++)
        pso_desc.RTVFormats[i] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, ARRAY_SIZE(rt_ds_size));
    dsv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, ARRAY_SIZE(rt_ds_size));

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));

    for (i = 0; i < ARRAY_SIZE(rt_ds_size); i++)
    {
        resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resource_desc.Width = rt_ds_size[i][0];
        resource_desc.Height = rt_ds_size[i][1];
        resource_desc.DepthOrArraySize = 1;
        resource_desc.MipLevels = 1;
        resource_desc.SampleDesc.Count = 1;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&rt[i]);
        ok(hr == S_OK, "Failed to create color image, hr %#x.\n", hr);

        memset(&rtv_desc, 0, sizeof(rtv_desc));
        rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        ID3D12Device_CreateRenderTargetView(context.device, rt[i], &rtv_desc, get_cpu_rtv_handle(&context, rtv_heap, i));

        resource_desc.Format = DXGI_FORMAT_D32_FLOAT;
        resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                &resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, NULL, &IID_ID3D12Resource, (void**)&ds[i]);
        ok(hr == S_OK, "Failed to create depth image, hr %#x.\n", hr);

        memset(&dsv_desc, 0, sizeof(dsv_desc));
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

        ID3D12Device_CreateDepthStencilView(context.device, ds[i], &dsv_desc, get_cpu_dsv_handle(&context, dsv_heap, i));
    }

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = 8.0f;
    viewport.Height = 8.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    scissor.left = 0;
    scissor.top = 0;
    scissor.bottom = 8;
    scissor.right = 8;

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        w = scissor.right;
        h = scissor.bottom;

        for (j = 0; j < tests[i].rt_count; j++)
        {
            rtvs[j] = get_cpu_rtv_handle(&context, rtv_heap, tests[i].rtvs[j]);
            ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, rtvs[j], red, 0, NULL);

            w = min(w, rt_ds_size[tests[i].rtvs[j]][0]);
            h = min(h, rt_ds_size[tests[i].rtvs[j]][1]);
        }

        if (tests[i].dsv != ~0u)
        {
            dsv = get_cpu_dsv_handle(&context, dsv_heap, tests[i].dsv);
            ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);

            w = min(w, rt_ds_size[tests[i].dsv][0]);
            h = min(h, rt_ds_size[tests[i].dsv][1]);
        }

        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list,
                tests[i].rt_count, rtvs, FALSE, tests[i].dsv != ~0u ? &dsv : NULL);

        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &scissor);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);

        ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

        for (j = 0; j < tests[i].rt_count; j++)
            transition_resource_state(context.list, rt[tests[i].rtvs[j]], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        if (tests[i].dsv != ~0u)
            transition_resource_state(context.list, ds[tests[i].dsv], D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);

        /* This is undefined behaviour, and the exact areas written to the respective
         * RTVs and DSV differ by vendor. The common denominator is that the all pixels
         * included in all bound views get written. */
        for (j = 0; j < tests[i].rt_count; j++)
        {
            get_texture_readback_with_command_list(rt[tests[i].rtvs[j]], 0, &rb, context.queue, context.list);

            for (y = 0; y < h; y++)
            {
                for (x = 0; x < w; x++)
                {
                    uint32_t got = get_readback_uint(&rb, x, y, 0);
                    uint32_t expected = 0xff00ff00;

                    ok(got == expected, "Got %#x, expected %#x at %u,%u.\n", got, expected, x, y);

                    if (got != expected)
                        break;
                }
            }

            release_resource_readback(&rb);

            reset_command_list(context.list, context.allocator);
            transition_resource_state(context.list, rt[tests[i].rtvs[j]], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }

        if (tests[i].dsv != ~0u)
        {
            get_texture_readback_with_command_list(ds[tests[i].dsv], 0, &rb, context.queue, context.list);

            for (y = 0; y < h; y++)
            {
                for (x = 0; x < w; x++)
                {
                    float got = get_readback_float(&rb, x, y);
                    float expected = 0.0f;

                    ok(got == expected, "Got %f, expected %f at %u,%u.\n", got, expected, x, y);

                    if (got != expected)
                        break;
                }
            }

            release_resource_readback(&rb);

            reset_command_list(context.list, context.allocator);
            transition_resource_state(context.list, ds[tests[i].dsv], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        }
    }

    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12DescriptorHeap_Release(dsv_heap);

    for (i = 0; i < ARRAY_SIZE(rt_ds_size); i++)
    {
        ID3D12Resource_Release(rt[i]);
        ID3D12Resource_Release(ds[i]);
    }

    destroy_test_context(&context);
}


void decode_rgb9e5(uint32_t packed, float color[3])
{
    // No inf, nan, or implied 1 and thus no denorms
    uint32_t r = (packed >>  0) & 0x1ffu;
    uint32_t g = (packed >>  9) & 0x1ffu;
    uint32_t b = (packed >> 18) & 0x1ffu;
    uint32_t e = (packed >> 27);

    float factor = (float)(1u << e) / (float)(1u << 24u);

    color[0] = (float)r * factor;
    color[1] = (float)g * factor;
    color[2] = (float)b * factor;
}

void test_rgb9e5_rendering(void)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format_query;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor;
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12DescriptorHeap *rtv_heap;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    D3D12_RESOURCE_DESC rt_desc;
    ID3D12PipelineState *pso;
    unsigned int i, x, y;
    ID3D12Resource *rt;
    D3D12_VIEWPORT vp;
    D3D12_RECT sci;
    HRESULT hr;

    static uint32_t reference[16][16];

    static const FLOAT white[] = { 1.0f, 1.0f, 1.0f, 1.0f };

#include "shaders/render_target/headers/ps_gradient.h"
#include "shaders/render_target/headers/vs_gradient.h"

    memset(&desc, 0, sizeof(desc));
    desc.no_pipeline = true;
    desc.no_render_target = true;

    if (!init_test_context(&context, &desc))
        return;

    memset(&format_query, 0, sizeof(format_query));
    format_query.Format = DXGI_FORMAT_R9G9B9E5_SHAREDEXP;

    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_FORMAT_SUPPORT, &format_query, sizeof(format_query));

    if (FAILED(hr) || !(format_query.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
    {
        skip("RGB9E5 not supported for rendering.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rtv_heap_desc, 0, sizeof(rtv_heap_desc));
    rtv_heap_desc.NumDescriptors = 1u;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    hr = ID3D12Device_CreateDescriptorHeap(context.device, &rtv_heap_desc, &IID_ID3D12DescriptorHeap, (void**)&rtv_heap);
    ok(hr == S_OK, "Failed to create descriptor heap, hr %#x.\n", hr);

    rtv_descriptor = get_cpu_rtv_handle(&context, rtv_heap, 0);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&rt_desc, 0, sizeof(rt_desc));
    rt_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rt_desc.Width = 16;
    rt_desc.Height = 16;
    rt_desc.DepthOrArraySize = 1;
    rt_desc.MipLevels = 1;
    rt_desc.SampleDesc.Count = 1;
    rt_desc.Format = DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
    rt_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    rt_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &rt_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void**)&rt);
    ok(hr == S_OK, "Failed to create render target, hr %#x.\n", hr);

    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.Format = DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    ID3D12Device_CreateRenderTargetView(context.device, rt, &rtv_desc, rtv_descriptor);

    memset(&rs_desc, 0, sizeof(rs_desc));
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    init_pipeline_state_desc(&pso_desc, context.root_signature, DXGI_FORMAT_R9G9B9E5_SHAREDEXP,
            &vs_gradient_dxbc, &ps_gradient_dxbc, NULL);
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&pso);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    memset(&vp, 0, sizeof(vp));
    vp.Width = 16.0f;
    vp.Height = 16.0f;
    vp.MaxDepth = 1.0f;

    memset(&sci, 0, sizeof(sci));
    sci.right = 16;
    sci.bottom = 16;

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &rtv_descriptor, TRUE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, rtv_descriptor, white, 0, NULL);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(rt, 0, &rb, context.queue, context.list);

    for (x = 0; x < 16; x++)
    {
        for (y = 0; y < 16; y++)
        {
            float rgb_got[3], rgb_expected[3];
            uint32_t packed = get_readback_uint(&rb, x, y, 0);

            rgb_expected[0] = (((float)x) + 0.5f) / 16.0f;
            rgb_expected[1] = 1.0f - (((float)y) + 0.5f) / 16.0f;
            rgb_expected[2] = rgb_expected[0] + rgb_expected[1];

            decode_rgb9e5(packed, rgb_got);

            for (i = 0; i < 3; i++)
                ok(fabs(rgb_got[i] - rgb_expected[i]) < 0.01f, "Got %f, expected %f for channel %u at %u,%u.\n", rgb_got[i], rgb_expected[i], i, x, y);

            reference[x][y] = packed;
        }
    }

    release_resource_readback(&rb);
    ID3D12PipelineState_Release(pso);

    reset_command_list(context.list, context.allocator);

    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0x7u; /* no alpha */

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&pso);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &rtv_descriptor, TRUE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, rtv_descriptor, white, 0, NULL);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(rt, 0, &rb, context.queue, context.list);

    for (x = 0; x < 16; x++)
    {
        for (y = 0; y < 16; y++)
        {
            uint32_t packed = get_readback_uint(&rb, x, y, 0);

            ok(packed == reference[x][y], "Got %#x, expected %#x at %u,%u.\n", packed, reference[x][y], x, y);
        }
    }

    release_resource_readback(&rb);
    ID3D12PipelineState_Release(pso);

    reset_command_list(context.list, context.allocator);

    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0x8u; /* only alpha */

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&pso);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &rtv_descriptor, TRUE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, rtv_descriptor, white, 0, NULL);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

    transition_resource_state(context.list, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(rt, 0, &rb, context.queue, context.list);

    for (x = 0; x < 16; x++)
    {
        for (y = 0; y < 16; y++)
        {
            float rgb_got[3];
            uint32_t packed = get_readback_uint(&rb, x, y, 0);
            decode_rgb9e5(packed, rgb_got);

            for (i = 0; i < 3; i++)
                ok(rgb_got[i] == 1.0f, "Got %f, expected 1.0 for channel %u at %u,%u.\n", rgb_got[i], i, x, y);
        }
    }

    release_resource_readback(&rb);
    ID3D12PipelineState_Release(pso);

    ID3D12Resource_Release(rt);
    ID3D12DescriptorHeap_Release(rtv_heap);

    destroy_test_context(&context);
}
