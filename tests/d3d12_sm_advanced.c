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

static void run_64bit_atomics_test(struct test_context *context,
        D3D12_SHADER_BYTECODE cs,
        bool use_heap, bool use_typed)
{
    static const uint64_t inputs[] = { 1ull << 40, 3ull << 31, 3ull << 29, 1ull << 63 };
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_range[2];
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_ROOT_PARAMETER root_parameters[3];
    ID3D12RootSignature *root_signature;
    ID3D12Resource *output_texture;
    ID3D12Resource *output_buffer;
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    ID3D12Resource *input_buffer;
    uint64_t expected_values[11];
    struct resource_readback rb;
    ID3D12DescriptorHeap *heap;
    ID3D12PipelineState *pso;
    const uint64_t *values;
    unsigned int i, j;

    input_buffer = create_upload_buffer(context->device, sizeof(inputs), inputs);
    output_buffer = create_default_buffer(context->device, sizeof(expected_values),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    output_texture = create_default_texture2d(context->device,
            ARRAY_SIZE(expected_values), ARRAY_SIZE(expected_values),
            1, 1, DXGI_FORMAT_R32G32_UINT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    expected_values[0] = inputs[0] + inputs[1] + inputs[2] + inputs[3];
    expected_values[1] = ~inputs[0] & ~inputs[1] & ~inputs[2] & ~inputs[3];
    expected_values[2] = inputs[0] | inputs[1] | inputs[2] | inputs[3];
    expected_values[3] = inputs[3];
    expected_values[4] = inputs[2];
    expected_values[5] = inputs[0];
    expected_values[6] = inputs[3];
    expected_values[7] = ~0ull ^ inputs[0] ^ inputs[1] ^ inputs[2] ^ inputs[3];

    heap = NULL;

    if (use_heap)
    {
        heap = create_gpu_descriptor_heap(context->device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);
        memset(&root_signature_desc, 0, sizeof(root_signature_desc));
        memset(root_parameters, 0, sizeof(root_parameters));
        memset(descriptor_range, 0, sizeof(descriptor_range));
        root_signature_desc.NumParameters = 1;
        root_signature_desc.pParameters = &root_parameters[0];
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_parameters[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(descriptor_range);
        root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_range;
        descriptor_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptor_range[0].NumDescriptors = 1;
        descriptor_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptor_range[1].NumDescriptors = 2;
        descriptor_range[1].BaseShaderRegister = 0;
        descriptor_range[1].OffsetInDescriptorsFromTableStart = 1;

        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Buffer.FirstElement = 0;
        srv_desc.Buffer.NumElements = 4;
        srv_desc.Buffer.StructureByteStride = 8;
        h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        ID3D12Device_CreateShaderResourceView(context->device, input_buffer, &srv_desc, h);

        memset(&uav_desc, 0, sizeof(uav_desc));
        h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);

        if (use_typed)
        {
            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uav_desc.Format = DXGI_FORMAT_R32G32_UINT;
            h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context->device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            ID3D12Device_CreateUnorderedAccessView(context->device, output_texture, NULL, &uav_desc, h);

            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            memset(&uav_desc.Buffer, 0, sizeof(uav_desc.Buffer));
            uav_desc.Buffer.NumElements = ARRAY_SIZE(expected_values);
            h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context->device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            ID3D12Device_CreateUnorderedAccessView(context->device, output_buffer, NULL, &uav_desc, h);
        }
        else
        {
            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav_desc.Buffer.FirstElement = 0;
            uav_desc.Buffer.NumElements = ARRAY_SIZE(expected_values);
            uav_desc.Format = DXGI_FORMAT_UNKNOWN;
            uav_desc.Buffer.StructureByteStride = 8;

            h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context->device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            ID3D12Device_CreateUnorderedAccessView(context->device, output_buffer, NULL, &uav_desc, h);
            h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context->device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            ID3D12Device_CreateUnorderedAccessView(context->device, output_buffer, NULL, &uav_desc, h);
        }
    }
    else
    {
        memset(&root_signature_desc, 0, sizeof(root_signature_desc));
        memset(root_parameters, 0, sizeof(root_parameters));
        root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
        root_signature_desc.pParameters = root_parameters;
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_parameters[2].Descriptor.ShaderRegister = 1;
    }

    create_root_signature(context->device, &root_signature_desc, &root_signature);
    pso = create_compute_pipeline_state(context->device, root_signature, cs);
    if (heap)
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context->list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context->list, root_signature);
    if (heap)
    {
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context->list, 0,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
    }
    else
    {
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context->list, 0,
                ID3D12Resource_GetGPUVirtualAddress(input_buffer));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context->list, 1,
                ID3D12Resource_GetGPUVirtualAddress(output_buffer));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context->list, 2,
                ID3D12Resource_GetGPUVirtualAddress(output_buffer));
    }
    ID3D12GraphicsCommandList_SetPipelineState(context->list, pso);
    ID3D12GraphicsCommandList_Dispatch(context->list, 1, 1, 1);
    transition_resource_state(context->list, output_buffer,
              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffer, 0, &rb, context->queue, context->list);
    reset_command_list(context->list, context->allocator);

    values = get_readback_data(&rb, 0, 0, 0, 1);
    for (i = 0; i < 8; i++)
    {
        if (use_typed && i != 5 && i != 6)
            continue;

        ok(values[i] == expected_values[i], "Value %u = 0x%"PRIx64", expected 0x%"PRIx64"\n",
                i, values[i], expected_values[i]);
    }

    /* We're spamming exchanges or compare exchanges. There is only one winner. */
    if (!use_typed)
    {
        for (i = 8; i < ARRAY_SIZE(expected_values); i++)
        {
            for (j = 0; j < ARRAY_SIZE(inputs); j++)
                if (values[i] == inputs[j])
                    break;

            ok(j < ARRAY_SIZE(inputs), "Got value 0x%"PRIx64", but it does not exist in inputs.\n", values[i]);
        }
    }

    release_resource_readback(&rb);

    if (use_typed)
    {
        transition_resource_state(context->list, output_texture,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_texture_readback_with_command_list(output_texture, 0, &rb, context->queue, context->list);
        reset_command_list(context->list, context->allocator);

        for (i = 0; i < 8; i++)
        {
            if (i == 5 || i == 6)
                continue;
            values = get_readback_data(&rb, i, i, 0, sizeof(uint64_t));
            ok(*values == expected_values[i], "Value %u = 0x%"PRIx64", expected 0x%"PRIx64"\n",
                    i, *values, expected_values[i]);
        }

        for (i = 8; i < ARRAY_SIZE(expected_values); i++)
        {
            values = get_readback_data(&rb, i, i, 0, sizeof(uint64_t));
            for (j = 0; j < ARRAY_SIZE(inputs); j++)
                if (*values == inputs[j])
                    break;

            ok(j < ARRAY_SIZE(inputs), "Got value 0x%"PRIx64", but it does not exist in inputs.\n", *values);
        }
        release_resource_readback(&rb);
    }

    if (heap)
        ID3D12DescriptorHeap_Release(heap);
    ID3D12Resource_Release(output_texture);
    ID3D12Resource_Release(input_buffer);
    ID3D12Resource_Release(output_buffer);
    ID3D12PipelineState_Release(pso);
    ID3D12RootSignature_Release(root_signature);
}

void test_shader_sm66_64bit_atomics(void)
{
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_FEATURE_DATA_D3D12_OPTIONS11 options11;
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1;
    D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9;
    struct test_context context;
    ID3D12Device *device;
    HRESULT hr;

#include "shaders/sm_advanced/headers/cs_64bit_atomics.h"
#include "shaders/sm_advanced/headers/cs_64bit_atomics_typed.h"
#include "shaders/sm_advanced/headers/cs_64bit_atomics_shared.h"

    if (!init_compute_test_context(&context))
        return;

    device = context.device;

    memset(&options9, 0, sizeof(options9));
    memset(&options11, 0, sizeof(options11));
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS9, &options9, sizeof(options9));
    ok(SUCCEEDED(hr), "OPTIONS9 is not supported by runtime.\n");
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS11, &options11, sizeof(options11));
    ok(SUCCEEDED(hr), "OPTIONS11 is not supported by runtime.\n");
    /* For later, when we test more exotic 64-bit atomic scenarios ... */

    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));
    if (FAILED(hr) || !options1.Int64ShaderOps)
    {
        skip("64-bit shader operations not supported, skipping ...\n");
        destroy_test_context(&context);
        return;
    }

    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));
    if (FAILED(hr) || shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_6)
    {
        skip("Device does not support SM 6.6.\n");
        destroy_test_context(&context);
        return;
    }

    vkd3d_test_set_context("64-bit atomic root descriptor");
    run_64bit_atomics_test(&context, cs_64bit_atomics_dxil, false, false);
    vkd3d_test_set_context(NULL);

    if (options11.AtomicInt64OnDescriptorHeapResourceSupported)
    {
        vkd3d_test_set_context("64-bit atomic table (raw)");
        run_64bit_atomics_test(&context, cs_64bit_atomics_dxil, true, false);
        vkd3d_test_set_context(NULL);
    }
    else
        skip("AtomicInt64OnDescriptorHeapResourceSupported not set, skipping.\n");

    if (options11.AtomicInt64OnDescriptorHeapResourceSupported &&
        options9.AtomicInt64OnTypedResourceSupported)
    {
        vkd3d_test_set_context("64-bit atomic table (typed)");
        run_64bit_atomics_test(&context, cs_64bit_atomics_typed_dxil, true, true);
        vkd3d_test_set_context(NULL);
    }
    else
        skip("AtomicInt64OnTypedResourceSupported is not set, skipping.\n");

    if (options9.AtomicInt64OnGroupSharedSupported)
    {
        vkd3d_test_set_context("64-bit atomic groupshared");
        run_64bit_atomics_test(&context, cs_64bit_atomics_shared_dxil, false, false);
        vkd3d_test_set_context(NULL);
    }
    else
        skip("AtomicInt64OnGroupSharedSupported is not set, skipping.\n");

    destroy_test_context(&context);
}

void test_shader_sm66_compute_derivatives(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_STATIC_SAMPLER_DESC static_samplers[1];
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    D3D12_ROOT_PARAMETER root_parameters[1];
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    ID3D12RootSignature *root_signature;
    ID3D12Resource *result_texture;
    ID3D12Resource *input_texture;
    struct test_context context;
    struct resource_readback rb;
    ID3D12DescriptorHeap *heap;
    ID3D12PipelineState *pso;
    ID3D12Device *device;
    unsigned int i, x, y;
    HRESULT hr;

#include "shaders/sm_advanced/headers/cs_derivative_1d.h"
#include "shaders/sm_advanced/headers/cs_derivative_2d_aligned.h"
#include "shaders/sm_advanced/headers/cs_derivative_2d_unaligned.h"

    struct test
    {
        const D3D12_SHADER_BYTECODE *cs;
        unsigned int dispatch_x;
        unsigned int dispatch_y;
    };

    static const struct test tests[] =
    {
        { &cs_derivative_1d_dxil, 4, 1 },
        { &cs_derivative_2d_aligned_dxil, 2, 2 },
        { &cs_derivative_2d_unaligned_dxil, 2, 2 },
    };

    if (!init_compute_test_context(&context))
        return;

    device = context.device;

    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));
    if (FAILED(hr) || shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_6)
    {
        skip("Device does not support SM 6.6.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    memset(root_parameters, 0, sizeof(root_parameters));
    memset(descriptor_ranges, 0, sizeof(descriptor_ranges));
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(descriptor_ranges);
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[0].NumDescriptors = 1;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 1;
    descriptor_ranges[1].NumDescriptors = 1;
    root_signature_desc.NumStaticSamplers = ARRAY_SIZE(static_samplers);
    root_signature_desc.pStaticSamplers = static_samplers;
    memset(static_samplers, 0, sizeof(static_samplers));
    static_samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    static_samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    static_samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    static_samplers[0].MaxLOD = 1000.0f;
    static_samplers[0].RegisterSpace = 0;
    static_samplers[0].ShaderRegister = 0;

    create_root_signature(device, &root_signature_desc, &root_signature);
    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);

    input_texture = create_default_texture2d(device, 128, 128, 1, 8,
            DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    result_texture = create_default_texture2d(device, 16, 16, 1, 1,
            DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    {
        D3D12_SUBRESOURCE_DATA data[8];
        uint8_t buffer_data[128 * 128 * 2];
        uint8_t *pbuffer = buffer_data;
        unsigned int width;

        for (i = 0; i < ARRAY_SIZE(data); i++)
        {
            width = 128 >> i;
            memset(pbuffer, i + 1, width * width);
            data[i].RowPitch = width;
            data[i].SlicePitch = data[i].RowPitch * width;
            data[i].pData = pbuffer;
            pbuffer += width * width;
        }

        upload_texture_data(input_texture, data, 8, context.queue, context.list);
        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, input_texture,
                   D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Format = DXGI_FORMAT_R8_UNORM;
        srv_desc.Texture2D.MipLevels = 8;
        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        ID3D12Device_CreateShaderResourceView(device, input_texture, &srv_desc, cpu_handle);
    }

    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
        memset(&uav_desc, 0, sizeof(uav_desc));
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        cpu_handle.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12Device_CreateUnorderedAccessView(device, result_texture, NULL, &uav_desc, cpu_handle);
    }

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        pso = create_compute_pipeline_state(device, root_signature, *tests[i].cs);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
        ID3D12GraphicsCommandList_Dispatch(context.list, tests[i].dispatch_x, tests[i].dispatch_y, 1);
        transition_resource_state(context.list, result_texture,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_texture_readback_with_command_list(result_texture, 0, &rb, context.queue, context.list);

        for (y = 0; y < 16; y++)
        {
            for (x = 0; x < 16; x++)
            {
                const struct vec4 *values;
                struct vec4 expected;

                values = get_readback_vec4(&rb, x, y);

                expected.x = 1.0f + 2.0f; /* Expect to sample LOD 2. */
                expected.y = 1.0f / 32.0f; /* ddx_fine of UV.x is 1 / 32. */
                expected.z = 1.0f / 32.0f; /* ddy_fine of UV.y is also 1 / 32. */
                expected.w = 2.0f; /* queried LOD */

                ok(compare_vec4(values, &expected, 0),
                        "Mismatch at %u, %u: Expected (%f, %f, %f, %f), got (%f %f %f %f).\n",
                        x, y, expected.x, expected.y, expected.z, expected.w,
                        values->x, values->y, values->z, values->w);
            }
        }
        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, result_texture,
                                  D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12PipelineState_Release(pso);
        release_resource_readback(&rb);
    }
    vkd3d_test_set_context(NULL);
    ID3D12Resource_Release(input_texture);
    ID3D12Resource_Release(result_texture);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12RootSignature_Release(root_signature);
    destroy_test_context(&context);
}

void test_shader_sm66_wave_size(void)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1;
    D3D12_ROOT_PARAMETER root_parameters[2];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct test_context context;
    struct resource_readback rb;
    uint32_t expected_data[128];
    ID3D12PipelineState *pso;
    uint32_t input_data[128];
    bool supported_wave_size;
    HRESULT hr, expected_hr;
    ID3D12Resource *src;
    ID3D12Resource *dst;
    unsigned int i, j;
    uint32_t value;

    struct test
    {
        const struct D3D12_SHADER_BYTECODE *cs;
        unsigned int wave_size;
    };

#include "shaders/sm_advanced/headers/cs_wave_size_wave16.h"
#include "shaders/sm_advanced/headers/cs_wave_size_wave32.h"
#include "shaders/sm_advanced/headers/cs_wave_size_wave64.h"

    static const struct test tests[] =
    {
        { &cs_wave_size_wave16_dxil, 16 },
        { &cs_wave_size_wave32_dxil, 32 },
        { &cs_wave_size_wave64_dxil, 64 },
    };

    if (!init_compute_test_context(&context))
        return;

    if (!context_supports_dxil(&context))
    {
        skip("Context does not support DXIL.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&shader_model, 0, sizeof(shader_model));
    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) ||
            shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_6)
    {
        skip("Device does not support SM 6.6.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(root_parameters);
    rs_desc.pParameters = root_parameters;
    memset(root_parameters, 0, sizeof(root_parameters));
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    memset(&options1, 0, sizeof(options1));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        supported_wave_size = tests[i].wave_size >= options1.WaveLaneCountMin && tests[i].wave_size <= options1.WaveLaneCountMax;
        pso = NULL;
        memset(&compute_desc, 0, sizeof(compute_desc));
        compute_desc.CS = *tests[i].cs;
        compute_desc.pRootSignature = context.root_signature;

        expected_hr = supported_wave_size ? S_OK : E_INVALIDARG;
        hr = ID3D12Device_CreateComputePipelineState(context.device, &compute_desc, &IID_ID3D12PipelineState, (void**)&pso);
        ok(hr == expected_hr, "Got hr #%x, expected %#x.\n", hr, expected_hr);

        if (!supported_wave_size)
        {
            skip("WaveSize %u not supported, skipping.\n", tests[i].wave_size);
            continue;
        }

        if (SUCCEEDED(hr))
        {
            for (j = 0; j < ARRAY_SIZE(input_data); j++)
            {
                input_data[j] = 100;
                expected_data[j] = 100 * (j & (tests[i].wave_size - 1));
            }

            src = create_upload_buffer(context.device, sizeof(input_data), input_data);
            dst = create_default_buffer(context.device, sizeof(input_data) * 2,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
            ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
            ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(src));
            ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(dst));
            ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

            transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            get_buffer_readback_with_command_list(dst, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

            for (j = 0; j < ARRAY_SIZE(input_data); j++)
            {
                value = get_readback_uint(&rb, j, 0, 0);
                ok(value == expected_data[j], "Prefix sum mismatch, value %u: %u != %u\n", j, value, expected_data[j]);
                value = get_readback_uint(&rb, j + 128, 0, 0);
                ok(value == tests[i].wave_size, "Expected wave size: %u, got %u\n", tests[i].wave_size, value);
            }

            ID3D12Resource_Release(src);
            ID3D12Resource_Release(dst);
            ID3D12PipelineState_Release(pso);
            release_resource_readback(&rb);
            reset_command_list(context.list, context.allocator);
        }
    }

    vkd3d_test_set_context(NULL);
    destroy_test_context(&context);
}

void test_shader_sm66_quad_op_semantics(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    D3D12_ROOT_PARAMETER root_parameters[1];
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    ID3D12RootSignature *root_signature;
    ID3D12Resource *result_texture;
    ID3D12Resource *input_texture;
    struct test_context context;
    struct resource_readback rb;
    ID3D12DescriptorHeap *heap;
    ID3D12PipelineState *pso;
    ID3D12Device *device;
    unsigned int i, x, y;

#include "shaders/sm_advanced/headers/cs_quad_swap_2d_sm66.h"
/*#include "shaders/sm_advanced/headers/cs_quad_swap_2d_sm60.h"*/
#include "shaders/sm_advanced/headers/cs_quad_swap_2d_unaligned.h"

    struct test
    {
        const D3D12_SHADER_BYTECODE *cs;
        unsigned int dispatch_x;
        unsigned int dispatch_y;
        D3D_SHADER_MODEL minimum_shader_model;
        bool quad_swap_is_2d;
    };

    /* Quad swap ops are context sensitive based on shading model and work group size (wat).
     * Pre 6.6 it's always based on lane index.
     * 6.6+ it depends on the group size. If X and Y align with 2, we get 2D semantics based on GroupThreadID.xy,
     * otherwise, it's based on GroupThread.x.
     * Testing with unaligned X is meaningless, since not even 1D quads will work reliably in that case.
     */
    static const struct test tests[] =
    {
        /* There is no well defined mapping between threadID and lanes in pre SM6.6. This test isn't technically valid.
         * It needs to be rewritten, but it's not exactly a very exciting test either ...
         * { &cs_quad_swap_2d_sm60_dxil, 2, 2, D3D_SHADER_MODEL_6_0, false },*/
        { &cs_quad_swap_2d_sm66_dxil, 2, 2, D3D_SHADER_MODEL_6_6, true },
        { &cs_quad_swap_2d_unaligned_dxil, 2, 2, D3D_SHADER_MODEL_6_6, false },
    };

    if (!init_compute_test_context(&context))
        return;

    device = context.device;

    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    memset(root_parameters, 0, sizeof(root_parameters));
    memset(descriptor_ranges, 0, sizeof(descriptor_ranges));
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(descriptor_ranges);
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[0].NumDescriptors = 1;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 1;
    descriptor_ranges[1].NumDescriptors = 1;

    create_root_signature(device, &root_signature_desc, &root_signature);
    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);

    input_texture = create_default_texture2d(device, 16, 16, 1, 1,
            DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    result_texture = create_default_texture2d(device, 16, 16, 1, 1,
            DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    {
        D3D12_SUBRESOURCE_DATA data;
        float buffer_data[16 * 16];

        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
                buffer_data[16 * y + x] = 1.0f + 16.0f * (float)y + (float)x;

        data.RowPitch = 16 * sizeof(float);
        data.SlicePitch = data.RowPitch * 16;
        data.pData = buffer_data;
        upload_texture_data(input_texture, &data, 1, context.queue, context.list);
        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, input_texture,
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
        srv_desc.Texture2D.MipLevels = 1;
        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        ID3D12Device_CreateShaderResourceView(device, input_texture, &srv_desc, cpu_handle);
    }

    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
        memset(&uav_desc, 0, sizeof(uav_desc));
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        cpu_handle.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12Device_CreateUnorderedAccessView(device, result_texture, NULL, &uav_desc, cpu_handle);
    }

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        if (shader_model.HighestShaderModel < tests[i].minimum_shader_model)
        {
            skip("Skipping test due to insufficient shader model support.\n");
            continue;
        }

        pso = create_compute_pipeline_state(device, root_signature, *tests[i].cs);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
        ID3D12GraphicsCommandList_Dispatch(context.list, tests[i].dispatch_x, tests[i].dispatch_y, 1);
        transition_resource_state(context.list, result_texture,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_texture_readback_with_command_list(result_texture, 0, &rb, context.queue, context.list);

        for (y = 0; y < 16; y++)
        {
            for (x = 0; x < 16; x++)
            {
                const struct vec4 *values;
                struct vec4 expected;

                values = get_readback_vec4(&rb, x, y);

                if (tests[i].quad_swap_is_2d)
                {
                    expected.x = 1.0f + (float)y * 16.0f + (float)x;
                    expected.y = 1.0f + (float)y * 16.0f + (float)(x ^ 1);
                    expected.z = 1.0f + (float)(y ^ 1) * 16.0f + (float)x;
                    expected.w = 1.0f + (float)(y ^ 1) * 16.0f + (float)(x ^ 1);
                }
                else
                {
                    expected.x = 1.0f + (float)y * 16.0f + (float)x;
                    expected.y = 1.0f + (float)y * 16.0f + (float)(x ^ 1);
                    expected.z = 1.0f + (float)y * 16.0f + (float)(x ^ 2);
                    expected.w = 1.0f + (float)y * 16.0f + (float)(x ^ 3);
                }

                ok(compare_vec4(values, &expected, 0),
                        "Mismatch at %u, %u: Expected (%f, %f, %f, %f), got (%f %f %f %f).\n",
                        x, y, expected.x, expected.y, expected.z, expected.w,
                        values->x, values->y, values->z, values->w);
            }
        }
        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, result_texture,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12PipelineState_Release(pso);
        release_resource_readback(&rb);
    }
    vkd3d_test_set_context(NULL);

    ID3D12Resource_Release(input_texture);
    ID3D12Resource_Release(result_texture);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12RootSignature_Release(root_signature);
    destroy_test_context(&context);
}

void test_sv_barycentric(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 features3;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    float base_x, base_y, rot, off_x, off_y;
    D3D12_INPUT_LAYOUT_DESC input_layout;
    struct test_context_desc desc;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    unsigned int i, x, y, offset;
    struct test_context context;
    D3D12_INDEX_BUFFER_VIEW ibv;
    struct resource_readback rb;
    unsigned int coverage_count;
    float vbo_data[16 * 4 * 4];
    ID3D12PipelineState *pso;
    UINT ibo_data[5 * 4 * 4];
    ID3D12Resource *vbo;
    ID3D12Resource *ibo;
    uint32_t value;
    HRESULT hr;

#include "shaders/sm_advanced/headers/vs_barycentrics.h"
#include "shaders/sm_advanced/headers/ps_barycentrics.h"

    static const D3D12_INPUT_ELEMENT_DESC input_elems[2] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "ATTR", 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

#define BARY_RES 128

    static const D3D12_VIEWPORT vp = { 0, 0, BARY_RES, BARY_RES, 0, 1 };
    static const D3D12_RECT sci = { 0, 0, BARY_RES, BARY_RES };
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const uint8_t provoking_lut[] = {
        192, 212, 224, 244,
        128, 144, 160, 176,
        68, 80, 100, 112,
        4, 20, 36, 52,
    };
    static const float quad_lut[4][2] = {
        { -0.3f, -0.3f },
        { +0.3f, -0.3f },
        { -0.3f, +0.3f },
        { +0.3f, +0.3f },
    };

    for (i = 0; i < 16; i++)
    {
        for (x = 0; x < 4; x++)
            ibo_data[5 * i + x] = 4 * i + x;
        ibo_data[5 * i + 4] = ~0u;
    }

    for (y = 0; y < 4; y++)
    {
        for (x = 0; x < 4; x++)
        {
            offset = 16 * (4 * y + x);
            base_x = -0.75f + 0.5f * (float)x;
            base_y = -0.75f + 0.5f * (float)y;

            /* Offset by subpixels so we get consistent raster. */
            base_x += 2.0f / (64.0f * BARY_RES);
            base_y += 2.0f / (64.0f * BARY_RES);

            /* Test different rotations. */
            rot = (0.5f + ((float)(y * 4 + x) / 16.0f)) * 2.0f * M_PI;

            for (i = 0; i < 4; i++)
            {
                /* Test different winding orders. */
                off_x = quad_lut[i ^ (x & 1)][0];
                off_y = quad_lut[i ^ (x & 1)][1];

                vbo_data[offset + 4 * i + 0] = base_x + off_x * cosf(rot) - off_y * sinf(rot);
                vbo_data[offset + 4 * i + 1] = base_y + off_x * sinf(rot) + off_y * cosf(rot);

                vbo_data[offset + 4 * i + 2] = (float)(offset + 4 * i + 0.5f); /* W. Make sure different results are observed for perspective and noperspective variants. */
                vbo_data[offset + 4 * i + 3] = (float)(offset + 4 * i);
            }
        }
    }

    input_layout.NumElements = ARRAY_SIZE(input_elems);
    input_layout.pInputElementDescs = input_elems;

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    desc.no_pipeline = true;
    desc.rt_width = BARY_RES;
    desc.rt_height = BARY_RES;
    desc.rt_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (!init_test_context(&context, &desc))
        return;

    if (!context_supports_dxil(&context))
    {
        skip("Context does not support DXIL.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&features3, 0, sizeof(features3));
    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS3, &features3, sizeof(features3))) || !features3.BarycentricsSupported)
    {
        skip("Context does not support barycentrics, hr #%x.\n", hr);
        destroy_test_context(&context);
        return;
    }

    vbo = create_upload_buffer(context.device, sizeof(vbo_data), vbo_data);
    ibo = create_upload_buffer(context.device, sizeof(ibo_data), ibo_data);

    context.root_signature = create_empty_root_signature(context.device, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    init_pipeline_state_desc_dxil(&pso_desc, context.root_signature, DXGI_FORMAT_R8G8B8A8_UNORM,
            &vs_barycentrics_dxil, &ps_barycentrics_dxil, &input_layout);
    pso_desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso);
    ok(SUCCEEDED(hr), "Failed to create pipeline, hr #%u.\n", hr);

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ibv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(ibo);
    ibv.Format = DXGI_FORMAT_R32_UINT;
    ibv.SizeInBytes = sizeof(ibo_data);
    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vbo);
    vbv.StrideInBytes = 4 * sizeof(float);
    vbv.SizeInBytes = sizeof(vbo_data);
    ID3D12GraphicsCommandList_IASetIndexBuffer(context.list, &ibv);
    ID3D12GraphicsCommandList_IASetVertexBuffers(context.list, 0, 1, &vbv);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_DrawIndexedInstanced(context.list, ARRAY_SIZE(ibo_data), 1, 0, 0, 0);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(context.render_target, 0, &rb, context.queue, context.list);
    coverage_count = 0;
    for (y = 0; y < BARY_RES; y++)
    {
        for (x = 0; x < BARY_RES; x++)
        {
            value = get_readback_uint(&rb, x, y, 0);
            if ((value >> 24) != 0xff) /* If we have coverage.*/
            {
                ok((value & 0xffff) == 0, "Value for pixel %u, %u is 0x%x.\n", x, y, value);
                ok((value >> 24) == 0x80, "Barycentrics don't sum to 1, alpha bits = 0x%x.\n", value >> 24);
                coverage_count++;
            }
            else
                ok(value == ~0u, "Value for pixel %u, %u is 0x%x.\n", x, y, value);
        }
    }
    /* Make sure we have enough test coverage. */
    ok(coverage_count >= (BARY_RES * BARY_RES) / 4, "Coverage is too low = %u.\n", coverage_count);

    for (y = 0; y < 4; y++)
    {
        for (x = 0; x < 4; x++)
        {
            /* Sample at quad centers. Based on rotation we should sample the provoking vertex for either first or second strip tri. */
            value = get_readback_uint(&rb, (BARY_RES / 4) * x + (BARY_RES / 8), (BARY_RES / 4) * y + (BARY_RES / 8), 0);
            ok(provoking_lut[y * 4 + x] == ((value >> 16) & 0xff), "Quad %u, %u, expected %u, got %u.\n", x, y, provoking_lut[y * 4 + x], (value >> 16) & 0xff);
        }
    }

    release_resource_readback(&rb);
    ID3D12PipelineState_Release(pso);
    ID3D12Resource_Release(vbo);
    ID3D12Resource_Release(ibo);
    destroy_test_context(&context);
}

