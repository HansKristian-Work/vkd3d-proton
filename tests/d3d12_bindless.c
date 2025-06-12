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

void test_bindless_heap_sm66_uav_counter(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_ROOT_PARAMETER root_parameters[1];
    ID3D12DescriptorHeap *heap;
    struct test_context context;
    ID3D12Resource *resource;
    struct resource_readback rb;
    static const uint32_t counts[] = { 129, 234, 22, 31 };
    unsigned int i;
    HRESULT hr;

#include "shaders/bindless/headers/bindless_heap_sm66_uav_counter.h"

    if (!init_compute_test_context(&context))
        return;

    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    hr = ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL,
            &shader_model, sizeof(shader_model));
    if (FAILED(hr) || shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_6)
    {
        skip("Shader model 6.6 not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    memset(root_parameters, 0, sizeof(root_parameters));
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[0].Constants.ShaderRegister = 0;
    root_parameters[0].Constants.RegisterSpace = 0;
    root_parameters[0].Constants.Num32BitValues = 1;
    create_root_signature(context.device, &root_signature_desc, &context.root_signature);

    resource = create_default_buffer(context.device, D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature, bindless_heap_sm66_uav_counter_dxil);
    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4);

    memset(&uav_desc, 0, sizeof(uav_desc));
    for (i = 0; i < 4; i++)
    {
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.Buffer.CounterOffsetInBytes = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT * i;
        uav_desc.Buffer.FirstElement = 1 + i;
        uav_desc.Buffer.NumElements = 1;
        uav_desc.Buffer.StructureByteStride = 4;
        ID3D12Device_CreateUnorderedAccessView(context.device, resource, resource, &uav_desc, get_cpu_descriptor_handle(&context, heap, i));
    }

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    for (i = 0; i < 4; i++)
    {
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 0, i, 0);
        ID3D12GraphicsCommandList_Dispatch(context.list, counts[i], 1, 1);
    }

    transition_resource_state(context.list, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(resource, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < 4; i++)
    {
        uint32_t uav_value, counter_value;
        uav_value = get_readback_uint(&rb, i + 1, 0, 0);
        counter_value = get_readback_uint(&rb, i * D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT / sizeof(uint32_t), 0, 0);
        ok(uav_value == counts[i] * 3, "UAV value %u, expected %u, got %u.\n", i, counts[i] * 3, uav_value);
        ok(counter_value == counts[i], "Counter value %u, expected %u, got %u.\n", i, counts[i], counter_value);
    }

    release_resource_readback(&rb);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12Resource_Release(resource);
    destroy_test_context(&context);
}

