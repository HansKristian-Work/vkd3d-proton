/*
 * Copyright 2020 Hans-Kristian Arntzen for Valve Corporation
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

static PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER pfn_D3D12CreateVersionedRootSignatureDeserializer;
static PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE pfn_D3D12SerializeVersionedRootSignature;
PFN_D3D12_CREATE_DEVICE pfn_D3D12CreateDevice;
PFN_D3D12_GET_DEBUG_INTERFACE pfn_D3D12GetDebugInterface;

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

static void fill_descriptor_heap_srv(ID3D12Device *device, ID3D12DescriptorHeap *heap,
        ID3D12Resource *resource, const D3D12_SHADER_RESOURCE_VIEW_DESC *desc, unsigned int count)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    UINT stride, i;

    stride = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    for (i = 0; i < count; i++)
    {
        ID3D12Device_CreateShaderResourceView(device, resource, desc, cpu_handle);
        cpu_handle.ptr += stride;
    }
}

static void copy_descriptor_heap(ID3D12Device *device, ID3D12DescriptorHeap *gpu_heap,
        ID3D12DescriptorHeap *cpu_heap, unsigned int count)
{
    ID3D12Device_CopyDescriptorsSimple(device, count,
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(gpu_heap),
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(cpu_heap),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

static void zero_descriptor_heap(ID3D12Device *device, ID3D12DescriptorHeap *heap,
        ID3D12Resource *resource, unsigned int count)
{
    fill_descriptor_heap_srv(device, heap, resource, NULL, count);
}

static void do_benchmark_run(ID3D12Device *device)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc;
    ID3D12DescriptorHeap *gpu_heap;
    ID3D12DescriptorHeap *cpu_heap;
    double start_time, end_time;
    ID3D12Resource *texture;
    HRESULT hr;

    heap_desc.NumDescriptors = 1000000;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NodeMask = 0;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void**)&cpu_heap);
    ok(SUCCEEDED(hr), "Failed to create descriptor heap, hr #%x.\n", hr);

    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = ID3D12Device_CreateDescriptorHeap(device, &heap_desc, &IID_ID3D12DescriptorHeap, (void**)&gpu_heap);
    ok(SUCCEEDED(hr), "Failed to create descriptor heap, hr #%x.\n", hr);

    texture = create_default_texture2d(device,
                                       256, 256, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
                                       D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ok(texture != NULL, "Failed to create texture.\n");

    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.PlaneSlice = 0;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;

    /* Benchmark creation of 1 million SRVs in CPU-only heaps. */
    {
        start_time = get_time();
        fill_descriptor_heap_srv(device, cpu_heap, texture, &srv_desc, 1000000);
        end_time = get_time();
        printf("Creating 1M SRVs on blank CPU heap took: %.3f ms.\n", 1e3 * (end_time - start_time));
    }

    /* Do the same thing again, but this time on a used heap, so we also have to destroy existing views. */
    {
        start_time = get_time();
        fill_descriptor_heap_srv(device, cpu_heap, texture, &srv_desc, 1000000);
        end_time = get_time();
        printf("Creating 1M SRVs on dirty CPU heap took: %.3f ms.\n", 1e3 * (end_time - start_time));
    }

    /* Fill shader visible heaps */
    {
        start_time = get_time();
        fill_descriptor_heap_srv(device, gpu_heap, texture, &srv_desc, 1000000);
        end_time = get_time();
        printf("Creating 1M SRVs on blank GPU-visible heap took: %.3f ms.\n", 1e3 * (end_time - start_time));
    }

    /* Do the same thing again, but this time on a used heap, so we also have to destroy existing views. */
    {
        start_time = get_time();
        fill_descriptor_heap_srv(device, gpu_heap, texture, &srv_desc, 1000000);
        end_time = get_time();
        printf("Creating 1M SRVs on dirty GPU-visible heap took: %.3f ms.\n", 1e3 * (end_time - start_time));
    }

    /* Try copying descriptors */
    {
        start_time = get_time();
        copy_descriptor_heap(device, gpu_heap, cpu_heap, 1000000);
        end_time = get_time();
        printf("Copying 1M SRVs to dirty GPU visible heap took: %.3f ms.\n", 1e3 * (end_time - start_time));
    }

    /* Try copying descriptors with duplication */
    {
        start_time = get_time();
        copy_descriptor_heap(device, gpu_heap, cpu_heap, 1000000);
        end_time = get_time();
        printf("Copying 1M SRVs (duplicates) took: %.3f ms.\n", 1e3 * (end_time - start_time));
    }

    /* Create zero descriptors. */
    {
        start_time = get_time();
        zero_descriptor_heap(device, gpu_heap, texture, 1000000);
        end_time = get_time();
        printf("Creating 1M null-SRVs took: %.3f ms.\n", 1e3 * (end_time - start_time));
    }

    /* Try copying descriptors on top of zero-initialized descriptor heap. */
    {
        start_time = get_time();
        copy_descriptor_heap(device, gpu_heap, cpu_heap, 1000000);
        end_time = get_time();
        printf("Copying 1M SRVs to zeroed GPU visible heap took: %.3f ms.\n", 1e3 * (end_time - start_time));
    }

    ID3D12Resource_Release(texture);
    ID3D12DescriptorHeap_Release(cpu_heap);
    ID3D12DescriptorHeap_Release(gpu_heap);
}

START_TEST(descriptor_performance)
{
    ID3D12Device *device;
    unsigned int i;

    setup(argc, argv);
    device = create_device();
    ok(device != NULL, "Failed to create device.\n");

    for (i = 0; i < 100; i++)
        do_benchmark_run(device);

    ID3D12Device_Release(device);
}