void test_shader_fp16(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 features4;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_ROOT_PARAMETER root_parameters[2];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12PipelineState *pso[2];
    ID3D12Resource *src;
    ID3D12Resource *dst;
    unsigned int i, j;
    uint32_t value;
    HRESULT hr;

#include "shaders/sm_advanced/headers/vs_fp16_nonnative.h"
#include "shaders/sm_advanced/headers/vs_fp16_native.h"
#include "shaders/sm_advanced/headers/ps_fp16_nonnative.h"
#include "shaders/sm_advanced/headers/ps_fp16_native.h"

    struct test
    {
        const struct D3D12_SHADER_BYTECODE *vs;
        const struct D3D12_SHADER_BYTECODE *ps;
        bool native_fp16;
        unsigned int src_offset;
        unsigned int dst_offset;
        const float *reference_fp32;
        const uint16_t *reference_fp16;
    };

    static const float reference_fp32[] = { 6.0f, 8.0f, 10.0f, 12.0f, -4.0f, -4.0f, -4.0f, -4.0f };
    static const uint16_t reference_fp16[] = { 0x4080, 0x40c0, 0x4100, 0x4140, 0xb400, 0xb400, 0xb400, 0xb400 };
    static const struct test tests[] =
    {
        { &vs_fp16_nonnative_dxil, &ps_fp16_nonnative_dxil, false, 0, 0, reference_fp32, NULL },
        { &vs_fp16_native_dxil, &ps_fp16_native_dxil, true, 32, 32, NULL, reference_fp16 },
    };

    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    D3D12_VIEWPORT vp = { 0, 0, 1, 1, 0, 1 };
    D3D12_RECT sci = { 0, 0, 1, 1 };
    uint32_t upload_data[8 + 4];

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    desc.no_pipeline = true;
    desc.rt_width = ARRAY_SIZE(tests);
    desc.rt_height = 1;
    desc.rt_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (!init_test_context(&context, &desc))
        return;

    if (!context_supports_dxil(&context))
    {
        skip("Context does not support DXIL.\n");
        destroy_test_context(&context);
        return;
    }

    {
        /* With non-native FP16 declared in the DXIL, raw buffers behave as if they are 32-bit types, despite being declared with FP16 in the shader. */
        float v;
        for (i = 0; i < 8; i++)
        {
            v = 1.0f + (float)i;
            memcpy(&upload_data[i], &v, sizeof(v));
        }
    }

    {
        /* With native FP16 (-enable-16bit-types), min16float is true FP16, so we should bake down some halfs. */
        uint16_t halfs[8];
        for (i = 0; i < 8; i++)
            halfs[i] = 0x3c00 + (i << 6);

        memcpy(upload_data + 8, halfs, sizeof(halfs));
    }
    src = create_upload_buffer(context.device, sizeof(upload_data), upload_data);
    dst = create_default_buffer(context.device, sizeof(upload_data), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    memset(&features4, 0, sizeof(features4));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS4, &features4, sizeof(features4));

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(root_parameters);
    rs_desc.pParameters = root_parameters;
    memset(root_parameters, 0, sizeof(root_parameters));
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        init_pipeline_state_desc_dxil(&pso_desc, context.root_signature, DXGI_FORMAT_R8G8B8A8_UNORM, tests[i].vs, tests[i].ps, NULL);
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso[i]);

        if (tests[i].native_fp16 && features4.Native16BitShaderOpsSupported)
            ok(SUCCEEDED(hr), "Failed to create pipeline, hr #%u.\n", hr);
        else if (tests[i].native_fp16 && !features4.Native16BitShaderOpsSupported)
        {
            ok(hr == E_INVALIDARG, "Unexpected hr: %x.\n", hr);
            skip("NativeFP16 is not supported. Failing pipeline compilation is expected.\n");
        }
        else
            ok(SUCCEEDED(hr), "Failed to create pipeline, hr #%u.\n", hr);

        if (FAILED(hr))
            pso[i] = NULL;
    }

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        if (pso[i])
        {
            ID3D12GraphicsCommandList_SetPipelineState(context.list, pso[i]);
            vp.TopLeftX = (float)i;
            sci.left = i;
            sci.right = i + 1;
            ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
            ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
            ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(src) + tests[i].src_offset);
            ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(dst) + tests[i].dst_offset);
            ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);
        }
    }

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(context.render_target, 0, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        unsigned int expected = pso[i] ? 0x80808080 : 0xffffffff;
        value = get_readback_uint(&rb, i, 0, 0);
        ok(value == expected, "0x%x != 0x%x", value, expected);
    }
    release_resource_readback(&rb);

    reset_command_list(context.list, context.allocator);
    get_buffer_readback_with_command_list(dst, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);
    for (i = 0; i < ARRAY_SIZE(upload_data); i++)
        upload_data[i] = get_readback_uint(&rb, i, 0, 0);
    release_resource_readback(&rb);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        if (!pso[i])
            continue;

        if (tests[i].native_fp16)
        {
            uint16_t f16_values[8];
            memcpy(f16_values, ((const uint8_t *)upload_data) + tests[i].dst_offset, sizeof(f16_values));
            for (j = 0; j < ARRAY_SIZE(f16_values); j++)
                ok(f16_values[j] == tests[i].reference_fp16[j], "Test %u, fp16 value %u, 0x%x != 0x%x\n", i, j, f16_values[j], tests[i].reference_fp16[j]);
        }
        else
        {
            float f32_values[8];
            memcpy(f32_values, ((const uint8_t *)upload_data) + tests[i].dst_offset, sizeof(f32_values));
            for (j = 0; j < ARRAY_SIZE(f32_values); j++)
                ok(f32_values[j] == tests[i].reference_fp32[j], "Test %u, fp32 value %u, %f != %f\n", i, j, f32_values[j], tests[i].reference_fp32[j]);
        }
    }

    ID3D12Resource_Release(src);
    ID3D12Resource_Release(dst);
    for (i = 0; i < ARRAY_SIZE(pso); i++)
        if (pso[i])
            ID3D12PipelineState_Release(pso[i]);
    destroy_test_context(&context);
}

void test_shader_sm62_denorm(void)
{
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_ROOT_PARAMETER root_parameters[2];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12PipelineState *pso;
    ID3D12Resource *src;
    ID3D12Resource *dst;
    uint32_t value;
    unsigned int i;

#include "shaders/sm_advanced/headers/cs_denorm_ftz.h"
#include "shaders/sm_advanced/headers/cs_denorm_preserve.h"

    struct test
    {
        const struct D3D12_SHADER_BYTECODE *cs;
        uint32_t input_fp32;
        uint32_t output_fp32;
    };

    static const struct test tests[] =
    {
        /* In denorms, additions work like uint32_t. */
        { &cs_denorm_ftz_dxil, 10, 0 },
        { &cs_denorm_preserve_dxil, 10, 20 },
    };

    if (!init_compute_test_context(&context))
        return;

    if (!context_supports_dxil(&context))
    {
        skip("Context does not support DXIL.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&shader_model, 0, sizeof(shader_model));
    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_2;
    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) ||
        shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_2)
    {
        skip("Device does not support SM 6.2.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(root_parameters);
    rs_desc.pParameters = root_parameters;
    memset(root_parameters, 0, sizeof(root_parameters));
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);
        pso = create_compute_pipeline_state(context.device, context.root_signature, *tests[i].cs);
        src = create_upload_buffer(context.device, sizeof(tests[i].input_fp32), &tests[i].input_fp32);
        dst = create_default_buffer(context.device, sizeof(tests[i].input_fp32), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(src));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(dst));
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

        transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(dst, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

        value = get_readback_uint(&rb, 0, 0, 0);
        ok(value == tests[i].output_fp32, "0x%x != 0x%x\n", value, tests[i].output_fp32);

        ID3D12Resource_Release(src);
        ID3D12Resource_Release(dst);
        ID3D12PipelineState_Release(pso);
        release_resource_readback(&rb);
        reset_command_list(context.list, context.allocator);
    }

    vkd3d_test_set_context(NULL);
    destroy_test_context(&context);
}

void test_shader_sm66_packed(void)
{
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 features4;
    D3D12_ROOT_PARAMETER root_parameters[2];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12PipelineState *pso;
    ID3D12Resource *src;
    ID3D12Resource *dst;
    unsigned int i, j;
    uint32_t value;

#include "shaders/sm_advanced/headers/cs_unpack_u8_u32.h"
#include "shaders/sm_advanced/headers/cs_unpack_u8_u16.h"
#include "shaders/sm_advanced/headers/cs_unpack_s8_s32.h"
#include "shaders/sm_advanced/headers/cs_unpack_s8_s16.h"
#include "shaders/sm_advanced/headers/cs_pack_u32_u8.h"
#include "shaders/sm_advanced/headers/cs_pack_u16_u8.h"
#include "shaders/sm_advanced/headers/cs_pack_s32_u8_clamp.h"
#include "shaders/sm_advanced/headers/cs_pack_s32_s8_clamp.h"
#include "shaders/sm_advanced/headers/cs_pack_s16_u8_clamp.h"
#include "shaders/sm_advanced/headers/cs_pack_s16_s8_clamp.h"

    struct test
    {
        const struct D3D12_SHADER_BYTECODE *cs;
        uint32_t input[4];
        uint32_t output[4];
        bool requires_16bit;
    };

    static const struct test tests[] =
    {
        { &cs_unpack_u8_u32_dxil, { 0xfedba917 }, { 0x17, 0xa9, 0xdb, 0xfe }, false },
        { &cs_unpack_u8_u16_dxil, { 0xfedba917 }, { 0x00a90017, 0x00fe00db }, true },
        { &cs_unpack_s8_s32_dxil, { 0xfedba917 }, { 0x17, 0xffffffa9, 0xffffffdb, 0xfffffffe }, false },
        { &cs_unpack_s8_s16_dxil, { 0xfedba917 }, { 0xffa90017, 0xfffeffdb }, true },
        { &cs_pack_u32_u8_dxil, { 0x101, 0x02, 0xfffffffe, 0x10005 }, { 0x05fe0201 }, false },
        { &cs_pack_u16_u8_dxil, { 0x00020101, 0x0005fffe }, { 0x05fe0201 }, true },
        { &cs_pack_s32_u8_clamp_dxil, { 0xff, 0x100, 0xffffffff, 0 }, { 0x0000ffff }, false },
        { &cs_pack_s32_s8_clamp_dxil, { 0xff, 0x100, 0xffffffff, 0xffff8000 }, { 0x80ff7f7f }, false },
        { &cs_pack_s16_u8_clamp_dxil, { 0x010000ff, 0x0000ffff }, { 0x0000ffff }, true },
        { &cs_pack_s16_s8_clamp_dxil, { 0x010000ff, 0x8000ffff }, { 0x80ff7f7f }, true },
    };

    if (!init_compute_test_context(&context))
        return;

    if (!context_supports_dxil(&context))
    {
        skip("Context does not support DXIL.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&shader_model, 0, sizeof(shader_model));
    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) ||
            shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_6)
    {
        skip("Device does not support SM 6.6.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(root_parameters);
    rs_desc.pParameters = root_parameters;
    memset(root_parameters, 0, sizeof(root_parameters));
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    memset(&features4, 0, sizeof(features4));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS4, &features4, sizeof(features4));

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        if (tests[i].requires_16bit && !features4.Native16BitShaderOpsSupported)
        {
            skip("Skipping unsupported test for 16-bit native operations.\n");
            continue;
        }

        pso = create_compute_pipeline_state(context.device, context.root_signature, *tests[i].cs);
        src = create_upload_buffer(context.device, sizeof(tests[i].input), tests[i].input);
        dst = create_default_buffer(context.device, sizeof(tests[i].output), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(src));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(dst));
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

        transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(dst, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

        for (j = 0; j < 4; j++)
        {
            value = get_readback_uint(&rb, j, 0, 0);
            ok(value == tests[i].output[j], "Value %u mismatch -> 0x%x != 0x%x\n", j, value, tests[i].output[j]);
        }

        ID3D12Resource_Release(src);
        ID3D12Resource_Release(dst);
        ID3D12PipelineState_Release(pso);
        release_resource_readback(&rb);
        reset_command_list(context.list, context.allocator);
    }

    vkd3d_test_set_context(NULL);
    destroy_test_context(&context);
}

