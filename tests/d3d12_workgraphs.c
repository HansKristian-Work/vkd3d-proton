/*
 * Copyright 2024 Hans-Kristian Arntzen for Valve Corporation
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

struct test_context_workgraph
{
    struct test_context context;
    ID3D12GraphicsCommandList10 *list;
    ID3D12Device5 *device;

    ID3D12RootSignature *default_root_uav_rs;
};

static bool export_strequal(LPCWSTR a, LPCWSTR b)
{
    if (!a || !b)
        return false;

    while (*a != '\0' && *b != '\0')
    {
        if (*a != *b)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

static bool init_workgraph_test_context(struct test_context_workgraph *context)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS21 options21;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_param[2];

    if (!init_compute_test_context(&context->context))
        return false;

    options21.WorkGraphsTier = D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED;

    if (FAILED(ID3D12Device_CheckFeatureSupport(context->context.device, D3D12_FEATURE_D3D12_OPTIONS21, &options21, sizeof(options21))) ||
        options21.WorkGraphsTier != D3D12_WORK_GRAPHS_TIER_1_0)
    {
        skip("Workgraphs not supported.\n");
        destroy_test_context(&context->context);
        return false;
    }

    if (FAILED(ID3D12GraphicsCommandList_QueryInterface(context->context.list, &IID_ID3D12GraphicsCommandList10, (void **)&context->list)))
    {
        skip("GCLExperimental not supported.\n");
        destroy_test_context(&context->context);
        return false;
    }

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_param, 0, sizeof(rs_param));
    rs_desc.NumParameters = ARRAY_SIZE(rs_param);
    rs_desc.pParameters = rs_param;
    rs_param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rs_param[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_param[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rs_param[1].Descriptor.ShaderRegister = 1;
    create_root_signature(context->context.device, &rs_desc, &context->default_root_uav_rs);

    ID3D12Device_QueryInterface(context->context.device, &IID_ID3D12Device5, (void **)&context->device);

    return true;
}

static void destroy_workgraph_test_context(struct test_context_workgraph *context)
{
    ID3D12GraphicsCommandList10_Release(context->list);
    ID3D12Device5_Release(context->device);
    ID3D12RootSignature_Release(context->default_root_uav_rs);
    destroy_test_context(&context->context);
}

#include "shaders/workgraph/headers/basic.h"
#include "shaders/workgraph/headers/broadcast_custom_input_uint2.h"
#include "shaders/workgraph/headers/broadcast_custom_input_uint.h"
#include "shaders/workgraph/headers/broadcast_custom_input_u16_2.h"
#include "shaders/workgraph/headers/broadcast_custom_input.h"
#include "shaders/workgraph/headers/thread_custom_input.h"
#include "shaders/workgraph/headers/coalesce_custom_input.h"

static void check_work_graph_properties(ID3D12StateObject *pso,
        LPCWSTR expected_program_name, LPCWSTR expected_entry_node, LPCWSTR expected_leaf_node,
        unsigned int expected_input_record_size,
        D3D12_PROGRAM_IDENTIFIER *ident, D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS *reqs)
{
    ID3D12StateObjectProperties1 *props1;
    ID3D12WorkGraphProperties *props;
    LPCWSTR program_name;
    D3D12_NODE_ID id;
    UINT num_graphs;
    HRESULT hr;
    UINT index;
    UINT size;

    hr = ID3D12StateObject_QueryInterface(pso, &IID_ID3D12WorkGraphProperties, (void **)&props);
    ok(SUCCEEDED(hr), "Failed to query work graph props, hr #%x.\n", hr);

    if (SUCCEEDED(hr))
    {
        hr = ID3D12StateObject_QueryInterface(pso, &IID_ID3D12StateObjectProperties1, (void **)&props1);
        ok(SUCCEEDED(hr), "Failed to query SOP1.\n");
    }
    else
        return;

    num_graphs = ID3D12WorkGraphProperties_GetNumWorkGraphs(props);
    ok(num_graphs == 1, "Expected 1 graph, got %u.\n", num_graphs);
    program_name = ID3D12WorkGraphProperties_GetProgramName(props, 0);
    ok(export_strequal(program_name, expected_program_name), "Unexpected program name.\n");
    index = ID3D12WorkGraphProperties_GetWorkGraphIndex(props, expected_program_name);
    ok(index == 0, "Unexpected index %u\n", index);
    index = ID3D12WorkGraphProperties_GetWorkGraphIndex(props, u"DummyDoesNotExist");
    ok(index == UINT32_MAX, "Unexpected index %u\n", index);

    id = ID3D12WorkGraphProperties_GetNodeID(props, 0, 0);
    ok(export_strequal(id.Name, expected_entry_node), "Unexpected node name.\n");
    ok(id.ArrayIndex == 0, "Unexpected ArrayIndex %u.\n", id.ArrayIndex);

    if (expected_leaf_node)
    {
        id = ID3D12WorkGraphProperties_GetNodeID(props, 0, 0);
        ok(export_strequal(id.Name, expected_leaf_node), "Unexpected node name.\n");
        ok(id.ArrayIndex == 0, "Unexpected ArrayIndex %u.\n", id.ArrayIndex);
    }

    /* We have a MaxDispatch property, so SV_DispatchGrid is part of the record implicitly. */
    size = ID3D12WorkGraphProperties_GetEntrypointRecordSizeInBytes(props, 0, 0);
    ok(size == expected_input_record_size, "Unexpected size %u.\n", size);

    *ident = ID3D12StateObjectProperties1_GetProgramIdentifier(props1, program_name);
    ok(ident->OpaqueData[0] || ident->OpaqueData[1] || ident->OpaqueData[2] || ident->OpaqueData[3],
        "Program identifier is NULL unexpectedly.\n");

    /* OOB writes 0 size. */
    memset(reqs, 0xff, sizeof(*reqs));
    ID3D12WorkGraphProperties_GetWorkGraphMemoryRequirements(props, 1, reqs);
    ok(reqs->MaxSizeInBytes == 0, "MaxSizeInBytes is not 0.\n");
    ok(reqs->MinSizeInBytes == 0, "MinSizeInBytes is not 0.\n");
    ok(reqs->SizeGranularityInBytes == 0, "Granularity is not 0.\n");

    memset(reqs, 0, sizeof(*reqs));
    ID3D12WorkGraphProperties_GetWorkGraphMemoryRequirements(props, 0, reqs);
    ok(reqs->MaxSizeInBytes >= reqs->MinSizeInBytes, "MinSize > MaxSize\n");

    ID3D12StateObjectProperties1_Release(props1);
    ID3D12WorkGraphProperties_Release(props);
}

