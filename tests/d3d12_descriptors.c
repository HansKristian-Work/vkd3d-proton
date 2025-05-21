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

void test_create_descriptor_heap(void)
{
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    ID3D12Device *device, *tmp_device;
    ID3D12DescriptorHeap *heap;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 16;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(hr == S_OK, "Failed to create descriptor heap, hr %#x.\n", hr);

    refcount = get_refcount(device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    hr = ID3D12DescriptorHeap_GetDevice(heap, &IID_ID3D12Device, (void **)&tmp_device);
    ok(hr == S_OK, "Failed to get device, hr %#x.\n", hr);
    refcount = get_refcount(device);
    ok(refcount == 3, "Got unexpected refcount %u.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(tmp_device);
    ok(refcount == 2, "Got unexpected refcount %u.\n", (unsigned int)refcount);

    check_interface(heap, &IID_ID3D12Object, true);
    check_interface(heap, &IID_ID3D12DeviceChild, true);
    check_interface(heap, &IID_ID3D12Pageable, true);
    check_interface(heap, &IID_ID3D12DescriptorHeap, true);

    refcount = ID3D12DescriptorHeap_Release(heap);
    ok(!refcount, "ID3D12DescriptorHeap has %u references left.\n", (unsigned int)refcount);

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(hr == S_OK, "Failed to create descriptor heap, hr %#x.\n", hr);
    refcount = ID3D12DescriptorHeap_Release(heap);
    ok(!refcount, "ID3D12DescriptorHeap has %u references left.\n", (unsigned int)refcount);

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(hr == S_OK, "Failed to create descriptor heap, hr %#x.\n", hr);
    refcount = ID3D12DescriptorHeap_Release(heap);
    ok(!refcount, "ID3D12DescriptorHeap has %u references left.\n", (unsigned int)refcount);

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(hr == S_OK, "Failed to create descriptor heap, hr %#x.\n", hr);
    refcount = ID3D12DescriptorHeap_Release(heap);
    ok(!refcount, "ID3D12DescriptorHeap has %u references left.\n", (unsigned int)refcount);

    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(hr == S_OK, "Failed to create descriptor heap, hr %#x.\n", hr);
    refcount = ID3D12DescriptorHeap_Release(heap);
    ok(!refcount, "ID3D12DescriptorHeap has %u references left.\n", (unsigned int)refcount);

    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_descriptor_tables(void)
{
    ID3D12DescriptorHeap *heap, *sampler_heap, *heaps[2];
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_range[4];
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc;
    D3D12_ROOT_PARAMETER root_parameters[3];
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    ID3D12Resource *cb, *textures[4];
    unsigned int i, descriptor_size;
    D3D12_SAMPLER_DESC sampler_desc;
    struct test_context_desc desc;
    D3D12_SUBRESOURCE_DATA data;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

    static const DWORD ps_code[] =
    {
#if 0
        Texture2D t0;
        Texture2D t1;
        Texture2D t2;
        Texture2D t3;
        SamplerState s0;

        cbuffer cb0
        {
            float4 c;
        };

        float4 main(float4 position : SV_POSITION) : SV_Target
        {
            float2 p = float2(position.x / 32.0f, position.y / 32.0f);

            return c.x * t0.Sample(s0, p) + c.y * t1.Sample(s0, p)
                    + c.z * t2.Sample(s0, p) + c.w * t3.Sample(s0, p);
        }
#endif
        0x43425844, 0xf848ef5f, 0x4da3fe0c, 0x776883a0, 0x6b3f0297, 0x00000001, 0x0000029c, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x58454853, 0x00000200, 0x00000050,
        0x00000080, 0x0100086a, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x0300005a, 0x00106000,
        0x00000000, 0x04001858, 0x00107000, 0x00000000, 0x00005555, 0x04001858, 0x00107000, 0x00000001,
        0x00005555, 0x04001858, 0x00107000, 0x00000002, 0x00005555, 0x04001858, 0x00107000, 0x00000003,
        0x00005555, 0x04002064, 0x00101032, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000,
        0x02000068, 0x00000003, 0x0a000038, 0x00100032, 0x00000000, 0x00101046, 0x00000000, 0x00004002,
        0x3d000000, 0x3d000000, 0x00000000, 0x00000000, 0x8b000045, 0x800000c2, 0x00155543, 0x001000f2,
        0x00000001, 0x00100046, 0x00000000, 0x00107e46, 0x00000001, 0x00106000, 0x00000000, 0x08000038,
        0x001000f2, 0x00000001, 0x00100e46, 0x00000001, 0x00208556, 0x00000000, 0x00000000, 0x8b000045,
        0x800000c2, 0x00155543, 0x001000f2, 0x00000002, 0x00100046, 0x00000000, 0x00107e46, 0x00000000,
        0x00106000, 0x00000000, 0x0a000032, 0x001000f2, 0x00000001, 0x00208006, 0x00000000, 0x00000000,
        0x00100e46, 0x00000002, 0x00100e46, 0x00000001, 0x8b000045, 0x800000c2, 0x00155543, 0x001000f2,
        0x00000002, 0x00100046, 0x00000000, 0x00107e46, 0x00000002, 0x00106000, 0x00000000, 0x8b000045,
        0x800000c2, 0x00155543, 0x001000f2, 0x00000000, 0x00100046, 0x00000000, 0x00107e46, 0x00000003,
        0x00106000, 0x00000000, 0x0a000032, 0x001000f2, 0x00000001, 0x00208aa6, 0x00000000, 0x00000000,
        0x00100e46, 0x00000002, 0x00100e46, 0x00000001, 0x0a000032, 0x001020f2, 0x00000000, 0x00208ff6,
        0x00000000, 0x00000000, 0x00100e46, 0x00000000, 0x00100e46, 0x00000001, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const struct vec4 constant = {0.1f, 0.2f, 0.3f, 0.1f};
    static const unsigned int texture_data[4] = {0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffff00};

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    cb = create_upload_buffer(context.device, sizeof(constant), &constant.x);

    descriptor_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range[0].NumDescriptors = 2;
    descriptor_range[0].BaseShaderRegister = 0;
    descriptor_range[0].RegisterSpace = 0;
    descriptor_range[0].OffsetInDescriptorsFromTableStart = 1;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_range[0];
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    descriptor_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    descriptor_range[1].NumDescriptors = 1;
    descriptor_range[1].BaseShaderRegister = 0;
    descriptor_range[1].RegisterSpace = 0;
    descriptor_range[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[1].DescriptorTable.pDescriptorRanges = &descriptor_range[1];
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    descriptor_range[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range[2].NumDescriptors = 2;
    descriptor_range[2].BaseShaderRegister = 2;
    descriptor_range[2].RegisterSpace = 0;
    descriptor_range[2].OffsetInDescriptorsFromTableStart = 0;
    descriptor_range[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    descriptor_range[3].NumDescriptors = 1;
    descriptor_range[3].BaseShaderRegister = 0;
    descriptor_range[3].RegisterSpace = 0;
    descriptor_range[3].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[2].DescriptorTable.NumDescriptorRanges = 2;
    root_parameters[2].DescriptorTable.pDescriptorRanges = &descriptor_range[2];
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = 3;
    root_signature_desc.pParameters = root_parameters;
    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL, &ps, NULL);

    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 6);
    sampler_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1);

    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (i = 0; i < ARRAY_SIZE(textures); ++i)
    {
        textures[i] = create_default_texture(context.device,
                1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_RESOURCE_STATE_COPY_DEST);
        data.pData = &texture_data[i];
        data.RowPitch = sizeof(texture_data[i]);
        data.SlicePitch = data.RowPitch;
        upload_texture_data(textures[i], &data, 1, queue, command_list);
        reset_command_list(command_list, context.allocator);
    }

    for (i = 0; i < ARRAY_SIZE(textures); ++i)
        transition_resource_state(command_list, textures[i],
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    cpu_handle.ptr += descriptor_size;
    /* t0-t3 */
    for (i = 0; i < ARRAY_SIZE(textures); ++i)
    {
        ID3D12Device_CreateShaderResourceView(context.device, textures[i], NULL, cpu_handle);
        cpu_handle.ptr += descriptor_size;
    }
    /* cbv0 */
    cbv_desc.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(cb);
    cbv_desc.SizeInBytes = align(sizeof(constant), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    ID3D12Device_CreateConstantBufferView(context.device, &cbv_desc, cpu_handle);

    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sampler_heap);
    /* s0 */
    ID3D12Device_CreateSampler(context.device, &sampler_desc, cpu_handle);

    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    heaps[0] = heap; heaps[1] = sampler_heap;
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, ARRAY_SIZE(heaps), heaps);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0, gpu_handle);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 1,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(sampler_heap));
    gpu_handle.ptr += 3 * descriptor_size;
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 2, gpu_handle);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xb2664c19, 2);

    ID3D12Resource_Release(cb);
    for (i = 0; i < ARRAY_SIZE(textures); ++i)
        ID3D12Resource_Release(textures[i]);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12DescriptorHeap_Release(sampler_heap);
    destroy_test_context(&context);
}

/* Tests overlapping descriptor heap ranges for SRV and UAV descriptor tables.
 * Only descriptors used by the pipeline have to be valid.
 */