void test_shader_sm64_packed(void)
{
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 features4;
    D3D12_ROOT_PARAMETER root_parameters[2];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12PipelineState *pso;
    ID3D12Resource *src;
    ID3D12Resource *dst;
    uint32_t value;
    unsigned int i;

#include "shaders/sm_advanced/headers/cs_packed_dot2add.h"
#include "shaders/sm_advanced/headers/cs_packed_u8dot.h"
#include "shaders/sm_advanced/headers/cs_packed_i8dot.h"

    struct test
    {
        const struct D3D12_SHADER_BYTECODE *cs;
        uint32_t input[3];
        uint32_t output;
    };

#define FP16_EXP2(x) ((15u + (x)) << 10)
    static const struct test tests[] =
    {
        { &cs_packed_u8dot_dxil, { 0x010203ff, 0x05ff0708, 1000 }, 1000 + 1 * 5 + 2 * 255 + 3 * 7 + 255 * 8 },
        { &cs_packed_i8dot_dxil, { 0x010203ff, 0x05ff0708, 1000 }, 1000 + 1 * 5 + 2 * (-1) + 3 * 7 + (-1) * 8 },
        { &cs_packed_u8dot_dxil, { 0x01010101, 0x01010101, 0xffffffffu }, 0x3u },
        { &cs_packed_i8dot_dxil, { 0x01010101, 0x01010101, 0x7fffffffu }, 0x80000003u },
        { &cs_packed_i8dot_dxil, { 0x01010101, 0x01010101, 0xffffffffu }, 0x00000003u },
        { &cs_packed_i8dot_dxil, { 0xffffffffu, 0x01010101, 0x80000003u }, 0x7fffffffu },
        { &cs_packed_i8dot_dxil, { 0xffffffffu, 0x01010101, 0x3u }, 0xffffffffu },
        { &cs_packed_dot2add_dxil, { FP16_EXP2(0), FP16_EXP2(0), 0x3f800000 }, 0x40000000 }, /* 2 = 1 + (1 * 1 + 0 * 0) */
        { &cs_packed_dot2add_dxil, { FP16_EXP2(0) | (FP16_EXP2(1) << 16), FP16_EXP2(2) | (FP16_EXP2(3) << 16), 0x3f800000 }, 0x41a80000 }, /* 21 = 1 + (1 * 4 + 2 * 8) */
        /* Carefully test inf behavior. Verify that the operation is acc += float(a.x * b.x) + float(a.y * b.y).
         * I.e., in precise mode, we must observe FP16 infs from multiply, expand to FP32, then add.
         * Based on careful AMD testing, dot2_f16_f32 is generated from this intrinsic,
         * and FADD is supposed to happen in > FP16, not FP16 as the docs might suggest. */
        { &cs_packed_dot2add_dxil, { FP16_EXP2(0), FP16_EXP2(15), 0x3f800000 }, 0x47000100 }, /* 32769 = 1 + 2^15 * 1 + 0 * 0 */
        { &cs_packed_dot2add_dxil, { FP16_EXP2(0), FP16_EXP2(16), 0x3f800000 }, 0x7f800000 }, /* inf = 1 + inf * 1 + 0 * 0 */
        { &cs_packed_dot2add_dxil, { FP16_EXP2(1), FP16_EXP2(15), 0x3f800000 }, 0x7f800000 }, /* inf = 1 + fp16 inf + 0 * 0 */
        { &cs_packed_dot2add_dxil, { FP16_EXP2(0) * 0x10001u, FP16_EXP2(15) * 0x10001u, 0x3f800000 }, 0x47800080 }, /* 2^16 + 1 = 1 + 2^15 + 2^15. This will inf if dot product is completed in FP16. */
        /* Verify addition order. The dot part must be completed before accumulating. If this is not done, both accumulations will round to original value. */
        { &cs_packed_dot2add_dxil, { FP16_EXP2(0) * 0x10001u, 0x3c00 * 0x10001u, 0x4b800000 }, 0x4b800001 },
    };
#undef FP16_EXP2

    if (!init_compute_test_context(&context))
        return;

    if (!context_supports_dxil(&context))
    {
        skip("Context does not support DXIL.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&shader_model, 0, sizeof(shader_model));
    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_4;
    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) ||
        shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_4)
    {
        skip("Device does not support SM 6.4.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(root_parameters);
    rs_desc.pParameters = root_parameters;
    memset(root_parameters, 0, sizeof(root_parameters));
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    memset(&features4, 0, sizeof(features4));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS4, &features4, sizeof(features4));

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        if (tests[i].cs == &cs_packed_dot2add_dxil && !features4.Native16BitShaderOpsSupported)
        {
            skip("Skipping unsupported test dot2add.\n");
            continue;
        }

        pso = create_compute_pipeline_state(context.device, context.root_signature, *tests[i].cs);
        src = create_upload_buffer(context.device, sizeof(tests[i].input), tests[i].input);
        dst = create_default_buffer(context.device, sizeof(tests[i].input), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(src));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(dst));
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

        transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(dst, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

        value = get_readback_uint(&rb, 0, 0, 0);
        ok(value == tests[i].output, "0x%x != 0x%x\n", value, tests[i].output);

        ID3D12Resource_Release(src);
        ID3D12Resource_Release(dst);
        ID3D12PipelineState_Release(pso);
        release_resource_readback(&rb);
        reset_command_list(context.list, context.allocator);
    }

    vkd3d_test_set_context(NULL);
    destroy_test_context(&context);
}

void test_shader_waveop_maximal_convergence(void)
{
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12PipelineState *pso_nonconverged;
    ID3D12PipelineState *pso_converged;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12Resource *outputs;
    ID3D12Resource *inputs;
    unsigned int i;

    static const uint32_t inputs_data[] =
    {
        2, 3, 1, 3,
        2, 0, 0, 1,
        0, 1, 3, 2,
        2, 1, 2, 2,
    };

    static const uint32_t reference_converged[] =
    {
        25, 25, 25, 25,
        25, 25, 25, 25,
        25, 25, 25, 25,
        25, 25, 25, 25,
    };

    static const uint32_t reference_nonconverged[] =
    {
        12, 9, 4, 9,
        12, 0, 0, 4,
        0, 4, 9, 12,
        12, 4, 12, 12,
    };

#if 0
    StructuredBuffer<uint> RO : register(t0);
    RWStructuredBuffer<uint> RW : register(u0);

    [numthreads(16, 1, 1)]
    void main(uint thr : SV_DispatchThreadID)
    {
        uint v = RO[thr];
        uint result;
        while (true)
        {
            uint first = WaveReadLaneFirst(v);
            if (v == first)
            {
                result = WaveActiveSum(v);
                break;
            }
        }

        RW[thr] = result;
    }
#endif

    /* Compiled with Version: dxcompiler.dll: 1.5 - 1.4.1907.0; dxil.dll: 1.4(10.0.18362.1) */
    static const BYTE reconvergence_dxil[] =
    {
        0x44, 0x58, 0x42, 0x43, 0x27, 0x94, 0xf5, 0xbd, 0x53, 0x66, 0x70, 0xdb, 0xe2, 0x95, 0x5c, 0x8c, 0xdc, 0x10, 0x0b, 0xdf, 0x01, 0x00, 0x00, 0x00, 0xe4, 0x06, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x34, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x50, 0x53, 0x56, 0x30, 0x5c, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x14, 0x06, 0x00, 0x00, 0x60, 0x00, 0x05, 0x00, 0x85, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00,
        0x10, 0x00, 0x00, 0x00, 0xfc, 0x05, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x7c, 0x01, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00,
        0x07, 0x81, 0x23, 0x91, 0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x14, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42,
        0xa4, 0x10, 0x32, 0x14, 0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x52, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x91, 0x22, 0xc4, 0x50,
        0x41, 0x51, 0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x29, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x1b, 0x8c, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0x02, 0xa8, 0x0d,
        0x84, 0xf0, 0xff, 0xff, 0xff, 0xff, 0x03, 0x20, 0x6d, 0x30, 0x86, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x09, 0xa8, 0x00, 0x49, 0x18, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42,
        0x20, 0x4c, 0x08, 0x06, 0x00, 0x00, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x3d, 0x00, 0x00, 0x00, 0x32, 0x22, 0x48, 0x09, 0x20, 0x64, 0x85, 0x04, 0x93, 0x22, 0xa4, 0x84, 0x04, 0x93, 0x22, 0xe3,
        0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x8a, 0x8c, 0x0b, 0x84, 0xa4, 0x4c, 0x10, 0x68, 0x23, 0x00, 0x25, 0x00, 0x14, 0xe6, 0x08, 0xc0, 0xa0, 0x0c, 0x63, 0x0c, 0x22, 0x73, 0x04, 0x48, 0x29, 0xc6,
        0x18, 0xc6, 0xd0, 0x21, 0x73, 0xcf, 0x70, 0xf9, 0x13, 0xf6, 0x10, 0x92, 0x1f, 0x02, 0xcd, 0xb0, 0x10, 0x28, 0x48, 0x73, 0x04, 0x41, 0x31, 0xd4, 0x30, 0x63, 0x2c, 0x62, 0x45, 0x01, 0x43, 0x8d,
        0x31, 0xc6, 0x18, 0x86, 0xdc, 0x4d, 0xc3, 0xe5, 0x4f, 0xd8, 0x43, 0x48, 0xfe, 0x4a, 0x48, 0x2b, 0x31, 0xf9, 0xc8, 0x6d, 0xa3, 0x62, 0x8c, 0x31, 0x46, 0x29, 0xe0, 0x50, 0x63, 0x50, 0x1c, 0x08,
        0x98, 0x89, 0x0c, 0xc6, 0x81, 0x1d, 0xc2, 0x61, 0x1e, 0xe6, 0xc1, 0x0d, 0x66, 0x81, 0x1e, 0xe4, 0xa1, 0x1e, 0xc6, 0x81, 0x1e, 0xea, 0x41, 0x1e, 0xca, 0x81, 0x1c, 0x44, 0xa1, 0x1e, 0xcc, 0xc1,
        0x1c, 0xca, 0x41, 0x1e, 0xf8, 0xa0, 0x1e, 0xdc, 0x61, 0x1e, 0xd2, 0xe1, 0x1c, 0xdc, 0xa1, 0x1c, 0xc8, 0x01, 0x0c, 0xd2, 0xc1, 0x1d, 0xe8, 0xc1, 0x0f, 0x50, 0x30, 0x88, 0xce, 0x64, 0x06, 0xe3,
        0xc0, 0x0e, 0xe1, 0x30, 0x0f, 0xf3, 0xe0, 0x06, 0xb2, 0x70, 0x0b, 0xb3, 0x40, 0x0f, 0xf2, 0x50, 0x0f, 0xe3, 0x40, 0x0f, 0xf5, 0x20, 0x0f, 0xe5, 0x40, 0x0e, 0xa2, 0x50, 0x0f, 0xe6, 0x60, 0x0e,
        0xe5, 0x20, 0x0f, 0x7c, 0x50, 0x0f, 0xee, 0x30, 0x0f, 0xe9, 0x70, 0x0e, 0xee, 0x50, 0x0e, 0xe4, 0x00, 0x06, 0xe9, 0xe0, 0x0e, 0xf4, 0xe0, 0x07, 0x28, 0x18, 0x64, 0xe7, 0x08, 0x40, 0x61, 0x0a,
        0x00, 0x00, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d,
        0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e,
        0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10,
        0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78,
        0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x04, 0x10, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x79, 0x0e, 0x20, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xf2, 0x10, 0x40, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x30, 0xe4, 0x61, 0x80, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xc8, 0xe3, 0x00, 0x01, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x90, 0x27, 0x02,
        0x02, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x2c, 0x10, 0x0a, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x14, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x1a,
        0x25, 0x30, 0x02, 0x50, 0x08, 0xc5, 0x50, 0x18, 0x05, 0x42, 0x6b, 0x04, 0x80, 0x70, 0x81, 0x02, 0x02, 0xd1, 0x9d, 0x01, 0xa0, 0x3a, 0x03, 0x00, 0x79, 0x18, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
        0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0xc4, 0x88, 0x0c, 0x6f, 0xec, 0xed, 0x4d, 0x0c, 0x44, 0x06, 0x26, 0x26, 0xc7, 0x05, 0xa6, 0xc6, 0x05, 0x06, 0x66, 0x43, 0x10, 0x4c, 0x10, 0x86, 0x63,
        0x82, 0x30, 0x20, 0x1b, 0x84, 0x81, 0x98, 0x20, 0x0c, 0xc9, 0x06, 0x61, 0x30, 0x28, 0x8c, 0xcd, 0x4d, 0x10, 0x06, 0x65, 0xc3, 0x80, 0x24, 0xc4, 0x04, 0xa1, 0x82, 0x08, 0x4c, 0x10, 0x86, 0x65,
        0x43, 0x42, 0x2c, 0x0c, 0x41, 0x0c, 0x0d, 0x71, 0x6c, 0x08, 0x9c, 0x09, 0xc2, 0xf5, 0x4c, 0x10, 0x96, 0x66, 0xc3, 0x42, 0x40, 0x0c, 0x41, 0x0c, 0x4d, 0x14, 0x45, 0xc7, 0x86, 0x40, 0xda, 0x40,
        0x3c, 0x13, 0x00, 0x4c, 0x10, 0x04, 0x60, 0x03, 0xb0, 0x61, 0x20, 0x2c, 0x6b, 0x43, 0x70, 0x6d, 0x18, 0x86, 0x0a, 0x23, 0xd1, 0x16, 0x96, 0xe6, 0x36, 0x41, 0xc0, 0x9c, 0x09, 0xc2, 0xc0, 0x6c,
        0x18, 0xb8, 0x61, 0xd8, 0x40, 0x10, 0x9b, 0xd1, 0x6d, 0x28, 0x2a, 0x0d, 0xa0, 0xbc, 0x2a, 0x6c, 0x6c, 0x76, 0x6d, 0x2e, 0x69, 0x64, 0x65, 0x6e, 0x74, 0x53, 0x82, 0xa0, 0x0a, 0x19, 0x9e, 0x8b,
        0x5d, 0x99, 0xdc, 0x5c, 0xda, 0x9b, 0xdb, 0x94, 0x80, 0x68, 0x42, 0x86, 0xe7, 0x62, 0x17, 0xc6, 0x66, 0x57, 0x26, 0x37, 0x25, 0x30, 0xea, 0x90, 0xe1, 0xb9, 0xcc, 0xa1, 0x85, 0x91, 0x95, 0xc9,
        0x35, 0xbd, 0x91, 0x95, 0xb1, 0x4d, 0x09, 0x92, 0x32, 0x64, 0x78, 0x2e, 0x72, 0x65, 0x73, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x73, 0x53, 0x82, 0xa9, 0x12, 0x19, 0x9e, 0x0b, 0x5d, 0x1e, 0x5c, 0x59,
        0x90, 0x9b, 0xdb, 0x1b, 0x5d, 0x18, 0x5d, 0xda, 0x9b, 0xdb, 0xdc, 0x94, 0x00, 0xab, 0x43, 0x86, 0xe7, 0x52, 0xe6, 0x46, 0x27, 0x97, 0x07, 0xf5, 0x96, 0xe6, 0x46, 0x37, 0x37, 0x25, 0xf0, 0x00,
        0x79, 0x18, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73,
        0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b,
        0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48, 0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20,
        0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61,
        0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87,
        0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78, 0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98,
        0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61,
        0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b,
        0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x8c, 0xcc, 0x21, 0x07, 0x7c, 0x70, 0x03, 0x74, 0x60, 0x07, 0x37, 0x90, 0x87, 0x72, 0x98, 0x87, 0x77, 0xa8,
        0x07, 0x79, 0x18, 0x87, 0x72, 0x70, 0x83, 0x70, 0xa0, 0x07, 0x7a, 0x90, 0x87, 0x74, 0x10, 0x87, 0x7a, 0xa0, 0x87, 0x72, 0x00, 0x00, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x1d, 0x00, 0x00, 0x00,
        0x66, 0x40, 0x0d, 0x97, 0xef, 0x3c, 0x7e, 0x40, 0x15, 0x05, 0x11, 0x95, 0x0e, 0x30, 0xf8, 0xc8, 0x6d, 0x5b, 0x41, 0x35, 0x5c, 0xbe, 0xf3, 0xf8, 0x01, 0x55, 0x14, 0x44, 0xc4, 0x4e, 0x4e, 0x44,
        0xf8, 0xc8, 0x6d, 0x1b, 0x81, 0x34, 0x5c, 0xbe, 0xf3, 0xf8, 0x42, 0x44, 0x00, 0x13, 0x11, 0x02, 0xcd, 0xb0, 0x10, 0x16, 0x20, 0x0d, 0x97, 0xef, 0x3c, 0xfe, 0x74, 0x44, 0x04, 0x30, 0x88, 0x83,
        0x8f, 0xdc, 0xb6, 0x09, 0x58, 0xc3, 0xe5, 0x3b, 0x8f, 0x6f, 0x01, 0x15, 0xa1, 0x09, 0x13, 0x52, 0x11, 0xe8, 0xe3, 0x23, 0xb7, 0x6d, 0x03, 0xdb, 0x70, 0xf9, 0xce, 0xe3, 0x5b, 0x40, 0x45, 0xac,
        0x04, 0x30, 0x94, 0x40, 0x43, 0x7c, 0x48, 0x24, 0x4d, 0x3e, 0x72, 0xdb, 0x06, 0x40, 0x30, 0x00, 0xd2, 0x00, 0x00, 0x00, 0x61, 0x20, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x13, 0x04, 0x43, 0x2c,
        0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x34, 0x66, 0x00, 0x4a, 0xae, 0x74, 0x03, 0x0a, 0x31, 0xa0, 0xd8, 0x03, 0xca, 0x3d, 0xa0, 0x14, 0x03, 0xc8, 0x94, 0xc0, 0x08, 0x00, 0x00, 0x00,
        0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0xc0, 0x58, 0x88, 0x20, 0x49, 0xcd, 0x88, 0x41, 0x02, 0x80, 0x20, 0x18, 0x30, 0x57, 0x22, 0x4c, 0x93, 0x33, 0x62, 0x60, 0x00, 0x20, 0x08, 0x06, 0xc4, 0x96,
        0x50, 0x23, 0x06, 0x08, 0x00, 0x82, 0x60, 0x10, 0x5d, 0x89, 0x10, 0x54, 0xa3, 0x09, 0x01, 0x30, 0x4b, 0x10, 0x8c, 0x18, 0x18, 0x00, 0x08, 0x82, 0x01, 0xc1, 0x29, 0xc1, 0x70, 0x83, 0x10, 0x80,
        0xc1, 0x2c, 0x83, 0x10, 0x04, 0x23, 0x06, 0x08, 0x00, 0x82, 0x60, 0x70, 0x7c, 0xcb, 0x80, 0x24, 0x23, 0x06, 0x0d, 0x00, 0x82, 0x60, 0xe0, 0x74, 0x0b, 0x62, 0x68, 0x41, 0x14, 0x45, 0x0a, 0x02,
        0x00, 0x00, 0x00, 0x00,
    };
    static const D3D12_SHADER_BYTECODE reconvergence_code = SHADER_BYTECODE(reconvergence_dxil);

    /* Compiled with Version: dxcompiler.dll: 1.6 - 1.6.2112.12 (770ac0cc1); dxil.dll: 1.6(101.6.2112.2)
     * This version of DXC seems to workaround the control flow issue and explicitly hoists the wave-op out of the branch
     * to avoid convergence, most curious! */
#if 0
@dx.break.cond = internal constant [1 x i32] zeroinitializer
define void @main() {
    %1 = load i32, i32* getelementptr inbounds ([1 x i32], [1 x i32]* @dx.break.cond, i32 0, i32 0)
    %2 = icmp eq i32 %1, 0
    %3 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
    %4 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
    %5 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
    %6 = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32 68, %dx.types.Handle %4, i32 %5, i32 0)  ; BufferLoad(srv,index,wot)
    %7 = extractvalue %dx.types.ResRet.i32 %6, 0
    br label %8

    ; <label>:8                                       ; preds = %13, %0
    %9 = call i32 @dx.op.waveReadLaneFirst.i32(i32 118, i32 %7)  ; WaveReadLaneFirst(value)
    %10 = icmp eq i32 %7, %9
    br i1 %10, label %11, label %13

    ; <label>:11                                      ; preds = %8
    %12 = call i32 @dx.op.waveActiveOp.i32(i32 119, i32 %7, i8 0, i8 1)  ; WaveActiveOp(value,op,sop)
    ; Wow ... Load a constant 0 from a Private array and branch on that.
    br i1 %2, label %14, label %13

    ; <label>:13                                      ; preds = %11, %8
    br label %8

    ; <label>:14                                      ; preds = %11
    call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %3, i32 %5, i32 0, i32 %12, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
    ret void
}
#endif
    static const BYTE nonconvergence_dxil[] =
    {
        0x44, 0x58, 0x42, 0x43, 0x6a, 0xe3, 0x1a, 0x21, 0xec, 0x33, 0x8f, 0x47, 0xcb, 0xd9, 0x75, 0x47, 0x59, 0x62, 0x3f, 0x1f, 0x01, 0x00, 0x00, 0x00, 0x6c, 0x07, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
        0x38, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0xe8, 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00,
        0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x50, 0x53, 0x56, 0x30, 0x78, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x41, 0x53, 0x48, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0xd4, 0x61, 0x94, 0x9f, 0xd5, 0x2a, 0x70, 0x1d, 0xed, 0x1f, 0xc3,
        0x53, 0x48, 0x48, 0x9e, 0x44, 0x58, 0x49, 0x4c, 0x60, 0x06, 0x00, 0x00, 0x60, 0x00, 0x05, 0x00, 0x98, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
        0x48, 0x06, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x8f, 0x01, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91,
        0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x14, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0xa4, 0x10, 0x32, 0x14,
        0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x52, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x91, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c,
        0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x29, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x1b, 0x8c, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07, 0x40, 0x02, 0xa8, 0x0d, 0x84, 0xf0, 0xff, 0xff,
        0xff, 0xff, 0x03, 0x20, 0x6d, 0x30, 0x86, 0xff, 0xff, 0xff, 0xff, 0x1f, 0x00, 0x09, 0xa8, 0x00, 0x49, 0x18, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42, 0x20, 0x4c, 0x08, 0x06,
        0x00, 0x00, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x00, 0x32, 0x22, 0x48, 0x09, 0x20, 0x64, 0x85, 0x04, 0x93, 0x22, 0xa4, 0x84, 0x04, 0x93, 0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14,
        0x12, 0x4c, 0x8a, 0x8c, 0x0b, 0x84, 0xa4, 0x4c, 0x10, 0x74, 0x73, 0x04, 0x60, 0x90, 0x01, 0x80, 0xc2, 0x08, 0x40, 0x09, 0x06, 0x91, 0x32, 0x00, 0x00, 0xc8, 0xcc, 0x11, 0x20, 0xa5, 0x00, 0x00,
        0x20, 0x44, 0x89, 0xd0, 0x3d, 0xc3, 0xe5, 0x4f, 0xd8, 0x43, 0x48, 0x7e, 0x08, 0x34, 0xc3, 0x42, 0xa0, 0x60, 0xcd, 0x11, 0x04, 0xc5, 0x60, 0x00, 0x01, 0xd0, 0xc8, 0x15, 0x65, 0x00, 0x06, 0x00,
        0x00, 0x00, 0x20, 0x82, 0x37, 0x0d, 0x97, 0x3f, 0x61, 0x0f, 0x21, 0xf9, 0x2b, 0x21, 0xad, 0xc4, 0xe4, 0x23, 0xb7, 0x8d, 0x0a, 0x00, 0x00, 0x00, 0xa5, 0x90, 0x80, 0x01, 0x40, 0x73, 0x20, 0x60,
        0x26, 0x32, 0x18, 0x07, 0x76, 0x08, 0x87, 0x79, 0x98, 0x07, 0x37, 0x98, 0x05, 0x7a, 0x90, 0x87, 0x7a, 0x18, 0x07, 0x7a, 0xa8, 0x07, 0x79, 0x28, 0x07, 0x72, 0x10, 0x85, 0x7a, 0x30, 0x07, 0x73,
        0x28, 0x07, 0x79, 0xe0, 0x83, 0x7a, 0x70, 0x87, 0x79, 0x48, 0x87, 0x73, 0x70, 0x87, 0x72, 0x20, 0x07, 0x30, 0x48, 0x07, 0x77, 0xa0, 0x07, 0x3f, 0x40, 0x01, 0x20, 0x3b, 0x93, 0x19, 0x8c, 0x03,
        0x3b, 0x84, 0xc3, 0x3c, 0xcc, 0x83, 0x1b, 0xc8, 0xc2, 0x2d, 0xcc, 0x02, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xd4, 0x83, 0x3c, 0x94, 0x03, 0x39, 0x88, 0x42, 0x3d, 0x98, 0x83, 0x39, 0x94,
        0x83, 0x3c, 0xf0, 0x41, 0x3d, 0xb8, 0xc3, 0x3c, 0xa4, 0xc3, 0x39, 0xb8, 0x43, 0x39, 0x90, 0x03, 0x18, 0xa4, 0x83, 0x3b, 0xd0, 0x83, 0x1f, 0xa0, 0x00, 0x10, 0x9e, 0x23, 0x00, 0x05, 0x02, 0x53,
        0x00, 0x00, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d,
        0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e,
        0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10,
        0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78,
        0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x3a, 0x0f, 0x24, 0x90, 0x21, 0x23, 0x25, 0x40, 0x00, 0x1e, 0xa6, 0x31, 0xe4, 0x21, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xc8, 0x63, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x90, 0x27, 0x01, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x80, 0x21, 0x8f, 0x01, 0x04, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x43, 0x1e, 0x07, 0x08, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x10, 0x10, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x79, 0x26, 0x20, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc8, 0x02, 0x01, 0x0b, 0x00, 0x00, 0x00, 0x32, 0x1e, 0x98, 0x14,
        0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x02, 0x25, 0x30, 0x02, 0x50, 0x0c, 0x85, 0x51, 0x08, 0x05, 0x42, 0xba, 0x40, 0x01, 0x81, 0xa8, 0x8d, 0x00, 0xd0, 0x9d, 0x01,
        0xa0, 0x3c, 0x03, 0x40, 0x61, 0x04, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0x44, 0x35, 0x18, 0x63, 0x0b, 0x73, 0x3b, 0x03, 0xb1,
        0x2b, 0x93, 0x9b, 0x4b, 0x7b, 0x73, 0x03, 0x99, 0x71, 0xb9, 0x01, 0x41, 0xa1, 0x0b, 0x3b, 0x9b, 0x7b, 0x91, 0x2a, 0x62, 0x2a, 0x0a, 0x9a, 0x2a, 0xfa, 0x9a, 0xb9, 0x81, 0x79, 0x31, 0x4b, 0x73,
        0x0b, 0x63, 0x4b, 0xd9, 0x10, 0x04, 0x13, 0x04, 0x00, 0x99, 0x20, 0x00, 0xc9, 0x06, 0x61, 0x20, 0x26, 0x08, 0x80, 0xb2, 0x41, 0x18, 0x0c, 0x0a, 0x63, 0x73, 0x1b, 0x06, 0xc4, 0x20, 0x26, 0x08,
        0x17, 0x44, 0x60, 0x82, 0x00, 0x2c, 0x13, 0x04, 0x80, 0xd9, 0x20, 0x0c, 0xcd, 0x86, 0x84, 0x50, 0x16, 0x82, 0x18, 0x18, 0xc2, 0xd9, 0x10, 0x3c, 0x13, 0x84, 0x2c, 0x9a, 0x20, 0x34, 0xcf, 0x86,
        0x85, 0x88, 0x16, 0x82, 0x18, 0x18, 0x49, 0x92, 0x9c, 0x0d, 0xc1, 0xb4, 0x81, 0x80, 0x28, 0x00, 0x98, 0x20, 0x14, 0x01, 0x89, 0xb6, 0xb0, 0x34, 0xb7, 0x09, 0x82, 0xe6, 0x4c, 0x10, 0x80, 0x66,
        0xc3, 0x90, 0x0d, 0xc3, 0x06, 0x82, 0xc0, 0x1a, 0x6d, 0x43, 0x61, 0x5d, 0x40, 0xb5, 0x55, 0x61, 0x63, 0xb3, 0x6b, 0x73, 0x49, 0x23, 0x2b, 0x73, 0xa3, 0x9b, 0x12, 0x04, 0x55, 0xc8, 0xf0, 0x5c,
        0xec, 0xca, 0xe4, 0xe6, 0xd2, 0xde, 0xdc, 0xa6, 0x04, 0x44, 0x13, 0x32, 0x3c, 0x17, 0xbb, 0x30, 0x36, 0xbb, 0x32, 0xb9, 0x29, 0x81, 0x51, 0x87, 0x0c, 0xcf, 0x65, 0x0e, 0x2d, 0x8c, 0xac, 0x4c,
        0xae, 0xe9, 0x8d, 0xac, 0x8c, 0x6d, 0x4a, 0x80, 0x94, 0x21, 0xc3, 0x73, 0x91, 0x2b, 0x9b, 0x7b, 0xab, 0x93, 0x1b, 0x2b, 0x9b, 0x9b, 0x12, 0x50, 0x75, 0xc8, 0xf0, 0x5c, 0xca, 0xdc, 0xe8, 0xe4,
        0xf2, 0xa0, 0xde, 0xd2, 0xdc, 0xe8, 0xe6, 0xa6, 0x04, 0x1b, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x51, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88,
        0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce,
        0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48,
        0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e,
        0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b,
        0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78,
        0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1,
        0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39,
        0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x0c, 0xc4, 0x21, 0x07, 0x7c, 0x70,
        0x03, 0x7a, 0x28, 0x87, 0x76, 0x80, 0x87, 0x19, 0xcc, 0x43, 0x0e, 0xf8, 0xe0, 0x06, 0xe2, 0x20, 0x0f, 0xe5, 0x10, 0x0e, 0xeb, 0xe0, 0x06, 0xe2, 0x20, 0x0f, 0x33, 0x22, 0x88, 0x1c, 0xf0, 0xc1,
        0x0d, 0xc8, 0x41, 0x1c, 0xce, 0xc1, 0x0d, 0xec, 0x21, 0x1c, 0xe4, 0x81, 0x1d, 0xc2, 0x21, 0x1f, 0xde, 0xa1, 0x1e, 0xe8, 0x01, 0x00, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
        0x06, 0xd0, 0x0c, 0x97, 0x1f, 0x44, 0x04, 0xa0, 0xf8, 0x82, 0xd3, 0x0c, 0x76, 0x40, 0x0d, 0x97, 0xef, 0x3c, 0x7e, 0x40, 0x15, 0x05, 0x11, 0x95, 0x0e, 0x30, 0xf8, 0xc8, 0x6d, 0x9b, 0x41, 0x35,
        0x5c, 0xbe, 0xf3, 0xf8, 0x01, 0x55, 0x14, 0x44, 0xc4, 0x4e, 0x4e, 0x44, 0xf8, 0xc8, 0x6d, 0x5b, 0x81, 0x34, 0x5c, 0xbe, 0xf3, 0xf8, 0x42, 0x44, 0x00, 0x13, 0x11, 0x02, 0xcd, 0xb0, 0x10, 0x26,
        0x20, 0x0d, 0x97, 0xef, 0x3c, 0xfe, 0x74, 0x44, 0x04, 0x30, 0x88, 0x83, 0x8f, 0xdc, 0xb6, 0x0d, 0x58, 0xc3, 0xe5, 0x3b, 0x8f, 0x6f, 0x01, 0x15, 0xa1, 0x09, 0x13, 0x52, 0x11, 0xe8, 0xe3, 0x23,
        0xb7, 0x6d, 0x04, 0xdb, 0x70, 0xf9, 0xce, 0xe3, 0x5b, 0x40, 0x45, 0xac, 0x04, 0x30, 0x94, 0x40, 0x43, 0x7c, 0x48, 0x24, 0x4d, 0x3e, 0x72, 0xdb, 0x16, 0x40, 0x30, 0x00, 0xd2, 0x00, 0x00, 0x00,
        0x61, 0x20, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x13, 0x04, 0x45, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x04, 0x66, 0x00, 0x4a, 0xae, 0x74, 0x03, 0x0a, 0x31, 0xa0, 0xd8, 0x03,
        0xca, 0x3d, 0xa0, 0x14, 0x03, 0x08, 0x95, 0xc0, 0x08, 0x00, 0xed, 0xa1, 0x8e, 0x40, 0x00, 0x80, 0x04, 0x48, 0x00, 0x00, 0x14, 0x00, 0x30, 0xdc, 0x10, 0x54, 0x60, 0x30, 0x62, 0x90, 0x00, 0x20,
        0x08, 0x06, 0x8e, 0xb6, 0x14, 0x96, 0x05, 0x8d, 0x18, 0x24, 0x00, 0x08, 0x82, 0x81, 0xb3, 0x31, 0xc5, 0x75, 0x45, 0x23, 0x06, 0x06, 0x00, 0x82, 0x60, 0x60, 0x7c, 0x0c, 0x36, 0x62, 0x80, 0x00,
        0x20, 0x08, 0x06, 0xd3, 0xc6, 0x08, 0x41, 0x36, 0x9a, 0x10, 0x00, 0xb3, 0x04, 0xc1, 0x88, 0x81, 0x01, 0x80, 0x20, 0x18, 0x18, 0x60, 0xd0, 0x04, 0xc3, 0x0d, 0x42, 0x00, 0x06, 0xb3, 0x0c, 0xc2,
        0x10, 0x8c, 0x18, 0x20, 0x00, 0x08, 0x82, 0x41, 0x32, 0x06, 0xce, 0xb0, 0x30, 0xb3, 0x0c, 0xc4, 0x90, 0xcc, 0x12, 0x04, 0x23, 0x06, 0x0d, 0x00, 0x82, 0x60, 0x00, 0x85, 0x81, 0x83, 0x18, 0x5e,
        0x40, 0x51, 0x54, 0x83, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    static const D3D12_SHADER_BYTECODE nonconvergence_code = SHADER_BYTECODE(nonconvergence_dxil);

    memset(root_parameters, 0, sizeof(root_parameters));
    memset(&rs_desc, 0, sizeof(rs_desc));
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_desc.pParameters = root_parameters;
    rs_desc.NumParameters = ARRAY_SIZE(root_parameters);

    if (!init_compute_test_context(&context))
        return;

    if (!context_supports_dxil(&context))
    {
        skip("Context does not support DXIL.\n");
        destroy_test_context(&context);
        return;
    }

    create_root_signature(context.device, &rs_desc, &context.root_signature);
    pso_converged = create_compute_pipeline_state(context.device, context.root_signature, reconvergence_code);
    pso_nonconverged = create_compute_pipeline_state(context.device, context.root_signature, nonconvergence_code);

    inputs = create_upload_buffer(context.device, sizeof(inputs_data), inputs_data);
    outputs = create_default_buffer(context.device, sizeof(inputs_data) * 2,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso_converged);
    ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context.list, 0,
            ID3D12Resource_GetGPUVirtualAddress(inputs));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1,
            ID3D12Resource_GetGPUVirtualAddress(outputs));
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1,
            ID3D12Resource_GetGPUVirtualAddress(outputs) + sizeof(reference_converged));
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso_nonconverged);
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

    transition_resource_state(context.list, outputs,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(outputs, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(reference_converged); i++)
    {
        uint32_t v = get_readback_uint(&rb, i, 0, 0);
        ok(v == reference_converged[i], "Element %u, %u != %u.\n", i, v, reference_converged[i]);
    }

    for (i = 0; i < ARRAY_SIZE(reference_nonconverged); i++)
    {
        uint32_t v = get_readback_uint(&rb, i + 16, 0, 0);
        ok(v == reference_nonconverged[i], "Element %u, %u != %u.\n", i, v, reference_nonconverged[i]);
    }

    release_resource_readback(&rb);

    ID3D12Resource_Release(inputs);
    ID3D12Resource_Release(outputs);
    ID3D12PipelineState_Release(pso_converged);
    ID3D12PipelineState_Release(pso_nonconverged);
    destroy_test_context(&context);
}

