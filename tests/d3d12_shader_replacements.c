/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API
#include "d3d12_crosstest.h"
#include "../libs/vkd3d-shader/shaders/first_light_trailsort.h"

static int compare_sort_uints(const void *a, const void *b)
{
    unsigned int x = *(const unsigned int *)a, y = *(const unsigned int *)b;
    return (x > y) - (x < y);
}

void test_first_light_trailsort(void)
{
    static const unsigned int sizes[] = {0, 1, 2, 3, 15, 31, 32, 33, 63, 64, 65, 127,
            255, 511, 1023, 1024, 1025, 1055, 2047, 2048, 2049, 4095, 4096, 8193, 16384, 32769, 65537};
    const D3D12_SHADER_BYTECODE shader = {first_light_trailsort_dxil, sizeof(first_light_trailsort_dxil)};
    enum { MAX_N = 65537, GUARD = 32, OUTPUT_WORDS = 2 * MAX_N + 2 * GUARD };
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu, output_gpu;
    D3D12_DESCRIPTOR_RANGE ranges[2];
    D3D12_ROOT_PARAMETER parameters[2];
    D3D12_ROOT_SIGNATURE_DESC root_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
    ID3D12Resource *input, *count, *initial, *output;
    ID3D12DescriptorHeap *heap;
    struct resource_readback rb;
    struct test_context context;
    unsigned int *values, *expected, *clear;
    unsigned int size_idx, pattern, n, i, seed, value, stride, first_bad;
    HRESULT hr;

    if (!init_compute_test_context(&context))
        return;
    if (!context_supports_dxil(&context))
    {
        skip("DXIL not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(ranges, 0, sizeof(ranges));
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 2;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    memset(parameters, 0, sizeof(parameters));
    for (i = 0; i < ARRAY_SIZE(parameters); ++i)
    {
        parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[i].DescriptorTable.NumDescriptorRanges = 1;
        parameters[i].DescriptorTable.pDescriptorRanges = &ranges[i];
    }
    memset(&root_desc, 0, sizeof(root_desc));
    root_desc.NumParameters = ARRAY_SIZE(parameters);
    root_desc.pParameters = parameters;
    hr = create_root_signature(context.device, &root_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", (unsigned int)hr);
    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature, shader);

    values = malloc(MAX_N * sizeof(*values));
    expected = malloc(MAX_N * sizeof(*expected));
    clear = malloc(OUTPUT_WORDS * sizeof(*clear));
    assert_that(values && expected && clear, "Failed to allocate test data.\n");
    for (i = 0; i < OUTPUT_WORDS; ++i)
        clear[i] = 0xa53c9e71;
    input = create_upload_buffer(context.device, MAX_N * sizeof(*values), NULL);
    count = create_upload_buffer(context.device, sizeof(n), NULL);
    initial = create_upload_buffer(context.device, OUTPUT_WORDS * sizeof(*clear), clear);
    output = create_default_buffer(context.device, OUTPUT_WORDS * sizeof(*clear),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);
    cpu = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    gpu = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
    stride = ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    output_gpu.ptr = gpu.ptr + 2 * stride;
    memset(&srv, 0, sizeof(srv));
    srv.Format = DXGI_FORMAT_R32_UINT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = MAX_N;
    ID3D12Device_CreateShaderResourceView(context.device, input, &srv, cpu);
    cpu.ptr += stride;
    srv.Buffer.NumElements = 1;
    ID3D12Device_CreateShaderResourceView(context.device, count, &srv, cpu);
    cpu.ptr += stride;
    memset(&uav, 0, sizeof(uav));
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.FirstElement = GUARD;
    uav.Buffer.NumElements = 2 * MAX_N;
    ID3D12Device_CreateUnorderedAccessView(context.device, output, NULL, &uav, cpu);

    for (size_idx = 0; size_idx < ARRAY_SIZE(sizes); ++size_idx)
    {
        n = sizes[size_idx];
        for (pattern = 0; pattern < 6; ++pattern)
        {
            vkd3d_test_set_context("N %u, pattern %u", n, pattern);
            seed = 0x12345678u + n + pattern;
            for (i = 0; i < n; ++i)
            {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                value = seed;
                if (pattern == 1) value = i;
                if (pattern == 2) value = n - i;
                if (pattern == 3) value = 0xffffffffu;
                if (pattern == 4) value &= 31;
                if (pattern == 5) value = (i & 1) ? 0x80000000u : 0x7fffffffu;
                values[i] = expected[i] = value;
            }
            qsort(expected, n, sizeof(*expected), compare_sort_uints);
            update_buffer_data(input, 0, n * sizeof(*values), values);
            update_buffer_data(count, 0, sizeof(n), &n);
            if (size_idx || pattern)
                transition_resource_state(context.list, output,
                        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12GraphicsCommandList_CopyBufferRegion(context.list, output, 0,
                    initial, 0, OUTPUT_WORDS * sizeof(*clear));
            transition_resource_state(context.list, output,
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
            ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
            ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, gpu);
            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 1, output_gpu);
            ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
            transition_resource_state(context.list, output,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            get_buffer_readback_with_command_list(output, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);
            first_bad = n;
            for (i = 0; i < n; ++i)
            {
                if (get_readback_uint(&rb, GUARD + i, 0, 0) != expected[i])
                {
                    first_bad = i;
                    break;
                }
            }
            ok(first_bad == n, "Sort mismatch at word %u.\n", first_bad);
            first_bad = OUTPUT_WORDS;
            for (i = 0; i < OUTPUT_WORDS; ++i)
            {
                if (i >= GUARD && i < GUARD + (n > 1024 ? 2 * n : n))
                    continue;
                if (get_readback_uint(&rb, i, 0, 0) != clear[i])
                {
                    first_bad = i;
                    break;
                }
            }
            ok(first_bad == OUTPUT_WORDS, "Guard corruption at word %u.\n", first_bad);
            release_resource_readback(&rb);
            reset_command_list(context.list, context.allocator);
        }
    }
    vkd3d_test_set_context(NULL);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12Resource_Release(output);
    ID3D12Resource_Release(initial);
    ID3D12Resource_Release(count);
    ID3D12Resource_Release(input);
    free(clear);
    free(expected);
    free(values);
    destroy_test_context(&context);
}
