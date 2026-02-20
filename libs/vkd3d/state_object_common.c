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

bool vkd3d_export_equal(LPCWSTR export, const struct vkd3d_shader_library_entry_point *entry)
{
    return vkd3d_export_strequal(export, entry->mangled_entry_point) ||
            vkd3d_export_strequal(export, entry->plain_entry_point);
}

bool d3d12_state_object_association_data_equal(
        const struct d3d12_state_object_association *a,
        const struct d3d12_state_object_association *b)
{
    /* Normalize dummy root signatures. */
    if (a && (a->kind == VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE ||
            a->kind == VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE))
    {
        if (!a->root_signature || a->root_signature->layout_compatibility_hash == 0)
            a = NULL;
    }

    if (b && (b->kind == VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE ||
            b->kind == VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE))
    {
        if (!b->root_signature || b->root_signature->layout_compatibility_hash == 0)
            b = NULL;
    }

    if (!a && !b)
        return true;
    if ((!!a) != (!!b))
        return false;
    if (a->kind != b->kind)
        return false;

    switch (a->kind)
    {
        case VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_PIPELINE_CONFIG1:
            return memcmp(&a->pipeline_config, &b->pipeline_config, sizeof(a->pipeline_config)) == 0;
        case VKD3D_SHADER_SUBOBJECT_KIND_RAYTRACING_SHADER_CONFIG:
            return memcmp(&a->shader_config, &b->shader_config, sizeof(a->shader_config)) == 0;
        case VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE:
        case VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE:
            /* Here we need exact checks so we can resolve conflicts. */
            return d3d12_root_signature_is_pipeline_compatible(a->root_signature, b->root_signature);

        default:
            break;
    }

    return false;
}

static bool d3d12_state_object_find_explicit_assignment_override(
        enum vkd3d_shader_subobject_kind kind,
        const struct d3d12_state_object_association *associations,
        size_t associations_count,
        const struct d3d12_state_object_association *association)
{
    size_t i;

    for (i = 0; i < associations_count; i++)
    {
        /* Don't care about explicit DXIL subobject to entry point assignments since even default declared RTPSO objects
         * will override them. */
        if (associations[i].priority != VKD3D_ASSOCIATION_PRIORITY_EXPLICIT)
            continue;

        if (d3d12_state_object_association_data_equal(association, &associations[i]))
            return true;
    }

    return false;
}

