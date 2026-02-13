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

void test_buffers_oob_behavior_vectorized_structured_16bit(void)
{
    static const unsigned int strides[] = { 2, 4, 6, 8, 4, 6, 8 };
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[1];
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options4;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_ROOT_PARAMETER root_parameters[1];
    ID3D12Resource *output_buffers[7];
    struct resource_readback rb;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    uint32_t first_element;
    uint32_t num_elements;
    unsigned int i, j;
    HRESULT hr;

#include "shaders/robustness/headers/oob_behavior_vectorized_structured_16bit_write.h"

    if (!init_compute_test_context(&context))
        return;

    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_2;
    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));
    if (FAILED(hr) || shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_2)
    {
        skip("Shader model 6.2 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&options4, 0, sizeof(options4));
    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS4, &options4, sizeof(options4));
    if (FAILED(hr))
        options4.Native16BitShaderOpsSupported = FALSE;

    if (!options4.Native16BitShaderOpsSupported)
    {
        skip("Skipping 16-bit robustness tests.\n");
        destroy_test_context(&context);
        return;
    }

    root_signature_desc.NumParameters = 1;
    root_signature_desc.Flags = 0;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;

    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[0].NumDescriptors = UINT_MAX;
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ARRAY_SIZE(output_buffers));

    if (is_mesa_intel_device(context.device) ||
        (is_nvidia_device(context.device) && is_vk_device_extension_supported(context.device, "VK_EXT_descriptor_heap")))
    {
        /* There appears to be driver issues.
         * SSBO not aligned to the advertised 4 bytes seems to break down for unknown reasons.
         * There's also some strange code dealing with using lower 2 bits to encode range offsets.
         * The SSBO itself is encoded with all bits, so this is confusing. */
        num_elements = 10;
        first_element = 2;
        bug_if(true) ok(false, "Skipping 16-bit alignment test due to driver not working as expected.\n");
    }
    else
    {
        num_elements = 9;
        first_element = 1;
    }

    for (i = 0; i < ARRAY_SIZE(output_buffers); i++)
    {
        output_buffers[i] = create_default_buffer(context.device, 1024,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        memset(&uav_desc, 0, sizeof(uav_desc));
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.StructureByteStride = strides[i];
        /* Very spicy test. */
        uav_desc.Buffer.NumElements = num_elements;
        uav_desc.Buffer.FirstElement = first_element;

        ID3D12Device_CreateUnorderedAccessView(context.device, output_buffers[i], NULL, &uav_desc, get_cpu_descriptor_handle(&context, heap, i));
    }

    context.pipeline_state = create_compute_pipeline_state(context.device,
        context.root_signature, oob_behavior_vectorized_structured_16bit_write_dxil);
    ok(context.pipeline_state, "Failed to create PSO.\n");

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

    for (i = 0; i < ARRAY_SIZE(output_buffers); i++)
    {
        transition_resource_state(context.list, output_buffers[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output_buffers[i], DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

        for (j = 0; j < 256; j++)
        {
            uint16_t value = get_readback_uint16(&rb, j, 0);
            uint16_t expected = j - first_element * strides[i] / 2;
            bool is_todo;

            if (expected & 0x8000)
                expected = 0;
            else if (expected >= strides[i] / 2 * num_elements)
                expected = 0;

            /* For RAW buffers, AMD robustness is 4 byte aligned.
             * This is theoretically out of spec, but this is so minor, that we can let it slide.
             * Intel has similar issues even on native too. (TODO: verify with this test). */
            is_todo = (first_element % 2 || num_elements % 2) &&
                    strides[i] % 4 != 0 && j == (strides[i] / 2) * (first_element + num_elements);

            todo_if(is_todo)
            ok(value == expected, "UAV %u, u16 index %u, expected %u, got %u\n", i, j, expected, value);
        }

        reset_command_list(context.list, context.allocator);
        release_resource_readback(&rb);
    }

    ID3D12DescriptorHeap_Release(heap);
    for (i = 0; i < ARRAY_SIZE(output_buffers); i++)
        ID3D12Resource_Release(output_buffers[i]);
    destroy_test_context(&context);
}

void test_buffers_oob_behavior_vectorized_byte_address(void)
{
    /* Vectorized structured buffers are handled by other tests, but
     * vectorized byte address buffers are particularly
     * weird due to component based robustness.
     * Intended to trip vectorized load-store optimizations in dxil-spirv. */
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[1];
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options4;
    ID3D12PipelineState *read_pso_32bit_smem;
    ID3D12GraphicsCommandList *command_list;
    D3D12_ROOT_PARAMETER root_parameters[1];
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    ID3D12PipelineState *write_pso_32bit;
    ID3D12PipelineState *write_pso_16bit;
    ID3D12PipelineState *read_pso_32bit;
    ID3D12PipelineState *read_pso_16bit;
    unsigned int descriptor_size, i, j;
    ID3D12Resource *read_output_buffer;
    ID3D12DescriptorHeap *gpu_heap;
    ID3D12DescriptorHeap *cpu_heap;
    ID3D12Resource *output_buffer;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

#include "shaders/robustness/headers/oob_behavior_vectorized_byte_address_16bit_read.h"
#include "shaders/robustness/headers/oob_behavior_vectorized_byte_address_16bit_write.h"
#include "shaders/robustness/headers/oob_behavior_vectorized_byte_address_32bit_read.h"
#include "shaders/robustness/headers/oob_behavior_vectorized_byte_address_32bit_read_smem.h"
#include "shaders/robustness/headers/oob_behavior_vectorized_byte_address_32bit_write.h"

    if (!init_compute_test_context(&context))
        return;

    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_2;
    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));
    if (FAILED(hr) || shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_2)
    {
        skip("Shader model 6.2 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&options4, 0, sizeof(options4));
    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS4, &options4, sizeof(options4));
    if (FAILED(hr))
        options4.Native16BitShaderOpsSupported = FALSE;

    if (!options4.Native16BitShaderOpsSupported)
        skip("Skipping 16-bit robustness tests.\n");

    command_list = context.list;
    queue = context.queue;

    root_signature_desc.NumParameters = 1;
    root_signature_desc.Flags = 0;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;

    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[0].NumDescriptors = UINT_MAX;
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);
    output_buffer = create_default_buffer(context.device, 8 * 16 * sizeof(uint32_t),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    read_output_buffer = create_default_buffer(context.device, 12 * 16 * sizeof(uint32_t),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    write_pso_32bit = create_compute_pipeline_state(context.device,
            context.root_signature, oob_behavior_vectorized_byte_address_32bit_write_dxil);

    if (options4.Native16BitShaderOpsSupported)
    {
        write_pso_16bit = create_compute_pipeline_state(context.device,
                 context.root_signature, oob_behavior_vectorized_byte_address_16bit_write_dxil);
    }
    else
        write_pso_16bit = NULL;

    read_pso_32bit = create_compute_pipeline_state(context.device,
            context.root_signature, oob_behavior_vectorized_byte_address_32bit_read_dxil);
    read_pso_32bit_smem = create_compute_pipeline_state(context.device,
            context.root_signature, oob_behavior_vectorized_byte_address_32bit_read_smem_dxil);

    if (options4.Native16BitShaderOpsSupported)
    {
        read_pso_16bit = create_compute_pipeline_state(context.device,
                context.root_signature, oob_behavior_vectorized_byte_address_16bit_read_dxil);
    }
    else
        read_pso_16bit = NULL;

    gpu_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8 + 2);
    cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8 + 2);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap);
    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(gpu_heap);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device,
              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpu_handle;
        view.Format = DXGI_FORMAT_R32_TYPELESS;
        view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view.Buffer.FirstElement = 0;
        view.Buffer.NumElements = 16 * 8;
        view.Buffer.StructureByteStride = 0;
        view.Buffer.CounterOffsetInBytes = 0;
        view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        ID3D12Device_CreateUnorderedAccessView(context.device, output_buffer, NULL, &view, h);
        h.ptr += descriptor_size;
        view.Buffer.NumElements = 16 * 12;
        ID3D12Device_CreateUnorderedAccessView(context.device, read_output_buffer, NULL, &view, h);
    }

    for (i = 0; i < 8; i++)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpu_handle;
        view.Format = DXGI_FORMAT_R32_TYPELESS;
        view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view.Buffer.FirstElement = 16 * i;
        view.Buffer.NumElements = 8;
        view.Buffer.StructureByteStride = 0;
        view.Buffer.CounterOffsetInBytes = 0;
        view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        h.ptr += (2 + i) * descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(context.device, output_buffer, NULL, &view, h);
    }

    ID3D12Device_CopyDescriptorsSimple(context.device, 8 + 2,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu_heap),
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &gpu_heap);
    {
        const UINT clear_value[4] = { 0xaaaaaaaau, 0xaaaaaaaau, 0xaaaaaaaau, 0xaaaaaaaau };
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(command_list,
                gpu_handle, cpu_handle, output_buffer,
                clear_value, 0, NULL);
        uav_barrier(command_list, output_buffer);
    }

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0, gpu_handle);

    ID3D12GraphicsCommandList_SetPipelineState(command_list, write_pso_32bit);
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);
    if (write_pso_16bit)
    {
        ID3D12GraphicsCommandList_SetPipelineState(command_list, write_pso_16bit);
        ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);
    }

    uav_barrier(command_list, output_buffer);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, read_pso_32bit_smem);
    ID3D12GraphicsCommandList_Dispatch(command_list, 64, 1, 1);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, read_pso_32bit);
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);
    if (read_pso_16bit)
    {
        ID3D12GraphicsCommandList_SetPipelineState(command_list, read_pso_16bit);
        ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);
    }

    transition_resource_state(command_list, output_buffer,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    for (i = 0; i < 4; i++)
    {
        uint32_t value, expected;
        for (j = 0; j < 16; j++)
        {
            value = get_readback_uint(&rb, 16 * i + j, 0, 0);
            expected = j < 8 ? j : 0xaaaaaaaau;
            ok(value == expected, "32-bit value %u, %u: #%x != #%x.\n", i, j, value, expected);
        }
    }

    for (i = 4; i < 8; i++)
    {
        uint16_t value, expected;
        for (j = 0; j < 32; j++)
        {
            value = get_readback_uint16(&rb, 32 * i + j, 0);
            expected = options4.Native16BitShaderOpsSupported && j < 16 ? j : 0xaaaau;
            ok(value == expected, "16-bit value %u, %u: #%x != #%x.\n", i, j, value, expected);
        }
    }

    release_resource_readback(&rb);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, read_output_buffer,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(read_output_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    for (i = 0; i < 4; i++)
    {
        uint32_t value, expected;
        for (j = 0; j < 16; j++)
        {
            value = get_readback_uint(&rb, 16 * i + j, 0, 0);
            expected = j < 8 ? j : 0;
            ok(value == expected, "32-bit value %u, %u: #%x != #%x.\n", i, j, value, expected);
        }
    }

    for (i = 4; i < 8; i++)
    {
        uint16_t value, expected;
        for (j = 0; j < 32; j++)
        {
            value = get_readback_uint16(&rb, 32 * i + j, 0);
            expected = options4.Native16BitShaderOpsSupported && j < 16 ? j : 0;
            ok(value == expected, "16-bit value %u, %u: #%x != #%x.\n", i, j, value, expected);
        }
    }

    for (i = 8; i < 12; i++)
    {
        uint32_t value, expected;
        for (j = 0; j < 16; j++)
        {
            value = get_readback_uint(&rb, 16 * i + j, 0, 0);
            expected = j < 8 ? j : 0;
            ok(value == expected, "32-bit smem value %u, %u: #%x != #%x.\n", i, j, value, expected);
        }
    }

    release_resource_readback(&rb);
    ID3D12DescriptorHeap_Release(gpu_heap);
    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12Resource_Release(output_buffer);
    ID3D12Resource_Release(read_output_buffer);
    ID3D12PipelineState_Release(write_pso_32bit);
    if (write_pso_16bit)
        ID3D12PipelineState_Release(write_pso_16bit);
    ID3D12PipelineState_Release(read_pso_32bit);
    ID3D12PipelineState_Release(read_pso_32bit_smem);
    if (read_pso_16bit)
        ID3D12PipelineState_Release(read_pso_16bit);
    destroy_test_context(&context);
}

static void test_buffers_oob_behavior(bool use_dxil)
{
    ID3D12DescriptorHeap *heap, *aux_cpu_heap, *aux_gpu_heap;
    const unsigned int chunk_size = 4 * 8 * 12 * 16;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[1];
    D3D12_ROOT_PARAMETER root_parameters[1];

    ID3D12Resource *output_buffer;
    struct resource_readback rb;

    const unsigned int chunk_size_words = chunk_size / 4;
    unsigned int i, j, word_index, descriptor_size;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

#include "shaders/robustness/headers/buffers_oob_behavior.h"

    if (!init_compute_test_context(&context))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    command_list = context.list;
    queue = context.queue;

    root_signature_desc.NumParameters = 1;
    root_signature_desc.Flags = 0;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;

    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[0].NumDescriptors = UINT_MAX;
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    output_buffer = create_default_buffer(context.device, chunk_size * 32, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    context.pipeline_state = create_compute_pipeline_state(context.device,
        context.root_signature, use_dxil ? buffers_oob_behavior_dxil : buffers_oob_behavior_dxbc);

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 512);
    aux_cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    aux_gpu_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    /* Exhaustively test all combinations of stride and offset within a 16 byte boundary. */
    for (j = 0; j < 4; j++)
    {
        for (i = 0; i < 4; i++)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC view;
            D3D12_CPU_DESCRIPTOR_HANDLE h = cpu_handle;
            unsigned int stride = 4 + 4 * j;
            view.Format = DXGI_FORMAT_UNKNOWN;
            view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            view.Buffer.FirstElement = (j * 4 + i) * (chunk_size / stride) + 1; /* Offset by one element always for more test coverage. */
            view.Buffer.NumElements = 1 + i;
            view.Buffer.StructureByteStride = stride;
            view.Buffer.CounterOffsetInBytes = 0;
            view.Buffer.Flags = 0;
            h.ptr += (j * 4 + i) * descriptor_size;
            ID3D12Device_CreateUnorderedAccessView(context.device, output_buffer, NULL, &view, h);
        }
    }

    for (i = 0; i < 16; i++)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpu_handle;
        view.Format = DXGI_FORMAT_R32_TYPELESS;
        view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view.Buffer.FirstElement = (16 + i) * chunk_size_words;
        view.Buffer.NumElements = 4;
        view.Buffer.StructureByteStride = 0;
        view.Buffer.CounterOffsetInBytes = 0;
        view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        h.ptr += (i + 16) * descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(context.device, output_buffer, NULL, &view, h);
    }

    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &aux_gpu_heap);
    {
        const UINT clear_value[4] = { 0xaaaaaaaau, 0xaaaaaaaau, 0xaaaaaaaau, 0xaaaaaaaau };
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc, gpu_desc;
        D3D12_UNORDERED_ACCESS_VIEW_DESC view;
        D3D12_RESOURCE_BARRIER barrier;

        cpu_desc = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(aux_cpu_heap);
        gpu_desc = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(aux_gpu_heap);
        view.Format = DXGI_FORMAT_R32_TYPELESS;
        view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view.Buffer.FirstElement = 0;
        view.Buffer.NumElements = chunk_size_words * 32;
        view.Buffer.StructureByteStride = 0;
        view.Buffer.CounterOffsetInBytes = 0;
        view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        ID3D12Device_CreateUnorderedAccessView(context.device, output_buffer, NULL, &view, cpu_desc);
        ID3D12Device_CreateUnorderedAccessView(context.device, output_buffer, NULL, &view, gpu_desc);
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(command_list,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(aux_gpu_heap),
                cpu_desc, output_buffer, clear_value, 0, NULL);

        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.UAV.pResource = output_buffer;
        ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barrier);
    }

    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0, gpu_handle);
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);

    transition_resource_state(command_list, output_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    for (j = 0; j < 4; j++)
    {
        unsigned int stride_words = 1 + j;

        for (i = 0; i < 4; i++)
        {
            unsigned int base_offset_words = chunk_size_words * (j * 4 + i) + stride_words;
            unsigned int num_elements = 1 + i;
            unsigned int num_words = num_elements * stride_words;

            for (word_index = 0; word_index < num_words; word_index++)
            {
                UINT value = get_readback_uint(&rb, base_offset_words + word_index, 0, 0);
                UINT reference = word_index;
                ok(value == reference, "Readback value for structured buffer iteration (%u, %u, %u) is: %u\n", j, i, word_index, value);
            }

            /* Read beyond and verify we got robustness behavior. */
            for (word_index = 0; word_index < 16; word_index++)
            {
                UINT value = get_readback_uint(&rb, base_offset_words + word_index + num_words, 0, 0);
                UINT reference = 0xaaaaaaaau;
                ok(value == reference, "Readback value for boundary check iteration (%u, %u, %u) is: %u\n", j, i, word_index, value);
            }
        }
    }

    {
        UINT expected_block[16][8];
        memset(expected_block, 0xaa, sizeof(expected_block));
#define STORE1(desc, word, value) do { if ((word) < 4) expected_block[desc][word] = value; } while(0)
#define STORE2(desc, word, value0, value1) STORE1(desc, word, value0); STORE1(desc, (word) + 1, value1)
#define STORE3(desc, word, value0, value1, value2) STORE2(desc, word, value0, value1); STORE1(desc, (word) + 2, value2)
#define STORE4(desc, word, value0, value1, value2, value3) STORE3(desc, word, value0, value1, value2); STORE1(desc, (word) + 3, value3)
#define STAMP(i, j) (4 * (j) + (i))

        /* Do whatever the shader is doing, apply 16 byte robustness per 32-bit access. */
        for (j = 0; j < 4; j++)
            for (i = 0; i < 4; i++)
                STORE1(4 * 0 + j, i + j, STAMP(i, j));

        for (j = 0; j < 4; j++)
            for (i = 0; i < 4; i++)
                STORE2(4 * 1 + j, i + j, 2 * STAMP(i, j) + 0, 2 * STAMP(i, j) + 1);

        for (j = 0; j < 4; j++)
            for (i = 0; i < 4; i++)
                STORE3(4 * 2 + j, i + j, 3 * STAMP(i, j) + 0, 3 * STAMP(i, j) + 1, 3 * STAMP(i, j) + 2);

        for (j = 0; j < 4; j++)
            for (i = 0; i < 4; i++)
                STORE4(4 * 3 + j, i + j, 4 * STAMP(i, j) + 0, 4 * STAMP(i, j) + 1, 4 * STAMP(i, j) + 2, 4 * STAMP(i, j) + 3);

#undef STORE1
#undef STORE2
#undef STORE3
#undef STORE4
#undef STAMP

        for (j = 0; j < 4; j++)
        {
            for (i = 0; i < 4; i++)
            {
                unsigned int base_offset_words = chunk_size_words * (16 + 4 * j + i);

                for (word_index = 0; word_index < 8; word_index++)
                {
                    UINT value = get_readback_uint(&rb, base_offset_words + word_index, 0, 0);
                    UINT reference = expected_block[4 * j + i][word_index];
                    ok(value == reference, "Readback value for raw buffer iteration (%u, %u) is: %u\n", i, word_index, value);
                }
            }
        }
    }

    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    ID3D12Resource_Release(output_buffer);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12DescriptorHeap_Release(aux_cpu_heap);
    ID3D12DescriptorHeap_Release(aux_gpu_heap);
    destroy_test_context(&context);
}