void test_bindless_heap_sm66(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_FEATURE_DATA_SHADER_MODEL shader_model;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc;
    D3D12_ROOT_PARAMETER root_parameters[1];
    ID3D12DescriptorHeap *cpu_resource_heap;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    unsigned int descriptor_size_sampler;
    ID3D12RootSignature *root_signature;
    ID3D12DescriptorHeap *resource_heap;
    ID3D12Resource *input_textures[256];
    ID3D12DescriptorHeap *sampler_heap;
    unsigned int i, descriptor_size;
    D3D12_SAMPLER_DESC sampler_desc;
    ID3D12DescriptorHeap *heaps[2];
    ID3D12Resource *output_buffer;
    ID3D12Resource *input_buffer;
    struct resource_readback rb;
    struct test_context context;
    ID3D12PipelineState *pso;
    ID3D12Device *device;
    FLOAT clear_values[4];
    unsigned int x, y;
    D3D12_RECT rect;
    HRESULT hr;

#include "shaders/bindless/headers/bindless_heap_sm66.h"

    uint32_t initial_buffer_data[ARRAY_SIZE(input_textures)];

    if (!init_compute_test_context(&context))
        return;

    device = context.device;
    command_list = context.list;
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    descriptor_size_sampler = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    shader_model.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    hr = ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_SHADER_MODEL,
            &shader_model, sizeof(shader_model));
    if (FAILED(hr) || shader_model.HighestShaderModel < D3D_SHADER_MODEL_6_6)
    {
        skip("Shader model 6.6 not supported by device.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
            D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    memset(root_parameters, 0, sizeof(root_parameters));
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[0].Constants.ShaderRegister = 0;
    root_parameters[0].Constants.RegisterSpace = 0;
    root_parameters[0].Constants.Num32BitValues = 4;

    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);

    pso = create_compute_pipeline_state(device, root_signature, bindless_heap_sm66_dxil);
    cpu_resource_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            ARRAY_SIZE(input_textures) * root_parameters[0].Constants.Num32BitValues);
    resource_heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            ARRAY_SIZE(input_textures) * root_parameters[0].Constants.Num32BitValues);
    sampler_heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, ARRAY_SIZE(input_textures));

    heaps[0] = resource_heap;
    heaps[1] = sampler_heap;
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, ARRAY_SIZE(heaps), heaps);

    for (i = 0; i < ARRAY_SIZE(input_textures); i++)
    {
        input_textures[i] = create_default_texture2d(device, 2, 2, 1, 1,
                DXGI_FORMAT_R32_FLOAT,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        memset(&uav_desc, 0, sizeof(uav_desc));
        uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(resource_heap);
        cpu_handle.ptr += i * descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(device, input_textures[i], NULL, &uav_desc, cpu_handle);

        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_resource_heap);
        cpu_handle.ptr += i * descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(device, input_textures[i], NULL, &uav_desc, cpu_handle);

        gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(resource_heap);
        gpu_handle.ptr += i * descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(device, input_textures[i], NULL, &uav_desc, cpu_handle);

        memset(clear_values, 0, sizeof(clear_values));

        for (y = 0; y < 2; y++)
        {
            for (x = 0; x < 2; x++)
            {
                clear_values[0] = i + 1 + 1000 * (y * 2 + x);
                set_rect(&rect, x, y, x + 1, y + 1);
                ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(command_list, gpu_handle, cpu_handle,
                        input_textures[i], clear_values, 1, &rect);
            }
        }

        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2D.MipLevels = 1;

        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(resource_heap);
        cpu_handle.ptr += (i + 1 * ARRAY_SIZE(input_textures)) * descriptor_size;
        ID3D12Device_CreateShaderResourceView(device, input_textures[i], &srv_desc, cpu_handle);

        if (i % 2 == 0)
        {
            transition_resource_state(command_list, input_textures[i],
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            memset(&sampler_desc, 0, sizeof(sampler_desc));
            sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            sampler_desc.AddressU = i % 4 == 0 ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler_desc.AddressW = sampler_desc.AddressU;

            cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sampler_heap);
            cpu_handle.ptr += i * descriptor_size_sampler;
            ID3D12Device_CreateSampler(device, &sampler_desc, cpu_handle);
        }
        else
            uav_barrier(command_list, input_textures[i]);
    }

    for (i = 0; i < ARRAY_SIZE(input_textures); i++)
        initial_buffer_data[i] = i + 10000;
    input_buffer = create_upload_buffer(device, sizeof(initial_buffer_data), initial_buffer_data);

    output_buffer = create_default_buffer(device, ARRAY_SIZE(input_textures) * sizeof(uint32_t),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    for (i = 0; i < ARRAY_SIZE(input_textures); i++)
    {
        memset(&srv_desc, 0, sizeof(srv_desc));

        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(resource_heap);
        cpu_handle.ptr += (i + 2 * ARRAY_SIZE(input_textures)) * descriptor_size;

        if (i % 16 == 0)
        {
            memset(&cbv_desc, 0, sizeof(cbv_desc));
            cbv_desc.SizeInBytes = 256;
            cbv_desc.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(input_buffer) + (i % 32 == 0 ? 256 : 0);
            ID3D12Device_CreateConstantBufferView(device, &cbv_desc, cpu_handle);
        }
        else
        {
            if (i % 4 == 0)
            {
                srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
                srv_desc.Buffer.NumElements = 4;
                srv_desc.Buffer.FirstElement = i;
                srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            }
            else if (i % 4 == 1)
            {
                srv_desc.Format = DXGI_FORMAT_R32_UINT;
                srv_desc.Buffer.NumElements = 1;
                srv_desc.Buffer.FirstElement = i;
            }
            else
            {
                srv_desc.Format = DXGI_FORMAT_UNKNOWN;
                srv_desc.Buffer.StructureByteStride = 4;
                srv_desc.Buffer.FirstElement = i;
                srv_desc.Buffer.NumElements = 1;
            }

            ID3D12Device_CreateShaderResourceView(device, input_buffer, &srv_desc, cpu_handle);
        }
    }

    for (i = 0; i < ARRAY_SIZE(input_textures); i++)
    {
        memset(&uav_desc, 0, sizeof(uav_desc));

        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        if (i % 4 == 0)
        {
            uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
            uav_desc.Buffer.NumElements = 4;
            uav_desc.Buffer.FirstElement = i;
            uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        }
        else if (i % 4 == 1)
        {
            uav_desc.Format = DXGI_FORMAT_R32_UINT;
            uav_desc.Buffer.NumElements = 1;
            uav_desc.Buffer.FirstElement = i;
        }
        else
        {
            uav_desc.Format = DXGI_FORMAT_UNKNOWN;
            uav_desc.Buffer.StructureByteStride = 4;
            uav_desc.Buffer.FirstElement = i;
            uav_desc.Buffer.NumElements = 1;
        }

        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(resource_heap);
        cpu_handle.ptr += (i + 3 * ARRAY_SIZE(input_textures)) * descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(device, output_buffer, NULL, &uav_desc, cpu_handle);
    }

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, pso);
    for (i = 0; i < root_parameters[0].Constants.Num32BitValues; i++)
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(command_list, 0, i * ARRAY_SIZE(input_textures), i);
    ID3D12GraphicsCommandList_Dispatch(command_list, ARRAY_SIZE(input_textures) / 64, 1, 1);

    transition_resource_state(command_list, output_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, command_list);

    for (i = 0; i < ARRAY_SIZE(input_textures); i++)
    {
        UINT value, expected;
        value = get_readback_uint(&rb, i, 0, 0);
        expected = 0;
        if (i % 2 == 0)
        {
            expected += i + 1; /* SRV texture reads. */
            if (i % 4 == 0)
                expected += 3000; /* CLAMP sampler used, we'll sample pixel (1, 1). */
            else
                expected += 2000; /* WRAP, CLAMP used, sample pixel (0, 1). */
        }
        else
        {
            expected += i + 1; /* UAV texture reads. */
        }

        if (i % 16 == 0)
        {
            /* CBV reads. */
            if (i % 32 == 0)
                expected += 64 + 10000;
            else
                expected += 10000;
        }
        else
            expected += i + 10000; /* Buffer reads. */
        ok(expected == value, "Value %u mismatch, expected %u, got %u.\n", i, expected, value);
    }

    release_resource_readback(&rb);

    for (i = 0; i < ARRAY_SIZE(input_textures); i++)
        ID3D12Resource_Release(input_textures[i]);
    ID3D12Resource_Release(input_buffer);
    ID3D12Resource_Release(output_buffer);
    ID3D12DescriptorHeap_Release(cpu_resource_heap);
    ID3D12DescriptorHeap_Release(resource_heap);
    ID3D12DescriptorHeap_Release(sampler_heap);
    ID3D12RootSignature_Release(root_signature);
    ID3D12PipelineState_Release(pso);
    destroy_test_context(&context);
}