void test_descriptor_tables_overlapping_bindings(void)
{
    ID3D12Resource *input_buffers[2], *output_buffers[2];
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_range[2];
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_ROOT_PARAMETER root_parameters[3];
    ID3D12GraphicsCommandList *command_list;
    struct resource_readback rb;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

#include "shaders/descriptors/headers/overlapping_bindings.h"

    static const uint32_t buffer_data[] = {0xdeadbabe};
    static const uint32_t buffer_data2[] = {0, 1, 2, 3, 4, 5};

    if (!init_compute_test_context(&context))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    descriptor_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range[0].NumDescriptors = 10;
    descriptor_range[0].BaseShaderRegister = 0;
    descriptor_range[0].RegisterSpace = 0;
    descriptor_range[0].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_range[0];
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    descriptor_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_range[1].NumDescriptors = 10;
    descriptor_range[1].BaseShaderRegister = 0;
    descriptor_range[1].RegisterSpace = 0;
    descriptor_range[1].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[1].DescriptorTable.pDescriptorRanges = &descriptor_range[1];
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[2].Constants.ShaderRegister = 0;
    root_parameters[2].Constants.RegisterSpace = 0;
    root_parameters[2].Constants.Num32BitValues = 2;
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = 3;
    root_signature_desc.pParameters = root_parameters;
    hr = create_root_signature(device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    context.pipeline_state = create_compute_pipeline_state(device, context.root_signature, overlapping_bindings_dxbc);

    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 30);

    input_buffers[0] = create_default_buffer(device, sizeof(buffer_data),
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(input_buffers[0], 0, sizeof(buffer_data), &buffer_data, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, input_buffers[0],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    input_buffers[1] = create_default_buffer(device, sizeof(buffer_data2),
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(input_buffers[1], 0, sizeof(buffer_data2), &buffer_data2, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, input_buffers[1],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    output_buffers[0] = create_default_buffer(device, sizeof(buffer_data),
              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    output_buffers[1] = create_default_buffer(device, sizeof(buffer_data2),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.NumElements = ARRAY_SIZE(buffer_data);
    uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    ID3D12Device_CreateUnorderedAccessView(device, output_buffers[0], NULL, &uav_desc,
            get_cpu_descriptor_handle(&context, heap, 0)); /* u0 */
    uav_desc.Buffer.NumElements = ARRAY_SIZE(buffer_data2);
    ID3D12Device_CreateUnorderedAccessView(device, output_buffers[1], NULL, &uav_desc,
            get_cpu_descriptor_handle(&context, heap, 2)); /* u2 */

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Buffer.FirstElement = 0;
    srv_desc.Buffer.NumElements = ARRAY_SIZE(buffer_data);
    srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    ID3D12Device_CreateShaderResourceView(device, input_buffers[0], &srv_desc,
            get_cpu_descriptor_handle(&context, heap, 3)); /* t0 */
    srv_desc.Buffer.NumElements = ARRAY_SIZE(buffer_data2);
    ID3D12Device_CreateShaderResourceView(device, input_buffers[1], &srv_desc,
            get_cpu_descriptor_handle(&context, heap, 7)); /* t4 */

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0,
            get_gpu_descriptor_handle(&context, heap, 3));
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 1,
            get_gpu_descriptor_handle(&context, heap, 0));
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(command_list, 2,
            ARRAY_SIZE(buffer_data), 0);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(command_list, 2,
            ARRAY_SIZE(buffer_data2), 1);
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);

    for (i = 0; i < ARRAY_SIZE(output_buffers); ++i)
    {
        transition_resource_state(command_list, output_buffers[i],
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    }

    get_buffer_readback_with_command_list(output_buffers[0], DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    for (i = 0; i < ARRAY_SIZE(buffer_data); ++i)
    {
        unsigned int value = get_readback_uint(&rb, i, 0, 0);
        ok(value == buffer_data[i], "Got %#x, expected %#x.\n", value, buffer_data[i]);
    }
    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);
    get_buffer_readback_with_command_list(output_buffers[1], DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    for (i = 0; i < ARRAY_SIZE(buffer_data2); ++i)
    {
        unsigned int value = get_readback_uint(&rb, i, 0, 0);
        ok(value == buffer_data2[i], "Got %#x, expected %#x.\n", value, buffer_data2[i]);
    }
    release_resource_readback(&rb);

    for (i = 0; i < ARRAY_SIZE(input_buffers); ++i)
        ID3D12Resource_Release(input_buffers[i]);
    for (i = 0; i < ARRAY_SIZE(output_buffers); ++i)
        ID3D12Resource_Release(output_buffers[i]);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_update_root_descriptors(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_GPU_VIRTUAL_ADDRESS cb_va, uav_va;
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12GraphicsCommandList *command_list;
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pipeline_state;
    ID3D12Resource *resource, *cb;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

#include "shaders/descriptors/headers/update_root_descriptors.h"

    struct
    {
        uint32_t offset;
        uint32_t value;
        uint32_t uav_offset;
        uint8_t padding[D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 3 * sizeof(uint32_t)];
    }
    input[] =
    {
        {0, 4,  0},
        {2, 6,  0},
        {0, 5, 64},
        {7, 2, 64},
    };

    if (!init_compute_test_context(&context))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    cb = create_upload_buffer(context.device, sizeof(input), input);
    cb_va = ID3D12Resource_GetGPUVirtualAddress(cb);

    resource = create_default_buffer(device, 512,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    uav_va = ID3D12Resource_GetGPUVirtualAddress(resource);

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[1].Descriptor.ShaderRegister = 0;
    root_parameters[1].Descriptor.RegisterSpace = 0;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = 2;
    root_signature_desc.pParameters = root_parameters;
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    pipeline_state = create_compute_pipeline_state(device, root_signature, update_root_descriptors_dxbc);

    ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, root_signature);
    for (i = 0; i < ARRAY_SIZE(input); ++i)
    {
        ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(command_list,
                0, cb_va + i * sizeof(*input));
        if (!i || input[i - 1].uav_offset != input[i].uav_offset)
            ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list,
                    1, uav_va + input[i].uav_offset * sizeof(uint32_t));
        ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);
    }

    transition_sub_resource_state(command_list, resource, 0,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(resource, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    for (i = 0; i < ARRAY_SIZE(input); ++i)
    {
        unsigned int offset = input[i].uav_offset + input[i].offset;
        unsigned int value = get_readback_uint(&rb, offset, 0, 0);
        ok(value == input[i].value, "Got %#x, expected %#x.\n", value, input[i].value);
    }
    release_resource_readback(&rb);

    ID3D12Resource_Release(cb);
    ID3D12Resource_Release(resource);
    ID3D12RootSignature_Release(root_signature);
    ID3D12PipelineState_Release(pipeline_state);
    destroy_test_context(&context);
}

void test_update_descriptor_tables(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_range;
    ID3D12GraphicsCommandList *command_list;
    D3D12_STATIC_SAMPLER_DESC sampler_desc;
    ID3D12DescriptorHeap *heap, *cpu_heap;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    D3D12_ROOT_PARAMETER root_parameter;
    struct test_context_desc desc;
    D3D12_SUBRESOURCE_DATA data;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Resource *textures[3];
    ID3D12CommandQueue *queue;
    unsigned int i;
    D3D12_BOX box;
    HRESULT hr;
    RECT rect;

#include "shaders/descriptors/headers/update_descriptor_tables.h"

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const float texture_data[] = {0.5f, 0.25f, 0.1f};

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.ShaderRegister = 0;
    sampler_desc.RegisterSpace = 0;
    sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range.NumDescriptors = 2;
    descriptor_range.BaseShaderRegister = 0;
    descriptor_range.RegisterSpace = 0;
    descriptor_range.OffsetInDescriptorsFromTableStart = 0;
    root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameter.DescriptorTable.NumDescriptorRanges = 1;
    root_parameter.DescriptorTable.pDescriptorRanges = &descriptor_range;
    root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = 1;
    root_signature_desc.pParameters = &root_parameter;
    root_signature_desc.NumStaticSamplers = 1;
    root_signature_desc.pStaticSamplers = &sampler_desc;
    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL, &update_descriptor_tables_dxbc, NULL);

    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 4;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = ID3D12Device_CreateDescriptorHeap(context.device, &heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create descriptor heap, hr %#x.\n", hr);

    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = ID3D12Device_CreateDescriptorHeap(context.device, &heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&cpu_heap);
    ok(SUCCEEDED(hr), "Failed to create descriptor heap, hr %#x.\n", hr);

    for (i = 0; i < ARRAY_SIZE(textures); ++i)
    {
        textures[i] = create_default_texture(context.device, 1, 1, DXGI_FORMAT_R32_FLOAT,
                0, D3D12_RESOURCE_STATE_COPY_DEST);
        data.pData = &texture_data[i];
        data.RowPitch = sizeof(texture_data[i]);
        data.SlicePitch = data.RowPitch;
        upload_texture_data(textures[i], &data, 1, queue, command_list);
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, textures[i],
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    for (i = 0; i < heap_desc.NumDescriptors; ++i)
    {
        ID3D12Device_CreateShaderResourceView(context.device, textures[2], NULL,
                get_cpu_descriptor_handle(&context, heap, i));
    }
    for (i = 0; i < ARRAY_SIZE(textures); ++i)
    {
        ID3D12Device_CreateShaderResourceView(context.device, textures[i], NULL,
                get_cpu_descriptor_handle(&context, cpu_heap, i));
    }

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);

    set_rect(&rect, 0, 0, 16, 32);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &rect);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            get_gpu_descriptor_handle(&context, heap, 0));
    ID3D12Device_CopyDescriptorsSimple(context.device, 2,
            get_cpu_sampler_handle(&context, heap, 0),
            get_cpu_sampler_handle(&context, cpu_heap, 0),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    set_rect(&rect, 16, 0, 32, 32);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &rect);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            get_gpu_descriptor_handle(&context, heap, 2));
    ID3D12Device_CreateShaderResourceView(context.device, textures[1], NULL,
            get_cpu_descriptor_handle(&context, heap, 2));
    ID3D12Device_CreateShaderResourceView(context.device, textures[0], NULL,
            get_cpu_descriptor_handle(&context, heap, 3));
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    set_box(&box, 0, 0, 0, 16, 32, 1);
    check_readback_data_uint(&rb, &box, 0xff00407f, 1);
    set_box(&box, 16, 0, 0, 32, 32, 1);
    check_readback_data_uint(&rb, &box, 0xff007f40, 1);
    release_resource_readback(&rb);

    for (i = 0; i < ARRAY_SIZE(textures); ++i)
        ID3D12Resource_Release(textures[i]);
    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_update_descriptor_heap_after_closing_command_list(void)
{
    ID3D12Resource *red_texture, *green_texture;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    ID3D12DescriptorHeap *cpu_heap, *heap;
    D3D12_SUBRESOURCE_DATA texture_data;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int value;
    HRESULT hr;

#include "shaders/descriptors/headers/update_descriptor_heap_after_closing.h"

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const unsigned int red_data[] = {0xff0000ff};
    static const unsigned int green_data[] = {0xff00ff00};

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    context.root_signature = create_texture_root_signature(context.device,
            D3D12_SHADER_VISIBILITY_PIXEL, 0, 0);
    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL,
            &update_descriptor_heap_after_closing_dxbc, NULL);

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);

    cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    red_texture = create_default_texture(context.device, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
            0, D3D12_RESOURCE_STATE_COPY_DEST);
    texture_data.pData = red_data;
    texture_data.RowPitch = sizeof(*red_data);
    texture_data.SlicePitch = texture_data.RowPitch;
    upload_texture_data(red_texture, &texture_data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, red_texture,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    green_texture = create_default_texture(context.device, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
            0, D3D12_RESOURCE_STATE_COPY_DEST);
    texture_data.pData = green_data;
    upload_texture_data(green_texture, &texture_data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, green_texture,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    ID3D12Device_CreateShaderResourceView(context.device, red_texture, NULL,
            get_cpu_descriptor_handle(&context, cpu_heap, 0));
    ID3D12Device_CopyDescriptorsSimple(context.device, 1,
            get_cpu_sampler_handle(&context, heap, 0),
            get_cpu_sampler_handle(&context, cpu_heap, 0),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    hr = ID3D12GraphicsCommandList_Close(command_list);
    ok(SUCCEEDED(hr), "Failed to close command list, hr %#x.\n", hr);

    /* Update the descriptor heap used by the closed command list. */
    ID3D12Device_CreateShaderResourceView(context.device, green_texture, NULL, cpu_handle);

    exec_command_list(queue, command_list);
    wait_queue_idle(context.device, queue);
    reset_command_list(command_list, context.allocator);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    value = get_readback_uint(&rb, 0, 0, 0);
    ok(value == 0xff00ff00, "Got unexpected value %#x.\n", value);
    release_resource_readback(&rb);

    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12Resource_Release(green_texture);
    ID3D12Resource_Release(red_texture);
    destroy_test_context(&context);
}

void test_update_compute_descriptor_tables(void)
{
    struct cb_data
    {
        struct uvec4 srv_size[2];
        struct uvec4 uav_size[2];
    };

    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12PipelineState *buffer_pso, *texture_pso;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[4];
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_ROOT_PARAMETER root_parameters[5];
    D3D12_SUBRESOURCE_DATA subresource_data;
    ID3D12Resource *buffer_cb, *texture_cb;
    ID3D12DescriptorHeap *descriptor_heap;
    ID3D12Resource *output_buffers[2];
    ID3D12Resource *input_buffers[5];
    ID3D12Resource *textures[3];
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    struct cb_data cb_data;
    ID3D12Device *device;
    unsigned int i;
    uint32_t data;
    HRESULT hr;

#include "shaders/descriptors/headers/update_compute_descriptor_tables_buffer.h"
#include "shaders/descriptors/headers/update_compute_descriptor_tables_texture.h"

    static const uint32_t buffer0_data[] = {1, 2, 3, 1};
    static const uint32_t buffer1_data[] = {10, 20, 30, 10};
    static const uint32_t buffer2_data[] = {100, 200, 300, 200};
    static const uint32_t buffer3_data[] = {1000, 2000, 2000, 2000};
    static const uint32_t buffer4_data[] = {0, 0, 0, 0};
    static const uint32_t texture0_data[4][4] =
    {
        {1, 0, 0, 0},
        {10000, 100, 1000, 10000},
        {0, 0, 0, 2},
        {0, 30000, 10000, 10},
    };
    static const uint32_t texture1_data[4][4] =
    {
        {6, 0, 0, 0},
        {600, 0, 1000, 60000},
        {0, 40, 0, 0},
        {0, 30000, 0, 0},
    };
    static const uint32_t texture2_data[4][4] =
    {
        {1, 1, 1, 1},
        {2, 2, 2, 2},
        {3, 3, 3, 3},
        {4, 4, 4, 4},
    };
    static const uint32_t expected_output0[] = {7, 70, 800, 7000, 70, 0, 800, 7000, 61113, 91646, 800, 40};
    static const uint32_t expected_output1[] = {61113, 91646, 800, 40, 7, 70, 800, 7000};

    if (!init_compute_test_context(&context))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[0].Constants.ShaderRegister = 1;
    root_parameters[0].Constants.RegisterSpace = 0;
    root_parameters[0].Constants.Num32BitValues = 1;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[0].NumDescriptors = 1;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[1].DescriptorTable.pDescriptorRanges = &descriptor_ranges[0];
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[1].NumDescriptors = 2;
    descriptor_ranges[1].BaseShaderRegister = 0;
    descriptor_ranges[1].RegisterSpace = 0;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[2].DescriptorTable.pDescriptorRanges = &descriptor_ranges[1];
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    descriptor_ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[2].NumDescriptors = 4;
    descriptor_ranges[2].BaseShaderRegister = 4;
    descriptor_ranges[2].RegisterSpace = 0;
    descriptor_ranges[2].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[3].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[3].DescriptorTable.pDescriptorRanges = &descriptor_ranges[2];
    root_parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    descriptor_ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    descriptor_ranges[3].NumDescriptors = 1;
    descriptor_ranges[3].BaseShaderRegister = 0;
    descriptor_ranges[3].RegisterSpace = 0;
    descriptor_ranges[3].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[4].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[4].DescriptorTable.pDescriptorRanges = &descriptor_ranges[3];
    root_parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_signature_desc.NumParameters = 5;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    buffer_pso = create_compute_pipeline_state(device, context.root_signature,
        update_compute_descriptor_tables_buffer_dxbc);
    texture_pso = create_compute_pipeline_state(device, context.root_signature,
        update_compute_descriptor_tables_texture_dxbc);

    for (i = 0; i < ARRAY_SIZE(output_buffers); ++i)
    {
        output_buffers[i] = create_default_buffer(device, 1024,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    input_buffers[0] = create_default_buffer(device, sizeof(buffer0_data),
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(input_buffers[0], 0, sizeof(buffer0_data), buffer0_data, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, input_buffers[0],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    input_buffers[1] = create_default_buffer(device, sizeof(buffer1_data),
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(input_buffers[1], 0, sizeof(buffer1_data), buffer1_data, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, input_buffers[1],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    input_buffers[2] = create_default_buffer(device, sizeof(buffer2_data),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(input_buffers[2], 0, sizeof(buffer2_data), buffer2_data, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, input_buffers[2],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    input_buffers[3] = create_default_buffer(device, sizeof(buffer3_data),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(input_buffers[3], 0, sizeof(buffer3_data), buffer3_data, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, input_buffers[3],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    input_buffers[4] = create_default_buffer(device, sizeof(buffer4_data),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(input_buffers[4], 0, sizeof(buffer4_data), buffer4_data, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, input_buffers[4],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    textures[0] = create_default_texture(context.device,
            4, 4, DXGI_FORMAT_R32_UINT, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    subresource_data.pData = texture0_data;
    subresource_data.RowPitch = sizeof(*texture0_data);
    subresource_data.SlicePitch = subresource_data.RowPitch;
    upload_texture_data(textures[0], &subresource_data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, textures[0],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    textures[1] = create_default_texture(context.device,
            4, 4, DXGI_FORMAT_R32_UINT, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    subresource_data.pData = texture1_data;
    subresource_data.RowPitch = sizeof(*texture1_data);
    subresource_data.SlicePitch = subresource_data.RowPitch;
    upload_texture_data(textures[1], &subresource_data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, textures[1],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    textures[2] = create_default_texture(context.device, 4, 4, DXGI_FORMAT_R32_UINT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    subresource_data.pData = texture2_data;
    subresource_data.RowPitch = sizeof(*texture2_data);
    subresource_data.SlicePitch = subresource_data.RowPitch;
    upload_texture_data(textures[2], &subresource_data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, textures[2],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    memset(&cb_data, 0, sizeof(cb_data));
    cb_data.srv_size[0].x = ARRAY_SIZE(buffer0_data);
    cb_data.srv_size[1].x = ARRAY_SIZE(buffer1_data);
    cb_data.uav_size[0].x = ARRAY_SIZE(buffer2_data);
    cb_data.uav_size[1].x = ARRAY_SIZE(buffer3_data);
    buffer_cb = create_upload_buffer(device, sizeof(cb_data), &cb_data);

    memset(&cb_data, 0, sizeof(cb_data));
    cb_data.srv_size[0].x = 4;
    cb_data.srv_size[0].y = 4;
    cb_data.srv_size[1].x = 4;
    cb_data.srv_size[1].y = 4;
    cb_data.uav_size[0].x = ARRAY_SIZE(buffer2_data);
    cb_data.uav_size[1].x = 4;
    cb_data.uav_size[1].y = 4;
    texture_cb = create_upload_buffer(device, sizeof(cb_data), &cb_data);

    descriptor_heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 30);

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R32_UINT;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Buffer.FirstElement = 0;
    srv_desc.Buffer.NumElements = ARRAY_SIZE(buffer0_data);
    ID3D12Device_CreateShaderResourceView(device, input_buffers[0], &srv_desc,
            get_cpu_descriptor_handle(&context, descriptor_heap, 0));
    srv_desc.Buffer.NumElements = ARRAY_SIZE(buffer1_data);
    ID3D12Device_CreateShaderResourceView(device, input_buffers[1], &srv_desc,
            get_cpu_descriptor_handle(&context, descriptor_heap, 1));

    ID3D12Device_CreateShaderResourceView(device, input_buffers[1], &srv_desc,
            get_cpu_descriptor_handle(&context, descriptor_heap, 6));
    srv_desc.Buffer.NumElements = ARRAY_SIZE(buffer4_data);
    ID3D12Device_CreateShaderResourceView(device, input_buffers[4], &srv_desc,
            get_cpu_descriptor_handle(&context, descriptor_heap, 7));

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.Format = DXGI_FORMAT_R32_UINT;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.NumElements = ARRAY_SIZE(buffer2_data);
    ID3D12Device_CreateUnorderedAccessView(device, input_buffers[2], NULL, &uav_desc,
            get_cpu_descriptor_handle(&context, descriptor_heap, 2));
    ID3D12Device_CreateUnorderedAccessView(device, input_buffers[2], NULL, &uav_desc,
            get_cpu_descriptor_handle(&context, descriptor_heap, 12));
    uav_desc.Buffer.NumElements = ARRAY_SIZE(buffer3_data);
    ID3D12Device_CreateUnorderedAccessView(device, input_buffers[3], NULL, &uav_desc,
            get_cpu_descriptor_handle(&context, descriptor_heap, 5));

    ID3D12Device_CreateShaderResourceView(device, textures[0], NULL,
            get_cpu_descriptor_handle(&context, descriptor_heap, 10));
    ID3D12Device_CreateShaderResourceView(device, textures[1], NULL,
            get_cpu_descriptor_handle(&context, descriptor_heap, 11));

    ID3D12Device_CreateUnorderedAccessView(device, textures[2], NULL, NULL,
            get_cpu_descriptor_handle(&context, descriptor_heap, 14));

    cbv_desc.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(buffer_cb);
    cbv_desc.SizeInBytes = align(sizeof(cb_data), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    ID3D12Device_CreateConstantBufferView(context.device, &cbv_desc,
            get_cpu_descriptor_handle(&context, descriptor_heap, 8));

    cbv_desc.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(texture_cb);
    cbv_desc.SizeInBytes = align(sizeof(cb_data), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    ID3D12Device_CreateConstantBufferView(context.device, &cbv_desc,
            get_cpu_descriptor_handle(&context, descriptor_heap, 9));

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.NumElements = 256;
    uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    ID3D12Device_CreateUnorderedAccessView(device, output_buffers[0], NULL, &uav_desc,
            get_cpu_descriptor_handle(&context, descriptor_heap, 20));
    ID3D12Device_CreateUnorderedAccessView(device, output_buffers[1], NULL, &uav_desc,
            get_cpu_descriptor_handle(&context, descriptor_heap, 21));

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &descriptor_heap);

    ID3D12GraphicsCommandList_SetPipelineState(command_list, buffer_pso);

    ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(command_list, 0, 0 /* offset */, 0);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list,
            1, get_gpu_descriptor_handle(&context, descriptor_heap, 20)); /* u0 */
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list,
            2, get_gpu_descriptor_handle(&context, descriptor_heap, 0)); /* t1-t2 */
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list,
            3, get_gpu_descriptor_handle(&context, descriptor_heap, 2)); /* u4-u7 */
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list,
            4, get_gpu_descriptor_handle(&context, descriptor_heap, 8)); /* b0 */
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);

    ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(command_list, 0, 16 /* offset */, 0);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list,
            2, get_gpu_descriptor_handle(&context, descriptor_heap, 6));  /* t1-t2 */
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);

    ID3D12GraphicsCommandList_SetPipelineState(command_list, texture_pso);

    transition_resource_state(command_list, input_buffers[4],
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(command_list, 0, 32 /* offset */, 0);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list,
            2, get_gpu_descriptor_handle(&context, descriptor_heap, 10)); /* t1-t2 */
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list,
            3, get_gpu_descriptor_handle(&context, descriptor_heap, 12)); /* u4-u7 */
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list,
            4, get_gpu_descriptor_handle(&context, descriptor_heap, 9)); /* b0 */
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);

    ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(command_list, 0, 0 /* offset */, 0);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list,
            1, get_gpu_descriptor_handle(&context, descriptor_heap, 21)); /* u0 */
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);

    ID3D12GraphicsCommandList_SetPipelineState(command_list, buffer_pso);

    ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(command_list, 0, 16 /* offset */, 0);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list,
            2, get_gpu_descriptor_handle(&context, descriptor_heap, 0)); /* t1-t2 */
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list,
            3, get_gpu_descriptor_handle(&context, descriptor_heap, 2)); /* u4-u7 */
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list,
            4, get_gpu_descriptor_handle(&context, descriptor_heap, 8)); /* b0 */
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);

    transition_sub_resource_state(command_list, output_buffers[0], 0,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffers[0], DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    for (i = 0; i < ARRAY_SIZE(expected_output0); ++i)
    {
        data = get_readback_uint(&rb, i, 0, 0);
        ok(data == expected_output0[i], "Got %#x, expected %#x at %u.\n", data, expected_output0[i], i);
    }
    release_resource_readback(&rb);

    reset_command_list(command_list, context.allocator);
    transition_sub_resource_state(command_list, output_buffers[1], 0,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffers[1], DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    for (i = 0; i < ARRAY_SIZE(expected_output1); ++i)
    {
        data = get_readback_uint(&rb, i, 0, 0);
        ok(data == expected_output1[i], "Got %#x, expected %#x at %u.\n", data, expected_output1[i], i);
    }
    release_resource_readback(&rb);

    ID3D12Resource_Release(buffer_cb);
    ID3D12Resource_Release(texture_cb);
    for (i = 0; i < ARRAY_SIZE(input_buffers); ++i)
        ID3D12Resource_Release(input_buffers[i]);
    for (i = 0; i < ARRAY_SIZE(textures); ++i)
        ID3D12Resource_Release(textures[i]);
    for (i = 0; i < ARRAY_SIZE(output_buffers); ++i)
        ID3D12Resource_Release(output_buffers[i]);
    ID3D12PipelineState_Release(buffer_pso);
    ID3D12PipelineState_Release(texture_pso);
    ID3D12DescriptorHeap_Release(descriptor_heap);
    destroy_test_context(&context);
}

void test_update_descriptor_tables_after_root_signature_change(void)
{
    ID3D12RootSignature *root_signature, *root_signature2;
    ID3D12PipelineState *pipeline_state, *pipeline_state2;
    ID3D12DescriptorHeap *heap, *sampler_heap, *heaps[2];
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_range[4];
    D3D12_ROOT_PARAMETER root_parameters[3];
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    unsigned int i, descriptor_size;
    D3D12_SAMPLER_DESC sampler_desc;
    struct test_context_desc desc;
    ID3D12Resource *textures[2];
    D3D12_SUBRESOURCE_DATA data;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

#include "shaders/descriptors/headers/update_descriptor_tables_after_root_signature_change.h"

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const unsigned int texture_data[] = {0xff00ff00, 0xff0000ff};

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    descriptor_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range[0].NumDescriptors = 2;
    descriptor_range[0].BaseShaderRegister = 0;
    descriptor_range[0].RegisterSpace = 0;
    descriptor_range[0].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_range[0];
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    descriptor_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    descriptor_range[1].NumDescriptors = 1;
    descriptor_range[1].BaseShaderRegister = 0;
    descriptor_range[1].RegisterSpace = 0;
    descriptor_range[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[1].DescriptorTable.pDescriptorRanges = &descriptor_range[1];
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    descriptor_range[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range[2].NumDescriptors = 2;
    descriptor_range[2].BaseShaderRegister = 2;
    descriptor_range[2].RegisterSpace = 0;
    descriptor_range[2].OffsetInDescriptorsFromTableStart = 0;
    descriptor_range[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    descriptor_range[3].NumDescriptors = 1;
    descriptor_range[3].BaseShaderRegister = 0;
    descriptor_range[3].RegisterSpace = 0;
    descriptor_range[3].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[2].DescriptorTable.NumDescriptorRanges = 2;
    root_parameters[2].DescriptorTable.pDescriptorRanges = &descriptor_range[2];
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    hr = create_root_signature(context.device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters) - 1;
    hr = create_root_signature(context.device, &root_signature_desc, &root_signature2);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    pipeline_state = create_pipeline_state(context.device,
            root_signature, context.render_target_desc.Format, NULL,
            &update_descriptor_tables_after_root_signature_change_dxbc, NULL);
    pipeline_state2 = create_pipeline_state(context.device,
            root_signature2, context.render_target_desc.Format, NULL,
            &update_descriptor_tables_after_root_signature_change_dxbc, NULL);

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 6);
    sampler_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1);

    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ID3D12Device_CreateSampler(context.device, &sampler_desc, get_cpu_descriptor_handle(&context, sampler_heap, 0));

    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (i = 0; i < ARRAY_SIZE(textures); ++i)
    {
        textures[i] = create_default_texture(context.device,
                1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_RESOURCE_STATE_COPY_DEST);
        data.pData = &texture_data[i];
        data.RowPitch = sizeof(texture_data[i]);
        data.SlicePitch = data.RowPitch;
        upload_texture_data(textures[i], &data, 1, queue, command_list);
        reset_command_list(command_list, context.allocator);
    }

    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    for (i = 0; i < ARRAY_SIZE(textures); ++i)
    {
        transition_resource_state(command_list, textures[i],
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ID3D12Device_CreateShaderResourceView(context.device, textures[i], NULL, cpu_handle);
        cpu_handle.ptr += descriptor_size;
    }
    for (; i < 6; ++i)
    {
        ID3D12Device_CreateShaderResourceView(context.device, textures[1], NULL, cpu_handle);
        cpu_handle.ptr += descriptor_size;
    }

    heaps[0] = heap; heaps[1] = sampler_heap;
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, ARRAY_SIZE(heaps), heaps);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_state);

    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            get_gpu_descriptor_handle(&context, heap, 0));
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 1,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(sampler_heap));
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 2,
            get_gpu_descriptor_handle(&context, heap, 2));

    ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_state2);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, root_signature2);

    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            get_gpu_descriptor_handle(&context, heap, 0));
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 1,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(sampler_heap));

    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, ARRAY_SIZE(heaps), heaps);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_state2);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, root_signature2);

    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            get_gpu_descriptor_handle(&context, heap, 0));
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 1,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(sampler_heap));

    ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_state);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, root_signature);

    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12PipelineState_Release(pipeline_state);
    ID3D12PipelineState_Release(pipeline_state2);
    ID3D12RootSignature_Release(root_signature);
    ID3D12RootSignature_Release(root_signature2);
    for (i = 0; i < ARRAY_SIZE(textures); ++i)
        ID3D12Resource_Release(textures[i]);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12DescriptorHeap_Release(sampler_heap);
    destroy_test_context(&context);
}