void test_shader_sm65_wave_intrinsics(void)
{
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_ROOT_PARAMETER root_parameters[2];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12PipelineState *pso;
    unsigned int i, j, k;
    ID3D12Resource *src;
    ID3D12Resource *dst;
    uint32_t value;

    struct test
    {
        const struct D3D12_SHADER_BYTECODE *cs;
        uint32_t input[16];
        uint32_t output[6][16];
        unsigned int instances;
    };

#include "shaders/sm_advanced/headers/cs_wave_match.h"
#include "shaders/sm_advanced/headers/cs_wave_multi_prefix.h"

    /* Wave match is fairly basic. It groups elements which are equal into partitions.
     * That partition gets a ballot which is the mask of all active invocations in that partition.
     * The ballot is then broadcast to all active participants of the partition.
     * This is implemented as a scalarization loop on AMD, and probably maps to something more optimal on NV
     * if SPV_NV_shader_subgroup_partitioned is anything to go by ... */

    /* Multiprefix works on partitions, and each partition performs its own prefix sum operation.
     * There are various restrictions:
     * - An invocation must only appear in one partition.
     * - Undocumented, but obvious requirement: For invocation N, bit N must be set in mask.
     * Additionally, the mask is ANDed by active invocation mask.
     *
     * NV SPIR-V extension is more precise here:
        Add: "The ballot parameter to the partitioned operations must form a valid partition of the active invocations in the subgroup.
        The values of ballot are a valid partition if:
        for each active invocation i, the bit corresponding to i is set in i's value of ballot, and
        for any two active invocations i and j, if the bit corresponding to invocation j is set in invocation i's value of ballot,
        then invocation j's value of ballot must equal invocation i's value of ballot, and
        bits not corresponding to any invocation in the subgroup are ignored.
     */
    static const struct test tests[] =
    {
        { &cs_wave_match_dxil,
            { 20, 50, 80, 100, 40, 20, 90, 20, 10, 0, 20, 50, 80, 90, 110, 120 },
            {{ 0x4a1, 0x802, 0x1004, 0x8, 0x10, 0x4a1, 0x2040, 0x4a1, 0x100, 0x200, 0x4a1, 0x802, 0x1004, 0x2040, 0x4000, 0x8000 }}, 1 },

        /* First, test identities as a sanity check. */
        { &cs_wave_multi_prefix_dxil,
            { 0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000 },
            {
                { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
                { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, },
                { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
                { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, },
                { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
                { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
            }, 6 },

        /* Everything in same group. */
        { &cs_wave_multi_prefix_dxil,
            { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, },
            {
                { 0, 0x1, 0x3, 0x6, 0xa, 0xf, 0x15, 0x1c, 0x24, 0x2d, 0x37, 0x42, 0x4e, 0x5b, 0x69, 0x78, },
                { 0x1, 0x1, 0x2, 0x6, 0x18, 0x78, 0x2d0, 0x13b0, 0x9d80, 0x58980, 0x375f00, 0x2611500, 0x1c8cfc00, 0x7328cc00, 0x4c3b2800, 0x77775800, },
                { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, },
                { ~0u, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
                { 0, 1, 3, 3, 7, 7, 7, 7, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf },
                { 0, 1, 3, 0, 4, 1, 7, 0, 8, 1, 0xb, 0, 0xc, 0x1, 0xf, 0, },
            }, 6 },

        /* Everything in same group, still. Need to mask ballot before checking partitions. */
        { &cs_wave_multi_prefix_dxil,
            { ~0u, ~0u, ~0u, 0xffff, ~0u, 0x1ffff, ~0u, ~0u, ~0u, 0x8000ffff, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, },
            {
                { 0, 0x1, 0x3, 0x6, 0xa, 0xf, 0x15, 0x1c, 0x24, 0x2d, 0x37, 0x42, 0x4e, 0x5b, 0x69, 0x78, },
                { 0x1, 0x1, 0x2, 0x6, 0x18, 0x78, 0x2d0, 0x13b0, 0x9d80, 0x58980, 0x375f00, 0x2611500, 0x1c8cfc00, 0x7328cc00, 0x4c3b2800, 0x77775800, },
                { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, },
                { ~0u, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, },
                { 0, 1, 3, 3, 7, 7, 7, 7, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf },
                { 0, 1, 3, 0, 4, 1, 7, 0, 8, 1, 0xb, 0, 0xc, 0x1, 0xf, 0, },
            }, 6 },

        /* The real test ... */
        { &cs_wave_multi_prefix_dxil,
            { 0x4a1, 0x802, 0x1004, 0x8, 0x10, 0x4a1, 0x2040, 0x4a1, 0x100, 0x200, 0x4a1, 0x802, 0x1004, 0x2040, 0x4000, 0x8000 },
            {
                { 0, 0, 0, 0, 0, 1, 0, 7, 0, 0, 0xf, 2, 3, 7, },
                { 1, 1, 1, 1, 1, 1, 1, 6, 1, 1, 0x30, 0x2, 0x3, 0x7, 0x1, 0x1, },
                { 0, 0, 0, 0, 0, 1, 0, 2, 0, 0, 3, 1, 0, 1, },
                { ~0u, ~0u, ~0u, ~0u, ~0u, 1, ~0u, 0, ~0u, ~0u, 0, 2, 3, 7, ~0u, ~0u },
                { 0, 0, 0, 0, 0, 1, 0, 7, 0, 0, 0xf, 0x2, 0x3, 0x7, },
                { 0, 0, 0, 0, 0, 1, 0, 7, 0, 0, 0xf, 0x2, 0x3, 0x7, },
            }, 6 },

        /* With inactive lane handling ... */
        { &cs_wave_multi_prefix_dxil,
            { 0x4a1, 0x800802, 0x1004, 0x8, 0x10, 0x4a1, 0x8002040, 0x4a1, 0x100, 0x200, 0x4a1, 0x802, 0x1004, 0x2040, 0x4000, 0x8000 },
            {
                { 0, 0, 0, 0, 0, 1, 0, 7, 0, 0, 0xf, 2, 3, 7, },
                { 1, 1, 1, 1, 1, 1, 1, 6, 1, 1, 0x30, 0x2, 0x3, 0x7, 0x1, 0x1, },
                { 0, 0, 0, 0, 0, 1, 0, 2, 0, 0, 3, 1, 0, 1, },
                { ~0u, ~0u, ~0u, ~0u, ~0u, 1, ~0u, 0, ~0u, ~0u, 0, 2, 3, 7, ~0u, ~0u },
                { 0, 0, 0, 0, 0, 1, 0, 7, 0, 0, 0xf, 0x2, 0x3, 0x7, },
                { 0, 0, 0, 0, 0, 1, 0, 7, 0, 0, 0xf, 0x2, 0x3, 0x7, },
            }, 6 }
    };

    if (!init_compute_test_context(&context))
        return;

    if (!context_supports_dxil(&context))
    {
        skip("Context does not support DXIL.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&shader_model, 0, sizeof(shader_model));
    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_5;
    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) ||
        shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_5)
    {
        skip("Device does not support SM 6.5.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(root_parameters);
    rs_desc.pParameters = root_parameters;
    memset(root_parameters, 0, sizeof(root_parameters));
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);
        pso = create_compute_pipeline_state(context.device, context.root_signature, *tests[i].cs);
        src = create_upload_buffer(context.device, sizeof(tests[i].input), tests[i].input);
        dst = create_default_buffer(context.device, sizeof(tests[i].output), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(src));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(dst));
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

        transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(dst, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

        for (k = 0; k < tests[i].instances; k++)
        {
            for (j = 0; j < 16; j++)
            {
                value = get_readback_uint(&rb, k * 16 + j, 0, 0);
                ok(value == tests[i].output[k][j], "Index %u, instance %u: 0x%x != 0x%x\n", j, k, value, tests[i].output[k][j]);
            }
        }

        ID3D12Resource_Release(src);
        ID3D12Resource_Release(dst);
        ID3D12PipelineState_Release(pso);
        release_resource_readback(&rb);
        reset_command_list(context.list, context.allocator);
    }

    vkd3d_test_set_context(NULL);
    destroy_test_context(&context);
}

void test_shader_sm66_is_helper_lane(void)
{
    /* Oh, hi there. */
    static const float alpha_keys[4] = { 0.75f, 2.25f, 3.25f, 3.75f };
    static const float white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12Resource *readback_buffer;
    struct test_context_desc desc;
    ID3D12Resource *atomic_buffer;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Resource *input_keys;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    unsigned int x, y, i;
    HRESULT hr;

#include "shaders/sm_advanced/headers/ps_discard_atomic_loop.h"

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.rt_width = 2;
    desc.rt_height = 2;
    desc.no_pipeline = true;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    if (!context_supports_dxil(&context))
    {
        skip("Context does not support DXIL.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&shader_model, 0, sizeof(shader_model));
    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) ||
        shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_6)
    {
        skip("Device does not support SM 6.6.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    init_pipeline_state_desc_dxil(&pso_desc, context.root_signature,
            DXGI_FORMAT_R32G32B32A32_FLOAT, NULL, &ps_discard_atomic_loop_dxil, NULL);
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
            &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create state, hr %#x.\n", hr);

    input_keys = create_upload_buffer(context.device, sizeof(alpha_keys), alpha_keys);
    atomic_buffer = create_default_buffer(context.device, 4 * sizeof(uint32_t),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    readback_buffer = create_readback_buffer(context.device, 4 * sizeof(uint32_t));

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Buffer.NumElements = 4;
    srv_desc.Buffer.StructureByteStride = 4;
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    ID3D12Device_CreateShaderResourceView(context.device, input_keys, &srv_desc, cpu_handle);

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.Format = DXGI_FORMAT_UNKNOWN;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.NumElements = 4;
    uav_desc.Buffer.StructureByteStride = 4;

    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    cpu_handle.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ID3D12Device_CreateUnorderedAccessView(context.device, atomic_buffer, NULL, &uav_desc, cpu_handle);

    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    set_viewport(&context.viewport, 0.0f, 0.0f, 2.0f, 2.0f, 0.0f, 1.0f);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, atomic_buffer,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyBufferRegion(command_list, readback_buffer, 0, atomic_buffer, 0, 4 * sizeof(uint32_t));
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);

    {
        uint32_t *ptr;
        hr = ID3D12Resource_Map(readback_buffer, 0, NULL, (void**)&ptr);
        ok(SUCCEEDED(hr), "Failed to map buffer, hr #%x.\n", hr);
        if (SUCCEEDED(hr))
        {
            static const uint32_t expected[] = { 101, 0, 0, 101 };
            for (i = 0; i < ARRAY_SIZE(expected); i++)
                ok(ptr[i] == expected[i], "Atomic value %u, expected %u, got %u.\n", i, expected[i], ptr[i]);
            ID3D12Resource_Unmap(readback_buffer, 0, NULL);
        }
    }

    for (y = 0; y < 2; y++)
    {
        for (x = 0; x < 2; x++)
        {
            const struct vec4 *value;
            struct vec4 expected;

            value = get_readback_vec4(&rb, x, y);

            if (x == 0 && y == 0)
            {
                expected.x = 1.0f;
                expected.y = 4321.0f;
                expected.z = 4881.0f;
                expected.w = 8881.0f;
            }
            else
                memcpy(&expected, white, sizeof(white));

            ok(compare_vec4(value, &expected, 0), "Mismatch pixel %u, %u, (%f %f %f %f) != (%f %f %f %f).\n",
                    x, y, expected.x, expected.y, expected.z, expected.w,
                    value->x, value->y, value->z, value->w);
        }
    }

    ID3D12DescriptorHeap_Release(heap);
    release_resource_readback(&rb);
    ID3D12Resource_Release(readback_buffer);
    ID3D12Resource_Release(input_keys);
    ID3D12Resource_Release(atomic_buffer);
    destroy_test_context(&context);
}

void test_advanced_cbv_layout(void)
{
    /* This is extremely cursed in DXC ... D: */
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 features4;
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 features1;
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc;
    D3D12_ROOT_PARAMETER root_parameters[3];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_DESCRIPTOR_RANGE range[1];
    struct test_context context;
    struct resource_readback rb;
    ID3D12DescriptorHeap *heap;
    ID3D12Resource *cbv_buffer;
    ID3D12Resource *uav_buffer;
    uint32_t input_buffer[64];
    ID3D12PipelineState *pso;
    bool support_16bit;
    bool support_64bit;
    unsigned int i, j;

#include "shaders/sm_advanced/headers/cs_cbv_layout_legacy_uint64.h"
#include "shaders/sm_advanced/headers/cs_cbv_layout_legacy_uint16.h"
#include "shaders/sm_advanced/headers/cs_cbv_layout_modern_uint64.h"
#include "shaders/sm_advanced/headers/cs_cbv_layout_modern_uint16.h"
#include "shaders/sm_advanced/headers/cs_cbv_layout_modern_uint32.h"

    struct test
    {
        const D3D12_SHADER_BYTECODE *cs;
        bool requires_16bit;
        bool requires_64bit;
        uint32_t reference[16];
    };

    static const struct test tests[] =
    {
        { &cs_cbv_layout_legacy_uint64_dxil, false, true, {
            0xc080400, 0xe0a0602, 0x1c181410, 0x1e1a1612,
            0x2c282420, 0x2e2a2622, 0x3c383430, 0x3e3a3632,
            0x4c484440, 0x4e4a4642, 0x5c585450, 0x5e5a5652,
            0x6c686460, 0x6e6a6662, 0x7c787470, 0x7e7a7672 }},
        { &cs_cbv_layout_modern_uint64_dxil, false, true, {
            0xc080400, 0xe0a0602, 0x1c181410, 0x1e1a1612,
            0x2c282420, 0x2e2a2622, 0x3c383430, 0x3e3a3632,
            0x4c484440, 0x4e4a4642, 0x5c585450, 0x5e5a5652,
            0x6c686460, 0x6e6a6662, 0x7c787470, 0x7e7a7672 }},
        { &cs_cbv_layout_legacy_uint16_dxil, true, false, {
            0x40002, 0x40002, 0xc000a, 0xc000a,
            0x140012, 0x140012, 0x1c001a, 0x1c001a,
            0x240022, 0x240022, 0x2c002a, 0x2c002a,
            0x340032, 0x340032, 0x3c003a, 0x3c003a, }},
         { &cs_cbv_layout_modern_uint16_dxil, true, false, {
            0x08060402, 0x08060402, 0x100e0c0a, 0x100e0c0a,
            0x18161412, 0x18161412, 0x201e1c1a, 0x201e1c1a,
            0x28262422, 0x28262422, 0x302e2c2a, 0x302e2c2a,
            0x38363432, 0x38363432, 0x403e3c3a, 0x403e3c3a, }},
         { &cs_cbv_layout_modern_uint32_dxil, false, false, {
            0x03020100, 0x03020100, 0x07060504, 0x07060504,
            0x0b0a0908, 0x0b0a0908, 0x0f0e0d0c, 0x0f0e0d0c,
            0x13121110, 0x13121110, 0x17161514, 0x17161514,
            0x1b1a1918, 0x1b1a1918, 0x1f1e1d1c, 0x1f1e1d1c, }},
    };

    if (!init_compute_test_context(&context))
        return;

    if (!context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        skip("DXIL not supported.\n");
        return;
    }

    for (i = 0; i < ARRAY_SIZE(input_buffer); i++)
        input_buffer[i] = i;

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(root_parameters, 0, sizeof(root_parameters));
    memset(range, 0, sizeof(range));
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[2].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(range);
    root_parameters[2].DescriptorTable.pDescriptorRanges = range;
    range[0].NumDescriptors = 1;
    range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    range[0].RegisterSpace = 1;
    rs_desc.NumParameters = ARRAY_SIZE(root_parameters);
    rs_desc.pParameters = root_parameters;

    create_root_signature(context.device, &rs_desc, &context.root_signature);

    support_16bit =
            SUCCEEDED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS4,
                    &features4, sizeof(features4))) &&
            features4.Native16BitShaderOpsSupported;

    support_64bit =
            SUCCEEDED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS1,
                    &features1, sizeof(features1))) &&
            features1.Int64ShaderOps;

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    cbv_buffer = create_upload_buffer(context.device, sizeof(input_buffer), input_buffer);
    uav_buffer = create_default_buffer(context.device, 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    cbv_desc.SizeInBytes = sizeof(input_buffer);
    cbv_desc.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(cbv_buffer);
    ID3D12Device_CreateConstantBufferView(context.device, &cbv_desc,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        if (tests[i].requires_16bit && !support_16bit)
        {
            skip("Test requires 16-bit, but not supported.\n");
            continue;
        }

        if (tests[i].requires_64bit && !support_64bit)
        {
            skip("Test requires 64-bit, but not supported.\n");
            continue;
        }

        pso = create_compute_pipeline_state(context.device, context.root_signature, *tests[i].cs);
        ok(!!pso, "Failed to create PSO.\n");
        if (!pso)
            continue;

        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 0,
                ID3D12Resource_GetGPUVirtualAddress(uav_buffer));
        ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(context.list, 1,
                ID3D12Resource_GetGPUVirtualAddress(cbv_buffer));
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 2,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

        transition_resource_state(context.list, uav_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(uav_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, uav_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12PipelineState_Release(pso);

        for (j = 0; j < ARRAY_SIZE(tests[i].reference); j++)
        {
            uint32_t ref = tests[i].reference[j];
            uint32_t v = get_readback_uint(&rb, j, 0, 0);
            ok(v == ref, "Value %u: #%x != #%x\n", j, v, ref);
        }
        release_resource_readback(&rb);
    }
    vkd3d_test_set_context(NULL);

    ID3D12Resource_Release(cbv_buffer);
    ID3D12Resource_Release(uav_buffer);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

static void test_denorm_behavior(bool use_dxil)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 features4;
    D3D12_FEATURE_DATA_D3D12_OPTIONS features;
    D3D12_FEATURE_DATA_SHADER_MODEL model;
    D3D12_ROOT_PARAMETER root_param[2];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12PipelineState *pso;
    ID3D12Resource *dst;
    ID3D12Resource *src;
    bool support_16bit;
    bool support_64bit;
    unsigned int i, j;

    struct test
    {
        const D3D12_SHADER_BYTECODE *cs_dxbc;
        const D3D12_SHADER_BYTECODE *cs_dxil;
        const uint64_t input_data[4];
        const uint64_t output_data[4];
        bool requires_fp16;
        bool requires_fp64;
        bool any_fp32;
        const uint64_t alt_output_data[4];
    };

#include "shaders/sm_advanced/headers/cs_denorm_fp64_fp32_any_dxbc.h"
#include "shaders/sm_advanced/headers/cs_denorm_fp64_fp32_any.h"
#include "shaders/sm_advanced/headers/cs_denorm_fp64_fp32_ftz.h"
#include "shaders/sm_advanced/headers/cs_denorm_fp64_fp32_preserve.h"
#include "shaders/sm_advanced/headers/cs_denorm_fp16_fp32_any.h"
#include "shaders/sm_advanced/headers/cs_denorm_fp16_fp32_ftz.h"
#include "shaders/sm_advanced/headers/cs_denorm_fp16_fp32_preserve.h"
#include "shaders/sm_advanced/headers/cs_denorm_fp16_fp64_fp32_any.h"
#include "shaders/sm_advanced/headers/cs_denorm_fp16_fp64_fp32_ftz.h"
#include "shaders/sm_advanced/headers/cs_denorm_fp16_fp64_fp32_preserve.h"

#if 0
    // -enable-16bit-types and -denorm ftz/preserve/any
    RWByteAddressBuffer RWBuf : register(u0);
    ByteAddressBuffer ROBuf : register(t0);

    [numthreads(1, 1, 1)]
    void main()
    {
#ifdef FP64
        {
#ifdef DXBC
            uint4 loaded_v = ROBuf.Load4(0);
            double2 v = double2(asdouble(loaded_v.x, loaded_v.y), asdouble(loaded_v.z, loaded_v.w));
            precise double v2 = v.x + v.y;
			asuint(v2, loaded_v.x, loaded_v.y);
            RWBuf.Store2(0, loaded_v.xy);
#else
            double2 v = ROBuf.Load<double2>(0);
            precise double v2 = v.x + v.y;
            RWBuf.Store<double>(0, v2);
#endif
        }
#endif

        {
            float2 v = ROBuf.Load<float2>(16);
            precise float v2 = v.x + v.y;
            RWBuf.Store<float>(8, v2);
        }

#ifdef FP16
        {
            half4 v = ROBuf.Load<half4>(24);
            precise half2 v2 = v.xy + v.zw;
            RWBuf.Store<half2>(16, v2);
        }
#endif
    }
#endif

    /* Test different combinations where FP16 and/or FP64 is used with denorm preserve alongside FP32 explicit modes. */
#define MAKE_FLOAT2(a, b) (((uint64_t)(b) << 32) | (a))
#define MAKE_HALF4(a, b, c, d) (((uint64_t)(d) << 48) | ((uint64_t)(c) << 32) | ((uint64_t)(b) << 16) | ((uint64_t)(a) << 0))
#define TEST_DATA(fp16, fp32_ftz, fp32_denorm, fp64) \
    { 10, 20, MAKE_FLOAT2(2, 3), MAKE_HALF4(1, 4, 5, 0x8009)}, \
    { fp64 ? 30 : 0, fp32_denorm ? 5 : 0, fp16 ? MAKE_HALF4(6, 0x8005, 0, 0) : 0, 0 }, \
    fp16, fp64, !fp32_ftz && !fp32_denorm, \
    { fp64 ? 30 : 0, !fp32_denorm ? 5 : 0, fp16 ? MAKE_HALF4(6, 0x8005, 0, 0) : 0, 0 }

    static const struct test tests[] =
    {
        /* FP32 FTZ */
        { NULL, &cs_denorm_fp16_fp32_ftz_dxil, TEST_DATA(true, true, false, false) },
        { NULL, &cs_denorm_fp64_fp32_ftz_dxil, TEST_DATA(false, true, false, true) },
        { NULL, &cs_denorm_fp16_fp64_fp32_ftz_dxil, TEST_DATA(true, true, false, true) },
        /* FP32 preserve */
        { NULL, &cs_denorm_fp16_fp32_preserve_dxil, TEST_DATA(true, false, true, false) },
        { NULL, &cs_denorm_fp64_fp32_preserve_dxil, TEST_DATA(false, false, true, true) },
        { NULL, &cs_denorm_fp16_fp64_fp32_preserve_dxil, TEST_DATA(true, false, true, true) },
        /* FP32 any */
        { NULL, &cs_denorm_fp16_fp32_any_dxil, TEST_DATA(true, false, false, false) },
        { &cs_denorm_fp64_fp32_any_dxbc_dxbc, &cs_denorm_fp64_fp32_any_dxil, TEST_DATA(false, false, false, true) },
        { NULL, &cs_denorm_fp16_fp64_fp32_any_dxil, TEST_DATA(true, false, false, true) },
    };
#undef MAKE_FLOAT2
#undef MAKE_HALF4

    if (!init_compute_test_context(&context))
        return;

    model.HighestShaderModel = D3D_SHADER_MODEL_6_2;
    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
            model.HighestShaderModel < D3D_SHADER_MODEL_6_2)
    {
        skip("SM 6.2 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    support_16bit =
        SUCCEEDED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS4,
            &features4, sizeof(features4))) &&
        features4.Native16BitShaderOpsSupported;

    support_64bit =
        SUCCEEDED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS,
            &features, sizeof(features))) &&
        features.DoublePrecisionFloatShaderOps;

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(&root_param, 0, sizeof(root_param));
    rs_desc.NumParameters = ARRAY_SIZE(root_param);
    rs_desc.pParameters = root_param;
    root_param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;

    create_root_signature(context.device, &rs_desc, &context.root_signature);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        const D3D12_SHADER_BYTECODE *cs = use_dxil ? tests[i].cs_dxil : tests[i].cs_dxbc;
        if (!cs)
            continue;

        if (tests[i].requires_fp16 && !support_16bit)
        {
            skip("FP16 not supported.\n");
            continue;
        }

        if (tests[i].requires_fp64 && !support_64bit)
        {
            skip("FP64 not supported.\n");
            continue;
        }

        vkd3d_test_set_context("Test %u", i);
        pso = create_compute_pipeline_state(context.device, context.root_signature, *cs);
        src = create_upload_buffer(context.device, sizeof(tests[i].input_data), tests[i].input_data);
        dst = create_default_buffer(context.device, sizeof(tests[i].output_data), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(dst));
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(src));
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

        transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(dst, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

        for (j = 0; j < ARRAY_SIZE(tests[i].output_data); j++)
        {
            uint64_t v, ref, alt_ref;
            v = get_readback_uint64(&rb, j, 0);
            ref = tests[i].output_data[j];
            alt_ref = tests[i].alt_output_data[j];

            if (tests[i].any_fp32)
            {
                ok(v == ref || v == alt_ref, "Value %u mismatch, expected %"PRIx64" or %"PRIx64", got %"PRIx64".\n", j, ref, alt_ref, v);
            }
            else
            {
                ok(v == ref, "Value %u mismatch, expected %"PRIx64", got %"PRIx64".\n", j, ref, v);
            }
        }

        release_resource_readback(&rb);
        ID3D12Resource_Release(src);
        ID3D12Resource_Release(dst);
        ID3D12PipelineState_Release(pso);

        reset_command_list(context.list, context.allocator);
    }
    vkd3d_test_set_context(NULL);

    destroy_test_context(&context);
}