void test_buffers_oob_behavior_dxbc(void)
{
    test_buffers_oob_behavior(false);
}

void test_buffers_oob_behavior_dxil(void)
{
    test_buffers_oob_behavior(true);
}

static void test_undefined_structured_raw_alias(bool use_dxil)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_ROOT_PARAMETER root_parameters[1];
    struct resource_readback rb;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12Resource *output[16];
    ID3D12Resource *input;
    unsigned int i, j;
    bool is_amd_win;
    bool is_nv_win;
    bool is_nv;

#include "shaders/robustness/headers/undefined_structured_raw_alias.h"

    if (!init_compute_test_context(&context))
        return;
    is_nv = is_nvidia_device(context.device);
    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 32);

    memset(&srv_desc, 0, sizeof(srv_desc));
    memset(&uav_desc, 0, sizeof(uav_desc));
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    memset(descriptor_ranges, 0, sizeof(descriptor_ranges));
    memset(root_parameters, 0, sizeof(root_parameters));
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(descriptor_ranges);
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;

    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].NumDescriptors = 16;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[1].NumDescriptors = 16;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 16;

    create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature,
            use_dxil ? undefined_structured_raw_alias_dxil : undefined_structured_raw_alias_dxbc);

    {
        uint32_t buffer[1024];
        for (i = 0; i < ARRAY_SIZE(buffer); i++)
            buffer[i] = i;
        input = create_upload_buffer(context.device, sizeof(buffer), buffer);
    }

    for (i = 0; i < ARRAY_SIZE(output); i++)
    {
        output[i] = create_default_buffer(context.device, 1024 * sizeof(uint32_t),
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    for (i = 0; i < 8; i++)
    {
        srv_desc.Buffer.StructureByteStride = 4 * (i + 1);
        srv_desc.Buffer.FirstElement = 1;
        srv_desc.Buffer.NumElements = 16;

        ID3D12Device_CreateShaderResourceView(context.device, input, &srv_desc,
                get_cpu_descriptor_handle(&context, heap, i));

        uav_desc.Buffer.StructureByteStride = 4 * (i + 1);
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.NumElements = 17;

        ID3D12Device_CreateUnorderedAccessView(context.device, output[i], NULL, &uav_desc,
                get_cpu_descriptor_handle(&context, heap, i + 16));
    }

    for (i = 0; i < 8; i++)
    {
        srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
        srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        srv_desc.Buffer.StructureByteStride = 0;
        srv_desc.Buffer.FirstElement = 4;
        srv_desc.Buffer.NumElements = 4 * (i + 1);

        ID3D12Device_CreateShaderResourceView(context.device, input, &srv_desc,
                get_cpu_descriptor_handle(&context, heap, i + 8));

        uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
        uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        uav_desc.Buffer.StructureByteStride = 0;
        uav_desc.Buffer.FirstElement = 0;
        uav_desc.Buffer.NumElements = 4 * (i + 2);

        ID3D12Device_CreateUnorderedAccessView(context.device, output[i + 8], NULL, &uav_desc,
                get_cpu_descriptor_handle(&context, heap, i + 24));
    }

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

    /* AMD behavior: RAW emits a descriptor without stride. Typed always reads same value at offset = 0.
     * Structured: Passed down as stride to typed. */
    is_amd_win = is_amd_windows_device(context.device);

    /* NV behavior: This mostly comes down to how robustness is determined. */
    is_nv_win = is_nvidia_windows_device(context.device);

    /* Validate RAW aliasing. */
    for (i = 0; i < 8; i++)
    {
        unsigned int vecsize = (i / 2) + 1;
        unsigned int stride_dwords = i + 1;
        bool expected_failure;

        transition_resource_state(context.list, output[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output[i], DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        /* This will fail. */
        expected_failure = is_nv && !is_nv_win && vecsize % stride_dwords && vecsize != 3;

        if (!expected_failure)
        {
            for (j = 0; j < 256; j++)
            {
                unsigned int element_index = j / stride_dwords;
                uint32_t value, expected = 0;

                if (is_nv_win)
                {
                    /* Extremely awkward behavior. I cannot deduce HW behavior from this alone. */
                    if (stride_dwords <= 4)
                    {
                        if (j < 16)
                            expected = stride_dwords + j;

                        /* The dummy UAV texel buffer seems to always be scalar. */
                        if (element_index < 17)
                            expected |= (j / vecsize) << 24;

                        /* Unexplainable behavior. Might have something to do with edge condition, since 17 * 3 == 51?
                         * Another theory is that 17 * 3 is realigned to 2 elements in descriptor for whatever reason.
                         * Possibly related to 16-bit storage support since it can split a dword in two ... ? */
                        if (j == 50 && stride_dwords == 3)
                            expected = 0;
                    }
                    else if (stride_dwords == 6 || stride_dwords == 8)
                    {
                        /* 32, not 16. Likely caused by the equivalent texel buffer format being chosen as RGB32 and RGBA32, splitting the stride in half. */
                        if (j < 32)
                            expected = stride_dwords + j;
                        if (element_index < 17)
                            expected |= (j / vecsize) << 24;
                    }
                    else if (stride_dwords == 7)
                    {
                        /* Driver picks R32_UINT here, and likely adjusts the element count appropriately. */
                        if (element_index < 16)
                            expected = stride_dwords + j;
                        if (j < 16 * stride_dwords + 4) /* This makes zero sense. This implies some kind of sliced write. */
                            expected |= (j / vecsize) << 24;
                    }
                    else if (stride_dwords == 5)
                    {
                        /* Driver picks R32_UINT here. */
                        if (element_index < 16)
                            expected = stride_dwords + j;
                        if (element_index < 17)
                            expected |= (j / vecsize) << 24;
                    }
                }
                else
                {
                    /* We bypass any robustness here on AMD. */
                    if (element_index < 16 || (is_amd_win && j < 64 * vecsize))
                        expected = stride_dwords + j;
                    if (element_index < 17 || (is_amd_win && j < 64 * vecsize))
                        expected |= (j / vecsize) << 24;
                }

                value = get_readback_uint(&rb, j, 0, 0);
                ok(value == expected, "Structured: output %u, index %u, expected %u, got %u\n", i, j, expected, value);
            }
        }

        release_resource_readback(&rb);
    }

    /* Validate Structured aliasing. */
    for (i = 0; i < 8; i++)
    {
        unsigned int output_dword_range = 4 * (i + 2);
        unsigned int input_dword_range = 4 * (i + 1);
        unsigned int vecsize = (i / 2) + 1;
        bool expected_failure;

        transition_resource_state(context.list, output[i + 8], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output[i + 8], DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

        for (j = 0; j < 256; j++)
        {
            uint32_t value, expected = 0;
            unsigned int base_j;

            base_j = j - j % vecsize;

            /* This will fail. NV bounds check is all or nothing, and this would require partial OOB checks. */
            expected_failure = is_nv && !is_nv_win &&
                    ((base_j < input_dword_range && base_j + vecsize > input_dword_range) ||
                    (base_j < output_dword_range && base_j + vecsize > output_dword_range));
            if (expected_failure)
                continue;

            if (is_nv_win)
            {
                /* NumElements seems to be interpreted 1:1 for SRV. Stride is pulled from shader. */
                if (j / vecsize < input_dword_range)
                    expected = 4 + j;

                /* For UAV, it seems more like driver is doing byte based checks? Somehow it seems to be able to do per-component robustness, which is very surprising. */
                if (j < output_dword_range)
                    expected |= (j / vecsize) << 24;
                else
                    expected = 0;
            }
            else if (is_amd_win)
            {
                /* Very quirky behavior. Stride = 0 here, so all threads write and race. Last thread seems to win (9070xt).
                 * Thread 63 reads and writes at offset = 0 since stride = 0, so this is working as expected. */
                if (j < vecsize)
                    expected = (0x3f << 24) | (4 + j);
            }
            else
            {
                if (j < input_dword_range)
                    expected |= 4 + j;
                if (j < output_dword_range)
                    expected |= (j / vecsize) << 24;
            }

            value = get_readback_uint(&rb, j, 0, 0);
            ok(value == expected, "RAW: output %u, index %u, expected %u, got %u\n", i, j, expected, value);
        }

        reset_command_list(context.list, context.allocator);
        release_resource_readback(&rb);
    }

    for (i = 0; i < ARRAY_SIZE(output); i++)
        ID3D12Resource_Release(output[i]);
    ID3D12Resource_Release(input);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_undefined_structured_raw_alias_dxbc(void)
{
    test_undefined_structured_raw_alias(false);
}

void test_undefined_structured_raw_alias_dxil(void)
{
    test_undefined_structured_raw_alias(true);
}

static void test_undefined_structured_raw_read_typed(bool use_dxil)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_ROOT_PARAMETER root_parameters[1];
    struct resource_readback rb;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12Resource *output[16];
    ID3D12Resource *input;
    unsigned int i, j;
    bool is_amd_win;
    bool is_nv_heap;

#include "shaders/robustness/headers/undefined_structured_raw_read_typed.h"

    if (!init_compute_test_context(&context))
        return;
    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 32);

    memset(&srv_desc, 0, sizeof(srv_desc));
    memset(&uav_desc, 0, sizeof(uav_desc));
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    memset(descriptor_ranges, 0, sizeof(descriptor_ranges));
    memset(root_parameters, 0, sizeof(root_parameters));
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(descriptor_ranges);
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;

    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].NumDescriptors = 16;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[1].NumDescriptors = 16;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 16;

    create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature,
            use_dxil ? undefined_structured_raw_read_typed_dxil : undefined_structured_raw_read_typed_dxbc);

    {
        uint32_t buffer[1024];
        for (i = 0; i < ARRAY_SIZE(buffer); i++)
            buffer[i] = i;
        input = create_upload_buffer(context.device, sizeof(buffer), buffer);
    }

    for (i = 0; i < ARRAY_SIZE(output); i++)
    {
        output[i] = create_default_buffer(context.device, 1024 * sizeof(uint32_t),
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    uav_desc.Buffer.StructureByteStride = 16;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.NumElements = 64;

    for (i = 0; i < 8; i++)
    {
        srv_desc.Buffer.StructureByteStride = 4 * (i + 1);
        srv_desc.Buffer.FirstElement = 1;
        srv_desc.Buffer.NumElements = 5;

        ID3D12Device_CreateShaderResourceView(context.device, input, &srv_desc,
                get_cpu_descriptor_handle(&context, heap, i));

        ID3D12Device_CreateUnorderedAccessView(context.device, output[i], NULL, &uav_desc,
                get_cpu_descriptor_handle(&context, heap, i + 16));
    }

    for (i = 0; i < 8; i++)
    {
        srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
        srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        srv_desc.Buffer.StructureByteStride = 0;
        srv_desc.Buffer.FirstElement = 4;
        srv_desc.Buffer.NumElements = 4 * (i + 1);

        ID3D12Device_CreateShaderResourceView(context.device, input, &srv_desc,
                get_cpu_descriptor_handle(&context, heap, i + 8));

        ID3D12Device_CreateUnorderedAccessView(context.device, output[i + 8], NULL, &uav_desc,
                get_cpu_descriptor_handle(&context, heap, i + 24));
    }

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

    /* AMD behavior: RAW emits a descriptor without stride. Typed always reads same value at offset = 0.
     * Structured: Passed down as stride to typed. */
    is_amd_win = is_amd_windows_device(context.device);
    is_nv_heap = is_nvidia_device(context.device) &&
            is_vk_device_extension_supported(context.device, "VK_EXT_descriptor_heap");

    /* Validate structured. */
    for (i = 0; i < 8; i++)
    {
        unsigned int vecsize = (i / 4) + 1;
        unsigned int stride_dwords = i + 1;
        unsigned int in_bounds_dwords;

        in_bounds_dwords = stride_dwords * 5;

        transition_resource_state(context.list, output[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output[i], DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        for (j = 0; j < 64; j++)
        {
            struct uvec4 expected = {0};
            const struct uvec4 *value;

            if (is_amd_win)
            {
                /* Robustness is in terms of elements. */
                if (j < 5)
                    expected.x = stride_dwords + j * stride_dwords;

                if (stride_dwords > 4)
                {
                    /* Seems to be some kind of HW quirk. Observed on 9070xt.
                     * This implies more than 4 components for a texel buffer, which is bogus. */
                    expected.y = expected.x;
                    expected.z = expected.x;
                    expected.w = expected.x;
                }
            }
            else if (is_nv_heap)
            {
                /* Magic driver behavior. SSBO is expressed as a R32_UINT texel buffer when read as one. */
                expected.x = j < in_bounds_dwords ? (j + stride_dwords) : 0;
            }
            else
            {
                /* NV native behavior. We try to match this in vkd3d-proton since it's implementable. */
                if (i >= 4 && i < 8)
                {
                    /* Odd-ball case for structured stride > 16.
                     * The driver behavior seems to be that the largest texel buffer format that aligns to the stride is chosen. */

                    if (stride_dwords == 5 || stride_dwords == 7)
                    {
                        if (j < in_bounds_dwords)
                        {
                            /* R32_UINT */
                            expected.x = stride_dwords + j;
                        }
                    }
                    else if (stride_dwords == 6)
                    {
                        if (j < in_bounds_dwords / 3)
                        {
                            /* R32G32B32_UINT */
                            expected.x = stride_dwords + j * 3;
                            expected.y = expected.x + 1;
                            expected.z = expected.y + 1;
                        }
                    }
                    else if (stride_dwords == 8)
                    {
                        /* R32G32B32A32_UINT */
                        if (j < in_bounds_dwords / 4)
                        {
                            expected.x = stride_dwords + j * 4;
                            expected.y = expected.x + 1;
                            expected.z = expected.y + 1;
                            expected.w = expected.z + 1;
                        }
                    }
                }
                else
                {
                    /* Robustness is in terms of elements. Texel buffer format seems 1:1 with stride. */
                    if (j < 5)
                        expected.x = stride_dwords + j * stride_dwords;
                }
            }

            if (vecsize <= 1)
                expected.y = expected.x;
            if (vecsize <= 2)
                expected.z = expected.y;
            if (vecsize <= 3)
                expected.w = expected.z;

            /* We also test reading as float in shader here. It would ideally behave similar to R32_UINT bitwise. */
            if (vecsize == 4)
                expected.w = 1;

            value = get_readback_uvec4(&rb, j, 0);
            ok(compare_uvec4(value, &expected),
                    "output %u, index %u, expected (%u, %u, %u, %u), got (%u, %u, %u, %u)\n", i, j,
                    expected.x, expected.y, expected.z, expected.w,
                    value->x, value->y, value->z, value->w);
        }

        release_resource_readback(&rb);
    }

    /* Validate raw. */
    for (i = 0; i < 8; i++)
    {
        unsigned int vecsize = ((i + 8) / 4) + 1;
        unsigned int in_bounds_dwords;

        in_bounds_dwords = (i + 1) * 4;

        transition_resource_state(context.list, output[i + 8], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output[i + 8], DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

        for (j = 0; j < 64; j++)
        {
            struct uvec4 expected = {0};
            const struct uvec4 *value;

            if (is_amd_win)
            {
                /* No stride, so it reads same data. */
                expected.x = 4;
                expected.y = 4;
                expected.z = 4;
                expected.w = 4;
            }
            else
            {
                if (j < in_bounds_dwords)
                {
                    /* Assuming that typed side is a R32_UINT texel buffer. This seems to match NV behavior too. */
                    expected.x = 4 + j;
                }
            }

            if (vecsize <= 1)
                expected.y = expected.x;
            if (vecsize <= 2)
                expected.z = expected.y;
            if (vecsize <= 3)
                expected.w = expected.z;

            if (vecsize == 4 && !is_amd_win)
                expected.w = 1;

            value = get_readback_uvec4(&rb, j, 0);
            ok(compare_uvec4(value, &expected),
                    "RAW: output %u, index %u, expected (%u, %u, %u, %u), got (%u, %u, %u, %u)\n", i, j,
                    expected.x, expected.y, expected.z, expected.w,
                    value->x, value->y, value->z, value->w);
        }

        reset_command_list(context.list, context.allocator);
        release_resource_readback(&rb);
    }

    for (i = 0; i < ARRAY_SIZE(output); i++)
        ID3D12Resource_Release(output[i]);
    ID3D12Resource_Release(input);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_undefined_structured_raw_read_typed_dxbc(void)
{
    test_undefined_structured_raw_read_typed(false);
}

void test_undefined_structured_raw_read_typed_dxil(void)
{
    test_undefined_structured_raw_read_typed(true);
}

static void test_undefined_typed_read_structured_raw(bool use_dxil)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_ROOT_PARAMETER root_parameters[1];
    struct resource_readback rb;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12Resource *output[16];
    ID3D12Resource *input;
    unsigned int i, j;
    bool is_amd_win;
    bool is_nv_heap;
    bool is_nv_win;

#include "shaders/robustness/headers/undefined_typed_read_structured_raw.h"

    if (!init_compute_test_context(&context))
        return;
    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 32);

    memset(&srv_desc, 0, sizeof(srv_desc));
    memset(&uav_desc, 0, sizeof(uav_desc));
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    memset(descriptor_ranges, 0, sizeof(descriptor_ranges));
    memset(root_parameters, 0, sizeof(root_parameters));
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(descriptor_ranges);
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;

    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].NumDescriptors = 16;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[1].NumDescriptors = 16;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 16;

    create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature,
            use_dxil ? undefined_typed_read_structured_raw_dxil : undefined_typed_read_structured_raw_dxbc);

    {
        uint32_t buffer[1024];
        for (i = 0; i < ARRAY_SIZE(buffer); i++)
            buffer[i] = i;
        input = create_upload_buffer(context.device, sizeof(buffer), buffer);
    }

    for (i = 0; i < ARRAY_SIZE(output); i++)
    {
        output[i] = create_default_buffer(context.device, 1024 * sizeof(uint32_t),
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    uav_desc.Buffer.StructureByteStride = 16;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.NumElements = 64;

    for (i = 0; i < 16; i++)
    {
        /* Commit every possible GPU crime. */
        static const DXGI_FORMAT formats[] = {
            DXGI_FORMAT_R8G8B8A8_UINT,
            DXGI_FORMAT_R16G16_UINT,
            DXGI_FORMAT_R16G16B16A16_UINT,
            DXGI_FORMAT_R32G32_UINT,
            DXGI_FORMAT_R32G32B32_UINT,
            DXGI_FORMAT_R32G32B32_FLOAT,
            DXGI_FORMAT_R32G32B32A32_UINT,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R16G16_UNORM,
            DXGI_FORMAT_R16G16B16A16_UNORM,
            DXGI_FORMAT_R32G32_SINT,
            DXGI_FORMAT_R32G32B32_SINT,
            DXGI_FORMAT_R32G32B32_FLOAT,
            DXGI_FORMAT_R32G32B32A32_SINT,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
        };

        srv_desc.Format = formats[i];
        srv_desc.Buffer.FirstElement = 4;
        srv_desc.Buffer.NumElements = 12;

        ID3D12Device_CreateShaderResourceView(context.device, input, &srv_desc,
                get_cpu_descriptor_handle(&context, heap, i));

        ID3D12Device_CreateUnorderedAccessView(context.device, output[i], NULL, &uav_desc,
                get_cpu_descriptor_handle(&context, heap, i + 16));
    }

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

    /* AMD behavior: Typed buffer has stride + element count. Structured buffer works as expected.
     * RAW access works as expected, except that robustness is completely disabled. */
    is_amd_win = is_amd_windows_device(context.device);
    is_nv_win = is_nvidia_windows_device(context.device);
    is_nv_heap = is_nvidia_device(context.device) &&
            is_vk_device_extension_supported(context.device, "VK_EXT_descriptor_heap");

    /* Validate the buffer read. */
    for (i = 0; i < 16; i++)
    {
        unsigned int vecsize = ((i & 7) / 2) + 1;

        transition_resource_state(context.list, output[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output[i], DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        for (j = 0; j < 64; j++)
        {
            struct uvec4 expected = {0};
            const struct uvec4 *value;
            bool is_bug = false;

            if (is_nv_heap && i >= 8)
            {
                /* Structured buffer path. */
                unsigned int ssbo_size_words = 12;
                unsigned int ssbo_offset_words = vecsize * 4;
                unsigned int accessed_word = j * vecsize;

                /* SSBO size is treated as num elements * 4,
                 * so we always get 12 dwords here. */
                if (accessed_word < ssbo_size_words)
                {
                    expected.x = ssbo_offset_words + j * vecsize;
                    expected.y = expected.x + 1;
                    expected.z = expected.y + 1;
                    expected.w = expected.z + 1;
                    /* Driver does not seem to like RGBA8 aliasing with SSBO, but it works on native,
                     * so that's odd. */
                    is_bug = i == 8;
                }
            }
            else if (is_nv_heap)
            {
                /* For raw buffer, number of elements is treated as number of dwords. */
                if (j < 12 / vecsize)
                {
                    expected.x = vecsize * (4 + j);
                    expected.y = expected.x + 1;
                    expected.z = expected.y + 1;
                    expected.w = expected.z + 1;
                    /* Driver does not seem to like RGBA8 aliasing with SSBO, but it works on native,
                     * so that's odd. */
                    is_bug = i == 0;
                }
            }
            else if (is_nv_win && i < 8)
            {
                /* For raw buffer, number of elements is treated as number of dwords. */
                if (j < 12 / vecsize)
                {
                    expected.x = vecsize * (4 + j);
                    expected.y = expected.x + 1;
                    expected.z = expected.y + 1;
                    expected.w = expected.z + 1;
                }
            }
            else if (j < 12 || (is_amd_win && i < 8))
            {
                /* Assuming effective offset / size of the texel buffer can translate into raw domain as well. */
                expected.x = vecsize * (4 + j);
                expected.y = expected.x + 1;
                expected.z = expected.y + 1;
                expected.w = expected.z + 1;
            }

            if (vecsize <= 1)
                expected.y = expected.x;
            if (vecsize <= 2)
                expected.z = expected.y;
            if (vecsize <= 3)
                expected.w = expected.z;

            value = get_readback_uvec4(&rb, j, 0);
            bug_if(is_bug)
            ok(compare_uvec4(value, &expected),
                    "output %u, index %u, expected (%u, %u, %u, %u), got (%u, %u, %u, %u)\n", i, j,
                    expected.x, expected.y, expected.z, expected.w,
                    value->x, value->y, value->z, value->w);
        }

        release_resource_readback(&rb);
    }

    for (i = 0; i < ARRAY_SIZE(output); i++)
        ID3D12Resource_Release(output[i]);
    ID3D12Resource_Release(input);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_undefined_typed_read_structured_raw_dxbc(void)
{
    test_undefined_typed_read_structured_raw(false);
}

void test_undefined_typed_read_structured_raw_dxil(void)
{
    test_undefined_typed_read_structured_raw(true);
}

/* Older test. Only tests a very narrow subset. */
static void test_undefined_read_typed_buffer_as_untyped_simple(bool use_dxil)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_ROOT_PARAMETER root_parameters[1];
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[1];
    ID3D12DescriptorHeap *cpu_heap;
    ID3D12DescriptorHeap *heap;

    ID3D12Resource *output_buffer;
    struct resource_readback rb;

    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    unsigned int i, descriptor_size;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

#include "shaders/robustness/headers/undefined_read_typed_buffer_as_untyped.h"

    if (!init_compute_test_context(&context))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    command_list = context.list;
    queue = context.queue;

    root_signature_desc.NumParameters = 1;
    root_signature_desc.Flags = 0;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;

    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[0].NumDescriptors = UINT_MAX;
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    output_buffer = create_default_buffer(context.device, 64 * 1024, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    context.pipeline_state = create_compute_pipeline_state(context.device,
        context.root_signature, use_dxil ? undefined_read_typed_buffer_as_untyped_dxil : undefined_read_typed_buffer_as_untyped_dxbc);

    cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64);
    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap);
    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (i = 0; i < 64; i++)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpu_handle;

        /* This is not legal, but it just works on native D3D12 drivers :( */
        view.Format = DXGI_FORMAT_R32_UINT;
        view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view.Buffer.FirstElement = 4 * i;
        view.Buffer.NumElements = 4;
        view.Buffer.StructureByteStride = 0;
        view.Buffer.CounterOffsetInBytes = 0;
        view.Buffer.Flags = 0;
        h.ptr += i * descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(context.device, output_buffer, NULL, &view, h);
    }

    ID3D12Device_CopyDescriptorsSimple(context.device, 64,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap), cpu_handle,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0, gpu_handle);
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);

    transition_resource_state(command_list, output_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    for (i = 0; i < 256; i++)
    {
        UINT value = get_readback_uint(&rb, i, 0, 0);
        UINT reference = i;
        ok(value == reference, "Readback value for buffer iteration %u is: %u\n", i, value);
    }

    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    ID3D12Resource_Release(output_buffer);
    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_undefined_read_typed_buffer_as_untyped_simple_dxbc(void)
{
    test_undefined_read_typed_buffer_as_untyped_simple(false);
}

void test_undefined_read_typed_buffer_as_untyped_simple_dxil(void)
{
    test_undefined_read_typed_buffer_as_untyped_simple(true);
}

void test_null_descriptor_mismatch_type(void)
{
    /* A very cursed test. This is invalid in D3D12, but some games rely on this (or at least a subset) working ._. */
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[9];
    ID3D12DescriptorHeap *gpu_heap, *cpu_heap;
    D3D12_ROOT_PARAMETER root_parameters[2];

    ID3D12Resource *texture;
    ID3D12Resource *buffer;

    ID3D12Resource *output_buffer;
    struct resource_readback rb;

    ID3D12GraphicsCommandList *command_list;
    unsigned int i, descriptor_size;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

#include "shaders/robustness/headers/null_descriptor_mismatch_type.h"

    if (!init_compute_test_context(&context))
        return;

    command_list = context.list;
    queue = context.queue;

    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.Flags = 0;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(descriptor_ranges);
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_ranges[0];

    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].Descriptor.RegisterSpace = 3;
    root_parameters[1].Descriptor.ShaderRegister = 0;

    /* Need two idential ranges so we can alias two different resource dimensions over same table. */
    for (i = 0; i < ARRAY_SIZE(descriptor_ranges); i++)
    {
        descriptor_ranges[i].RegisterSpace = i % 3;
        descriptor_ranges[i].BaseShaderRegister = 0;
        descriptor_ranges[i].OffsetInDescriptorsFromTableStart = 512 * (i / 3);
        descriptor_ranges[i].NumDescriptors = 512;
    }

    for (i = 0; i < 3; i++)
    {
        descriptor_ranges[i + 0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptor_ranges[i + 3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptor_ranges[i + 6].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    }

    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    {
        const UINT buffer_data[] = { 1, 1, 1, 1 };
        buffer = create_default_buffer(context.device, 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        upload_buffer_data(buffer, 0, sizeof(buffer_data), buffer_data, queue, command_list);
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    {
        const UINT tex_data = 1;
        D3D12_SUBRESOURCE_DATA sub;
        sub.pData = &tex_data;
        sub.RowPitch = 1;
        sub.SlicePitch = 1;
        texture = create_default_texture2d(context.device, 1, 1, 1, 1, DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        upload_texture_data(texture, &sub, 1, queue, command_list);
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    output_buffer = create_default_buffer(context.device, 4 * 512, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature,
            null_descriptor_mismatch_type_dxbc);

    gpu_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 512 * 3);
    cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 7);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    /* Stamp out valid descriptors across the heap. */
    for (i = 0; i < 512; i++)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h;

        h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu_heap);
        h.ptr += (0 + i) * descriptor_size;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Format = DXGI_FORMAT_R32_UINT;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        view.Texture2D.MostDetailedMip = 0;
        view.Texture2D.PlaneSlice = 0;
        view.Texture2D.ResourceMinLODClamp = 0;
        ID3D12Device_CreateShaderResourceView(context.device, texture, &view, h);
    }

    for (i = 0; i < 512; i++)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h;

        h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu_heap);
        h.ptr += (512 + i) * descriptor_size;
        view.Format = DXGI_FORMAT_R32_UINT;
        view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        view.Texture2D.PlaneSlice = 0;
        view.Texture2D.MipSlice = 0;
        ID3D12Device_CreateUnorderedAccessView(context.device, texture, NULL, &view, h);
    }

    for (i = 0; i < 512; i++)
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h;

        h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu_heap);
        h.ptr += (1024 + i) * descriptor_size;
        view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(buffer);
        view.SizeInBytes = 256;
        ID3D12Device_CreateConstantBufferView(context.device, &view, h);
    }

    /* Create 7 template NULL descriptors which cover every possible descriptor type.
     * Allows us to test splat NULL descriptor copy. */
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_tex, uav_typed, uav_raw;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_tex, srv_typed, srv_raw;
        D3D12_CPU_DESCRIPTOR_HANDLE gpu_h, cpu_h;
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv;

        cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap);
        gpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu_heap);

        cbv.BufferLocation = 0;
        cbv.SizeInBytes = 0;
        ID3D12Device_CreateConstantBufferView(context.device, &cbv, cpu_h);
        ID3D12Device_CreateConstantBufferView(context.device, &cbv, gpu_h);
        cpu_h.ptr += descriptor_size;
        gpu_h.ptr += descriptor_size;

        memset(&srv_tex, 0, sizeof(srv_tex));
        srv_tex.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_tex.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_tex.Format = DXGI_FORMAT_R32_UINT;
        srv_typed = srv_tex;
        srv_typed.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_raw = srv_typed;
        srv_raw.Format = DXGI_FORMAT_R32_TYPELESS;
        srv_raw.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

        /* SRV tex null descriptor is misinterpreted on AMD Windows native when read as a buffer. */
        ID3D12Device_CreateShaderResourceView(context.device, NULL, &srv_typed /*&srv_tex*/, cpu_h);
        ID3D12Device_CreateShaderResourceView(context.device, NULL, &srv_typed /*&srv_tex*/, gpu_h);
        cpu_h.ptr += descriptor_size;
        gpu_h.ptr += descriptor_size;
        ID3D12Device_CreateShaderResourceView(context.device, NULL, &srv_typed, cpu_h);
        ID3D12Device_CreateShaderResourceView(context.device, NULL, &srv_typed, gpu_h);
        cpu_h.ptr += descriptor_size;
        gpu_h.ptr += descriptor_size;
        ID3D12Device_CreateShaderResourceView(context.device, NULL, &srv_raw, cpu_h);
        ID3D12Device_CreateShaderResourceView(context.device, NULL, &srv_raw, gpu_h);
        cpu_h.ptr += descriptor_size;
        gpu_h.ptr += descriptor_size;

        memset(&uav_tex, 0, sizeof(uav_tex));
        uav_tex.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav_tex.Format = DXGI_FORMAT_R32_UINT;
        uav_typed = uav_tex;
        uav_typed.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_raw = uav_typed;
        uav_raw.Format = DXGI_FORMAT_R32_TYPELESS;
        uav_raw.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

        /* UAV tex null descriptor is misinterpreted on AMD Windows native when read as a buffer. */
        ID3D12Device_CreateUnorderedAccessView(context.device, NULL, NULL, &uav_typed /*&uav_tex*/, cpu_h);
        ID3D12Device_CreateUnorderedAccessView(context.device, NULL, NULL, &uav_typed /*&uav_tex*/, gpu_h);
        cpu_h.ptr += descriptor_size;
        gpu_h.ptr += descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(context.device, NULL, NULL, &uav_typed, cpu_h);
        ID3D12Device_CreateUnorderedAccessView(context.device, NULL, NULL, &uav_typed, gpu_h);
        cpu_h.ptr += descriptor_size;
        gpu_h.ptr += descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(context.device, NULL, NULL, &uav_raw, cpu_h);
        ID3D12Device_CreateUnorderedAccessView(context.device, NULL, NULL, &uav_raw, gpu_h);
        cpu_h.ptr += descriptor_size;
        gpu_h.ptr += descriptor_size;
    }

    /* Copy random NULL descriptors. The types won't match, but this "happens to work" on native drivers :(.
     * The first batch of NULL descriptors were written directly, which lets us test that path as well. */
    for (i = 7; i < 512 * 3; i++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dst, src;
        dst = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu_heap);
        src = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap);
        dst.ptr += descriptor_size * i;
        src.ptr += descriptor_size * (i % 7);
        ID3D12Device_CopyDescriptorsSimple(context.device, 1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &gpu_heap);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(gpu_heap));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list, 1, ID3D12Resource_GetGPUVirtualAddress(output_buffer));
    ID3D12GraphicsCommandList_Dispatch(command_list, 512, 1, 1);

    transition_resource_state(command_list, output_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    for (i = 0; i < 512; i++)
    {
        UINT value = get_readback_uint(&rb, i, 0, 0);
        UINT reference = i;
        ok(value == reference, "Readback value [%u] is: %u\n", i, value);
    }

    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    ID3D12Resource_Release(buffer);
    ID3D12Resource_Release(texture);
    ID3D12Resource_Release(output_buffer);
    ID3D12DescriptorHeap_Release(gpu_heap);
    ID3D12DescriptorHeap_Release(cpu_heap);
    destroy_test_context(&context);
}