static void test_bindless_srv(bool use_dxil)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_ROOT_PARAMETER root_parameters[3];
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[4];
    ID3D12DescriptorHeap* heap;

    ID3D12Resource *input_buffers[256];
    ID3D12Resource *input_textures[256];
    ID3D12Resource *output_buffer;
    struct resource_readback rb;

    ID3D12GraphicsCommandList* command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    unsigned int i, descriptor_size;
    struct test_context context;
    ID3D12CommandQueue* queue;
    HRESULT hr;

#include "shaders/bindless/headers/bindless_srv.h"

    if (!init_compute_test_context(&context))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    command_list = context.list;
    queue = context.queue;

    root_signature_desc.NumParameters = 3;
    root_signature_desc.Flags = 0;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_ranges[0];

    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].DescriptorTable.NumDescriptorRanges = 2;
    root_parameters[1].DescriptorTable.pDescriptorRanges = &descriptor_ranges[2];

    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[2].Descriptor.RegisterSpace = 0;
    root_parameters[2].Descriptor.ShaderRegister = 0;

    /* Need two idential ranges so we can alias two different resource dimensions over same table. */
    for (i = 0; i < 4; i++)
    {
        descriptor_ranges[i].RegisterSpace = i + 1;
        descriptor_ranges[i].BaseShaderRegister = 4;
        descriptor_ranges[i].OffsetInDescriptorsFromTableStart = i >= 2 ? 1 : 0;
        descriptor_ranges[i].NumDescriptors = 64 * 1024;
        descriptor_ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    }

    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    for (i = 0; i < 256; i++)
    {
        const UINT buffer_data[] = { i * 2, i * 2, i * 2, i * 2 };
        input_buffers[i] = create_default_buffer(context.device, sizeof(buffer_data), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        upload_buffer_data(input_buffers[i], 0, sizeof(buffer_data), buffer_data, queue, command_list);
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, input_buffers[i], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    for (i = 0; i < 256; i++)
    {
        const UINT tex_data = i * 2 + 1;
        D3D12_SUBRESOURCE_DATA sub;
        sub.pData = &tex_data;
        sub.RowPitch = 1;
        sub.SlicePitch = 1;
        input_textures[i] = create_default_texture2d(context.device, 1, 1, 1, 1, DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        upload_texture_data(input_textures[i], &sub, 1, queue, command_list);
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, input_textures[i], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    output_buffer = create_default_buffer(context.device, 4 * 64, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    context.pipeline_state = create_compute_pipeline_state(context.device,
        context.root_signature,
        shader_bytecode(use_dxil ? (const void*)bindless_srv_code_dxil : (const void*)bindless_srv_code_dxbc, use_dxil ? sizeof(bindless_srv_code_dxil) : sizeof(bindless_srv_code_dxbc)));

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128 * 1024);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (i = 0; i < 512; i++)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpu_handle;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        h.ptr += (1024 + i) * descriptor_size;

        /* Every other resource is a buffer and texture SRV which are aliased over the same descriptor table range. */
        if (i & 1)
        {
            view.Format = DXGI_FORMAT_R32_UINT;
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = 1;
            view.Texture2D.MostDetailedMip = 0;
            view.Texture2D.PlaneSlice = 0;
            view.Texture2D.ResourceMinLODClamp = 0;
            ID3D12Device_CreateShaderResourceView(context.device, input_textures[i >> 1], &view, h);
        }
        else
        {
            view.Format = DXGI_FORMAT_UNKNOWN;
            view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            view.Buffer.FirstElement = 0;
            view.Buffer.NumElements = 4;
            view.Buffer.StructureByteStride = 4;
            view.Buffer.Flags = 0;
            ID3D12Device_CreateShaderResourceView(context.device, input_buffers[i >> 1], &view, h);
        }
    }

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    {
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = gpu_handle;
        gpu.ptr += 1024 * descriptor_size;
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0, gpu);
        gpu.ptr += descriptor_size;
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 1, gpu);
    }
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list, 2, ID3D12Resource_GetGPUVirtualAddress(output_buffer));
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);

    transition_resource_state(command_list, output_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    for (i = 0; i < 64; i++)
    {
        UINT value = get_readback_uint(&rb, i, 0, 0);
        UINT reference = (i + (i + 2) * 256) * 98 * 197;
        ok(value == reference, "Readback value is: %u\n", value);
    }

    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    for (i = 0; i < 256; i++)
    {
        ID3D12Resource_Release(input_buffers[i]);
        ID3D12Resource_Release(input_textures[i]);
    }
    ID3D12Resource_Release(output_buffer);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_bindless_srv_sm51(void)
{
    test_bindless_srv(false);
}

void test_bindless_srv_dxil(void)
{
    test_bindless_srv(true);
}

static void test_bindless_samplers(bool use_dxil)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_ROOT_PARAMETER root_parameters[3];
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    ID3D12DescriptorHeap *heaps[2];

    ID3D12Resource* input_texture;
    ID3D12Resource* output_buffer;
    struct resource_readback rb;

    ID3D12GraphicsCommandList* command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    unsigned int i, descriptor_size;
    struct test_context context;
    ID3D12CommandQueue* queue;
    HRESULT hr;

#include "shaders/bindless/headers/bindless_samplers.h"

    if (!init_compute_test_context(&context))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    command_list = context.list;
    queue = context.queue;

    root_signature_desc.NumParameters = 3;
    root_signature_desc.Flags = 0;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_ranges[0];

    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[1].DescriptorTable.pDescriptorRanges = &descriptor_ranges[1];

    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[2].Descriptor.RegisterSpace = 0;
    root_parameters[2].Descriptor.ShaderRegister = 0;

    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[0].NumDescriptors = 1;
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

    descriptor_ranges[1].RegisterSpace = 0;
    descriptor_ranges[1].BaseShaderRegister = 0;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[1].NumDescriptors = 1024;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;

    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    {
        const float tex_data[] = { 10, 100, 100, 100 };
        D3D12_SUBRESOURCE_DATA sub;
        sub.pData = tex_data;
        sub.RowPitch = 8;
        sub.SlicePitch = 8;
        input_texture = create_default_texture2d(context.device, 2, 2, 1, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        upload_texture_data(input_texture, &sub, 1, queue, command_list);
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, input_texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    output_buffer = create_default_buffer(context.device, 4 * 64, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    context.pipeline_state = create_compute_pipeline_state(context.device,
        context.root_signature,
        shader_bytecode(use_dxil ? (const void*)bindless_samplers_code_dxil : (const void*)bindless_samplers_code_dxbc, use_dxil ? sizeof(bindless_samplers_code_dxil) : sizeof(bindless_samplers_code_dxbc)));

    heaps[0] = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    heaps[1] = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1024);

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC view;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Format = DXGI_FORMAT_R32_FLOAT;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        view.Texture2D.MostDetailedMip = 0;
        view.Texture2D.PlaneSlice = 0;
        view.Texture2D.ResourceMinLODClamp = 0;
        ID3D12Device_CreateShaderResourceView(context.device, input_texture, &view,
                ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heaps[0]));
    }

    for (i = 0; i < 1024; i++)
    {
        D3D12_SAMPLER_DESC samp;
        memset(&samp, 0, sizeof(samp));
        samp.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samp.AddressU = samp.AddressV = samp.AddressW = (i & 1) ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heaps[1]);
        descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        cpu_handle.ptr += descriptor_size * i;
        ID3D12Device_CreateSampler(context.device, &samp, cpu_handle);
    }

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 2, heaps);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heaps[0]));
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 1, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heaps[1]));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list, 2, ID3D12Resource_GetGPUVirtualAddress(output_buffer));
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);

    transition_resource_state(command_list, output_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    for (i = 0; i < 64; i++)
    {
        UINT value = get_readback_uint(&rb, i, 0, 0);
        UINT reference = (i & 1) ? 100 : 10;
        ok(value == reference, "Readback value for index %u is: %u\n", i, value);
    }

    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    ID3D12Resource_Release(input_texture);
    ID3D12Resource_Release(output_buffer);
    ID3D12DescriptorHeap_Release(heaps[0]);
    ID3D12DescriptorHeap_Release(heaps[1]);
    destroy_test_context(&context);
}

