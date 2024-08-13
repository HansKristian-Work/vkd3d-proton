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
#include "vkd3d_private.h"
#include "vkd3d_string.h"

#define WG_TRACE TRACE

#define MAX_WORKGRAPH_LEVELS 32
#define WG_DIVIDER (32u * 1024u)

/* 64 bytes per node, nicely aligns to a cache line. */
struct d3d12_workgraph_indirect_command
{
    uint32_t primary_execute[3];
    uint32_t primary_linear_offset; /* Read by node as input metadata. */
    uint32_t secondary_execute[3];
    uint32_t secondary_linear_offset; /* Read by node as input metadata. */
    uint32_t expander_execute[3];
    uint32_t end_elements; /* Read by node as input metadata in coalesce / thread mode. */
    uint32_t linear_offset_atomic; /* Used by expander to write unrolled data. */
    uint32_t total_fused_elements;
    uint32_t padding[2];
};

struct d3d12_workgraph_node_shared_execution
{
    unsigned int node_pipeline_index;
    unsigned int node_payload_index;
};

struct d3d12_workgraph_level_execution
{
    unsigned int *nodes;
    size_t nodes_size;
    size_t nodes_count;

    struct d3d12_workgraph_node_shared_execution *shared_nodes;
    size_t shared_nodes_size;
    size_t shared_nodes_count;
};

struct d3d12_wg_state_object_pipeline
{
    /* This matters for entry point, otherwise, not. */
    VkPipeline vk_cpu_node_entry_pipeline;
    VkPipeline vk_gpu_node_entry_pipeline;
    VkPipeline vk_non_entry_pipeline;
    /* layout is part of modules array */

    /* Meta shader. distribute_payload_offsets.comp. This is specialized per node. */
    struct vkd3d_workgraph_meta_pipeline_info payload_offset_expander;

    /* These can be overridden per graph in theory. */
    const WCHAR *name;
    UINT array_index;
};

/* Many similarities to ray-tracing state objects, but the implementations are vastly different
 * to the point that there will just be more of a headache to try merging the two implementations into one.
 * This object represents a single work graph. A state object may hold many work graphs. */
struct d3d12_wg_state_object_program
{
    const WCHAR *name;
    /* Need to add nodes with all the overrides as well. */
    struct d3d12_workgraph_level_execution levels[MAX_WORKGRAPH_LEVELS];
    unsigned int num_levels;

    struct d3d12_wg_state_object_pipeline *pipelines;
    /* size is equal to number of entry points. */
    unsigned int num_pipelines;

    /* Meta shader. distribute_workgroups.comp */
    struct vkd3d_workgraph_meta_pipeline_info workgroup_distributor;
    struct vkd3d_workgraph_meta_pipeline_info gpu_input_setup;

    VkDeviceSize counters_scratch_offset;
    VkDeviceSize counters_scratch_size;
    VkDeviceSize indirect_commands_scratch_offset;
    VkDeviceSize indirect_commands_scratch_size;
    VkDeviceSize dividers_scratch_offset;
    VkDeviceSize dividers_scratch_size;
    VkDeviceSize required_scratch_size;
};

struct d3d12_wg_state_object_module
{
    VkShaderModule vk_module;
    VkDescriptorSetLayout vk_set_layout;
    VkPipelineLayout vk_pipeline_layout;
    struct d3d12_root_signature *root_signature;
    uint32_t push_set_index;
};

struct d3d12_wg_state_object_data
{
    struct d3d12_wg_state_object_program *programs;
    size_t programs_size;
    size_t programs_count;

    struct d3d12_state_object_association *associations;
    size_t associations_size;
    size_t associations_count;

    /* Used to finalize compilation later. */
    const struct D3D12_DXIL_LIBRARY_DESC **dxil_libraries;
    size_t dxil_libraries_size;
    size_t dxil_libraries_count;

    /* Map 1:1 with VkShaderModule. */
    struct vkd3d_shader_library_entry_point *entry_points;
    size_t entry_points_size;
    size_t entry_points_count;

    struct vkd3d_shader_library_subobject *subobjects;
    size_t subobjects_size;
    size_t subobjects_count;
};

static void d3d12_state_object_free_programs(
        struct d3d12_wg_state_object_program *programs, size_t count, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    size_t i, j;
    for (i = 0; i < count; i++)
    {
        vkd3d_free((void *)programs[i].name);
        for (j = 0; j < programs[i].num_levels; j++)
        {
            vkd3d_free(programs[i].levels[j].nodes);
            vkd3d_free(programs[i].levels[j].shared_nodes);
        }

        for (j = 0; j < programs[i].num_pipelines; j++)
        {
            VK_CALL(vkDestroyPipeline(device->vk_device, programs[i].pipelines[j].vk_non_entry_pipeline, NULL));
            VK_CALL(vkDestroyPipeline(device->vk_device, programs[i].pipelines[j].vk_cpu_node_entry_pipeline, NULL));
            VK_CALL(vkDestroyPipeline(device->vk_device, programs[i].pipelines[j].vk_gpu_node_entry_pipeline, NULL));
            vkd3d_free((void *)programs[i].pipelines[j].name);
        }
        vkd3d_free(programs[i].pipelines);
    }
    vkd3d_free(programs);
}

static void d3d12_wg_state_object_cleanup_data(
        struct d3d12_wg_state_object_data *data, struct d3d12_device *device)
{
    size_t i;

    d3d12_state_object_free_programs(data->programs, data->programs_count, device);

    vkd3d_shader_dxil_free_library_entry_points(data->entry_points, data->entry_points_count);
    vkd3d_shader_dxil_free_library_subobjects(data->subobjects, data->subobjects_count);
    vkd3d_free(data->dxil_libraries);

    for (i = 0; i < data->associations_count; i++)
    {
        if ((data->associations[i].kind == VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE ||
                data->associations[i].kind == VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE) &&
                data->associations[i].root_signature)
        {
            d3d12_root_signature_dec_ref(data->associations[i].root_signature);
        }

        vkd3d_free((void*)data->associations[i].export);
    }
    vkd3d_free(data->associations);
}

static HRESULT d3d12_wg_state_object_parse_subobject(
        struct d3d12_wg_state_object_data *data, struct d3d12_device *device,
        const D3D12_STATE_SUBOBJECT *obj, unsigned int association_priority)
{
    switch (obj->Type)
    {
        case D3D12_STATE_SUBOBJECT_TYPE_WORK_GRAPH:
        {
            const D3D12_WORK_GRAPH_DESC *wg_desc = obj->pDesc;
            struct d3d12_wg_state_object_program *program;

            if (wg_desc->NumEntrypoints != 0 || wg_desc->NumExplicitlyDefinedNodes != 0)
            {
                FIXME("Explicitly stated entry points is not supported.\n");
                return E_NOTIMPL;
            }

            if (wg_desc->Flags != D3D12_WORK_GRAPH_FLAG_INCLUDE_ALL_AVAILABLE_NODES)
            {
                FIXME("Only INCLUDE_ALL_AVAILABLE_NODES mode is supported.\n");
                return E_NOTIMPL;
            }

            vkd3d_array_reserve((void **)&data->programs, &data->programs_size,
                    data->programs_count + 1, sizeof(*data->programs));
            program = &data->programs[data->programs_count++];
            memset(program, 0, sizeof(*program));
            program->name = vkd3d_wstrdup(wg_desc->ProgramName);
            break;
        }

        case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
        case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
        {
            /* LOCAL_ROOT_SIGNATURE and GLOBAL_ROOT_SIGNATURE alias. */
            const D3D12_LOCAL_ROOT_SIGNATURE *rs = obj->pDesc;

            /* This is only chosen as default if there is nothing else.
             * Conflicting definitions seem to cause runtime to choose something
             * arbitrary. Just override the low priority default.
             * A high priority default association takes precedence if it exists. */
            vkd3d_array_reserve((void **)&data->associations, &data->associations_size,
                    data->associations_count + 1,
                    sizeof(*data->associations));

            /* Root signatures being exported to NULL takes priority as the default local RS.
             * They do however, take precedence over DXIL exported subobjects ... */
            data->associations[data->associations_count].export = NULL;
            data->associations[data->associations_count].kind =
                    obj->Type == D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE ?
                            VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE :
                            VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE;
            data->associations[data->associations_count].root_signature =
                    impl_from_ID3D12RootSignature(rs->pLocalRootSignature);
            data->associations[data->associations_count].priority = association_priority;

            /* Hold reference in case we need to duplicate the data structure due to compile defer. */
            if (data->associations[data->associations_count].root_signature)
                d3d12_root_signature_inc_ref(data->associations[data->associations_count].root_signature);
            data->associations_count++;
            break;
        }

        case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
        {
            const D3D12_DXIL_LIBRARY_DESC *lib = obj->pDesc;
            if (vkd3d_shader_dxil_append_library_entry_points_and_subobjects(lib, data->dxil_libraries_count,
                    &data->entry_points, &data->entry_points_size,
                    &data->entry_points_count,
                    &data->subobjects, &data->subobjects_size,
                    &data->subobjects_count) != VKD3D_OK)
            {
                ERR("Failed to parse DXIL library.\n");
                return E_OUTOFMEMORY;
            }
            vkd3d_array_reserve((void**)&data->dxil_libraries, &data->dxil_libraries_size,
                    data->dxil_libraries_count + 1, sizeof(*data->dxil_libraries));
            data->dxil_libraries[data->dxil_libraries_count++] = lib;
            break;
        }

        default:
            FIXME("Unimplemented workgraph subobject type %u.\n", obj->Type);
            return E_NOTIMPL;
    }

    return S_OK;
}

static uint32_t d3d12_work_graph_find_node_by_id(
        const struct vkd3d_shader_library_entry_point *entries,
        size_t entry_count, const char *node_id, UINT node_array_index)
{
    size_t i;

    for (i = 0; i < entry_count; i++)
    {
        if (entries[i].node_input &&
                entries[i].node_input->node_array_index == node_array_index &&
                strcmp(entries[i].node_input->node_id, node_id) == 0)
        {
            return (uint32_t)i;
        }
    }

    return UINT32_MAX;
}

static HRESULT d3d12_wg_state_object_program_add_node_to_level(
        struct d3d12_wg_state_object_program *program,
        struct d3d12_wg_state_object_data *data,
        unsigned int entry_point_index,
        unsigned int level,
        bool is_recursing);

static HRESULT d3d12_wg_state_object_program_add_outputs_to_level(
        struct d3d12_wg_state_object_program *program,
        struct d3d12_wg_state_object_data *data,
        unsigned int entry_point_index,
        unsigned int level)
{
    const struct vkd3d_shader_node_output_data *output;
    struct vkd3d_shader_library_entry_point *entry;
    uint32_t node_array_size, node_index;
    unsigned int i, j;
    HRESULT hr;

    entry = &data->entry_points[entry_point_index];

    for (i = 0; i < entry->node_outputs_count; i++)
    {
        output = &entry->node_outputs[i];

        /* Checked on the outside for now as well. */
        if (output->node_array_size == UINT32_MAX)
        {
            FIXME("Unbounded array size is not supported.\n");
            return E_NOTIMPL;
        }

        node_array_size = output->node_array_size ? output->node_array_size : 1;

        for (j = 0; j < node_array_size; j++)
        {
            node_index = d3d12_work_graph_find_node_by_id(
                    data->entry_points, data->entry_points_count,
                    output->node_id, output->node_array_index + j);

            if (node_index == UINT32_MAX)
            {
                /* It's okay if we don't find the input in sparse mode. */
                if (output->sparse_array)
                    continue;
                return E_INVALIDARG;
            }

            if (FAILED(hr = d3d12_wg_state_object_program_add_node_to_level(
                    program, data, node_index, level, false)))
            {
                return hr;
            }
        }
    }

    return S_OK;
}