void test_denorm_behavior_dxbc(void)
{
    test_denorm_behavior(false);
}

void test_denorm_behavior_dxil(void)
{
    test_denorm_behavior(true);
}

void test_sm67_helper_lane_wave_ops(void)
{
    const float default_color[4] = { 1000.0f, 1000.0f, 1000.0f, 1000.0f };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL model;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[1];
    struct test_context_desc desc;
    ID3D12PipelineState *psos[2];
    struct test_context context;
    struct resource_readback rb;
    ID3D12Resource *src;
    D3D12_VIEWPORT vp;
    unsigned int i, j;
    D3D12_RECT sci;
    HRESULT hr;

#include "shaders/sm_advanced/headers/vs_helper_lane_wave_ops.h"
#include "shaders/sm_advanced/headers/ps_helper_lane_wave_ops_enabled.h"
#include "shaders/sm_advanced/headers/ps_helper_lane_wave_ops_disabled.h"

#define INIT(x) ((x) << 0)
#define POST_DISCARD(x) ((x) << 3)
#define QUAD_ANY_RESULT(x) ((x) << 6)
#define QUAD_ALL_RESULT(x) ((x) << 7)
#define WAVE_ANY_RESULT(x) ((x) << 8)
#define WAVE_ALL_RESULT(x) ((x) << 9)
#define ELECTED(x) ((x) << 10)
#define FIRST_RESULT(x) ((x) << 11)
#define BALLOT(x) ((x) << 13)
#define ACTIVE_SUM(x) ((x) << 17)
#define ACTIVE_PREFIX_BITS(x) ((x) << 21)

#define EXEC_BIT 1
#define BOOL_BIT 2

    struct test
    {
        uint32_t input_data[4];
        uint32_t output_data[2][4];
    } tests[4 * 4 * 4 * 4];

    /* Compute expected results. */
    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        struct test *t = &tests[i];
        uint32_t first_lane = 0;
        uint32_t active_sum = 0;
        uint32_t exec_bits = 0;
        uint32_t bool_bits = 0;

        memset(t, 0, sizeof(*t));

        /* Test every possible input combination. */
        for (j = 0; j < 4; j++)
        {
            t->input_data[j] = (i >> (2 * j)) & 3;
        }

        /* Deal with WaveActiveCountBits */
        for (j = 0; j < 4; j++)
        {
            t->output_data[0][j] |= INIT(4);
            t->output_data[1][j] |= INIT(4);
            t->output_data[1][j] |= POST_DISCARD(4);
            if (t->input_data[j] & EXEC_BIT)
                active_sum += j;
            exec_bits |= (uint32_t)!!(t->input_data[j] & EXEC_BIT) << j;
            bool_bits |= (uint32_t)!!(t->input_data[j] & BOOL_BIT) << j;
        }

        first_lane = vkd3d_bitmask_tzcnt32(exec_bits);

        for (j = 0; j < 4; j++)
            t->output_data[0][j] |= POST_DISCARD(vkd3d_popcount(exec_bits));

        /* Any/All resolve */
        for (j = 0; j < 4; j++)
        {
            t->output_data[0][j] |= QUAD_ANY_RESULT(bool_bits != 0x0);
            t->output_data[1][j] |= QUAD_ANY_RESULT(bool_bits != 0x0);
            t->output_data[0][j] |= QUAD_ALL_RESULT(bool_bits == 0xf);
            t->output_data[1][j] |= QUAD_ALL_RESULT(bool_bits == 0xf);

            t->output_data[0][j] |= WAVE_ANY_RESULT((bool_bits & exec_bits) != 0x0);
            t->output_data[1][j] |= WAVE_ANY_RESULT(bool_bits != 0x0);
            t->output_data[0][j] |= WAVE_ALL_RESULT((bool_bits & exec_bits) == exec_bits);
            t->output_data[1][j] |= WAVE_ALL_RESULT(bool_bits == 0xf);
        }

        /* Elected / FirstLane */
        for (j = 0; j < 4; j++)
        {
            if (j == first_lane)
                t->output_data[0][j] |= ELECTED(1);
            if (j == 0)
                t->output_data[1][j] |= ELECTED(1);

            t->output_data[0][j] |= FIRST_RESULT(first_lane);
            t->output_data[1][j] |= FIRST_RESULT(0);
        }

        /* Ballot */
        for (j = 0; j < 4; j++)
        {
            t->output_data[0][j] |= BALLOT(exec_bits);
            t->output_data[1][j] |= BALLOT(0xf);
        }

        /* Active sum / prefix */
        for (j = 0; j < 4; j++)
        {
            t->output_data[0][j] |= ACTIVE_SUM(active_sum);
            t->output_data[1][j] |= ACTIVE_SUM(1 + 2 + 3);
            t->output_data[0][j] |= ACTIVE_PREFIX_BITS(vkd3d_popcount(exec_bits & ((1u << j) - 1u)));
            t->output_data[1][j] |= ACTIVE_PREFIX_BITS(j);
        }

        /* Mask out discard. */
        for (j = 0; j < 4; j++)
        {
            if (!(exec_bits & (1u << j)))
            {
                t->output_data[0][j] = 1000;
                t->output_data[1][j] = 1000;
            }
        }
    }

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32_UINT;
    desc.rt_width = 2;
    desc.rt_height = 2;
    desc.no_pipeline = true;
    desc.no_root_signature = true;

    if (!init_test_context(&context, &desc))
        return;

    model.HighestShaderModel = D3D_SHADER_MODEL_6_7;
    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
        model.HighestShaderModel < D3D_SHADER_MODEL_6_7)
    {
        skip("Shader model 6.7 is not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;

    hr = create_root_signature(context.device, &rs_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);

    init_pipeline_state_desc_shaders(&pso_desc, context.root_signature, DXGI_FORMAT_R32_UINT, NULL, NULL, 0, NULL, 0);
    pso_desc.VS = vs_helper_lane_wave_ops_dxil;
    pso_desc.PS = ps_helper_lane_wave_ops_disabled_dxil;
    pso_desc.DepthStencilState.DepthEnable = FALSE;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&psos[0]);
    ok(SUCCEEDED(hr), "Failed to create PSO, hr #%x.\n", hr);
    pso_desc.PS = ps_helper_lane_wave_ops_enabled_dxil;
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&psos[1]);
    ok(SUCCEEDED(hr), "Failed to create PSO, hr #%x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(tests) * 2; i++)
    {
        vkd3d_test_set_context("Test %u (helpers: %u)", i / 2, i % 2);

        src = create_upload_buffer(context.device, sizeof(tests[i / 2].input_data), tests[i / 2].input_data);

        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, default_color, 0, NULL);
        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, psos[i & 1]);
        ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(src));
        set_viewport(&vp, 0, 0, 2, 2, 0, 1);
        set_rect(&sci, 0, 0, 2, 2);
        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
        ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_texture_readback_with_command_list(context.render_target, 0, &rb, context.queue, context.list);

        for (j = 0; j < 4; j++)
        {
            uint32_t expected = tests[i / 2].output_data[i % 2][j];
            uint32_t v = get_readback_uint(&rb, j % 2, j / 2, 0);
            ok(expected == v, "(%u, %u): expected #%x, got #%x.\n", j % 2, j / 2, expected, v);
        }

        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12Resource_Release(src);
        release_resource_readback(&rb);
    }
    vkd3d_test_set_context(NULL);

    for (i = 0; i < ARRAY_SIZE(psos); i++)
        ID3D12PipelineState_Release(psos[i]);
    destroy_test_context(&context);
}