const struct d3d12_state_object_association *d3d12_state_object_find_association(
        enum vkd3d_shader_subobject_kind kind,
        const struct d3d12_state_object_association *associations,
        size_t associations_count,
        const struct D3D12_HIT_GROUP_DESC **hit_groups,
        size_t hit_groups_count,
        const struct vkd3d_shader_library_entry_point *entry,
        LPCWSTR export)
{
    const struct d3d12_state_object_association *hit_group_association = NULL;
    const struct d3d12_state_object_association *association = NULL;
    const D3D12_HIT_GROUP_DESC *hit_group;
    bool conflict = false;
    bool match;
    size_t i;

    for (i = 0; i < associations_count; i++)
    {
        if (associations[i].kind != kind)
            continue;
        if (association && associations[i].priority < association->priority)
            continue;

        if (associations[i].export)
        {
            if (entry)
                match = vkd3d_export_equal(associations[i].export, entry);
            else
                match = vkd3d_export_strequal(associations[i].export, export);
        }
        else
            match = true;

        if (match)
        {
            if (!association || associations[i].priority > association->priority)
            {
                association = &associations[i];
                conflict = false;
            }
            else if (!d3d12_state_object_association_data_equal(association, &associations[i]))
            {
                /* We might get a higher priority match later that makes this conflict irrelevant. */
                conflict = true;

                if (association->priority == VKD3D_ASSOCIATION_PRIORITY_DECLARED_STATE_OBJECT)
                {
                    /* Attempt to tie-break. There is a special rule where if an object is assigned explicitly,
                     * it takes lower precedence when it comes to resolving default associations.
                     * Attempt to tie-break this conflict.
                     * Normally, explicit default assignment should be used, but it is not required :( */
                    bool has_explicit_association_candidate;
                    bool has_explicit_association_existing;

                    has_explicit_association_existing =
                            d3d12_state_object_find_explicit_assignment_override(
                                    kind, associations, associations_count, association);
                    has_explicit_association_candidate =
                            d3d12_state_object_find_explicit_assignment_override(
                                    kind, associations, associations_count, &associations[i]);

                    if (has_explicit_association_candidate != has_explicit_association_existing)
                    {
                        /* Somewhat inverse. If the existing association has an explicit one, discount it here
                         * and accept the new one. */
                        if (has_explicit_association_existing)
                            association = &associations[i];
                        conflict = false;
                    }
                }
            }
        }
    }

    hit_group_association = NULL;

    /* If we didn't find an association for this entry point, we might have an association
     * in a hit group export. Alternatively, we might have a higher priority association which is only
     * set for the hit group.
     * FIXME: Is it possible to have multiple hit groups, all referring to same entry point, while using
     * different root signatures for the different instances of the entry point? :| */
    if (entry && (entry->stage == VK_SHADER_STAGE_ANY_HIT_BIT_KHR ||
            entry->stage == VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR ||
            entry->stage == VK_SHADER_STAGE_INTERSECTION_BIT_KHR))
    {
        for (i = 0; i < hit_groups_count; i++)
        {
            hit_group = hit_groups[i];

            match = vkd3d_export_equal(hit_group->ClosestHitShaderImport, entry) ||
                    vkd3d_export_equal(hit_group->AnyHitShaderImport, entry) ||
                    vkd3d_export_equal(hit_group->IntersectionShaderImport, entry);

            if (match)
            {
                hit_group_association = d3d12_state_object_find_association(
                        kind, associations, associations_count, hit_groups, hit_groups_count, NULL, hit_group->HitGroupExport);

                /* Accept hit group association if it has a higher priority, otherwise tie-break to the export itself. */
                if (hit_group_association && hit_group_association->priority > association->priority)
                {
                    association = hit_group_association;
                    conflict = false;
                    break;
                }
            }
        }
    }

    if (conflict)
    {
        /* Ending up with NULL might be intentional. It is only an error if a shader accesses resources that needs a
         * particular root signature. State objects however are more fatal since they are required. */
        if (kind == VKD3D_SHADER_SUBOBJECT_KIND_LOCAL_ROOT_SIGNATURE)
            WARN("Conflicting local root signatures defined for same export, using NULL local root signature.\n");
        else if (kind == VKD3D_SHADER_SUBOBJECT_KIND_GLOBAL_ROOT_SIGNATURE)
            WARN("Conflicting global root signatures defined for same export, using NULL global root signature.\n");
        else
            ERR("Conflicting state object defined for same export.\n");
        return NULL;
    }

    return association;
}

struct vkd3d_fused_root_signature_mappings *d3d12_state_object_fuse_root_signature_mappings(
        struct d3d12_root_signature *global, struct d3d12_root_signature *local)
{
    /* Need a fused mapping table. */
    uint32_t num_mappings = global->mapping_info.mappingCount + local->mapping_info.mappingCount;
    struct vkd3d_fused_root_signature_mappings *fused;

    fused = vkd3d_calloc(1, offsetof(struct vkd3d_fused_root_signature_mappings, mappings) +
            num_mappings * sizeof(VkDescriptorSetAndBindingMappingEXT));
    fused->mapping_info.sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT;
    fused->mapping_info.mappingCount = num_mappings;
    fused->mapping_info.pMappings = fused->mappings;

    memcpy(fused->mappings,
            global->mapping_info.pMappings,
            global->mapping_info.mappingCount * sizeof(*fused->mappings));

    memcpy(fused->mappings + global->mapping_info.mappingCount,
            local->mapping_info.pMappings,
            local->mapping_info.mappingCount * sizeof(*fused->mappings));

    return fused;
}

struct vkd3d_fused_root_signature_mappings *d3d12_state_object_build_workgraph_root_signature_mappings(
        struct d3d12_root_signature *global, struct d3d12_root_signature *local)
{
    struct vkd3d_fused_root_signature_mappings *fused;
    uint32_t num_mappings = 0;
    uint32_t i;

    if (global)
        num_mappings += global->mapping_info.mappingCount;
    if (local)
        num_mappings += local->mapping_info.mappingCount;

    fused = vkd3d_calloc(1, offsetof(struct vkd3d_fused_root_signature_mappings, mappings) +
            num_mappings * sizeof(VkDescriptorSetAndBindingMappingEXT));
    fused->mapping_info.sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT;
    fused->mapping_info.mappingCount = num_mappings;
    fused->mapping_info.pMappings = fused->mappings;