static HRESULT d3d12_wg_state_object_program_add_node_to_level(
        struct d3d12_wg_state_object_program *program,
        struct d3d12_wg_state_object_data *data,
        unsigned int entry_point_index,
        unsigned int level,
        bool is_recursing)
{
    struct vkd3d_shader_library_entry_point *entry;
    struct d3d12_workgraph_level_execution *exec;
    unsigned int i, j;
    HRESULT hr;

    if (level >= MAX_WORKGRAPH_LEVELS)
    {
        FIXME("Recursion factor is too deep.\n");
        return E_INVALIDARG;
    }

    entry = &data->entry_points[entry_point_index];

    exec = &program->levels[level];
    for (i = 0; i < exec->nodes_count; i++)
        if (exec->nodes[i] == entry_point_index)
            return S_OK;

    vkd3d_array_reserve((void **)&exec->nodes, &exec->nodes_size,
            exec->nodes_count + 1, sizeof(*exec->nodes));
    exec->nodes[exec->nodes_count++] = entry_point_index;
    program->num_levels = max(program->num_levels, level + 1);

    if (!is_recursing)
    {
        for (i = 0; i < entry->node_input->recursion_factor; i++)
        {
            if (FAILED(hr = d3d12_wg_state_object_program_add_node_to_level(
                    program, data, entry_point_index,
                    level + 1 + i, true)))
            {
                return hr;
            }
        }
    }

    if (FAILED(hr = d3d12_wg_state_object_program_add_outputs_to_level(program, data, entry_point_index, level + 1)))
        return hr;

    for (i = 0; i < data->entry_points_count; i++)
    {
        /* Fish for shared inputs. */
        const struct vkd3d_shader_node_input_data *candidate = data->entry_points[i].node_input;
        if (!candidate)
            continue;

        if (candidate->node_share_input_id &&
                strcmp(candidate->node_share_input_id, entry->node_input->node_id) == 0 &&
                candidate->node_share_input_array_index == entry->node_input->node_array_index)
        {
            for (j = 0; j < exec->shared_nodes_count; j++)
                if (exec->shared_nodes[j].node_pipeline_index == i)
                    break;

            if (j == exec->shared_nodes_count)
            {
                vkd3d_array_reserve((void **)&exec->shared_nodes, &exec->shared_nodes_size,
                        exec->shared_nodes_count + 1, sizeof(*exec->shared_nodes));
                exec->shared_nodes[exec->shared_nodes_count].node_pipeline_index = i;
                exec->shared_nodes[exec->shared_nodes_count].node_payload_index = entry_point_index;
                exec->shared_nodes_count++;

                /* A shared input node may also have its own outputs. */
                if (FAILED(hr = d3d12_wg_state_object_program_add_outputs_to_level(program, data, i, level + 1)))
                    return hr;
            }
        }
    }

    return S_OK;
}

static HRESULT d3d12_wg_state_object_resolve_entry_points(struct d3d12_wg_state_object *object,
        struct d3d12_wg_state_object_data *data,
        struct d3d12_wg_state_object_program *program)
{
    /* For now, we include everything.
     * For implicitly included nodes, the core rule is that if a node is used as an input it's not promoted to an entry point.
     * A node which is not referenced by any other is implicitly promoted to an entry point.
     * For now, we don't have meta-override available, so we'll assert that the node is set up properly. */
    bool *node_is_output_target = NULL;
    HRESULT hr = S_OK;
    size_t i, j, k;

    node_is_output_target = vkd3d_calloc(data->entry_points_count, sizeof(*node_is_output_target));
    for (i = 0; i < data->entry_points_count; i++)
    {
        for (j = 0; j < data->entry_points[i].node_outputs_count; j++)
        {
            const struct vkd3d_shader_node_output_data *output = &data->entry_points[i].node_outputs[j];
            uint32_t node_array_size;
            uint32_t node_index;

            if (output->node_array_size == UINT32_MAX)
            {
                /* This will be tricky ... We'll just have to deduce the array size late based on what exists in the graph. */
                FIXME("Cannot deal with unbounded node arrays yet.\n");
                hr = E_NOTIMPL;
                goto fail;
            }

            node_array_size = output->node_array_size ? output->node_array_size : 1;

            for (k = 0; k < node_array_size; k++)
            {
                node_index = d3d12_work_graph_find_node_by_id(
                        data->entry_points, data->entry_points_count,
                        output->node_id, output->node_array_index + k);

                if (node_index == UINT32_MAX)
                {
                    /* It's okay if we don't find the input in sparse mode. */
                    if (!output->sparse_array)
                    {
                        FIXME("NodeID %s[%u] was not found.\n", output->node_id, output->node_array_index + k);
                        hr = E_INVALIDARG;
                        goto fail;
                    }
                }

                node_is_output_target[node_index] = true;
            }
        }
    }

    /* Look at shared inputs. If a node has shared inputs with another node, and that node is output target,
     * inherit the output state. */
    for (i = 0; i < data->entry_points_count; i++)
    {
        const struct vkd3d_shader_node_input_data *node_input = data->entry_points[i].node_input;
        uint32_t node_index;

        if (!node_input)
            continue;
        if (!node_input->node_share_input_id || *node_input->node_share_input_id == '\0')
            continue;

        node_index = d3d12_work_graph_find_node_by_id(
                data->entry_points, data->entry_points_count,
                node_input->node_share_input_id, node_input->node_share_input_array_index);

        /* If we don't find the node, assume there is no sharing to care about anyway, so *shrug*. */
        if (node_index != UINT32_MAX)
            node_is_output_target[i] = node_is_output_target[node_index];
    }

    /* We cannot override this state yet, so verify shader matches our expectation. */
    for (i = 0; i < data->entry_points_count; i++)
    {
        const struct vkd3d_shader_node_input_data *node_input = data->entry_points[i].node_input;
        if (!node_input)
            continue;

        if (node_is_output_target[i] && node_input->is_program_entry)
        {
            FIXME("Node %s[%u] was marked as entry point, but it is used as node output.\n",
                    node_input->node_id, node_input->node_array_index);
            hr = E_NOTIMPL;
            goto fail;
        }
        else if (!node_is_output_target[i] && !node_input->is_program_entry)
        {
            FIXME("Node %s[%u] was not marked as entry point, but it is used as entry point.\n",
                    node_input->node_id, node_input->node_array_index);
            hr = E_NOTIMPL;
            goto fail;
        }

        /* Recurse through the nodes. Start with entry point and fill in any dependencies. */
        if (node_input->is_program_entry)
            if (FAILED(hr = d3d12_wg_state_object_program_add_node_to_level(program, data, i, 0, false)))
                goto fail;
    }

fail:
    vkd3d_free(node_is_output_target);
    return hr;
}

static HRESULT d3d12_wg_state_object_parse_subobjects(
        struct d3d12_wg_state_object_data *data, struct d3d12_device *device,
        const D3D12_STATE_OBJECT_DESC *desc)
{
    unsigned int i;
    HRESULT hr;

    for (i = 0; i < desc->NumSubobjects; i++)
    {
        if (FAILED(hr = d3d12_wg_state_object_parse_subobject(data, device, &desc->pSubobjects[i],
                VKD3D_ASSOCIATION_PRIORITY_DECLARED_STATE_OBJECT)))
            return hr;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_QueryInterface(ID3D12StateObject *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(riid, &IID_ID3D12StateObject)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12StateObject_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3D12StateObjectProperties) || IsEqualGUID(riid, &IID_ID3D12StateObjectProperties1))
    {
        struct d3d12_wg_state_object *state_object = wg_impl_from_ID3D12StateObject(iface);
        ID3D12StateObjectProperties1_AddRef(&state_object->ID3D12StateObjectProperties1_iface);
        *object = &state_object->ID3D12StateObjectProperties1_iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3D12WorkGraphProperties))
    {
        struct d3d12_wg_state_object *state_object = wg_impl_from_ID3D12StateObject(iface);
        ID3D12WorkGraphProperties_AddRef(&state_object->ID3D12WorkGraphProperties_iface);
        *object = &state_object->ID3D12WorkGraphProperties_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static inline struct d3d12_wg_state_object *impl_from_ID3D12StateObjectProperties(d3d12_state_object_properties_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_wg_state_object, ID3D12StateObjectProperties1_iface);
}

static inline struct d3d12_wg_state_object *impl_from_ID3D12WorkGraphProperties(d3d12_work_graph_properties_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_wg_state_object, ID3D12WorkGraphProperties_iface);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_properties_QueryInterface(d3d12_state_object_properties_iface *iface,
        REFIID riid, void **object)
{
    struct d3d12_wg_state_object *state_object = impl_from_ID3D12StateObjectProperties(iface);
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);
    return d3d12_state_object_QueryInterface(&state_object->ID3D12StateObject_iface, riid, object);
}

static HRESULT STDMETHODCALLTYPE d3d12_work_graph_properties_QueryInterface(d3d12_work_graph_properties_iface *iface,
        REFIID riid, void **object)
{
    struct d3d12_wg_state_object *state_object = impl_from_ID3D12WorkGraphProperties(iface);
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);
    return d3d12_state_object_QueryInterface(&state_object->ID3D12StateObject_iface, riid, object);
}