static ID3D12StateObject *create_workgraph_pso(
        struct test_context_workgraph *context,
        D3D12_SHADER_BYTECODE code, LPCWSTR program_name,
        ID3D12RootSignature *rs)
{
    D3D12_GLOBAL_ROOT_SIGNATURE grs_desc;
    D3D12_STATE_SUBOBJECT subobjects[3];
    D3D12_STATE_OBJECT_DESC pso_desc;
    D3D12_WORK_GRAPH_DESC wg_desc;
    D3D12_DXIL_LIBRARY_DESC dxil;
    ID3D12StateObject *pso;
    HRESULT hr;

    memset(&pso_desc, 0, sizeof(pso_desc));
    memset(subobjects, 0, sizeof(subobjects));
    memset(&dxil, 0, sizeof(dxil));
    memset(&wg_desc, 0, sizeof(wg_desc));
    pso_desc.NumSubobjects = ARRAY_SIZE(subobjects);
    pso_desc.pSubobjects = subobjects;
    pso_desc.Type = D3D12_STATE_OBJECT_TYPE_EXECUTABLE;
    grs_desc.pGlobalRootSignature = rs;

    subobjects[0].pDesc = &dxil;
    subobjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobjects[1].pDesc = &wg_desc;
    subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_WORK_GRAPH;
    subobjects[2].pDesc = &grs_desc;
    subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;

    dxil.DXILLibrary = code;
    wg_desc.Flags = D3D12_WORK_GRAPH_FLAG_INCLUDE_ALL_AVAILABLE_NODES;
    wg_desc.ProgramName = program_name;

    hr = ID3D12Device5_CreateStateObject(context->device, &pso_desc, &IID_ID3D12StateObject, (void **)&pso);
    ok(SUCCEEDED(hr), "Failed to create state object, hr #%x.\n", hr);
    if (FAILED(hr))
        return NULL;

    return pso;
}

struct workgraph_test_desc
{
    unsigned int entry_point_index;
    unsigned int num_records;
    unsigned int record_stride;
    unsigned int record_size;
    unsigned int num_graph_dispatches;
    unsigned int num_multi_instances;
    D3D12_DISPATCH_MODE mode;
    const void *records;
    unsigned int num_threads[3];
    bool is_bug;

    uint32_t (*expected_cb)(const struct workgraph_test_desc *desc, uint32_t i);
};

static uint32_t basic_expected(const struct workgraph_test_desc *desc, uint32_t value_index)
{
    const uint32_t *records = desc->records;
    uint32_t x, y, z, record_index;
    uint32_t expected = 0;

    x = value_index % 10;
    y = (value_index / 10) % 10;
    z = value_index / 100;

    for (record_index = 0; record_index < desc->num_records; record_index++)
    {
        if (x < records[0] * desc->num_threads[0] &&
            y < records[1] * desc->num_threads[1] &&
            z < records[2] * desc->num_threads[2])
        {
            expected += desc->num_multi_instances * desc->num_graph_dispatches;
        }
        records += desc->record_stride / sizeof(uint32_t);
    }
    return expected;
}

static uint32_t broadcast_input_expected(const struct workgraph_test_desc *desc, uint32_t value_index)
{
    const uint32_t *records = desc->records;
    uint32_t x, y, z, record_index;
    uint32_t expected = 0;

    x = value_index % 10;
    y = (value_index / 10) % 10;
    z = value_index / 100;

    for (record_index = 0; record_index < desc->num_records; record_index++)
    {
        if (x < 3 * desc->num_threads[0] &&
            y < 3 * desc->num_threads[1] &&
            z < 2 * desc->num_threads[2])
        {
            expected += records[0] ^ records[1];
        }
        records += desc->record_stride / sizeof(uint32_t);
    }
    return expected;
}

static uint32_t broadcast_input_uint2_expected(const struct workgraph_test_desc *desc, uint32_t value_index)
{
    const uint32_t *records = desc->records;
    uint32_t x, y, z, record_index;
    uint32_t expected = 0;

    x = value_index % 10;
    y = (value_index / 10) % 10;
    z = value_index / 100;

    for (record_index = 0; record_index < desc->num_records; record_index++)
    {
        if (x < records[1] * desc->num_threads[0] &&
            y < records[2] * desc->num_threads[1] &&
            z < desc->num_threads[2])
        {
            expected += records[0] ^ records[3];
        }
        records += desc->record_stride / sizeof(uint32_t);
    }
    return expected;
}

static uint32_t broadcast_input_uint16x2_expected(const struct workgraph_test_desc *desc, uint32_t value_index)
{
    const uint32_t *records = desc->records;
    uint32_t x, y, z, record_index;
    uint32_t expected = 0;

    x = value_index % 10;
    y = (value_index / 10) % 10;
    z = value_index / 100;

    for (record_index = 0; record_index < desc->num_records; record_index++)
    {
        if (x < (records[1] & 0xffff) * desc->num_threads[0] &&
            y < (records[1] >> 16) * desc->num_threads[1] &&
            z < desc->num_threads[2])
        {
            expected += records[0] ^ records[2];
        }
        records += desc->record_stride / sizeof(uint32_t);
    }
    return expected;
}

static uint32_t broadcast_input_uint_expected(const struct workgraph_test_desc *desc, uint32_t value_index)
{
    const uint32_t *records = desc->records;
    uint32_t x, y, z, record_index;
    uint32_t expected = 0;

    x = value_index % 10;
    y = (value_index / 10) % 10;
    z = value_index / 100;

    for (record_index = 0; record_index < desc->num_records; record_index++)
    {
        if (x < records[1] * desc->num_threads[0] &&
            y < desc->num_threads[1] &&
            z < desc->num_threads[2])
        {
            expected += records[0] ^ records[2];
        }
        records += desc->record_stride / sizeof(uint32_t);
    }
    return expected;
}