void test_copy_descriptors(void)
{
    struct data
    {
        unsigned int u[3];
        float f;
    };

    ID3D12DescriptorHeap *cpu_heap, *cpu_sampler_heap, *cpu_sampler_heap2;
    D3D12_CPU_DESCRIPTOR_HANDLE dst_handles[4], src_handles[4];
    ID3D12DescriptorHeap *heap, *sampler_heap, *heaps[2];
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[5];
    UINT dst_range_sizes[4], src_range_sizes[4];
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_ROOT_PARAMETER root_parameters[4];
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    ID3D12Resource *t[7], *u[3], *cb;
    struct depth_stencil_resource ds;
    D3D12_SAMPLER_DESC sampler_desc;
    struct test_context_desc desc;
    unsigned int descriptor_size;
    D3D12_SUBRESOURCE_DATA data;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    unsigned int sampler_size;
    ID3D12Device *device;
    unsigned int *result;
    unsigned int i;
    HRESULT hr;

#include "shaders/descriptors/headers/copy_descriptors.h"

    static const float cb0_data = 10.0f;
    static const UINT cb1_data = 11;
    static const INT cb2_data = -1;
    static const struct vec4 t0_data = {1.0f, 2.0f, 3.0f, 4.0f};
    static const UINT t1_data = 111;
    static const INT t2_data = 222;
    static const float t3_data = 333.3f;
    static const float t4_data = 44.44f;
    static const struct uvec4 t5_data = {50, 51, 52, 53};
    static const struct uvec4 u0_data = {10, 20, 30, 40};
    static const struct data u1_data = {{5, 6, 7}, 10.0f};

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(device,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    sampler_size = ID3D12Device_GetDescriptorHandleIncrementSize(device,
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    cpu_sampler_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2);
    cpu_sampler_heap2 = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2);
    sampler_heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 4);

    cpu_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 30);
    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 30);

    /* create samplers */
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_sampler_heap);
    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ID3D12Device_CreateSampler(context.device, &sampler_desc, cpu_handle);
    sampler_desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER;
    cpu_handle.ptr += sampler_size;
    ID3D12Device_CreateSampler(context.device, &sampler_desc, cpu_handle);

    /* create CBVs */
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap);
    cb = create_upload_buffer(context.device,
            3 * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, NULL);
    update_buffer_data(cb, 0, sizeof(cb0_data), &cb0_data);
    update_buffer_data(cb, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeof(cb1_data), &cb1_data);
    update_buffer_data(cb, 2 * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, sizeof(cb2_data), &cb2_data);
    cbv_desc.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(cb);
    cbv_desc.SizeInBytes = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    for (i = 0; i < 3; ++i)
    {
        ID3D12Device_CreateConstantBufferView(context.device, &cbv_desc, cpu_handle);
        cbv_desc.BufferLocation += D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        cpu_handle.ptr += descriptor_size;
    }

    /* create SRVs */
    cpu_handle = get_cpu_descriptor_handle(&context, cpu_heap, 10);

    t[0] = create_default_texture(context.device,
            1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    data.pData = &t0_data;
    data.RowPitch = sizeof(t0_data);
    data.SlicePitch = data.RowPitch;
    upload_texture_data(t[0], &data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, t[0],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    t[1] = create_default_texture(context.device,
            1, 1, DXGI_FORMAT_R32_UINT, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    data.pData = &t1_data;
    data.RowPitch = sizeof(t1_data);
    data.SlicePitch = data.RowPitch;
    upload_texture_data(t[1], &data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, t[1],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    t[2] = create_default_texture(context.device,
            1, 1, DXGI_FORMAT_R32_SINT, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    data.pData = &t2_data;
    data.RowPitch = sizeof(t2_data);
    data.SlicePitch = data.RowPitch;
    upload_texture_data(t[2], &data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, t[2],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    t[3] = create_default_buffer(device, sizeof(t3_data),
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(t[3], 0, sizeof(t3_data), &t3_data, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, t[3],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    t[4] = create_default_buffer(device, sizeof(t4_data),
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(t[4], 0, sizeof(t4_data), &t4_data, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, t[4],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    t[5] = create_default_buffer(device, sizeof(t5_data),
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(t[5], 0, sizeof(t5_data), &t5_data, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, t[5],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    init_depth_stencil(&ds, device, 32, 32, 1, 1, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, NULL);
    t[6] = ds.texture;
    ID3D12Resource_AddRef(t[6]);
    ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
            D3D12_CLEAR_FLAG_DEPTH, 0.5f, 0, 0, NULL);
    transition_resource_state(command_list, t[6],
            D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    for (i = 0; i < 3; ++i)
    {
        ID3D12Device_CreateShaderResourceView(device, t[i], NULL, cpu_handle);
        cpu_handle.ptr += descriptor_size;
    }

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Buffer.FirstElement = 0;
    srv_desc.Buffer.NumElements = 1;
    ID3D12Device_CreateShaderResourceView(device, t[3], &srv_desc, cpu_handle);
    cpu_handle.ptr += descriptor_size;

    srv_desc.Format = DXGI_FORMAT_UNKNOWN;
    srv_desc.Buffer.StructureByteStride = sizeof(t4_data);
    ID3D12Device_CreateShaderResourceView(device, t[4], &srv_desc, cpu_handle);
    cpu_handle.ptr += descriptor_size;

    srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    srv_desc.Buffer.NumElements = 4;
    srv_desc.Buffer.StructureByteStride = 0;
    srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    ID3D12Device_CreateShaderResourceView(device, t[5], &srv_desc, cpu_handle);
    cpu_handle.ptr += descriptor_size;

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(device, t[6], &srv_desc, cpu_handle);

    /* create UAVs */
    cpu_handle = get_cpu_descriptor_handle(&context, cpu_heap, 20);

    u[0] = create_default_buffer(device, sizeof(u0_data),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(u[0], 0, sizeof(u0_data), &u0_data, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, u[0],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    u[1] = create_default_buffer(device, sizeof(struct uvec4),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_buffer_data(u[1], 0, sizeof(u1_data), &u1_data, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, u[0],
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    u[2] = create_default_buffer(device, 44 * 4,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.NumElements = 4;
    uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    ID3D12Device_CreateUnorderedAccessView(device, u[0], NULL, &uav_desc, cpu_handle);
    cpu_handle.ptr += descriptor_size;

    uav_desc.Format = DXGI_FORMAT_UNKNOWN;
    uav_desc.Buffer.NumElements = 1;
    uav_desc.Buffer.StructureByteStride = sizeof(u1_data);
    uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    ID3D12Device_CreateUnorderedAccessView(device, u[1], NULL, &uav_desc, cpu_handle);
    cpu_handle.ptr += descriptor_size;

    uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    uav_desc.Buffer.NumElements = 44;
    uav_desc.Buffer.StructureByteStride = 0;
    uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    ID3D12Device_CreateUnorderedAccessView(device, u[2], NULL, &uav_desc, cpu_handle);

    /* root signature */
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    descriptor_ranges[0].NumDescriptors = 3;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_ranges[0];
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    descriptor_ranges[1].NumDescriptors = 4;
    descriptor_ranges[1].BaseShaderRegister = 0;
    descriptor_ranges[1].RegisterSpace = 0;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[1].DescriptorTable.pDescriptorRanges = &descriptor_ranges[1];
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    descriptor_ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[2].NumDescriptors = 7;
    descriptor_ranges[2].BaseShaderRegister = 0;
    descriptor_ranges[2].RegisterSpace = 0;
    descriptor_ranges[2].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[2].DescriptorTable.pDescriptorRanges = &descriptor_ranges[2];
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    descriptor_ranges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[3].NumDescriptors = 2;
    descriptor_ranges[3].BaseShaderRegister = 0;
    descriptor_ranges[3].RegisterSpace = 0;
    descriptor_ranges[3].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[4].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[4].NumDescriptors = 1;
    descriptor_ranges[4].BaseShaderRegister = 2;
    descriptor_ranges[4].RegisterSpace = 0;
    descriptor_ranges[4].OffsetInDescriptorsFromTableStart = 2;
    root_parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[3].DescriptorTable.NumDescriptorRanges = 2;
    root_parameters[3].DescriptorTable.pDescriptorRanges = &descriptor_ranges[3];
    root_parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = 4;
    root_signature_desc.pParameters = root_parameters;
    hr = create_root_signature(device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    context.pipeline_state = create_compute_pipeline_state(device, context.root_signature,
            copy_descriptors_dxbc);

    /* copy descriptors */
    dst_handles[0] = get_cpu_descriptor_handle(&context, heap, 5);
    dst_range_sizes[0] = 2;
    src_handles[0] = get_cpu_descriptor_handle(&context, cpu_heap, 0);
    src_range_sizes[0] = 2;
    /* cb0-cb1 */
    ID3D12Device_CopyDescriptors(device, 1, dst_handles, dst_range_sizes,
            1, src_handles, src_range_sizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dst_handles[0] = get_cpu_descriptor_handle(&context, heap, 7);
    dst_range_sizes[0] = 1;
    src_handles[0] = get_cpu_descriptor_handle(&context, cpu_heap, 2);
    src_range_sizes[0] = 1;
    /* cb2 */
    ID3D12Device_CopyDescriptors(device, 1, dst_handles, dst_range_sizes,
            1, src_handles, src_range_sizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12Device_CopyDescriptorsSimple(device, 2,
            get_cpu_sampler_handle(&context, cpu_sampler_heap2, 0),
            get_cpu_sampler_handle(&context, cpu_sampler_heap, 0),
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    dst_handles[0] = get_cpu_sampler_handle(&context, sampler_heap, 0);
    dst_range_sizes[0] = 4;
    src_handles[0] = get_cpu_sampler_handle(&context, cpu_sampler_heap2, 0);
    src_handles[1] = get_cpu_sampler_handle(&context, cpu_sampler_heap2, 0);
    src_handles[2] = get_cpu_sampler_handle(&context, cpu_sampler_heap2, 0);
    src_handles[3] = get_cpu_sampler_handle(&context, cpu_sampler_heap2, 1);
    /* s0-s3 */
    ID3D12Device_CopyDescriptors(device, 1, dst_handles, dst_range_sizes,
            4, src_handles, NULL, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    dst_handles[0] = get_cpu_descriptor_handle(&context, heap, 9);
    dst_range_sizes[0] = 4;
    dst_handles[1] = get_cpu_descriptor_handle(&context, heap, 9);
    dst_range_sizes[1] = 0;
    dst_handles[2] = get_cpu_descriptor_handle(&context, heap, 13);
    dst_range_sizes[2] = 3;
    dst_handles[3] = get_cpu_descriptor_handle(&context, heap, 13);
    dst_range_sizes[3] = 0;
    src_handles[0] = get_cpu_descriptor_handle(&context, cpu_heap, 10);
    src_range_sizes[0] = 8;
    /* t0-t6 */
    ID3D12Device_CopyDescriptors(device, 4, dst_handles, dst_range_sizes,
            1, src_handles, src_range_sizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    /* copy 1 uninitialized descriptor (19) */
    dst_handles[0] = get_cpu_descriptor_handle(&context, heap, 19);
    dst_range_sizes[0] = 2;
    dst_handles[1] = get_cpu_descriptor_handle(&context, heap, 21);
    dst_range_sizes[1] = 1;
    src_handles[0] = get_cpu_descriptor_handle(&context, cpu_heap, 19);
    src_range_sizes[0] = 2;
    src_handles[1] = get_cpu_descriptor_handle(&context, cpu_heap, 21);
    src_range_sizes[1] = 1;
    /* u1-u2 */
    ID3D12Device_CopyDescriptors(device, 2, dst_handles, dst_range_sizes,
            2, src_handles, src_range_sizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    /* u2 */
    ID3D12Device_CopyDescriptorsSimple(device, 1,
            get_cpu_descriptor_handle(&context, heap, 22),
            get_cpu_descriptor_handle(&context, cpu_heap, 22),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    /* range sizes equal to 0 */
    dst_handles[0] = get_cpu_descriptor_handle(&context, heap, 19);
    dst_range_sizes[0] = 0;
    dst_handles[1] = get_cpu_descriptor_handle(&context, heap, 19);
    dst_range_sizes[1] = 0;
    src_handles[0] = get_cpu_descriptor_handle(&context, cpu_heap, 0);
    src_range_sizes[0] = 1;
    src_handles[1] = get_cpu_descriptor_handle(&context, cpu_heap, 0);
    src_range_sizes[1] = 4;
    ID3D12Device_CopyDescriptors(device, 2, dst_handles, dst_range_sizes,
            2, src_handles, src_range_sizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dst_handles[0] = get_cpu_descriptor_handle(&context, heap, 19);
    dst_range_sizes[0] = 4;
    dst_handles[1] = get_cpu_descriptor_handle(&context, heap, 19);
    dst_range_sizes[1] = 4;
    src_handles[0] = get_cpu_descriptor_handle(&context, cpu_heap, 0);
    src_range_sizes[0] = 0;
    src_handles[1] = get_cpu_descriptor_handle(&context, cpu_heap, 0);
    src_range_sizes[1] = 0;
    ID3D12Device_CopyDescriptors(device, 2, dst_handles, dst_range_sizes,
            2, src_handles, src_range_sizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    heaps[0] = sampler_heap; heaps[1] = heap;
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, ARRAY_SIZE(heaps), heaps);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0,
            get_gpu_descriptor_handle(&context, heap, 5));
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 1,
            get_gpu_sampler_handle(&context, sampler_heap, 0));
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 2,
            get_gpu_descriptor_handle(&context, heap, 9));
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 3,
            get_gpu_descriptor_handle(&context, heap, 20));

    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);

    transition_sub_resource_state(command_list, u[2], 0,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(u[2], DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    result = get_readback_data(&rb, 0, 0, 0, sizeof(*result));
    ok(result[ 0] == cb0_data, "Got unexpected value %#x.\n", result[0]);
    ok(result[ 1] == cb1_data, "Got unexpected value %#x.\n", result[1]);
    ok(result[ 2] == (unsigned int)cb2_data, "Got unexpected value %#x.\n", result[2]);
    ok(result[ 3] == 0, "Got unexpected value %#x.\n", result[3]);
    ok(result[ 4] == t0_data.x, "Got unexpected value %#x.\n", result[4]);
    ok(result[ 5] == t0_data.y, "Got unexpected value %#x.\n", result[5]);
    ok(result[ 6] == t0_data.z, "Got unexpected value %#x.\n", result[6]);
    ok(result[ 7] == t0_data.w, "Got unexpected value %#x.\n", result[7]);
    ok(result[ 8] == t0_data.x, "Got unexpected value %#x.\n", result[8]);
    ok(result[ 9] == t0_data.y, "Got unexpected value %#x.\n", result[9]);
    ok(result[10] == t0_data.z, "Got unexpected value %#x.\n", result[10]);
    ok(result[11] == t0_data.w, "Got unexpected value %#x.\n", result[11]);
    ok(result[12] == t0_data.x, "Got unexpected value %#x.\n", result[12]);
    ok(result[13] == t0_data.y, "Got unexpected value %#x.\n", result[13]);
    ok(result[14] == t0_data.z, "Got unexpected value %#x.\n", result[14]);
    ok(result[15] == t0_data.w, "Got unexpected value %#x.\n", result[15]);
    ok(result[16] == t1_data, "Got unexpected value %#x.\n", result[16]);
    ok(result[17] == (unsigned int)t2_data, "Got unexpected value %#x.\n", result[17]);
    ok(result[18] == (unsigned int)t3_data, "Got unexpected value %#x.\n", result[18]);
    ok(result[19] == (unsigned int)t4_data, "Got unexpected value %#x.\n", result[19]);
    ok(result[20] == t5_data.x, "Got unexpected value %#x.\n", result[20]);
    ok(result[21] == t5_data.y, "Got unexpected value %#x.\n", result[21]);
    ok(result[22] == t5_data.z, "Got unexpected value %#x.\n", result[22]);
    ok(result[23] == t5_data.w, "Got unexpected value %#x.\n", result[23]);
    ok(result[24] == 1, "Got unexpected value %#x.\n", result[24]);
    ok(result[25] == 1, "Got unexpected value %#x.\n", result[25]);
    ok(result[26] == 1, "Got unexpected value %#x.\n", result[26]);
    ok(result[27] == 1, "Got unexpected value %#x.\n", result[27]);
    ok(result[28] == 0, "Got unexpected value %#x.\n", result[28]);
    ok(result[29] == 0, "Got unexpected value %#x.\n", result[29]);
    ok(result[30] == 0, "Got unexpected value %#x.\n", result[30]);
    ok(result[31] == 0, "Got unexpected value %#x.\n", result[31]);
    ok(result[32] == u0_data.x, "Got unexpected value %#x.\n", result[32]);
    ok(result[33] == u0_data.y, "Got unexpected value %#x.\n", result[33]);
    ok(result[34] == u0_data.z, "Got unexpected value %#x.\n", result[34]);
    ok(result[35] == u0_data.w, "Got unexpected value %#x.\n", result[35]);
    ok(result[36] == u1_data.u[0], "Got unexpected value %#x.\n", result[36]);
    ok(result[37] == u1_data.u[1], "Got unexpected value %#x.\n", result[37]);
    ok(result[38] == u1_data.u[2], "Got unexpected value %#x.\n", result[38]);
    ok(result[39] == u1_data.f, "Got unexpected value %#x.\n", result[39]);
    ok(result[40] == u1_data.f, "Got unexpected value %#x.\n", result[40]);
    ok(result[41] == u1_data.f, "Got unexpected value %#x.\n", result[41]);
    ok(result[42] == u1_data.f, "Got unexpected value %#x.\n", result[42]);
    ok(result[43] == 0xdeadbeef, "Got unexpected value %#x.\n", result[43]);
    assert(rb.width == 44);
    release_resource_readback(&rb);

    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12DescriptorHeap_Release(cpu_sampler_heap);
    ID3D12DescriptorHeap_Release(cpu_sampler_heap2);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12DescriptorHeap_Release(sampler_heap);
    ID3D12Resource_Release(cb);
    for (i = 0; i < ARRAY_SIZE(t); ++i)
        ID3D12Resource_Release(t[i]);
    for (i = 0; i < ARRAY_SIZE(u); ++i)
        ID3D12Resource_Release(u[i]);
    destroy_depth_stencil(&ds);
    destroy_test_context(&context);
}

void test_copy_descriptors_range_sizes(void)
{
    D3D12_CPU_DESCRIPTOR_HANDLE dst_handles[1], src_handles[1];
    D3D12_CPU_DESCRIPTOR_HANDLE green_handle, blue_handle;
    ID3D12Resource *green_texture, *blue_texture;
    UINT dst_range_sizes[1], src_range_sizes[1];
    ID3D12GraphicsCommandList *command_list;
    ID3D12DescriptorHeap *cpu_heap;
    struct test_context_desc desc;
    D3D12_SUBRESOURCE_DATA data;
    struct resource_readback rb;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int i;
    D3D12_BOX box;

#include "shaders/descriptors/headers/copy_descriptors_range_sizes.h"

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const struct vec4 green = {0.0f, 1.0f, 0.0f, 1.0f};
    static const struct vec4 blue = {0.0f, 0.0f, 1.0f, 1.0f};

    memset(&desc, 0, sizeof(desc));
    desc.rt_width = desc.rt_height = 6;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    cpu_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 10);
    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8);

    green_handle = get_cpu_descriptor_handle(&context, cpu_heap, 0);
    blue_handle = get_cpu_descriptor_handle(&context, cpu_heap, 1);

    green_texture = create_default_texture(context.device,
            1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    data.pData = &green;
    data.RowPitch = sizeof(green);
    data.SlicePitch = data.RowPitch;
    upload_texture_data(green_texture, &data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, green_texture,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ID3D12Device_CreateShaderResourceView(device, green_texture, NULL, green_handle);

    blue_texture = create_default_texture(context.device,
            1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    data.pData = &blue;
    data.RowPitch = sizeof(blue);
    data.SlicePitch = data.RowPitch;
    upload_texture_data(blue_texture, &data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, blue_texture,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ID3D12Device_CreateShaderResourceView(device, blue_texture, NULL, blue_handle);

    context.root_signature = create_texture_root_signature(context.device,
            D3D12_SHADER_VISIBILITY_PIXEL, 0, 0);
    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL,
            &copy_descriptors_range_sizes_dxbc, NULL);

    /* copy descriptors */
    dst_handles[0] = get_cpu_descriptor_handle(&context, heap, 1);
    dst_range_sizes[0] = 1;
    src_handles[0] = blue_handle;
    src_range_sizes[0] = 1;
    ID3D12Device_CopyDescriptors(device, 1, dst_handles, dst_range_sizes,
            1, src_handles, src_range_sizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    dst_handles[0] = get_cpu_descriptor_handle(&context, heap, 2);
    dst_range_sizes[0] = 1;
    src_handles[0] = green_handle;
    src_range_sizes[0] = 1;
    ID3D12Device_CopyDescriptors(device, 1, dst_handles, dst_range_sizes,
            1, src_handles, src_range_sizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    dst_handles[0] = get_cpu_descriptor_handle(&context, heap, 3);
    src_handles[0] = blue_handle;
    src_range_sizes[0] = 1;
    ID3D12Device_CopyDescriptors(device, 1, dst_handles, NULL,
            1, src_handles, src_range_sizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    dst_handles[0] = get_cpu_descriptor_handle(&context, heap, 4);
    src_handles[0] = green_handle;
    src_range_sizes[0] = 1;
    ID3D12Device_CopyDescriptors(device, 1, dst_handles, NULL,
            1, src_handles, src_range_sizes, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    dst_handles[0] = get_cpu_descriptor_handle(&context, heap, 5);
    src_handles[0] = blue_handle;
    ID3D12Device_CopyDescriptors(device, 1, dst_handles, NULL,
            1, src_handles, NULL, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    dst_handles[0] = get_cpu_descriptor_handle(&context, heap, 0);
    src_handles[0] = green_handle;
    ID3D12Device_CopyDescriptors(device, 1, dst_handles, NULL,
            1, src_handles, NULL, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    for (i = 0; i < desc.rt_width; ++i)
    {
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
                get_gpu_descriptor_handle(&context, heap, i));
        set_viewport(&context.viewport, i, 0.0f, 1.0f, desc.rt_height, 0.0f, 1.0f);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);
    }

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);
    for (i = 0; i < desc.rt_width; ++i)
    {
        set_box(&box, i, 0, 0, i + 1, desc.rt_height, 1);
        check_readback_data_uint(&rb, &box, i % 2 ? 0xffff0000 : 0xff00ff00, 0);
    }
    release_resource_readback(&rb);

    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12Resource_Release(blue_texture);
    ID3D12Resource_Release(green_texture);
    destroy_test_context(&context);
}

void test_copy_rtv_descriptors(void)
{
    D3D12_CPU_DESCRIPTOR_HANDLE dst_ranges[1], src_ranges[2];
    ID3D12GraphicsCommandList *command_list;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    UINT dst_sizes[1], src_sizes[2];
    ID3D12DescriptorHeap *rtv_heap;
    struct test_context_desc desc;
    struct test_context context;
    D3D12_RESOURCE_DESC rt_desc;
    ID3D12Resource *rt_texture;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};

    static const struct
    {
        float color[4];
    }
    clears[] =
    {
        {{1.0f, 0.0f, 0.0f, 1.0f}},
        {{0.0f, 1.0f, 0.0f, 1.0f}},
        {{0.0f, 0.0f, 1.0f, 1.0f}},
        {{0.0f, 1.0f, 1.0f, 1.0f}},
    };

    static const UINT expected[] =
    {
        0xffffff00u,
        0xff0000ffu,
        0xff00ff00u,
        0xffff0000u,
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    rt_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rt_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    rt_desc.Width = 1;
    rt_desc.Height = 1;
    rt_desc.DepthOrArraySize = 4;
    rt_desc.MipLevels = 1;
    rt_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rt_desc.SampleDesc.Count = 1;
    rt_desc.SampleDesc.Quality = 0;
    rt_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rt_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &rt_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, NULL,
            &IID_ID3D12Resource, (void **)&rt_texture);
    ok(hr == S_OK, "Failed to create committed resource, hr %#x.\n", hr);

    rtv_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 9);

    ID3D12Device_CreateRenderTargetView(device, rt_texture, NULL, get_cpu_rtv_handle(&context, rtv_heap, 0));
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, get_cpu_rtv_handle(&context, rtv_heap, 0), white, 0, NULL);

    rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    rtv_desc.Texture2DArray.MipSlice = 0;
    rtv_desc.Texture2DArray.ArraySize = 1;
    rtv_desc.Texture2DArray.PlaneSlice = 0;

    for (i = 0; i < 4; i++)
    {
        rtv_desc.Texture2DArray.FirstArraySlice = i;
        ID3D12Device_CreateRenderTargetView(device, rt_texture, &rtv_desc,
                get_cpu_rtv_handle(&context, rtv_heap, 1 + i));
    }

    ID3D12Device_CopyDescriptorsSimple(device, 2,
            get_cpu_rtv_handle(&context, rtv_heap, 5),
            get_cpu_rtv_handle(&context, rtv_heap, 2),
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    dst_ranges[0] = get_cpu_rtv_handle(&context, rtv_heap, 7);
    src_ranges[0] = get_cpu_rtv_handle(&context, rtv_heap, 4);
    src_ranges[1] = get_cpu_rtv_handle(&context, rtv_heap, 1);

    dst_sizes[0] = 2;
    src_sizes[0] = 1;
    src_sizes[1] = 1;

    ID3D12Device_CopyDescriptors(device,
            ARRAY_SIZE(dst_ranges), dst_ranges, dst_sizes,
            ARRAY_SIZE(src_ranges), src_ranges, src_sizes,
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (i = 0; i < 4; i++)
        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, get_cpu_rtv_handle(&context, rtv_heap, 5 + i), clears[i].color, 0, NULL);

    for (i = 0; i < 4; i++)
    {
        transition_sub_resource_state(command_list, rt_texture, i,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(rt_texture, i, queue, command_list, expected[i], 0);
        reset_command_list(command_list, context.allocator);
    }

    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12Resource_Release(rt_texture);
    destroy_test_context(&context);
}

void test_descriptors_visibility(void)
{
    ID3D12Resource *vs_raw_buffer, *ps_raw_buffer;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    D3D12_STATIC_SAMPLER_DESC sampler_desc[2];
    ID3D12Resource *vs_texture, *ps_texture;
    ID3D12GraphicsCommandList *command_list;
    D3D12_ROOT_PARAMETER root_parameters[6];
    ID3D12Resource *vs_cb, *ps_cb;
    struct test_context_desc desc;
    D3D12_SUBRESOURCE_DATA data;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    HRESULT hr;

#include "shaders/descriptors/headers/descriptors_visibility.h"
#include "shaders/descriptors/headers/descriptors_visibility_ps.h"

    static const struct vec4 vs_cb_data = {4.0f, 8.0f, 16.0f, 32.0f};
    static const struct vec4 ps_cb_data = {1.0f, 2.0f, 3.0f, 4.0f};
    static const uint32_t vs_buffer_data[] = {0, 1, 2, 3, 4, 5, 6};
    static const uint32_t ps_buffer_data[] = {2, 4, 8};
    static const float vs_texture_data[] = {1.0f, 1.0f, 0.0f, 1.0f};
    static const float ps_texture_data[] = {0.0f, 1.0f, 0.0f, 1.0f};
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    sampler_desc[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler_desc[0].MipLODBias = 0.0f;
    sampler_desc[0].MaxAnisotropy = 0;
    sampler_desc[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler_desc[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler_desc[0].MinLOD = 0.0f;
    sampler_desc[0].MaxLOD = 0.0f;
    sampler_desc[0].ShaderRegister = 0;
    sampler_desc[0].RegisterSpace = 0;
    sampler_desc[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    sampler_desc[1] = sampler_desc[0];
    sampler_desc[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameters[1].Descriptor.ShaderRegister = 0;
    root_parameters[1].Descriptor.RegisterSpace = 0;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[2].Descriptor.ShaderRegister = 0;
    root_parameters[2].Descriptor.RegisterSpace = 0;
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    root_parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[3].Descriptor.ShaderRegister = 0;
    root_parameters[3].Descriptor.RegisterSpace = 0;
    root_parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].NumDescriptors = 1;
    descriptor_ranges[0].BaseShaderRegister = 1;
    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[4].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[4].DescriptorTable.pDescriptorRanges = &descriptor_ranges[0];
    root_parameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[1].NumDescriptors = 1;
    descriptor_ranges[1].BaseShaderRegister = 1;
    descriptor_ranges[1].RegisterSpace = 0;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    root_parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[5].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[5].DescriptorTable.pDescriptorRanges = &descriptor_ranges[1];
    root_parameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = 6;
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 2;
    root_signature_desc.pStaticSamplers = sampler_desc;
    hr = create_root_signature(device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    context.pipeline_state = create_pipeline_state(device,
            context.root_signature, context.render_target_desc.Format,
            &descriptors_visibility_dxbc, &descriptors_visibility_ps_dxbc, NULL);

    vs_cb = create_upload_buffer(device, sizeof(vs_cb_data), &vs_cb_data);
    ps_cb = create_upload_buffer(device, sizeof(ps_cb_data), &ps_cb_data);

    vs_raw_buffer = create_upload_buffer(device, sizeof(vs_buffer_data), vs_buffer_data);
    ps_raw_buffer = create_upload_buffer(device, sizeof(ps_buffer_data), ps_buffer_data);

    vs_texture = create_default_texture(device,
            1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    data.pData = vs_texture_data;
    data.RowPitch = sizeof(vs_texture_data);
    data.SlicePitch = data.RowPitch;
    upload_texture_data(vs_texture, &data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, vs_texture,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    ps_texture = create_default_texture(device,
            1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    data.pData = ps_texture_data;
    data.RowPitch = sizeof(ps_texture_data);
    data.SlicePitch = data.RowPitch;
    upload_texture_data(ps_texture, &data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, ps_texture,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    ID3D12Device_CreateShaderResourceView(device, vs_texture, NULL,
            get_cpu_descriptor_handle(&context, heap, 0));
    ID3D12Device_CreateShaderResourceView(device, ps_texture, NULL,
            get_cpu_descriptor_handle(&context, heap, 1));

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(command_list,
            0, ID3D12Resource_GetGPUVirtualAddress(vs_cb));
    ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(command_list,
            1, ID3D12Resource_GetGPUVirtualAddress(ps_cb));
    ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(command_list,
            2, ID3D12Resource_GetGPUVirtualAddress(vs_raw_buffer));
    ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(command_list,
            3, ID3D12Resource_GetGPUVirtualAddress(ps_raw_buffer));
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list,
            4, get_gpu_descriptor_handle(&context, heap, 0));
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list,
            5, get_gpu_descriptor_handle(&context, heap, 1));

    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_resource_state(command_list, context.render_target,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12Resource_Release(vs_cb);
    ID3D12Resource_Release(ps_cb);
    ID3D12Resource_Release(vs_texture);
    ID3D12Resource_Release(ps_texture);
    ID3D12Resource_Release(vs_raw_buffer);
    ID3D12Resource_Release(ps_raw_buffer);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_create_null_descriptors(void)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12Device *device;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 16;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc,
            &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create descriptor heap, hr %#x.\n", hr);

    cbv_desc.BufferLocation = 0;
    cbv_desc.SizeInBytes = 0;
    ID3D12Device_CreateConstantBufferView(device, &cbv_desc,
            get_cpu_descriptor_handle(&context, heap, 0));

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Buffer.FirstElement = 0;
    srv_desc.Buffer.NumElements = 1;
    srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    ID3D12Device_CreateShaderResourceView(device, NULL, &srv_desc,
            get_cpu_descriptor_handle(&context, heap, 1));

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(device, NULL, &srv_desc,
            get_cpu_descriptor_handle(&context, heap, 2));

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.Format = DXGI_FORMAT_R32_UINT;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.NumElements = 1;
    ID3D12Device_CreateUnorderedAccessView(device, NULL, NULL, &uav_desc,
            get_cpu_descriptor_handle(&context, heap, 3));

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.Format = DXGI_FORMAT_R32_UINT;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice = 0;
    uav_desc.Texture2D.PlaneSlice = 0;
    ID3D12Device_CreateUnorderedAccessView(device, NULL, NULL, &uav_desc,
            get_cpu_descriptor_handle(&context, heap, 3));

    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_null_cbv(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc;
    ID3D12GraphicsCommandList *command_list;
    D3D12_ROOT_PARAMETER root_parameters[2];
    D3D12_DESCRIPTOR_RANGE descriptor_range;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int index;
    HRESULT hr;

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};

#include "shaders/descriptors/headers/null_cbv.h"

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;
    device = context.device;

    descriptor_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    descriptor_range.NumDescriptors = 1;
    descriptor_range.BaseShaderRegister = 1;
    descriptor_range.RegisterSpace = 0;
    descriptor_range.OffsetInDescriptorsFromTableStart = 0;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_range;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[1].Constants.ShaderRegister = 0;
    root_parameters[1].Constants.RegisterSpace = 0;
    root_parameters[1].Constants.Num32BitValues = 1;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    hr = create_root_signature(device, &root_signature_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL,
            &null_cbv_dxbc, NULL);

    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16);

    cbv_desc.BufferLocation = 0;
    cbv_desc.SizeInBytes = 0; /* Size doesn't appear to matter for NULL CBV. */
    ID3D12Device_CreateConstantBufferView(device, &cbv_desc,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));

    for (index = 0; index < 1200; index += 100)
    {
        vkd3d_test_set_context("index %u", index);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 1, 1, &index, 0);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        transition_sub_resource_state(command_list, context.render_target, 0,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, queue, command_list, 0x00000000, 0);

        reset_command_list(command_list, context.allocator);
        transition_sub_resource_state(command_list, context.render_target, 0,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    vkd3d_test_set_context(NULL);

    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_null_srv(void)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    struct uvec4 location;
    ID3D12Device *device;
    unsigned int i, j;

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};

#include "shaders/descriptors/headers/null_srv_sample.h"
#include "shaders/descriptors/headers/null_srv_ld.h"
#include "shaders/descriptors/headers/null_srv_buffer.h"

    static const DXGI_FORMAT formats[] =
    {
        DXGI_FORMAT_R32_FLOAT,
        DXGI_FORMAT_R32_UINT,
        DXGI_FORMAT_R8G8B8A8_UNORM,
    };
    /* component mapping is ignored for NULL SRVs */
    static const unsigned int component_mappings[] =
    {
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1,
                D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1),
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;
    device = context.device;

    context.root_signature = create_texture_root_signature(context.device,
            D3D12_SHADER_VISIBILITY_PIXEL, 4, 0);

    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL, &null_srv_sample_dxbc, NULL);

    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16);

    for (i = 0; i < ARRAY_SIZE(formats); ++i)
    {
        for (j = 0; j < ARRAY_SIZE(component_mappings); ++j)
        {
            vkd3d_test_set_context("format %#x, component mapping %#x", formats[i], component_mappings[j]);

            memset(&srv_desc, 0, sizeof(srv_desc));
            srv_desc.Format = formats[i];
            srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Shader4ComponentMapping = component_mappings[j];
            srv_desc.Texture2D.MipLevels = 1;
            ID3D12Device_CreateShaderResourceView(device, NULL, &srv_desc,
                    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));

            ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

            ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
            ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
            ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
            ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
                    ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
            ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
            ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
            ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

            transition_sub_resource_state(command_list, context.render_target, 0,
                    D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            check_sub_resource_uint(context.render_target, 0, queue, command_list, 0x00000000, 0);

            reset_command_list(command_list, context.allocator);
            transition_sub_resource_state(command_list, context.render_target, 0,
                    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
    }
    vkd3d_test_set_context(NULL);

    ID3D12PipelineState_Release(context.pipeline_state);
    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL, &null_srv_ld_dxbc, NULL);

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(device, NULL, &srv_desc,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    location.x = 10;
    location.y = 20;
    location.z = 0;
    location.w = 0;
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 1, 4, &location, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_sub_resource_state(command_list, context.render_target, 0,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0x00000000, 0);

    /* buffer */
    ID3D12PipelineState_Release(context.pipeline_state);
    context.pipeline_state = create_pipeline_state(context.device,
            context.root_signature, context.render_target_desc.Format, NULL, &null_srv_buffer_dxbc, NULL);
    reset_command_list(command_list, context.allocator);
    transition_sub_resource_state(command_list, context.render_target, 0,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Buffer.FirstElement = 0;
    srv_desc.Buffer.NumElements = 1024;
    srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    ID3D12Device_CreateShaderResourceView(device, NULL, &srv_desc,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));

    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    location.x = 0;
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 1, 4, &location, 0);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_sub_resource_state(command_list, context.render_target, 0,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0x00000000, 0);

    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_null_uav(void)
{
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[1];
    D3D12_ROOT_PARAMETER root_parameters[2];
    const D3D12_SHADER_BYTECODE *current_ps;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    ID3D12DescriptorHeap *uav_heap;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};

#include "shaders/descriptors/headers/null_uav_ld_texture.h"
#include "shaders/descriptors/headers/null_uav_ld_buffer.h"

    static const struct test
    {
        const D3D12_SHADER_BYTECODE *ps;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
        uint32_t location;
    }
    tests[] =
    {
        {&null_uav_ld_texture_dxbc, {DXGI_FORMAT_R32_UINT, D3D12_UAV_DIMENSION_TEXTURE2D}, 0},
        {&null_uav_ld_buffer_dxbc,
                {DXGI_FORMAT_R32_TYPELESS, D3D12_UAV_DIMENSION_BUFFER, .Buffer = {0, 1024, .Flags = D3D12_BUFFER_UAV_FLAG_RAW}},
                0},
        {&null_uav_ld_buffer_dxbc,
                {DXGI_FORMAT_R32_TYPELESS, D3D12_UAV_DIMENSION_BUFFER, .Buffer = {0, 1024, .Flags = D3D12_BUFFER_UAV_FLAG_RAW}},
                1024},
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    descriptor_ranges[0].NumDescriptors = 1;
    descriptor_ranges[0].BaseShaderRegister = 1;
    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[1].Constants.ShaderRegister = 0;
    root_parameters[1].Constants.RegisterSpace = 0;
    root_parameters[1].Constants.Num32BitValues = 1;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;
    root_signature_desc.NumStaticSamplers = 0;
    root_signature_desc.pStaticSamplers = NULL;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    hr = create_root_signature(device, &root_signature_desc, &context.root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    uav_heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    current_ps = NULL;
    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        const struct test *test = &tests[i];

        vkd3d_test_set_context("Test %u", i);

        if (current_ps != test->ps)
        {
            if (context.pipeline_state)
                ID3D12PipelineState_Release(context.pipeline_state);
            current_ps = tests[i].ps;
            context.pipeline_state = create_pipeline_state(context.device,
                    context.root_signature, context.render_target_desc.Format, NULL, current_ps, NULL);
        }

        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(uav_heap);
        ID3D12Device_CreateUnorderedAccessView(device, NULL, NULL, &test->uav_desc, cpu_handle);

        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &uav_heap);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(uav_heap));
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstant(command_list, 1, test->location, 0);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        transition_sub_resource_state(command_list, context.render_target, 0,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        check_sub_resource_uint(context.render_target, 0, queue, command_list, 0x00000000, 0);

        reset_command_list(command_list, context.allocator);
        transition_sub_resource_state(command_list, context.render_target, 0,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    vkd3d_test_set_context(NULL);

    ID3D12DescriptorHeap_Release(uav_heap);
    destroy_test_context(&context);
}

void test_null_rtv(void)
{
    ID3D12GraphicsCommandList *command_list;
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D12_HEAP_PROPERTIES heap_properties;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;

    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};

    memset(&desc, 0, sizeof(desc));
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtv_desc.Texture2D.MipSlice = 0;
    rtv_desc.Texture2D.PlaneSlice = 0;
    ID3D12Device_CreateRenderTargetView(device, context.render_target, &rtv_desc, context.rtv);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, true, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);

    ID3D12Device_CreateRenderTargetView(device, NULL, &rtv_desc, context.rtv);
    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, true, NULL);

    /* Attempting to clear a NULL RTV crashes on native D3D12, so try to draw something instead */
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    transition_sub_resource_state(command_list, context.render_target, 0,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(context.render_target, 0, queue, command_list, 0xffffffff, 0);

    destroy_test_context(&context);
}

void test_cpu_descriptors_lifetime(void)
{
    static const float blue[] = {0.0f, 0.0f, 1.0f, 1.0f};
    static const float red[] = {1.0f, 0.0f, 0.0f, 1.0f};
    ID3D12Resource *red_resource, *blue_resource;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle;
    D3D12_HEAP_PROPERTIES heap_properties;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12DescriptorHeap *rtv_heap;
    D3D12_CLEAR_VALUE clear_value;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    HRESULT hr;

    if (!init_test_context(&context, NULL))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    rtv_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    rtv_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = 32;
    resource_desc.Height = 32;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    clear_value.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clear_value.Color[0] = 1.0f;
    clear_value.Color[1] = 0.0f;
    clear_value.Color[2] = 0.0f;
    clear_value.Color[3] = 1.0f;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)&red_resource);
    ok(hr == S_OK, "Failed to create texture, hr %#x.\n", hr);
    clear_value.Color[0] = 0.0f;
    clear_value.Color[1] = 0.0f;
    clear_value.Color[2] = 1.0f;
    clear_value.Color[3] = 1.0f;
    hr = ID3D12Device_CreateCommittedResource(device,
            &heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
            &IID_ID3D12Resource, (void **)&blue_resource);
    ok(hr == S_OK, "Failed to create texture, hr %#x.\n", hr);

    ID3D12Device_CreateRenderTargetView(device, red_resource, NULL, rtv_handle);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv_handle, red, 0, NULL);
    /* Destroy the previous RTV and create a new one in its place. */
    ID3D12Device_CreateRenderTargetView(device, blue_resource, NULL, rtv_handle);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv_handle, blue, 0, NULL);

    /* Destroy the CPU descriptor heap. */
    ID3D12DescriptorHeap_Release(rtv_heap);

    transition_resource_state(command_list, red_resource,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, blue_resource,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(red_resource, 0, queue, command_list, 0xff0000ff, 0);
    reset_command_list(command_list, context.allocator);
    check_sub_resource_uint(blue_resource, 0, queue, command_list, 0xffff0000, 0);

    rtv_heap = create_cpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    rtv_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(rtv_heap);

    /* Try again with OMSetRenderTargets(). */
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, red_resource,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    transition_resource_state(command_list, blue_resource,
            D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    ID3D12Device_CreateRenderTargetView(device, red_resource, NULL, rtv_handle);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv_handle, red, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &rtv_handle, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    /* Destroy the previous RTV and create a new one in its place. */
    ID3D12Device_CreateRenderTargetView(device, blue_resource, NULL, rtv_handle);
    ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, rtv_handle, blue, 0, NULL);

    ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &rtv_handle, false, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
    ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

    /* Destroy the previous RTV and create a new one in its place. */
    ID3D12Device_CreateRenderTargetView(device, red_resource, NULL, rtv_handle);

    /* Destroy the CPU descriptor heap. */
    ID3D12DescriptorHeap_Release(rtv_heap);

    transition_resource_state(command_list, red_resource,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_resource_state(command_list, blue_resource,
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    check_sub_resource_uint(red_resource, 0, queue, command_list, 0xff00ff00, 0);
    reset_command_list(command_list, context.allocator);
    check_sub_resource_uint(blue_resource, 0, queue, command_list, 0xff00ff00, 0);

    ID3D12Resource_Release(blue_resource);
    ID3D12Resource_Release(red_resource);
    destroy_test_context(&context);
}

void test_create_sampler(void)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    unsigned int sampler_increment_size;
    D3D12_SAMPLER_DESC sampler_desc;
    ID3D12DescriptorHeap *heap;
    ID3D12Device *device;
    unsigned int i;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    sampler_increment_size = ID3D12Device_GetDescriptorHandleIncrementSize(device,
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    trace("Sampler descriptor handle increment size: %u.\n", sampler_increment_size);
    ok(sampler_increment_size, "Got unexpected increment size %#x.\n", sampler_increment_size);

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    heap_desc.NumDescriptors = 16;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create descriptor heap, hr %#x.\n", hr);

    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    ID3D12Device_CreateSampler(device, &sampler_desc, cpu_handle);

    cpu_handle.ptr += sampler_increment_size;
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    for (i = 1; i < heap_desc.NumDescriptors; ++i)
    {
        ID3D12Device_CreateSampler(device, &sampler_desc, cpu_handle);
        cpu_handle.ptr += sampler_increment_size;
    }

    trace("MinMaxFiltering: %#x.\n", is_min_max_filtering_supported(device));
    if (is_min_max_filtering_supported(device))
    {
        cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        sampler_desc.Filter = D3D12_FILTER_MINIMUM_MIN_MAG_MIP_POINT;
        ID3D12Device_CreateSampler(device, &sampler_desc, cpu_handle);

        cpu_handle.ptr += sampler_increment_size;
        sampler_desc.Filter = D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_POINT;
        ID3D12Device_CreateSampler(device, &sampler_desc, cpu_handle);
    }

    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    sampler_desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS;
    ID3D12Device_CreateSampler(device, &sampler_desc, cpu_handle);

    refcount = ID3D12DescriptorHeap_Release(heap);
    ok(!refcount, "ID3D12DescriptorHeap has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_create_sampler2(void)
{
    /* This is just a smoke test that calling CreateSampler2() does not blow up.
     * Also, note that there is no CreateSampler1(). */
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    D3D12_SAMPLER_DESC2 sampler_desc;
    ID3D12DescriptorHeap *heap;
    ID3D12Device11 *device11;
    ID3D12Device *device;
    ULONG refcount;
    HRESULT hr;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    if (FAILED(ID3D12Device_QueryInterface(device, &IID_ID3D12Device11, (void**)&device11)))
    {
        skip("ID3D12Device11 not supported.\n");
        ID3D12Device_Release(device);
        return;
    }

    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    heap_desc.NumDescriptors = 16;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void **)&heap);
    ok(SUCCEEDED(hr), "Failed to create descriptor heap, hr %#x.\n", hr);

    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    ID3D12Device11_CreateSampler2(device11, &sampler_desc, cpu_handle);

    ID3D12Device11_Release(device11);

    refcount = ID3D12DescriptorHeap_Release(heap);
    ok(!refcount, "ID3D12DescriptorHeap has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_create_unordered_access_view(void)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    ID3D12Resource *buffer, *texture;
    unsigned int descriptor_size;
    ID3D12DescriptorHeap *heap;
    ID3D12Device *device;
    ULONG refcount;

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(device,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    trace("CBV/SRV/UAV descriptor size: %u.\n", descriptor_size);
    ok(descriptor_size, "Got unexpected descriptor size %#x.\n", descriptor_size);

    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16);

    buffer = create_default_buffer(device, 64 * sizeof(float),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.NumElements = 64;
    uav_desc.Buffer.StructureByteStride = 0;
    uav_desc.Buffer.CounterOffsetInBytes = 0;
    uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    ID3D12Device_CreateUnorderedAccessView(device, buffer, NULL, &uav_desc, cpu_handle);

    cpu_handle.ptr += descriptor_size;

    /* DXGI_FORMAT_R32_UINT view for DXGI_FORMAT_R8G8B8A8_TYPELESS resources. */
    texture = create_default_texture(device, 8, 8, DXGI_FORMAT_R8G8B8A8_TYPELESS,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    uav_desc.Format = DXGI_FORMAT_R32_UINT;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice = 0;
    uav_desc.Texture2D.PlaneSlice = 0;
    ID3D12Device_CreateUnorderedAccessView(device, texture, NULL, &uav_desc, cpu_handle);

    ID3D12Resource_Release(buffer);
    ID3D12Resource_Release(texture);
    refcount = ID3D12DescriptorHeap_Release(heap);
    ok(!refcount, "ID3D12DescriptorHeap has %u references left.\n", (unsigned int)refcount);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}

void test_sampler_border_color(void)
{
    ID3D12DescriptorHeap *heap, *sampler_heap, *heaps[2];
    D3D12_STATIC_SAMPLER_DESC static_sampler_desc;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_range[2];
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    ID3D12RootSignature *root_signature;
    ID3D12PipelineState *pipeline_state;
    D3D12_SAMPLER_DESC sampler_desc;
    struct test_context_desc desc;
    struct resource_readback rb;
    struct test_context context;
    ID3D12CommandQueue *queue;
    ID3D12Resource *texture;
    ID3D12Device *device;
    unsigned int i;
    HRESULT hr;

#include "shaders/descriptors/headers/sampler_border_color.h"

    static const float red[] = {1.0f, 0.0f, 0.0f, 1.0f};
    static const struct
    {
        bool static_sampler;
        unsigned int expected_color;
        float border_color[4];
        D3D12_STATIC_BORDER_COLOR static_border_color;
    }
    tests[] =
    {
        {false, 0x00000000u, {0.0f, 0.0f, 0.0f, 0.0f}},
        {false, 0xff000000u, {0.0f, 0.0f, 0.0f, 1.0f}},
        {false, 0xffffffffu, {1.0f, 1.0f, 1.0f, 1.0f}},
        {false, 0xccb3804du, {0.3f, 0.5f, 0.7f, 0.8f}},
        {true,  0x00000000u, {0.0f, 0.0f, 0.0f, 0.0f}, D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK},
        {true,  0xff000000u, {0.0f, 0.0f, 0.0f, 1.0f}, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK},
        {true,  0xffffffffu, {1.0f, 1.0f, 1.0f, 1.0f}, D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE},
    };

    memset(&desc, 0, sizeof(desc));
    desc.rt_width = 640;
    desc.rt_height = 480;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    descriptor_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_range[0].NumDescriptors = 1;
    descriptor_range[0].BaseShaderRegister = 0;
    descriptor_range[0].RegisterSpace = 0;
    descriptor_range[0].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[0].DescriptorTable.pDescriptorRanges = &descriptor_range[0];
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    descriptor_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    descriptor_range[1].NumDescriptors = 1;
    descriptor_range[1].BaseShaderRegister = 0;
    descriptor_range[1].RegisterSpace = 0;
    descriptor_range[1].OffsetInDescriptorsFromTableStart = 0;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    root_parameters[1].DescriptorTable.pDescriptorRanges = &descriptor_range[1];
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);

    sampler_heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1);

    {
        D3D12_SUBRESOURCE_DATA sub;
        sub.pData = red;
        sub.RowPitch = 4;
        sub.SlicePitch = 4;
        texture = create_default_texture2d(device, 1, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        upload_texture_data(texture, &sub, 1, queue, command_list);
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, texture,
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        ID3D12Device_CreateShaderResourceView(device, texture, NULL, cpu_handle);
    }

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        unsigned int color;
        vkd3d_test_set_context("Test %u", i);

        memset(&root_signature_desc, 0, sizeof(root_signature_desc));
        root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
        root_signature_desc.pParameters = root_parameters;

        if (tests[i].static_sampler)
        {
            root_signature_desc.NumParameters -= 1;
            root_signature_desc.NumStaticSamplers = 1;
            root_signature_desc.pStaticSamplers = &static_sampler_desc;

            memset(&static_sampler_desc, 0, sizeof(static_sampler_desc));
            static_sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            static_sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            static_sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            static_sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            static_sampler_desc.BorderColor = tests[i].static_border_color;
            static_sampler_desc.ShaderRegister = 0;
            static_sampler_desc.RegisterSpace = 0;
            static_sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        else
        {
            memset(&sampler_desc, 0, sizeof(sampler_desc));
            sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            memcpy(sampler_desc.BorderColor, tests[i].border_color, sizeof(sampler_desc.BorderColor));
            ID3D12Device_CreateSampler(device, &sampler_desc, get_cpu_sampler_handle(&context, sampler_heap, 0));
        }

        hr = create_root_signature(device, &root_signature_desc, &root_signature);
        ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

        pipeline_state = create_pipeline_state(device, root_signature,
                context.render_target_desc.Format, NULL, &sampler_border_color_dxbc, NULL);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, red, 0, NULL);

        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline_state);
        heaps[0] = heap;
        heaps[1] = sampler_heap;
        ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, ARRAY_SIZE(heaps), heaps);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0, gpu_handle);
        if (!tests[i].static_sampler)
        {
            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 1,
                    get_gpu_sampler_handle(&context, sampler_heap, 0));
        }
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_texture_readback_with_command_list(context.render_target, 0, &rb, queue, command_list);

        color = get_readback_uint(&rb, 0, 0, 0);
        ok(compare_color(color, tests[i].expected_color, 1),
                "Got color 0x%08x, expected 0x%08x.\n",
                color, tests[i].expected_color);

        release_resource_readback(&rb);

        ID3D12RootSignature_Release(root_signature);
        ID3D12PipelineState_Release(pipeline_state);

        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    vkd3d_test_set_context(NULL);

    ID3D12Resource_Release(texture);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12DescriptorHeap_Release(sampler_heap);
    destroy_test_context(&context);
}

static void test_typed_buffers_many_objects(bool use_dxil)
{
    ID3D12DescriptorHeap *cpu_heap, *gpu_heap;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    D3D12_ROOT_PARAMETER root_parameters[2];

    ID3D12Resource *output_buffer, *input_buffer;
    struct resource_readback rb;

    unsigned int i, j, descriptor_size;
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE host_handle;
    D3D12_CPU_DESCRIPTOR_HANDLE visible_handle;
    struct test_context context;
    ID3D12CommandQueue *queue;
    HRESULT hr;

#include "shaders/descriptors/headers/typed_buffer_many_objects.h"

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
    root_parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;

    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].Constants.ShaderRegister = 0;
    root_parameters[1].Constants.RegisterSpace = 0;
    root_parameters[1].Constants.Num32BitValues = 1;

    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;
    descriptor_ranges[0].NumDescriptors = UINT_MAX;
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    descriptor_ranges[1].RegisterSpace = 0;
    descriptor_ranges[1].BaseShaderRegister = 0;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 500000;
    descriptor_ranges[1].NumDescriptors = UINT_MAX;
    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

    hr = create_root_signature(context.device, &root_signature_desc, &context.root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    output_buffer = create_default_buffer(context.device, 64 * 1024 * 1024, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    input_buffer = create_default_buffer(context.device, 64 * 1024 * 1024, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

    context.pipeline_state = create_compute_pipeline_state(context.device,
        context.root_signature, use_dxil ? typed_buffer_many_objects_dxil : typed_buffer_many_objects_dxbc);

    cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1000000);
    gpu_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1000000);
    host_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap);
    visible_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu_heap);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    /* This will likely completely exhaust NV drivers memory unless typed offset buffer is implemented properly. */
    for (j = 10; j >= 2; j--)
    {
        for (i = 0; i < 1000000; i++)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE h = host_handle;
            D3D12_UNORDERED_ACCESS_VIEW_DESC view;
            view.Format = DXGI_FORMAT_R32_UINT;
            view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            view.Buffer.StructureByteStride = 0;
            view.Buffer.CounterOffsetInBytes = 0;
            view.Buffer.FirstElement = 4 * i;
            /* Final iteration is j == 2. First descriptor covers entire view so we can clear the buffer to something non-zero in the loop below. */
            view.Buffer.NumElements = i == 0 ? (16 * 1024 * 1024) : j;
            h.ptr += i * descriptor_size;
            ID3D12Device_CreateUnorderedAccessView(context.device, output_buffer, NULL, &view, h);
        }

        for (i = 500000; i < 1000000; i++)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE h = host_handle;
            D3D12_SHADER_RESOURCE_VIEW_DESC view;
            view.Format = DXGI_FORMAT_R32_UINT;
            view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            view.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            view.Buffer.StructureByteStride = 0;
            view.Buffer.FirstElement = 4 * (i - 500000);
            view.Buffer.NumElements = i == 500000 ? (16 * 1024 * 1024) : j;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            h.ptr += i * descriptor_size;
            ID3D12Device_CreateShaderResourceView(context.device, input_buffer, &view, h);
        }
    }

    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = host_handle;
        h.ptr += descriptor_size;
        ID3D12Device_CopyDescriptorsSimple(context.device, 16, visible_handle, h, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        /* Tests that we handle the case where only the offset changes. */
        ID3D12Device_CopyDescriptorsSimple(context.device, 1000000, visible_handle, host_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &gpu_heap);

    /* Check that UAV clear works with typed offsets. */
    for (i = 0; i < 1024; i++)
    {
        const UINT clear_value[4] = { i + 1, i + 2, i + 3, i + 4 };
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc;
        D3D12_RESOURCE_BARRIER barrier;

        cpu_desc = host_handle;
        gpu_desc = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(gpu_heap);
        cpu_desc.ptr += i * descriptor_size;
        gpu_desc.ptr += i * descriptor_size;

        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(command_list, gpu_desc, cpu_desc,
            output_buffer, clear_value, 0, NULL);

        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.UAV.pResource = output_buffer;
        ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barrier);
    }

    transition_resource_state(command_list, output_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyBufferRegion(command_list, input_buffer, 0, output_buffer, 0, 64 * 1024 * 1024);
    transition_resource_state(command_list, output_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition_resource_state(command_list, input_buffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &gpu_heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(gpu_heap));

    for (i = 0; i < 500000; i += 50000)
    {
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(command_list, 1, i, 0);
        ID3D12GraphicsCommandList_Dispatch(command_list, 50000, 1, 1);
    }

    transition_resource_state(command_list, output_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffer, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);

    /* Don't bother testing all 1M descriptors, after 64k we should be pretty sure that we got it right. */
    for (i = 0; i < 64 * 1024; i++)
    {
        UINT value, reference;
        value = get_readback_uint(&rb, 4 * i, 0, 0);
        reference = 0x3f;
        ok(value == reference, "Readback value for iteration #%u, elem %u is: %u (ref: %u)\n", i, 0, value, reference);

        value = get_readback_uint(&rb, 4 * i + 1, 0, 0);
        reference = i < 1024 ? (201 + i) : 201;
        ok(value == reference, "Readback value for iteration #%u, elem %u is: %u (ref: %u)\n", i, 1, value, reference);

        for (j = 2; j < 4; j++)
        {
            value = get_readback_uint(&rb, 4 * i + j, 0, 0);
            reference = j == 2 && i == 0 ? 401 : 1;
            ok(value == reference, "Readback value for iteration #%u, elem %u is: %u (ref: %u)\n", i, j, value, reference);
        }
    }

    release_resource_readback(&rb);
    reset_command_list(command_list, context.allocator);

    ID3D12Resource_Release(output_buffer);
    ID3D12Resource_Release(input_buffer);
    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12DescriptorHeap_Release(gpu_heap);
    destroy_test_context(&context);
}

void test_typed_buffers_many_objects_dxbc(void)
{
    test_typed_buffers_many_objects(false);
}

void test_typed_buffers_many_objects_dxil(void)
{
    test_typed_buffers_many_objects(true);
}

void test_view_min_lod(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC view_desc;
    ID3D12GraphicsCommandList *command_list;
    const D3D12_SHADER_BYTECODE *ps = NULL;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    ID3D12PipelineState *pso = NULL;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    ID3D12Resource *texture;
    unsigned int offset;
    unsigned int i;
    HRESULT hr;

#include "shaders/descriptors/headers/view_min_lod_load.h"
#include "shaders/descriptors/headers/view_min_lod_sample.h"

    static const float red[] = {1.0f, 0.0f, 0.0f, 1.0f};
    unsigned int texture_data[8 * 8 + 4 * 4 + 2 * 2 + 1 * 1];
    D3D12_SUBRESOURCE_DATA resource_data[4];
    static const struct
    {
        const D3D12_SHADER_BYTECODE *ps;
        int most_detailed_mip;
        int mip_count;
        float test_lod;
        float min_lod;
        unsigned int expected_color;
        bool is_todo;
    }
    tests[] =
    {
        {&view_min_lod_load_dxbc, 0, -1, -1.0f, 0.0f, 0x00000000},
        {&view_min_lod_load_dxbc, 0, -1,  0.0f, 0.0f, 0x0f0f0f0f},
        {&view_min_lod_load_dxbc, 0, -1,  1.0f, 0.0f, 0xffffffff},
        {&view_min_lod_load_dxbc, 0, -1,  2.0f, 0.0f, 0x0f0f0f0f},
        {&view_min_lod_load_dxbc, 0, -1,  3.0f, 0.0f, 0xffffffff},

        {&view_min_lod_load_dxbc, 0, -1, -1.0f, 1.0f, 0x00000000},
        {&view_min_lod_load_dxbc, 0, -1,  0.0f, 1.0f, 0x00000000},
        {&view_min_lod_load_dxbc, 0, -1,  1.0f, 1.0f, 0xffffffff},
        {&view_min_lod_load_dxbc, 0, -1,  2.0f, 1.0f, 0x0f0f0f0f},
        {&view_min_lod_load_dxbc, 0, -1,  3.0f, 1.0f, 0xffffffff},

        {&view_min_lod_load_dxbc, 1, -1, -1.0f, 1.0f, 0x00000000},
        {&view_min_lod_load_dxbc, 1, -1,  0.0f, 1.0f, 0xffffffff},
        {&view_min_lod_load_dxbc, 1, -1,  1.0f, 1.0f, 0x0f0f0f0f},
        {&view_min_lod_load_dxbc, 1, -1,  2.0f, 1.0f, 0xffffffff},
        {&view_min_lod_load_dxbc, 1, -1,  3.0f, 1.0f, 0x00000000},

        {&view_min_lod_load_dxbc, 1, -1, -1.0f, 9.0f, 0x00000000},
        {&view_min_lod_load_dxbc, 1, -1,  0.0f, 9.0f, 0x00000000},
        {&view_min_lod_load_dxbc, 1, -1,  1.0f, 9.0f, 0x00000000},
        {&view_min_lod_load_dxbc, 1, -1,  2.0f, 9.0f, 0x00000000},
        {&view_min_lod_load_dxbc, 1, -1,  3.0f, 9.0f, 0x00000000},

        /* floor(minLOD) behavior for integer fetch. */
        {&view_min_lod_load_dxbc, 1, -1, 0.0f, 1.9f, 0xffffffff},
        {&view_min_lod_load_dxbc, 1, -1, 0.0f, 2.0f, 0x00000000},
        {&view_min_lod_load_dxbc, 1, -1, 1.0f, 2.9f, 0x0f0f0f0f},
        {&view_min_lod_load_dxbc, 1, -1, 1.0f, 3.0f, 0x00000000},

        /* Out-of-bounds MinLODClamp behaviour */
        {&view_min_lod_load_dxbc, 0, 1, 0.0f, 0.0f, 0x0f0f0f0f},
        {&view_min_lod_load_dxbc, 0, 1, 0.0f, 0.1f, 0x00000000},
        {&view_min_lod_load_dxbc, 0, 1, 0.0f, 1.0f / 256.0f, 0x00000000},
        {&view_min_lod_load_dxbc, 0, 1, 0.0f, 1.0f / 512.0f, 0x00000000},
        {&view_min_lod_load_dxbc, 0, 1, 0.0f, 1.5f / 1024.0f, 0x0f0f0f0f},
        {&view_min_lod_load_dxbc, 0, 1, 0.0f, 1.0f / 1024.0f, 0x0f0f0f0f},

        {&view_min_lod_load_dxbc, 0, 2, 1.0f, 1.0f, 0xffffffff},
        {&view_min_lod_load_dxbc, 0, 2, 1.0f, 1.1f, 0x00000000},
        {&view_min_lod_load_dxbc, 0, 2, 1.0f, 1.0f + 1.0f / 256.0f, 0x00000000},
        {&view_min_lod_load_dxbc, 0, 2, 1.0f, 1.0f + 1.0f / 512.0f, 0x00000000},
        {&view_min_lod_load_dxbc, 0, 2, 1.0f, 1.0f + 1.0f / 1024.0f, 0xffffffff},

        {&view_min_lod_sample_dxbc, 0, -1, -1.0f, 0.0f, 0x0f0f0f0f},
        {&view_min_lod_sample_dxbc, 0, -1,  0.0f, 0.0f, 0x0f0f0f0f},
        {&view_min_lod_sample_dxbc, 0, -1,  1.0f, 0.0f, 0xffffffff},
        {&view_min_lod_sample_dxbc, 0, -1,  2.0f, 0.0f, 0x0f0f0f0f},
        {&view_min_lod_sample_dxbc, 0, -1,  3.0f, 0.0f, 0xffffffff},

        {&view_min_lod_sample_dxbc, 0, -1, -1.0f, 1.0f, 0xffffffff},
        {&view_min_lod_sample_dxbc, 0, -1,  0.0f, 1.0f, 0xffffffff},
        {&view_min_lod_sample_dxbc, 0, -1,  1.0f, 1.0f, 0xffffffff},
        {&view_min_lod_sample_dxbc, 0, -1,  2.0f, 1.0f, 0x0f0f0f0f},
        {&view_min_lod_sample_dxbc, 0, -1,  3.0f, 1.0f, 0xffffffff},

        {&view_min_lod_sample_dxbc, 1, -1, -1.0f, 1.0f, 0xffffffff},
        {&view_min_lod_sample_dxbc, 1, -1,  0.0f, 1.0f, 0xffffffff},
        {&view_min_lod_sample_dxbc, 1, -1,  1.0f, 1.0f, 0x0f0f0f0f},
        {&view_min_lod_sample_dxbc, 1, -1,  2.0f, 1.0f, 0xffffffff},
        {&view_min_lod_sample_dxbc, 1, -1,  3.0f, 1.0f, 0xffffffff},
        {&view_min_lod_sample_dxbc, 1, -1,  4.0f, 1.0f, 0xffffffff},

        {&view_min_lod_sample_dxbc, 1, -1, -1.0f, 9.0f, 0x00000000},
        {&view_min_lod_sample_dxbc, 1, -1,  0.0f, 9.0f, 0x00000000},
        {&view_min_lod_sample_dxbc, 1, -1,  1.0f, 9.0f, 0x00000000},
        {&view_min_lod_sample_dxbc, 1, -1,  2.0f, 9.0f, 0x00000000},
        {&view_min_lod_sample_dxbc, 1, -1,  3.0f, 9.0f, 0x00000000},

        /* Tests rounding behavior for POINT mip filter. Nearest mip level is selected after clamp on AMD and NV,
         * but not Intel, oddly enough. */
        {&view_min_lod_sample_dxbc, 1, -1, 0.25f, 1.00f, 0xffffffff},
        {&view_min_lod_sample_dxbc, 1, -1, 0.25f, 1.25f, 0xffffffff},
        {&view_min_lod_sample_dxbc, 1, -1, 0.25f, 2.00f, 0x0f0f0f0f},
        {&view_min_lod_sample_dxbc, 1, -1, -0.25f, 1.00f, 0xffffffff},
        {&view_min_lod_sample_dxbc, 1, -1, -0.25f, 1.25f, 0xffffffff},
        {&view_min_lod_sample_dxbc, 1, -1, -0.25f, 2.00f, 0x0f0f0f0f},
        /* Intel HW fails these tests on native, D3D11 functional spec is not well defined here. */
        /*{&ps_view_min_lod_sample, 1, -1, 0.25f, 1.75f, 0x0f0f0f0f},*/
        /*{&ps_view_min_lod_sample, 1, -1, -0.25f, 1.75f, 0x0f0f0f0f},*/
    };

    /* Alternate mip colors */
    offset = 0;
    for (i = 0; i < 4; i++)
    {
        const unsigned int size = 8u >> i;

        resource_data[i] = (D3D12_SUBRESOURCE_DATA) {&texture_data[offset], sizeof(unsigned int) * size};
        memset(&texture_data[offset], (i % 2 == 0) ? 0x0F : 0xFF, sizeof(unsigned int) * size * size);
        offset += size * size;
    }

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    command_list = context.list;
    queue = context.queue;

    context.root_signature = create_texture_root_signature(context.device,
            D3D12_SHADER_VISIBILITY_PIXEL, 4, 0);

    init_pipeline_state_desc(&pso_desc, context.root_signature,
            context.render_target_desc.Format, NULL, NULL, NULL);

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);

    texture = create_default_texture2d(context.device,
            8, 8, 1, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    upload_texture_data(texture, resource_data, 4, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    for (i = 0; i < ARRAY_SIZE(tests); ++i)
    {
        vkd3d_test_set_context("Test %u", i);
        
        if (ps != tests[i].ps)
        {
            if (pso)
                ID3D12PipelineState_Release(pso);

            ps = tests[i].ps;
            pso_desc.PS = *tests[i].ps;
            hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc,
                    &IID_ID3D12PipelineState, (void **)&pso);
            ok(hr == S_OK, "Failed to create graphics pipeline state, hr %#x.\n", hr);
        }

        view_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view_desc.Texture2D.MostDetailedMip = tests[i].most_detailed_mip;
        view_desc.Texture2D.MipLevels = tests[i].mip_count;
        view_desc.Texture2D.PlaneSlice = 0;
        view_desc.Texture2D.ResourceMinLODClamp = tests[i].min_lod;

        ID3D12Device_CreateShaderResourceView(context.device, texture, &view_desc,
                ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, red, 0, NULL);

        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, pso);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0, gpu_handle);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, 1, 1, &tests[i].test_lod, 0);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        todo_if(tests[i].is_todo) check_sub_resource_uint(context.render_target, 0, queue, command_list, tests[i].expected_color, 0);

        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, context.render_target,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    vkd3d_test_set_context(NULL);

    ID3D12PipelineState_Release(pso);
    ID3D12Resource_Release(texture);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_typed_srv_uav_cast(void)
{
    /* Test rules for CastFullyTypedFormat.
     * This is more of a meta-test. It's not supposed to generate any real results, but we can observe outputs
     * from D3D12 validation layers and VK validation layers to sanity check our assumptions.
     * Should also serve as more clear documentation on actual behavior. */
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv;
    struct test_context context;
    ID3D12Resource *uav_texture;
    ID3D12DescriptorHeap *heap;
    ID3D12Resource *texture;
    VKD3D_UNUSED HRESULT hr;
    unsigned int i;

    struct test
    {
        DXGI_FORMAT image;
        DXGI_FORMAT view;
        bool valid_srv;
        bool valid_uav;
    };

    /* Rules:
     * FLOAT cannot be cast to non-FLOAT and vice versa.
     * UNORM cannot be cast to SNORM and vice versa.
     * Everything else is fair game as long as it's within same family.
     * UAVs have some weird legacy rules which were probably inherited from D3D11 ... */
    static const struct test tests[] =
    {
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, true, true },
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, true, false },
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SINT, true, true },
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UINT, true, true },
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SNORM, false, false },

        { DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_UNORM, false, false },
        { DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, false, false },
        { DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_SINT, true, true },
        { DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_UINT, true, true },
        { DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_SNORM, true, true },

        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_UNORM, true, true },
        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, true, false },
        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT, true, true },
        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_UINT, true, true },
        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SNORM, true, true },

        { DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM, true, true },
        { DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, true, false },
        { DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_SINT, true, true },
        { DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UINT, true, true },
        { DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_SNORM, true, true },

        /* FLOAT cannot cast with UINT or SINT. */
        { DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT, false, false },
        { DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_SINT, false, false },
        { DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_FLOAT, false, false },
        { DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32_FLOAT, false, false },
        { DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_UINT, false, false },
        { DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_SINT, false, false },
        { DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_FLOAT, false, false },
        { DXGI_FORMAT_R16_SINT, DXGI_FORMAT_R16_FLOAT, false, false },

        /* 5.3.9.5 from D3D11 functional spec. 32-bit typeless formats
         * can be viewed as R32{U,I,F}. The D3D12 validation runtime appears to be buggy
         * and also allows fully typed views even if bits per component don't match.
         * This feature is derived from legacy D3D11 jank, so assume the validation layers are
         * just buggy. */

        { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R32_UINT, false, true },
        { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R32_SINT, false, true },
        { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R32_FLOAT, false, true },
        { DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R32_UINT, false, true },
        { DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R32_SINT, false, true },
        { DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R32_FLOAT, false, true },

        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32_FLOAT, false, false },
        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R32_FLOAT, false, false },
        { DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R32_FLOAT, false, false },
        { DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R32_FLOAT, false, false },
        { DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R32_UINT, false, false },
        { DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R32_SINT, false, false },

        /* D3D12 validation does not complain about these, but it should according to D3D11 functional spec.
         * No docs for D3D12 say otherwise.
         * These tests can trip assertions in drivers since we will not emit MUTABLE at all
         * for some of these tests. */
#if 0
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32_UINT, false, true },
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32_SINT, false, true },
        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R32_UINT, false, true },
        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R32_SINT, false, true },
        { DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R32_UINT, false, true },
        { DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R32_SINT, false, true },
        { DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R32_UINT, false, true },
        { DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R32_SINT, false, true },
        { DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R32_FLOAT, false, true },
#endif
    };

    if (!init_compute_test_context(&context))
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3))) ||
            !options3.CastingFullyTypedFormatSupported)
    {
        skip("CastingFullyTypedFormat is not supported, skipping ...\n");
        destroy_test_context(&context);
        return;
    }

    memset(&srv, 0, sizeof(srv));
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;

    memset(&uav, 0, sizeof(uav));
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

#define PURGE() do { \
    if (uav_texture) ID3D12Resource_Release(uav_texture); \
    if (texture) ID3D12Resource_Release(texture); \
    if (heap) ID3D12DescriptorHeap_Release(heap); \
    uav_texture = NULL; \
    texture = NULL; \
    heap = NULL;     \
} while(0)

#define PURGE_CONTEXT() do { \
    PURGE(); \
    destroy_test_context(&context); \
    context.device = NULL; \
} while(0)

#define BEGIN_CONTEXT() do { \
    if (!context.device) \
        init_compute_test_context(&context); \
} while(0)

#define BEGIN() do { \
    BEGIN_CONTEXT(); \
    uav_texture = create_default_texture2d(context.device, 1024, 1024, 1, 1, tests[i].image, \
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST); \
    texture = create_default_texture2d(context.device, 1024, 1024, 1, 1, tests[i].image, \
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST); \
    heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1); \
} while(0)

    /* When enabled, only intended to pass on real runtime.
     * The native runtime actually performs validation and will trip device lost if a mistake is made.
     * We don't validate this in vkd3d-proton. */