static ULONG STDMETHODCALLTYPE d3d12_state_object_AddRef(ID3D12StateObject *iface)
{
    struct d3d12_wg_state_object *state_object = wg_impl_from_ID3D12StateObject(iface);
    ULONG refcount = InterlockedIncrement(&state_object->refcount);

    TRACE("%p increasing refcount to %u.\n", state_object, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_state_object_properties_AddRef(d3d12_state_object_properties_iface *iface)
{
    struct d3d12_wg_state_object *state_object = impl_from_ID3D12StateObjectProperties(iface);
    ULONG refcount = InterlockedIncrement(&state_object->refcount);

    TRACE("%p increasing refcount to %u.\n", state_object, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_work_graph_properties_AddRef(d3d12_work_graph_properties_iface *iface)
{
    struct d3d12_wg_state_object *state_object = impl_from_ID3D12WorkGraphProperties(iface);
    ULONG refcount = InterlockedIncrement(&state_object->refcount);

    TRACE("%p increasing refcount to %u.\n", state_object, refcount);

    return refcount;
}

static inline void d3d12_state_object_inc_ref(struct d3d12_wg_state_object *state_object)
{
    InterlockedIncrement(&state_object->internal_refcount);
}

static HRESULT d3d12_state_object_allocate_ring(struct d3d12_wg_state_object_ring *ring, VkDeviceSize size,
        struct d3d12_device *device)
{
    HRESULT hr;
    if (FAILED(hr = vkd3d_create_buffer_explicit_usage(device,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            size, "workgraph-ring", &ring->vk_buffer)))
    {
        return hr;
    }

    if (FAILED(hr = vkd3d_allocate_internal_buffer_memory(device, ring->vk_buffer,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &ring->allocation)))
    {
        return hr;
    }

    ring->va = vkd3d_get_buffer_device_address(device, ring->vk_buffer);
    return S_OK;
}

static void d3d12_state_object_cleanup_allocation(struct d3d12_wg_state_object_ring *ring, struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    if (ring->allocation.vk_memory)
        vkd3d_free_device_memory(device, &ring->allocation);
    VK_CALL(vkDestroyBuffer(device->vk_device, ring->vk_buffer, NULL));
}

static void d3d12_state_object_cleanup(struct d3d12_wg_state_object *state_object)
{
    const struct vkd3d_vk_device_procs *vk_procs = &state_object->device->vk_procs;
    unsigned int i;

    d3d12_state_object_free_programs(state_object->programs, state_object->programs_count, state_object->device);
    vkd3d_shader_dxil_free_library_entry_points(state_object->entry_points, state_object->entry_points_count);

    for (i = 0; i < state_object->modules_count; i++)
    {
        VK_CALL(vkDestroyShaderModule(state_object->device->vk_device, state_object->modules[i].vk_module, NULL));
        VK_CALL(vkDestroyDescriptorSetLayout(state_object->device->vk_device, state_object->modules[i].vk_set_layout, NULL));
        VK_CALL(vkDestroyPipelineLayout(state_object->device->vk_device, state_object->modules[i].vk_pipeline_layout, NULL));
        if (state_object->modules[i].root_signature)
            d3d12_root_signature_dec_ref(state_object->modules[i].root_signature);
    }
    vkd3d_free(state_object->modules);
    vkd3d_free(state_object->coalesce_dividers);

    d3d12_state_object_cleanup_allocation(&state_object->payload[0], state_object->device);
    d3d12_state_object_cleanup_allocation(&state_object->payload[1], state_object->device);
    d3d12_state_object_cleanup_allocation(&state_object->unrolled_offsets, state_object->device);
}

static void d3d12_state_object_dec_ref(struct d3d12_wg_state_object *state_object)
{
    ULONG refcount = InterlockedDecrement(&state_object->internal_refcount);

    TRACE("%p decreasing internal refcount to %u.\n", state_object, refcount);

    if (!refcount)
    {
        struct d3d12_device *device = state_object->device;
        vkd3d_private_store_destroy(&state_object->private_store);
        d3d12_state_object_cleanup(state_object);
        vkd3d_free(state_object);
        d3d12_device_release(device);
    }
}

static ULONG d3d12_state_object_release(struct d3d12_wg_state_object *state_object)
{
    ULONG refcount = InterlockedDecrement(&state_object->refcount);

    TRACE("%p decreasing refcount to %u.\n", state_object, refcount);

    if (!refcount)
        d3d12_state_object_dec_ref(state_object);
    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_state_object_Release(ID3D12StateObject *iface)
{
    struct d3d12_wg_state_object *state_object = wg_impl_from_ID3D12StateObject(iface);
    return d3d12_state_object_release(state_object);
}

static ULONG STDMETHODCALLTYPE d3d12_state_object_properties_Release(d3d12_state_object_properties_iface *iface)
{
    struct d3d12_wg_state_object *state_object = impl_from_ID3D12StateObjectProperties(iface);
    return d3d12_state_object_release(state_object);
}

static ULONG STDMETHODCALLTYPE d3d12_work_graph_properties_Release(d3d12_work_graph_properties_iface *iface)
{
    struct d3d12_wg_state_object *state_object = impl_from_ID3D12WorkGraphProperties(iface);
    return d3d12_state_object_release(state_object);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_GetPrivateData(ID3D12StateObject *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_wg_state_object *state_object = wg_impl_from_ID3D12StateObject(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&state_object->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_SetPrivateData(ID3D12StateObject *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_wg_state_object *state_object = wg_impl_from_ID3D12StateObject(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&state_object->private_store, guid, data_size, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_SetPrivateDataInterface(ID3D12StateObject *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_wg_state_object *state_object = wg_impl_from_ID3D12StateObject(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&state_object->private_store, guid, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_state_object_GetDevice(ID3D12StateObject *iface,
        REFIID iid, void **device)
{
    struct d3d12_wg_state_object *state_object = wg_impl_from_ID3D12StateObject(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(state_object->device, iid, device);
}

static CONST_VTBL struct ID3D12StateObjectVtbl d3d12_state_object_vtbl =
{
    /* IUnknown methods */
    d3d12_state_object_QueryInterface,
    d3d12_state_object_AddRef,
    d3d12_state_object_Release,
    /* ID3D12Object methods */
    d3d12_state_object_GetPrivateData,
    d3d12_state_object_SetPrivateData,
    d3d12_state_object_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_state_object_GetDevice,
};

/* These only apply to RT PSOs. */
static void * STDMETHODCALLTYPE d3d12_state_object_properties_GetShaderIdentifier(d3d12_state_object_properties_iface *iface,
        LPCWSTR export_name)
{
    TRACE("iface %p, export_name %s.\n", iface, debugstr_w(export_name));
    return NULL;
}

static UINT64 STDMETHODCALLTYPE d3d12_state_object_properties_GetShaderStackSize(d3d12_state_object_properties_iface *iface,
        LPCWSTR export_name)
{
    TRACE("iface %p, export_name %s.\n", iface, debugstr_w(export_name));
    return UINT32_MAX;
}

static UINT64 STDMETHODCALLTYPE d3d12_state_object_properties_GetPipelineStackSize(d3d12_state_object_properties_iface *iface)
{
    TRACE("iface %p\n", iface);
    return UINT32_MAX;
}

static void STDMETHODCALLTYPE d3d12_state_object_properties_SetPipelineStackSize(d3d12_state_object_properties_iface *iface,
        UINT64 stack_size_in_bytes)
{
    TRACE("iface %p, stack_size_in_bytes %llu!\n", iface, (unsigned long long)stack_size_in_bytes);
}

static D3D12_PROGRAM_IDENTIFIER * STDMETHODCALLTYPE d3d12_state_object_properties_GetProgramIdentifier(
        d3d12_state_object_properties_iface *iface,
        D3D12_PROGRAM_IDENTIFIER *ret,
        LPCWSTR pProgramName)
{
    struct d3d12_wg_state_object *state_object = impl_from_ID3D12StateObjectProperties(iface);
    size_t i;

    TRACE("iface %p, ret %p, pProgramName %s!\n", iface, ret, debugstr_w(pProgramName));

    memset(ret, 0, sizeof(*ret));
    for (i = 0; i < state_object->programs_count; i++)
    {
        if (vkd3d_export_strequal(state_object->programs[i].name, pProgramName))
        {
            ret->OpaqueData[0] = i;
            ret->OpaqueData[1] = (UINT64)state_object; /* Allows sanity checking. */
            break;
        }
    }

    return ret;
}

static CONST_VTBL struct ID3D12StateObjectProperties1Vtbl d3d12_state_object_properties_vtbl =
{
    /* IUnknown methods */
    d3d12_state_object_properties_QueryInterface,
    d3d12_state_object_properties_AddRef,
    d3d12_state_object_properties_Release,
    /* ID3D12StateObjectProperties methods */
    d3d12_state_object_properties_GetShaderIdentifier,
    d3d12_state_object_properties_GetShaderStackSize,
    d3d12_state_object_properties_GetPipelineStackSize,
    d3d12_state_object_properties_SetPipelineStackSize,
    /* ID3D12StateObjectProperties1 methods */
    d3d12_state_object_properties_GetProgramIdentifier,
};

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetNumWorkGraphs(
        d3d12_work_graph_properties_iface *iface)
{
    struct d3d12_wg_state_object *object = impl_from_ID3D12WorkGraphProperties(iface);
    TRACE("iface %p\n", iface);
    return object->programs_count;
}

static LPCWSTR STDMETHODCALLTYPE d3d12_work_graph_properties_GetProgramName(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex)
{
    struct d3d12_wg_state_object *object = impl_from_ID3D12WorkGraphProperties(iface);
    TRACE("iface %p, WorkGraphIndex %u\n", iface, WorkGraphIndex);
    if (WorkGraphIndex >= object->programs_count)
    {
        ERR("WorkGraphIndex %u is out of bound.\n", WorkGraphIndex);
        return NULL;
    }

    return object->programs[WorkGraphIndex].name;
}

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetWorkGraphIndex(
        d3d12_work_graph_properties_iface *iface,
        LPCWSTR pProgramName)
{
    struct d3d12_wg_state_object *object = impl_from_ID3D12WorkGraphProperties(iface);
    size_t i;

    TRACE("iface %p, pProgramName %s\n", iface, debugstr_w(pProgramName));

    for (i = 0; i < object->programs_count; i++)
        if (vkd3d_export_strequal(object->programs[i].name, pProgramName))
            return (UINT)i;

    return -1;
}

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetNumNodes(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex)
{
    struct d3d12_wg_state_object *object = impl_from_ID3D12WorkGraphProperties(iface);
    const struct d3d12_wg_state_object_program *program;
    unsigned int count, i;
    TRACE("iface %p, WorkGraphIndex %u\n", iface, WorkGraphIndex);

    if (WorkGraphIndex >= object->programs_count)
    {
        ERR("WorkGraphIndex %u is out of bound.\n", WorkGraphIndex);
        return -1u;
    }

    program = &object->programs[WorkGraphIndex];

    count = 0;
    for (i = 0; i < program->num_pipelines; i++)
    {
        if (program->pipelines[i].vk_non_entry_pipeline != VK_NULL_HANDLE ||
            program->pipelines[i].vk_cpu_node_entry_pipeline != VK_NULL_HANDLE)
        {
            count++;
        }
    }
    return count;
}

static unsigned int d3d12_work_graph_properties_node_index_to_entry(
        const struct d3d12_wg_state_object_program *program, UINT NodeIndex)
{
    unsigned int count, i;
    count = 0;

    for (i = 0; i < program->num_pipelines; i++)
    {
        if (program->pipelines[i].vk_non_entry_pipeline != VK_NULL_HANDLE ||
            program->pipelines[i].vk_cpu_node_entry_pipeline != VK_NULL_HANDLE)
        {
            if (count == NodeIndex)
                return i;
            count++;
        }
    }

    return -1u;
}

static const struct d3d12_wg_state_object_pipeline *d3d12_work_graph_properties_node_index_pipeline(
        const struct d3d12_wg_state_object_program *program, UINT NodeIndex)
{
    unsigned int index = d3d12_work_graph_properties_node_index_to_entry(program, NodeIndex);
    if (index == -1u)
        return NULL;
    else
        return &program->pipelines[index];
}

static D3D12_NODE_ID * STDMETHODCALLTYPE d3d12_work_graph_properties_GetNodeID(
        d3d12_work_graph_properties_iface *iface,
        D3D12_NODE_ID *ret,
        UINT WorkGraphIndex,
        UINT NodeIndex)
{
    struct d3d12_wg_state_object *object = impl_from_ID3D12WorkGraphProperties(iface);
    const struct d3d12_wg_state_object_pipeline *pipeline;
    TRACE("iface %p, ret %p, WorkGraphIndex %u, NodeIndex %u\n", iface, ret, WorkGraphIndex, NodeIndex);

    if (WorkGraphIndex >= object->programs_count)
    {
        ERR("WorkGraphIndex %u is out of bound.\n", WorkGraphIndex);
        ret->Name = NULL;
        ret->ArrayIndex = 0;
        return ret;
    }

    pipeline = d3d12_work_graph_properties_node_index_pipeline(&object->programs[WorkGraphIndex], NodeIndex);

    if (pipeline)
    {
        ret->Name = pipeline->name;
        ret->ArrayIndex = pipeline->array_index;
    }
    else
    {
        ret->Name = NULL;
        ret->ArrayIndex = 0;
    }

    return ret;
}

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetNodeIndex(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex,
        D3D12_NODE_ID NodeID)
{
    struct d3d12_wg_state_object *object = impl_from_ID3D12WorkGraphProperties(iface);
    const struct d3d12_wg_state_object_program *program;
    unsigned int count, i;
    TRACE("iface %p, WorkGraphIndex %u, NodeID.Name, NodeID.ArrayIndex %u, stub!\n",
            iface, WorkGraphIndex, debugstr_w(NodeID.Name), NodeID.ArrayIndex);

    if (WorkGraphIndex >= object->programs_count)
    {
        ERR("WorkGraphIndex %u is out of bound.\n", WorkGraphIndex);
        return -1u;
    }

    program = &object->programs[WorkGraphIndex];
    count = 0;

    for (i = 0; i < program->num_pipelines; i++)
    {
        if (program->pipelines[i].vk_non_entry_pipeline != VK_NULL_HANDLE ||
            program->pipelines[i].vk_cpu_node_entry_pipeline != VK_NULL_HANDLE)
        {
            if (vkd3d_export_strequal(NodeID.Name, program->pipelines[i].name) &&
                NodeID.ArrayIndex == program->pipelines[i].array_index)
            {
                return count;
            }
            count++;
        }
    }

    return -1u;
}

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetNodeLocalRootArgumentsTableIndex(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex,
        UINT NodeIndex)
{
    struct d3d12_wg_state_object *object = impl_from_ID3D12WorkGraphProperties(iface);
    unsigned int entry;
    TRACE("iface %p, WorkGraphIndex %u, NodeIndex %u\n", iface, WorkGraphIndex, NodeIndex);

    if (WorkGraphIndex >= object->programs_count)
    {
        ERR("WorkGraphIndex %u is out of bound.\n", WorkGraphIndex);
        return -1u;
    }

    entry = d3d12_work_graph_properties_node_index_to_entry(&object->programs[WorkGraphIndex], NodeIndex);

    if (entry == -1u)
        return -1u;
    else
        return object->entry_points[entry].node_input->local_root_arguments_table_index;
}

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetNumEntrypoints(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex)
{
    struct d3d12_wg_state_object *object = impl_from_ID3D12WorkGraphProperties(iface);
    const struct d3d12_wg_state_object_program *program;
    TRACE("iface %p, WorkGraphIndex %u\n", iface, WorkGraphIndex);

    if (WorkGraphIndex >= object->programs_count)
    {
        ERR("WorkGraphIndex %u is out of bound.\n", WorkGraphIndex);
        return 0;
    }

    program = &object->programs[WorkGraphIndex];
    return program->levels[0].nodes_count;
}

static D3D12_NODE_ID * STDMETHODCALLTYPE d3d12_work_graph_properties_GetEntrypointID(
        d3d12_work_graph_properties_iface *iface,
        D3D12_NODE_ID *ret,
        UINT WorkGraphIndex,
        UINT EntrypointIndex)
{
    struct d3d12_wg_state_object *object = impl_from_ID3D12WorkGraphProperties(iface);
    const struct d3d12_wg_state_object_program *program;
    TRACE("iface %p, ret %p, WorkGraphIndex %u, EntrypointIndex %u\n", iface, ret, WorkGraphIndex, EntrypointIndex);

    if (WorkGraphIndex >= object->programs_count)
    {
        ERR("WorkGraphIndex %u is out of bound.\n", WorkGraphIndex);
        return 0;
    }

    program = &object->programs[WorkGraphIndex];
    if (EntrypointIndex >= program->levels[0].nodes_count)
    {
        ERR("EntrypointIndex %u out of range.\n", EntrypointIndex);
        ret->Name = NULL;
        ret->ArrayIndex = 0;
        return ret;
    }

    ret->Name = program->pipelines[program->levels[0].nodes[EntrypointIndex]].name;
    ret->ArrayIndex = program->pipelines[program->levels[0].nodes[EntrypointIndex]].array_index;
    return ret;
}

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetEntrypointIndex(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex,
        D3D12_NODE_ID NodeID)
{
    struct d3d12_wg_state_object *object = impl_from_ID3D12WorkGraphProperties(iface);
    const struct d3d12_wg_state_object_program *program;
    unsigned int i;

    TRACE("iface %p, WorkGraphIndex %u, NodeID.Name, NodeID.ArrayIndex %u!\n",
            iface, WorkGraphIndex, debugstr_w(NodeID.Name), NodeID.ArrayIndex);

    if (WorkGraphIndex >= object->programs_count)
    {
        ERR("WorkGraphIndex %u is out of bound.\n", WorkGraphIndex);
        return 0;
    }

    program = &object->programs[WorkGraphIndex];

    for (i = 0; i < program->levels[0].nodes_count; i++)
    {
        unsigned int node_index = program->levels[0].nodes[i];
        if (vkd3d_export_strequal(program->pipelines[node_index].name, NodeID.Name) &&
                program->pipelines[node_index].array_index == NodeID.ArrayIndex)
        {
            return i;
        }
    }

    return -1;
}

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetEntrypointRecordSizeInBytes(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex,
        UINT EntrypointIndex)
{
    struct d3d12_wg_state_object *object = impl_from_ID3D12WorkGraphProperties(iface);
    const struct d3d12_wg_state_object_program *program;
    unsigned int node_index;
    TRACE("iface %p, WorkGraphIndex %u, EntrypointIndex %u!\n", iface, WorkGraphIndex, EntrypointIndex);

    if (WorkGraphIndex >= object->programs_count)
    {
        ERR("WorkGraphIndex %u is out of bound.\n", WorkGraphIndex);
        return 0;
    }

    program = &object->programs[WorkGraphIndex];

    if (EntrypointIndex >= program->levels[0].nodes_count)
    {
        ERR("EntrypointIndex %u out of range.\n", EntrypointIndex);
        return 0;
    }

    node_index = program->levels[0].nodes[EntrypointIndex];
    return object->entry_points[node_index].node_input->payload_stride;
}

static void STDMETHODCALLTYPE d3d12_work_graph_properties_GetWorkGraphMemoryRequirements(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex,
        D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS *pWorkGraphMemoryRequirements)
{
    /* Arbitrary */
    const VkDeviceSize packed_offset_payload_max_size = 256 * 1024 * 1024;
    const VkDeviceSize packed_offset_payload_min_size = 16 * 1024 * 1024;
    const VkDeviceSize size_granularity = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT; /* arbitrary, but 64k is reasonable. */

    struct d3d12_wg_state_object *object = impl_from_ID3D12WorkGraphProperties(iface);
    TRACE("iface %p, WorkGraphIndex %u, pWorkGraphMemoryRequirements %p\n", iface, WorkGraphIndex, pWorkGraphMemoryRequirements);
    if (WorkGraphIndex >= object->programs_count)
    {
        ERR("WorkGraphIndex %u is out of bound.\n", WorkGraphIndex);
        memset(pWorkGraphMemoryRequirements, 0, sizeof(*pWorkGraphMemoryRequirements));
        return;
    }

    pWorkGraphMemoryRequirements->MinSizeInBytes =
            align64(object->programs[WorkGraphIndex].required_scratch_size + packed_offset_payload_min_size, size_granularity);
    pWorkGraphMemoryRequirements->MaxSizeInBytes =
            align64(object->programs[WorkGraphIndex].required_scratch_size + packed_offset_payload_max_size, size_granularity);
    pWorkGraphMemoryRequirements->SizeGranularityInBytes = size_granularity;
    TRACE("Required scratch size: %"PRIu64" bytes.\n", pWorkGraphMemoryRequirements->MinSizeInBytes);
}

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetEntrypointRecordAlignmentInBytes(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex,
        UINT EntrypointIndex)
{
    FIXME("iface %p, WorkGraphIndex %u, EntrypointIndex %u, stub!\n", iface, WorkGraphIndex, EntrypointIndex);
    /* Do we need to be more specific? */
    return 16;
}

static CONST_VTBL struct ID3D12WorkGraphPropertiesVtbl d3d12_work_graph_properties_vtbl =
{
    /* IUnknown methods */
    d3d12_work_graph_properties_QueryInterface,
    d3d12_work_graph_properties_AddRef,
    d3d12_work_graph_properties_Release,
    /* ID3D12WorkGraphProperties methods */
    d3d12_work_graph_properties_GetNumWorkGraphs,
    d3d12_work_graph_properties_GetProgramName,
    d3d12_work_graph_properties_GetWorkGraphIndex,
    d3d12_work_graph_properties_GetNumNodes,
    d3d12_work_graph_properties_GetNodeID,
    d3d12_work_graph_properties_GetNodeIndex,
    d3d12_work_graph_properties_GetNodeLocalRootArgumentsTableIndex,
    d3d12_work_graph_properties_GetNumEntrypoints,
    d3d12_work_graph_properties_GetEntrypointID,
    d3d12_work_graph_properties_GetEntrypointIndex,
    d3d12_work_graph_properties_GetEntrypointRecordSizeInBytes,
    d3d12_work_graph_properties_GetWorkGraphMemoryRequirements,
    d3d12_work_graph_properties_GetEntrypointRecordAlignmentInBytes,
};

struct d3d12_wg_state_object_spec_constant_tmp
{
    VkSpecializationMapEntry *map_entries;
    uint32_t *spec_data;
    size_t map_entries_size;
    size_t spec_data_count;
    size_t spec_data_size;
};

static HRESULT d3d12_wg_state_object_compile_pipeline(
        struct d3d12_wg_state_object *object,
        struct d3d12_wg_state_object_data *data,
        struct d3d12_wg_state_object_program *program,
        unsigned int entry_point_index,
        struct d3d12_wg_state_object_spec_constant_tmp *tmp)
{
    const struct vkd3d_vk_device_procs *vk_procs = &object->device->vk_procs;
    const struct vkd3d_shader_library_entry_point *entry;
    VkComputePipelineCreateInfo pipeline_info;
    VkSpecializationInfo spec_info;
    unsigned int i;
    VkResult vr;

    memset(&pipeline_info, 0, sizeof(pipeline_info));
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.pSpecializationInfo = &spec_info;
    pipeline_info.stage.pName = "main";
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;

    if (program->pipelines[entry_point_index].vk_cpu_node_entry_pipeline != VK_NULL_HANDLE ||
            program->pipelines[entry_point_index].vk_gpu_node_entry_pipeline != VK_NULL_HANDLE ||
            program->pipelines[entry_point_index].vk_non_entry_pipeline != VK_NULL_HANDLE)
    {
        return S_OK;
    }

    entry = &data->entry_points[entry_point_index];

    /* TODO: These can be overridden during compilation. */
    program->pipelines[entry_point_index].name = vkd3d_dup_entry_point(entry->node_input->node_id);
    program->pipelines[entry_point_index].array_index = entry->node_input->node_array_index;

    if (!entry->node_input->is_program_entry)
    {
        vkd3d_meta_get_workgraph_payload_offset_pipeline(&object->device->meta_ops,
                entry->node_input->node_track_rw_input_sharing ?
                        entry->node_input->dispatch_grid_type_bits / 8 : 0,
                entry->node_input->dispatch_grid_is_upper_bound ? entry->node_input->dispatch_grid_components : 1,
                &program->pipelines[entry_point_index].payload_offset_expander);
    }

    /* Set up spec constants for node outputs, etc. This will
     * have to be expanded a bit to also cover things like sparse array checks, recursion state, etc. */
    tmp->spec_data_count = entry->node_outputs_count;
    /* Workgroup size is also a spec constant. */
    if (entry->node_input->launch_type == VKD3D_SHADER_NODE_LAUNCH_TYPE_THREAD)
        tmp->spec_data_count++;
    if (entry->node_input->is_program_entry)
        tmp->spec_data_count++;

    if (tmp->spec_data_count)
    {
        vkd3d_array_reserve((void **)&tmp->spec_data, &tmp->spec_data_size, tmp->spec_data_count, sizeof(*tmp->spec_data));
        vkd3d_array_reserve((void **)&tmp->map_entries, &tmp->map_entries_size, tmp->spec_data_count, sizeof(*tmp->map_entries));
    }

    pipeline_info.stage.module = object->modules[entry_point_index].vk_module;
    pipeline_info.layout = object->modules[entry_point_index].vk_pipeline_layout;
    if (d3d12_device_uses_descriptor_buffers(object->device))
        pipeline_info.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    spec_info.pData = tmp->spec_data;
    spec_info.dataSize = tmp->spec_data_count * sizeof(uint32_t);
    spec_info.pMapEntries = tmp->map_entries;
    spec_info.mapEntryCount = tmp->spec_data_count;

    for (i = 0; i < entry->node_outputs_count; i++)
    {
        tmp->map_entries[i].offset = sizeof(uint32_t) * i;
        tmp->map_entries[i].size = sizeof(uint32_t);
        tmp->map_entries[i].constantID = entry->node_outputs[i].node_index_spec_constant_id;
        tmp->spec_data[i] = d3d12_work_graph_find_node_by_id(
                data->entry_points, data->entry_points_count,
                entry->node_outputs[i].node_id, entry->node_outputs[i].node_array_index);
    }

    if (entry->node_input->launch_type == VKD3D_SHADER_NODE_LAUNCH_TYPE_THREAD)
    {
        tmp->map_entries[entry->node_outputs_count].offset = sizeof(uint32_t) * entry->node_outputs_count;
        tmp->map_entries[entry->node_outputs_count].size = sizeof(uint32_t);
        tmp->map_entries[entry->node_outputs_count].constantID = 0;
        tmp->spec_data[entry->node_outputs_count] = object->device->device_info.vulkan_1_1_properties.subgroupSize;
    }

    if (entry->node_input->is_program_entry)
    {
        tmp->map_entries[tmp->spec_data_count - 1].offset = sizeof(uint32_t) * (tmp->spec_data_count - 1);
        tmp->map_entries[tmp->spec_data_count - 1].size = sizeof(uint32_t);
        tmp->map_entries[tmp->spec_data_count - 1].constantID = entry->node_input->is_indirect_bda_stride_program_entry_spec_id;

        tmp->spec_data[tmp->spec_data_count - 1] = 0;
        vr = VK_CALL(vkCreateComputePipelines(object->device->vk_device,
                VK_NULL_HANDLE, 1, &pipeline_info, NULL,
                &program->pipelines[entry_point_index].vk_cpu_node_entry_pipeline));

        tmp->spec_data[tmp->spec_data_count - 1] = 1;
        vr = VK_CALL(vkCreateComputePipelines(object->device->vk_device,
                VK_NULL_HANDLE, 1, &pipeline_info, NULL,
                &program->pipelines[entry_point_index].vk_gpu_node_entry_pipeline));
    }
    else
    {
        vr = VK_CALL(vkCreateComputePipelines(object->device->vk_device,
                VK_NULL_HANDLE, 1, &pipeline_info, NULL,
                &program->pipelines[entry_point_index].vk_non_entry_pipeline));
    }

    if (vr < 0)
    {
        ERR("Failed to create pipeline, vr %d\n", vr);
        return hresult_from_vk_result(vr);
    }

    return S_OK;
}

static HRESULT d3d12_wg_state_object_compile_program(
        struct d3d12_wg_state_object *object,
        struct d3d12_wg_state_object_data *data,
        struct d3d12_wg_state_object_program *program)
{
    struct d3d12_wg_state_object_spec_constant_tmp tmp;
    unsigned int level;
    HRESULT hr = S_OK;
    unsigned int i;

    memset(&tmp, 0, sizeof(tmp));
    program->pipelines = vkd3d_calloc(data->entry_points_count, sizeof(*program->pipelines));
    program->num_pipelines = data->entry_points_count;

    vkd3d_meta_get_workgraph_workgroup_pipeline(&object->device->meta_ops,
            &program->workgroup_distributor);
    vkd3d_meta_get_workgraph_setup_gpu_input_pipeline(&object->device->meta_ops,
            &program->gpu_input_setup);

    program->counters_scratch_offset = 0;
    program->counters_scratch_size = (1 + data->entry_points_count) * sizeof(uint32_t) * 2;
    program->indirect_commands_scratch_offset = align(program->counters_scratch_size, 64);
    program->indirect_commands_scratch_size =
            data->entry_points_count * sizeof(struct d3d12_workgraph_indirect_command);
    program->dividers_scratch_offset =
            align(program->indirect_commands_scratch_offset + program->indirect_commands_scratch_size, 64);
    program->dividers_scratch_size = data->entry_points_count * sizeof(uint32_t);
    program->required_scratch_size = program->dividers_scratch_offset + program->dividers_scratch_size;

    for (level = 0; level < program->num_levels; level++)
    {
        const struct d3d12_workgraph_level_execution *exec = &program->levels[level];
        for (i = 0; i < exec->nodes_count; i++)
            if (FAILED(hr = d3d12_wg_state_object_compile_pipeline(object, data, program, exec->nodes[i], &tmp)))
                goto fail;

        for (i = 0; i < exec->shared_nodes_count; i++)
            if (FAILED(hr = d3d12_wg_state_object_compile_pipeline(object, data, program, exec->shared_nodes[i].node_pipeline_index, &tmp)))
                goto fail;
    }

fail:
    vkd3d_free(tmp.map_entries);
    vkd3d_free(tmp.spec_data);
    return hr;
}

static HRESULT d3d12_wg_state_object_convert_entry_point(
        struct d3d12_wg_state_object *object,
        struct d3d12_wg_state_object_data *data,
        struct d3d12_wg_state_object_module *module,
        struct vkd3d_shader_library_entry_point *entry)
{
    struct vkd3d_shader_interface_local_info shader_interface_local_info;
    const struct d3d12_state_object_association *global_rs_assoc;
    const struct d3d12_state_object_association *local_rs_assoc;
    struct vkd3d_shader_interface_info shader_interface_info;
    struct vkd3d_shader_descriptor_binding push_ubo_binding;
    struct vkd3d_shader_compile_arguments compile_args;
    struct vkd3d_shader_code dxil, spirv;

    HRESULT hr;

    memset(&compile_args, 0, sizeof(compile_args));
    compile_args.target_extensions = object->device->vk_info.shader_extensions;
    compile_args.target_extension_count = object->device->vk_info.shader_extension_count;
    compile_args.target = VKD3D_SHADER_TARGET_SPIRV_VULKAN_1_0;
    compile_args.min_subgroup_size = object->device->device_info.vulkan_1_3_properties.minSubgroupSize;
    compile_args.max_subgroup_size = object->device->device_info.vulkan_1_3_properties.maxSubgroupSize;
    /* Don't care about wave size promotion in RT. */
    compile_args.quirks = &vkd3d_shader_quirk_info;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DRIVER_VERSION_SENSITIVE_SHADERS)
    {
        compile_args.driver_id = object->device->device_info.vulkan_1_2_properties.driverID;
        compile_args.driver_version = object->device->device_info.properties2.properties.driverVersion;
    }

    memset(&shader_interface_info, 0, sizeof(shader_interface_info));
    memset(&shader_interface_local_info, 0, sizeof(shader_interface_local_info));

    shader_interface_info.min_ssbo_alignment = d3d12_device_get_ssbo_alignment(object->device);

    shader_interface_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shader_interface_info.xfb_info = NULL;

    shader_interface_info.descriptor_size_cbv_srv_uav = d3d12_device_get_descriptor_handle_increment_size(
            object->device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    shader_interface_info.descriptor_size_sampler = d3d12_device_get_descriptor_handle_increment_size(
            object->device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    global_rs_assoc = d3d12_state_object_find_association(VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE,
            data->associations, data->associations_count,
            NULL, 0, entry, NULL);

    local_rs_assoc = d3d12_state_object_find_association(VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE,
            data->associations, data->associations_count,
            NULL, 0, entry, NULL);

    /* Since we're dispatching shaders one by one, we can be very lenient with pipeline layout compat.
     * Just rebind descriptors for each dispatch. It should be fine, at least for now. */

    if (!global_rs_assoc)
    {
        struct d3d12_root_signature *global_root_signature;
        if (FAILED(hr = d3d12_root_signature_create_empty(object->device, &global_root_signature)))
            return E_OUTOFMEMORY;
        d3d12_root_signature_inc_ref(global_root_signature);
        ID3D12RootSignature_Release(&global_root_signature->ID3D12RootSignature_iface);
        module->root_signature = global_root_signature;
    }
    else
    {
        d3d12_root_signature_inc_ref(module->root_signature = global_rs_assoc->root_signature);
    }

    /* Create a modified pipeline layout which uses the work graph layout.
     * It uses push constants for various metadata, and moves root parameters to push UBO. */
    if (FAILED(hr = d3d12_root_signature_create_work_graph_layout(
            module->root_signature, &module->vk_set_layout, &module->vk_pipeline_layout)))
        return hr;

    if (module->root_signature)
    {
        struct d3d12_root_signature *rs = module->root_signature;
        /* We might have different bindings per PSO, even if they are considered pipeline layout compatible.
         * Register/space declaration could differ, but those don't change the Vulkan pipeline layout. */
        shader_interface_info.flags = d3d12_root_signature_get_shader_interface_flags(rs, VKD3D_PIPELINE_TYPE_COMPUTE);
        shader_interface_info.descriptor_tables.offset = rs->descriptor_table_offset;
        shader_interface_info.descriptor_tables.count = rs->descriptor_table_count;
        shader_interface_info.bindings = rs->bindings;
        shader_interface_info.binding_count = rs->binding_count;
        shader_interface_info.push_constant_buffers = rs->root_constants;
        shader_interface_info.push_constant_buffer_count = rs->root_constant_count;
        shader_interface_info.push_constant_ubo_binding = &rs->push_constant_ubo_binding;
        shader_interface_info.offset_buffer_binding = &rs->offset_buffer_binding;
#ifdef VKD3D_ENABLE_DESCRIPTOR_QA
        shader_interface_info.descriptor_qa_global_binding = &rs->descriptor_qa_global_info;
        shader_interface_info.descriptor_qa_heap_binding = &rs->descriptor_qa_heap_binding;
#endif

        if (!(shader_interface_info.flags & VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK))
        {
            push_ubo_binding.binding = 0;
            push_ubo_binding.set = rs->compute.num_set_layouts;
            shader_interface_info.push_constant_ubo_binding = &push_ubo_binding;
            shader_interface_info.flags |= VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK;
        }

        module->push_set_index = rs->compute.push_constant_range.size ? push_ubo_binding.set : UINT32_MAX;
    }
    else
    {
        module->push_set_index = UINT32_MAX;
    }

    if (local_rs_assoc)
    {
        struct d3d12_root_signature *rs = local_rs_assoc->root_signature;
        shader_interface_local_info.local_root_parameters = rs->parameters;
        shader_interface_local_info.local_root_parameter_count = rs->parameter_count;
        shader_interface_local_info.shader_record_constant_buffers = rs->root_constants;
        shader_interface_local_info.shader_record_buffer_count = rs->root_constant_count;

        if (rs->static_sampler_count)
        {
            FIXME("Static samplers not implemented yet.\n");
            return E_NOTIMPL;
        }

        shader_interface_local_info.bindings = rs->bindings;
        shader_interface_local_info.binding_count = rs->binding_count;

        /* Promote state which might only be active in local root signature. */
        shader_interface_info.flags |= d3d12_root_signature_get_shader_interface_flags(rs, VKD3D_PIPELINE_TYPE_COMPUTE);
        if (rs->compute.flags & (VKD3D_ROOT_SIGNATURE_USE_SSBO_OFFSET_BUFFER | VKD3D_ROOT_SIGNATURE_USE_TYPED_OFFSET_BUFFER))
            shader_interface_info.offset_buffer_binding = &rs->offset_buffer_binding;
    }

    memset(&dxil, 0, sizeof(dxil));
    memset(&spirv, 0, sizeof(spirv));

    /* TODO: If we're exporting multiple entry points from one DXIL library,
     * we can amortize the parsing cost. */
    dxil.code = data->dxil_libraries[entry->identifier]->DXILLibrary.pShaderBytecode;
    dxil.size = data->dxil_libraries[entry->identifier]->DXILLibrary.BytecodeLength;

    if (vkd3d_shader_compile_dxil_export(&dxil, entry->real_entry_point, entry->debug_entry_point,
            &spirv, NULL,
            &shader_interface_info, &shader_interface_local_info, &compile_args) != VKD3D_OK)
    {
        ERR("Failed to convert DXIL export: %s (%s)\n",
                entry->real_entry_point, entry->debug_entry_point);
        return E_OUTOFMEMORY;
    }

    if (!d3d12_device_validate_shader_meta(object->device, &spirv.meta))
        return E_INVALIDARG;

    if (FAILED(hr = d3d12_pipeline_state_create_shader_module(object->device, &module->vk_module, &spirv)))
        return hr;

    return S_OK;
}

static HRESULT d3d12_wg_state_object_allocate_rings(struct d3d12_wg_state_object *object)
{
    /* This is the big achilles heel of any emulation path. This memory requirement is way too huge
     * to make sense as scratch memory, so we'll have to come up with some global device data share or something
     * to make this feasible, but for simple testing, we can just assume one workgraph per device. */
    const VkDeviceSize payload_ring_size = 128 * 1024 * 1024;
    HRESULT hr;

    if (FAILED(hr = d3d12_state_object_allocate_ring(&object->payload[0], payload_ring_size, object->device)))
        return hr;
    if (FAILED(hr = d3d12_state_object_allocate_ring(&object->payload[1], payload_ring_size, object->device)))
        return hr;
    if (FAILED(hr = d3d12_state_object_allocate_ring(&object->unrolled_offsets, payload_ring_size, object->device)))
        return hr;

    return S_OK;
}

static HRESULT d3d12_wg_state_object_compile_programs(
        struct d3d12_wg_state_object *object, struct d3d12_wg_state_object_data *data)
{
    HRESULT hr;
    size_t i;

    object->programs = data->programs;
    object->programs_count = data->programs_count;
    data->programs = NULL;
    data->programs_count = 0;

    /* Build up a tree of executions. Level 0 are entry points which may receive work.
     * First we run all nodes in level 0, then we figure out how to distribute work, run every node in level 1,
     * etc, etc. */
    for (i = 0; i < object->programs_count; i++)
    {
        struct d3d12_wg_state_object_program *program = &object->programs[i];
        if (FAILED(hr = d3d12_wg_state_object_resolve_entry_points(object, data, program)))
            return hr;
    }

    /* Convert modules separately, per-program.
     * We will combine them with spec constants later when resolving the programs.
     * It's possible we'll have to compile multiple variants for cases where a node can be used as both
     * an entry point and non-entry point and there are overrides for dispatch parameters.
     * Many things can hopefully be resolved through spec constants of course, but we'll have to see
     * as the implementation comes together. */
    object->modules = vkd3d_calloc(data->entry_points_count, sizeof(*object->modules));
    object->modules_count = data->entry_points_count;
    object->coalesce_dividers = vkd3d_malloc(object->modules_count * sizeof(uint32_t));
    for (i = 0; i < data->entry_points_count; i++)
    {
        switch (data->entry_points[i].node_input->launch_type)
        {
            case VKD3D_SHADER_NODE_LAUNCH_TYPE_BROADCASTING:
                object->coalesce_dividers[i] = 0;
                break;

            case VKD3D_SHADER_NODE_LAUNCH_TYPE_THREAD:
                object->coalesce_dividers[i] = object->device->device_info.vulkan_1_1_properties.subgroupSize;
                break;

            case VKD3D_SHADER_NODE_LAUNCH_TYPE_COALESCING:
                object->coalesce_dividers[i] = data->entry_points[i].node_input->coalesce_factor;
                break;

            default:
                return E_INVALIDARG;
        }

        if (FAILED(hr = d3d12_wg_state_object_convert_entry_point(object, data, &object->modules[i],
                &data->entry_points[i])))
            return hr;
    }

    /* Create pipelines. Every program can have different overrides like node assignments, so we'll have to
     * assume we have to compile pipelines like this. For duplicated spec constant setups, we can fortunately
     * rely on caching to get us most of the way. */
    for (i = 0; i < object->programs_count; i++)
    {
        struct d3d12_wg_state_object_program *program = &object->programs[i];
        if (FAILED(hr = d3d12_wg_state_object_compile_program(object, data, program)))
            return hr;
    }

    if (FAILED(hr = d3d12_wg_state_object_allocate_rings(object)))
        return hr;

    return S_OK;
}

static HRESULT d3d12_wg_state_object_init(struct d3d12_wg_state_object *object, struct d3d12_device *device,
        const D3D12_STATE_OBJECT_DESC *desc)
{
    struct d3d12_wg_state_object_data data;
    HRESULT hr = S_OK;

    memset(&data, 0, sizeof(data));
    if (FAILED(hr = d3d12_wg_state_object_parse_subobjects(&data, device, desc)))
        goto fail;

    object->ID3D12StateObject_iface.lpVtbl = &d3d12_state_object_vtbl;
    object->ID3D12StateObjectProperties1_iface.lpVtbl = &d3d12_state_object_properties_vtbl;
    object->ID3D12WorkGraphProperties_iface.lpVtbl = &d3d12_work_graph_properties_vtbl;
    object->refcount = 1;
    object->internal_refcount = 1;
    object->device = device;
    object->type = desc->Type;

    if (FAILED(hr = d3d12_wg_state_object_compile_programs(object, &data)))
        goto fail;
    if (FAILED(hr = vkd3d_private_store_init(&object->private_store)))
        goto fail;
    d3d12_device_add_ref(object->device);

    object->entry_points = data.entry_points;
    object->entry_points_count = data.entry_points_count;
    data.entry_points = NULL;
    data.entry_points_count = 0;

fail:
    d3d12_wg_state_object_cleanup_data(&data, device);
    if (FAILED(hr))
        d3d12_state_object_cleanup(object);
    return hr;
}

HRESULT d3d12_wg_state_object_create(struct d3d12_device *device, const D3D12_STATE_OBJECT_DESC *desc,
        struct d3d12_wg_state_object **state_object)
{
    struct d3d12_wg_state_object *object;
    HRESULT hr;

    if (!(object = vkd3d_calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    hr = d3d12_wg_state_object_init(object, device, desc);

    if (FAILED(hr))
    {
        vkd3d_free(object);
        return hr;
    }

    *state_object = object;
    return S_OK;
}

void d3d12_command_list_workgraph_initialize_scratch(struct d3d12_command_list *list)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_unique_resource *resource;
    struct d3d12_wg_state_object *wg_state;
    uint32_t wg_state_program_index;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;

    wg_state = (struct d3d12_wg_state_object *)list->wg_state.ProgramIdentifier.OpaqueData[1];
    wg_state_program_index = (uint32_t)list->wg_state.ProgramIdentifier.OpaqueData[0];

    if (!wg_state)
    {
        WARN("WG state is not set.\n");
        return;
    }

    if (!list->wg_state.BackingMemory.StartAddress)
        return;

    d3d12_command_list_end_current_render_pass(list, false);
    d3d12_command_list_debug_mark_begin_region(list, "WGInitScratch");

    /* INITIALIZE is assumed to happen in UAV. */
    memset(&dep_info, 0, sizeof(dep_info));
    memset(&vk_barrier, 0, sizeof(vk_barrier));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &vk_barrier;

    /* App is responsible for doing the UAV barrier here, so we just need to do a transitive barrier into CLEAR. */
    vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
    vk_barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    if (list->wg_state.BackingMemory.SizeInBytes < wg_state->programs[wg_state_program_index].required_scratch_size)
    {
        ERR("Backing memory is not large enough (%"PRIu64" < %"PRIu64".\n",
                list->wg_state.BackingMemory.SizeInBytes < wg_state->programs[wg_state_program_index].required_scratch_size);
        return;
    }

    resource = vkd3d_va_map_deref(&list->device->memory_allocator.va_map, list->wg_state.BackingMemory.StartAddress);
    if (resource)
    {
        VK_CALL(vkCmdFillBuffer(list->cmd.vk_command_buffer, resource->vk_buffer,
                list->wg_state.BackingMemory.StartAddress - resource->va,
                wg_state->programs[wg_state_program_index].dividers_scratch_offset, 0));

        VK_CALL(vkCmdUpdateBuffer(list->cmd.vk_command_buffer, resource->vk_buffer,
                list->wg_state.BackingMemory.StartAddress - resource->va +
                wg_state->programs[wg_state_program_index].dividers_scratch_offset,
                wg_state->modules_count * sizeof(uint32_t), wg_state->coalesce_dividers));
    }

    vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
    vk_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
    d3d12_command_list_debug_mark_end_region(list);
}

static void d3d12_command_list_workgraph_bind_resources(struct d3d12_command_list *list,
        const struct d3d12_wg_state_object *state,
        const struct d3d12_wg_state_object_program *program,
        VkPipelineLayout vk_layout, uint32_t vk_push_set,
        VkBuffer vk_root_parameter_buffer, VkDeviceSize vk_root_parameter_buffer_offset)
{
    const struct vkd3d_bindless_state *bindless_state = &list->device->bindless_state;
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkDescriptorBufferInfo buffer_info;
    VkWriteDescriptorSet write;
    unsigned int i;

    if (d3d12_device_uses_descriptor_buffers(list->device))
    {
        VK_CALL(vkCmdSetDescriptorBufferOffsetsEXT(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                vk_layout, 0, bindless_state->set_count,
                bindless_state->vk_descriptor_buffer_indices,
                list->descriptor_heap.buffers.vk_offsets));
    }
    else
    {
        for (i = 0; i < bindless_state->set_count; i++)
        {
            if (list->descriptor_heap.sets.vk_sets[i])
            {
                VK_CALL(vkCmdBindDescriptorSets(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                        vk_layout, i, 1,
                        &list->descriptor_heap.sets.vk_sets[i], 0, NULL));
            }
        }
    }

    if (vk_push_set != UINT32_MAX)
    {
        memset(&write, 0, sizeof(write));
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &buffer_info;

        buffer_info.offset = vk_root_parameter_buffer_offset;
        buffer_info.buffer = vk_root_parameter_buffer;
        buffer_info.range = sizeof(union vkd3d_root_parameter_data);

        VK_CALL(vkCmdPushDescriptorSetKHR(list->cmd.vk_command_buffer,
                VK_PIPELINE_BIND_POINT_COMPUTE, vk_layout, vk_push_set, 1, &write));
    }
}

static void d3d12_command_list_workgraph_execute_node_cpu_entry(struct d3d12_command_list *list,
        const struct d3d12_wg_state_object *state,
        const struct d3d12_wg_state_object_program *program,
        uint32_t node_index, const D3D12_NODE_CPU_INPUT *desc,
        VkDeviceAddress output_payload, VkDeviceAddress input_payload,
        VkBuffer vk_root_parameter_buffer, VkDeviceSize vk_root_parameter_buffer_offset)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_shader_node_input_data *node_input;
    struct vkd3d_shader_node_input_push_signature push;
    struct vkd3d_scratch_allocation offset_scratch;
    VkPipelineLayout vk_layout;
    uint32_t table_index;

    memset(&push, 0, sizeof(push));
    vk_layout = state->modules[node_index].vk_pipeline_layout;

    /* Just rebind resources every time. We have to execute intermediate shaders anyway,
     * which clobbers all descriptor state. */
    d3d12_command_list_workgraph_bind_resources(list, state, program,
            vk_layout, state->modules[node_index].push_set_index,
            vk_root_parameter_buffer, vk_root_parameter_buffer_offset);

    node_input = state->entry_points[node_index].node_input;

    push.node_payload_bda = input_payload;
    push.node_payload_output_bda = output_payload;

    push.node_payload_stride_or_offsets_bda = desc->RecordStrideInBytes;
    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_UNIFORM_UPLOAD, sizeof(uint32_t) * 3, sizeof(uint32_t), ~0u, &offset_scratch))
        return;

    ((uint32_t *)offset_scratch.host_ptr)[0] = 0; /* primary offset */
    ((uint32_t *)offset_scratch.host_ptr)[1] = desc->NumRecords & ~(WG_DIVIDER - 1u); /* secondary offset */
    ((uint32_t *)offset_scratch.host_ptr)[2] = desc->NumRecords; /* total nodes */
    push.node_linear_offset_bda = offset_scratch.va;
    push.node_total_nodes_bda = offset_scratch.va + 2 * sizeof(uint32_t);

    push.node_payload_output_atomic_bda = list->wg_state.BackingMemory.StartAddress;
    /* Counteract fixed offset applied by shader */
    push.node_payload_output_offset = program->required_scratch_size / sizeof(uint32_t) - 2;
    push.node_payload_output_stride =
            (list->wg_state.BackingMemory.SizeInBytes - program->required_scratch_size) / max(state->modules_count, 1);
    push.node_payload_output_stride /= sizeof(uint32_t);

    table_index = node_input->local_root_arguments_table_index;
    if (table_index != UINT32_MAX)
    {
        push.local_root_signature_bda =
                list->wg_state.NodeLocalRootArgumentsTable.StartAddress + table_index *
                list->wg_state.NodeLocalRootArgumentsTable.StrideInBytes;
    }

    VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            program->pipelines[node_index].vk_cpu_node_entry_pipeline));

    VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
            vk_layout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(push), &push));

    if (node_input->launch_type == VKD3D_SHADER_NODE_LAUNCH_TYPE_BROADCASTING)
    {
        uint32_t num_x = node_input->broadcast_grid[0];
        uint32_t num_y = node_input->broadcast_grid[1];
        uint32_t num_z = node_input->broadcast_grid[2];
        uint32_t num_wgs_x = desc->NumRecords;
        uint32_t x, y, z;

        // If the SV_DispatchGrid does not contain a component, it's implied to be 1.
        if (node_input->dispatch_grid_components)
        {
            if (node_input->dispatch_grid_components < 3)
                num_z = 1;
            if (node_input->dispatch_grid_components < 2)
                num_y = 1;
        }

        for (z = 0; z < num_z; z++)
        {
            for (y = 0; y < num_y; y++)
            {
                for (x = 0; x < num_x; x++)
                {
                    push.node_grid_dispatch[0] = x;
                    push.node_grid_dispatch[1] = y;
                    push.node_grid_dispatch[2] = z;
                    push.node_linear_offset_bda = offset_scratch.va;

                    VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
                            vk_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                            offsetof(struct vkd3d_shader_node_input_push_signature, node_grid_dispatch),
                            3 * sizeof(uint32_t), push.node_grid_dispatch));

                    VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
                            vk_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                            offsetof(struct vkd3d_shader_node_input_push_signature, node_linear_offset_bda),
                            sizeof(uint32_t), &push.node_linear_offset_bda));

                    /* Primary offset */
                    if (num_wgs_x >= WG_DIVIDER)
                        VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, WG_DIVIDER, num_wgs_x / WG_DIVIDER, 1));

                    push.node_linear_offset_bda += sizeof(uint32_t);
                    VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
                            vk_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                            offsetof(struct vkd3d_shader_node_input_push_signature, node_linear_offset_bda),
                            sizeof(uint32_t), &push.node_linear_offset_bda));

                    /* Secondary offset */
                    if (num_wgs_x % WG_DIVIDER)
                        VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, num_wgs_x % WG_DIVIDER, 1, 1));
                }
            }
        }
    }
    else
    {
        uint32_t num_wgs_x;
        if (node_input->launch_type == VKD3D_SHADER_NODE_LAUNCH_TYPE_COALESCING)
        {
            num_wgs_x = (desc->NumRecords + node_input->coalesce_factor - 1) / node_input->coalesce_factor;
        }
        else
        {
            num_wgs_x =
                    align(desc->NumRecords, list->device->device_info.vulkan_1_1_properties.subgroupSize) /
                    list->device->device_info.vulkan_1_1_properties.subgroupSize;
        }

        /* Primary offset. */
        if (num_wgs_x >= WG_DIVIDER)
            VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, WG_DIVIDER, num_wgs_x / WG_DIVIDER, 1));

        push.node_linear_offset_bda += sizeof(uint32_t);
        VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
                vk_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                offsetof(struct vkd3d_shader_node_input_push_signature, node_linear_offset_bda),
                sizeof(uint32_t), &push.node_linear_offset_bda));

        /* Secondary offset */
        if (num_wgs_x % WG_DIVIDER)
            VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, num_wgs_x % WG_DIVIDER, 1, 1));
    }
}

