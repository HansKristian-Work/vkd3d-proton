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