void test_bindless_samplers_sm51(void)
{
    test_bindless_samplers(false);
}

void test_bindless_samplers_dxil(void)
{
    test_bindless_samplers(true);
}

void test_bindless_full_root_parameters_sm51(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_ROOT_PARAMETER root_parameters[63];
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[63];
    ID3D12DescriptorHeap* heap;

    ID3D12Resource* input_buffers[1024];
    ID3D12Resource* output_buffer;
    struct resource_readback rb;

    ID3D12GraphicsCommandList* command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    unsigned int i, descriptor_size;
    struct test_context context;
    ID3D12CommandQueue* queue;
    HRESULT hr;

#include "shaders/bindless/headers/bindless_full_root_parameters.h"

    if (!init_compute_test_context(&context))
        return;
    command_list = context.list;
    queue = context.queue;

    root_signature_desc.NumParameters = 63;
    root_signature_desc.Flags = 0;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.pParameters = root_parameters;

    for (i = 0; i < 62; i++)
    {
        root_parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_parameters[i].DescriptorTable.NumDescriptorRanges = 1;
        root_parameters[i].DescriptorTable.pDescriptorRanges = &descriptor_ranges[i];

        descriptor_ranges[i].RegisterSpace = i;
        descriptor_ranges[i].BaseShaderRegister = 0;
        descriptor_ranges[i].OffsetInDescriptorsFromTableStart = 0;
        descriptor_ranges[i].NumDescriptors = 64 * 1024;
        descriptor_ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    }

    root_parameters[62].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[62].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[62].Descriptor.RegisterSpace = 62;
    root_parameters[62].Descriptor.ShaderRegister = 0;

    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    for (i = 0; i < 1024; i++)
    {
        const UINT buffer_data[] = { i, i, i, i };
        input_buffers[i] = create_default_buffer(context.device, sizeof(buffer_data), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        upload_buffer_data(input_buffers[i], 0, sizeof(buffer_data), buffer_data, queue, command_list);
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, input_buffers[i], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    output_buffer = create_default_buffer(context.device, 4 * 64, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    context.pipeline_state = create_compute_pipeline_state(context.device,
        context.root_signature, bindless_full_root_parameters_dxbc);

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128 * 1024);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (i = 0; i < 1024; i++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpu_handle;
        D3D12_SHADER_RESOURCE_VIEW_DESC view;

        h.ptr += i * descriptor_size;

        view.Format = DXGI_FORMAT_UNKNOWN;
        view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Buffer.FirstElement = 0;
        view.Buffer.NumElements = 4;
        view.Buffer.StructureByteStride = 4;
        view.Buffer.Flags = 0;
        ID3D12Device_CreateShaderResourceView(context.device, input_buffers[i], &view, h);
    }

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);

    for (i = 0; i < 62; i++)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = gpu_handle;
        gpu.ptr += i * descriptor_size;
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, i, gpu);
    }
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list, 62, ID3D12Resource_GetGPUVirtualAddress(output_buffer));
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);

    transition_resource_state(command_list, output_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    for (i = 0; i < 64; i++)
    {
        UINT value = get_readback_uint(&rb, i, 0, 0);
        UINT reference = 62 * (i + (i + 61)) / 2;
        ok(value == reference, "Readback value is: %u\n", value);
    }

    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    for (i = 0; i < 1024; i++)
        ID3D12Resource_Release(input_buffers[i]);
    ID3D12Resource_Release(output_buffer);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

static void test_bindless_cbv(bool use_dxil)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_ROOT_PARAMETER root_parameters[2];
    D3D12_DESCRIPTOR_RANGE descriptor_ranges;
    ID3D12DescriptorHeap* heap;

    ID3D12Resource* input_buffers[512];
    ID3D12Resource* output_buffer;
    struct resource_readback rb;

    ID3D12GraphicsCommandList* command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    unsigned int i, descriptor_size;
    struct test_context context;
    ID3D12CommandQueue* queue;
    HRESULT hr;

#include "shaders/bindless/headers/bindless_cbv.h"

    if (!init_compute_test_context(&context))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    command_list = context.list;
    queue = context.queue;

    root_signature_desc.NumParameters = 2;
    root_signature_desc.Flags = 0;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_ranges;

    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].Descriptor.RegisterSpace = 0;
    root_parameters[1].Descriptor.ShaderRegister = 0;

    descriptor_ranges.RegisterSpace = 1;
    descriptor_ranges.BaseShaderRegister = 2;
    descriptor_ranges.OffsetInDescriptorsFromTableStart = 1;
    descriptor_ranges.NumDescriptors = UINT_MAX;
    descriptor_ranges.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;

    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    for (i = 0; i < 512; i++)
    {
        const UINT buffer_data[] = { i, i, i, i };
        input_buffers[i] = create_default_buffer(context.device, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        upload_buffer_data(input_buffers[i], 0, sizeof(buffer_data), buffer_data, queue, command_list);
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, input_buffers[i], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    output_buffer = create_default_buffer(context.device, 4 * 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    context.pipeline_state = create_compute_pipeline_state(context.device,
        context.root_signature,
        shader_bytecode(use_dxil ? (const void*)bindless_cbv_code_dxil : (const void*)bindless_cbv_code_dxbc, use_dxil ? sizeof(bindless_cbv_code_dxil) : sizeof(bindless_cbv_code_dxbc)));

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 800000);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (i = 0; i < 512; i++)
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpu_handle;
        view.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(input_buffers[i]);
        view.SizeInBytes = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        h.ptr += i * descriptor_size;
        ID3D12Device_CreateConstantBufferView(context.device, &view, h);
    }

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0, gpu_handle);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list, 1, ID3D12Resource_GetGPUVirtualAddress(output_buffer));
    ID3D12GraphicsCommandList_Dispatch(command_list, 4, 1, 1);

    transition_resource_state(command_list, output_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffer, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

    for (i = 0; i < 256; i++)
    {
        UINT value = get_readback_uint(&rb, i, 0, 0);
        UINT reference = i + 1;

        if (use_dxil && (i & 63) != 0)
        {
            /* DXC is bugged and does not emit NonUniformResourceIndex correctly for CBVs,
               so only check the first lane for correctness. */
            continue;
        }

        ok(value == reference, "Readback value for iteration %u is: %u\n", i, value);
    }

    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    for (i = 0; i < 512; i++)
        ID3D12Resource_Release(input_buffers[i]);
    ID3D12Resource_Release(output_buffer);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

static void test_bindless_uav(bool use_dxil)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_ROOT_PARAMETER root_parameters[1];
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    ID3D12DescriptorHeap *heap;

    ID3D12Resource *output_buffers[256];
    ID3D12Resource *output_textures[256];
    struct resource_readback rb;

    ID3D12GraphicsCommandList* command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    unsigned int i, descriptor_size;
    struct test_context context;
    ID3D12CommandQueue* queue;
    HRESULT hr;

#include "shaders/bindless/headers/bindless_uav.h"

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
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;

    descriptor_ranges[0].RegisterSpace = 1;
    descriptor_ranges[0].BaseShaderRegister = 2;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 1;
    descriptor_ranges[0].NumDescriptors = UINT_MAX;
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    descriptor_ranges[1].RegisterSpace = 2;
    descriptor_ranges[1].BaseShaderRegister = 2;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 256 + 1;
    descriptor_ranges[1].NumDescriptors = UINT_MAX;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    for (i = 0; i < 256; i++)
        output_buffers[i] = create_default_buffer(context.device, 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (i = 0; i < 256; i++)
        output_textures[i] = create_default_texture2d(context.device, 1, 1, 1, 1, DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    context.pipeline_state = create_compute_pipeline_state(context.device,
        context.root_signature,
        shader_bytecode(use_dxil ? (const void*)bindless_uav_code_dxil : (const void*)bindless_uav_code_dxbc, use_dxil ? sizeof(bindless_uav_code_dxil) : sizeof(bindless_uav_code_dxbc)));

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 800000);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (i = 0; i < 256; i++)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpu_handle;
        view.Format = DXGI_FORMAT_UNKNOWN;
        view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view.Buffer.FirstElement = 0;
        view.Buffer.NumElements = 64;
        view.Buffer.StructureByteStride = 4;
        view.Buffer.CounterOffsetInBytes = 0;
        view.Buffer.Flags = 0;
        h.ptr += (i + 1) * descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(context.device, output_buffers[i], NULL, &view, h);
    }

    for (i = 0; i < 256; i++)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpu_handle;
        view.Format = DXGI_FORMAT_R32_UINT;
        view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipSlice = 0;
        view.Texture2D.PlaneSlice = 0;
        h.ptr += (256 + i + 1) * descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(context.device, output_textures[i], NULL, &view, h);
    }

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0, gpu_handle);
    ID3D12GraphicsCommandList_Dispatch(command_list, 4, 1, 1);

    for (i = 0; i < 256; i++)
        transition_resource_state(command_list, output_buffers[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    for (i = 0; i < 256; i++)
        transition_resource_state(command_list, output_textures[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    for (i = 0; i < 256; i++)
    {
        UINT value;
        UINT reference = i + 1;

        get_buffer_readback_with_command_list(output_buffers[i], DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);
        value = get_readback_uint(&rb, 0, 0, 0);

        ok(value == reference, "Readback value for buffer iteration %u is: %u\n", i, value);

        release_resource_readback(&rb);
        reset_command_list(command_list, context.allocator);
    }

    for (i = 0; i < 256; i++)
    {
        UINT value;
        UINT reference = i + 1 + 256;
        get_texture_readback_with_command_list(output_textures[i], 0, &rb, queue, command_list);

        value = get_readback_uint(&rb, 0, 0, 0);
        ok(value == reference, "Readback value for texture iteration %u is: %u\n", i, value);

        release_resource_readback(&rb);
        reset_command_list(command_list, context.allocator);
    }

    for (i = 0; i < 256; i++)
        ID3D12Resource_Release(output_buffers[i]);
    for (i = 0; i < 256; i++)
        ID3D12Resource_Release(output_textures[i]);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_bindless_cbv_sm51(void)
{
    test_bindless_cbv(false);
}

void test_bindless_cbv_dxil(void)
{
    test_bindless_cbv(true);
}

static void test_bindless_uav_counter(bool use_dxil)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_ROOT_PARAMETER root_parameters[1];
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[1];
    ID3D12DescriptorHeap *heap, *cpu_heap;

    ID3D12Resource *output_buffers[256];
    struct resource_readback rb;

    ID3D12GraphicsCommandList* command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    unsigned int i, descriptor_size;
    struct test_context context;
    ID3D12CommandQueue* queue;
    HRESULT hr;

#include "shaders/bindless/headers/bindless_uav_counter.h"

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

    descriptor_ranges[0].RegisterSpace = 1;
    descriptor_ranges[0].BaseShaderRegister = 2;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 1;
    descriptor_ranges[0].NumDescriptors = UINT_MAX;
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    for (i = 0; i < 256; i++)
        output_buffers[i] = create_default_buffer(context.device, 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    context.pipeline_state = create_compute_pipeline_state(context.device,
        context.root_signature,
        shader_bytecode(use_dxil ? (const void*)bindless_uav_counter_code_dxil : (const void*)bindless_uav_counter_code_dxbc, use_dxil ? sizeof(bindless_uav_counter_code_dxil) : sizeof(bindless_uav_counter_code_dxbc)));

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 800000);
    cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 800000);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (i = 0; i < 256; i++)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpu_handle;

        view.Format = DXGI_FORMAT_UNKNOWN;
        view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view.Buffer.FirstElement = 1;
        view.Buffer.NumElements = 63;
        view.Buffer.StructureByteStride = 4;
        view.Buffer.CounterOffsetInBytes = 0;
        view.Buffer.Flags = 0;
        h.ptr += (i + 1) * descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(context.device, output_buffers[i], output_buffers[i ^ 1], &view, h);
    }

    /* Cannot UAV clear structured buffers, so use a separate raw byte address buffer for that. */
    for (i = 0; i < 256; i++)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_h = cpu_handle;
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_h = gpu_handle;
        static const UINT init_data[4] = { 10, 10, 10, 10 };

        cpu_h.ptr += (512 + i) * descriptor_size;
        gpu_h.ptr += (512 + i) * descriptor_size;

        view.Format = DXGI_FORMAT_R32_TYPELESS;
        view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view.Buffer.FirstElement = 0;
        view.Buffer.NumElements = 64;
        view.Buffer.StructureByteStride = 0;
        view.Buffer.CounterOffsetInBytes = 0;
        view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

        ID3D12Device_CreateUnorderedAccessView(context.device, output_buffers[i], NULL, &view, cpu_h);
        cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap);
        cpu_h.ptr += (512 + i) * descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(context.device, output_buffers[i], NULL, &view, cpu_h);
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(command_list, gpu_h, cpu_h, output_buffers[i], init_data, 0, NULL);
    }

    uav_barrier(context.list, NULL);

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0, gpu_handle);
    ID3D12GraphicsCommandList_Dispatch(command_list, 4, 1, 1);

    for (i = 0; i < 256; i++)
        transition_resource_state(command_list, output_buffers[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    for (i = 0; i < 256; i++)
    {
        UINT value;
        UINT reference = i + 1;
        get_buffer_readback_with_command_list(output_buffers[i], DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

        value = get_readback_uint(&rb, 1, 0, 0);
        ok(value == reference, "Readback value for buffer iteration %u is: %u\n", i, value);

        value = get_readback_uint(&rb, 0, 0, 0);
        reference = ((i ^ 1) & 3) == 0 ? 14 : 10;
        ok(value == reference, "Readback value for buffer counter iteration %u is: %u\n", i, value);

        release_resource_readback(&rb);
        reset_command_list(command_list, context.allocator);
    }

    for (i = 0; i < 256; i++)
        ID3D12Resource_Release(output_buffers[i]);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12DescriptorHeap_Release(cpu_heap);
    destroy_test_context(&context);
}

void test_bindless_uav_sm51(void)
{
    test_bindless_uav(false);
}

void test_bindless_uav_dxil(void)
{
    test_bindless_uav(true);
}

void test_bindless_uav_counter_sm51(void)
{
    test_bindless_uav_counter(false);
}

void test_bindless_uav_counter_dxil(void)
{
    test_bindless_uav_counter(true);
}

static void test_bindless_bufinfo(bool use_dxil)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_ROOT_PARAMETER root_parameters[1];
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    ID3D12DescriptorHeap *heap;

    ID3D12Resource *output_buffers[256];
    ID3D12Resource *output_textures[256];
    struct resource_readback rb;

    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    unsigned int i, descriptor_size;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

#include "shaders/bindless/headers/bindless_bufinfo.h"

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
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;

    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[0].NumDescriptors = UINT_MAX;
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    descriptor_ranges[1].RegisterSpace = 1;
    descriptor_ranges[1].BaseShaderRegister = 0;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 256;
    descriptor_ranges[1].NumDescriptors = UINT_MAX;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    for (i = 0; i < 256; i++)
        output_buffers[i] = create_default_buffer(context.device, 4096, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (i = 0; i < 256; i++)
        output_textures[i] = create_default_texture2d(context.device, i + 1, 1, 1, 1, DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    context.pipeline_state = create_compute_pipeline_state(context.device,
        context.root_signature,
        shader_bytecode(use_dxil ? (const void *)bindless_bufinfo_code_dxil : (const void *)bindless_bufinfo_code_dxbc, use_dxil ? sizeof(bindless_bufinfo_code_dxil) : sizeof(bindless_bufinfo_code_dxbc)));

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 800000);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (i = 0; i < 256; i++)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpu_handle;
        view.Format = DXGI_FORMAT_UNKNOWN;
        view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        view.Buffer.FirstElement = i & 3; /* Unaligned FirstElement might affect bufinfo computation if we have to emulate alignment. */
        view.Buffer.NumElements = 1 + i;
        view.Buffer.StructureByteStride = 4;
        view.Buffer.CounterOffsetInBytes = 0;
        view.Buffer.Flags = 0;
        h.ptr += i * descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(context.device, output_buffers[i], NULL, &view, h);
    }

    for (i = 0; i < 256; i++)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC view;
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpu_handle;
        view.Format = DXGI_FORMAT_R32_UINT;
        view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipSlice = 0;
        view.Texture2D.PlaneSlice = 0;
        h.ptr += (256 + i) * descriptor_size;
        ID3D12Device_CreateUnorderedAccessView(context.device, output_textures[i], NULL, &view, h);
    }

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0, gpu_handle);
    ID3D12GraphicsCommandList_Dispatch(command_list, 4, 1, 1);

    for (i = 0; i < 256; i++)
        transition_resource_state(command_list, output_buffers[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    for (i = 0; i < 256; i++)
        transition_resource_state(command_list, output_textures[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    for (i = 0; i < 256; i++)
    {
        UINT value;
        UINT reference = i + 1;
        get_buffer_readback_with_command_list(output_buffers[i], DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);

        value = get_readback_uint(&rb, i & 3, 0, 0);
        ok(value == reference, "Readback value for buffer iteration %u is: %u\n", i, value);

        release_resource_readback(&rb);
        reset_command_list(command_list, context.allocator);
    }

    for (i = 0; i < 256; i++)
    {
        UINT value;
        UINT reference = i + 1;
        get_texture_readback_with_command_list(output_textures[i], 0, &rb, queue, command_list);

        value = get_readback_uint(&rb, 0, 0, 0);
        ok(value == reference, "Readback value for texture iteration %u is: %u\n", i, value);

        release_resource_readback(&rb);
        reset_command_list(command_list, context.allocator);
    }

    for (i = 0; i < 256; i++)
        ID3D12Resource_Release(output_buffers[i]);
    for (i = 0; i < 256; i++)
        ID3D12Resource_Release(output_textures[i]);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_bindless_bufinfo_sm51(void)
{
    test_bindless_bufinfo(false);
}

void test_bindless_bufinfo_dxil(void)
{
    test_bindless_bufinfo(true);
}

void test_divergent_buffer_index_varying(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_DESCRIPTOR_RANGE desc_range;

    struct test_context_desc context_desc;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_param;
    struct test_context context;
    D3D12_INDEX_BUFFER_VIEW ibv;
    struct resource_readback rb;
    ID3D12DescriptorHeap *heap;
    unsigned int x, y, i;
    ID3D12Resource *ibo;
    ID3D12Resource *cbv;
    D3D12_VIEWPORT vp;
    D3D12_RECT sci;
    HRESULT hr;

    /* AMD D3D12 drivers seem to automatically promote this shader to non-uniform.
     * Assetto Corsa EVO has been observed in the wild to rely on this behavior.
     * Specifically CBVs seem to be promoted for whatever reason. */

#include "shaders/bindless/headers/vs_divergent_buffer_index_varying.h"
#include "shaders/bindless/headers/ps_divergent_buffer_index_varying.h"

    uint32_t cbv_data[64 * (256 / 4)] = { 0 };
    uint32_t ibo_data[64 * 6];

    for (i = 0; i < 64; i++)
    {
        ibo_data[6 * i + 0] = 4 * i + 0;
        ibo_data[6 * i + 1] = 4 * i + 1;
        ibo_data[6 * i + 2] = 4 * i + 2;
        ibo_data[6 * i + 3] = 4 * i + 3;
        ibo_data[6 * i + 4] = 4 * i + 2;
        ibo_data[6 * i + 5] = 4 * i + 1;
        cbv_data[i * (256 / 4)] = 1000 + i;
    }

    memset(&context_desc, 0, sizeof(context_desc));
    context_desc.no_pipeline = true;
    context_desc.rt_format = DXGI_FORMAT_R32_FLOAT;
    context_desc.rt_width = 16;
    context_desc.rt_height = 16;
    context_desc.no_root_signature = true;

    if (!init_test_context(&context, &context_desc))
        return;

    if (!is_amd_vulkan_device(context.device) && !is_amd_windows_device(context.device))
    {
        skip("Skipping test which is intended to prove weirdness on AMD specifically.\n");
        destroy_test_context(&context);
        return;
    }

    ibo = create_upload_buffer(context.device, sizeof(ibo_data), ibo_data);
    cbv = create_upload_buffer(context.device, sizeof(cbv_data), cbv_data);
    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64);

    for (i = 0; i < 64; i++)
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc;
        D3D12_CPU_DESCRIPTOR_HANDLE h;

        memset(&cbv_desc, 0, sizeof(cbv_desc));
        cbv_desc.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(cbv) + 256 * i;
        cbv_desc.SizeInBytes = 256;
        h = get_cpu_descriptor_handle(&context, heap, i);
        ID3D12Device_CreateConstantBufferView(context.device, &cbv_desc, h);
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(&rs_param, 0, sizeof(rs_param));
    memset(&desc_range, 0, sizeof(desc_range));
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &rs_param;
    rs_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_param.DescriptorTable.NumDescriptorRanges = 1;
    rs_param.DescriptorTable.pDescriptorRanges = &desc_range;
    desc_range.NumDescriptors = 64;
    desc_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ok(SUCCEEDED(create_root_signature(context.device, &rs_desc, &context.root_signature)), "Failed to create root signature.\n");

    init_pipeline_state_desc_dxil(&pso_desc, context.root_signature, DXGI_FORMAT_R32_FLOAT, &vs_divergent_buffer_index_varying_dxil, &ps_divergent_buffer_index_varying_dxil, NULL);
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso_desc.DepthStencilState.DepthEnable = FALSE;

    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&context.pipeline_state);
    ok(SUCCEEDED(hr), "Failed to create PSO, hr %x\n", hr);

    ibv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(ibo);
    ibv.Format = DXGI_FORMAT_R32_UINT;
    ibv.SizeInBytes = sizeof(ibo_data);
    set_viewport(&vp, 0, 0, 16, 16, 0, 1);
    set_rect(&sci, 0, 0, 16, 16);

    ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &context.rtv, TRUE, NULL);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_IASetIndexBuffer(context.list, &ibv);
    ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &sci);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(context.list, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
    ID3D12GraphicsCommandList_DrawIndexedInstanced(context.list, 64 * 6, 1, 0, 0, 0);

    transition_resource_state(context.list, context.render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(context.render_target, 0, &rb, context.queue, context.list);

    for (y = 0; y < 16; y++)
    {
        for (x = 0; x < 16; x++)
        {
            float expected = 1000.0f + ((y >> 1) * 8 + (x >> 1));
            float value;
            value = get_readback_float(&rb, x, y);
            ok(value == expected, "Coord %u, %u: Expected %f, got %f\n", x, y, expected, value);
        }
    }

    release_resource_readback(&rb);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12Resource_Release(ibo);
    ID3D12Resource_Release(cbv);
    destroy_test_context(&context);
}