void test_quad_vote_sm67_compute(void)
{
    D3D12_FEATURE_DATA_SHADER_MODEL model;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[2];
    struct test_context context;
    struct resource_readback rb;
    unsigned int i, j, k;
    ID3D12Resource *src;
    ID3D12Resource *dst;
    HRESULT hr;

#include "shaders/sm_advanced/headers/cs_quad_vote.h"

#define ONE_OR_ZERO 1000 /* Sentinel */
    static const struct test
    {
        uint32_t input_data[4];
        /* Per input: Any, All, Any (masked lanes), All (masked lanes) */
        uint32_t expected_output[4][4];
    } tests[] = {
        {
            /* Nothing */
            { 1, 1, 1, 1 },
            {
                { 0, 0, 0, 0 },
                { 0, 0, 0, 0 },
                { 0, 0, 0, 0 },
                { 0, 0, 0, 0 },
            },
        },
        {
            /* Any should trigger, not all */
            { 3, 1, 1, 1 },
            {
                { 1, 0, 1, 0 },
                { 1, 0, 1, 0 },
                { 1, 0, 1, 0 },
                { 1, 0, 1, 0 },
            },
        },
        {
            /* All should trigger */
            { 3, 3, 3, 3 },
            {
                { 1, 1, 1, 1 },
                { 1, 1, 1, 1 },
                { 1, 1, 1, 1 },
                { 1, 1, 1, 1 },
            },
        },
        {
            /* Test all in control flow. First three threads enter branch and use all(). This is UB, meaning we can implement it as quad broadcasts in SPIR-V just fine.
             * From docs: 
             *  Since these routines rely on quad-level values,
             *  they assume that all lanes in the quad are active,
             *  including helper lanes (those that are masked from final writes).
             *  This means they should be treated like the existing DDX and DDY intrinsics in that sense.
             * AMD does not change result based on control flow here. NV returns true for masked all however ... */
            { 3, 3, 3, 0 },
            {
                { 1, 0, 1, ONE_OR_ZERO },
                { 1, 0, 1, ONE_OR_ZERO },
                { 1, 0, 1, ONE_OR_ZERO },
                { 1, 0, ~0u, ~0u },
            },
        },
        {
            /* Same idea, but any where we prove spilling from a neighbor lane that should be inactive. */
            { 1, 1, 1, 2 },
            {
                { 1, 0, ONE_OR_ZERO, 0 },
                { 1, 0, ONE_OR_ZERO, 0 },
                { 1, 0, ONE_OR_ZERO, 0 },
                { 1, 0, ~0u, ~0u },
            },
        },
    };

    if (!init_compute_test_context(&context))
        return;

    model.HighestShaderModel = D3D_SHADER_MODEL_6_7;
    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
        model.HighestShaderModel < D3D_SHADER_MODEL_6_7)
    {
        skip("Shader model 6.7 is not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;

    hr = create_root_signature(context.device, &rs_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);

    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature, cs_quad_vote_dxil);
    ok(!!context.pipeline_state, "Failed to create pipeline state.\n");

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);
        src = create_upload_buffer(context.device, sizeof(tests[i].input_data), tests[i].input_data);
        dst = create_default_buffer(context.device, sizeof(tests[i].expected_output), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(src));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(dst));
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

        transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(dst, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

        for (j = 0; j < 4; j++)
        {
            for (k = 0; k < 4; k++)
            {
                uint32_t expected = tests[i].expected_output[j][k];
                uint32_t v = get_readback_uint(&rb, j * 4 + k, 0, 0);
                if (expected == ONE_OR_ZERO)
                    ok(v <= 1, "(%u, %u), expected 0 or 1, got %u.\n", j, k, v);
                else
                    ok(expected == v, "(%u, %u), expected %u, got %u.\n", j, k, expected, v);
            }
        }

        reset_command_list(context.list, context.allocator);
        ID3D12Resource_Release(src);
        ID3D12Resource_Release(dst);
        release_resource_readback(&rb);
    }
    vkd3d_test_set_context(NULL);

    destroy_test_context(&context);
}

void test_sm67_multi_sample_uav(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS14 features14;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    struct test_context_desc context_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL model;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_DESCRIPTOR_RANGE rs_range[1];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[1];
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_h;
    ID3D12DescriptorHeap *desc_heap;
    struct test_context context;
    struct resource_readback rb;
    ID3D12Resource *resolve_dst;
    unsigned int i, x, y;
    ID3D12Resource *src;
    ID3D12Resource *dst;
    HRESULT hr;

#include "shaders/sm_advanced/headers/cs_multisample_uav.h"

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_render_target = true;
    context_desc.no_root_signature = true;

    if (!init_test_context(&context, &context_desc))
        return;

    model.HighestShaderModel = D3D_SHADER_MODEL_6_7;
    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
            model.HighestShaderModel < D3D_SHADER_MODEL_6_7)
    {
        skip("Shader model 6.7 is not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS14, &features14, sizeof(features14))) ||
            !features14.WriteableMSAATexturesSupported)
    {
        skip("Writable MSAA textures not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    memset(rs_range, 0, sizeof(rs_range));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(rs_range);
    rs_params[0].DescriptorTable.pDescriptorRanges = rs_range;
    rs_range[0].NumDescriptors = 3;
    rs_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    memset(&resource_desc, 0, sizeof(resource_desc));
    memset(&heap_properties, 0, sizeof(heap_properties));
    resource_desc.Width = 4;
    resource_desc.Height = 4;
    resource_desc.DepthOrArraySize = 4;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Format = DXGI_FORMAT_R32_FLOAT;
    resource_desc.SampleDesc.Count = 4;
    resource_desc.MipLevels = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    /* Validation requires RTV usage to be set for MSAA. */
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
            &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void**)&src);
    ok(hr == E_INVALIDARG, "Unexpected hr #%x.\n", hr);
    resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
            &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void **)&src);
    ok(SUCCEEDED(hr), "Failed to create UAV MSAA texture, hr #%x.\n", hr);

    dst = create_default_buffer(context.device,
            resource_desc.Width * resource_desc.Height *
                    resource_desc.DepthOrArraySize * resource_desc.SampleDesc.Count * sizeof(float),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    resolve_dst = create_default_texture2d(context.device, resource_desc.Width, resource_desc.Height, resource_desc.DepthOrArraySize,
            1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_RESOLVE_DEST);

    desc_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);

    cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(desc_heap);
    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DMS;
    uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
    ID3D12Device_CreateUnorderedAccessView(context.device, src, NULL, &uav_desc, cpu_h);
    cpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY;
    uav_desc.Texture2DMSArray.ArraySize = 4;
    ID3D12Device_CreateUnorderedAccessView(context.device, src, NULL, &uav_desc, cpu_h);
    cpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.NumElements = resource_desc.Width * resource_desc.Height * resource_desc.DepthOrArraySize;
    uav_desc.Buffer.StructureByteStride = 16;
    uav_desc.Format = DXGI_FORMAT_UNKNOWN;
    ID3D12Device_CreateUnorderedAccessView(context.device, dst, NULL, &uav_desc, cpu_h);

    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature, cs_multisample_uav_dxil);
    ok(!!context.pipeline_state, "Failed to create compute pipeline.\n");

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &desc_heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(desc_heap));
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
    transition_resource_state(context.list, src, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    for (i = 0; i < 4; i++)
        ID3D12GraphicsCommandList_ResolveSubresource(context.list, resolve_dst, i, src, i, DXGI_FORMAT_R32_FLOAT);
    transition_resource_state(context.list, resolve_dst, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(dst, DXGI_FORMAT_R32_FLOAT, &rb, context.queue, context.list);
    for (i = 0; i < 4 * 4 * 4 * 4; i++)
    {
        uint32_t layer, sample;
        float v, expected;
        v = get_readback_float(&rb, i, 0);
        expected = (float)i;
        sample = i % 4;
        x = (i / 4) % 4;
        y = (i / 16) % 4;
        layer = i / 64;
        ok(v == expected, "(%u, %u, %u, sample %u): expected %f, got %f.\n", x, y, layer, sample, expected, v);
    }

    reset_command_list(context.list, context.allocator);
    release_resource_readback(&rb);

    for (i = 0; i < 4; i++)
    {
        get_texture_readback_with_command_list(resolve_dst, i, &rb, context.queue, context.list);

        for (y = 0; y < 4; y++)
        {
            for (x = 0; x < 4; x++)
            {
                float v, expected;
                v = get_readback_float(&rb, x, y);
                expected = 1.5f + 4.0f * (float)(i * 16 + y * 4 + x);
                ok(v == expected, "(%u, %u, %u): expected %f, got %f.\n", x, y, i, expected, v);
            }
        }

        reset_command_list(context.list, context.allocator);
        release_resource_readback(&rb);
    }

    ID3D12DescriptorHeap_Release(desc_heap);
    ID3D12Resource_Release(src);
    ID3D12Resource_Release(dst);
    ID3D12Resource_Release(resolve_dst);
    destroy_test_context(&context);
}

void test_sm67_sample_cmp_level(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS14 features14;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    D3D12_STATIC_SAMPLER_DESC sampler_desc;
    struct test_context_desc context_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL model;
    D3D12_DESCRIPTOR_RANGE rs_range[2];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[1];
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_h;
    ID3D12DescriptorHeap *desc_heap;
    ID3D12DescriptorHeap *dsv_heap;
    struct test_context context;
    struct resource_readback rb;
    unsigned int i, x, y;
    ID3D12Resource *src;
    ID3D12Resource *dst;
    HRESULT hr;

#include "shaders/sm_advanced/headers/cs_sample_cmp_level.h"

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_render_target = true;
    context_desc.no_root_signature = true;

    if (!init_test_context(&context, &context_desc))
        return;

    model.HighestShaderModel = D3D_SHADER_MODEL_6_7;
    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
        model.HighestShaderModel < D3D_SHADER_MODEL_6_7)
    {
        skip("Shader model 6.7 is not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS14, &features14, sizeof(features14))) ||
        !features14.AdvancedTextureOpsSupported)
    {
        skip("Advanced texture ops not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    memset(rs_range, 0, sizeof(rs_range));
    memset(&sampler_desc, 0, sizeof(sampler_desc));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_desc.NumStaticSamplers = 1;
    rs_desc.pStaticSamplers = &sampler_desc;

    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    sampler_desc.MaxLOD = 1000.0f;
    sampler_desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(rs_range);
    rs_params[0].DescriptorTable.pDescriptorRanges = rs_range;
    rs_range[0].NumDescriptors = 1;
    rs_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rs_range[1].NumDescriptors = 1;
    rs_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rs_range[1].OffsetInDescriptorsFromTableStart = 1;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature, cs_sample_cmp_level_dxil);
    ok(!!context.pipeline_state, "Failed to create pipeline state.\n");

    src = create_default_texture2d(context.device, 8, 8, 1, 4, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    dst = create_default_texture2d(context.device, 8, 8, 1, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    desc_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    dsv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 3);

    for (i = 0; i < 4; i++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(dsv_heap);
        cpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        memset(&dsv_desc, 0, sizeof(dsv_desc));
        dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
        dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsv_desc.Texture2D.MipSlice = i;
        ID3D12Device_CreateDepthStencilView(context.device, src, &dsv_desc, cpu_h);
        ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, cpu_h,
                D3D12_CLEAR_FLAG_DEPTH, 0.25f * (float)(i + 1), 0, 0, NULL);
    }

    transition_resource_state(context.list, src, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(desc_heap);
    memset(&uav_desc, 0, sizeof(uav_desc));
    memset(&srv_desc, 0, sizeof(srv_desc));

    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 4;
    ID3D12Device_CreateShaderResourceView(context.device, src, &srv_desc, cpu_h);
    cpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
    ID3D12Device_CreateUnorderedAccessView(context.device, dst, NULL, &uav_desc, cpu_h);

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &desc_heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(desc_heap));
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
    transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(dst, 0, &rb, context.queue, context.list);

    for (y = 0; y < 8; y++)
    {
        for (x = 0; x < 8; x++)
        {
            float expected, ref_value, v, mip;
            int int_mip;

            v = get_readback_float(&rb, x, y);
            mip = 1.0f / 16.0f + 4.0f * (float)(y) / 8.0f;
            ref_value = (float)x / 8.0f;

            int_mip = (int)(mip + 0.5f);
            if (int_mip > 3)
                int_mip = 3;

            expected = 0.25f * (float)(int_mip + 1);
            expected = ref_value <= expected ? 1.0f : 0.0f;
            ok(v == expected, "%u, %u: expected %f, got %f.\n", x, y, expected, v);
        }
    }

    release_resource_readback(&rb);
    ID3D12DescriptorHeap_Release(dsv_heap);
    ID3D12DescriptorHeap_Release(desc_heap);
    ID3D12Resource_Release(src);
    ID3D12Resource_Release(dst);
    destroy_test_context(&context);
}

void test_sm67_dynamic_texture_offset(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS14 features14;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_STATIC_SAMPLER_DESC sampler_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL model;
    D3D12_DESCRIPTOR_RANGE rs_range[1];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[3];
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_h;
    ID3D12DescriptorHeap *desc_heap;
    D3D12_SUBRESOURCE_DATA data[4];
    ID3D12Resource *coord_buffer;
    struct test_context context;
    struct resource_readback rb;
    ID3D12Resource *src;
    ID3D12Resource *dst;
    unsigned int i;
    HRESULT hr;

#include "shaders/sm_advanced/headers/cs_dynamic_texture_offset.h"

    static const struct test_input
    {
        float u, v, mip;
        int32_t offset_x, offset_y;
    } input_coordinates[4] = {
        { 0.01f, 0.01f, 0.5f, 9, -13 },
        { 0.51f, 0.01f, 0.75f, 9, -13 },
        { 0.21f, -0.01f, 1.5f, 3, -13 },
        { 0.01f, 0.51f, 1.75f, 0, -4 },
    };

    uint8_t input_texture_data[4][16][16];

    if (!init_compute_test_context(&context))
        return;

    model.HighestShaderModel = D3D_SHADER_MODEL_6_7;
    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
            model.HighestShaderModel < D3D_SHADER_MODEL_6_7)
    {
        skip("Shader model 6.7 is not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS14, &features14, sizeof(features14))) ||
            !features14.AdvancedTextureOpsSupported)
    {
        skip("Advanced texture ops not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    memset(rs_range, 0, sizeof(rs_range));
    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.MaxLOD = 1000.0f;
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_desc.pStaticSamplers = &sampler_desc;
    rs_desc.NumStaticSamplers = 1;

    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(rs_range);
    rs_params[0].DescriptorTable.pDescriptorRanges = rs_range;
    rs_range[0].NumDescriptors = 1;
    rs_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rs_params[1].Descriptor.RegisterSpace = 1;

    rs_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rs_params[2].Descriptor.RegisterSpace = 1;

    create_root_signature(context.device, &rs_desc, &context.root_signature);

    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature, cs_dynamic_texture_offset_dxil);
    ok(!!context.pipeline_state, "Failed to create compute pipeline.\n");

    src = create_default_texture2d(context.device, 16, 16, 1, 4, DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    coord_buffer = create_upload_buffer(context.device, sizeof(input_coordinates), input_coordinates);
    dst = create_default_buffer(context.device, ARRAY_SIZE(input_coordinates) * sizeof(float),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    srand(1234);
    for (i = 0; i < sizeof(input_texture_data); i++)
        (&input_texture_data[0][0][0])[i] = (uint8_t)rand();

    memset(data, 0, sizeof(data));
    for (i = 0; i < ARRAY_SIZE(data); i++)
    {
        data[i].pData = input_texture_data[i];
        data[i].RowPitch = sizeof(input_texture_data[0][0]);
        data[i].SlicePitch = sizeof(input_texture_data[0]);
    }
    upload_texture_data(src, data, ARRAY_SIZE(data), context.queue, context.list);
    reset_command_list(context.list, context.allocator);

    desc_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(desc_heap);
    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Format = DXGI_FORMAT_R8_UNORM;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = 4;
    ID3D12Device_CreateShaderResourceView(context.device, src, &srv_desc, cpu_h);

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &desc_heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(desc_heap));
    ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(coord_buffer));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 2, ID3D12Resource_GetGPUVirtualAddress(dst));
    ID3D12GraphicsCommandList_Dispatch(context.list, ARRAY_SIZE(input_coordinates), 1, 1);
    transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(dst, DXGI_FORMAT_R32_FLOAT, &rb, context.queue, context.list);
    for (i = 0; i < ARRAY_SIZE(input_coordinates); i++)
    {
        const struct test_input *t = &input_coordinates[i];
        int mip_lo, mip_hi;
        int int_u, int_v;
        float mip_frac;
        float samp_lo;
        float samp_hi;
        int mip_size;
        float ref;
        float v;

        mip_lo = (int)t->mip;
        mip_hi = mip_lo + 1;
        if (mip_hi > 3)
            mip_hi = 3;
        mip_frac = t->mip - (float)mip_lo;

        /* The offset is applied per level, which is basically impossible to emulate. */
        mip_size = 16 >> mip_lo;
        int_u = (int)floorf(t->u * (float)mip_size);
        int_v = (int)floorf(t->v * (float)mip_size);
#define sext4(x) (((int32_t)(x) << 28) >> 28)
        int_u += sext4(t->offset_x);
        int_v += sext4(t->offset_y);
        int_u &= mip_size - 1;
        int_v &= mip_size - 1;
        samp_lo = (float)input_texture_data[mip_lo][int_v][int_u] / 255.0f;

        mip_size = 16 >> mip_hi;
        int_u = (int)floorf(t->u * (float)mip_size);
        int_v = (int)floorf(t->v * (float)mip_size);
        int_u += sext4(t->offset_x);
        int_v += sext4(t->offset_y);
        int_u &= mip_size - 1;
        int_v &= mip_size - 1;
        samp_hi = (float)input_texture_data[mip_hi][int_v][int_u] / 255.0f;

        ref = samp_lo * (1.0f - mip_frac) + samp_hi * mip_frac;
        v = get_readback_float(&rb, i, 0);
        ok(fabsf(ref - v) <= 1.1f / 255.0f, "Coord %u: (%f, %f, %f, %d, %d), expected %f, got %f.\n", i, t->u, t->v, t->mip, t->offset_x, t->offset_y, ref, v);
    }

    release_resource_readback(&rb);
    ID3D12DescriptorHeap_Release(desc_heap);
    ID3D12Resource_Release(src);
    ID3D12Resource_Release(dst);
    ID3D12Resource_Release(coord_buffer);
    destroy_test_context(&context);
}

void test_sm67_raw_gather(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS14 features14;
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 features4;
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 features1;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_STATIC_SAMPLER_DESC sampler_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL model;
    D3D12_DESCRIPTOR_RANGE rs_range[1];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[2];
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_h;
    ID3D12DescriptorHeap *desc_heap;
    D3D12_SUBRESOURCE_DATA data;
    struct test_context context;
    struct resource_readback rb;
    ID3D12PipelineState *pso;
    ID3D12Resource *src;
    ID3D12Resource *dst;
    bool supports_int64;
    bool supports_int16;
    unsigned int i, j;
    HRESULT hr;

#include "shaders/sm_advanced/headers/cs_raw_gather_16.h"
#include "shaders/sm_advanced/headers/cs_raw_gather_32.h"
#include "shaders/sm_advanced/headers/cs_raw_gather_64.h"

    static const struct test
    {
        uint32_t inputs[8];
        uint32_t outputs[80 / 4];
        D3D12_SHADER_BYTECODE cs;
        /* Need to test how swizzling affects the raw gather.
         * The assumption is that implementation can just GatherRed or GatherRed | (GatherGreen << 32) for 64-bit (R32G32_UINT view). */
        UINT swizzle;
        DXGI_FORMAT dxgi_format;
        bool requires_int16;
        bool requires_int64;
        unsigned int row_pitch;
        unsigned int slice_size;
    } tests[] = {
        {
            { 1000 | (1001 << 16), 1002 | (1003 << 16) },
            { 1002 | (1003 << 16), 1001 | (1000 << 16),
              1002 | (1003 << 16), 1001 | (1000 << 16),
              0, 0,
              0x10001, 0x10001,
              1002 | (1003 << 16), 1001 | (1000 << 16),
            },
            SHADER_BYTECODE(cs_raw_gather_16_code_dxil),
            D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
                D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
                D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0),
            DXGI_FORMAT_R16_UINT,
            true, false,
            4, 8,
        },
        {
            { 1000, 1001, 1002, 1003 },
            { 1002, 1003, 1001, 1000,
              1002, 1003, 1001, 1000,
                 0,    0,    0,    0,
                 1,    1,    1,    1,
              1002, 1003, 1001, 1000
            },
            SHADER_BYTECODE(cs_raw_gather_32_code_dxil),
            D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
                D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
                D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0),
            DXGI_FORMAT_R32_UINT,
            false, false,
            8, 16,
        },
        {
            { 1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007 },
            { 1004, 1005, 1006, 1007, 1002, 1003, 1000, 1001 },
            SHADER_BYTECODE(cs_raw_gather_64_code_dxil),
            D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
                D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
                D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0),
            DXGI_FORMAT_R32G32_UINT,
            false, true,
            16, 32,
        },
        {
            { 1000 | (1001 << 16), 1002 | (1003 << 16) },
            { 0x10001, 0x10001,
              0x10001, 0x10001,
                    0,       0,
              0x10001, 0x10001,
              1002 | (1003 << 16), 1001 | (1000 << 16),
            },
            SHADER_BYTECODE(cs_raw_gather_16_code_dxil),
            /* Prove that swizzle affects the gather, it's literally just GatherRed in disguise <_< */
            D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
                D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0),
            DXGI_FORMAT_R16_UINT,
            true, false,
            4, 8,
        },
        {
            { 1000, 1001, 1002, 1003 },
            {    1,    1,    1,    1,
                 1,    1,    1,    1,
                 0,    0,    0,    0,
                 1,    1,    1,    1,
              1002, 1003, 1001, 1000,
            },
            SHADER_BYTECODE(cs_raw_gather_32_code_dxil),
            D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
                D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0),
            DXGI_FORMAT_R32_UINT,
            false, false,
            8, 16,
        },
        {
            { 1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007 },
            { 1005, 1004, 1007, 1006, 1003, 1002, 1001, 1000 },
            SHADER_BYTECODE(cs_raw_gather_64_code_dxil),
            /* Swap components. Proves 64-bit raw gather is just two separate gathers. */
            D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
                D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,
                D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0),
            DXGI_FORMAT_R32G32_UINT,
            false, true,
            16, 32,
        },
        {
            { 0x1001, 0x1102, 0x1203, 0x1304, },
            {
              0x03, 0x04, 0x02, 0x01,
              0x03, 0x04, 0x02, 0x01,
              0x12, 0x13, 0x11, 0x10,
              0, 0, 0, 0,
              0, 0, 0, 0,
            },
            SHADER_BYTECODE(cs_raw_gather_32_code_dxil),
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            /* Prove that the image view must be single component 16-bit / 32-bit
             * and we're not expected to emit magic opcodes that bypass the descriptor type. */
            DXGI_FORMAT_R8G8B8A8_UINT,
            false, false,
            8, 16,
        },
    };

    if (!init_compute_test_context(&context))
        return;

    model.HighestShaderModel = D3D_SHADER_MODEL_6_7;
    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
        model.HighestShaderModel < D3D_SHADER_MODEL_6_7)
    {
        skip("Shader model 6.7 is not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS14, &features14, sizeof(features14))) ||
        !features14.AdvancedTextureOpsSupported)
    {
        skip("Advanced texture ops not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (SUCCEEDED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS1, &features1, sizeof(features1))) &&
        features1.Int64ShaderOps)
    {
        supports_int64 = true;
    }
    else
        supports_int64 = false;

    if (SUCCEEDED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS4, &features4, sizeof(features4))) &&
        features4.Native16BitShaderOpsSupported)
    {
        supports_int16 = true;
    }
    else
        supports_int16 = false;

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    memset(rs_range, 0, sizeof(rs_range));
    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_desc.pStaticSamplers = &sampler_desc;
    rs_desc.NumStaticSamplers = 1;

    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(rs_range);
    rs_params[0].DescriptorTable.pDescriptorRanges = rs_range;
    rs_range[0].NumDescriptors = 1;
    rs_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;

    desc_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    create_root_signature(context.device, &rs_desc, &context.root_signature);

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        if (tests[i].requires_int16 && !supports_int16)
        {
            skip("Skipping test due to missing int16 support.\n");
            continue;
        }

        if (tests[i].requires_int64 && !supports_int64)
        {
            skip("Skipping test due to missing int64 support.\n");
            continue;
        }

        pso = create_compute_pipeline_state(context.device, context.root_signature, tests[i].cs);
        ok(!!pso, "Failed to create compute pipeline.\n");

        src = create_default_texture2d(context.device, 2, 2, 1, 1, tests[i].dxgi_format,
                D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        dst = create_default_buffer(context.device, sizeof(tests[i].outputs),
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        data.pData = tests[i].inputs;
        data.RowPitch = tests[i].row_pitch;
        data.SlicePitch = tests[i].slice_size;
        upload_texture_data(src, &data, 1, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(desc_heap);
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Format = tests[i].dxgi_format;
        srv_desc.Shader4ComponentMapping = tests[i].swizzle;
        srv_desc.Texture2D.MipLevels = 1;
        ID3D12Device_CreateShaderResourceView(context.device, src, &srv_desc, cpu_h);

        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &desc_heap);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(desc_heap));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(dst));
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
        transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(dst, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        for (j = 0; j < ARRAY_SIZE(tests[i].outputs); j++)
        {
            uint32_t expected, v;

            expected = tests[i].outputs[j];
            v = get_readback_uint(&rb, j, 0, 0);
            ok(v == expected, "uint %u: expected #%x, got #%x.\n", j, expected, v);
        }

        ID3D12PipelineState_Release(pso);
        ID3D12Resource_Release(src);
        ID3D12Resource_Release(dst);
        release_resource_readback(&rb);
    }
    vkd3d_test_set_context(NULL);

    ID3D12DescriptorHeap_Release(desc_heap);
    destroy_test_context(&context);
}