static void d3d12_command_list_workgraph_barrier(struct d3d12_command_list *list,
        VkPipelineStageFlags2 vk_dst_stages, VkAccessFlags2 vk_dst_access)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;

    memset(&dep_info, 0, sizeof(dep_info));
    memset(&vk_barrier, 0, sizeof(vk_barrier));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &vk_barrier;

    /* Back to back workgraphs without barrier is allowed, as long as app doesn't expect ordering. */
    vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    vk_barrier.dstStageMask = vk_dst_stages;
    vk_barrier.dstAccessMask = vk_dst_access;
    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
}

static void d3d12_command_list_emit_distribute_workgroups(struct d3d12_command_list *list,
        const struct d3d12_wg_state_object *state,
        const struct d3d12_wg_state_object_program *program)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_workgraph_workgroups_args args;

    d3d12_command_list_workgraph_barrier(list,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

    VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE, program->workgroup_distributor.vk_pipeline));

    args.node_atomics = list->wg_state.BackingMemory.StartAddress;
    args.num_nodes = state->entry_points_count;
    args.commands = program->indirect_commands_scratch_offset + list->wg_state.BackingMemory.StartAddress;
    args.dividers = program->dividers_scratch_offset + list->wg_state.BackingMemory.StartAddress;

    VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
            program->workgroup_distributor.vk_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(args), &args));

    VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, 1, 1, 1));

    d3d12_command_list_workgraph_barrier(list,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
}