    if (global)
    {
        memcpy(fused->mappings,
                global->mapping_info.pMappings,
                global->mapping_info.mappingCount * sizeof(*fused->mappings));
    }

    if (local)
    {
        memcpy(fused->mappings + (global ? global->mapping_info.mappingCount : 0),
                local->mapping_info.pMappings,
                local->mapping_info.mappingCount * sizeof(*fused->mappings));
    }

    /* Reroute to INDIRECT tokens instead. Gotta love how flexible this is :3 */
    for (i = 0; i < num_mappings; i++)
    {
        VkDescriptorSetAndBindingMappingEXT tmp = fused->mappings[i];
        switch (tmp.source)
        {
            case VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_DATA_EXT:
                fused->mappings[i].source = VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_ADDRESS_EXT;
                fused->mappings[i].sourceData.pushAddressOffset =
                        offsetof(struct vkd3d_shader_node_input_push_signature, root_parameter_bda);
                break;

            case VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_ADDRESS_EXT:
                fused->mappings[i].source = VK_DESCRIPTOR_MAPPING_SOURCE_INDIRECT_ADDRESS_EXT;
                fused->mappings[i].sourceData.indirectAddress.pushOffset =
                        offsetof(struct vkd3d_shader_node_input_push_signature, root_parameter_bda);
                fused->mappings[i].sourceData.indirectAddress.addressOffset = tmp.sourceData.pushAddressOffset;
                break;

            case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT:
                fused->mappings[i].source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_INDIRECT_INDEX_EXT;
                memset(&fused->mappings[i].sourceData.indirectIndex, 0, sizeof(fused->mappings[i].sourceData.indirectIndex));
                fused->mappings[i].sourceData.indirectIndex.pushOffset =
                        offsetof(struct vkd3d_shader_node_input_push_signature, root_parameter_bda);
                fused->mappings[i].sourceData.indirectIndex.heapOffset = tmp.sourceData.pushIndex.heapOffset;
                fused->mappings[i].sourceData.indirectIndex.heapIndexStride = tmp.sourceData.pushIndex.heapIndexStride;
                fused->mappings[i].sourceData.indirectIndex.heapArrayStride = tmp.sourceData.pushIndex.heapArrayStride;
                fused->mappings[i].sourceData.indirectIndex.addressOffset = tmp.sourceData.pushIndex.pushOffset;
                break;

            case VK_DESCRIPTOR_MAPPING_SOURCE_SHADER_RECORD_DATA_EXT:
                fused->mappings[i].source = VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_ADDRESS_EXT;
                fused->mappings[i].sourceData.pushAddressOffset =
                        offsetof(struct vkd3d_shader_node_input_push_signature, local_root_signature_bda);
                if (tmp.sourceData.shaderRecordDataOffset != 0)
                    FIXME("WorkGraph with SHADER_RECORD_DATA_EXT needs explicit local root signature lowering.\n");
                break;

            case VK_DESCRIPTOR_MAPPING_SOURCE_SHADER_RECORD_ADDRESS_EXT:
                fused->mappings[i].source = VK_DESCRIPTOR_MAPPING_SOURCE_INDIRECT_ADDRESS_EXT;
                fused->mappings[i].sourceData.indirectAddress.pushOffset =
                        offsetof(struct vkd3d_shader_node_input_push_signature, local_root_signature_bda);
                fused->mappings[i].sourceData.indirectAddress.addressOffset = tmp.sourceData.pushAddressOffset;
                break;

            case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_SHADER_RECORD_INDEX_EXT:
                fused->mappings[i].source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_INDIRECT_INDEX_EXT;
                fused->mappings[i].sourceData.indirectIndex.pushOffset =
                        offsetof(struct vkd3d_shader_node_input_push_signature, local_root_signature_bda);
                fused->mappings[i].sourceData.indirectIndex.heapOffset = tmp.sourceData.shaderRecordIndex.heapOffset;
                fused->mappings[i].sourceData.indirectIndex.heapIndexStride = tmp.sourceData.shaderRecordIndex.heapIndexStride;
                fused->mappings[i].sourceData.indirectIndex.heapArrayStride = tmp.sourceData.shaderRecordIndex.heapArrayStride;
                fused->mappings[i].sourceData.indirectIndex.addressOffset = tmp.sourceData.shaderRecordIndex.shaderRecordOffset;
                break;

            default:
                break;
        }
    }

    return fused;
}
