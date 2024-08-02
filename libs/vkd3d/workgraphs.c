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

struct d3d12_workgraph_level_execution
{
    unsigned int *nodes;
    size_t nodes_size;
    size_t nodes_count;
};

struct d3d12_wg_state_object_pipeline
{
    VkPipeline vk_node_pipeline;
    /* layout is part of modules array */

    /* Meta shader. distribute_payload_offsets.comp. This is specialized per node. */
    struct vkd3d_workgraph_meta_pipeline_info payload_offset_expander;
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
            vkd3d_free(programs[i].levels[j].nodes);

        for (j = 0; j < programs[i].num_pipelines; j++)
            VK_CALL(vkDestroyPipeline(device->vk_device, programs[i].pipelines[j].vk_node_pipeline, NULL));
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

    exec = &program->levels[level];
    for (i = 0; i < exec->nodes_count; i++)
        if (exec->nodes[i] == entry_point_index)
            return S_OK;

    vkd3d_array_reserve((void **)&exec->nodes, &exec->nodes_size,
            exec->nodes_count + 1, sizeof(*exec->nodes));
    exec->nodes[exec->nodes_count++] = entry_point_index;
    program->num_levels = max(program->num_levels, level + 1);

    entry = &data->entry_points[entry_point_index];

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

    for (i = 0; i < entry->node_outputs_count; i++)
    {
        const struct vkd3d_shader_node_output_data *output = &entry->node_outputs[i];
        uint32_t node_array_size, node_index;

        /* Checked on the outside for now as well. */
        if (output->node_array_size == UINT32_MAX)
        {
            FIXME("Unbounded array size is not supported.\n");
            hr = E_NOTIMPL;
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
                    program, data, node_index, level + 1, false)))
            {
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

    /* We cannot override this state yet, so verify shader matches our expectation. */
    for (i = 0; i < data->entry_points_count; i++)
    {
        if (!data->entry_points[i].node_input)
            continue;

        if (node_is_output_target[i] && data->entry_points[i].node_input->is_program_entry)
        {
            FIXME("Node %s[%u] was marked as entry point, but it is used as node output.\n",
                    data->entry_points[i].node_input->node_id,
                    data->entry_points[i].node_input->node_array_index);
            hr = E_NOTIMPL;
            goto fail;
        }
        else if (!node_is_output_target[i] && !data->entry_points[i].node_input->is_program_entry)
        {
            FIXME("Node %s[%u] was not marked as entry point, but it is used as entry point.\n",
                    data->entry_points[i].node_input->node_id,
                    data->entry_points[i].node_input->node_array_index);
            hr = E_NOTIMPL;
            goto fail;
        }

        /* Recurse through the nodes. Start with entry point and fill in any dependencies. */
        if (data->entry_points[i].node_input->is_program_entry)
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

    d3d12_state_object_cleanup_allocation(&state_object->payload[0], state_object->device);
    d3d12_state_object_cleanup_allocation(&state_object->payload[1], state_object->device);
    d3d12_state_object_cleanup_allocation(&state_object->packed_payload_offsets, state_object->device);
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
    FIXME("iface %p, WorkGraphIndex %u, stub!\n", iface, WorkGraphIndex);
    return -1;
}

static D3D12_NODE_ID * STDMETHODCALLTYPE d3d12_work_graph_properties_GetNodeID(
        d3d12_work_graph_properties_iface *iface,
        D3D12_NODE_ID *ret,
        UINT WorkGraphIndex,
        UINT NodeIndex)
{
    FIXME("iface %p, ret %p, WorkGraphIndex %u, NodeIndex %u, stub!\n", iface, ret, WorkGraphIndex, NodeIndex);
    ret->Name = NULL;
    ret->ArrayIndex = 0;
    return ret;
}

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetNodeIndex(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex,
        D3D12_NODE_ID NodeID)
{
    FIXME("iface %p, WorkGraphIndex %u, NodeID.Name, NodeID.ArrayIndex %u, stub!\n",
            iface, WorkGraphIndex, debugstr_w(NodeID.Name), NodeID.ArrayIndex);
    return -1;
}

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetNodeLocalRootArgumentsTableIndex(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex,
        UINT NodeIndex)
{
    FIXME("iface %p, WorkGraphIndex %u, NodeIndex %u, stub!\n", iface, WorkGraphIndex, NodeIndex);
    return -1;
}

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetNumEntrypoints(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex)
{
    FIXME("iface %p, WorkGraphIndex %u, stub!\n", iface, WorkGraphIndex);
    return -1;
}

static D3D12_NODE_ID * STDMETHODCALLTYPE d3d12_work_graph_properties_GetEntrypointID(
        d3d12_work_graph_properties_iface *iface,
        D3D12_NODE_ID *ret,
        UINT WorkGraphIndex,
        UINT EntrypointIndex)
{
    FIXME("iface %p, ret %p, WorkGraphIndex %u, EntrypointIndex %u, stub!\n", iface, ret, WorkGraphIndex, EntrypointIndex);
    ret->Name = NULL;
    ret->ArrayIndex = 0;
    return ret;
}

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetEntrypointIndex(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex,
        D3D12_NODE_ID NodeID)
{
    FIXME("iface %p, WorkGraphIndex %u, NodeID.Name, NodeID.ArrayIndex %u, stub!\n",
            iface, WorkGraphIndex, debugstr_w(NodeID.Name), NodeID.ArrayIndex);
    return -1;
}

static UINT STDMETHODCALLTYPE d3d12_work_graph_properties_GetEntrypointRecordSizeInBytes(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex,
        UINT EntrypointIndex)
{
    FIXME("iface %p, WorkGraphIndex %u, EntrypointIndex %u, stub!\n", iface, WorkGraphIndex, EntrypointIndex);
    return 0;
}

static void STDMETHODCALLTYPE d3d12_work_graph_properties_GetWorkGraphMemoryRequirements(
        d3d12_work_graph_properties_iface *iface,
        UINT WorkGraphIndex,
        D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS *pWorkGraphMemoryRequirements)
{
    struct d3d12_wg_state_object *object = impl_from_ID3D12WorkGraphProperties(iface);
    TRACE("iface %p, WorkGraphIndex %u, pWorkGraphMemoryRequirements %p\n", iface, WorkGraphIndex, pWorkGraphMemoryRequirements);
    if (WorkGraphIndex >= object->programs_count)
    {
        ERR("WorkGraphIndex %u is out of bound.\n", WorkGraphIndex);
        memset(pWorkGraphMemoryRequirements, 0, sizeof(*pWorkGraphMemoryRequirements));
        return;
    }

    TRACE("Required scratch size: %"PRIu64" bytes.\n", object->programs[WorkGraphIndex].required_scratch_size);
    pWorkGraphMemoryRequirements->MinSizeInBytes = object->programs[WorkGraphIndex].required_scratch_size;
    pWorkGraphMemoryRequirements->MaxSizeInBytes = object->programs[WorkGraphIndex].required_scratch_size;
    pWorkGraphMemoryRequirements->SizeGranularityInBytes = 1;
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

static HRESULT d3d12_wg_state_object_compile_program(
        struct d3d12_wg_state_object *object,
        struct d3d12_wg_state_object_data *data,
        struct d3d12_wg_state_object_program *program)
{
    const struct vkd3d_vk_device_procs *vk_procs = &object->device->vk_procs;
    VkSpecializationMapEntry *map_entries = NULL;
    VkComputePipelineCreateInfo pipeline_info;
    VkSpecializationInfo spec_info;
    size_t map_entries_size = 0;
    uint32_t *spec_data = NULL;
    size_t spec_data_count = 0;
    size_t spec_data_size = 0;
    VkResult vr = VK_SUCCESS;
    unsigned int level;
    unsigned int i, j;

    program->pipelines = vkd3d_calloc(data->entry_points_count, sizeof(*program->pipelines));
    program->num_pipelines = data->entry_points_count;

    memset(&pipeline_info, 0, sizeof(pipeline_info));
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.pSpecializationInfo = &spec_info;
    pipeline_info.stage.pName = "main";
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;

    vkd3d_meta_get_workgraph_workgroup_pipeline(&object->device->meta_ops,
            &program->workgroup_distributor);

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
        const struct vkd3d_shader_library_entry_point *entry;

        for (i = 0; i < exec->nodes_count; i++)
        {
            unsigned int entry_point_index = exec->nodes[i];
            if (program->pipelines[entry_point_index].vk_node_pipeline != VK_NULL_HANDLE)
                continue;

            entry = &data->entry_points[entry_point_index];

            if (!entry->node_input->is_program_entry)
            {
                vkd3d_meta_get_workgraph_payload_offset_pipeline(&object->device->meta_ops,
                        entry->node_input->node_track_rw_input_sharing ?
                                entry->node_input->dispatch_grid_type_bits / 8 : 0,
                        entry->node_input->dispatch_grid_components,
                        &program->pipelines[entry_point_index].payload_offset_expander);
            }

            /* Set up spec constants for node outputs, etc. This will
             * have to be expanded a bit to also cover things like sparse array checks, recursion state, etc. */
            spec_data_count = entry->node_outputs_count;
            /* Workgroup size is also a spec constant. */
            if (entry->node_input->launch_type == VKD3D_SHADER_NODE_LAUNCH_TYPE_THREAD)
                spec_data_count++;

            vkd3d_array_reserve((void **)&spec_data, &spec_data_size, spec_data_count, sizeof(*spec_data));
            vkd3d_array_reserve((void **)&map_entries, &map_entries_size, spec_data_count, sizeof(*map_entries));

            pipeline_info.stage.module = object->modules[entry_point_index].vk_module;
            pipeline_info.layout = object->modules[entry_point_index].vk_pipeline_layout;
            spec_info.pData = spec_data;
            spec_info.dataSize = spec_data_count * sizeof(uint32_t);
            spec_info.pMapEntries = map_entries;
            spec_info.mapEntryCount = spec_data_count;

            for (j = 0; j < entry->node_outputs_count; j++)
            {
                map_entries[j].offset = sizeof(uint32_t) * j;
                map_entries[j].size = sizeof(uint32_t);
                map_entries[j].constantID = entry->node_outputs[j].node_index_spec_constant_id;
                spec_data[j] = d3d12_work_graph_find_node_by_id(
                        data->entry_points, data->entry_points_count,
                        entry->node_outputs[j].node_id, entry->node_outputs[j].node_array_index);
            }

            if (entry->node_input->launch_type == VKD3D_SHADER_NODE_LAUNCH_TYPE_THREAD)
            {
                map_entries[entry->node_outputs_count].offset = sizeof(uint32_t) * entry->node_outputs_count;
                map_entries[entry->node_outputs_count].size = sizeof(uint32_t);
                map_entries[entry->node_outputs_count].constantID = 0;
                spec_data[entry->node_outputs_count] = object->device->device_info.vulkan_1_1_properties.subgroupSize;
            }

            vr = VK_CALL(vkCreateComputePipelines(object->device->vk_device,
                    VK_NULL_HANDLE, 1, &pipeline_info, NULL,
                    &program->pipelines[entry_point_index].vk_node_pipeline));

            if (vr < 0)
            {
                ERR("Failed to create pipeline, vr %d\n", vr);
                goto fail;
            }
        }
    }

fail:
    vkd3d_free(map_entries);
    vkd3d_free(spec_data);
    return hresult_from_vk_result(vr);
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
    const VkDeviceSize packed_offset_payload_size = 256 * 1024 * 1024;
    const VkDeviceSize payload_ring_size = 128 * 1024 * 1024;
    HRESULT hr;

    if (FAILED(hr = d3d12_state_object_allocate_ring(&object->payload[0], payload_ring_size, object->device)))
        return hr;
    if (FAILED(hr = d3d12_state_object_allocate_ring(&object->payload[1], payload_ring_size, object->device)))
        return hr;
    if (FAILED(hr = d3d12_state_object_allocate_ring(&object->unrolled_offsets, payload_ring_size, object->device)))
        return hr;
    if (FAILED(hr = d3d12_state_object_allocate_ring(&object->packed_payload_offsets,
            packed_offset_payload_size, object->device)))
        return hr;

    /* Can get very tiny if the state object holds a ton of objects, but that's *shrug* for now. */
    object->packed_payload_offset_size_per_node = packed_offset_payload_size / max(object->modules_count, 1);

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
    for (i = 0; i < data->entry_points_count; i++)
        if (FAILED(hr = d3d12_wg_state_object_convert_entry_point(object, data, &object->modules[i], &data->entry_points[i])))
            return hr;

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
    vk_barrier.dstAccessMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
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
                list->wg_state.BackingMemory.SizeInBytes, 0));

        /* TODO: Copy over the dividers. */
    }

    vk_barrier.srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
    vk_barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    vk_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    vk_barrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
    VK_CALL(vkCmdPipelineBarrier2(list->cmd.vk_command_buffer, &dep_info));
    d3d12_command_list_debug_mark_end_region(list);
}