static void d3d12_command_list_emit_distribute_payload_offsets(struct d3d12_command_list *list,
        const struct d3d12_wg_state_object *state,
        const struct d3d12_wg_state_object_program *program,
        uint32_t level, D3D12_GPU_VIRTUAL_ADDRESS payload_va)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_workgraph_payload_offsets_args args;
    const struct vkd3d_unique_resource *resource;
    unsigned int node_index;
    unsigned int i;

    resource = vkd3d_va_map_deref(&list->device->memory_allocator.va_map, list->wg_state.BackingMemory.StartAddress);
    if (!resource)
        return;

    for (i = 0; i < program->levels[level].nodes_count; i++)
    {
        const struct vkd3d_shader_node_input_data *node_input;
        VkDeviceSize vk_offset;

        node_index = program->levels[level].nodes[i];
        node_input = state->entry_points[node_index].node_input;

        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                program->pipelines[node_index].payload_offset_expander.vk_pipeline));

        args.node_index = node_index;
        args.commands = list->wg_state.BackingMemory.StartAddress + program->indirect_commands_scratch_offset;
        args.payload = payload_va;
        args.unrolled_offsets = state->unrolled_offsets.va;
        args.packed_offset_counts = list->wg_state.BackingMemory.StartAddress + program->required_scratch_size;
        args.packed_offset_counts_stride =
                (list->wg_state.BackingMemory.SizeInBytes - program->required_scratch_size) / max(state->modules_count, 1);
        args.packed_offset_counts_stride /= sizeof(uint32_t);
        args.payload_stride = node_input->payload_stride;

        if (node_input->dispatch_grid_is_upper_bound)
        {
            args.grid_offset_or_count = (int)node_input->dispatch_grid_offset;
            /* Component type and components is spec constant. */
        }
        else
        {
            uint32_t total_wgs = node_input->broadcast_grid[0] *
                    node_input->broadcast_grid[1] *
                    node_input->broadcast_grid[2];
            args.grid_offset_or_count = -(int)total_wgs;
        }

        VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
                program->pipelines[node_index].payload_offset_expander.vk_pipeline_layout,
                VK_SHADER_STAGE_COMPUTE_BIT,
                0, sizeof(args), &args));

        vk_offset = list->wg_state.BackingMemory.StartAddress - resource->va;
        vk_offset += program->indirect_commands_scratch_offset;
        vk_offset += node_index * sizeof(struct d3d12_workgraph_indirect_command);
        vk_offset += offsetof(struct d3d12_workgraph_indirect_command, expander_execute);

        VK_CALL(vkCmdDispatchIndirect(list->cmd.vk_command_buffer, resource->vk_buffer, vk_offset));
    }

    /* This shader only expands offsets, indirect data is already written. */
    d3d12_command_list_workgraph_barrier(list, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

struct vkd3d_workgraph_gpu_input_indirect
{
    uint32_t primary_indirect[3];
    uint32_t primary_linear_offset;
    uint32_t secondary_indirect[3];
    uint32_t secondary_linear_offset;
};

static void d3d12_command_list_workgraph_execute_node_gpu(
        struct d3d12_command_list *list, const struct d3d12_wg_state_object *state,
        const struct d3d12_wg_state_object_program *program,
        D3D12_GPU_VIRTUAL_ADDRESS output_va, D3D12_GPU_VIRTUAL_ADDRESS input_va,
        unsigned int level, unsigned int index, const struct vkd3d_scratch_allocation *indirect_scratch,
        VkBuffer vk_root_parameter_buffer, VkDeviceSize vk_root_parameter_buffer_offset)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    const struct vkd3d_shader_node_input_data *node_input;
    struct vkd3d_shader_node_input_push_signature push;
    VkDeviceAddress secondary_linear_offset_bda;
    VkDeviceAddress primary_linear_offset_bda;
    VkDeviceSize vk_secondary_indirect_offset;
    VkDeviceSize vk_primary_indirect_offset;
    uint32_t num_x, num_y, num_z;
    VkBuffer vk_indirect_buffer;
    VkPipelineLayout vk_layout;
    unsigned int table_index;
    unsigned int node_index;
    uint32_t x, y, z;

    memset(&push, 0, sizeof(push));
    node_index = program->levels[level].nodes[index];
    vk_layout = state->modules[node_index].vk_pipeline_layout;
    node_input = state->entry_points[node_index].node_input;

    /* Just rebind resources every time. We have to execute intermediate shaders anyway,
     * which clobbers all descriptor state. */
    d3d12_command_list_workgraph_bind_resources(list, state, program,
            vk_layout, state->modules[node_index].push_set_index,
            vk_root_parameter_buffer, vk_root_parameter_buffer_offset);

    push.node_payload_output_bda = output_va;

    if (level == 0)
    {
        assert(indirect_scratch);
        /* GPU node entries load payload/stride indirectly straight from app buffer. */
        push.node_payload_bda = input_va + offsetof(D3D12_NODE_GPU_INPUT, Records.StartAddress);
        push.node_payload_stride_or_offsets_bda = input_va + offsetof(D3D12_NODE_GPU_INPUT, Records.StrideInBytes);
        push.node_total_nodes_bda = input_va + offsetof(D3D12_NODE_GPU_INPUT, NumRecords);

        vk_indirect_buffer = indirect_scratch->buffer;
        vk_primary_indirect_offset = indirect_scratch->offset;
        vk_secondary_indirect_offset = indirect_scratch->offset;
        vk_primary_indirect_offset += index * sizeof(struct vkd3d_workgraph_gpu_input_indirect);
        vk_secondary_indirect_offset += index * sizeof(struct vkd3d_workgraph_gpu_input_indirect);
        vk_primary_indirect_offset += offsetof(struct vkd3d_workgraph_gpu_input_indirect, primary_indirect);
        vk_secondary_indirect_offset += offsetof(struct vkd3d_workgraph_gpu_input_indirect, secondary_indirect);

        primary_linear_offset_bda =
                indirect_scratch->va + offsetof(struct vkd3d_workgraph_gpu_input_indirect, primary_linear_offset);
        secondary_linear_offset_bda =
                indirect_scratch->va + offsetof(struct vkd3d_workgraph_gpu_input_indirect, secondary_linear_offset);

        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                program->pipelines[node_index].vk_gpu_node_entry_pipeline));
    }
    else
    {
        const struct vkd3d_unique_resource *resource;
        VkDeviceAddress indirect_va =
                list->wg_state.BackingMemory.StartAddress + program->indirect_commands_scratch_offset +
                sizeof(struct d3d12_workgraph_indirect_command) * node_index;

        push.node_payload_stride_or_offsets_bda = state->unrolled_offsets.va;
        push.node_payload_bda = input_va;

        primary_linear_offset_bda = indirect_va + offsetof(struct d3d12_workgraph_indirect_command, primary_linear_offset);
        secondary_linear_offset_bda = indirect_va + offsetof(struct d3d12_workgraph_indirect_command, secondary_linear_offset);

        resource = vkd3d_va_map_deref(&list->device->memory_allocator.va_map, list->wg_state.BackingMemory.StartAddress);
        if (!resource)
            return;

        vk_indirect_buffer = resource->vk_buffer;
        vk_primary_indirect_offset = indirect_va - resource->va;
        vk_secondary_indirect_offset = indirect_va - resource->va;
        vk_primary_indirect_offset += offsetof(struct d3d12_workgraph_indirect_command, primary_execute);
        vk_secondary_indirect_offset += offsetof(struct d3d12_workgraph_indirect_command, secondary_execute);

        push.node_total_nodes_bda =
                list->wg_state.BackingMemory.StartAddress + program->indirect_commands_scratch_offset +
                sizeof(struct d3d12_workgraph_indirect_command) * node_index +
                offsetof(struct d3d12_workgraph_indirect_command, end_elements);

        VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer,
                VK_PIPELINE_BIND_POINT_COMPUTE,
                program->pipelines[node_index].vk_non_entry_pipeline));
    }

    push.node_payload_output_atomic_bda = list->wg_state.BackingMemory.StartAddress;
    /* Compensate for offset applied by shader. */
    push.node_payload_output_offset = program->required_scratch_size / sizeof(uint32_t) - 2;
    push.node_payload_output_stride =
            (list->wg_state.BackingMemory.SizeInBytes - program->required_scratch_size) / max(state->modules_count, 1);
    push.node_payload_output_stride /= sizeof(uint32_t);

    table_index = node_input->local_root_arguments_table_index;
    if (table_index != UINT32_MAX)
    {
        push.local_root_signature_bda =
                list->wg_state.NodeLocalRootArgumentsTable.StartAddress + table_index *
                list->wg_state.NodeLocalRootArgumentsTable.StrideInBytes;
    }

    VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
            vk_layout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(push), &push));

    if (node_input->launch_type == VKD3D_SHADER_NODE_LAUNCH_TYPE_BROADCASTING)
    {
        num_x = node_input->broadcast_grid[0];
        num_y = node_input->broadcast_grid[1];
        num_z = node_input->broadcast_grid[2];

        // If the SV_DispatchGrid does not contain a component, it's implied to be 1.
        if (node_input->dispatch_grid_is_upper_bound)
        {
            if (node_input->dispatch_grid_components < 3)
                num_z = 1;
            if (node_input->dispatch_grid_components < 2)
                num_y = 1;
        }
    }
    else
    {
        num_x = 1;
        num_y = 1;
        num_z = 1;
    }

    for (z = 0; z < num_z; z++)
    {
        for (y = 0; y < num_y; y++)
        {
            for (x = 0; x < num_x; x++)
            {
                push.node_grid_dispatch[0] = x;
                push.node_grid_dispatch[1] = y;
                push.node_grid_dispatch[2] = z;
                push.node_linear_offset_bda = primary_linear_offset_bda;

                VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
                        vk_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                        offsetof(struct vkd3d_shader_node_input_push_signature, node_grid_dispatch),
                        3 * sizeof(uint32_t), push.node_grid_dispatch));

                VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
                        vk_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                        offsetof(struct vkd3d_shader_node_input_push_signature, node_linear_offset_bda),
                        sizeof(VkDeviceAddress), &push.node_linear_offset_bda));

                VK_CALL(vkCmdDispatchIndirect(list->cmd.vk_command_buffer, vk_indirect_buffer, vk_primary_indirect_offset));

                push.node_linear_offset_bda = secondary_linear_offset_bda;

                VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer,
                        vk_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                        offsetof(struct vkd3d_shader_node_input_push_signature, node_linear_offset_bda),
                        sizeof(VkDeviceAddress), &push.node_linear_offset_bda));

                VK_CALL(vkCmdDispatchIndirect(list->cmd.vk_command_buffer, vk_indirect_buffer, vk_secondary_indirect_offset));
            }
        }
    }
}