void test_sm67_integer_sampling(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS14 features14;
    D3D12_FEATURE_DATA_ROOT_SIGNATURE rs_features;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL model;
    struct test_context_desc context_desc;
    ID3D12DescriptorHeap *desc_heaps[2];
    D3D12_ROOT_PARAMETER1 rs_params[1];
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_h;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Device11 *device11;
    ID3D12Resource *dst;
    unsigned int i;
    HRESULT hr;

#include "shaders/sm_advanced/headers/cs_integer_sampling.h"

    static const struct input_textures_test
    {
        DXGI_FORMAT tex_format;
        DXGI_FORMAT view_format;
        D3D12_RESOURCE_FLAGS flags;
        D3D12_RESOURCE_STATES initial_state;
        unsigned int plane;
        uint8_t data[16];
        unsigned int row_pitch;
        unsigned int slice_pitch;
    } input_textures[] = {
        {
            DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_UINT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, 0,
            { 1, 2, 3, 4 }, 2, 4,
        },
        {
            DXGI_FORMAT_R8_SINT, DXGI_FORMAT_R8_SINT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, 0,
            { 0xfe, 0xff, 0, 1 }, 2, 4,
        },
        {
            DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_UINT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, 0,
            { 1, 0, 2, 0, 3, 0, 4, 0 }, 4, 8,
        },
        {
            DXGI_FORMAT_R16_SINT, DXGI_FORMAT_R16_SINT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, 0,
            { 0xfe, 0xff, 0xff, 0xff, 0, 0, 1, 0 }, 4, 8,
        },
        {
            DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, 0,
            { 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0 }, 8, 16,
        },
        {
            DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32_SINT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, 0,
            { 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 1, 0, 0, 0 }, 8, 16,
        },
        {
            /* Special case, test stencil with border color. */
            DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_COPY_DEST, 1,
            { 1, 2, 3, 4 }, 2, 4,
        },
    };
    ID3D12Resource *srcs[ARRAY_SIZE(input_textures)];

    static const struct sampler_descs
    {
        D3D12_FILTER filter;
        bool border_color_enable;
        UINT border_color[4];
    } samplers[] = {
        { D3D12_FILTER_MIN_MAG_MIP_POINT, false },
        { D3D12_FILTER_MINIMUM_MIN_MAG_MIP_POINT, false },
        { D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_POINT, false },
        /* Apparently, to get well-behaved results, it needs to be within range in uint32_t or int32_t reinterpretation.
         * AMD behavior when out of bounds is to clamp, NV behavior seems to be a mix of masking and extending.
         * A UINT8 texture can end up with > 0xff values in the border for example. To make the test repeatable,
         * dynamically select sampler based on the image index. */
        { D3D12_FILTER_MIN_MAG_MIP_POINT, true, { 0xf0, 0xf1, 0xf2, 0xf3 } },
        { D3D12_FILTER_MIN_MAG_MIP_POINT, true, { 0xfffffff0, 0xfffffff1, 0xfffffff2, 0xfffffff3 } },
        { D3D12_FILTER_MIN_MAG_MIP_POINT, true, { 0xfff0, 0xfff1, 0xfff2, 0xfff3 } },
        { D3D12_FILTER_MIN_MAG_MIP_POINT, true, { 0xfffff000, 0xfffff001, 0xfffff002, 0xfffff003 } },
        { D3D12_FILTER_MIN_MAG_MIP_POINT, true, { 0xfffffff0, 0xfffffff1, 0xfffffff2, 0xfffffff3 } },
        { D3D12_FILTER_MIN_MAG_MIP_POINT, true, { 0xf0000000, 0xf0000001, 0xf0000002, 0xf0000003 } },
        /* AMD and NV do not agree if R or G border color should be sampled in native D3D12 for stencil.
         * We observe same results in Vulkan. */
        { D3D12_FILTER_MIN_MAG_MIP_POINT, true, { 0xf4, 0xf4, 0xf6, 0xf7 } },
    };
    D3D12_SAMPLER_DESC2 sampler_descs[ARRAY_SIZE(samplers)];

    static const struct static_sampler_descs
    {
        D3D12_FILTER filter;
        bool border_color_enable;
        D3D12_STATIC_BORDER_COLOR border_color;
    } static_samplers[] = {
        { D3D12_FILTER_MIN_MAG_MIP_POINT, false },
        { D3D12_FILTER_MINIMUM_MIN_MAG_MIP_POINT, false },
        { D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_POINT, false },
        { D3D12_FILTER_MIN_MAG_MIP_POINT, true, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT },
        { D3D12_FILTER_MIN_MAG_MIP_POINT, true, D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT },
    };
    D3D12_STATIC_SAMPLER_DESC1 static_sampler_descs[ARRAY_SIZE(static_samplers)];

    /* See comments about border color variants in sampler_descs. */
#define SAMPLER_DESC_TESTS (4 + ARRAY_SIZE(static_sampler_descs))

    /* Integer border color behavior. For UINT: clamp the border color to format range. For SINT, reinterpret as int32_t, clamp to format range. */
    static const struct uvec4 reference_output[SAMPLER_DESC_TESTS * 7] =
    {
        /* *** Static samplers *** */
        /* POINT sampled */
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        /* MIN sampled, it's deceptive due to POINT. It does not take minimum at all. */
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        /* MAX sampled, it's deceptive due to POINT. It does not take maximum at all. */
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        /* Black border. */
        { 0, 1, 4, 0 },
        { 0, -2u, 1, 0 },
        { 0, 1, 4, 0 },
        { 0, -2u, 1, 0 },
        { 0, 1, 4, 0 },
        { 0, -2u, 1, 0 },
        { 0, 1, 4, 0 },
        /* White border. */
        { 1, 1, 4, 1 },
        { 1, -2u, 1, 1 },
        { 1, 1, 4, 1 },
        { 1, -2u, 1, 1 },
        { 1, 1, 4, 1 },
        { 1, -2u, 1, 1 },
        { 1, 1, 4, 1 },
        /* *** Heap samplers *** */
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        /* MIN sampled, it's deceptive due to POINT. It does not take minimum at all. */
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        /* MAX sampled, it's deceptive due to POINT. It does not take maximum at all. */
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        { 1, -2u, 1, -2u },
        { 4, 1, 4, 1 },
        /* Border. */
        { 0xf0, 1, 4, 0xf0 },
        { 0xfffffff0, -2u, 1, 0xfffffff0 },
        { 0xfff0, 1, 4, 0xfff0 },
        { 0xfffff000, -2u, 1, 0xfffff000 },
        { 0xfffffff0, 1, 4, 0xfffffff0 },
        { 0xf0000000, -2u, 1, 0xf0000000 },
        { 0xf4, 1, 4, 0xf4 },
    };

    memset(sampler_descs, 0, sizeof(sampler_descs));
    memset(static_sampler_descs, 0, sizeof(static_sampler_descs));

    for (i = 0; i < ARRAY_SIZE(sampler_descs); i++)
    {
        sampler_descs[i].AddressU = samplers[i].border_color_enable ? D3D12_TEXTURE_ADDRESS_MODE_BORDER : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_descs[i].AddressV = sampler_descs[i].AddressU;
        sampler_descs[i].AddressW = sampler_descs[i].AddressU;
        sampler_descs[i].Filter = samplers[i].filter;
        sampler_descs[i].Flags = samplers[i].border_color_enable ? D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR : D3D12_SAMPLER_FLAG_NONE;
        memcpy(sampler_descs[i].UintBorderColor, samplers[i].border_color, sizeof(samplers[i].border_color));
    }

    for (i = 0; i < ARRAY_SIZE(static_sampler_descs); i++)
    {
        static_sampler_descs[i].AddressU = static_samplers[i].border_color_enable ? D3D12_TEXTURE_ADDRESS_MODE_BORDER : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        static_sampler_descs[i].AddressV = static_sampler_descs[i].AddressU;
        static_sampler_descs[i].AddressW = static_sampler_descs[i].AddressU;
        static_sampler_descs[i].Filter = static_samplers[i].filter;
        static_sampler_descs[i].Flags = static_samplers[i].border_color_enable ? D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR : D3D12_SAMPLER_FLAG_NONE;
        static_sampler_descs[i].ShaderRegister = i;
        static_sampler_descs[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        static_sampler_descs[i].BorderColor = static_samplers[i].border_color;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;

    rs_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_2;
    rs_desc.Desc_1_2.NumStaticSamplers = ARRAY_SIZE(static_sampler_descs);
    rs_desc.Desc_1_2.pStaticSamplers = static_sampler_descs;
    rs_desc.Desc_1_2.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.Desc_1_2.pParameters = rs_params;
    rs_desc.Desc_1_2.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_render_target = true;
    context_desc.no_root_signature = true;
    if (!init_test_context(&context, &context_desc))
        return;

    if (!is_min_max_filtering_supported(context.device))
    {
        skip("Min-max filter is not supported.\n");
        destroy_test_context(&context);
        return;
    }

    model.HighestShaderModel = D3D_SHADER_MODEL_6_7;
    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
        model.HighestShaderModel < D3D_SHADER_MODEL_6_7)
    {
        skip("Shader model 6.7 is not supported.\n");
        destroy_test_context(&context);
        return;
    }

    rs_features.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_2;
    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_ROOT_SIGNATURE, &rs_features, sizeof(rs_features))) ||
        rs_features.HighestVersion < D3D_ROOT_SIGNATURE_VERSION_1_2)
    {
        skip("Root Signature version 1.2 is not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS14, &features14, sizeof(features14))) ||
        !features14.AdvancedTextureOpsSupported)
    {
        skip("Advanced texture ops not supported.\n");
        destroy_test_context(&context);
        return;
    }

    hr = create_versioned_root_signature(context.device, &rs_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);
    if (FAILED(hr))
    {
        skip("Root signature v1.2 not supported, hr #%x.\n", hr);
        destroy_test_context(&context);
        return;
    }

    hr = ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device11, (void **)&device11);
    ok(SUCCEEDED(hr), "Failed to query ID3D12Device11, hr #%x.\n", hr);
    if (FAILED(hr))
    {
        skip("ID3D12Device11 is not supported.\n");
        destroy_test_context(&context);
        return;
    }

    desc_heaps[0] = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ARRAY_SIZE(srcs));
    desc_heaps[1] = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, ARRAY_SIZE(sampler_descs));
    cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(desc_heaps[0]);

    for (i = 0; i < ARRAY_SIZE(srcs); i++)
    {
        D3D12_SUBRESOURCE_DATA data;

        srcs[i] = create_default_texture2d(context.device, 2, 2, 1, 1,
                input_textures[i].tex_format, input_textures[i].flags,
                input_textures[i].initial_state);

        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = input_textures[i].view_format;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        srv_desc.Texture2D.PlaneSlice = input_textures[i].plane;
        ID3D12Device_CreateShaderResourceView(context.device, srcs[i], &srv_desc, cpu_h);
        cpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        data.pData = input_textures[i].data;
        data.RowPitch = input_textures[i].row_pitch;
        data.SlicePitch = input_textures[i].slice_pitch;

        upload_texture_data_base(srcs[i], &data, input_textures[i].plane, 1, context.queue, context.list);
        reset_command_list(context.list, context.allocator);
    }

    cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(desc_heaps[1]);
    for (i = 0; i < ARRAY_SIZE(sampler_descs); i++)
    {
        ID3D12Device11_CreateSampler2(device11, &sampler_descs[i], cpu_h);
        cpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }

    dst = create_default_buffer(context.device, SAMPLER_DESC_TESTS * ARRAY_SIZE(srcs) * 16,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature, cs_integer_sampling_dxil);
    ok(!!context.pipeline_state, "Failed to create pipeline state.\n");

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 2, desc_heaps);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(dst));
    ID3D12GraphicsCommandList_Dispatch(context.list, ARRAY_SIZE(srcs), SAMPLER_DESC_TESTS, 1);
    transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(dst, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(reference_output); i++)
    {
        const struct uvec4 *expected, *v;
        expected = &reference_output[i];
        v = get_readback_uvec4(&rb, i, 0);
        ok(compare_uvec4(expected, v),
                "Output (tex %zu, samp %zu) failed: expected (#%x, #%x, #%x, #%x), got (#%x, #%x, #%x, #%x).\n",
                i % ARRAY_SIZE(srcs), i / ARRAY_SIZE(srcs),
                expected->x, expected->y, expected->z, expected->w, v->x, v->y, v->z, v->w);
    }

    release_resource_readback(&rb);
    for (i = 0; i < ARRAY_SIZE(srcs); i++)
        ID3D12Resource_Release(srcs[i]);
    ID3D12Resource_Release(dst);
    ID3D12DescriptorHeap_Release(desc_heaps[0]);
    ID3D12DescriptorHeap_Release(desc_heaps[1]);
    ID3D12Device11_Release(device11);

    destroy_test_context(&context);
}