static uint32_t thread_input_expected(const struct workgraph_test_desc *desc, uint32_t value_index)
{
    const uint32_t *records = desc->records;
    uint32_t i;

    for (i = 0; i < desc->num_records; i++)
        if (records[2 * i] == value_index)
            return records[2 * i + 1];

    return 0;
}

static uint32_t coalesced_input_expected(const struct workgraph_test_desc *desc, uint32_t value_index)
{
    const uint32_t *records = desc->records;
    uint32_t i;

    for (i = 0; i < desc->num_records; i++)
    {
        uint32_t offset = records[2 * i];
        if (value_index >= offset && value_index < offset + desc->num_threads[0])
            return records[2 * i + 1];
    }

    return 0;
}

static void execute_workgraph_pso_simple(struct test_context_workgraph *context,
        ID3D12StateObject *pso, const D3D12_PROGRAM_IDENTIFIER *ident,
        const D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS *wg_reqs,
        const void *node_payload, size_t node_payload_stride, size_t node_payload_count,
        const D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE *local_root_table,
        ID3D12Resource **scratch_output)
{
    ID3D12Resource *scratch = *scratch_output;
    D3D12_DISPATCH_GRAPH_DESC dispatch_desc;
    D3D12_SET_PROGRAM_DESC program_desc;

    if (wg_reqs->MinSizeInBytes)
    {
        if (!scratch)
        {
            scratch = create_default_buffer(context->context.device, wg_reqs->MinSizeInBytes,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        }
    }
    else
        scratch = NULL;

    memset(&program_desc, 0, sizeof(program_desc));
    program_desc.Type = D3D12_PROGRAM_TYPE_WORK_GRAPH;
    if (scratch)
    {
        program_desc.WorkGraph.BackingMemory.SizeInBytes = wg_reqs->MinSizeInBytes;
        program_desc.WorkGraph.BackingMemory.StartAddress = ID3D12Resource_GetGPUVirtualAddress(scratch);
    }

    if (local_root_table)
        program_desc.WorkGraph.NodeLocalRootArgumentsTable = *local_root_table;

    /* Only needs to be called once after memory has been allocated / clobbered by another graph.
     * Unclear if SetProgram itself initializes the work graph memory,
     * or if that is deferred to DispatchGraph time. */
    program_desc.WorkGraph.Flags = D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE;
    program_desc.WorkGraph.ProgramIdentifier = *ident;
    ID3D12GraphicsCommandList10_SetProgram(context->list, &program_desc);

    dispatch_desc.Mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
    dispatch_desc.NodeCPUInput.EntrypointIndex = 0;
    dispatch_desc.NodeCPUInput.NumRecords = node_payload_count;
    dispatch_desc.NodeCPUInput.pRecords = (void *)node_payload;
    dispatch_desc.NodeCPUInput.RecordStrideInBytes = node_payload_stride;

    ID3D12GraphicsCommandList10_DispatchGraph(context->list, &dispatch_desc);
    *scratch_output = scratch;
}

static void execute_workgraph_test(struct test_context_workgraph *context,
        ID3D12StateObject *pso, const D3D12_PROGRAM_IDENTIFIER *ident,
        const D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS *wg_reqs, const struct workgraph_test_desc *desc)
{
    D3D12_MULTI_NODE_GPU_INPUT multi_gpu_input;
    D3D12_DISPATCH_GRAPH_DESC dispatch_desc;
    D3D12_SET_PROGRAM_DESC program_desc;
    D3D12_NODE_CPU_INPUT node_cpu_input;
    D3D12_NODE_GPU_INPUT node_gpu_input;
    ID3D12Resource *input_payload;
    ID3D12Resource *input_multi;
    struct resource_readback rb;
    ID3D12Resource *scratch;
    ID3D12Resource *output;
    ID3D12Resource *input;
    unsigned int i;

    if (wg_reqs->MinSizeInBytes)
    {
        scratch = create_default_buffer(context->context.device, wg_reqs->MinSizeInBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    }
    else
        scratch = NULL;

    output = create_default_buffer(context->context.device, 1024 * sizeof(uint32_t),
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

    ID3D12GraphicsCommandList_SetComputeRootSignature(
        context->context.list, context->default_root_uav_rs);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context->context.list, 0,
        ID3D12Resource_GetGPUVirtualAddress(output));

    memset(&program_desc, 0, sizeof(program_desc));
    program_desc.Type = D3D12_PROGRAM_TYPE_WORK_GRAPH;
    if (scratch)
    {
        program_desc.WorkGraph.BackingMemory.SizeInBytes = wg_reqs->MinSizeInBytes;
        program_desc.WorkGraph.BackingMemory.StartAddress = ID3D12Resource_GetGPUVirtualAddress(scratch);
    }

    /* Only needs to be called once after memory has been allocated / clobbered by another graph.
     * Unclear if SetProgram itself initializes the work graph memory,
     * or if that is deferred to DispatchGraph time. */
    program_desc.WorkGraph.Flags = D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE;
    program_desc.WorkGraph.ProgramIdentifier = *ident;
    ID3D12GraphicsCommandList10_SetProgram(context->list, &program_desc);

    /* 0 stride to duplicate is allowed. Use this to easily test multi-dispatch. */

    input_payload = create_upload_buffer(context->context.device, desc->record_size, desc->records);
    node_gpu_input.EntrypointIndex = desc->entry_point_index;
    /* Value > 1 breaks on WARP. It's treated as just 1. */
    node_gpu_input.NumRecords = desc->num_records;
    node_gpu_input.Records.StartAddress = ID3D12Resource_GetGPUVirtualAddress(input_payload);
    node_gpu_input.Records.StrideInBytes = desc->record_stride;
    input = create_upload_buffer(context->context.device, sizeof(node_gpu_input), &node_gpu_input);

    multi_gpu_input.NumNodeInputs = desc->num_multi_instances;
    multi_gpu_input.NodeInputs.StartAddress = ID3D12Resource_GetGPUVirtualAddress(input);
    multi_gpu_input.NodeInputs.StrideInBytes = 0;
    input_multi = create_upload_buffer(context->context.device, sizeof(multi_gpu_input), &multi_gpu_input);

    /* Apparently the spec allows for back-to-back dispatch graphs without barrier,
     * even if the scratch buffer is the same. The thing that is explicitly dis-allowed
     * is using same scratch across multiple queues. */
    node_cpu_input.EntrypointIndex = desc->entry_point_index;
    node_cpu_input.NumRecords = desc->num_records;
    node_cpu_input.pRecords = (void *)desc->records;
    node_cpu_input.RecordStrideInBytes = desc->record_stride;

    dispatch_desc.Mode = desc->mode;

    switch (desc->mode)
    {
        case D3D12_DISPATCH_MODE_NODE_CPU_INPUT:
            dispatch_desc.NodeCPUInput = node_cpu_input;
            for (i = 0; i < desc->num_graph_dispatches; i++)
                ID3D12GraphicsCommandList10_DispatchGraph(context->list, &dispatch_desc);
            break;

        case D3D12_DISPATCH_MODE_MULTI_NODE_CPU_INPUT:
            dispatch_desc.MultiNodeCPUInput.pNodeInputs = &node_cpu_input;
            dispatch_desc.MultiNodeCPUInput.NumNodeInputs = desc->num_multi_instances;
            dispatch_desc.MultiNodeCPUInput.NodeInputStrideInBytes = 0;
            for (i = 0; i < desc->num_graph_dispatches; i++)
                ID3D12GraphicsCommandList10_DispatchGraph(context->list, &dispatch_desc);
            break;

        case D3D12_DISPATCH_MODE_NODE_GPU_INPUT:
            dispatch_desc.NodeGPUInput = ID3D12Resource_GetGPUVirtualAddress(input);
            for (i = 0; i < desc->num_graph_dispatches; i++)
                ID3D12GraphicsCommandList10_DispatchGraph(context->list, &dispatch_desc);
            break;

        case D3D12_DISPATCH_MODE_MULTI_NODE_GPU_INPUT:
            dispatch_desc.MultiNodeGPUInput = ID3D12Resource_GetGPUVirtualAddress(input_multi);
            for (i = 0; i < desc->num_graph_dispatches; i++)
                ID3D12GraphicsCommandList10_DispatchGraph(context->list, &dispatch_desc);
            break;

        default:
            break;
    }

    transition_resource_state(context->context.list, output,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(output, DXGI_FORMAT_R32_UINT, &rb,
            context->context.queue, context->context.list);
    reset_command_list(context->context.list, context->context.allocator);

    for (i = 0; i < 1024; i++)
    {
        uint32_t expected;
        uint32_t v;

        expected = desc->expected_cb(desc, i);
        v = get_readback_uint(&rb, i, 0, 0);

        bug_if(desc->is_bug) ok(expected == v, "Value %u: expected %u, got %u.\n", i, expected, v);
    }

    release_resource_readback(&rb);
    if (scratch)
        ID3D12Resource_Release(scratch);
    ID3D12Resource_Release(input_payload);
    ID3D12Resource_Release(input_multi);
    ID3D12Resource_Release(input);
    ID3D12Resource_Release(output);
}

void test_workgraph_basic(void)
{
    D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS wg_reqs;
    struct test_context_workgraph context;
    D3D12_PROGRAM_IDENTIFIER ident;
    ID3D12StateObject *pso;

    if (!init_workgraph_test_context(&context))
        return;

    pso = create_workgraph_pso(&context, basic_dxil, u"Dummy", context.default_root_uav_rs);
    check_work_graph_properties(pso, u"Dummy", u"BroadcastNode", NULL,
        12 /* implied struct with uint3 SV_DispatchGrid*/, &ident, &wg_reqs);

    /* Test various ways to dispatch a basic broadcast node. */

    {
        const uint32_t records[] = { 1, 2, 2, 0, 2, 1, 1, 0 };
        struct workgraph_test_desc desc;
        memset(&desc, 0, sizeof(desc));

        desc.expected_cb = basic_expected;
        desc.mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
        desc.num_graph_dispatches = 3;
        desc.num_multi_instances = 1;
        desc.num_records = 2;
        desc.record_size = sizeof(records);
        desc.record_stride = 0;
        desc.records = records;
        desc.num_threads[0] = 2;
        desc.num_threads[1] = 3;
        desc.num_threads[2] = 4;

        if (is_amd_windows_device(context.context.device))
        {
            skip("Skipping stride 0 test. Hangs AMD GPU.\n");
        }
        else
        {
            vkd3d_test_set_context("CPU Input, stride 0");
            execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
        }
        desc.record_stride = 16;
        vkd3d_test_set_context("CPU Input, stride 16");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
    }

    if (!is_amd_windows_device(context.context.device))
    {
        uint32_t records[] = { 1, 2, 2 };
        struct workgraph_test_desc desc;
        memset(&desc, 0, sizeof(desc));

        desc.expected_cb = basic_expected;
        desc.mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
        desc.num_graph_dispatches = 1;
        desc.num_multi_instances = 1;
        desc.num_records = 0;
        desc.record_size = 12;
        desc.record_stride = 0;
        desc.records = records;
        desc.num_threads[0] = 2;
        desc.num_threads[1] = 3;
        desc.num_threads[2] = 4;

        vkd3d_test_set_context("Empty CPU Input - num records");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
        desc.num_records = 10;
        records[1] = 0;
        vkd3d_test_set_context("Empty CPU Input - empty workgroup");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
    }
    else
    {
        skip("Skipping stride 0 tests. Hangs AMD GPU.\n");
    }

    if (!is_amd_windows_device(context.context.device))
    {
        const uint32_t records[] = { 1, 2, 2, 2, 1, 1 };
        struct workgraph_test_desc desc;
        memset(&desc, 0, sizeof(desc));

        desc.expected_cb = basic_expected;
        desc.mode = D3D12_DISPATCH_MODE_MULTI_NODE_CPU_INPUT;
        desc.num_graph_dispatches = 4;
        desc.num_multi_instances = 3;
        desc.num_records = 2;
        desc.record_size = sizeof(records);
        desc.record_stride = 12;
        desc.records = records;
        desc.num_threads[0] = 2;
        desc.num_threads[1] = 3;
        desc.num_threads[2] = 4;

        vkd3d_test_set_context("Multi CPU Input");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
    }
    else
    {
        skip("Skipping GPU hang on AMD GPU due to stride = 0.\n");
    }

    {
        const uint32_t records[] = { 2, 1, 2, 2, 2, 2, 2, 2, 1 };
        struct workgraph_test_desc desc;
        memset(&desc, 0, sizeof(desc));

        desc.expected_cb = basic_expected;
        desc.mode = D3D12_DISPATCH_MODE_NODE_GPU_INPUT;
        desc.num_graph_dispatches = 4;
        desc.num_multi_instances = 1;
        desc.num_records = 1;
        desc.record_size = sizeof(records);
        desc.record_stride = 0;
        desc.records = records;
        desc.num_threads[0] = 2;
        desc.num_threads[1] = 3;
        desc.num_threads[2] = 4;

        vkd3d_test_set_context("GPU Input - single");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
        desc.num_records = 0;
        vkd3d_test_set_context("GPU Input - empty");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
        desc.num_records = 3;

        /* Bugged on WARP. */
        desc.is_bug = use_warp_device;
        vkd3d_test_set_context("GPU Input - multi");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
        desc.record_stride = 12;
        vkd3d_test_set_context("GPU Input - multi + stride");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
    }

    {
        const uint32_t records[] = { 2, 1, 2, 2, 2, 2, 2, 2, 1 };
        struct workgraph_test_desc desc;
        memset(&desc, 0, sizeof(desc));

        desc.expected_cb = basic_expected;
        desc.mode = D3D12_DISPATCH_MODE_MULTI_NODE_GPU_INPUT;
        desc.num_graph_dispatches = 4;
        desc.num_multi_instances = 2;
        desc.num_records = 1;
        desc.record_size = sizeof(records);
        desc.record_stride = 0;
        desc.records = records;
        desc.num_threads[0] = 2;
        desc.num_threads[1] = 3;
        desc.num_threads[2] = 4;

        vkd3d_test_set_context("Multi GPU Input - single");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
        desc.num_records = 0;
        vkd3d_test_set_context("Multi GPU Input - empty");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
        desc.num_records = 3;

        /* Bugged on WARP. */
        desc.is_bug = use_warp_device;
        vkd3d_test_set_context("Multi GPU Input - multi");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
        desc.record_stride = 12;
        vkd3d_test_set_context("Multi GPU Input - multi + stride");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
    }

    ID3D12StateObject_Release(pso);
    destroy_workgraph_test_context(&context);
}

void test_workgraph_broadcast_input(void)
{
    D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS wg_reqs;
    struct test_context_workgraph context;
    D3D12_PROGRAM_IDENTIFIER ident;
    ID3D12StateObject *pso;

    if (!init_workgraph_test_context(&context))
        return;

    pso = create_workgraph_pso(&context, broadcast_custom_input_dxil, u"Dummy", context.default_root_uav_rs);
    check_work_graph_properties(pso, u"Dummy", u"BroadcastNode", NULL, 8, &ident, &wg_reqs);

    {
        const uint32_t records[] = { 19, 800, 400, 90 };
        struct workgraph_test_desc desc;
        memset(&desc, 0, sizeof(desc));

        desc.expected_cb = broadcast_input_expected;
        desc.mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
        desc.num_graph_dispatches = 1;
        desc.num_multi_instances = 1;
        desc.num_records = 2;
        desc.record_size = sizeof(records);
        desc.record_stride = 8;
        desc.records = records;
        desc.num_threads[0] = 2;
        desc.num_threads[1] = 3;
        desc.num_threads[2] = 4;

        vkd3d_test_set_context("Plain broadcast");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
    }

    ID3D12StateObject_Release(pso);

    /* Proves that the input struct to a broadcast node is interpreted as-is, which will complicate any attempt to
     * do GPU input nodes. 1D and 2D dispatch is supported, which we'd have to massage to indirect 3D dispatch somehow.
     * Raw 16-bit packing can be used too, which complicates things even more ... */

    pso = create_workgraph_pso(&context, broadcast_custom_input_uint2_dxil, u"Dummy", context.default_root_uav_rs);
    check_work_graph_properties(pso, u"Dummy", u"BroadcastNode", NULL, 16, &ident, &wg_reqs);

    {
        const uint32_t records[] = { 19, 3, 2, 800, 400, 2, 1, 90 };
        struct workgraph_test_desc desc;
        memset(&desc, 0, sizeof(desc));

        desc.expected_cb = broadcast_input_uint2_expected;
        desc.mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
        desc.num_graph_dispatches = 1;
        desc.num_multi_instances = 1;
        desc.num_records = 2;
        desc.record_size = sizeof(records);
        desc.record_stride = 16;
        desc.records = records;
        desc.num_threads[0] = 2;
        desc.num_threads[1] = 3;
        desc.num_threads[2] = 4;

        vkd3d_test_set_context("uint2 broadcast");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
    }

    ID3D12StateObject_Release(pso);

    pso = create_workgraph_pso(&context, broadcast_custom_input_uint_dxil, u"Dummy", context.default_root_uav_rs);
    check_work_graph_properties(pso, u"Dummy", u"BroadcastNode", NULL, 12, &ident, &wg_reqs);

    {
        const uint32_t records[] = { 19, 3, 800, 400, 2, 90 };
        struct workgraph_test_desc desc;
        memset(&desc, 0, sizeof(desc));

        desc.expected_cb = broadcast_input_uint_expected;
        desc.mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
        desc.num_graph_dispatches = 1;
        desc.num_multi_instances = 1;
        desc.num_records = 2;
        desc.record_size = sizeof(records);
        desc.record_stride = 12;
        desc.records = records;
        desc.num_threads[0] = 2;
        desc.num_threads[1] = 3;
        desc.num_threads[2] = 4;

        vkd3d_test_set_context("uint broadcast");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
    }

    ID3D12StateObject_Release(pso);

    pso = create_workgraph_pso(&context, broadcast_custom_input_u16_2_dxil, u"Dummy", context.default_root_uav_rs);
    check_work_graph_properties(pso, u"Dummy", u"BroadcastNode", NULL, 12, &ident, &wg_reqs);

    {
        const uint32_t records[] = { 19, 3 | (2 << 16), 800, 400, 2 | (1 << 16), 90};
        struct workgraph_test_desc desc;
        memset(&desc, 0, sizeof(desc));

        desc.expected_cb = broadcast_input_uint16x2_expected;
        desc.mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
        desc.num_graph_dispatches = 1;
        desc.num_multi_instances = 1;
        desc.num_records = 2;
        desc.record_size = sizeof(records);
        desc.record_stride = 12;
        desc.records = records;
        desc.num_threads[0] = 2;
        desc.num_threads[1] = 3;
        desc.num_threads[2] = 4;

        vkd3d_test_set_context("uint16_t broadcast");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
    }

    ID3D12StateObject_Release(pso);

    destroy_workgraph_test_context(&context);
}

void test_workgraph_thread_input(void)
{
    D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS wg_reqs;
    struct test_context_workgraph context;
    D3D12_PROGRAM_IDENTIFIER ident;
    ID3D12StateObject *pso;
    unsigned int i;

    if (!init_workgraph_test_context(&context))
        return;

    pso = create_workgraph_pso(&context, thread_custom_input_dxil, u"Dummy", context.default_root_uav_rs);
    check_work_graph_properties(pso, u"Dummy", u"ThreadNode", NULL, 8, &ident, &wg_reqs);

    {
        struct workgraph_test_desc desc;
        uint32_t records[2 * 256];
        memset(&desc, 0, sizeof(desc));

        for (i = 0; i < ARRAY_SIZE(records) / 2; i++)
        {
            records[2 * i + 0] = (i ^ 7) * 3;
            records[2 * i + 1] = i ^ 0xabcd;
        }

        desc.expected_cb = thread_input_expected;
        desc.mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
        desc.num_graph_dispatches = 1;
        desc.num_multi_instances = 1;
        desc.num_records = ARRAY_SIZE(records) / 2;
        desc.record_size = sizeof(records);
        desc.record_stride = 2 * sizeof(records[0]);
        desc.records = records;

        vkd3d_test_set_context("CPU");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
        vkd3d_test_set_context("GPU");
        desc.mode = D3D12_DISPATCH_MODE_NODE_GPU_INPUT;
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
    }

    ID3D12StateObject_Release(pso);
    destroy_workgraph_test_context(&context);
}

void test_workgraph_coalesced_input(void)
{
    D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS wg_reqs;
    struct test_context_workgraph context;
    D3D12_PROGRAM_IDENTIFIER ident;
    ID3D12StateObject *pso;

    if (!init_workgraph_test_context(&context))
        return;

    pso = create_workgraph_pso(&context, coalesce_custom_input_dxil, u"Dummy", context.default_root_uav_rs);
    check_work_graph_properties(pso, u"Dummy", u"CoalesceNode", NULL, 8, &ident, &wg_reqs);

    {
        static const uint32_t records[] =
        {
            100, 50,
            150, 40,
            200, 79,
            300, 40,
            400, 30,
            490, 20,
            530, 10,
            32, 32,
        };
        struct workgraph_test_desc desc;

        memset(&desc, 0, sizeof(desc));

        desc.expected_cb = coalesced_input_expected;
        desc.mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
        desc.num_graph_dispatches = 1;
        desc.num_multi_instances = 1;
        desc.num_records = ARRAY_SIZE(records) / 2;
        desc.record_size = sizeof(records);
        desc.record_stride = 2 * sizeof(records[0]);
        desc.records = records;
        desc.num_threads[0] = 24;

        vkd3d_test_set_context("CPU");
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
        vkd3d_test_set_context("GPU");
        desc.mode = D3D12_DISPATCH_MODE_NODE_GPU_INPUT;
        execute_workgraph_test(&context, pso, &ident, &wg_reqs, &desc);
    }

    ID3D12StateObject_Release(pso);
    destroy_workgraph_test_context(&context);
}

static void test_workgraph_two_level_broadcast_inner(struct test_context_workgraph *context,
    ID3D12StateObject *pso)
{
    D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS wg_reqs;
    D3D12_PROGRAM_IDENTIFIER ident;
    ID3D12Resource *scratch = NULL;
    struct resource_readback rb;
    ID3D12Resource *output[2];
    unsigned i, j, k;

    struct entry_data
    {
        uint32_t grid;
        uint32_t node_idx;
        uint32_t size;
        uint32_t offset;
        uint32_t increment;
    };

    static const struct entry_data node_data[] = {
        { 2, 0, 1, 1, 1 },
        { 2, 1, 1, 3, 2 },
        { 3, 0, 3, 5, 3 },
        { 1, 1, 3, 4, 4 },
    };

    check_work_graph_properties(pso, u"Dummy", u"EntryNode", NULL, sizeof(struct entry_data), &ident, &wg_reqs);

    ID3D12GraphicsCommandList_SetComputeRootSignature(
        context->context.list, context->default_root_uav_rs);

    for (i = 0; i < ARRAY_SIZE(output); i++)
    {
        output[i] = create_default_buffer(context->context.device, 4 * 1024, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context->context.list, i,
            ID3D12Resource_GetGPUVirtualAddress(output[i]));
    }

    execute_workgraph_pso_simple(context, pso, &ident, &wg_reqs, node_data, sizeof(node_data[0]), ARRAY_SIZE(node_data), NULL, &scratch);

    for (i = 0; i < ARRAY_SIZE(output); i++)
    {
        uint32_t reference_output[1024];
        memset(reference_output, 0, sizeof(reference_output));

        for (j = 0; j < ARRAY_SIZE(node_data); j++)
        {
            if (node_data[j].node_idx != i)
                continue;
            for (k = 0; k < node_data[j].size * node_data[j].grid * 64; k++)
            {
                assert(k + node_data[j].offset < ARRAY_SIZE(reference_output));
                reference_output[k + node_data[j].offset] += node_data[j].increment;
            }
        }

        transition_resource_state(context->context.list, output[i], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_buffer_readback_with_command_list(output[i], DXGI_FORMAT_R32_UINT, &rb, context->context.queue, context->context.list);

        for (j = 0; j < ARRAY_SIZE(reference_output); j++)
        {
            ok(reference_output[j] == get_readback_uint(&rb, j, 0, 0),
                "Buffer %u[%u] = %u, expected %u\n", i, j, get_readback_uint(&rb, j, 0, 0), reference_output[j]);
        }

        release_resource_readback(&rb);
        reset_command_list(context->context.list, context->context.allocator);
    }

    ID3D12Resource_Release(output[0]);
    ID3D12Resource_Release(output[1]);
    ID3D12Resource_Release(scratch);
}

void test_workgraph_two_level_broadcast(void)
{
    struct test_context_workgraph context;
    ID3D12StateObject *pso[4];
    unsigned int i;

#include "shaders/workgraph/headers/two_level_broadcast.h"
#include "shaders/workgraph/headers/two_level_broadcast_node_array.h"
#include "shaders/workgraph/headers/two_level_thread.h"
#include "shaders/workgraph/headers/two_level_thread_node_array.h"

    if (!init_workgraph_test_context(&context))
        return;

    pso[0] = create_workgraph_pso(&context, two_level_broadcast_dxil, u"Dummy", context.default_root_uav_rs);
    pso[1] = create_workgraph_pso(&context, two_level_broadcast_node_array_dxil, u"Dummy", context.default_root_uav_rs);
    pso[2] = create_workgraph_pso(&context, two_level_thread_dxil, u"Dummy", context.default_root_uav_rs);
    pso[3] = create_workgraph_pso(&context, two_level_thread_node_array_dxil, u"Dummy", context.default_root_uav_rs);

    vkd3d_test_set_context("Broadcast - Node");
    test_workgraph_two_level_broadcast_inner(&context, pso[0]);
    vkd3d_test_set_context("Broadcast - NodeArray");
    test_workgraph_two_level_broadcast_inner(&context, pso[1]);
    vkd3d_test_set_context("Thread - Node");
    test_workgraph_two_level_broadcast_inner(&context, pso[2]);
    vkd3d_test_set_context("Thread - NodeArray");
    test_workgraph_two_level_broadcast_inner(&context, pso[3]);

    for (i = 0; i < ARRAY_SIZE(pso); i++)
        ID3D12StateObject_Release(pso[i]);
    destroy_workgraph_test_context(&context);
}

void test_workgraph_two_level_empty(void)
{
    D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS wg_reqs;
    struct test_context_workgraph context;
    D3D12_PROGRAM_IDENTIFIER ident;
    ID3D12Resource *scratch = NULL;
    struct resource_readback rb;
    ID3D12StateObject *pso;
    ID3D12Resource *output;
    unsigned i;

#include "shaders/workgraph/headers/two_level_empty_node.h"

    if (!init_workgraph_test_context(&context))
        return;

    pso = create_workgraph_pso(&context, two_level_empty_node_dxil, u"Dummy", context.default_root_uav_rs);

    check_work_graph_properties(pso, u"Dummy", u"EntryNode", NULL, 0, &ident, &wg_reqs);

    ID3D12GraphicsCommandList_SetComputeRootSignature(context.context.list, context.default_root_uav_rs);
    output = create_default_buffer(context.context.device, 4 * 1024, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.context.list, 0, ID3D12Resource_GetGPUVirtualAddress(output));

    execute_workgraph_pso_simple(&context, pso, &ident, &wg_reqs, NULL, 0, 1, NULL, &scratch);

    transition_resource_state(context.context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output, DXGI_FORMAT_R32_UINT, &rb, context.context.queue, context.context.list);

    {
        static const uint32_t reference[16] = { 5, 5, 4, 6, 4, 6, 3, 2, 3, 2, 9, 7, 9, 7 };

        for (i = 0; i < ARRAY_SIZE(reference); i++)
        {
            uint32_t value = get_readback_uint(&rb, i, 0, 0);
            ok(value == reference[i], "Value %u: expected %u, got %u.\n", i, reference[i], value);
        }
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(scratch);
    ID3D12Resource_Release(output);
    ID3D12StateObject_Release(pso);
    destroy_workgraph_test_context(&context);
}

void test_workgraph_basic_recursion(void)
{
    D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS wg_reqs;
    struct test_context_workgraph context;
    D3D12_PROGRAM_IDENTIFIER ident;
    ID3D12Resource *scratch = NULL;
    struct resource_readback rb;
    ID3D12StateObject *pso;
    ID3D12Resource *output;
    unsigned i;

#include "shaders/workgraph/headers/basic_recursion.h"

    if (!init_workgraph_test_context(&context))
        return;

    pso = create_workgraph_pso(&context, basic_recursion_dxil, u"Dummy", context.default_root_uav_rs);

    check_work_graph_properties(pso, u"Dummy", u"EntryNode", NULL, 0, &ident, &wg_reqs);

    ID3D12GraphicsCommandList_SetComputeRootSignature(context.context.list, context.default_root_uav_rs);
    output = create_default_buffer(context.context.device, 4 * 1024, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.context.list, 0, ID3D12Resource_GetGPUVirtualAddress(output));

    execute_workgraph_pso_simple(&context, pso, &ident, &wg_reqs, NULL, 0, 1, NULL, &scratch);

    transition_resource_state(context.context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output, DXGI_FORMAT_R32_UINT, &rb, context.context.queue, context.context.list);

    {
        static const uint32_t reference[64] = { 0, 7, 13, 17, 25, 34, 46, 59, 71, 76, 68, 47, 23, 7, 1 };

        for (i = 0; i < ARRAY_SIZE(reference); i++)
        {
            uint32_t value = get_readback_uint(&rb, i, 0, 0);
            ok(value == reference[i], "Value %u: expected %u, got %u.\n", i, reference[i], value);
        }
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(scratch);
    ID3D12Resource_Release(output);
    ID3D12StateObject_Release(pso);
    destroy_workgraph_test_context(&context);
}

void test_workgraph_cross_group_sharing(void)
{
    D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS wg_reqs;
    struct test_context_workgraph context;
    D3D12_PROGRAM_IDENTIFIER ident;
    ID3D12Resource *scratch = NULL;
    struct resource_readback rb;
    ID3D12StateObject *pso;
    ID3D12Resource *output;
    unsigned i;

#include "shaders/workgraph/headers/cross_group_sharing.h"

    if (!init_workgraph_test_context(&context))
        return;

    pso = create_workgraph_pso(&context, cross_group_sharing_dxil, u"Dummy", context.default_root_uav_rs);

    check_work_graph_properties(pso, u"Dummy", u"EntryNode", NULL, 0, &ident, &wg_reqs);

    ID3D12GraphicsCommandList_SetComputeRootSignature(context.context.list, context.default_root_uav_rs);
    output = create_default_buffer(context.context.device, 4 * 1024, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.context.list, 0, ID3D12Resource_GetGPUVirtualAddress(output));

    execute_workgraph_pso_simple(&context, pso, &ident, &wg_reqs, NULL, 0, 1, NULL, &scratch);

    transition_resource_state(context.context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output, DXGI_FORMAT_R32_UINT, &rb, context.context.queue, context.context.list);

    {
        static const uint32_t reference[4] = { 512 * 64 + 9, 256 * 64 + 11 };

        for (i = 0; i < ARRAY_SIZE(reference) * 64; i++)
        {
            uint32_t value = get_readback_uint(&rb, i, 0, 0);
            ok(value == reference[i / 64], "Value %u: expected %u, got %u.\n", i, reference[i / 64], value);
        }
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(scratch);
    ID3D12Resource_Release(output);
    ID3D12StateObject_Release(pso);
    destroy_workgraph_test_context(&context);
}

void test_workgraph_shared_inputs(void)
{
    D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS wg_reqs;
    struct test_context_workgraph context;
    D3D12_PROGRAM_IDENTIFIER ident;
    ID3D12Resource *scratch = NULL;
    struct resource_readback rb;
    ID3D12StateObject *pso;
    ID3D12Resource *output;
    unsigned i;

#include "shaders/workgraph/headers/shared_inputs.h"

    if (!init_workgraph_test_context(&context))
        return;

    pso = create_workgraph_pso(&context, shared_inputs_dxil, u"Dummy", context.default_root_uav_rs);

    check_work_graph_properties(pso, u"Dummy", u"EntryNode", NULL, 0, &ident, &wg_reqs);

    ID3D12GraphicsCommandList_SetComputeRootSignature(context.context.list, context.default_root_uav_rs);
    output = create_default_buffer(context.context.device, 4 * 1024, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.context.list, 0, ID3D12Resource_GetGPUVirtualAddress(output));

    execute_workgraph_pso_simple(&context, pso, &ident, &wg_reqs, NULL, 0, 1, NULL, &scratch);

    transition_resource_state(context.context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output, DXGI_FORMAT_R32_UINT, &rb, context.context.queue, context.context.list);

    {
        static const uint32_t reference[4] = { 512 * 9, 256 * 9 };

        for (i = 0; i < ARRAY_SIZE(reference) * 64; i++)
        {
            uint32_t value = get_readback_uint(&rb, i, 0, 0);
            ok(value == reference[i / 64], "Value %u: expected %u, got %u.\n", i, reference[i / 64], value);
        }
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(scratch);
    ID3D12Resource_Release(output);
    ID3D12StateObject_Release(pso);
    destroy_workgraph_test_context(&context);
}

void test_workgraph_local_root_signature(void)
{
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE local_table;
    D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS wg_reqs;
    struct test_context_workgraph context;
    D3D12_PROGRAM_IDENTIFIER ident;
    ID3D12Resource *scratch = NULL;
    struct resource_readback rb;
    ID3D12StateObject *pso;
    ID3D12Resource *output;
    ID3D12Resource *local;

#include "shaders/workgraph/headers/local_root_signature.h"

    struct
    {
        D3D12_GPU_VIRTUAL_ADDRESS va;
        uint32_t const0;
        uint32_t const1;
    } local_data[] = {
        { 0, 30, 20 },
        { 0, 40, 50 },
    };

    if (!init_workgraph_test_context(&context))
        return;

    pso = create_workgraph_pso(&context, local_root_signature_dxil, u"Dummy", context.default_root_uav_rs);

    check_work_graph_properties(pso, u"Dummy", u"EntryNode", NULL, 0, &ident, &wg_reqs);

    ID3D12GraphicsCommandList_SetComputeRootSignature(context.context.list, context.default_root_uav_rs);
    output = create_default_buffer(context.context.device, 4 * 1024, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.context.list, 0, ID3D12Resource_GetGPUVirtualAddress(output));

    local_data[0].va = ID3D12Resource_GetGPUVirtualAddress(output);
    local_data[1].va = local_data[0].va;
    local = create_upload_buffer(context.context.device, sizeof(local_data), local_data);

    local_table.StartAddress = ID3D12Resource_GetGPUVirtualAddress(local);
    local_table.StrideInBytes = sizeof(local_data[0]);
    local_table.SizeInBytes = sizeof(local_data);
    execute_workgraph_pso_simple(&context, pso, &ident, &wg_reqs, NULL, 0, 1, &local_table, &scratch);

    transition_resource_state(context.context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output, DXGI_FORMAT_R32_UINT, &rb, context.context.queue, context.context.list);

    {
        uint32_t value0 = get_readback_uint(&rb, local_data[0].const0, 0, 0);
        uint32_t value1 = get_readback_uint(&rb, local_data[1].const1, 0, 0);
        ok(value0 == local_data[0].const1 * 30, "Expected %u, got %u\n", local_data[0].const1 * 30, value0);
        ok(value1 == local_data[1].const0 * 60, "Expected %u, got %u\n", local_data[1].const0 * 60, value1);
    }

    release_resource_readback(&rb);
    ID3D12Resource_Release(scratch);
    ID3D12Resource_Release(output);
    ID3D12Resource_Release(local);
    ID3D12StateObject_Release(pso);
    destroy_workgraph_test_context(&context);
}