static void d3d12_command_list_workgraph_execute_level(struct d3d12_command_list *list,
        const struct d3d12_wg_state_object *state,
        const struct d3d12_wg_state_object_program *program,
        uint32_t level, VkDeviceAddress output_payload, VkDeviceAddress input_payload,
        VkBuffer vk_root_parameter_buffer, VkDeviceSize vk_root_parameter_buffer_offset)
{
    uint32_t i;

    d3d12_command_list_emit_distribute_workgroups(list, state, program);
    d3d12_command_list_emit_distribute_payload_offsets(list, state, program, level, input_payload);

    /* Execute nodes */
    for (i = 0; i < program->levels[level].nodes_count; i++)
    {
        d3d12_command_list_workgraph_execute_node_gpu(list, state, program,
                output_payload, input_payload, level, i, NULL,
                vk_root_parameter_buffer, vk_root_parameter_buffer_offset);
    }
}

static bool d3d12_command_list_workgraph_setup_indirect(
        struct d3d12_command_list *list, struct d3d12_wg_state_object *wg_state,
        const struct d3d12_wg_state_object_program *program, D3D12_GPU_VIRTUAL_ADDRESS va,
        struct vkd3d_scratch_allocation *indirect_scratch)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_scratch_allocation dividers_scratch;
    struct vkd3d_workgraph_setup_gpu_input_args args;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep_info;
    unsigned int num_wgs;
    unsigned int i;

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_DEVICE_STORAGE,
            program->levels[0].nodes_count * sizeof(struct vkd3d_workgraph_gpu_input_indirect),
            64, ~0u, indirect_scratch))
        return false;

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_UNIFORM_UPLOAD,
            program->levels[0].nodes_count * sizeof(uint32_t),
            64, ~0u, &dividers_scratch))
        return false;

    for (i = 0; i < program->levels[0].nodes_count; i++)
    {
        unsigned int node_index = program->levels[0].nodes[i];
        const struct vkd3d_shader_node_input_data *input;
        unsigned int coalesce_divider;

        input = wg_state->entry_points[node_index].node_input;
        if (input->launch_type == VKD3D_SHADER_NODE_LAUNCH_TYPE_BROADCASTING)
            coalesce_divider = 1;
        else if (input->launch_type == VKD3D_SHADER_NODE_LAUNCH_TYPE_COALESCING)
            coalesce_divider = input->coalesce_factor;
        else
            coalesce_divider = list->device->device_info.vulkan_1_1_properties.subgroupSize;

        ((uint32_t *)dividers_scratch.host_ptr)[i] = coalesce_divider;
    }

    args.gpu_input_va = va;
    args.indirect_commands_va = indirect_scratch->va;
    args.coalesce_divider_va = dividers_scratch.va;
    args.num_entry_points = program->levels[0].nodes_count;

    VK_CALL(vkCmdPushConstants(list->cmd.vk_command_buffer, program->gpu_input_setup.vk_pipeline_layout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args), &args));
    VK_CALL(vkCmdBindPipeline(list->cmd.vk_command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, program->gpu_input_setup.vk_pipeline));

    num_wgs = align(args.num_entry_points, vkd3d_meta_get_workgraph_setup_gpu_input_workgroup_size()) /
            vkd3d_meta_get_workgraph_setup_gpu_input_workgroup_size();
    VK_CALL(vkCmdDispatch(list->cmd.vk_command_buffer, num_wgs, 1, 1));

    memset(&vk_barrier, 0, sizeof(vk_barrier));
    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    dep_info.memoryBarrierCount = 1;
    dep_info.pMemoryBarriers = &vk_barrier;

    vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));

    return true;
}

