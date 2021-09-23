/*
 * Copyright 2021 Hans-Kristian Arntzen for Valve Corporation
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

#define INITGUID
#define VKD3D_TEST_DECLARE_MAIN
#include "d3d12_crosstest.h"

struct context
{
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;
    ID3D12Resource *resources[64];
    ID3D12DescriptorHeap *heap;
};

static void setup(int argc, char **argv)
{
    pfn_D3D12CreateDevice = get_d3d12_pfn(D3D12CreateDevice);
    pfn_D3D12GetDebugInterface = get_d3d12_pfn(D3D12GetDebugInterface);

    parse_args(argc, argv);
    enable_d3d12_debug_layer(argc, argv);
    init_adapter_info();

    pfn_D3D12CreateVersionedRootSignatureDeserializer = get_d3d12_pfn(D3D12CreateVersionedRootSignatureDeserializer);
    pfn_D3D12SerializeVersionedRootSignature = get_d3d12_pfn(D3D12SerializeVersionedRootSignature);
}

static double get_time(void)
{
#ifdef _WIN32
    LARGE_INTEGER lc, lf;
    QueryPerformanceCounter(&lc);
    QueryPerformanceFrequency(&lf);
    return (double)lc.QuadPart / (double)lf.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + 1e-9 * ts.tv_nsec;
#endif
}

static bool init_context(struct context *ctx)
{
    unsigned int i;
    memset(ctx, 0, sizeof(*ctx));

    if (!(ctx->device = create_device()))
        return false;

    ctx->heap = create_cpu_descriptor_heap(ctx->device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 64);
    for (i = 0; i < ARRAY_SIZE(ctx->resources); i++)
    {
        ctx->resources[i] = create_default_texture2d(ctx->device, 512, 512, 1, 1,
                DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    ID3D12Device_CreateCommandAllocator(ctx->device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void**)&ctx->allocator);
    ctx->queue = create_command_queue(ctx->device, D3D12_COMMAND_LIST_TYPE_DIRECT, 0);
    ID3D12Device_CreateCommandList(ctx->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, ctx->allocator,
            NULL, &IID_ID3D12GraphicsCommandList, (void**)&ctx->list);
    ID3D12GraphicsCommandList_Close(ctx->list);

    for (i = 0; i < 64; i++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h;
        h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(ctx->heap);
        h.ptr += i * ID3D12Device_GetDescriptorHandleIncrementSize(ctx->device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        ID3D12Device_CreateRenderTargetView(ctx->device, ctx->resources[i], NULL, h);
    }

    return true;
}

static void destroy_context(struct context *ctx)
{
    unsigned int i;
    ID3D12DescriptorHeap_Release(ctx->heap);
    for (i = 0; i < ARRAY_SIZE(ctx->resources); i++)
        ID3D12Resource_Release(ctx->resources[i]);
    ID3D12GraphicsCommandList_Release(ctx->list);
    ID3D12CommandAllocator_Release(ctx->allocator);
    ID3D12CommandQueue_Release(ctx->queue);
    ID3D12Device_Release(ctx->device);
}

enum clear_mode
{
    CLEAR_MODE_NULL,
    CLEAR_MODE_FULL_RECT,
    CLEAR_MODE_PARTIAL
};

static double do_benchmark_run(struct context *ctx, unsigned int clear_iterations,
        enum clear_mode mode, bool pre_discard, unsigned int num_resources)
{
    const D3D12_RECT partial_rect = { 257, 0, 509, 259 };
    const D3D12_RECT full_rect = { 0, 0, 512, 512 };
    D3D12_CPU_DESCRIPTOR_HANDLE base_h, h;
    D3D12_DISCARD_REGION discard_region;
    double start_time, end_time;
    unsigned int resource_index;
    UINT descriptor_size;
    FLOAT clear_color[4];
    ID3D12Fence *fence;
    unsigned int i;

    base_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(ctx->heap);
    ID3D12GraphicsCommandList_Reset(ctx->list, ctx->allocator, NULL);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(ctx->device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    discard_region.FirstSubresource = 0;
    discard_region.NumSubresources = 1;
    discard_region.NumRects = 0;
    discard_region.pRects = NULL;

    /* Spam clear on different RTVs and see what happens to perf. */
    for (i = 0; i < clear_iterations; i++)
    {
        resource_index = i & (num_resources - 1);
        h = base_h;
        h.ptr += resource_index * descriptor_size;
        clear_color[0] = (float)(i & 255) / 255.0f;
        clear_color[1] = (float)((i + 1) & 255) / 255.0f;
        clear_color[2] = (float)((i + 2) & 255) / 255.0f;
        clear_color[3] = (float)((i + 3) & 255) / 255.0f;

        if (pre_discard)
            ID3D12GraphicsCommandList_DiscardResource(ctx->list, ctx->resources[resource_index], &discard_region);

        switch (mode)
        {
            default:
                ID3D12GraphicsCommandList_ClearRenderTargetView(ctx->list, h, clear_color, 0, NULL);
                break;

            case CLEAR_MODE_FULL_RECT:
                ID3D12GraphicsCommandList_ClearRenderTargetView(ctx->list, h, clear_color, 1, &full_rect);
                break;

            case CLEAR_MODE_PARTIAL:
                ID3D12GraphicsCommandList_ClearRenderTargetView(ctx->list, h, clear_color, 1, &partial_rect);
                break;
        }
    }

    ID3D12GraphicsCommandList_Close(ctx->list);
    ID3D12Device_CreateFence(ctx->device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fence);
    start_time = get_time();
    ID3D12CommandQueue_ExecuteCommandLists(ctx->queue, 1, (ID3D12CommandList * const *)&ctx->list);
    ID3D12CommandQueue_Signal(ctx->queue, fence, 1);
    ID3D12Fence_SetEventOnCompletion(fence, 1, NULL);
    end_time = get_time();
    ID3D12Fence_Release(fence);
    ID3D12CommandAllocator_Reset(ctx->allocator);
    return end_time - start_time;
}