#define VERIFY_FAILURE_CASES 0

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test %u", i);

        BEGIN();

        srv.Format = tests[i].view;
        uav.Format = tests[i].view;

        if (tests[i].valid_srv || VERIFY_FAILURE_CASES)
        {
            ID3D12Device_CreateShaderResourceView(context.device, texture, &srv,
                    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));

            if (!tests[i].valid_srv && VERIFY_FAILURE_CASES)
            {
                hr = ID3D12Device_GetDeviceRemovedReason(context.device);

                if ((tests[i].image == DXGI_FORMAT_R8G8B8A8_UNORM && tests[i].view == DXGI_FORMAT_R8G8B8A8_SNORM) ||
                    (tests[i].image == DXGI_FORMAT_R8G8B8A8_SNORM && tests[i].view == DXGI_FORMAT_R8G8B8A8_UNORM) ||
                    (tests[i].image == DXGI_FORMAT_R8G8B8A8_SNORM && tests[i].view == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB))
                {
                    /* It is a documented bug that the runtime forgot to validate UNORM <-> SNORM cast, so trip errors here. */
                    hr = E_FAIL;
                }

                ok(FAILED(hr), "Unexpected hr %#x\n", hr);
                PURGE_CONTEXT();
                BEGIN();
            }

            ID3D12Device_CreateShaderResourceView(context.device, uav_texture, &srv,
                    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));

            if (!tests[i].valid_srv && VERIFY_FAILURE_CASES)
            {
                hr = ID3D12Device_GetDeviceRemovedReason(context.device);

                if ((tests[i].image == DXGI_FORMAT_R8G8B8A8_UNORM && tests[i].view == DXGI_FORMAT_R8G8B8A8_SNORM) ||
                    (tests[i].image == DXGI_FORMAT_R8G8B8A8_SNORM && tests[i].view == DXGI_FORMAT_R8G8B8A8_UNORM) ||
                    (tests[i].image == DXGI_FORMAT_R8G8B8A8_SNORM && tests[i].view == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB))
                {
                    /* It is a documented bug that the runtime forgot to validate UNORM <-> SNORM cast, so trip errors here. */
                    hr = E_FAIL;
                }

                ok(FAILED(hr), "Unexpected hr %#x\n", hr);
                PURGE_CONTEXT();
                BEGIN();
            }
        }

        if (tests[i].valid_uav || VERIFY_FAILURE_CASES)
        {
            ID3D12Device_CreateUnorderedAccessView(context.device, uav_texture, NULL, &uav,
                    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));

            if (!tests[i].valid_uav && VERIFY_FAILURE_CASES)
            {
                hr = ID3D12Device_GetDeviceRemovedReason(context.device);

                if ((tests[i].image == DXGI_FORMAT_R8G8B8A8_UNORM && tests[i].view == DXGI_FORMAT_R8G8B8A8_SNORM) ||
                    (tests[i].image == DXGI_FORMAT_R8G8B8A8_SNORM && tests[i].view == DXGI_FORMAT_R8G8B8A8_UNORM) ||
                    (tests[i].image == DXGI_FORMAT_R8G8B8A8_SNORM && tests[i].view == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB))
                {
                    /* It is a documented bug that the runtime forgot to validate UNORM <-> SNORM cast, so trip errors here. */
                    hr = E_FAIL;
                }

                ok(FAILED(hr), "Unexpected hr %#x\n", hr);
                PURGE_CONTEXT();
                BEGIN();
            }
        }

        hr = ID3D12Device_GetDeviceRemovedReason(context.device);
        ok(SUCCEEDED(hr), "Unexpected hr %#x\n", hr);

        if (SUCCEEDED(hr))
        {
            PURGE();
        }
        else
        {
            PURGE_CONTEXT();
            BEGIN_CONTEXT();
        }
    }

    vkd3d_test_set_context(NULL);

    destroy_test_context(&context);
}