static void d3d12_command_list_workgraph_execute_entry_gpu(
        struct d3d12_command_list *list, struct d3d12_wg_state_object *state,
        const struct d3d12_wg_state_object_program *program, D3D12_GPU_VIRTUAL_ADDRESS va,
        VkBuffer vk_root_param_buffer, VkDeviceSize vk_root_param_offset)
{
    struct vkd3d_scratch_allocation indirect_scratch;
    unsigned int i;

    if (program->levels[0].nodes_count == 0)
        return;
    if (!d3d12_command_list_workgraph_setup_indirect(list, state, program, va, &indirect_scratch))
        return;

    /* We don't know which entry gets to execute, so have to spam indirects.
     * FIXME: DGCC can help with this if number of entry points is large. */
    for (i = 0; i < program->levels[0].nodes_count; i++)
    {
        d3d12_command_list_workgraph_execute_node_gpu(
                list, state, program, state->payload[0].va, va, 0, i,
                &indirect_scratch, vk_root_param_buffer, vk_root_param_offset);
    }
}

static void d3d12_command_list_workgraph_execute_entry_cpu(
        struct d3d12_command_list *list, struct d3d12_wg_state_object *wg_state,
        const struct d3d12_wg_state_object_program *program, const D3D12_NODE_CPU_INPUT *desc,
        VkBuffer vk_root_param_buffer, VkDeviceSize vk_root_param_offset)
{
    struct vkd3d_scratch_allocation payload_scratch;
    VkDeviceSize payload_size;
    uint32_t node_index;
    unsigned int i;

    if (desc->NumRecords == 0)
        return;

    if (desc->EntrypointIndex >= program->levels[0].nodes_count)
    {
        ERR("EntryPointIndex %u is out of bounds.\n", desc->EntrypointIndex);
        return;
    }

    node_index = program->levels[0].nodes[desc->EntrypointIndex];
    payload_size = desc->NumRecords * desc->RecordStrideInBytes;
    if (desc->RecordStrideInBytes == 0)
        payload_size = wg_state->entry_points[node_index].node_input->payload_stride;

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_UNIFORM_UPLOAD, payload_size, 64, ~0u, &payload_scratch))
        return;

    /* Alternatively use vkCmdUpdateBuffer, but at least for now, just rely on ReBAR doing its thing. */
    memcpy(payload_scratch.host_ptr, desc->pRecords, payload_size);

    d3d12_command_list_workgraph_execute_node_cpu_entry(list, wg_state, program, node_index, desc,
            wg_state->payload[0].va, payload_scratch.va,
            vk_root_param_buffer, vk_root_param_offset);

    /* For any shared nodes, just execute those as well. */
    for (i = 0; i < program->levels[0].shared_nodes_count; i++)
    {
        if (program->levels[0].shared_nodes[i].node_payload_index == node_index)
        {
            d3d12_command_list_workgraph_execute_node_cpu_entry(list, wg_state, program,
                    program->levels[0].shared_nodes[i].node_pipeline_index, desc,
                    wg_state->payload[0].va, payload_scratch.va,
                    vk_root_param_buffer, vk_root_param_offset);
        }
    }
}

