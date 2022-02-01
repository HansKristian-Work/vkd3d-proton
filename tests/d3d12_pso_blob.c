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

void test_get_cached_blob(void)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute_desc;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12RootSignature *root_signature;
    struct test_context context;
    ID3D12PipelineState *state;
    ID3D12Device *device;
    ID3DBlob *blob;
    HRESULT hr;

#if 0
    [numthreads(1,1,1)]
    void main() { }
#endif
    static const DWORD cs_dxbc[] =
    {
        0x43425844, 0x1acc3ad0, 0x71c7b057, 0xc72c4306, 0xf432cb57, 0x00000001, 0x00000074, 0x00000003, 
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f, 
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000020, 0x00050050, 0x00000008, 0x0100086a, 
        0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x0100003e,
    };

    if (!init_test_context(&context, NULL))
        return;

    device = context.device;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    memset(&compute_desc, 0, sizeof(compute_desc));
    compute_desc.pRootSignature = root_signature;
    compute_desc.CS.pShaderBytecode = cs_dxbc;
    compute_desc.CS.BytecodeLength = sizeof(cs_dxbc);

    hr = ID3D12Device_CreateComputePipelineState(device,
            &compute_desc, &IID_ID3D12PipelineState, (void**)&state);
    ok(hr == S_OK, "Failed to create compute pipeline, hr %#x.\n", hr);

    hr = ID3D12PipelineState_GetCachedBlob(state, &blob);
    ok(hr == S_OK, "Failed to get cached blob, hr %#x.\n", hr);
    ok(ID3D10Blob_GetBufferSize(blob) > 0, "Cached blob is empty.\n");

    ID3D12PipelineState_Release(state);

    compute_desc.CachedPSO.pCachedBlob = ID3D10Blob_GetBufferPointer(blob);
    compute_desc.CachedPSO.CachedBlobSizeInBytes = ID3D10Blob_GetBufferSize(blob);

    hr = ID3D12Device_CreateComputePipelineState(device,
            &compute_desc, &IID_ID3D12PipelineState, (void**)&state);
    ok(hr == S_OK, "Failed to create compute pipeline, hr %#x.\n", hr);

    ID3D12PipelineState_Release(state);
    ID3D12RootSignature_Release(root_signature);

    ID3D10Blob_Release(blob);
    destroy_test_context(&context);
}

void test_pipeline_library(void)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics_desc;
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute_desc;
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12PipelineLibrary *pipeline_library;
    ID3D12RootSignature *root_signature;
    struct test_context context;
    ID3D12PipelineState *state;
    size_t serialized_size;
    ID3D12Device1 *device1;
    void *serialized_data;
    ID3D12Device *device;
    HRESULT hr;

#if 0
    [numthreads(1,1,1)]
    void main() { }
#endif
    static const DWORD cs_dxbc[] =
    {
        0x43425844, 0x1acc3ad0, 0x71c7b057, 0xc72c4306, 0xf432cb57, 0x00000001, 0x00000074, 0x00000003, 
        0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f, 
        0x00000008, 0x00000000, 0x00000008, 0x58454853, 0x00000020, 0x00050050, 0x00000008, 0x0100086a, 
        0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x0100003e,
    };