void test_typed_srv_cast_clear(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3;
    D3D12_HEAP_PROPERTIES heap_properties;
    struct test_context_desc test_desc;
    D3D12_RENDER_TARGET_VIEW_DESC rtv;
    D3D12_CLEAR_VALUE clear_value;
    struct test_context context;
    unsigned int test_iteration;
    ID3D12DescriptorHeap *heap;
    D3D12_RESOURCE_DESC desc;
    ID3D12Resource *texture;
    unsigned int i, j;
    FLOAT colors[4];
    bool fast_clear;

    struct test
    {
        DXGI_FORMAT image;
        DXGI_FORMAT view;
        float clear_value;
        float optimized_clear_value;
        uint32_t expected_component;
        uint32_t ulp;
    };

    /* RADV currently misses some cases where fast clear triggers for signed <-> unsigned and we get weird results. */

    static const struct test tests[] =
    {
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SINT, 0.0f, 0.0f, 0 },
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SINT, 1.0f, 1.0f / 255.0f, 1 },
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SINT, 127.0f, 127.0f / 255.0f, 0x7f, 0},
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SINT, -128.0f, 128.0f / 255.0f, 0x80 },
        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT, 0.0f, 0.0f, 0 },
        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT, 127.0f, 127.0f, 0x7f, 0},
        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT, -128.0f, 128.0f, 0x80 },

        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SNORM, 0.0f, 0.0f, 0 },
        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SNORM, 1.0f, 127.0f / 255.0f, 0x7f, 0},
        /* Could be 0x80 or 0x81 */
        { DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SNORM, -1.0f, 129.0f / 255.0f, 0x80, 1},

        { DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_UINT, 0.0f, 0.0f, 0},
        { DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_UINT, 255.0f, -1.0f / 127.0f, 0xff, 0},
        /* AMD native drivers return 0x81 here. Seems broken, but NV and Intel do the right thing ... */
        { DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_UINT, 128.0f, -1.0f, 0x80, 1 },
        { DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_UINT, 129.0f, -1.0f, 0x81 },
        { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UINT, 128.0f, -1.0f, 0x80 },
        { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UINT, 129.0f, -1.0f, 0x81 },

        { DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UINT, 0.0f, 0.0f, 0 },
        { DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UINT, 1.0f, 1.0f, 1 },
        { DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UINT, 255.0f, -1.0f, 0xff, 0},
        { DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM, 0.0f, 0.0f, 0 },
        { DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM, 1.0f, -1.0f, 0xff, 0},
        { DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM, 0.0f, 0.0f, 0 },
        { DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM, 128.0f / 255.0f, -128.0f, 0x80 },
        { DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM, 129.0f / 255.0f, -127.0f, 0x81 },
