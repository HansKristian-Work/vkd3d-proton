/*
 * Copyright 2023 Philip Rebohle for Valve Corporation
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
#include "d3d12_dstorage_blobs.h"

const GUID IID_META_COMMAND_DSTORAGE = {0x1bddd090,0xc47e,0x459c,{0x8f,0x81,0x42,0xc9,0xf9,0x7a,0x53,0x08}};

static inline uint64_t hash_fnv1_init()
{
    return 0xcbf29ce484222325ull;
}

static inline uint64_t hash_fnv1_iterate_u8(uint64_t h, uint8_t value)
{
    return (h * 0x100000001b3ull) ^ value;
}

static void verify_hash(const uint8_t *data, size_t offset, size_t size, uint64_t expected)
{
    uint64_t hash;
    size_t i;

    hash = hash_fnv1_init();

    for (i = 0; i < size; i++)
      hash = hash_fnv1_iterate_u8(hash, data[offset + i]);

    ok(hash == expected, "Hash mismatch: Got 0x%"PRIx64", expected 0x%"PRIx64".\n", hash, expected);
}

void test_dstorage_decompression(void)
{
    ID3D12Resource *input_buffer, *output_buffer, *control_buffer, *scratch_buffer;
    uint64_t scratch_buffer_size, input_buffer_size, output_buffer_size;
    D3D12_FEATURE_DATA_QUERY_META_COMMAND query;
    D3D12_META_COMMAND_DESC *meta_command_descs;
    void *control_buffer_ptr, *input_buffer_ptr;
    ID3D12GraphicsCommandList4 *command_list4;
    ID3D12CommandAllocator *command_allocator;
    ID3D12GraphicsCommandList *command_list;
    uint64_t create_args[4], exec_args[11];
    D3D12_COMMAND_QUEUE_DESC queue_desc;
    uint32_t query_in[4], query_out[6];
    D3D12_RESOURCE_DESC resource_desc;
    D3D12_HEAP_PROPERTIES heap_desc;
    ID3D12MetaCommand *meta_command;
    struct resource_readback rb;
    bool supports_meta_command;
    uint32_t max_stream_count;
    ID3D12CommandQueue *queue;
    uint32_t control_data[5];
    ID3D12Device5 *device5;
    ID3D12Device *device;
    unsigned int i;
    uint32_t count;
    ULONG refcount;
    size_t offset;
    HRESULT hr;

    struct
    {
        const void *data;
        size_t compressed_size;
        size_t output_size;
        uint64_t hash;
        uint32_t in_offset;
        uint32_t out_offset;
    }
    compressed_files[] =
    {
        {gdeflate_test_file_a, sizeof(gdeflate_test_file_a), 172186, 0xf1b06045e7861250ull},
        {gdeflate_test_file_b, sizeof(gdeflate_test_file_b), 358081, 0xd630273cd1f11214ull},
    };

    if (!(device = create_device()))
    {
        skip("Failed to create device.\n");
        return;
    }

    if (is_intel_windows_device(device) || is_nvidia_windows_device(device))
    {
        /* Running this test on Nvidia's Windows driver instantly triggers a device loss
         * for unknown reasons. Nvidia returns 1 in one of the undocumented feature bits
         * where AMD reports 0, so it is possible that it expects different behaviour
         * from the caller despite metacommand creation parameters being identical. */
        skip("Metacommand usage not known for this device.\n");
        ID3D12Device_Release(device);
        return;
    }

    hr = ID3D12Device_QueryInterface(device, &IID_ID3D12Device5, (void**)&device5);

    if (FAILED(hr))
    {
        skip("ID3D12Device5 not supported by implementation.\n");
        ID3D12Device_Release(device);
        return;
    }

    /* Skip test if meta command is not supported */
    supports_meta_command = false;

    count = 0;
    hr = ID3D12Device5_EnumerateMetaCommands(device5, &count, NULL);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    meta_command_descs = calloc(count, sizeof(*meta_command_descs));

    hr = ID3D12Device5_EnumerateMetaCommands(device5, &count, meta_command_descs);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    for (i = 0; i < count && !supports_meta_command; i++)
        supports_meta_command = !memcmp(&meta_command_descs[i].Id, &IID_META_COMMAND_DSTORAGE, sizeof(meta_command_descs[i].Id));

    free(meta_command_descs);

    if (!supports_meta_command)
    {
        skip("DirectStorage meta command not supported.\n");
        ID3D12Device5_Release(device5);
        ID3D12Device_Release(device);
        return;
    }

    input_buffer_size = 0;
    output_buffer_size = 0;

    for (i = 0; i < ARRAY_SIZE(compressed_files); i++)
    {
        compressed_files[i].in_offset = input_buffer_size;
        compressed_files[i].out_offset = output_buffer_size;
        input_buffer_size += compressed_files[i].compressed_size;
        output_buffer_size += (compressed_files[i].output_size + 15) & ~((size_t)0xf);
    }

    /* Align output buffer size so that creating readback buffers doesn't just fail */
    output_buffer_size += 0xffffu;
    output_buffer_size &= ~((size_t)0xffffu);

    memcpy(&query.CommandId, &IID_META_COMMAND_DSTORAGE, sizeof(query.CommandId));
    query.NodeMask = 0;
    query.QueryInputDataSizeInBytes = sizeof(query_in);
    query.pQueryInputData = query_in;
    query.QueryOutputDataSizeInBytes = sizeof(query_out);
    query.pQueryOutputData = query_out;

    memset(query_in, 0, sizeof(query_in));
    query_in[0] = 0x100001u; /* up to 16 streams */
    query_in[2] = 1u;

    hr = ID3D12Device5_CheckFeatureSupport(device5, D3D12_FEATURE_QUERY_META_COMMAND, &query, sizeof(query));

    if (FAILED(hr))
    {
        skip("DirectStorage meta command not supported.\n");
        ID3D12Device5_Release(device5);
        ID3D12Device_Release(device);
        return;
    }

    max_stream_count = query_out[0] >> 16;
    ok(max_stream_count > 0, "Unexpected maximum stream count %u.\n", max_stream_count);

    scratch_buffer_size = query_out[2];

    /* Test invalid create infos */
    memset(create_args, 0, sizeof(create_args));
    create_args[0] = 1u;                /* version */
    create_args[1] = 0u;                /* format */
    create_args[2] = max_stream_count;  /* stream count */

    hr = ID3D12Device5_CreateMetaCommand(device5, &IID_META_COMMAND_DSTORAGE,
            0, create_args, sizeof(create_args), &IID_ID3D12MetaCommand, (void**)&meta_command);
    ok(hr == DXGI_ERROR_UNSUPPORTED, "Unexpected hr %#x.\n", hr);

    create_args[0] = 0u;                /* version */
    create_args[1] = 1u;                /* format */

    hr = ID3D12Device5_CreateMetaCommand(device5, &IID_META_COMMAND_DSTORAGE,
            0, create_args, sizeof(create_args), &IID_ID3D12MetaCommand, (void**)&meta_command);
    ok(hr == DXGI_ERROR_UNSUPPORTED, "Unexpected hr %#x.\n", hr);

    /* Create the actual meta command */
    create_args[0] = 1u;
    create_args[1] = 1u;

    hr = ID3D12Device5_CreateMetaCommand(device5, &IID_META_COMMAND_DSTORAGE,
            0, create_args, sizeof(create_args), &IID_ID3D12MetaCommand, (void**)&meta_command);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    /* Create resources for the actual decompression. */
    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap_desc.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
    heap_desc.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Height = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    /* Control buffer */
    resource_desc.Width = sizeof(control_data);
    hr = ID3D12Device5_CreateCommittedResource(device5, &heap_desc,
            D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            NULL, &IID_ID3D12Resource, (void**)&control_buffer);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    hr = ID3D12Resource_Map(control_buffer, 0, NULL, &control_buffer_ptr);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    /* Input buffer */
    resource_desc.Width = input_buffer_size;
    hr = ID3D12Device5_CreateCommittedResource(device5, &heap_desc,
            D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            NULL, &IID_ID3D12Resource, (void**)&input_buffer);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    hr = ID3D12Resource_Map(input_buffer, 0, NULL, &input_buffer_ptr);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    offset = 0;

    for (i = 0; i < ARRAY_SIZE(compressed_files); i++)
    {
        memcpy((char*)input_buffer_ptr + offset,
                compressed_files[i].data,
                compressed_files[i].compressed_size);

        offset += compressed_files[i].compressed_size;
    }

    /* Scratch buffer, if requested by the implementation */
    memset(&heap_desc, 0, sizeof(heap_desc));
    heap_desc.Type = D3D12_HEAP_TYPE_DEFAULT;

    if (scratch_buffer_size)
    {
        resource_desc.Width = sizeof(scratch_buffer_size);

        hr = ID3D12Device5_CreateCommittedResource(device5, &heap_desc,
                D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                NULL, &IID_ID3D12Resource, (void**)&scratch_buffer);
        ok(hr == S_OK, "Unexpected hr %#x.\n", hr);
    }
    else
        scratch_buffer = NULL;

    /* Output buffer */
    resource_desc.Width = output_buffer_size;
    hr = ID3D12Device5_CreateCommittedResource(device5, &heap_desc,
            D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            NULL, &IID_ID3D12Resource, (void**)&output_buffer);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    /* Set up compute queue and list to do the decompression on */
    memset(&queue_desc, 0u, sizeof(queue_desc));
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

    hr = ID3D12Device5_CreateCommandQueue(device5, &queue_desc, &IID_ID3D12CommandQueue, (void**)&queue);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    hr = ID3D12Device5_CreateCommandAllocator(device5, queue_desc.Type,
            &IID_ID3D12CommandAllocator, (void**)&command_allocator);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    hr = ID3D12Device5_CreateCommandList1(device5, 0, queue_desc.Type,
            D3D12_COMMAND_LIST_FLAG_NONE, &IID_ID3D12GraphicsCommandList4, (void**)&command_list4);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    hr = ID3D12GraphicsCommandList4_QueryInterface(command_list4, &IID_ID3D12GraphicsCommandList, (void**)&command_list);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    /* Initialize meta command. This is expected to be done once after
     * creation, and in case of DirectStorage, does not take any arguments. */
    reset_command_list(command_list, command_allocator);

    ID3D12GraphicsCommandList4_InitializeMetaCommand(command_list4,
            meta_command, NULL, 0);

    uav_barrier(command_list, NULL);

    /* Test decompressing two buffers at once */
    max_stream_count = min(max_stream_count, 2u);

    control_data[0] = max_stream_count;
    control_data[1] = compressed_files[0].in_offset;
    control_data[2] = compressed_files[0].out_offset;
    control_data[3] = compressed_files[1].in_offset;
    control_data[4] = compressed_files[1].out_offset;
    memcpy(control_buffer_ptr, control_data, sizeof(control_data));

    memset(exec_args, 0, sizeof(exec_args));
    exec_args[0] = ID3D12Resource_GetGPUVirtualAddress(input_buffer);
    exec_args[1] = input_buffer_size;
    exec_args[2] = ID3D12Resource_GetGPUVirtualAddress(output_buffer);
    exec_args[3] = output_buffer_size;
    exec_args[4] = ID3D12Resource_GetGPUVirtualAddress(control_buffer);
    exec_args[5] = sizeof(control_data);
    exec_args[6] = scratch_buffer ? ID3D12Resource_GetGPUVirtualAddress(scratch_buffer) : 0;
    exec_args[7] = scratch_buffer_size;
    exec_args[8] = max_stream_count; /* stream count */

    ID3D12GraphicsCommandList4_ExecuteMetaCommand(command_list4,
            meta_command, exec_args, sizeof(exec_args));

    transition_resource_state(command_list, output_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(output_buffer, DXGI_FORMAT_R8_UINT, &rb, queue, command_list);
    verify_hash(rb.data, control_data[2], compressed_files[0].output_size, compressed_files[0].hash);

    if (max_stream_count > 1)
        verify_hash(rb.data, control_data[4], compressed_files[1].output_size, compressed_files[1].hash);

    release_resource_readback(&rb);

    ID3D12GraphicsCommandList4_Release(command_list4);
    ID3D12GraphicsCommandList_Release(command_list);
    ID3D12CommandAllocator_Release(command_allocator);
    ID3D12CommandQueue_Release(queue);

    if (scratch_buffer)
        ID3D12Resource_Release(scratch_buffer);

    ID3D12Resource_Release(input_buffer);
    ID3D12Resource_Release(output_buffer);
    ID3D12Resource_Release(control_buffer);

    ID3D12MetaCommand_Release(meta_command);

    ID3D12Device5_Release(device5);
    refcount = ID3D12Device_Release(device);
    ok(!refcount, "ID3D12Device has %u references left.\n", (unsigned int)refcount);
}