#if 0
    float4 main() : SV_POSITION {
            return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
#endif
    static const DWORD vs_dxbc[] =
    {
        0x43425844, 0xae39b246, 0xddd05b5a, 0x5057a6a2, 0x034461ee, 0x00000001, 0x000000b8, 0x00000003, 
        0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f, 
        0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003, 0x00000000, 
        0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x58454853, 0x00000040, 0x00010050, 0x00000010, 
        0x0100086a, 0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x08000036, 0x001020f2, 0x00000000, 
        0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0100003e,
    };

#if 0
    float4 main() : SV_TARGET {
            return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
#endif
    static const DWORD ps_dxbc[] =
    {
        0x43425844, 0x29b14cf3, 0xb991cf90, 0x9e455ffc, 0x4675b046, 0x00000001, 0x000000b4, 0x00000003, 
        0x0000002c, 0x0000003c, 0x00000070, 0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f, 
        0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000, 
        0x0000000f, 0x545f5653, 0x45475241, 0xabab0054, 0x58454853, 0x0000003c, 0x00000050, 0x0000000f, 
        0x0100086a, 0x03000065, 0x001020f2, 0x00000000, 0x08000036, 0x001020f2, 0x00000000, 0x00004002, 
        0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000, 0x0100003e,
    };

    const WCHAR *graphics_name = u"GRAPHICS";
    const WCHAR *compute_name  = u"COMPUTE";

    if (!init_test_context(&context, NULL))
        return;

    device = context.device;

    if (FAILED(hr = ID3D12Device_QueryInterface(device, &IID_ID3D12Device1, (void**)&device1)))
    {
        skip("ID3D12Device1 not available.\n");
        return;
    }

    /* Test adding pipelines to an empty pipeline library */
    hr = ID3D12Device1_CreatePipelineLibrary(device1, NULL, 0, &IID_ID3D12PipelineLibrary, (void**)&pipeline_library);
    ok(hr == S_OK, "Failed to create pipeline library, hr %#x.\n", hr);

    /* ppData == NULL means a query */
    hr = ID3D12Device1_CreatePipelineLibrary(device1, NULL, 0, NULL, NULL);
    ok(hr == S_FALSE, "Failed to query pipeline library, hr %#x.\n", hr);

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(hr == S_OK, "Failed to create root signature, hr %#x.\n", hr);

    memset(&compute_desc, 0, sizeof(compute_desc));
    compute_desc.pRootSignature = root_signature;
    compute_desc.CS.pShaderBytecode = cs_dxbc;
    compute_desc.CS.BytecodeLength = sizeof(cs_dxbc);

    hr = ID3D12PipelineLibrary_LoadComputePipeline(pipeline_library,
            compute_name, &compute_desc, &IID_ID3D12PipelineState, (void**)&state);
    ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

    hr = ID3D12Device_CreateComputePipelineState(device,
            &compute_desc, &IID_ID3D12PipelineState, (void**)&state);
    ok(hr == S_OK, "Failed to create compute pipeline, hr %#x.\n", hr);

    hr = ID3D12PipelineLibrary_StorePipeline(pipeline_library, compute_name, state);
    ok(hr == S_OK, "Failed to store compute pipeline, hr %x.\n", hr);

    ID3D12PipelineState_Release(state);

    memset(&graphics_desc, 0, sizeof(graphics_desc));
    graphics_desc.pRootSignature = root_signature;
    graphics_desc.VS.pShaderBytecode = vs_dxbc;
    graphics_desc.VS.BytecodeLength = sizeof(vs_dxbc);
    graphics_desc.PS.pShaderBytecode = ps_dxbc;
    graphics_desc.PS.BytecodeLength = sizeof(ps_dxbc);
    graphics_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    graphics_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    graphics_desc.RasterizerState.FrontCounterClockwise = true;
    graphics_desc.SampleMask = 0x1;
    graphics_desc.SampleDesc.Count = 1;
    graphics_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphics_desc.NumRenderTargets = 1;
    graphics_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

    hr = ID3D12PipelineLibrary_LoadGraphicsPipeline(pipeline_library,
            graphics_name, &graphics_desc, &IID_ID3D12PipelineState, (void**)&state);
    ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

    hr = ID3D12Device_CreateGraphicsPipelineState(device,
            &graphics_desc, &IID_ID3D12PipelineState, (void**)&state);
    ok(hr == S_OK, "Failed to create graphics pipeline, hr %#x.\n", hr);

    hr = ID3D12PipelineLibrary_StorePipeline(pipeline_library, graphics_name, state);
    ok(hr == S_OK, "Failed to store compute pipeline, hr %x.\n", hr);
    hr = ID3D12PipelineLibrary_StorePipeline(pipeline_library, compute_name, state);
    ok(hr == E_INVALIDARG, "Storing pipeline with already existing name succeeded, hr %x.\n", hr);

    ID3D12PipelineState_Release(state);

    /* Test looking up pipelines in a new pipeline library */
    hr = ID3D12PipelineLibrary_LoadComputePipeline(pipeline_library,
            compute_name, &compute_desc, &IID_ID3D12PipelineState, (void**)&state);
    ok(hr == S_OK, "Failed to load compute pipeline from pipeline library, hr %#x.\n", hr);
    ID3D12PipelineState_Release(state);

    hr = ID3D12PipelineLibrary_LoadGraphicsPipeline(pipeline_library,
            graphics_name, &graphics_desc, &IID_ID3D12PipelineState, (void**)&state);
    ok(hr == S_OK, "Failed to load graphics pipeline from pipeline library, hr %#x.\n", hr);
    ID3D12PipelineState_Release(state);

    serialized_size = ID3D12PipelineLibrary_GetSerializedSize(pipeline_library);
    ok(serialized_size > 0, "Serialized size for pipeline library is 0.\n");

    serialized_data = malloc(serialized_size);
    hr = ID3D12PipelineLibrary_Serialize(pipeline_library, serialized_data, serialized_size - 1);
    ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

    hr = ID3D12PipelineLibrary_Serialize(pipeline_library, serialized_data, serialized_size);
    ok(hr == S_OK, "Failed to serialize pipeline library, hr %#x.\n", hr);

    ID3D12PipelineLibrary_Release(pipeline_library);

    /* Test deserializing a pipeline library */
    hr = ID3D12Device1_CreatePipelineLibrary(device1, serialized_data,
            serialized_size, &IID_ID3D12PipelineLibrary, (void**)&pipeline_library);
    ok(hr == S_OK, "Failed to create pipeline library, hr %#x.\n");

    hr = ID3D12PipelineLibrary_LoadGraphicsPipeline(pipeline_library,
            graphics_name, &graphics_desc, &IID_ID3D12PipelineState, (void**)&state);
    ok(hr == S_OK, "Failed to load graphics pipeline from pipeline library, hr %#x.\n", hr);
    ID3D12PipelineState_Release(state);

    hr = ID3D12PipelineLibrary_LoadComputePipeline(pipeline_library,
            compute_name, &compute_desc, &IID_ID3D12PipelineState, (void**)&state);
    ok(hr == S_OK, "Failed to load compute pipeline from pipeline library, hr %#x.\n", hr);
    ID3D12PipelineState_Release(state);

    hr = ID3D12PipelineLibrary_LoadComputePipeline(pipeline_library,
            graphics_name, &compute_desc, &IID_ID3D12PipelineState, (void**)&state);
    ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

    if (SUCCEEDED(hr))
        ID3D12PipelineState_Release(state);

    ID3D12PipelineLibrary_Release(pipeline_library);

    free(serialized_data);
    ID3D12RootSignature_Release(root_signature);
    ID3D12Device1_Release(device1);
    destroy_test_context(&context);
}