#if 0
        /* Not allowed by validation layer. */
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SNORM, 0.0f, 0.0f, 0 },
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SNORM, 1.0f, 127.0f / 255.0f, 0x7f },
        { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SNORM, -1.0f, 129.0f / 255.0f, 0x80 /* Could be 0x80 or 0x81 */, 1},
        { DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_UNORM, 0.0f, 0.0f, 0 },
        { DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_UNORM, 1.0f, -1.0f / 127.0f, 0xff },
#endif
    };

    memset(&test_desc, 0, sizeof(test_desc));
    test_desc.no_root_signature = true;
    test_desc.no_pipeline = true;
    test_desc.no_render_target = true;

    if (!init_test_context(&context, &test_desc))
        return;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3))) ||
        !options3.CastingFullyTypedFormatSupported)
    {
        skip("CastingFullyTypedFormat is not supported, skipping ...\n");
        destroy_test_context(&context);
        return;
    }

    heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    for (i = 0; i < 2 * ARRAY_SIZE(tests); i++)
    {
        test_iteration = i / 2;
        fast_clear = !!(i % 2);

        vkd3d_test_set_context("Test %u (%s)", test_iteration, fast_clear ? "fast" : "non-fast");

        memset(&desc, 0, sizeof(desc));
        desc.Width = 1024;
        desc.Height = 1024;
        desc.Format = tests[test_iteration].image;
        desc.DepthOrArraySize = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        desc.MipLevels = 1;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.SampleDesc.Count = 1;

        if (tests[test_iteration].image == DXGI_FORMAT_R8G8B8A8_TYPELESS)
        {
            clear_value.Format = tests[test_iteration].view;
            for (j = 0; j < 4; j++)
                clear_value.Color[j] = tests[test_iteration].clear_value;
        }
        else
        {
            clear_value.Format = tests[test_iteration].image;
            for (j = 0; j < 4; j++)
                clear_value.Color[j] = tests[test_iteration].optimized_clear_value;
        }

        ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
                &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                fast_clear ? &clear_value : NULL,
                &IID_ID3D12Resource, (void**)&texture);

        memset(&rtv, 0, sizeof(rtv));
        rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtv.Format = tests[test_iteration].view;
        ID3D12Device_CreateRenderTargetView(context.device, texture, &rtv,
                ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));

        for (j = 0; j < 4; j++)
            colors[j] = tests[test_iteration].clear_value;

        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list,
                ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap),
                colors, 0, NULL);

        transition_resource_state(context.list, texture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        check_sub_resource_uint(texture, 0, context.queue, context.list, tests[test_iteration].expected_component * 0x01010101u, tests[test_iteration].ulp);

        reset_command_list(context.list, context.allocator);
        ID3D12Resource_Release(texture);
    }
    vkd3d_test_set_context(NULL);

    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_uav_3d_sliced_view(void)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER root_params[2];
    ID3D12PipelineState *pso_poison;
    ID3D12PipelineState *pso_actual;
    struct resource_readback rb[2];
    D3D12_DESCRIPTOR_RANGE range;
    D3D12_GPU_DESCRIPTOR_HANDLE h;
    uint32_t reference[16][4][4];
    struct test_context context;
    ID3D12DescriptorHeap *heap;
    ID3D12Resource *resource;
    unsigned int x, y, z;
    unsigned int i;

#include "shaders/descriptors/headers/uav_3d_sliced_view_actual.h"
#include "shaders/descriptors/headers/uav_3d_sliced_view_poison.h"

    static const D3D12_TEX3D_UAV slices[] =
    {
        /* Just to clear everything */
        { 0, 0, -1u }, /* -1 means all remaining slices. */
        { 1, 0, -1u },
        /* ... */

        { 0, 0, 2 },
        { 0, 5, 3 },
        { 0, 9, 1 },
        { 0, 12, 4 },
        { 0, 10, 5 },
        { 1, 0, 2 },
        { 1, 4, 3 },
        { 0, 15, -1u },
        /* WSize = 0 is not allowed. Trips DEVICE_LOST. */
    };

    if (!init_compute_test_context(&context))
        return;

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(root_params, 0, sizeof(root_params));
    memset(&range, 0, sizeof(range));

    rs_desc.NumParameters = ARRAY_SIZE(root_params);
    rs_desc.pParameters = root_params;

    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_params[0].DescriptorTable.NumDescriptorRanges = 1;
    root_params[0].DescriptorTable.pDescriptorRanges = &range;
    range.NumDescriptors = 1;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;

    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_params[1].Constants.Num32BitValues = 1;

    create_root_signature(context.device, &rs_desc, &context.root_signature);
    pso_actual = create_compute_pipeline_state(context.device, context.root_signature, uav_3d_sliced_view_actual_dxbc);
    pso_poison = create_compute_pipeline_state(context.device, context.root_signature, uav_3d_sliced_view_poison_dxbc);

    resource = create_default_texture3d(context.device, 4, 4, 16, 2, DXGI_FORMAT_R32_UINT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    memset(&uav, 0, sizeof(uav));
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    uav.Format = DXGI_FORMAT_R32_UINT;

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ARRAY_SIZE(slices));

    for (i = 0; i < ARRAY_SIZE(slices); i++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        h.ptr += i * ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        uav.Texture3D = slices[i];
        ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav, h);
    }

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);

    h = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap);

    for (i = 0; i < ARRAY_SIZE(slices); i++)
    {
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0, h);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, i + 1, 0);
        /* First, attempt to flood the descriptor with writes. Validates robustness. */
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso_poison);
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
        uav_barrier(context.list, resource);
        /* Now, only write in bounds. Makes sure FirstWSlice offset works. */
        ID3D12GraphicsCommandList_SetPipelineState(context.list, pso_actual);
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
        uav_barrier(context.list, resource);

        h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    transition_resource_state(context.list, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(resource, 0, &rb[0], context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(slices); i++)
    {
        unsigned int num_slices;

        if (slices[i].MipSlice != 0)
            continue;

        num_slices = min(16 - slices[i].FirstWSlice, slices[i].WSize);

        for (z = 0; z < num_slices; z++)
        {
            for (y = 0; y < 4; y++)
            {
                for (x = 0; x < 4; x++)
                {
                    uint32_t *ref = &reference[z + slices[i].FirstWSlice][y][x];
                    *ref = i + 1;
                    *ref |= 4 << 8;
                    *ref |= 4 << 16;
                    *ref |= num_slices << 24;
                }
            }
        }
    }

    for (z = 0; z < 16; z++)
    {
        for (y = 0; y < 4; y++)
        {
            for (x = 0; x < 4; x++)
            {
                uint32_t value;
                value = get_readback_uint(&rb[0], x, y, z);
                todo ok(value == reference[z][y][x], "Error for mip 0 at %u, %u, %u. Got %x, expected %x.\n", x, y, z, value, reference[z][y][x]);
            }
        }
    }

    reset_command_list(context.list, context.allocator);
    get_texture_readback_with_command_list(resource, 1, &rb[1], context.queue, context.list);

    for (i = 0; i < ARRAY_SIZE(slices); i++)
    {
        unsigned int num_slices;

        if (slices[i].MipSlice != 1)
            continue;

        num_slices = min(8 - slices[i].FirstWSlice, slices[i].WSize);

        for (z = 0; z < num_slices; z++)
        {
            for (y = 0; y < 2; y++)
            {
                for (x = 0; x < 2; x++)
                {
                    uint32_t *ref = &reference[z + slices[i].FirstWSlice][y][x];
                    *ref = i + 1;
                    *ref |= 2 << 8;
                    *ref |= 2 << 16;
                    *ref |= num_slices << 24;
                }
            }
        }
    }

    for (z = 0; z < 8; z++)
    {
        for (y = 0; y < 2; y++)
        {
            for (x = 0; x < 2; x++)
            {
                uint32_t value;
                value = get_readback_uint(&rb[1], x, y, z);
                todo ok(value == reference[z][y][x], "Error for mip 1 at %u, %u, %u. Got %x, expected %x.\n", x, y, z, value, reference[z][y][x]);
            }
        }
    }

    for (i = 0; i < ARRAY_SIZE(rb); i++)
        release_resource_readback(&rb[i]);
    ID3D12Resource_Release(resource);
    ID3D12PipelineState_Release(pso_actual);
    ID3D12PipelineState_Release(pso_poison);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_root_descriptor_offset_sign(void)
{
    /* Exploratory test in nature. Will likely crash GPU if not on native drivers. Tweak ifdef to run it. */
#if 1
    skip("Skipping exploratory test for root descriptor over/underflow test.\n");
#else
    ID3D12RootSignature *root_signature;
    D3D12_ROOT_PARAMETER root_params[3];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12Resource *output_buffer;
    ID3D12Resource *input_buffer;
    struct resource_readback rb;
    struct test_context context;
    ID3D12PipelineState *pso;
    uint32_t values[4];
    unsigned int i;

#include "shaders/descriptors/headers/root_descriptor_offset_sign.h"

    static const uint32_t test_data[] = { 0, 1, 2, 3, 4, 5, 6, 7 };

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.NumParameters = ARRAY_SIZE(root_params);
    rs_desc.pParameters = root_params;
    memset(root_params, 0, sizeof(root_params));
    root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_params[2].Descriptor.ShaderRegister = 1;


    if (!init_compute_test_context(&context))
        return;

    if (!context_supports_dxil(&context))
    {
        destroy_test_context(&context);
        return;
    }

    input_buffer = create_upload_buffer(context.device, sizeof(test_data), test_data);
    output_buffer = create_default_buffer(context.device, 16,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    create_root_signature(context.device, &rs_desc, &root_signature);
    pso = create_compute_pipeline_state(context.device, root_signature, root_descriptor_offset_sign_dxil);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, pso);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, root_signature);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 0,
            ID3D12Resource_GetGPUVirtualAddress(output_buffer));
    ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context.list, 1,
            ID3D12Resource_GetGPUVirtualAddress(input_buffer) + 16);
    ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(context.list, 2,
            ID3D12Resource_GetGPUVirtualAddress(input_buffer) + 16);
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
    transition_resource_state(context.list, output_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffer, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < 4; i++)
        values[i] = get_readback_uint(&rb, i, 0, 0);

    skip("Got structured access [-1] = #%x\n", values[0]);
    skip("Got structured access [1u << 30] = #%x\n", values[1]);
    skip("Got byte address [-4] = #%x\n", values[2]);
    skip("Got byte address [0] = #%x\n", values[3]);

    /* Observed on AMD:
    test_root_descriptor_offset_sign:5262: Test skipped: Got structured access [-1] = #4b416743 <-- Garbage. Likely we accessed garbage memory way out at (4 * UINT_MAX) & UINT_MAX offset.
    test_root_descriptor_offset_sign:5263: Test skipped: Got structured access [1u << 30] = #4 <-- Suggests 32-bit uint offset.
    test_root_descriptor_offset_sign:5264: Test skipped: Got byte address [-4] = #0 <-- Suggests we hit robustness for driver generated descriptor.
    test_root_descriptor_offset_sign:5265: Test skipped: Got byte address [0] = #4
    */

    /* Observed on NV: Blue screen of death (?!?!). */

    /* Observed on Intel: All 0. Likely faulted and terminated the dispatch before we could write results. */

    ID3D12RootSignature_Release(root_signature);
    ID3D12PipelineState_Release(pso);
    ID3D12Resource_Release(input_buffer);
    ID3D12Resource_Release(output_buffer);
    release_resource_readback(&rb);
    destroy_test_context(&context);
#endif
}

static void test_uav_counters_null_behavior(bool use_dxil)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12DescriptorHeap *cpu_heap;
    D3D12_ROOT_PARAMETER rs_param;
    D3D12_DESCRIPTOR_RANGE range;
    struct test_context context;
    struct resource_readback rb;
    ID3D12DescriptorHeap *heap;
    ID3D12Resource *resource;
    unsigned int i;

#include "shaders/descriptors/headers/uav_counters_null_behavior.h"

    if (!init_compute_test_context(&context))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        skip("Context does not support DXIL.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(&rs_param, 0, sizeof(rs_param));
    memset(&range, 0, sizeof(range));
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &rs_param;
    rs_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_param.DescriptorTable.NumDescriptorRanges = 1;
    rs_param.DescriptorTable.pDescriptorRanges = &range;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 8;
    create_root_signature(context.device, &rs_desc, &context.root_signature);
    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature,
        use_dxil ? uav_counters_null_behavior_dxil : uav_counters_null_behavior_dxbc);

    cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8);
    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8);
    resource = create_default_buffer(context.device, D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT * 9,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    for (i = 0; i < 8; i++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_h, gpu_h;
        cpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap);
        gpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        cpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * i;
        gpu_h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * i;

        uav_desc.Buffer.NumElements = 4;
        uav_desc.Buffer.FirstElement = 4 * i;
        uav_desc.Buffer.StructureByteStride = 4;
        uav_desc.Buffer.CounterOffsetInBytes = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT * (i + 1);

        /* AMD drivers don't seem to clear the UAV counter if we pass in NULL, so
         * test a path which does not do that. */
        if (i < 4)
        {
            ID3D12Device_CreateUnorderedAccessView(context.device, resource, resource, &uav_desc, cpu_h);
            ID3D12Device_CreateUnorderedAccessView(context.device, resource, resource, &uav_desc, gpu_h);
        }

        uav_desc.Buffer.CounterOffsetInBytes = 0;

        /* Test writing NULL UAV counter after a non-NULL UAV counter. Makes sure that we are indeed supposed
         * to clear out UAV counters to NULL every time. */
        if ((i & 3) == 3)
        {
            ID3D12Device_CreateUnorderedAccessView(context.device, NULL, NULL, &uav_desc, cpu_h);
            ID3D12Device_CreateUnorderedAccessView(context.device, NULL, NULL, &uav_desc, gpu_h);
        }
        else if ((i & 3) >= 1)
        {
            ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav_desc, cpu_h);
            ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav_desc, gpu_h);
        }
        else
        {
            /* Test copy behavior. Make sure we correctly copy NULL counters as well. */
            ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav_desc, cpu_h);
            ID3D12Device_CopyDescriptorsSimple(context.device, 1,
                       gpu_h, cpu_h, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0,
              ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
    ID3D12GraphicsCommandList_Dispatch(context.list, 8 * 4, 1, 1);
    transition_resource_state(context.list, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(resource, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < 8 * 4; i++)
    {
        /* Possible behavior is very varied here:
         * NV: If UAV counter is NULL, NV makes the main descriptor robust.
         * AMD: Writing NULL uav counter does not update the counter descriptor, the atomic update will still go through.
         * Intel: Behaves as you would expect. Atomic op returns 0, writes to main descriptor behaves as you'd expect. */
        uint32_t value = get_readback_uint(&rb, i, 0, 0);
        ok(value == 0 || (value >= 64 && value < (64 + 4)), "Unexpected value %u = %u\n", i, value);
    }

    for (i = 0; i < 8; i++)
    {
        uint32_t value = get_readback_uint(&rb, (i + 1) * (D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT / 4), 0, 0);
        if (i < 4)
        {
            /* AMD behavior: Passing NULL does not necessarily clear out UAV counter.
             * It is undefined to access UAV counter like this.
             * https://docs.microsoft.com/en-us/windows/win32/direct3d12/uav-counters
             * "If a shader attempts to access the counter of a UAV that does not have an associated counter,
             *  then the debug layer will issue a warning,
             *  and a GPU page fault will occur causing the apps’s device to be removed." */
            ok(value == 0 || value == 4, "Unexpected counter %u = %u.\n", i, value);
        }
        else
        {
            /* Technically undefined, but all drivers behave robustly here, we should too. */
            ok(value == 0, "Unexpected counter %u = %u.\n", i, value);
        }
    }

    release_resource_readback(&rb);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12Resource_Release(resource);
    destroy_test_context(&context);
}

void test_uav_counter_null_behavior_dxbc(void)
{
    test_uav_counters_null_behavior(false);
}

void test_uav_counter_null_behavior_dxil(void)
{
    test_uav_counters_null_behavior(true);
}

static void test_uav_robustness_oob_structure_element(bool use_dxil)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE gpu_h;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[2];
    D3D12_DESCRIPTOR_RANGE range;
    struct test_context context;
    struct resource_readback rb;
    ID3D12DescriptorHeap *heap;
    ID3D12Resource *resource;
    unsigned int i;

#include "shaders/descriptors/headers/uav_robustness_oob_structure_element.h"

    if (!init_compute_test_context(&context))
        return;

    if (use_dxil && !context_supports_dxil(&context))
    {
        skip("Context does not support DXIL.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    memset(&range, 0, sizeof(range));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].DescriptorTable.NumDescriptorRanges = 1;
    rs_params[0].DescriptorTable.pDescriptorRanges = &range;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[1].Constants.Num32BitValues = 3;
    create_root_signature(context.device, &rs_desc, &context.root_signature);
    context.pipeline_state = create_compute_pipeline_state(context.device, context.root_signature,
        use_dxil ? uav_robustness_oob_structure_element_dxil : uav_robustness_oob_structure_element_dxbc);

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    resource = create_default_buffer(context.device, 64,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    memset(&uav_desc, 0, sizeof(uav_desc));
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    gpu_h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    uav_desc.Buffer.NumElements = 2;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.StructureByteStride = 16;
    uav_desc.Buffer.CounterOffsetInBytes = 0;

    ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav_desc, gpu_h);

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 0,
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));

    for (i = 0; i < 4; i++)
    {
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, 1, 0);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, i, 1);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, 0xf001 + i, 2);
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
    }

    /* This is undefined. Accesses a structure out of bounds. */
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, 1, 0);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, 4, 1);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, 0xbaad, 2);
    ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

    uav_barrier(context.list, NULL);

    /* This is definitely OOB. */
    for (i = 0; i < 5; i++)
    {
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, 2, 0);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, i, 1);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstant(context.list, 1, 0xbad, 2);
        ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);
    }

    transition_resource_state(context.list, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(resource, DXGI_FORMAT_R32_UINT, &rb, context.queue, context.list);

    for (i = 0; i < 9; i++)
    {
        uint32_t v = get_readback_uint(&rb, 4 + i, 0, 0);

        if (i < 4)
            ok(v == 0xf001 + i, "Index %u: Expected #%x, got #%x.\n", i, 0xf001 + i, v);
        else if (i == 4)
        {
            /* UB, either 0 or 0xbaad happens depending how driver deals with robustness. */
            ok(v == 0 || v == 0xbaad, "UB scenario: got #%x, expected 0 or 0xbaad.\n", v);
            if (v == 0xbaad)
                skip("Driver checks robustness based on structure element alone.\n");
            else
                skip("Driver checks robustness based on byte offset.\n");
        }
        else
            ok(v == 0, "OOB scenario: got #%x, expected 0.\n", v);
    }

    release_resource_readback(&rb);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12Resource_Release(resource);
    destroy_test_context(&context);
}

void test_uav_robustness_oob_structure_element_dxbc(void)
{
    test_uav_robustness_oob_structure_element(false);
}

void test_uav_robustness_oob_structure_element_dxil(void)
{
    test_uav_robustness_oob_structure_element(true);
}