void test_sm68_draw_parameters(void)
{
    ID3D12CommandSignature *simple_sig, *complex_sig;
    ID3D12DescriptorHeap *uav_heap, *uav_cpu_heap;
    D3D12_FEATURE_DATA_D3D12_OPTIONS21 options21;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_INDIRECT_ARGUMENT_DESC sig_args[2];
    D3D12_COMMAND_SIGNATURE_DESC sig_desc;
    struct test_context_desc context_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_ROOT_PARAMETER root_params[3];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12Resource *uav, *draw_buffer;
    struct resource_readback rb;
    struct test_context context;
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissor;
    unsigned int i, j;
    HRESULT hr;

    static const uint32_t zero_uint[] = { 0, 0, 0, 0 };

    struct draw_args
    {
        uint32_t draw_id;
        uint32_t vertex_count;
        uint32_t instance_count;
        uint32_t base_vertex;
        uint32_t base_instance;
    }
    *draw_args, tests[] =
    {
        { 0, 3, 1, 0, 0 },
        { 1, 3, 1, 4, 2 },
        { 2, 1, 5, 2, 8 },
        { 3, 4, 8, 7, 9 },
    };

    enum
    {
        MODE_DIRECT,
        MODE_EXECUTE_INDIRECT_SIMPLE,
        MODE_EXECUTE_INDIRECT_COMPLEX,
        MODE_COUNT
    };

#include "shaders/sm_advanced/headers/vs_draw_args.h"

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.no_render_target = true;
    context_desc.no_root_signature = true;
    if (!init_test_context(&context, &context_desc))
        return;

    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_8;
    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));
    if (FAILED(hr) || shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_8)
    {
        skip("Device does not support SM 6.8.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&options21, 0, sizeof(options21));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS21, &options21, sizeof(options21));

    if (!options21.ExtendedCommandInfoSupported)
    {
        skip("Extended command info not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(root_params, 0, sizeof(root_params));
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_params[2].Constants.Num32BitValues = 1; /* draw id */
    root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(root_params);
    rs_desc.pParameters = root_params;

    hr = create_root_signature(context.device, &rs_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    init_pipeline_state_desc_dxil(&pso_desc, context.root_signature,
            DXGI_FORMAT_UNKNOWN, &vs_draw_args_dxil, NULL, NULL);
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    memset(&pso_desc.PS, 0, sizeof(pso_desc.PS));

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    memset(sig_args, 0, sizeof(sig_args));
    sig_args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    sig_args[0].Constant.RootParameterIndex = 2;
    sig_args[0].Constant.Num32BitValuesToSet = 1;

    sig_args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    memset(&sig_desc, 0, sizeof(sig_desc));
    sig_desc.ByteStride = sizeof(struct draw_args);
    sig_desc.NumArgumentDescs = 1;
    sig_desc.pArgumentDescs = &sig_args[1];

    hr = ID3D12Device_CreateCommandSignature(context.device, &sig_desc, NULL, &IID_ID3D12CommandSignature, (void**)&simple_sig);
    ok(hr == S_OK, "Failed to create command signature, hr %#x.\n", hr);

    /* Be robust here in case the implementation does not support dgc */
    sig_desc.NumArgumentDescs = ARRAY_SIZE(sig_args);
    sig_desc.pArgumentDescs = sig_args;

    complex_sig = NULL;

    hr = ID3D12Device_CreateCommandSignature(context.device, &sig_desc, context.root_signature, &IID_ID3D12CommandSignature, (void**)&complex_sig);
    todo_if(hr == E_NOTIMPL)
    ok(hr == S_OK, "Failed to create command signature, hr %#x.\n", hr);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Width = ARRAY_SIZE(tests) * sizeof(uint32_t);
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void**)&uav);
    ok(hr == S_OK, "Failed to create UAV buffer, hr %#x.\n", hr);

    uav_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    uav_cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Format = DXGI_FORMAT_R32_UINT;
    uav_desc.Buffer.NumElements = ARRAY_SIZE(tests);

    ID3D12Device_CreateUnorderedAccessView(context.device, uav, NULL, &uav_desc,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(uav_heap));
    ID3D12Device_CreateUnorderedAccessView(context.device, uav, NULL, &uav_desc,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(uav_cpu_heap));

    heap_properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
    heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

    resource_desc.Width = sizeof(tests);
    resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL, &IID_ID3D12Resource, (void**)&draw_buffer);
    ok(hr == S_OK, "Failed to create draw buffer, hr %#x.\n", hr);

    hr = ID3D12Resource_Map(draw_buffer, 0, NULL, (void**)&draw_args);
    ok(hr == S_OK, "Failed to map draw buffer, hr %#x.\n", hr);
    memcpy(draw_args, tests, sizeof(tests));

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = 1.0f;
    viewport.Height = 1.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    scissor.left = 0;
    scissor.top = 0;
    scissor.right = 1;
    scissor.bottom = 1;

    for (i = 0; i < MODE_COUNT; i++)
    {
        vkd3d_test_set_context("Mode %u", i);

        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(context.list,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(uav_heap),
                ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(uav_cpu_heap),
                uav, zero_uint, 0, NULL);

        uav_barrier(context.list, uav);

        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 0, NULL, FALSE, NULL);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
        ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(uav));
        ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(draw_buffer));
        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &scissor);

        if (i == MODE_EXECUTE_INDIRECT_COMPLEX)
        {
            if (!complex_sig)
            {
                skip("Complex command signatures not supported by implementation.\n");
                break;
            }

            ID3D12GraphicsCommandList_ExecuteIndirect(context.list,
                    complex_sig, ARRAY_SIZE(tests), draw_buffer, 0, NULL, 0);
        }
        else
        {
            for (j = 0; j < ARRAY_SIZE(tests); j++)
            {
                ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstant(context.list, 2, tests[j].draw_id, 0);

                if (i == MODE_EXECUTE_INDIRECT_SIMPLE)
                {
                    ID3D12GraphicsCommandList_ExecuteIndirect(context.list, simple_sig, 1, draw_buffer,
                            j * sizeof(struct draw_args) + offsetof(struct draw_args, vertex_count), NULL, 0);
                }
                else
                {
                    ID3D12GraphicsCommandList_DrawInstanced(context.list, tests[j].vertex_count,
                            tests[j].instance_count, tests[j].base_vertex, tests[j].base_instance);
                }
            }
        }

        transition_resource_state(context.list, uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_buffer_readback_with_command_list(uav, 0, &rb, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        transition_resource_state(context.list, uav, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        for (j = 0; j < ARRAY_SIZE(tests); j++)
        {
            uint32_t got = get_readback_uint(&rb, j, 0, 0);
            uint32_t expected = (2u << (tests[j].vertex_count * tests[j].instance_count - 1u)) - 1u;

            ok(got == expected, "Got %#x, expected %#x at offset %u.\n", got, expected, j);
        }

        release_resource_readback(&rb);
    }

    ID3D12Resource_Release(uav);
    ID3D12Resource_Release(draw_buffer);

    ID3D12CommandSignature_Release(simple_sig);

    if (complex_sig)
        ID3D12CommandSignature_Release(complex_sig);

    ID3D12DescriptorHeap_Release(uav_heap);
    ID3D12DescriptorHeap_Release(uav_cpu_heap);

    destroy_test_context(&context);
}

void test_sm68_wave_size_range(void)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1;
    D3D12_ROOT_PARAMETER root_parameters[2];
    uint32_t value, expected, wave_size;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12PipelineState *pso;
    uint32_t input_data[128];
    bool supported_wave_size;
    HRESULT hr, expected_hr;
    ID3D12Resource *src;
    ID3D12Resource *dst;
    unsigned int i, j;

    struct test
    {
        const struct D3D12_SHADER_BYTECODE *cs;
        unsigned int wave_size_min;
        unsigned int wave_size_max;
        unsigned int wave_size_preferred;
    };

#include "shaders/sm_advanced/headers/cs_wave_size_range_8_16.h"
#include "shaders/sm_advanced/headers/cs_wave_size_range_16_32.h"
#include "shaders/sm_advanced/headers/cs_wave_size_range_64_128.h"
#include "shaders/sm_advanced/headers/cs_wave_size_range_16.h"
#include "shaders/sm_advanced/headers/cs_wave_size_range_32.h"
#include "shaders/sm_advanced/headers/cs_wave_size_range_64.h"
#include "shaders/sm_advanced/headers/cs_wave_size_range_prefer_16.h"
#include "shaders/sm_advanced/headers/cs_wave_size_range_prefer_32.h"
#include "shaders/sm_advanced/headers/cs_wave_size_range_prefer_64.h"

    static const struct test tests[] =
    {
        { &cs_wave_size_range_16_dxil, 16, 16, 0 },
        { &cs_wave_size_range_32_dxil, 32, 32, 0 },
        { &cs_wave_size_range_64_dxil, 64, 64, 0 },

        { &cs_wave_size_range_8_16_dxil,    8,  16, 0 },
        { &cs_wave_size_range_16_32_dxil,  16,  32, 0 },
        { &cs_wave_size_range_64_128_dxil, 64, 128, 0 },

        { &cs_wave_size_range_prefer_16_dxil, 4, 128, 16 },
        { &cs_wave_size_range_prefer_32_dxil, 4, 128, 32 },
        { &cs_wave_size_range_prefer_64_dxil, 4, 128, 64 },
    };

    if (!init_compute_test_context(&context))
        return;

    if (!context_supports_dxil(&context))
    {
        skip("Context does not support DXIL.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&shader_model, 0, sizeof(shader_model));
    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_8;
    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) ||
            shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_8)
    {
        skip("Device does not support SM 6.8.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(root_parameters);
    rs_desc.pParameters = root_parameters;
    memset(root_parameters, 0, sizeof(root_parameters));
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    create_root_signature(context.device, &rs_desc, &context.root_signature);

    memset(&options1, 0, sizeof(options1));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        supported_wave_size = tests[i].wave_size_max >= options1.WaveLaneCountMin && tests[i].wave_size_min <= options1.WaveLaneCountMax;

        memset(&compute_desc, 0, sizeof(compute_desc));
        compute_desc.CS = *tests[i].cs;
        compute_desc.pRootSignature = context.root_signature;

        expected_hr = supported_wave_size ? S_OK : E_INVALIDARG;
        hr = ID3D12Device_CreateComputePipelineState(context.device, &compute_desc, &IID_ID3D12PipelineState, (void**)&pso);
        ok(hr == expected_hr, "Got hr #%x, expected %#x.\n", hr, expected_hr);

        if (!supported_wave_size)
        {
            skip("WaveSize range [%u, %u] not supported, skipping.\n", tests[i].wave_size_min, tests[i].wave_size_max);
            continue;
        }

        for (j = 0; j < ARRAY_SIZE(input_data); j++)
            input_data[j] = 100;

        src = create_upload_buffer(context.device, sizeof(input_data), input_data);
        dst = create_default_buffer(context.device, sizeof(input_data) * 2,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
        ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(src));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(dst));
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

        transition_resource_state(context.list, dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(dst, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

        for (j = 0; j < ARRAY_SIZE(input_data); j++)
        {
            wave_size = get_readback_uint(&rb, j + 128, 0, 0);

            ok(wave_size >= options1.WaveLaneCountMin && wave_size <= options1.WaveLaneCountMax, "Expected wave size within device supported range [%u,%u], got %u.\n",
                    options1.WaveLaneCountMin, options1.WaveLaneCountMax, wave_size);

            if (tests[i].wave_size_preferred >= options1.WaveLaneCountMin && tests[i].wave_size_preferred <= options1.WaveLaneCountMax)
            {
                ok(wave_size == tests[i].wave_size_preferred, "Expected wave size: %u, got %u\n", tests[i].wave_size_preferred, wave_size);
            }
            else
            {
                ok(wave_size >= tests[i].wave_size_min && wave_size <= tests[i].wave_size_max, "Expected wave size in range [%u,%u], got %u.\n",
                        tests[i].wave_size_min, tests[i].wave_size_max, wave_size);
            }

            expected = (j % wave_size) * 100;
            value = get_readback_uint(&rb, j, 0, 0);
            ok(value == expected, "Prefix sum mismatch, value %u: %u != %u\n", j, value, expected);
        }

        ID3D12Resource_Release(src);
        ID3D12Resource_Release(dst);
        ID3D12PipelineState_Release(pso);
        release_resource_readback(&rb);
        reset_command_list(context.list, context.allocator);
    }

    vkd3d_test_set_context(NULL);
    destroy_test_context(&context);
}

void test_sm68_sample_cmp_bias_grad(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS21 options21;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    ID3D12DescriptorHeap *dsv_heap, *srv_heap;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    D3D12_STATIC_SAMPLER_DESC sampler_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_ROOT_PARAMETER root_params[2];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_DESCRIPTOR_RANGE srv_range;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12Resource *ds_resource;
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissor;
    unsigned int i;
    HRESULT hr;

    struct shader_args
    {
        float grad_x;
        float grad_y;
        float lod_bias;
        float min_lod;
        float dref;
    };

    struct test_results
    {
        float sample_bias;
        float sample_grad;
        float calc_lod;
        float calc_lod_unclamped;
    };

    static const struct
    {
        struct shader_args args;
        struct test_results expected;
    }
    tests[] =
    {
        { { 1.0f/16.0f, 1.0f/16.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f, 0.0f } },
        { { 1.0f/16.0f, 1.0f/16.0f, 0.0f, 0.0f, 0.9f }, { 0.0f, 0.0f, 0.0f, 0.0f } },

        /* Test min lod behaviour */
        { { 1.0f/16.0f, 1.0f/16.0f, 0.0f, 1.0f, 0.8f }, { 1.0f, 1.0f, 0.0f, 0.0f } },
        { { 1.0f/16.0f, 1.0f/16.0f, 0.0f, 1.0f, 0.7f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
        { { 1.0f/16.0f, 1.0f/16.0f, 0.0f, 2.8f, 0.4f }, { 1.0f, 1.0f, 0.0f, 0.0f } },
        { { 1.0f/16.0f, 1.0f/16.0f, 0.0f, 2.8f, 0.3f }, { 0.0f, 0.0f, 0.0f, 0.0f } },

        /* Test gradient and bias behaviour */
        { { 1.0f/8.0f, 1.0f/8.0f, 1.0f, 0.0f, 0.8f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f/8.0f, 1.0f/8.0f, 1.0f, 0.0f, 0.7f }, { 0.0f, 0.0f, 1.0f, 1.0f } },

        { {-1.0f/8.0f, 0.0f, 0.8f, 0.0f, 0.8f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 1.0f/8.0f, 0.0f, 1.2f, 0.0f, 0.7f }, { 0.0f, 0.0f, 1.0f, 1.0f } },

        { { 0.0f, 1.0f/8.0f, 1.0f, 0.0f, 0.8f }, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { { 0.0f, 1.0f/8.0f, 1.0f, 0.0f, 0.7f }, { 0.0f, 0.0f, 1.0f, 1.0f } },

        { { 2.0f,       2.0f,       5.1f, 0.0f, 0.2f }, { 1.0f, 1.0f, 4.0f,  5.1f } },
        { { 1.0f/32.0f, 1.0f/32.0f,-1.2f, 0.0f, 0.9f }, { 0.0f, 0.0f, 0.0f, -1.2f } },
    };

    static const float clear_values[] = { 0.95f, 0.75f, 0.55f, 0.35f, 0.15f };
    static const float clear_color[] = { -1.0f, -1.0f, -1.0f, -1.0f };

#include "shaders/sm_advanced/headers/vs_sample_cmp_grad_bias.h"
#include "shaders/sm_advanced/headers/ps_sample_cmp_grad_bias.h"

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    desc.no_pipeline = true;
    desc.rt_width = 16;
    desc.rt_height = 16;
    desc.rt_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    if (!init_test_context(&context, &desc))
        return;

    memset(&shader_model, 0, sizeof(shader_model));
    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_8;
    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model))) ||
            shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_8)
    {
        skip("Device does not support SM 6.8.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&options21, 0, sizeof(options21));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS21, &options21, sizeof(options21));

    if (!options21.SampleCmpGradientAndBiasSupported)
    {
        skip("SampleCmpGradientAndBias not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&srv_range, 0, sizeof(srv_range));
    srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_range.NumDescriptors = 1;

    memset(&root_params, 0, sizeof(root_params));
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_params[0].Constants.Num32BitValues = sizeof(struct shader_args) / sizeof(float);
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[1].DescriptorTable.NumDescriptorRanges = 1;
    root_params[1].DescriptorTable.pDescriptorRanges = &srv_range;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    sampler_desc.MinLOD = 0.0f;
    sampler_desc.MaxLOD = 16.0f;
    sampler_desc.MipLODBias = 0.0f;
    sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(root_params);
    rs_desc.pParameters = root_params;
    rs_desc.NumStaticSamplers = 1;
    rs_desc.pStaticSamplers = &sampler_desc;

    hr = create_root_signature(context.device, &rs_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    init_pipeline_state_desc_dxil(&pso_desc, context.root_signature,
            DXGI_FORMAT_R32G32B32A32_FLOAT, &vs_sample_cmp_grad_bias_dxil, &ps_sample_cmp_grad_bias_dxil, NULL);
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void**)&context.pipeline_state);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    resource_desc.Width = 16;
    resource_desc.Height = 16;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 5;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
            &resource_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, NULL, &IID_ID3D12Resource, (void**)&ds_resource);
    ok(hr == S_OK, "Failed to create depth image, hr %#x.\n", hr);

    dsv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, resource_desc.MipLevels);
    srv_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    memset(&dsv_desc, 0, sizeof(dsv_desc));
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;

    for (i = 0; i < resource_desc.MipLevels; i++)
    {
        dsv_desc.Texture2D.MipSlice = i;
        ID3D12Device_CreateDepthStencilView(context.device, ds_resource,
                &dsv_desc, get_cpu_dsv_handle(&context, dsv_heap, i));
    }

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.Texture2D.MipLevels = resource_desc.MipLevels;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    ID3D12Device_CreateShaderResourceView(context.device, ds_resource,
            &srv_desc, get_cpu_descriptor_handle(&context, srv_heap, 0));

    for (i = 0; i < ARRAY_SIZE(clear_values); i++)
    {
        ID3D12GraphicsCommandList_ClearDepthStencilView(context.list, get_cpu_dsv_handle(&context, dsv_heap, i),
                D3D12_CLEAR_FLAG_DEPTH, clear_values[i], 0, 0, NULL);
    }

    transition_resource_state(context.list, ds_resource,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = 16.0f;
    viewport.Height = 16.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    scissor.left = 0;
    scissor.top = 0;
    scissor.right = 16;
    scissor.bottom = 16;

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        const struct test_results *rb_data;

        vkd3d_test_set_context("Test %u", i);

        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &srv_heap);
        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, context.rtv, clear_color, 0, NULL);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &scissor);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(context.list, 0,
                sizeof(struct shader_args) / sizeof(float), &tests[i].args, 0);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(context.list, 1,
                get_gpu_descriptor_handle(&context, srv_heap, 0));
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_texture_readback_with_command_list(context.render_target, 0, &rb, context.queue, context.list);

        rb_data = get_readback_data(&rb, 0, 0, 0, sizeof(*rb_data));

        ok(rb_data->sample_bias == tests[i].expected.sample_bias,
                "Got %f, expected %f for bias.\n", rb_data->sample_bias, tests[i].expected.sample_bias);
        ok(rb_data->sample_grad == tests[i].expected.sample_grad,
                "Got %f, expected %f for grad.\n", rb_data->sample_grad, tests[i].expected.sample_grad);
        ok(fabs(rb_data->calc_lod - tests[i].expected.calc_lod) < 0.5f,
                "Got LOD %f, expected %f.\n", rb_data->calc_lod, tests[i].expected.calc_lod);
        ok(fabs(rb_data->calc_lod_unclamped - tests[i].expected.calc_lod_unclamped) < 0.5f,
                "Got unclamped LOD %f, expected %f.\n", rb_data->calc_lod_unclamped, tests[i].expected.calc_lod_unclamped);

        release_resource_readback(&rb);

        reset_command_list(context.list, context.allocator);
        transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    ID3D12DescriptorHeap_Release(dsv_heap);
    ID3D12DescriptorHeap_Release(srv_heap);
    ID3D12Resource_Release(ds_resource);

    destroy_test_context(&context);
}