static void d3d12_command_list_workgraph_execute_entry_level(
        struct d3d12_command_list *list, struct d3d12_wg_state_object *wg_state,
        const struct d3d12_wg_state_object_program *program, const D3D12_DISPATCH_GRAPH_DESC *desc,
        VkBuffer vk_root_param_buffer, VkDeviceSize vk_root_param_offset)
{
    unsigned int i;
    switch (desc->Mode)
    {
        case D3D12_DISPATCH_MODE_NODE_CPU_INPUT:
            d3d12_command_list_workgraph_execute_entry_cpu(
                    list, wg_state, program, &desc->NodeCPUInput,
                    vk_root_param_buffer, vk_root_param_offset);
            break;

        case D3D12_DISPATCH_MODE_MULTI_NODE_CPU_INPUT:
            for (i = 0; i < desc->MultiNodeCPUInput.NumNodeInputs; i++)
            {
                const D3D12_NODE_CPU_INPUT *input;
                input = (const void *)((const uint8_t *)desc->MultiNodeCPUInput.pNodeInputs +
                        desc->MultiNodeCPUInput.NodeInputStrideInBytes * i);
                d3d12_command_list_workgraph_execute_entry_cpu(
                        list, wg_state, program, input,
                        vk_root_param_buffer, vk_root_param_offset);
            }
            break;

        case D3D12_DISPATCH_MODE_NODE_GPU_INPUT:
            d3d12_command_list_workgraph_execute_entry_gpu(
                    list, wg_state, program, desc->NodeGPUInput,
                    vk_root_param_buffer, vk_root_param_offset);
            break;

        default:
            FIXME("Unimplemented mode %u\n", desc->Mode);
            break;
    }
}

void d3d12_command_list_workgraph_dispatch(struct d3d12_command_list *list, const D3D12_DISPATCH_GRAPH_DESC *desc)
{
    const struct d3d12_wg_state_object_program *program;
    struct vkd3d_scratch_allocation root_param_scratch;
    struct d3d12_wg_state_object *wg_state;
    uint32_t wg_state_program_index;
    uint32_t i;

    wg_state = (struct d3d12_wg_state_object *)list->wg_state.ProgramIdentifier.OpaqueData[1];
    wg_state_program_index = (uint32_t)list->wg_state.ProgramIdentifier.OpaqueData[0];

    if (!wg_state)
    {
        WARN("WG state is not set.\n");
        return;
    }

    d3d12_command_list_end_current_render_pass(list, false);
    d3d12_command_list_debug_mark_begin_region(list, "WGDispatch");

    d3d12_command_list_invalidate_current_pipeline(list, true);
    d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, &list->graphics_bindings);
    d3d12_command_list_update_descriptor_buffers(list);

    d3d12_command_list_workgraph_barrier(list,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    program = &wg_state->programs[wg_state_program_index];

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_UNIFORM_UPLOAD,
            sizeof(union vkd3d_root_parameter_data), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT,
            ~0u, &root_param_scratch))
        return;

    d3d12_command_list_fetch_root_parameter_data(list, &list->compute_bindings, root_param_scratch.host_ptr);
    /* TODO: For mesh nodes, may have to fetch graphics root parameter data. */

    d3d12_command_list_workgraph_execute_entry_level(list, wg_state, program, desc,
            root_param_scratch.buffer, root_param_scratch.offset);

    for (i = 1; i < program->num_levels; i++)
    {
        d3d12_command_list_workgraph_execute_level(list, wg_state, program, i,
                wg_state->payload[i & 1].va, wg_state->payload[(i & 1) ^ 1].va,
                root_param_scratch.buffer, root_param_scratch.offset);
    }

    d3d12_command_list_debug_mark_end_region(list);
}