void test_undefined_descriptor_heap_mismatch_types(void)
{
    /* Highly undefined. Exhaustively test all scenarios
     * where there is a mismatch between descriptor type and shader's expected type.
     * On native drivers, there seems to be a lot of safeguards in place to avoid hangs. */
    enum
    {
        SRV_TEX = 0,
        UAV_TEX,
        SRV_RAW_BUFFER,
        SRV_TYPED_BUFFER,
        UAV_RAW_BUFFER,
        UAV_TYPED_BUFFER,
        CBV,
        TYPE_COUNT
    };

    D3D12_FEATURE_DATA_SHADER_MODEL model;
    ID3D12PipelineState *psos[TYPE_COUNT];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_param[1];
    ID3D12DescriptorHeap *gpu_heap;
    ID3D12DescriptorHeap *cpu_heap;
    D3D12_SUBRESOURCE_DATA subdata;
    struct test_context context;
    struct resource_readback rb;
    ID3D12Resource *tex[2];
    ID3D12Resource *output;
    float float_data[64];
    ID3D12Resource *buf;
    float float_value;
    unsigned int i, j;

#include "shaders/descriptors/headers/undefined_descriptor_heap_mismatch_types_srv_raw.h"
#include "shaders/descriptors/headers/undefined_descriptor_heap_mismatch_types_srv_tex.h"
#include "shaders/descriptors/headers/undefined_descriptor_heap_mismatch_types_srv_typed.h"
#include "shaders/descriptors/headers/undefined_descriptor_heap_mismatch_types_uav_raw.h"
#include "shaders/descriptors/headers/undefined_descriptor_heap_mismatch_types_uav_tex.h"
#include "shaders/descriptors/headers/undefined_descriptor_heap_mismatch_types_uav_typed.h"
#include "shaders/descriptors/headers/undefined_descriptor_heap_mismatch_types_cbv.h"

    const D3D12_SHADER_BYTECODE dxil_code[TYPE_COUNT] =
    {
        undefined_descriptor_heap_mismatch_types_srv_tex_dxil,
        undefined_descriptor_heap_mismatch_types_uav_tex_dxil,
        undefined_descriptor_heap_mismatch_types_srv_raw_dxil,
        undefined_descriptor_heap_mismatch_types_srv_typed_dxil,
        undefined_descriptor_heap_mismatch_types_uav_raw_dxil,
        undefined_descriptor_heap_mismatch_types_uav_typed_dxil,
        undefined_descriptor_heap_mismatch_types_cbv_dxil,
    };

    if (!init_compute_test_context(&context))
        return;

    model.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
            model.HighestShaderModel < D3D_SHADER_MODEL_6_6)
    {
        skip("Shader model 6.6 not supported, skipping test.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_param, 0, sizeof(rs_param));
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    rs_desc.NumParameters = ARRAY_SIZE(rs_param);
    rs_desc.pParameters = rs_param;
    rs_param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;

    create_root_signature(context.device, &rs_desc, &context.root_signature);

    for (i = 0; i < ARRAY_SIZE(psos); i++)
        psos[i] = create_compute_pipeline_state(context.device, context.root_signature, dxil_code[i]);

    tex[0] = create_default_texture2d(context.device, 1, 1, 1, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    tex[1] = create_default_texture2d(context.device, 1, 1, 1, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    buf = create_default_buffer(context.device, sizeof(float_data), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    output = create_default_buffer(context.device, sizeof(float), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

    gpu_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    cpu_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, TYPE_COUNT);

    for (i = 0; i < 64; i++)
        float_data[i] = 10.0f + (float)i;
    upload_buffer_data(buf, 0, sizeof(float_data), float_data, context.queue, context.list);
    reset_command_list(context.list, context.allocator);

    /* Damage the descriptor heap with completely unrelated descriptors. This way, we make it so that
     * we observe if we forgot to clear out sibling NULL descriptors in create calls.
     * Buffer SRVs are useful since they will write both typed and untyped on vkd3d-proton. */
    for (i = 0; i < TYPE_COUNT; i++)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.Buffer.FirstElement = 32;
        srv_desc.Buffer.NumElements = 1;
        srv_desc.Buffer.StructureByteStride = 4;
        ID3D12Device_CreateShaderResourceView(context.device, buf, &srv_desc, get_cpu_handle(context.device, cpu_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, i));
    }

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
        srv_desc.Texture2D.MipLevels = 1;
        ID3D12Device_CreateShaderResourceView(context.device, tex[0], &srv_desc, get_cpu_handle(context.device, cpu_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SRV_TEX));
    }

    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
        memset(&uav_desc, 0, sizeof(uav_desc));
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
        ID3D12Device_CreateUnorderedAccessView(context.device, tex[1], NULL, &uav_desc, get_cpu_handle(context.device, cpu_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, UAV_TEX));
    }

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.Buffer.NumElements = 1;
        srv_desc.Buffer.StructureByteStride = 4;
        ID3D12Device_CreateShaderResourceView(context.device, buf, &srv_desc, get_cpu_handle(context.device, cpu_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SRV_RAW_BUFFER));
    }

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
        srv_desc.Buffer.FirstElement = 1;
        srv_desc.Buffer.NumElements = 1;
        ID3D12Device_CreateShaderResourceView(context.device, buf, &srv_desc, get_cpu_handle(context.device, cpu_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, SRV_TYPED_BUFFER));
    }

    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
        memset(&uav_desc, 0, sizeof(uav_desc));
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.Buffer.FirstElement = 2;
        uav_desc.Buffer.NumElements = 1;
        uav_desc.Buffer.StructureByteStride = 4;
        ID3D12Device_CreateUnorderedAccessView(context.device, buf, NULL, &uav_desc, get_cpu_handle(context.device, cpu_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, UAV_RAW_BUFFER));
    }

    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
        memset(&uav_desc, 0, sizeof(uav_desc));
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
        uav_desc.Buffer.FirstElement = 3;
        uav_desc.Buffer.NumElements = 1;
        ID3D12Device_CreateUnorderedAccessView(context.device, buf, NULL, &uav_desc, get_cpu_handle(context.device, cpu_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, UAV_TYPED_BUFFER));
    }

    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc;
        memset(&cbv_desc, 0, sizeof(cbv_desc));
        cbv_desc.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(buf);
        cbv_desc.SizeInBytes = sizeof(float_data);
        ID3D12Device_CreateConstantBufferView(context.device, &cbv_desc, get_cpu_handle(context.device, cpu_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, CBV));
    }

    float_value = 1.0f;
    subdata.pData = &float_value;
    subdata.RowPitch = sizeof(float);
    subdata.SlicePitch = sizeof(float);
    upload_texture_data(tex[0], &subdata, 1, context.queue, context.list);
    reset_command_list(context.list, context.allocator);
    float_value = 2.0f;
    upload_texture_data(tex[1], &subdata, 1, context.queue, context.list);
    reset_command_list(context.list, context.allocator);

    for (j = 0; j < TYPE_COUNT; j++)
    {
        for (i = 0; i < TYPE_COUNT; i++)
        {
            unsigned int clobber_descriptor_type;
            unsigned int descriptor_type = j;
            unsigned int access_type = i;
            bool working_aliasing;
            bool is_todo = true;
            bool accept_zero;
            float expected;
            float value;

            static const float expected_for_descriptor[TYPE_COUNT] =
            {
                1.0f,
                2.0f,
                10.0f,
                11.0f,
                12.0f,
                13.0f,
                10.0f,
            };

            /* On current NVIDIA, images are 4 byte, but texel buffers are 16 byte,
             * but we have to alias them together. */
            if (!is_nvidia_windows_device(context.device) && is_nvidia_device(context.device))
            {
                if ((access_type == SRV_TEX || access_type == UAV_TEX) &&
                        (descriptor_type == SRV_TYPED_BUFFER || descriptor_type == UAV_TYPED_BUFFER))
                {
                    skip("Skipping typed buffer sampled as image on NVIDIA since it will hang GPU.\n");
                    continue;
                }
                else if ((access_type == SRV_TYPED_BUFFER || access_type == UAV_TYPED_BUFFER) &&
                        (descriptor_type == SRV_TEX || descriptor_type == UAV_TEX))
                {
                    skip("Skipping image sampled as typed buffer on NVIDIA since it will hang GPU.\n");
                    continue;
                }
            }
            else if (is_mesa_intel_device(context.device))
            {
                /* ANV currently gets MUTABLE single path, which is not very robust.
                 * This will improve with descriptor buffers. */
                if ((access_type == UAV_TEX || access_type == UAV_TYPED_BUFFER) && descriptor_type == SRV_RAW_BUFFER)
                {
                    skip("Skipping SRV raw buffer sampled as typed UAV on ANV since it will hang GPU.\n");
                    continue;
                }
                else if ((access_type == SRV_TEX || access_type == UAV_TEX) && descriptor_type == UAV_RAW_BUFFER)
                {
                    skip("Skipping UAV raw buffer buffer sampled as texture on ANV since it will hang GPU.\n");
                    continue;
                }
                else if ((access_type == SRV_TYPED_BUFFER || access_type == UAV_TYPED_BUFFER) && descriptor_type == UAV_RAW_BUFFER)
                {
                    skip("Skipping UAV raw buffer sampled as texel buffer on ANV since it will hang GPU.\n");
                    continue;
                }
                else if ((access_type == UAV_TEX || access_type == UAV_TYPED_BUFFER) && descriptor_type == CBV)
                {
                    skip("Skipping CBV sampled as typed UAV on ANV since it will hang GPU.\n");
                    continue;
                }
            }
            else if (is_amd_vulkan_device(context.device) && !is_radv_device(context.device))
            {
                /* For amdvlk/proprietary. Here we have 32 byte layout which has some failure cases
                 * where we expect hangs. */
                if ((access_type == UAV_RAW_BUFFER || access_type == SRV_RAW_BUFFER || access_type == CBV) &&
                        (descriptor_type == SRV_TEX || descriptor_type == UAV_TEX))
                {
                    skip("Skipping texture accessed through raw buffer since it will hang GPU.\n");
                    continue;
                }
            }

            /* First, clobber the descriptor with something that is clearly wrong.
             * When we write the wrong descriptor afterward, we want to make sure that we either observe
             * the new descriptor as-is, or we sample NULL. */
            clobber_descriptor_type = access_type ^ 1;
            clobber_descriptor_type = min(clobber_descriptor_type, TYPE_COUNT - 1);
            ID3D12Device_CopyDescriptorsSimple(context.device, 1, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu_heap),
                    get_cpu_handle(context.device, cpu_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, clobber_descriptor_type),
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            ID3D12Device_CopyDescriptorsSimple(context.device, 1, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu_heap),
                    get_cpu_handle(context.device, cpu_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_type),
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &gpu_heap);
            ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
            ID3D12GraphicsCommandList_SetPipelineState(context.list, psos[access_type]);
            ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 0, ID3D12Resource_GetGPUVirtualAddress(output));
            ID3D12GraphicsCommandList_Dispatch(context.list, 1, 1, 1);

            transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            get_buffer_readback_with_command_list(output, DXGI_FORMAT_R32_FLOAT, &rb, context.queue, context.list);

            working_aliasing = false;
            accept_zero = false;

            /* Typed and raw buffers seem to "just werk". We also make that scenario work in vkd3d-proton.
             * SRV/UAV image seems to work too.
             * CBV/raw buffer aliasing also seems to mostly work, with a caveat on NV that sampling a CBV as typed buffer results in 0. */

            if ((descriptor_type == SRV_TYPED_BUFFER || descriptor_type == UAV_TYPED_BUFFER) &&
                (access_type == SRV_RAW_BUFFER || access_type == UAV_RAW_BUFFER))
            {
                working_aliasing = true;

                /* We explicitly make typed / SSBO work through two descriptors side by side. */
                is_todo = is_mesa_intel_device(context.device);
            }
            else if ((access_type == SRV_TYPED_BUFFER || access_type == UAV_TYPED_BUFFER) &&
                    (descriptor_type == SRV_RAW_BUFFER || descriptor_type == UAV_RAW_BUFFER))
            {
                working_aliasing = true;

                /* We explicitly make typed / SSBO work through two descriptors side by side. */
                is_todo = is_mesa_intel_device(context.device);
            }
            else if ((access_type == SRV_TYPED_BUFFER || access_type == UAV_TYPED_BUFFER) &&
                    descriptor_type == CBV)
            {
                working_aliasing = true;
                /* NV samples zero here. */
                accept_zero = true;
                /* We explicitly make this work through creating a NULL typed buffer. */
                is_todo = false;
            }
            else if ((access_type == SRV_RAW_BUFFER || access_type == UAV_RAW_BUFFER) &&
                    descriptor_type == CBV)
            {
                working_aliasing = true;
                /* NV samples zero here. */
                accept_zero = true;

                /* UBO aliases SSBO in vkd3d-proton, and most modern GPUs have same descriptor layout. */
                is_todo = false;
            }
            else if (descriptor_type == SRV_TEX && access_type == UAV_TEX)
            {
                /* Trivial */
                working_aliasing = true;

                /* STORAGE vs SAMPLED does not really matter on most GPUs. */
                is_todo = false;
                accept_zero = is_mesa_intel_device(context.device);
            }
            else if (descriptor_type == UAV_TEX && access_type == SRV_TEX)
            {
                /* Trivial */
                working_aliasing = true;

                /* STORAGE vs SAMPLED does not really matter on most GPUs. */
                is_todo = false;
            }
            else if (access_type == CBV && (descriptor_type == SRV_RAW_BUFFER || descriptor_type == UAV_RAW_BUFFER))
            {
                working_aliasing = true;
                /* AMD samples zero here. */
                accept_zero = true;

                /* UBO aliases SSBO in vkd3d-proton, and most modern GPUs have same descriptor layout. */
                is_todo = false;
            }
            else if (access_type == CBV && (descriptor_type == SRV_TYPED_BUFFER || descriptor_type == UAV_TYPED_BUFFER))
            {
                working_aliasing = true;
                /* AMD samples zero here. */
                accept_zero = true;

                /* A typed buffer will write an SSBO, UBO aliases SSBO in vkd3d-proton,
                 * and most modern GPUs have same descriptor layout. */
                is_todo = false;
            }
            else if ((access_type == SRV_RAW_BUFFER && descriptor_type == UAV_RAW_BUFFER) ||
                    (access_type == UAV_RAW_BUFFER && descriptor_type == SRV_RAW_BUFFER))
            {
                /* Trivial */
                working_aliasing = true;

                /* STORAGE vs SAMPLED doesn't matter. */
                is_todo = false;
            }
            else if (access_type == SRV_TYPED_BUFFER && descriptor_type == UAV_TYPED_BUFFER)
            {
                /* Trivial */
                working_aliasing = true;

                /* STORAGE vs SAMPLED doesn't matter. */
                is_todo = false;
            }
            else if (access_type == UAV_TYPED_BUFFER && descriptor_type == SRV_TYPED_BUFFER)
            {
                /* Trivial */
                working_aliasing = true;

                /* STORAGE vs SAMPLED doesn't matter. */
				is_todo = false;
                accept_zero = is_mesa_intel_device(context.device);
            }
            else if (access_type == descriptor_type)
                is_todo = false;

            value = get_readback_float(&rb, 0, 0);
            expected = expected_for_descriptor[descriptor_type];

            if (i == j || working_aliasing)
            {
                /* CBV shader accesses buffer at an offset to make sure we get different results than the SRV_RAW_BUFFER test. */
                if (access_type == CBV)
                    expected += 16.0f;
                todo_if(is_todo) ok(value == expected || (accept_zero && value == 0.0f),
                        "Descriptor type %u, Shader type %u, expected %f, got %f.\n",
                        descriptor_type, access_type, expected, value);
            }
            else if (value != expected)
            {
                if (value == 0.0f)
                {
                    skip("Accessing type %u is not compatible with descriptor type %u, got NULL result instead.\n",
                            access_type, descriptor_type);
                }
                else
                {
                    skip("Accessing type %u is not compatible with descriptor type %u (as expected, expected %f, got %f).\n",
                            access_type, descriptor_type, expected, value);
                }
            }
            else
                skip("Unexpected working aliasing for descriptor type %u, shader type %u.\n", descriptor_type, access_type);

            release_resource_readback(&rb);
            reset_command_list(context.list, context.allocator);
        }
    }

    for (i = 0; i < ARRAY_SIZE(tex); i++)
        ID3D12Resource_Release(tex[i]);
    for (i = 0; i < ARRAY_SIZE(psos); i++)
        ID3D12PipelineState_Release(psos[i]);
    ID3D12Resource_Release(buf);
    ID3D12DescriptorHeap_Release(gpu_heap);
    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12Resource_Release(output);
    destroy_test_context(&context);
}