static void d3d12_command_list_workgraph_execute_node(struct d3d12_command_list *list,
        const struct d3d12_wg_state_object *state,
        const struct d3d12_wg_state_object_program *program,
        uint32_t node_index, uint32_t level,
        VkDeviceAddress output_payload, VkDeviceAddress input_payload)
{
    const struct vkd3d_vk_device_procs *vk_procs = &list->device->vk_procs;
    struct vkd3d_shader_node_input_push_signature push;
    VkPipelineLayout vk_layout;

    memset(&push, 0, sizeof(push));
    vk_layout = state->modules[node_index].vk_pipeline_layout;
}

void d3d12_command_list_workgraph_dispatch(struct d3d12_command_list *list, const D3D12_DISPATCH_GRAPH_DESC *desc)
{
    const struct d3d12_wg_state_object_program *program;
    struct vkd3d_scratch_allocation scratch;
    struct d3d12_wg_state_object *wg_state;
    uint32_t wg_state_program_index;
    VkDeviceSize payload_size;
    uint32_t node_index;

    wg_state = (struct d3d12_wg_state_object *)list->wg_state.ProgramIdentifier.OpaqueData[1];
    wg_state_program_index = (uint32_t)list->wg_state.ProgramIdentifier.OpaqueData[0];

    if (!wg_state)
    {
        WARN("WG state is not set.\n");
        return;
    }

    /* GPU input will be very awkward to support well without DGCC.
     * Multi CPU input should be fairly simple, but not particularly interesting for bringup. */
    if (desc->Mode != D3D12_DISPATCH_MODE_NODE_CPU_INPUT)
    {
        FIXME("Unsupported input type: %u\n", desc->Mode);
        return;
    }

    if (desc->NodeCPUInput.NumRecords == 0)
        return;

    program = &wg_state->programs[wg_state_program_index];

    if (desc->NodeCPUInput.EntrypointIndex >= program->levels[0].nodes_count)
    {
        ERR("EntryPointIndex %u is out of bounds.\n", desc->NodeCPUInput.EntrypointIndex);
        return;
    }

    d3d12_command_list_end_current_render_pass(list, false);
    d3d12_command_list_debug_mark_begin_region(list, "ClearUAV");

    d3d12_command_list_invalidate_current_pipeline(list, true);
    d3d12_command_list_invalidate_root_parameters(list, &list->compute_bindings, true, &list->graphics_bindings);
    d3d12_command_list_update_descriptor_buffers(list);

    node_index = program->levels[0].nodes[desc->NodeCPUInput.EntrypointIndex];
    payload_size = desc->NodeCPUInput.NumRecords * desc->NodeCPUInput.RecordStrideInBytes;
    if (desc->NodeCPUInput.RecordStrideInBytes == 0)
        payload_size = wg_state->entry_points[node_index].node_input->payload_stride;

    if (!d3d12_command_allocator_allocate_scratch_memory(list->allocator,
            VKD3D_SCRATCH_POOL_KIND_UNIFORM_UPLOAD, payload_size, 64, ~0u, &scratch))
        return;

    /* Alternatively use vkCmdUpdateBuffer, but at least for now, just rely on ReBAR doing its thing. */
    memcpy(scratch.host_ptr, desc->NodeCPUInput.pRecords, payload_size);
}