START_TEST(renderpass_performance)
{
    struct test
    {
        unsigned int iteration_count;
        const char *desc;
        enum clear_mode mode;
        bool pre_discard;
        unsigned int num_resources;
    };
    static const struct test tests[] =
    {
        { 8 * 1024, "8k clear, NULL rect", CLEAR_MODE_NULL, false, 64 },
        { 32 * 1024, "32k clear, NULL rect", CLEAR_MODE_NULL, false, 64 },
        { 128 * 1024, "128k clear, NULL rect", CLEAR_MODE_NULL, false, 64 },
        { 8 * 1024, "8k clear, full rect", CLEAR_MODE_FULL_RECT, false, 64 },
        { 32 * 1024, "32k clear, full rect", CLEAR_MODE_FULL_RECT, false, 64 },
        { 128 * 1024, "128k clear, full rect", CLEAR_MODE_FULL_RECT, false, 64 },
        { 8 * 1024, "8k clear, partial rects", CLEAR_MODE_PARTIAL, false, 64 },
        { 32 * 1024, "32k clear, partial rects", CLEAR_MODE_PARTIAL, false, 64 },
        { 128 * 1024, "128k clear, partial rects", CLEAR_MODE_PARTIAL, false, 64 },
        { 128 * 1024, "128k clear, NULL rect, discard", CLEAR_MODE_NULL, true, 64 },
        { 128 * 1024, "128k clear, full rect, discard", CLEAR_MODE_FULL_RECT, true, 64 },
        { 128 * 1024, "128k clear, partial rect, discard", CLEAR_MODE_PARTIAL, true, 64 },
        { 64 * 1024, "64k clear, batch size 1", CLEAR_MODE_NULL, false, 1 },
        { 64 * 1024, "64k clear, batch size 2", CLEAR_MODE_NULL, false, 2 },
        { 64 * 1024, "64k clear, batch size 4", CLEAR_MODE_NULL, false, 4 },
        { 64 * 1024, "64k clear, batch size 8", CLEAR_MODE_NULL, false, 8 },
        { 64 * 1024, "64k clear, batch size 16", CLEAR_MODE_NULL, false, 16 },
        { 64 * 1024, "64k clear, batch size 32", CLEAR_MODE_NULL, false, 32 },
        { 128 * 1024, "128k clear, batch size 1", CLEAR_MODE_NULL, false, 1 },
        { 128 * 1024, "128k clear, batch size 2", CLEAR_MODE_NULL, false, 2 },
        { 128 * 1024, "128k clear, batch size 4", CLEAR_MODE_NULL, false, 4 },
        { 128 * 1024, "128k clear, batch size 8", CLEAR_MODE_NULL, false, 8 },
        { 128 * 1024, "128k clear, batch size 16", CLEAR_MODE_NULL, false, 16 },
        { 128 * 1024, "128k clear, batch size 32", CLEAR_MODE_NULL, false, 32 },
    };
    struct context ctx;
    unsigned int i;
    double t;

    setup(argc, argv);
    if (!init_context(&ctx))
        return;

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        t = do_benchmark_run(&ctx, tests[i].iteration_count, tests[i].mode, tests[i].pre_discard, tests[i].num_resources);
        printf("[%40s] => %8.3f ms.\n", tests[i].desc, 1e3 * t);
    }

    destroy_context(&ctx);
}