void test_sampler_non_normalized_coordinates(void)
{
    ID3D12PipelineState *pso_static_sampler, *pso_sampler_descriptor;
    ID3D12RootSignature *rs_static_sampler, *rs_sampler_descriptor;
    ID3D12DescriptorHeap *sampler_heap, *srv_heap, *rtv_heap;
    D3D12_FEATURE_DATA_ROOT_SIGNATURE rootsig_info;
    D3D12_STATIC_SAMPLER_DESC1 static_sampler_desc;
    D3D12_FEATURE_DATA_D3D12_OPTIONS17 options17;
    D3D12_DESCRIPTOR_RANGE1 descriptor_ranges[2];
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
    D3D12_ROOT_PARAMETER1 rs_params[2];
    D3D12_SAMPLER_DESC2 sampler_desc;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    D3D12_SUBRESOURCE_DATA subdata;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12Device11 *device11;
    ID3D12Resource *tex, *rt;
    D3D12_VIEWPORT viewport;
    unsigned int i, x, y;
    D3D12_RECT scissor;
    HRESULT hr;

    static const FLOAT black[] = { 0.0f, 0.0f, 0.0f, 0.0f };

#include "shaders/descriptors/headers/sampler_non_normalized_coordinates.h"

    static uint8_t tex_data[4][4] =
    {
        { 0xff, 0xc0, 0x80, 0x40 },
        { 0xc0, 0x80, 0x40, 0x20 },
        { 0x80, 0x40, 0x20, 0x10 },
        { 0x40, 0x20, 0x10, 0x00 },
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    desc.no_pipeline = true;
    desc.no_render_target = true;

    if (!init_test_context(&context, &desc))
        return;

    memset(&rootsig_info, 0, sizeof(rootsig_info));
    rootsig_info.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_2;
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_ROOT_SIGNATURE, &options17, sizeof(options17));

    if (rootsig_info.HighestVersion < D3D_ROOT_SIGNATURE_VERSION_1_2)
    {
        skip("Root signature version 1.2 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&options17, 0, sizeof(options17));
    ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS17, &options17, sizeof(options17));

    if (!options17.NonNormalizedCoordinateSamplersSupported)
    {
        skip("Non-normalized sampler coordinates not supported.\n");
        destroy_test_context(&context);
        return;
    }

    if (FAILED(ID3D12Device_QueryInterface(context.device, &IID_ID3D12Device11, (void **)&device11)))
    {
        skip("ID3D12Device11 not supported.\n");
        destroy_test_context(&context);
        return;
    }

    memset(&static_sampler_desc, 0, sizeof(static_sampler_desc));
    static_sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    static_sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    static_sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    static_sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    static_sampler_desc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    static_sampler_desc.ShaderRegister = 0;
    static_sampler_desc.RegisterSpace = 0;
    static_sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    static_sampler_desc.Flags = D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES;

    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler_desc.Flags = D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES;

    memset(descriptor_ranges, 0, sizeof(descriptor_ranges));
    descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptor_ranges[0].NumDescriptors = 1;
    descriptor_ranges[0].BaseShaderRegister = 0;
    descriptor_ranges[0].RegisterSpace = 0;
    descriptor_ranges[0].OffsetInDescriptorsFromTableStart = 0;

    descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    descriptor_ranges[1].NumDescriptors = 1;
    descriptor_ranges[1].BaseShaderRegister = 0;
    descriptor_ranges[1].RegisterSpace = 0;
    descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 0;

    memset(rs_params, 0, sizeof(rs_params));
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[0].DescriptorTable.NumDescriptorRanges = 1;
    rs_params[0].DescriptorTable.pDescriptorRanges = &descriptor_ranges[0];
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[1].DescriptorTable.NumDescriptorRanges = 1;
    rs_params[1].DescriptorTable.pDescriptorRanges = &descriptor_ranges[1];
    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_2;
    rs_desc.Desc_1_2.NumParameters = 1;
    rs_desc.Desc_1_2.pParameters = rs_params;
    rs_desc.Desc_1_2.NumStaticSamplers = 1;
    rs_desc.Desc_1_2.pStaticSamplers = &static_sampler_desc;

    hr = create_versioned_root_signature(context.device, &rs_desc, &rs_static_sampler);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_2;
    rs_desc.Desc_1_2.NumParameters = 2;
    rs_desc.Desc_1_2.pParameters = rs_params;

    hr = create_versioned_root_signature(context.device, &rs_desc, &rs_sampler_descriptor);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr %#x.\n", hr);

    init_pipeline_state_desc(&pso_desc, rs_static_sampler, DXGI_FORMAT_R8_UNORM, NULL,
            &sampler_non_normalized_coordinates_dxbc, NULL);
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso_static_sampler);
    ok(SUCCEEDED(hr), "Failed to create graphics pipeline, hr %#x.\n", hr);

    init_pipeline_state_desc(&pso_desc, rs_sampler_descriptor, DXGI_FORMAT_R8_UNORM, NULL,
            &sampler_non_normalized_coordinates_dxbc, NULL);
    hr = ID3D12Device_CreateGraphicsPipelineState(context.device, &pso_desc, &IID_ID3D12PipelineState, (void **)&pso_sampler_descriptor);
    ok(SUCCEEDED(hr), "Failed to create graphics pipeline, hr %#x.\n", hr);

    rtv_heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    srv_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    sampler_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1);

    tex = create_default_texture(context.device, 4, 4, DXGI_FORMAT_R8_UNORM, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Device_CreateShaderResourceView(context.device, tex, NULL, get_cpu_descriptor_handle(&context, srv_heap, 0));

    rt = create_default_texture(context.device, 4, 4, DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    rtv = get_cpu_rtv_handle(&context, rtv_heap, 0);
    ID3D12Device_CreateRenderTargetView(context.device, rt, NULL, rtv);

    ID3D12Device11_CreateSampler2(device11, &sampler_desc, get_cpu_sampler_handle(&context, sampler_heap, 0));

    subdata.pData = tex_data;
    subdata.RowPitch = 4;
    subdata.SlicePitch = 16;
    upload_texture_data(tex, &subdata, 1, context.queue, context.list);
    reset_command_list(context.list, context.allocator);

    transition_resource_state(context.list, tex,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = 4.0f;
    viewport.Height = 4.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 0.0f;

    scissor.left = 0;
    scissor.top = 0;
    scissor.right = 4;
    scissor.bottom = 4;

    for (i = 0; i < 2; i++)
    {
        ID3D12DescriptorHeap *descriptor_heaps[2];
        descriptor_heaps[0] = srv_heap;
        descriptor_heaps[1] = sampler_heap;

        vkd3d_test_set_context("Test %u", i);

        ID3D12GraphicsCommandList_OMSetRenderTargets(context.list, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, rtv, black, 0, NULL);
        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1 + i, descriptor_heaps);

        if (i == 0)
        {
            ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, rs_static_sampler);
            ID3D12GraphicsCommandList_SetPipelineState(context.list, pso_static_sampler);
        }
        else
        {
            ID3D12GraphicsCommandList_SetGraphicsRootSignature(context.list, rs_sampler_descriptor);
            ID3D12GraphicsCommandList_SetPipelineState(context.list, pso_sampler_descriptor);

            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(context.list, 1,
                    get_gpu_sampler_handle(&context, sampler_heap, 0));
        }

        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(context.list, 0,
                get_gpu_descriptor_handle(&context, srv_heap, 0));
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(context.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_RSSetViewports(context.list, 1, &viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(context.list, 1, &scissor);
        ID3D12GraphicsCommandList_DrawInstanced(context.list, 3, 1, 0, 0);

        transition_resource_state(context.list, rt,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_texture_readback_with_command_list(rt, 0, &rb, context.queue, context.list);

        for (y = 0; y < 4; y++)
        {
            for (x = 0; x < 4; x++)
            {
                uint8_t u8 = get_readback_uint8(&rb, x, y);
                ok(u8 == tex_data[y][x], "Got %#x at %u,%u, expected %#x.\n", u8, x, y, tex_data[y][x]);
            }
        }

        release_resource_readback(&rb);
        reset_command_list(context.list, context.allocator);

        transition_resource_state(context.list, rt,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    ID3D12Resource_Release(tex);
    ID3D12Resource_Release(rt);

    ID3D12DescriptorHeap_Release(rtv_heap);
    ID3D12DescriptorHeap_Release(srv_heap);
    ID3D12DescriptorHeap_Release(sampler_heap);

    ID3D12PipelineState_Release(pso_static_sampler);
    ID3D12PipelineState_Release(pso_sampler_descriptor);

    ID3D12RootSignature_Release(rs_static_sampler);
    ID3D12RootSignature_Release(rs_sampler_descriptor);

    ID3D12Device11_Release(device11);
    destroy_test_context(&context);
}

void test_sampler_rounding(void)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS19 opts19;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_DESCRIPTOR_RANGE rs_ranges[2];
    ID3D12DescriptorHeap *sampler_heap;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[4];
    D3D12_SAMPLER_DESC sampler_desc;
    ID3D12DescriptorHeap *desc_heap;
    ID3D12PipelineState *pso_sample;
    ID3D12PipelineState *pso_gather;
    struct resource_readback rb;
    struct test_context context;
    ID3D12Resource *texture;
    ID3D12Resource *output;
    unsigned int i, j;

#include "shaders/descriptors/headers/sampler_rounding.h"
#include "shaders/descriptors/headers/sampler_rounding_gather.h"

    /* Step in units of fractional ULPs. Texel coordinates have 8 bits of fixed point precision. */
    static const struct test
    {
        float uv_stride[3];
        unsigned int pixels;
        bool gather;
        bool linear;
    } tests[] = {
        { { 0.50f - 4.0f / 256.0f, 0.5f, 1.0f / (128.0f * 256.0f) }, 1024, false, false }, /* cover the range from +/- 4 subtexels */
        { { 0.75f - 4.0f / 256.0f, 0.5f, 1.0f / (128.0f * 256.0f) }, 1024, false, true },
        { { 0.75f - 4.0f / 256.0f, 0.5f, 1.0f / (128.0f * 256.0f) }, 1024, true, false },
        { { 0.75f - 4.0f / 256.0f, 0.5f, 1.0f / (128.0f * 256.0f) }, 1024, true, true },
    };

    if (!init_compute_test_context(&context))
        return;

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    memset(rs_ranges, 0, sizeof(rs_ranges));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rs_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;

    rs_params[0].Constants.Num32BitValues = 3;
    rs_params[2].DescriptorTable.NumDescriptorRanges = 1;
    rs_params[2].DescriptorTable.pDescriptorRanges = &rs_ranges[0];
    rs_params[3].DescriptorTable.NumDescriptorRanges = 1;
    rs_params[3].DescriptorTable.pDescriptorRanges = &rs_ranges[1];
    rs_ranges[0].NumDescriptors = 1;
    rs_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rs_ranges[1].NumDescriptors = 1;
    rs_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;

    create_root_signature(context.device, &rs_desc, &context.root_signature);
    pso_sample = create_compute_pipeline_state(context.device, context.root_signature, sampler_rounding_dxbc);
    pso_gather = create_compute_pipeline_state(context.device, context.root_signature, sampler_rounding_gather_dxbc);

    if (FAILED(ID3D12Device_CheckFeatureSupport(context.device, D3D12_FEATURE_D3D12_OPTIONS19, &opts19, sizeof(opts19))))
        memset(&opts19, 0, sizeof(opts19));

    if (opts19.PointSamplingAddressesNeverRoundUp)
        skip("Driver exposes PointSamplingAddressesNeverRoundUp.\n");
    else
        skip("Driver does not expose PointSamplingAddressesNeverRoundUp.\n");

    texture = create_default_texture2d(context.device, 2, 1, 1, 1, DXGI_FORMAT_R10G10B10A2_UNORM,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);

    desc_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    sampler_heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1);

    {
        static const uint32_t pixels[2] = { 0u, ~0u };
        D3D12_SUBRESOURCE_DATA tex_data;
        tex_data.pData = pixels;
        tex_data.RowPitch = 8;
        tex_data.SlicePitch = 8;
        upload_texture_data(texture, &tex_data, 1, context.queue, context.list);
        reset_command_list(context.list, context.allocator);
    }

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    ID3D12Device_CreateShaderResourceView(context.device, texture, &srv_desc, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(desc_heap));

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        ID3D12DescriptorHeap *heaps[2] = { desc_heap, sampler_heap };

        vkd3d_test_set_context("Test %u", i);

        output = create_default_buffer(context.device, tests[i].pixels * sizeof(float),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        memset(&sampler_desc, 0, sizeof(sampler_desc));
        sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler_desc.Filter = tests[i].linear ? D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_POINT;
        ID3D12Device_CreateSampler(context.device, &sampler_desc, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sampler_heap));

        ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, ARRAY_SIZE(heaps), heaps);
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
        ID3D12GraphicsCommandList_SetPipelineState(context.list, tests[i].gather ? pso_gather : pso_sample);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(context.list, 0, ARRAY_SIZE(tests[i].uv_stride), tests[i].uv_stride, 0);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 1, ID3D12Resource_GetGPUVirtualAddress(output));
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 2, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(desc_heap));
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, 3, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(sampler_heap));
        ID3D12GraphicsCommandList_Dispatch(context.list, tests[i].pixels, 1, 1);
        transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output, DXGI_FORMAT_R32_FLOAT, &rb, context.queue, context.list);

        for (j = 0; j < tests[i].pixels; j++)
        {
            float v, expected_lo, expected_hi;
            float uv_hi;
            float uv_lo;
            float uv;

            uv = tests[i].uv_stride[0] + (float)j * tests[i].uv_stride[2];
            uv *= 256.0f * 2.0f; /* width = 2 */

            /* 3.2.4.1 FLOAT -> Fixed Point Integer. 0.6 ULP error allowed in the fixed point round.
             * Point sampling also goes through this rounding process. */

            uv_lo = ceilf(uv - 0.6f);
            uv_hi = floorf(uv + 0.6f);

            if (tests[i].gather)
            {
                /* Gather behaves like bilinear and should ignore point sampling in sampler.
                 * This should thus be unaffected by PointSamplingAddressesNeverRoundUp, but this is mildly suspect.
                 * Texture filtering behavior seems to have changed dramatically in more recent WARP builds however. ... */
                if (use_warp_device)
                {
                    /* WARP does not seem to round to fixed point at all. It uses the escape hatch:
                     * scaledU is converted to **at least** 16.8 Fixed Point(3.2.4.1). Call this fxpScaledU.
                     * It's likely only limited by FP precision.
                     */
                    expected_lo = uv < 256.0f + 128.0f ? 0.0f : 1.0f;
                    expected_hi = expected_lo;
                }
                else
                {
                    expected_lo = uv_lo < 256.0f + 128.0f ? 0.0f : 1.0f;
                    expected_hi = uv_hi < 256.0f + 128.0f ? 0.0f : 1.0f;
                }
            }
            else if (!tests[i].linear)
            {
                /* Point sampling. https://microsoft.github.io/DirectX-Specs/d3d/VulkanOn12.html#changing-the-spec-for-point-sampling-address-computations */
                if (opts19.PointSamplingAddressesNeverRoundUp || is_nvidia_windows_device(context.device) || use_warp_device)
                {
                    /* NVIDIA ignores the fixed point rounding for point sampling.
                     * It seems like it abuses the 16.8 fixed point rule by pretending to have "infinite" precision.
                     * In the asympotote, this becomes a truncation. */

                    /* WARP is similar. It truncates. */
                    expected_lo = uv < 256.0f ? 0.0f : 1.0f;
                    expected_hi = expected_lo;
                }
                else
                {
                    /* AMD and Intel always do RTE. */
                    expected_lo = uv_lo < 256.0f ? 0.0f : 1.0f;
                    expected_hi = uv_hi < 256.0f ? 0.0f : 1.0f;
                }
            }
            else
            {
                /* Linear sampling. Hardware always rounds to 8 subtexel fixed point. */
                if (uv_lo >= 256.0f + 128.0f)
                {
                    expected_lo = 1.0f;
                }
                else
                {
                    float weight = (uv_lo - 128.0f) / 256.0f;

                    /* 7.18.16.2 Texture Filtering Arithmetic Precision. Filtering must be performed with precision of format, so we expect maximal error of < 1 ULP in 10-bit domain. */
                    expected_lo = weight - 0.6f / 1023.0f; /* lerp between 0 and 1. Allow a little of 0.5 ULP error in filtering. HW passes this, the exact ULP range isn't interesting. */
                }

                if (uv_hi >= 256.0f + 128.0f)
                {
                    expected_hi = 1.0f;
                }
                else
                {
                    float weight = (uv_hi - 128.0f) / 256.0f;

                    /* 7.18.16.2 Texture Filtering Arithmetic Precision. Filtering must be performed with precision of format, so we expect maximal error of < 1 ULP in 10-bit domain. */
                    expected_hi = weight + 0.6f / 1023.0f; /* lerp between 0 and 1. Allow a little of 0.5 ULP error in filtering. HW passes this, the exact ULP range isn't interesting. */
                }
            }

            v = get_readback_float(&rb, j, 0);
            ok(v >= expected_lo && v <= expected_hi, "UV [raw %.6f] [snap lo %d.#%02x, snap hi %d.#%02x]: Expected value in range ([%f, %f] / 256.0), got (%f / 256.0)\n",
                uv,
                (int)(uv_lo / 256.0f), ((uint32_t)uv_lo) & 0xff,
                (int)(uv_hi / 256.0f), ((uint32_t)uv_hi) & 0xff,
                expected_lo * 256.0f, expected_hi * 256.0f, v * 256.0f);
        }

        release_resource_readback(&rb);
        ID3D12Resource_Release(output);
        reset_command_list(context.list, context.allocator);
    }
    vkd3d_test_set_context(NULL);

    ID3D12Resource_Release(texture);
    ID3D12DescriptorHeap_Release(desc_heap);
    ID3D12DescriptorHeap_Release(sampler_heap);
    ID3D12PipelineState_Release(pso_sample);
    ID3D12PipelineState_Release(pso_gather);
    destroy_test_context(&context);
}

void test_tex_array_reinterpretation(bool use_dxil, D3D12_RESOURCE_DIMENSION dim, bool uav)
{
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_SUBRESOURCE_DATA subresource_data;
    D3D12_DESCRIPTOR_RANGE rs_range[2];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[3];
    uint8_t image_data[256 * 128];
    struct test_context context;
    struct resource_readback rb;
    ID3D12DescriptorHeap *heap;
    ID3D12Resource *resource;
    ID3D12Resource *output;
    unsigned int i, j;

#include "shaders/descriptors/headers/tex2d_array_reinterpretation.h"
#include "shaders/descriptors/headers/tex1d_array_reinterpretation.h"
#include "shaders/descriptors/headers/rwtex2d_array_reinterpretation.h"
#include "shaders/descriptors/headers/rwtex1d_array_reinterpretation.h"

    struct output_info
    {
        uint32_t dimensions[2];
        uint32_t array_dimensions[2];
        uint32_t array_layers;
        uint32_t levels;
        uint32_t array_levels;
        float sampled;
        float array_sampled;
    };

    if (!init_compute_test_context(&context))
        return;

    heap = create_gpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256);
    resource = create_default_texture_dimension(context.device, dim,
        256, dim == D3D12_RESOURCE_DIMENSION_TEXTURE2D ? 128 : 1,
        16, 8, DXGI_FORMAT_R8_UNORM, uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COMMON);
    output = create_default_buffer(context.device, sizeof(struct output_info) * 256 * 16, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    memset(rs_range, 0, sizeof(rs_range));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;

    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].DescriptorTable.NumDescriptorRanges = 1;
    rs_params[0].DescriptorTable.pDescriptorRanges = &rs_range[0];

    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[1].DescriptorTable.NumDescriptorRanges = 1;
    rs_params[1].DescriptorTable.pDescriptorRanges = &rs_range[1];

    rs_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rs_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rs_range[0].NumDescriptors = 64;
    rs_range[0].BaseShaderRegister = 0;

    if (uav)
    {
        rs_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        rs_range[0].RegisterSpace = 1;
    }
    else
    {
        rs_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        rs_range[0].RegisterSpace = 0;
    }

    rs_range[1].NumDescriptors = 64;
    rs_range[1].BaseShaderRegister = 0;

    if (uav)
    {
        rs_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        rs_range[1].RegisterSpace = 2;
    }
    else
    {
        rs_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        rs_range[1].RegisterSpace = 1;
    }

    create_root_signature(context.device, &rs_desc, &context.root_signature);

    if (uav)
    {
        if (dim == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            context.pipeline_state = create_compute_pipeline_state(
                context.device, context.root_signature,
                use_dxil ? rwtex2d_array_reinterpretation_dxil : rwtex2d_array_reinterpretation_dxbc);
        }
        else
        {
            context.pipeline_state = create_compute_pipeline_state(
                context.device, context.root_signature,
                use_dxil ? rwtex1d_array_reinterpretation_dxil : rwtex1d_array_reinterpretation_dxbc);
        }
    }
    else
    {
        if (dim == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            context.pipeline_state = create_compute_pipeline_state(
                context.device, context.root_signature,
                use_dxil ? tex2d_array_reinterpretation_dxil : tex2d_array_reinterpretation_dxbc);
        }
        else
        {
            context.pipeline_state = create_compute_pipeline_state(
                context.device, context.root_signature,
                use_dxil ? tex1d_array_reinterpretation_dxil : tex1d_array_reinterpretation_dxbc);
        }
    }

    subresource_data.pData = image_data;
    subresource_data.RowPitch = 256;
    subresource_data.SlicePitch = 256 * 128;

    for (i = 0; i < 128; i++)
    {
        memset(image_data, i + 1, sizeof(image_data));
        upload_texture_data_base(resource, &subresource_data, i, 1, context.queue, context.list);
        reset_command_list(context.list, context.allocator);

        memset(&srv_desc, 0, sizeof(srv_desc));
        memset(&uav_desc, 0, sizeof(uav_desc));

        /* 22.3.12 Input Resource Declaration Statement in D3D11.3 functional spec states that:
         *  The following describes which (created) resources are permitted to be
            bound to the t# for each declaration resourceType:
            declaration 'Buffer':              resource 'Buffer'
            declaration 'Texture1D':           resource 'Texture1D' with array length == 1
            declaration 'Texture1DArray':      resource 'Texture1D' with array length >= 1
            declaration 'Texture2D[MS#]':      resource 'Texture2D' with array length == 1
            declaration 'Texture2DArray[MS#]': resource 'Texture2D' with array length >= 1
            declaration 'Texture3D':           resource 'Texture3D'
            declartionn 'TextureCube':         resource 'TextureCube */

        if (uav)
        {
            uav_desc.Format = DXGI_FORMAT_R8_UNORM;

            if (dim == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
            {
                if (i >= 8)
                {
                    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                    uav_desc.Texture2DArray.MipSlice = i % 8;
                    uav_desc.Texture2DArray.FirstArraySlice = i / 8;
                    if (i >= 64)
                    {
                        /* This is not in spec, but will probably just work? */
                        uav_desc.Texture2DArray.ArraySize = 16 - uav_desc.Texture2DArray.FirstArraySlice;
                    }
                    else
                    {
                        /* Accessing this as Texture2D should be in-spec on D3D. */
                        uav_desc.Texture2DArray.ArraySize = 1;
                    }
                }
                else
                {
                    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                    uav_desc.Texture2D.MipSlice = i % 8;
                }
            }
            else
            {
                if (i >= 8)
                {
                    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
                    uav_desc.Texture1DArray.MipSlice = i % 8;
                    uav_desc.Texture1DArray.FirstArraySlice = i / 8;
                    if (i >= 64)
                    {
                        /* This is not in spec, but will probably just work? */
                        uav_desc.Texture1DArray.ArraySize = 16 - uav_desc.Texture1DArray.FirstArraySlice;
                    }
                    else
                    {
                        /* Accessing this as Texture2D should be in-spec on D3D. */
                        uav_desc.Texture1DArray.ArraySize = 1;
                    }
                }
                else
                {
                    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
                    uav_desc.Texture1D.MipSlice = i % 8;
                }
            }

            /* Also test the null descriptor variant of that. */
            ID3D12Device_CreateUnorderedAccessView(context.device, resource, NULL, &uav_desc, get_cpu_descriptor_handle(&context, heap, i));
            ID3D12Device_CreateUnorderedAccessView(context.device, NULL, NULL, &uav_desc, get_cpu_descriptor_handle(&context, heap, i + 128));
        }
        else
        {
            srv_desc.Format = DXGI_FORMAT_R8_UNORM;
            srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

            if (dim == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
            {
                if (i >= 8)
                {
                    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                    srv_desc.Texture2DArray.MostDetailedMip = i % 8;
                    srv_desc.Texture2DArray.MipLevels = 8 - srv_desc.Texture2D.MostDetailedMip;
                    srv_desc.Texture2DArray.FirstArraySlice = i / 8;
                    if (i >= 64)
                    {
                        /* This is not in spec, but will probably just work? */
                        srv_desc.Texture2DArray.ArraySize = 16 - srv_desc.Texture2DArray.FirstArraySlice;
                    }
                    else
                    {
                        /* Accessing this as Texture2D should be in-spec on D3D. */
                        srv_desc.Texture2DArray.ArraySize = 1;
                    }
                }
                else
                {
                    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srv_desc.Texture2D.MostDetailedMip = i % 8;
                    srv_desc.Texture2D.MipLevels = 8 - srv_desc.Texture2D.MostDetailedMip;
                }
            }
            else
            {
                if (i >= 8)
                {
                    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
                    srv_desc.Texture1DArray.MostDetailedMip = i % 8;
                    srv_desc.Texture1DArray.MipLevels = 8 - srv_desc.Texture1D.MostDetailedMip;
                    srv_desc.Texture1DArray.FirstArraySlice = i / 8;
                    if (i >= 64)
                    {
                        /* This is not in spec, but will probably just work? */
                        srv_desc.Texture1DArray.ArraySize = 16 - srv_desc.Texture1DArray.FirstArraySlice;
                    }
                    else
                    {
                        /* Accessing this as Texture2D should be in-spec on D3D. */
                        srv_desc.Texture1DArray.ArraySize = 1;
                    }
                }
                else
                {
                    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
                    srv_desc.Texture1D.MostDetailedMip = i % 8;
                    srv_desc.Texture1D.MipLevels = 8 - srv_desc.Texture1D.MostDetailedMip;
                }
            }

            /* Also test the null descriptor variant of that. */
            ID3D12Device_CreateShaderResourceView(context.device, resource, &srv_desc, get_cpu_descriptor_handle(&context, heap, i));
            ID3D12Device_CreateShaderResourceView(context.device, NULL, &srv_desc, get_cpu_descriptor_handle(&context, heap, i + 128));
        }
    }

    if (uav)
        transition_resource_state(context.list, resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.list, 1, &heap);
    ID3D12GraphicsCommandList_SetPipelineState(context.list, context.pipeline_state);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.list, context.root_signature);
    for (i = 0; i < 2; i++)
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.list, i, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.list, 2, ID3D12Resource_GetGPUVirtualAddress(output));
    ID3D12GraphicsCommandList_Dispatch(context.list, 256, 1, 1);

    transition_resource_state(context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output, DXGI_FORMAT_UNKNOWN, &rb, context.queue, context.list);

    for (i = 0; i < 256; i++)
    {
        for (j = 0; j < 16; j++)
        {
            struct output_info expected_info;
            struct output_info actual_info;
            uint32_t query_level = 0;

            memset(&expected_info, 0, sizeof(expected_info));
            memcpy(&actual_info, get_readback_data(&rb, i * 16 + j, 0, 0, sizeof(actual_info)), sizeof(actual_info));

            if (i < 128)
            {
                uint32_t level, layer, view_layers, view_levels;
                uint32_t sample_subresource;
                uint32_t sample_layer;
                uint32_t sample_level;

                level = i % 8;
                layer = i / 8;
                view_layers = i >= 64 ? 16 - layer : 1;
                view_levels = uav ? 1 : 8 - level;
                query_level = level + (uav ? 0 : j);

                /* Querying dimensions out of range returns 0 */
                expected_info.dimensions[0] = query_level < 8 ? max(256 >> query_level, 1) : 0;
                expected_info.dimensions[1] = query_level < 8 ? max(128 >> query_level, 1) : 0;

                if (dim == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
                    expected_info.dimensions[1] = 0; /* Dummy value filled by shader. */

                expected_info.array_dimensions[0] = expected_info.dimensions[0];
                expected_info.array_dimensions[1] = expected_info.dimensions[1];
                expected_info.array_layers = query_level < 8 ? view_layers : 0;
                expected_info.levels = uav ? 0 : view_levels;
                expected_info.array_levels = uav ? 0 : view_levels;

                sample_layer = 0;
                sample_level = uav ? 0 : j;
                sample_subresource = (sample_layer + layer) * 8 + (sample_level + level);
                expected_info.sampled = sample_layer < view_layers && sample_level < view_levels ? sample_subresource + 1 : 0;

                if (uav)
                {
                    sample_layer = j;
                    sample_level = 0;
                }
                else
                {
                    sample_layer = j % 4;
                    sample_level = j / 4;
                }
                sample_subresource = (sample_layer + layer) * 8 + (sample_level + level);
                expected_info.array_sampled = sample_layer < view_layers && sample_level < view_levels ? sample_subresource + 1 : 0;
            }

            bug_if(query_level >= 8 && is_amd_windows_device(context.device))
            ok(memcmp(&actual_info, &expected_info, sizeof(expected_info)) == 0,
                "Group %u, Thread %u:\n "
                "expected\n\t(%u, %u) (%u, %u)\n\tlevels (%u, %u)\n\tsampled (%f, %f)\n\tlayers %u\n "
                "got\n\t(%u, %u) (%u, %u)\n\tlevels (%u, %u)\n\tsampled (%f, %f)\n\tlayers %u\n\n", i, j,
                expected_info.dimensions[0], expected_info.dimensions[1],
                expected_info.array_dimensions[0], expected_info.array_dimensions[1],
                expected_info.levels, expected_info.array_levels,
                expected_info.sampled, expected_info.array_sampled,
                expected_info.array_layers,
                actual_info.dimensions[0], actual_info.dimensions[1],
                actual_info.array_dimensions[0], actual_info.array_dimensions[1],
                actual_info.levels, actual_info.array_levels,
                actual_info.sampled, actual_info.array_sampled,
                actual_info.array_layers);
        }
    }

    ID3D12Resource_Release(resource);
    ID3D12Resource_Release(output);
    release_resource_readback(&rb);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_tex2d_array_reinterpretation_sm51(void)
{
    test_tex_array_reinterpretation(false, D3D12_RESOURCE_DIMENSION_TEXTURE2D, false);
}

void test_tex2d_array_reinterpretation_dxil(void)
{
    test_tex_array_reinterpretation(true, D3D12_RESOURCE_DIMENSION_TEXTURE2D, false);
}

void test_tex1d_array_reinterpretation_sm51(void)
{
    test_tex_array_reinterpretation(false, D3D12_RESOURCE_DIMENSION_TEXTURE1D, false);
}

void test_tex1d_array_reinterpretation_dxil(void)
{
    test_tex_array_reinterpretation(true, D3D12_RESOURCE_DIMENSION_TEXTURE1D, false);
}

void test_rwtex2d_array_reinterpretation_sm51(void)
{
    test_tex_array_reinterpretation(false, D3D12_RESOURCE_DIMENSION_TEXTURE2D, true);
}

void test_rwtex2d_array_reinterpretation_dxil(void)
{
    test_tex_array_reinterpretation(true, D3D12_RESOURCE_DIMENSION_TEXTURE2D, true);
}

void test_rwtex1d_array_reinterpretation_sm51(void)
{
    test_tex_array_reinterpretation(false, D3D12_RESOURCE_DIMENSION_TEXTURE1D, true);
}

void test_rwtex1d_array_reinterpretation_dxil(void)
{
    test_tex_array_reinterpretation(true, D3D12_RESOURCE_DIMENSION_TEXTURE1D, true);
}