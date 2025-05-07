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

struct raytracing_test_context
{
    struct test_context context;
    ID3D12Device5 *device5;
    ID3D12GraphicsCommandList4 *list4;
};

static void destroy_raytracing_test_context(struct raytracing_test_context *context)
{
    ID3D12Device5_Release(context->device5);
    ID3D12GraphicsCommandList4_Release(context->list4);
    destroy_test_context(&context->context);
}

static bool init_raytracing_test_context(struct raytracing_test_context *context, D3D12_RAYTRACING_TIER tier)
{
    if (!init_compute_test_context(&context->context))
        return false;

    if (!context_supports_dxil(&context->context))
    {
        destroy_test_context(&context->context);
        return false;
    }

    if (FAILED(ID3D12Device_QueryInterface(context->context.device, &IID_ID3D12Device5, (void**)&context->device5)))
    {
        skip("ID3D12Device5 is not supported. Skipping RT test.\n");
        destroy_test_context(&context->context);
        return false;
    }

    if (FAILED(ID3D12GraphicsCommandList_QueryInterface(context->context.list, &IID_ID3D12GraphicsCommandList4, (void**)&context->list4)))
    {
        skip("ID3D12GraphicsCommandList4 is not supported. Skipping RT test.\n");
        ID3D12Device5_Release(context->device5);
        destroy_test_context(&context->context);
        return false;
    }

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5;
        if (FAILED(ID3D12Device5_CheckFeatureSupport(context->device5, D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5))) ||
                opts5.RaytracingTier < tier)
        {
            skip("Raytracing tier #%x is not supported on this device. Skipping RT test.\n", tier);
            ID3D12Device5_Release(context->device5);
            ID3D12GraphicsCommandList4_Release(context->list4);
            destroy_test_context(&context->context);
            return false;
        }
    }

    return true;
}

static D3D12_SHADER_BYTECODE get_rayquery_shader(void)
{
#include "shaders/rt/headers/rayquery.h"
    return rayquery_dxil;
}

static D3D12_SHADER_BYTECODE get_static_sampler_rt_lib(void)
{
#include "shaders/rt/headers/static_sampler_rt_lib.h"
    return static_sampler_rt_lib_dxil;
}

static D3D12_SHADER_BYTECODE get_default_assignment_bindings_rt_lib(void)
{
#include "shaders/rt/headers/default_assignment_bindings_rt.h"
    return default_assignment_bindings_rt_dxil;
}

static D3D12_SHADER_BYTECODE get_dummy_raygen_rt_lib(void)
{
#include "shaders/rt/headers/dummy_raygen.h"
    return dummy_raygen_dxil;
}

static D3D12_SHADER_BYTECODE get_embedded_root_signature_subobject_rt_lib(void)
{
#include "shaders/rt/headers/embedded_root_signature_subobject_rt.h"
    return embedded_root_signature_subobject_rt_dxil;
}

static D3D12_SHADER_BYTECODE get_embedded_root_signature_subobject_rt_lib_conflict(void)
{
#include "shaders/rt/headers/embedded_root_signature_subobject_rt_conflict.h"
    return embedded_root_signature_subobject_rt_conflict_dxil;
}

static D3D12_SHADER_BYTECODE get_embedded_root_signature_subobject_rt_lib_conflict_mixed(void)
{
#include "shaders/rt/headers/embedded_root_signature_subobject_rt_conflict_mixed.h"
    return embedded_root_signature_subobject_rt_conflict_mixed_dxil;
}

static D3D12_SHADER_BYTECODE get_embedded_subobject_rt_lib(void)
{
#include "shaders/rt/headers/embedded_subobject.h"
    return embedded_subobject_dxil;
}

static D3D12_SHADER_BYTECODE get_embedded_subobject_dupe_rt_lib(void)
{
#include "shaders/rt/headers/embedded_subobject_dupe.h"
    return embedded_subobject_dupe_dxil;
}

static D3D12_SHADER_BYTECODE get_default_rt_lib(void)
{
#include "shaders/rt/headers/default.h"
    return default_dxil;
}

static D3D12_SHADER_BYTECODE get_multi_rs_lib(void)
{
#include "shaders/rt/headers/multi_rs.h"
    return multi_rs_dxil;
}

static D3D12_SHADER_BYTECODE get_misfire_lib(void)
{
#include "shaders/rt/headers/misfire.h"
    return misfire_dxil;
}

struct initial_vbo
{
    float f32[3 * 3 * 2];
    int16_t i16[3 * 3 * 2];
    uint16_t f16[3 * 3 * 2];
};

struct initial_ibo
{
    uint32_t u32[6];
    uint16_t u16[6];
};

struct test_geometry
{
    ID3D12Resource *vbo;
    ID3D12Resource *zero_vbo;
    ID3D12Resource *ibo;
};

static void destroy_test_geometry(struct test_geometry *geom)
{
    ID3D12Resource_Release(geom->vbo);
    ID3D12Resource_Release(geom->zero_vbo);
    ID3D12Resource_Release(geom->ibo);
}

static void init_test_geometry(ID3D12Device *device, struct test_geometry *geom)
{
    unsigned int i;

    /* Emit quads with the different Tier 1.0 formats. */
    {
        struct initial_vbo initial_vbo_data;
        float *pv = initial_vbo_data.f32;
        *pv++ = -1.0f; *pv++ = +1.0f; *pv++ = 0.0f;
        *pv++ = -1.0f; *pv++ = -1.0f; *pv++ = 0.0f;
        *pv++ = +1.0f; *pv++ = -1.0f; *pv++ = 0.0f;

        *pv++ = +1.0f; *pv++ = +1.0f; *pv++ = 0.0f;
        *pv++ = -1.0f; *pv++ = +1.0f; *pv++ = 0.0f;
        *pv++ = +1.0f; *pv++ = -1.0f; *pv++ = 0.0f;

        for (i = 0; i < 3 * 3 * 2; i++)
        {
            initial_vbo_data.i16[i] = (int16_t)(0x7fff * initial_vbo_data.f32[i]);
            initial_vbo_data.f16[i] = 0x3c00 | (initial_vbo_data.f32[i] < 0.0f ? 0x8000 : 0);
        }

        geom->vbo = create_upload_buffer(device, sizeof(initial_vbo_data), &initial_vbo_data);
        geom->zero_vbo = create_default_buffer(device, sizeof(initial_vbo_data), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    {
        static const struct initial_ibo initial_ibo_data = {
            { 0, 1, 2, 3, 2, 1 },
            { 0, 1, 2, 3, 2, 1 },
        };
        geom->ibo = create_upload_buffer(device, sizeof(initial_ibo_data), &initial_ibo_data);
    }
}

static ID3D12Resource *create_transform_buffer(ID3D12Device *device, unsigned int count, float x_stride)
{
    ID3D12Resource *transform_buffer;
    float *transform;
    unsigned int i;

    transform = calloc(12 * count, sizeof(float));
    for (i = 0; i < count; i++)
    {
        /* Row-major affine transform. */
        transform[12 * i + 0] = 1.0f;
        transform[12 * i + 5] = 1.0f;
        transform[12 * i + 10] = 1.0f;
        transform[12 * i + 3] = x_stride * (float)i;
    }

    transform_buffer = create_upload_buffer(device, 12 * count * sizeof(float), transform);
    free(transform);
    return transform_buffer;
}

static ID3D12Resource *create_aabb_buffer(ID3D12Device *device,
        unsigned int geom_count, unsigned int aabb_per_geom, float x_stride)
{
    D3D12_RAYTRACING_AABB *aabbs = calloc(geom_count * aabb_per_geom, sizeof(*aabbs));
    ID3D12Resource *aabb_buffer;
    D3D12_RAYTRACING_AABB *tmp;
    unsigned int geom, aabb;

    for (geom = 0; geom < geom_count; geom++)
    {
        for (aabb = 0; aabb < aabb_per_geom; aabb++)
        {
            /* Only the last AABB in a geom series is considered "active", as this is to test AABBStride parameter.
             * The other ones are placed at a large, degenerate Z offset as to be ignored. */
            tmp = &aabbs[geom * aabb_per_geom + aabb];
            tmp->MinX = -0.5f + (float)geom * x_stride;
            tmp->MaxX = +0.5f + (float)geom * x_stride;
            tmp->MinY = -0.5f;
            tmp->MaxY = +0.5f;

            if (aabb + 1 == aabb_per_geom)
            {
                tmp->MinZ = -0.5f;
                tmp->MaxZ = +0.5f;
            }
            else
            {
                tmp->MinZ = -10000.0f;
                tmp->MaxZ = -9000.0f;
            }
        }
    }

    aabb_buffer = create_upload_buffer(device, geom_count * aabb_per_geom * sizeof(*aabbs), aabbs);
    free(aabbs);
    return aabb_buffer;
}

struct rt_acceleration_structure
{
    ID3D12Resource *scratch;
    ID3D12Resource *scratch_update;
    ID3D12Resource *rtas;
};

static ID3D12Resource *duplicate_acceleration_structure(struct raytracing_test_context *context,
        ID3D12Resource *rtas, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE mode)
{
    ID3D12Resource *new_rtas;

    new_rtas = create_default_buffer(context->context.device, ID3D12Resource_GetDesc(rtas).Width,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

    ID3D12GraphicsCommandList4_CopyRaytracingAccelerationStructure(context->list4,
            ID3D12Resource_GetGPUVirtualAddress(new_rtas),
            ID3D12Resource_GetGPUVirtualAddress(rtas), mode);

    uav_barrier(context->context.list, new_rtas);
    return new_rtas;
}

static void update_acceleration_structure(struct raytracing_test_context *context,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *inputs,
        struct rt_acceleration_structure *rtas)
{
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_info;
    build_info.DestAccelerationStructureData = ID3D12Resource_GetGPUVirtualAddress(rtas->rtas);
    /* In-place update is supported. */
    build_info.SourceAccelerationStructureData = ID3D12Resource_GetGPUVirtualAddress(rtas->rtas);
    build_info.Inputs = *inputs;
    build_info.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    build_info.ScratchAccelerationStructureData = ID3D12Resource_GetGPUVirtualAddress(rtas->scratch_update);

    ID3D12GraphicsCommandList4_BuildRaytracingAccelerationStructure(context->list4, &build_info, 0, NULL);
    uav_barrier(context->context.list, rtas->rtas);
    uav_barrier(context->context.list, rtas->scratch_update);
}

static void create_acceleration_structure(struct raytracing_test_context *context,
        const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *inputs,
        struct rt_acceleration_structure *rtas, D3D12_GPU_VIRTUAL_ADDRESS postbuild_va)
{
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postbuild_desc[3];
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild_info;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build_info;

    /* Guard against stubbed variant. */
    prebuild_info.ScratchDataSizeInBytes = 16;
    prebuild_info.ResultDataMaxSizeInBytes = 16;
    prebuild_info.UpdateScratchDataSizeInBytes = 16;
    ID3D12Device5_GetRaytracingAccelerationStructurePrebuildInfo(context->device5, inputs, &prebuild_info);

    /* An AS in D3D12 is just a plain UAV-enabled buffer, similar with scratch buffers. */
    rtas->scratch = create_default_buffer(context->context.device,
            prebuild_info.ScratchDataSizeInBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (inputs->Flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE)
    {
        rtas->scratch_update = create_default_buffer(context->context.device,
                prebuild_info.UpdateScratchDataSizeInBytes,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    else
        rtas->scratch_update = NULL;

    /* Verify how the build mode behaves here. */
    if (inputs->Flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE)
    {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tmp_inputs = *inputs;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tmp_prebuild_info;

        memset(&tmp_prebuild_info, 0, sizeof(tmp_prebuild_info));

        /* We should ignore this flag when querying prebuild info.
         * Deduced by: Equal sizes, no validation errors.
         * Vulkan does not seem to care either, and this trips validation
         * errors in neither D3D12 nor Vulkan. */

         /* If ALLOW_UPDATE is not set, AMD Windows driver freaks out a little and reports weird values,
          * so it's not completely ignored. It likely checks for either PERFORM or ALLOW_UPDATE when reporting. */

        tmp_inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        ID3D12Device5_GetRaytracingAccelerationStructurePrebuildInfo(context->device5, &tmp_inputs, &tmp_prebuild_info);
        ok(tmp_prebuild_info.ResultDataMaxSizeInBytes == prebuild_info.ResultDataMaxSizeInBytes,
                "Size mismatch, %"PRIu64" != %"PRIu64".\n",
                tmp_prebuild_info.ResultDataMaxSizeInBytes,
                prebuild_info.ResultDataMaxSizeInBytes);
        ok(tmp_prebuild_info.ScratchDataSizeInBytes == prebuild_info.ScratchDataSizeInBytes,
                "Size mismatch, %"PRIu64" != %"PRIu64".\n",
                tmp_prebuild_info.ScratchDataSizeInBytes,
                prebuild_info.ScratchDataSizeInBytes);
        ok(tmp_prebuild_info.UpdateScratchDataSizeInBytes == prebuild_info.UpdateScratchDataSizeInBytes,
                "Size mismatch, %"PRIu64" != %"PRIu64".\n",
                tmp_prebuild_info.UpdateScratchDataSizeInBytes,
                prebuild_info.UpdateScratchDataSizeInBytes);
    }

    rtas->rtas = create_default_buffer(context->context.device,
            prebuild_info.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

    build_info.DestAccelerationStructureData = ID3D12Resource_GetGPUVirtualAddress(rtas->rtas);
    build_info.Inputs = *inputs;
    build_info.ScratchAccelerationStructureData = ID3D12Resource_GetGPUVirtualAddress(rtas->scratch);
    build_info.SourceAccelerationStructureData = 0;

    postbuild_desc[0].InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
    postbuild_desc[0].DestBuffer = postbuild_va;
    postbuild_desc[1].InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_CURRENT_SIZE;
    postbuild_desc[1].DestBuffer = postbuild_desc[0].DestBuffer + sizeof(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE_DESC);
    postbuild_desc[2].InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION;
    postbuild_desc[2].DestBuffer = postbuild_desc[1].DestBuffer + sizeof(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_CURRENT_SIZE_DESC);

    ID3D12GraphicsCommandList4_BuildRaytracingAccelerationStructure(context->list4, &build_info,
            postbuild_va ? ARRAY_SIZE(postbuild_desc) : 0, postbuild_desc);
    uav_barrier(context->context.list, rtas->rtas);
    uav_barrier(context->context.list, rtas->scratch);
}

static void destroy_acceleration_structure(struct rt_acceleration_structure *rtas)
{
    ID3D12Resource_Release(rtas->scratch);
    ID3D12Resource_Release(rtas->rtas);
    if (rtas->scratch_update)
        ID3D12Resource_Release(rtas->scratch_update);
}

struct test_rt_geometry
{
    ID3D12Resource *bottom_acceleration_structures_tri[3];
    ID3D12Resource *bottom_acceleration_structures_aabb[3];
    ID3D12Resource *top_acceleration_structures[3];
    struct rt_acceleration_structure bottom_rtas_tri;
    struct rt_acceleration_structure bottom_rtas_aabb;
    struct rt_acceleration_structure top_rtas;
    ID3D12Resource *transform_buffer;
    ID3D12Resource *aabb_buffer;
    ID3D12Resource *instance_buffer;
};

static void destroy_rt_geometry(struct test_rt_geometry *rt_geom)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(rt_geom->bottom_acceleration_structures_tri); i++)
        ID3D12Resource_Release(rt_geom->bottom_acceleration_structures_tri[i]);
    for (i = 0; i < ARRAY_SIZE(rt_geom->bottom_acceleration_structures_aabb); i++)
        ID3D12Resource_Release(rt_geom->bottom_acceleration_structures_aabb[i]);
    for (i = 0; i < ARRAY_SIZE(rt_geom->top_acceleration_structures); i++)
        ID3D12Resource_Release(rt_geom->top_acceleration_structures[i]);
    ID3D12Resource_Release(rt_geom->transform_buffer);
    ID3D12Resource_Release(rt_geom->aabb_buffer);
    ID3D12Resource_Release(rt_geom->instance_buffer);
    destroy_acceleration_structure(&rt_geom->bottom_rtas_tri);
    destroy_acceleration_structure(&rt_geom->bottom_rtas_aabb);
    destroy_acceleration_structure(&rt_geom->top_rtas);
}

static bool instance_index_is_aabb(unsigned int index)
{
    return !!(index & 2);
}

static void init_rt_geometry(struct raytracing_test_context *context, struct test_rt_geometry *rt_geom,
        struct test_geometry *geom,
        unsigned int num_geom_desc, float geom_offset_x,
        unsigned int num_unmasked_instances_y, float instance_geom_scale, float instance_offset_y,
        D3D12_GPU_VIRTUAL_ADDRESS postbuild_va)
{
#define NUM_GEOM_TEMPLATES 6
#define NUM_AABB_PER_GEOM 2
    D3D12_RAYTRACING_GEOMETRY_DESC geom_desc_template[NUM_GEOM_TEMPLATES];
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs;
    D3D12_RAYTRACING_INSTANCE_DESC *instance_desc;
    D3D12_RAYTRACING_GEOMETRY_DESC *geom_desc;
    unsigned int i;

    /* Create X * Y quads where X = num_geom_desc, and Y = num_unmasked_instances_y.
     * Additionally, create a set of quads at Y iteration -1 which are intended to be masked
     * (miss shader should be triggered).
     * The intention is that hit SBT index is row-linear, i.e. hit index = Y * num_geom_desc + X.
     * Size and placement of the quads are controlled by parameters. */

    rt_geom->transform_buffer = create_transform_buffer(context->context.device, num_geom_desc, geom_offset_x);
    rt_geom->aabb_buffer = create_aabb_buffer(context->context.device, num_geom_desc, NUM_AABB_PER_GEOM, geom_offset_x);
    memset(geom_desc_template, 0, sizeof(geom_desc_template));

    /* Setup a template for different geom descs which all effectively do the same thing, a simple quad. */
    /* Tests the configuration space of the 6 supported vertex formats, and the 3 index types. */
    geom_desc_template[0].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;

    geom_desc_template[0].Triangles.VertexBuffer.StartAddress =
            ID3D12Resource_GetGPUVirtualAddress(geom->vbo) + offsetof(struct initial_vbo, f32);
    geom_desc_template[0].Triangles.VertexBuffer.StrideInBytes = 3 * sizeof(float);
    geom_desc_template[0].Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geom_desc_template[0].Triangles.VertexCount = 6;

    geom_desc_template[1] = geom_desc_template[0];
    /* First, render something wrong, update the RTAS later and verify that it works. */
    geom_desc_template[1].Triangles.VertexBuffer.StartAddress =
            ID3D12Resource_GetGPUVirtualAddress(geom->zero_vbo) + offsetof(struct initial_vbo, f32);
    geom_desc_template[1].Triangles.VertexFormat = DXGI_FORMAT_R32G32_FLOAT;

    geom_desc_template[2].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geom_desc_template[2].Triangles.VertexBuffer.StartAddress =
            ID3D12Resource_GetGPUVirtualAddress(geom->vbo) + offsetof(struct initial_vbo, i16);
    geom_desc_template[2].Triangles.VertexBuffer.StrideInBytes = 3 * sizeof(int16_t);
    geom_desc_template[2].Triangles.VertexFormat = DXGI_FORMAT_R16G16B16A16_SNORM;
    geom_desc_template[2].Triangles.VertexCount = 4;
    geom_desc_template[2].Triangles.IndexBuffer =
            ID3D12Resource_GetGPUVirtualAddress(geom->ibo) + offsetof(struct initial_ibo, u16);
    geom_desc_template[2].Triangles.IndexFormat = DXGI_FORMAT_R16_UINT;
    geom_desc_template[2].Triangles.IndexCount = 6;

    geom_desc_template[3] = geom_desc_template[2];
    geom_desc_template[3].Triangles.VertexFormat = DXGI_FORMAT_R16G16_SNORM;
    geom_desc_template[3].Triangles.IndexBuffer =
            ID3D12Resource_GetGPUVirtualAddress(geom->ibo) + offsetof(struct initial_ibo, u32);
    geom_desc_template[3].Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;

    geom_desc_template[4] = geom_desc_template[2];
    geom_desc_template[4].Triangles.VertexBuffer.StartAddress =
            ID3D12Resource_GetGPUVirtualAddress(geom->vbo) + offsetof(struct initial_vbo, f16);
    geom_desc_template[4].Triangles.VertexFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    geom_desc_template[5] = geom_desc_template[3];
    geom_desc_template[5].Triangles.VertexBuffer.StartAddress =
            ID3D12Resource_GetGPUVirtualAddress(geom->vbo) + offsetof(struct initial_vbo, f16);
    geom_desc_template[5].Triangles.VertexFormat = DXGI_FORMAT_R16G16_FLOAT;

    /* Every other geometry desc is considered opaque.
     * Lets us test any-hit invocation rules. */
    for (i = 0; i < ARRAY_SIZE(geom_desc_template); i++)
    {
        geom_desc_template[i].Flags = (i & 1) ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        geom_desc_template[i].Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
    }

    /* Create bottom AS. One quad is centered around origin, but other triangles are translated. */
    memset(&inputs, 0, sizeof(inputs));
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = num_geom_desc;
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION |
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

    geom_desc = malloc(sizeof(*geom_desc) * num_geom_desc);
    for (i = 0; i < num_geom_desc; i++)
        geom_desc[i] = geom_desc_template[i % ARRAY_SIZE(geom_desc_template)];
    inputs.pGeometryDescs = geom_desc;

    /* Identity transform for index 0, checks that we handle NULL here for geom_desc 0. */
    for (i = 1; i < num_geom_desc; i++)
    {
        geom_desc[i].Triangles.Transform3x4 =
                ID3D12Resource_GetGPUVirtualAddress(rt_geom->transform_buffer) + i * 4 * 3 * sizeof(float);
    }

    create_acceleration_structure(context, &inputs, &rt_geom->bottom_rtas_tri, postbuild_va);

    /* Update, and now use correct VBO. */
    geom_desc_template[1].Triangles.VertexBuffer.StartAddress =
            ID3D12Resource_GetGPUVirtualAddress(geom->vbo) + offsetof(struct initial_vbo, f32);
    for (i = 1; i < num_geom_desc; i += ARRAY_SIZE(geom_desc_template))
        geom_desc[i].Triangles.VertexBuffer = geom_desc_template[1].Triangles.VertexBuffer;
    update_acceleration_structure(context, &inputs, &rt_geom->bottom_rtas_tri);

    for (i = 0; i < num_geom_desc; i++)
    {
        geom_desc[i].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
        geom_desc[i].AABBs.AABBCount = NUM_AABB_PER_GEOM;
        geom_desc[i].AABBs.AABBs.StartAddress = ID3D12Resource_GetGPUVirtualAddress(rt_geom->aabb_buffer) +
                i * geom_desc[i].AABBs.AABBCount * sizeof(D3D12_RAYTRACING_AABB);
        geom_desc[i].AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);
    }

    create_acceleration_structure(context, &inputs, &rt_geom->bottom_rtas_aabb, 0);

    /* Tests CLONE and COMPACTING copies. COMPACTING can never increase size, so it's safe to allocate up front.
     * We test the compacted size later. */
    rt_geom->bottom_acceleration_structures_tri[0] = rt_geom->bottom_rtas_tri.rtas;
    ID3D12Resource_AddRef(rt_geom->bottom_rtas_tri.rtas);
    rt_geom->bottom_acceleration_structures_tri[1] = duplicate_acceleration_structure(context,
            rt_geom->bottom_acceleration_structures_tri[0],
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
    rt_geom->bottom_acceleration_structures_tri[2] = duplicate_acceleration_structure(context,
            rt_geom->bottom_acceleration_structures_tri[1],
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE);

    rt_geom->bottom_acceleration_structures_aabb[0] = rt_geom->bottom_rtas_aabb.rtas;
    ID3D12Resource_AddRef(rt_geom->bottom_rtas_aabb.rtas);
    rt_geom->bottom_acceleration_structures_aabb[1] = duplicate_acceleration_structure(context,
            rt_geom->bottom_acceleration_structures_aabb[0],
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
    rt_geom->bottom_acceleration_structures_aabb[2] = duplicate_acceleration_structure(context,
            rt_geom->bottom_acceleration_structures_aabb[1],
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE);

    /* Create instance buffer. One for every top-level entry into the AS. */
    instance_desc = calloc(num_unmasked_instances_y + 1, sizeof(*instance_desc));

    for (i = 0; i < num_unmasked_instances_y; i++)
    {
        instance_desc[i].Transform[0][0] = instance_geom_scale;
        instance_desc[i].Transform[1][1] = instance_geom_scale;
        instance_desc[i].Transform[2][2] = instance_geom_scale;
        instance_desc[i].Transform[1][3] = instance_offset_y * (float)i;
        instance_desc[i].InstanceMask = 0xff;
        instance_desc[i].InstanceContributionToHitGroupIndex = num_geom_desc * i;
        if (i == 0 || i == 2)
            instance_desc[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
        else if (i == 1 || i == 3)
            instance_desc[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;

        if (instance_index_is_aabb(i))
        {
            instance_desc[i].AccelerationStructure =
                    ID3D12Resource_GetGPUVirtualAddress(rt_geom->bottom_acceleration_structures_aabb[i & 1]);
        }
        else
        {
            instance_desc[i].AccelerationStructure =
                    ID3D12Resource_GetGPUVirtualAddress(rt_geom->bottom_acceleration_structures_tri[i & 1]);
        }
    }

    instance_desc[num_unmasked_instances_y].Transform[0][0] = instance_geom_scale;
    instance_desc[num_unmasked_instances_y].Transform[1][1] = instance_geom_scale;
    instance_desc[num_unmasked_instances_y].Transform[2][2] = instance_geom_scale;
    instance_desc[num_unmasked_instances_y].Transform[1][3] = -instance_offset_y;
    instance_desc[num_unmasked_instances_y].InstanceMask = 0xfe; /* This instance will be masked out since shader uses mask of 0x01. */
    instance_desc[num_unmasked_instances_y].InstanceContributionToHitGroupIndex = 0;
    instance_desc[num_unmasked_instances_y].AccelerationStructure =
            ID3D12Resource_GetGPUVirtualAddress(rt_geom->bottom_acceleration_structures_tri[2]);

    rt_geom->instance_buffer = create_upload_buffer(context->context.device,
            (num_unmasked_instances_y + 1) * sizeof(*instance_desc), instance_desc);

    /* Create top AS */
    memset(&inputs, 0, sizeof(inputs));
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = num_unmasked_instances_y + 1;
    inputs.InstanceDescs = ID3D12Resource_GetGPUVirtualAddress(rt_geom->instance_buffer);
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;

    create_acceleration_structure(context, &inputs, &rt_geom->top_rtas,
            postbuild_va ? (postbuild_va + 4 * sizeof(uint64_t)) : 0);

    /* Tests CLONE and COMPACTING copies. COMPACTING can never increase size, so it's safe to allocate up front.
     * We test the compacted size later. */
    rt_geom->top_acceleration_structures[0] = rt_geom->top_rtas.rtas;
    ID3D12Resource_AddRef(rt_geom->top_rtas.rtas);
    rt_geom->top_acceleration_structures[1] = duplicate_acceleration_structure(context,
            rt_geom->top_acceleration_structures[0], D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
    rt_geom->top_acceleration_structures[2] = duplicate_acceleration_structure(context,
            rt_geom->top_acceleration_structures[1], D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE);

    free(geom_desc);
    free(instance_desc);
}

struct rt_pso_factory
{
    D3D12_STATE_SUBOBJECT *subobjects;
    size_t subobjects_count;

    void **allocs;
    size_t allocs_count;
};

static void rt_pso_factory_init(struct rt_pso_factory *factory)
{
    memset(factory, 0, sizeof(*factory));
}

static unsigned int rt_pso_factory_add_subobject(struct rt_pso_factory *factory, const D3D12_STATE_SUBOBJECT *object)
{
    unsigned ret = factory->subobjects_count;
    factory->subobjects = realloc(factory->subobjects, (factory->subobjects_count + 1) * sizeof(*factory->subobjects));
    factory->subobjects[factory->subobjects_count++] = *object;
    return ret;
}

static void *rt_pso_factory_calloc(struct rt_pso_factory *factory, size_t nmemb, size_t count)
{
    void *mem = calloc(nmemb, count);
    factory->allocs = realloc(factory->allocs, (factory->allocs_count + 1) * sizeof(*factory->allocs));
    factory->allocs[factory->allocs_count++] = mem;
    return mem;
}

static unsigned int rt_pso_factory_add_state_object_config(struct rt_pso_factory *factory, D3D12_STATE_OBJECT_FLAGS flags)
{
    D3D12_STATE_OBJECT_CONFIG *config;
    D3D12_STATE_SUBOBJECT desc;

    config = rt_pso_factory_calloc(factory, 1, sizeof(*config));
    config->Flags = flags;

    desc.Type = D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG;
    desc.pDesc = config;
    return rt_pso_factory_add_subobject(factory, &desc);
}

static unsigned int rt_pso_factory_add_pipeline_config(struct rt_pso_factory *factory, unsigned int recursion_depth)
{
    D3D12_RAYTRACING_PIPELINE_CONFIG *config;
    D3D12_STATE_SUBOBJECT desc;

    config = rt_pso_factory_calloc(factory, 1, sizeof(*config));
    config->MaxTraceRecursionDepth = recursion_depth;

    desc.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    desc.pDesc = config;
    return rt_pso_factory_add_subobject(factory, &desc);
}

static unsigned int rt_pso_factory_add_pipeline_config1(struct rt_pso_factory *factory, unsigned int recursion_depth,
        D3D12_RAYTRACING_PIPELINE_FLAGS flags)
{
    D3D12_RAYTRACING_PIPELINE_CONFIG1 *config;
    D3D12_STATE_SUBOBJECT desc;

    config = rt_pso_factory_calloc(factory, 1, sizeof(*config));
    config->MaxTraceRecursionDepth = recursion_depth;
    config->Flags = flags;

    desc.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1;
    desc.pDesc = config;
    return rt_pso_factory_add_subobject(factory, &desc);
}

static unsigned int rt_pso_factory_add_shader_config(struct rt_pso_factory *factory,
        unsigned int attrib_size, unsigned int payload_size)
{
    D3D12_RAYTRACING_SHADER_CONFIG *config;
    D3D12_STATE_SUBOBJECT desc;

    config = rt_pso_factory_calloc(factory, 1, sizeof(*config));
    config->MaxAttributeSizeInBytes = attrib_size;
    config->MaxPayloadSizeInBytes = payload_size;

    desc.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    desc.pDesc = config;
    return rt_pso_factory_add_subobject(factory, &desc);
}

static unsigned int rt_pso_factory_add_global_root_signature(struct rt_pso_factory *factory, ID3D12RootSignature *rs)
{
    D3D12_GLOBAL_ROOT_SIGNATURE *global_rs_desc;
    D3D12_STATE_SUBOBJECT desc;

    global_rs_desc = rt_pso_factory_calloc(factory, 1, sizeof(*global_rs_desc));
    global_rs_desc->pGlobalRootSignature = rs;

    desc.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    desc.pDesc = global_rs_desc;
    return rt_pso_factory_add_subobject(factory, &desc);
}

static unsigned int rt_pso_factory_add_local_root_signature(struct rt_pso_factory *factory, ID3D12RootSignature *rs)
{
    D3D12_LOCAL_ROOT_SIGNATURE *local_rs_desc;
    D3D12_STATE_SUBOBJECT desc;

    local_rs_desc = rt_pso_factory_calloc(factory, 1, sizeof(*local_rs_desc));
    local_rs_desc->pLocalRootSignature = rs;

    desc.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
    desc.pDesc = local_rs_desc;
    return rt_pso_factory_add_subobject(factory, &desc);
}

static unsigned int rt_pso_factory_add_dxil_library(struct rt_pso_factory *factory,
        D3D12_SHADER_BYTECODE dxil, unsigned int num_exports, D3D12_EXPORT_DESC *exports)
{
    D3D12_DXIL_LIBRARY_DESC *lib;
    D3D12_STATE_SUBOBJECT desc;

    lib = rt_pso_factory_calloc(factory, 1, sizeof(*lib));
    lib->DXILLibrary = dxil;
    lib->NumExports = num_exports;
    lib->pExports = exports;

    desc.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    desc.pDesc = lib;
    return rt_pso_factory_add_subobject(factory, &desc);
}

static unsigned int rt_pso_factory_add_hit_group(struct rt_pso_factory *factory, const D3D12_HIT_GROUP_DESC *hit_group)
{
    D3D12_STATE_SUBOBJECT desc;
    desc.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    desc.pDesc = hit_group;
    return rt_pso_factory_add_subobject(factory, &desc);
}

static unsigned int rt_pso_factory_add_subobject_to_exports_association(struct rt_pso_factory *factory,
        unsigned int subobject_index, unsigned int num_exports, LPCWSTR *exports)
{
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION *assoc;
    D3D12_STATE_SUBOBJECT desc;

    assoc = rt_pso_factory_calloc(factory, 1, sizeof(*assoc));
    assoc->pExports = exports;
    assoc->NumExports = num_exports;
    /* Resolve later when compiling since base pointer can change. Encode offset here for now. */
    assoc->pSubobjectToAssociate = (const void*)(uintptr_t)subobject_index;

    desc.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    desc.pDesc = assoc;
    return rt_pso_factory_add_subobject(factory, &desc);
}

static unsigned int rt_pso_factory_add_dxil_subobject_to_exports_association(struct rt_pso_factory *factory,
        LPCWSTR object, unsigned int num_exports, LPCWSTR *exports)
{
    D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION *assoc;
    D3D12_STATE_SUBOBJECT desc;

    assoc = rt_pso_factory_calloc(factory, 1, sizeof(*assoc));
    assoc->pExports = exports;
    assoc->NumExports = num_exports;
    assoc->SubobjectToAssociate = object;

    desc.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    desc.pDesc = assoc;
    return rt_pso_factory_add_subobject(factory, &desc);
}

static unsigned int rt_pso_factory_add_existing_collection(struct rt_pso_factory *factory,
        ID3D12StateObject *collection, unsigned int num_exports, D3D12_EXPORT_DESC *exports)
{
    D3D12_EXISTING_COLLECTION_DESC *coll;
    D3D12_STATE_SUBOBJECT desc;

    coll = rt_pso_factory_calloc(factory, 1, sizeof(*coll));
    coll->NumExports = num_exports;
    coll->pExports = exports;
    coll->pExistingCollection = collection;

    desc.Type = D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION;
    desc.pDesc = coll;
    return rt_pso_factory_add_subobject(factory, &desc);
}

static unsigned int rt_pso_factory_add_default_node_mask(struct rt_pso_factory* factory)
{
    D3D12_STATE_SUBOBJECT desc;
    D3D12_NODE_MASK *mask;

    mask = rt_pso_factory_calloc(factory, 1, sizeof(*mask));
    /* This node mask is weird and some runtimes have bugs. We'll just ignore it anyways in vkd3d-proton.
     * https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_state_subobject_type */
    mask->NodeMask = 1;

    desc.Type = D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK;
    desc.pDesc = mask;
    return rt_pso_factory_add_subobject(factory, &desc);
}

static ID3D12StateObject *rt_pso_factory_compile(struct raytracing_test_context *context,
        struct rt_pso_factory *factory,
        D3D12_STATE_OBJECT_TYPE type)
{
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION *assoc;
    D3D12_STATE_OBJECT_DESC desc;
    ID3D12StateObject *rt_pso;
    size_t i;

    memset(&desc, 0, sizeof(desc));
    desc.Type = type;
    desc.NumSubobjects = factory->subobjects_count;
    desc.pSubobjects = factory->subobjects;

    for (i = 0; i < factory->subobjects_count; i++)
    {
        if (factory->subobjects[i].Type == D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION)
        {
            /* Resolve offsets to true pointers. */
            assoc = (D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION *)factory->subobjects[i].pDesc;
            assoc->pSubobjectToAssociate = factory->subobjects + (uintptr_t)assoc->pSubobjectToAssociate;
        }
    }

    rt_pso = NULL;
    ID3D12Device5_CreateStateObject(context->device5, &desc, &IID_ID3D12StateObject, (void **)&rt_pso);

    free(factory->subobjects);
    for (i = 0; i < factory->allocs_count; i++)
        free(factory->allocs[i]);
    free(factory->allocs);
    memset(factory, 0, sizeof(*factory));

    return rt_pso;
}

static ID3D12StateObject *rt_pso_add_to_state_object(ID3D12Device5 *device, ID3D12StateObject *parent, ID3D12StateObject *addition,
        const D3D12_HIT_GROUP_DESC *hit_group)
{
    D3D12_EXISTING_COLLECTION_DESC existing;
    ID3D12StateObject *new_state_object;
    D3D12_STATE_OBJECT_CONFIG config;
    D3D12_STATE_SUBOBJECT subobj[3];
    D3D12_STATE_OBJECT_DESC desc;
    ID3D12Device7 *device7;
    HRESULT hr;

    desc.NumSubobjects = 1;
    desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    desc.pSubobjects = subobj;
    subobj[0].Type = D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION;
    subobj[0].pDesc = &existing;
    existing.NumExports = 0;
    existing.pExports = NULL;
    existing.pExistingCollection = addition;

    if (hit_group)
    {
        subobj[desc.NumSubobjects].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        subobj[desc.NumSubobjects].pDesc = hit_group;
        desc.NumSubobjects++;
    }

    if (FAILED(ID3D12Device5_QueryInterface(device, &IID_ID3D12Device7, (void**)&device7)))
    {
        skip("Failed to query ID3D12Device7.\n");
        return NULL;
    }

    /* Have to add ALLOW_STATE_OBJECT_ADDITIONS for both parent and addition. */
    hr = ID3D12Device7_AddToStateObject(device7, &desc, parent, &IID_ID3D12StateObject, (void**)&new_state_object);
    ok(hr == E_INVALIDARG, "Unexpected hr #%x.\n", hr);

    config.Flags = D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS;
    subobj[desc.NumSubobjects].Type = D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG;
    subobj[desc.NumSubobjects].pDesc = &config;
    desc.NumSubobjects++;

    /* Type must be RAYTRACING_PIPELINE. */
    desc.Type = D3D12_STATE_OBJECT_TYPE_COLLECTION;
    hr = ID3D12Device7_AddToStateObject(device7, &desc, parent, &IID_ID3D12StateObject, (void**)&new_state_object);
    ok(hr == E_INVALIDARG, "Unexpected hr #%x.\n", hr);
    desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

    hr = ID3D12Device7_AddToStateObject(device7, &desc, parent, &IID_ID3D12StateObject, (void**)&new_state_object);
    ok(SUCCEEDED(hr), "Failed to AddToStateObject, hr #%x.\n", hr);
    if (FAILED(hr))
        new_state_object = NULL;
    ID3D12Device7_Release(device7);
    return new_state_object;
}

enum rt_test_mode
{
    TEST_MODE_PLAIN,
    TEST_MODE_TRACE_RAY_FORCE_OPAQUE,
    TEST_MODE_TRACE_RAY_FORCE_NON_OPAQUE,
    TEST_MODE_TRACE_RAY_SKIP_TRIANGLES,
    TEST_MODE_TRACE_RAY_SKIP_AABBS,
    TEST_MODE_PSO_SKIP_TRIANGLES,
    TEST_MODE_PSO_SKIP_AABBS,
    TEST_MODE_INDIRECT,
    TEST_MODE_PSO_ADD_TO_STATE_OBJECT,
};

static ID3D12StateObject *create_rt_collection(struct raytracing_test_context *context,
        unsigned int num_exports, D3D12_EXPORT_DESC *exports,
        const D3D12_HIT_GROUP_DESC *hit_group,
        ID3D12RootSignature *global_rs, ID3D12RootSignature *local_rs,
        enum rt_test_mode test_mode)
{
    struct rt_pso_factory factory;

    rt_pso_factory_init(&factory);

    rt_pso_factory_add_default_node_mask(&factory);

    if (test_mode == TEST_MODE_PSO_ADD_TO_STATE_OBJECT)
    {
        rt_pso_factory_add_state_object_config(&factory,
            D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS |
            D3D12_STATE_OBJECT_FLAG_ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS);
    }
    else
    {
        rt_pso_factory_add_state_object_config(&factory,
            D3D12_STATE_OBJECT_FLAG_ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS);
    }

    if (test_mode == TEST_MODE_PSO_SKIP_TRIANGLES)
        rt_pso_factory_add_pipeline_config1(&factory, 1, D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_TRIANGLES);
    else if (test_mode == TEST_MODE_PSO_SKIP_AABBS)
        rt_pso_factory_add_pipeline_config1(&factory, 1, D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_PROCEDURAL_PRIMITIVES);
    else
        rt_pso_factory_add_pipeline_config(&factory, 1);

    rt_pso_factory_add_shader_config(&factory, 8, 8);

    if (global_rs)
        rt_pso_factory_add_global_root_signature(&factory, global_rs);

    rt_pso_factory_add_dxil_library(&factory, get_default_rt_lib(), num_exports, exports);

    if (local_rs)
        rt_pso_factory_add_local_root_signature(&factory, local_rs);

    if (hit_group)
        rt_pso_factory_add_hit_group(&factory, hit_group);

    return rt_pso_factory_compile(context, &factory, D3D12_STATE_OBJECT_TYPE_COLLECTION);
}

static uint32_t test_mode_to_trace_flags(enum rt_test_mode mode)
{
    switch (mode)
    {
        default:
        case TEST_MODE_PLAIN:
            return 0;

        case TEST_MODE_TRACE_RAY_FORCE_OPAQUE:
            return D3D12_RAY_FLAG_FORCE_OPAQUE;
        case TEST_MODE_TRACE_RAY_FORCE_NON_OPAQUE:
            return D3D12_RAY_FLAG_FORCE_NON_OPAQUE;
        case TEST_MODE_TRACE_RAY_SKIP_TRIANGLES:
            return D3D12_RAY_FLAG_SKIP_TRIANGLES;
        case TEST_MODE_TRACE_RAY_SKIP_AABBS:
            return D3D12_RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;
    }
}

static void test_raytracing_pipeline(enum rt_test_mode mode, D3D12_RAYTRACING_TIER minimum_tier)
{
#define NUM_GEOM_DESC 6
#define NUM_UNMASKED_INSTANCES 8
#define INSTANCE_OFFSET_Y (100.0f)
#define GEOM_OFFSET_X (10.0f)
#define INSTANCE_GEOM_SCALE (0.5f)

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postbuild_desc[3];
    float sbt_colors[NUM_GEOM_DESC * NUM_UNMASKED_INSTANCES + 1][2] = {{0}};
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    ID3D12CommandSignature *command_signature_cs;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    ID3D12GraphicsCommandList4 *command_list4;
    ID3D12CommandSignature *command_signature;
    ID3D12StateObject *rt_object_library_aabb;
    ID3D12StateObject *rt_object_library_tri;
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    struct raytracing_test_context context;
    ID3D12DescriptorHeap *descriptor_heap;
    ID3D12RootSignature *local_rs_table;
    struct test_rt_geometry test_rtases;
    D3D12_DESCRIPTOR_RANGE table_range;
    ID3D12Resource *postbuild_readback;
    ID3D12Resource *sbt_colors_buffer;
    ID3D12Resource *postbuild_buffer;
    ID3D12Resource *indirect_buffer;
    unsigned int i, descriptor_size;
    ID3D12StateObject *rt_pso_added;
    ID3D12RootSignature *global_rs;
    struct test_geometry test_geom;
    ID3D12RootSignature *local_rs;
    ID3D12Resource *ray_positions;
    ID3D12PipelineState *cs_pso;
    ID3D12Resource *cs_indirect;
    struct resource_readback rb;
    unsigned int instance, geom;
    ID3D12Resource *ray_colors;
    ID3D12CommandQueue *queue;
    ID3D12StateObject *rt_pso;
    unsigned int ref_count;
    ID3D12Device *device;
    ID3D12Resource *sbt;
    HRESULT hr;

    if (!init_raytracing_test_context(&context, minimum_tier))
        return;

    device = context.context.device;
    command_list = context.context.list;
    command_list4 = context.list4;
    queue = context.context.queue;
    cs_pso = NULL;
    cs_indirect = NULL;

    postbuild_readback = create_readback_buffer(device, 4096);
    postbuild_buffer = create_default_buffer(device, 4096, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    init_test_geometry(device, &test_geom);
    init_rt_geometry(&context, &test_rtases, &test_geom,
            NUM_GEOM_DESC, GEOM_OFFSET_X,
            NUM_UNMASKED_INSTANCES, INSTANCE_GEOM_SCALE, INSTANCE_OFFSET_Y,
            ID3D12Resource_GetGPUVirtualAddress(postbuild_buffer));

    /* Create global root signature. All RT shaders can access these parameters. */
    {
        memset(&root_signature_desc, 0, sizeof(root_signature_desc));
        memset(root_parameters, 0, sizeof(root_parameters));
        memset(descriptor_ranges, 0, sizeof(descriptor_ranges));

        root_signature_desc.NumParameters = 2;
        root_signature_desc.pParameters = root_parameters;
        root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[0].DescriptorTable.NumDescriptorRanges = 2;
        root_parameters[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;
        descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;  /* Acceleration structure and ray origins. */
        descriptor_ranges[0].NumDescriptors = 2;
        descriptor_ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; /* Output from raygen shader */
        descriptor_ranges[1].OffsetInDescriptorsFromTableStart = 2;
        descriptor_ranges[1].NumDescriptors = 1;

        root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_parameters[1].Constants.Num32BitValues = 1;
        root_parameters[1].Constants.RegisterSpace = 0;
        root_parameters[1].Constants.ShaderRegister = 0;

        hr = create_root_signature(device, &root_signature_desc, &global_rs);
        ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);
    }

    /* Create local root signature. This defines how the data in the SBT for each individual shader is laid out. */
    {
        memset(&root_signature_desc, 0, sizeof(root_signature_desc));
        memset(root_parameters, 0, sizeof(root_parameters));
        memset(descriptor_ranges, 0, sizeof(descriptor_ranges));

        /* 32BIT_CONSTANTS are 4 byte aligned. Descriptor tables take up 8 bytes instead of 4,
           since the raw GPU VA of descriptor heap is placed in the buffer,
           but it must still belong to the bound descriptor heap.
           Root descriptors take up 8 bytes (raw pointers). */

        root_signature_desc.NumParameters = 2;
        root_signature_desc.pParameters = root_parameters;
        /* We can have different implementation for local root sigs. */
        root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_parameters[0].Constants.Num32BitValues = 1;
        root_parameters[0].Constants.RegisterSpace = 1;
        root_parameters[0].Constants.ShaderRegister = 0;

        root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_parameters[1].Constants.Num32BitValues = 1;
        root_parameters[1].Constants.RegisterSpace = 1;
        root_parameters[1].Constants.ShaderRegister = 1;

        hr = create_root_signature(device, &root_signature_desc, &local_rs);
        ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);

        root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[0].DescriptorTable.pDescriptorRanges = &table_range;
        root_parameters[0].DescriptorTable.NumDescriptorRanges = 1;
        table_range.OffsetInDescriptorsFromTableStart = 0;
        table_range.RegisterSpace = 1;
        table_range.BaseShaderRegister = 0;
        table_range.NumDescriptors = 1;
        table_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;

        root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        root_parameters[1].Descriptor.RegisterSpace = 1;
        root_parameters[1].Descriptor.ShaderRegister = 1;

        hr = create_root_signature(device, &root_signature_desc, &local_rs_table);
        ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);
    }

    /* Create RT collection (triangles). */
    {
        D3D12_EXPORT_DESC dxil_exports[] = {
            { u"XRayClosest", u"RayClosest", 0 },
            { u"XRayAnyTriangle", u"RayAnyTriangle", 0 },
        };
        D3D12_HIT_GROUP_DESC hit_group;

        memset(&hit_group, 0, sizeof(hit_group));
        hit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hit_group.ClosestHitShaderImport = u"XRayClosest";
        hit_group.AnyHitShaderImport = u"XRayAnyTriangle";
        hit_group.HitGroupExport = u"XRayHitTriangle";

        rt_object_library_tri = create_rt_collection(&context,
                ARRAY_SIZE(dxil_exports), dxil_exports,
                &hit_group, global_rs, local_rs, mode);
    }

    /* Create RT collection (AABB). */
    {
        D3D12_EXPORT_DESC dxil_exports[] = {
            { u"XRayClosestAABB", u"RayClosest", 0 },
            { u"XRayAnyAABB", u"RayAnyAABB", 0 },
            { u"XRayIntersect", u"RayIntersect", 0 },
        };
        D3D12_HIT_GROUP_DESC hit_group;

        memset(&hit_group, 0, sizeof(hit_group));
        hit_group.Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
        hit_group.ClosestHitShaderImport = u"XRayClosestAABB";
        hit_group.AnyHitShaderImport = u"XRayAnyAABB";
        hit_group.IntersectionShaderImport = u"XRayIntersect";
        hit_group.HitGroupExport = u"XRayHitAABB";

        rt_object_library_aabb = create_rt_collection(&context,
                ARRAY_SIZE(dxil_exports), dxil_exports,
                &hit_group, global_rs, local_rs, mode);
    }

    /* Create RT PSO. */
    if (rt_object_library_tri && rt_object_library_aabb)
    {
        const WCHAR *table_export[] = { u"XRayMiss" };
        D3D12_EXPORT_DESC dxil_exports[2] = {
            { u"XRayMiss", u"RayMiss", 0 },
            { u"XRayGen", u"RayGen", 0 },
        };
        unsigned int local_rs_table_index;
        D3D12_HIT_GROUP_DESC hit_group;
        struct rt_pso_factory factory;
        unsigned int local_rs_index;

        rt_pso_factory_init(&factory);
        rt_pso_factory_add_default_node_mask(&factory);

        if (mode == TEST_MODE_PSO_ADD_TO_STATE_OBJECT)
            rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS);
        else
            rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_NONE);
        rt_pso_factory_add_global_root_signature(&factory, global_rs);

        if (mode == TEST_MODE_PSO_SKIP_TRIANGLES)
            rt_pso_factory_add_pipeline_config1(&factory, 1, D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_TRIANGLES);
        else if (mode == TEST_MODE_PSO_SKIP_AABBS)
            rt_pso_factory_add_pipeline_config1(&factory, 1, D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_PROCEDURAL_PRIMITIVES);
        else
            rt_pso_factory_add_pipeline_config(&factory, 1);

        rt_pso_factory_add_shader_config(&factory, 8, 8);
        /* All entry points are exported by default. Test with custom exports, because why not. */
        rt_pso_factory_add_dxil_library(&factory, get_default_rt_lib(), ARRAY_SIZE(dxil_exports), dxil_exports);
        local_rs_table_index = rt_pso_factory_add_local_root_signature(&factory, local_rs_table);
        local_rs_index = rt_pso_factory_add_local_root_signature(&factory, local_rs);

        /* Apparently, we have to point to a subobject in the array, otherwise, it just silently fails. */
        rt_pso_factory_add_subobject_to_exports_association(&factory,
                local_rs_table_index, ARRAY_SIZE(table_export), table_export);
        rt_pso_factory_add_subobject_to_exports_association(&factory,
                local_rs_index, 0, NULL);

        /* Defer this. */
        if (mode != TEST_MODE_PSO_ADD_TO_STATE_OBJECT)
        {
            rt_pso_factory_add_existing_collection(&factory, rt_object_library_tri, 0, NULL);
            rt_pso_factory_add_existing_collection(&factory, rt_object_library_aabb, 0, NULL);

            memset(&hit_group, 0, sizeof(hit_group));
            hit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
            hit_group.ClosestHitShaderImport = u"XRayClosest";
            hit_group.HitGroupExport = u"XRayHit2";
            rt_pso_factory_add_hit_group(&factory, &hit_group);
        }

        rt_pso = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

        if (mode != TEST_MODE_PSO_ADD_TO_STATE_OBJECT)
        {
            /* Docs say there should be ref-count of the collection, but apparently, that refcount is private. */
            ref_count = ID3D12StateObject_AddRef(rt_object_library_tri);
            ok(ref_count == 2, "Collection ref count is %u.\n", ref_count);
            ID3D12StateObject_Release(rt_object_library_tri);

            ref_count = ID3D12StateObject_AddRef(rt_object_library_aabb);
            ok(ref_count == 2, "Collection ref count is %u.\n", ref_count);
            ID3D12StateObject_Release(rt_object_library_aabb);
        }
    }
    else
        rt_pso = NULL;

    /* Add two iterations of AddToStateObject so we have test coverage of that scenario. */
    if (mode == TEST_MODE_PSO_ADD_TO_STATE_OBJECT && rt_pso)
    {
        D3D12_HIT_GROUP_DESC hit_group;

        memset(&hit_group, 0, sizeof(hit_group));
        hit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hit_group.ClosestHitShaderImport = u"XRayClosest";
        hit_group.HitGroupExport = u"XRayHit2";

        rt_pso_added = rt_pso_add_to_state_object(context.device5, rt_pso, rt_object_library_tri, &hit_group);
        ID3D12StateObject_Release(rt_pso);
        rt_pso = rt_pso_added;
        ref_count = ID3D12StateObject_AddRef(rt_object_library_tri);
        ok(ref_count == 2, "Collection ref count is %u.\n", ref_count);
        ID3D12StateObject_Release(rt_object_library_tri);
    }

    if (mode == TEST_MODE_PSO_ADD_TO_STATE_OBJECT && rt_pso)
    {
        uint8_t pre_ray_miss_data[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];
        uint8_t pre_ray_gen_data[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];
        uint8_t pre_hit_data[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];
        ID3D12StateObjectProperties *combined_props;
        static const WCHAR ray_miss[] = u"XRayMiss";
        static const WCHAR ray_hit2[] = u"XRayHit2";
        static const WCHAR ray_gen[] = u"XRayGen";
        ID3D12StateObjectProperties *post_props;
        ID3D12StateObjectProperties *pre_props;

        uint8_t *combined_ray_miss;
        uint8_t *combined_ray_gen;
        uint8_t *combined_hit;

        uint8_t *post_ray_miss;
        uint8_t *post_ray_gen;
        uint8_t *post_hit;

        uint8_t *pre_ray_miss;
        uint8_t *pre_ray_gen;
        uint8_t *pre_hit;

        ID3D12StateObject_QueryInterface(rt_pso, &IID_ID3D12StateObjectProperties, (void**)&pre_props);

        pre_ray_gen = ID3D12StateObjectProperties_GetShaderIdentifier(pre_props, ray_gen);
        pre_hit = ID3D12StateObjectProperties_GetShaderIdentifier(pre_props, ray_hit2);
        pre_ray_miss = ID3D12StateObjectProperties_GetShaderIdentifier(pre_props, ray_miss);
        memcpy(pre_ray_miss_data, pre_ray_miss, sizeof(pre_ray_miss_data));
        memcpy(pre_ray_gen_data, pre_ray_gen, sizeof(pre_ray_gen_data));
        memcpy(pre_hit_data, pre_hit, sizeof(pre_hit_data));

        rt_pso_added = rt_pso_add_to_state_object(context.device5, rt_pso, rt_object_library_aabb, NULL);
        ID3D12StateObject_QueryInterface(rt_pso, &IID_ID3D12StateObjectProperties, (void**)&post_props);
        ID3D12StateObject_QueryInterface(rt_pso_added, &IID_ID3D12StateObjectProperties, (void**)&combined_props);

        post_ray_gen = ID3D12StateObjectProperties_GetShaderIdentifier(post_props, ray_gen);
        post_hit = ID3D12StateObjectProperties_GetShaderIdentifier(post_props, ray_hit2);
        post_ray_miss = ID3D12StateObjectProperties_GetShaderIdentifier(post_props, ray_miss);

        combined_ray_gen = ID3D12StateObjectProperties_GetShaderIdentifier(combined_props, ray_gen);
        combined_hit = ID3D12StateObjectProperties_GetShaderIdentifier(combined_props, ray_hit2);
        combined_ray_miss = ID3D12StateObjectProperties_GetShaderIdentifier(combined_props, ray_miss);

        /* The docs do talk about taking some weird internal locks to deal with AddToStateObject(), so verify
         * that we don't have to return the parent property pointer here. */
        ok(pre_props == post_props, "Unexpected result in interface check.\n");
        ok(combined_props != post_props, "Unexpected result in interface check.\n");

        ok(pre_ray_gen == post_ray_gen, "Unexpected SBT pointers.\n");
        ok(pre_hit == post_hit, "Unexpected SBT pointers.\n");
        ok(pre_ray_miss == post_ray_miss, "Unexpected SBT pointers.\n");

        /* Apparently, we have to inherit the pointer to the SBT directly. */
        ok(combined_ray_gen == post_ray_gen, "Unexpected SBT pointers.\n");
        ok(combined_hit == post_hit, "Unexpected SBT pointers.\n");
        ok(combined_ray_miss == post_ray_miss, "Unexpected SBT pointers.\n");

        /* Verify that we cannot modify the SBT data in place. */
        ok(memcmp(combined_hit, pre_hit_data, sizeof(pre_hit_data)) == 0, "Detected variance for existing SBT entries.\n");
        ok(memcmp(combined_ray_gen, pre_ray_gen_data, sizeof(pre_ray_gen_data)) == 0, "Detected variance for existing SBT entries.\n");
        ok(memcmp(combined_ray_miss, pre_ray_miss_data, sizeof(pre_ray_miss_data)) == 0, "Detected variance for existing SBT entries.\n");

        ID3D12StateObject_Release(rt_pso);
        rt_pso = rt_pso_added;
        ref_count = ID3D12StateObject_AddRef(rt_object_library_aabb);
        ok(ref_count == 2, "Collection ref count is %u.\n", ref_count);
        ID3D12StateObject_Release(rt_object_library_aabb);

        ID3D12StateObjectProperties_Release(pre_props);
        ID3D12StateObjectProperties_Release(post_props);
        ID3D12StateObjectProperties_Release(combined_props);
    }

    /* Docs say that refcount should be held by RTPSO, but apparently it doesn't on native drivers. */
    ID3D12RootSignature_AddRef(global_rs);
    ID3D12RootSignature_AddRef(local_rs);
    ref_count = ID3D12RootSignature_Release(global_rs);
    ok(ref_count == 1, "Ref count %u != 1.\n", ref_count);
    ref_count = ID3D12RootSignature_Release(local_rs);
    ok(ref_count == 1, "Ref count %u != 1.\n", ref_count);

    descriptor_heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4);
    descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cpu_handle = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(descriptor_heap);
    gpu_handle = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(descriptor_heap);

    /* Build SBT (Shader Binding Table) */
    sbt_colors_buffer = NULL;
    sbt = NULL;

    if (rt_pso)
    {
        ID3D12StateObjectProperties *props;

        sbt_colors[0][0] = 1000.0f;
        sbt_colors[0][1] = 2000.0f;
        for (i = 1; i < ARRAY_SIZE(sbt_colors); i++)
        {
            sbt_colors[i][0] = 2 * i + 1;
            sbt_colors[i][1] = 2 * i + 2;
        }

        {
            uint8_t padded[2048];
            memcpy(padded + 0, &sbt_colors[0][0], sizeof(float));
            memcpy(padded + 1024, &sbt_colors[0][1], sizeof(float));
            sbt_colors_buffer = create_upload_buffer(device, sizeof(padded), padded);
        }

        /* Why this is a separate interface, we will never know ... */
        if (SUCCEEDED(ID3D12StateObject_QueryInterface(rt_pso, &IID_ID3D12StateObjectProperties, (void **)&props)))
        {
            static const WCHAR ray_tri_intersect[] = u"XRayHitTriangle::intersection";
            static const WCHAR ray_aabb_intersect[] = u"XRayHitAABB::intersection";
            static const WCHAR ray_tri_closest[] = u"XRayHitTriangle::closesthit";
            static const WCHAR ray_aabb_closest[] = u"XRayHitAABB::closesthit";
            static const WCHAR ray_tri_anyhit[] = u"XRayHitTriangle::anyhit";
            static const WCHAR ray_aabb_anyhit[] = u"XRayHitAABB::anyhit";
            static const WCHAR ray_broken3[] = u"XRayHitTriangle::X";
            static const WCHAR ray_broken2[] = u"XRayHitTriangle::";
            static const WCHAR ray_broken1[] = u"XRayHitTriangle:";
            static const WCHAR ray_broken0[] = u"XRayHitTriangle";
            static const WCHAR ray_hit_tri[] = u"XRayHitTriangle";
            static const WCHAR ray_hit_aabb[] = u"XRayHitAABB";
            static const WCHAR ray_miss[] = u"XRayMiss";
            static const WCHAR ray_hit2[] = u"XRayHit2";
            static const WCHAR ray_gen[] = u"XRayGen";
            ID3D12StateObject *tmp_rt_pso;
            const void *ray_hit_aabb_sbt;
            unsigned int min_stack_size;
            const void *ray_miss_sbt;
            const void *ray_gen_sbt;
            const void *ray_hit_sbt;
            const void *ray_hit_sbt2;
            unsigned int stack_size;
            uint8_t sbt_data[4096];

            hr = ID3D12StateObjectProperties_QueryInterface(props, &IID_ID3D12StateObject, (void **)&tmp_rt_pso);
            ok(SUCCEEDED(hr), "Failed to query state object interface from properties.\n");
            if (SUCCEEDED(hr))
                ID3D12StateObject_Release(tmp_rt_pso);

            /* Test reference count semantics for non-derived interface. */
            ref_count = ID3D12StateObjectProperties_AddRef(props);
            ok(ref_count == 3, "Unexpected refcount %u.\n", ref_count);
            ref_count = ID3D12StateObjectProperties_AddRef(props);
            ok(ref_count == 4, "Unexpected refcount %u.\n", ref_count);
            ref_count = ID3D12StateObject_AddRef(rt_pso);
            ok(ref_count == 5, "Unexpected refcount %u.\n", ref_count);
            ref_count = ID3D12StateObject_AddRef(rt_pso);
            ok(ref_count == 6, "Unexpected refcount %u.\n", ref_count);
            ref_count = ID3D12StateObjectProperties_Release(props);
            ok(ref_count == 5, "Unexpected refcount %u.\n", ref_count);
            ref_count = ID3D12StateObjectProperties_Release(props);
            ok(ref_count == 4, "Unexpected refcount %u.\n", ref_count);
            ref_count = ID3D12StateObject_Release(rt_pso);
            ok(ref_count == 3, "Unexpected refcount %u.\n", ref_count);
            ref_count = ID3D12StateObject_Release(rt_pso);
            ok(ref_count == 2, "Unexpected refcount %u.\n", ref_count);

            /* Test that we get something sensible, different drivers return different values here. */
#define ARBITRARY_STACK_LIMIT 32

            /* AMD Windows returns 0 here for all stack sizes. There is no well defined return value we expect here,
             * but verify we return something sane. */
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_gen);
            ok(stack_size <= ARBITRARY_STACK_LIMIT, "Stack size %u > %u.\n", stack_size, ARBITRARY_STACK_LIMIT);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_miss);
            ok(stack_size <= ARBITRARY_STACK_LIMIT, "Stack size %u > %u.\n", stack_size, ARBITRARY_STACK_LIMIT);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_tri_closest);
            ok(stack_size <= ARBITRARY_STACK_LIMIT, "Stack size %u > %u.\n", stack_size, ARBITRARY_STACK_LIMIT);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_tri_anyhit);
            ok(stack_size <= ARBITRARY_STACK_LIMIT, "Stack size %u > %u.\n", stack_size, ARBITRARY_STACK_LIMIT);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_tri_intersect);
            ok(stack_size == ~0u, "Stack size %u != UINT_MAX.\n", stack_size);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_aabb_closest);
            ok(stack_size <= ARBITRARY_STACK_LIMIT, "Stack size %u > %u.\n", stack_size, ARBITRARY_STACK_LIMIT);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_aabb_anyhit);
            ok(stack_size <= ARBITRARY_STACK_LIMIT, "Stack size %u > %u.\n", stack_size, ARBITRARY_STACK_LIMIT);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_aabb_intersect);
            ok(stack_size <= ARBITRARY_STACK_LIMIT, "Stack size %u > %u.\n", stack_size, ARBITRARY_STACK_LIMIT);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_broken0);
            ok(stack_size == ~0u, "Stack size %u != UINT_MAX.\n", stack_size);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_broken1);
            ok(stack_size == ~0u, "Stack size %u != UINT_MAX.\n", stack_size);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_broken2);
            ok(stack_size == ~0u, "Stack size %u != UINT_MAX.\n", stack_size);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_broken3);
            ok(stack_size == ~0u, "Stack size %u != UINT_MAX.\n", stack_size);

            stack_size = ID3D12StateObjectProperties_GetPipelineStackSize(props);
            ok(stack_size <= ARBITRARY_STACK_LIMIT, "Stack size %u < %u.\n", stack_size, ARBITRARY_STACK_LIMIT);

            /* Apparently even if we set stack size here, it will be clamped to the conservative stack size on AMD?
             * Driver behavior on NV and AMD is different here, choose NV behavior as it makes more sense. */
            min_stack_size = stack_size;
            ID3D12StateObjectProperties_SetPipelineStackSize(props, 256);
            stack_size = ID3D12StateObjectProperties_GetPipelineStackSize(props);
            ok(stack_size <= min_stack_size || stack_size == 256, "Stack size %u > %u && %u != 256.\n", stack_size, min_stack_size, stack_size);

            ray_gen_sbt = ID3D12StateObjectProperties_GetShaderIdentifier(props, ray_gen);
            ray_hit_sbt = ID3D12StateObjectProperties_GetShaderIdentifier(props, ray_hit_tri);
            ray_hit_sbt2 = ID3D12StateObjectProperties_GetShaderIdentifier(props, ray_hit2);
            ray_miss_sbt = ID3D12StateObjectProperties_GetShaderIdentifier(props, ray_miss);
            ray_hit_aabb_sbt = ID3D12StateObjectProperties_GetShaderIdentifier(props, ray_hit_aabb);
            ok(!!ray_gen_sbt, "Failed to get SBT.\n");
            ok(!!ray_hit_sbt, "Failed to get SBT.\n");
            ok(!!ray_hit_sbt2, "Failed to get SBT.\n");
            ok(!!ray_miss_sbt, "Failed to get SBT.\n");
            ok(!!ray_hit_aabb_sbt, "Failed to get SBT.\n");

            memcpy(sbt_data, ray_miss_sbt, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            {
                bool use_aabb, use_anyhit_shader;
                const void *active_sbt;

                for (instance = 0; instance < NUM_UNMASKED_INSTANCES; instance++)
                {
                    for (geom = 0; geom < NUM_GEOM_DESC; geom++)
                    {
                        i = instance * NUM_GEOM_DESC + geom;
                        use_aabb = instance_index_is_aabb(instance);
                        use_anyhit_shader = !!(i & 3);

                        if (use_aabb)
                            active_sbt = ray_hit_aabb_sbt;
                        else
                            active_sbt = use_anyhit_shader ? ray_hit_sbt : ray_hit_sbt2;

                        memcpy(sbt_data + (i + 1) * 64, active_sbt, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
                    }
                }
            }
            memcpy(sbt_data + (NUM_GEOM_DESC * NUM_UNMASKED_INSTANCES + 1) * 64, ray_gen_sbt, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

            /* Local root signature data is placed after the shader identifier at offset 32 bytes. */

            /* For miss shader, we use a different local root signature.
             * Tests that we handle local tables + local root descriptor. */
            {
                UINT64 miss_sbt[2];
                miss_sbt[0] = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(descriptor_heap).ptr + 3 * descriptor_size;
                miss_sbt[1] = ID3D12Resource_GetGPUVirtualAddress(sbt_colors_buffer) + 1024;
                memcpy(sbt_data + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, miss_sbt, sizeof(miss_sbt));
            }

            for (i = 1; i < ARRAY_SIZE(sbt_colors); i++)
                memcpy(sbt_data + D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 64 * i, sbt_colors[i], sizeof(sbt_colors[i]));

            sbt = create_upload_buffer(device, sizeof(sbt_data), sbt_data);
            ID3D12StateObjectProperties_Release(props);
        }
        else
        {
            destroy_raytracing_test_context(&context);
            return;
        }
    }

    {
        /* For test, we want to hit miss shader, then hit group indices in order. */
        float ray_pos[NUM_GEOM_DESC * NUM_UNMASKED_INSTANCES + 1][2];
        unsigned int x, y;

        /* Should hit instance 2, but gets masked out. */
        ray_pos[0][0] = 0.0f;
        ray_pos[0][1] = -INSTANCE_OFFSET_Y;

        for (y = 0; y < NUM_UNMASKED_INSTANCES; y++)
        {
            for (x = 0; x < NUM_GEOM_DESC; x++)
            {
                ray_pos[y * NUM_GEOM_DESC + x + 1][0] = INSTANCE_GEOM_SCALE * GEOM_OFFSET_X * (float)x; /* Instance transform will scale X offset from 10 * index to 5 * index. */
                ray_pos[y * NUM_GEOM_DESC + x + 1][1] = INSTANCE_OFFSET_Y * (float)y;
            }
        }

        ray_colors = create_default_buffer(device, sizeof(sbt_colors), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ray_positions = create_upload_buffer(device, sizeof(ray_pos), ray_pos);
    }

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC as_desc;
        D3D12_GPU_VIRTUAL_ADDRESS rtases[2];

        memset(&as_desc, 0, sizeof(as_desc));
        as_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        as_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        as_desc.Format = DXGI_FORMAT_UNKNOWN;
        as_desc.RaytracingAccelerationStructure.Location = ID3D12Resource_GetGPUVirtualAddress(test_rtases.top_acceleration_structures[2]);
        ID3D12Device_CreateShaderResourceView(device, NULL, &as_desc, cpu_handle);
        cpu_handle.ptr += descriptor_size;

        rtases[0] = ID3D12Resource_GetGPUVirtualAddress(test_rtases.bottom_acceleration_structures_tri[0]);
        rtases[1] = ID3D12Resource_GetGPUVirtualAddress(test_rtases.top_acceleration_structures[0]);
        /* Emitting this is not COPY_DEST, but UNORDERED_ACCESS for some bizarre reason. */

        postbuild_desc[0].InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
        postbuild_desc[0].DestBuffer = ID3D12Resource_GetGPUVirtualAddress(postbuild_buffer) + 8 * sizeof(uint64_t);
        postbuild_desc[1].InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_CURRENT_SIZE;
        postbuild_desc[1].DestBuffer = postbuild_desc[0].DestBuffer + 2 * sizeof(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE_DESC);
        postbuild_desc[2].InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION;
        postbuild_desc[2].DestBuffer = postbuild_desc[1].DestBuffer + 2 * sizeof(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_CURRENT_SIZE_DESC);
        ID3D12GraphicsCommandList4_EmitRaytracingAccelerationStructurePostbuildInfo(command_list4, &postbuild_desc[0], 2, rtases);
        ID3D12GraphicsCommandList4_EmitRaytracingAccelerationStructurePostbuildInfo(command_list4, &postbuild_desc[1], 2, rtases);
        ID3D12GraphicsCommandList4_EmitRaytracingAccelerationStructurePostbuildInfo(command_list4, &postbuild_desc[2], 2, rtases);

        transition_resource_state(command_list, postbuild_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12GraphicsCommandList_CopyResource(command_list, postbuild_readback, postbuild_buffer);
    }

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC ray_pos_desc;
        memset(&ray_pos_desc, 0, sizeof(ray_pos_desc));
        ray_pos_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        ray_pos_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        ray_pos_desc.Format = DXGI_FORMAT_UNKNOWN;
        ray_pos_desc.Buffer.FirstElement = 0;
        ray_pos_desc.Buffer.NumElements = NUM_GEOM_DESC * NUM_UNMASKED_INSTANCES + 1;
        ray_pos_desc.Buffer.StructureByteStride = 8;
        ID3D12Device_CreateShaderResourceView(device, ray_positions, &ray_pos_desc, cpu_handle);
        cpu_handle.ptr += descriptor_size;
    }

    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC ray_col_desc;
        memset(&ray_col_desc, 0, sizeof(ray_col_desc));
        ray_col_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        ray_col_desc.Format = DXGI_FORMAT_UNKNOWN;
        ray_col_desc.Buffer.FirstElement = 0;
        ray_col_desc.Buffer.NumElements = NUM_GEOM_DESC * NUM_UNMASKED_INSTANCES + 1;
        ray_col_desc.Buffer.StructureByteStride = 8;
        ID3D12Device_CreateUnorderedAccessView(device, ray_colors, NULL, &ray_col_desc, cpu_handle);
        cpu_handle.ptr += descriptor_size;
    }

    if (sbt_colors_buffer)
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC miss_view_desc;
        memset(&miss_view_desc, 0, sizeof(miss_view_desc));
        miss_view_desc.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(sbt_colors_buffer);
        miss_view_desc.SizeInBytes = ID3D12Resource_GetDesc(sbt_colors_buffer).Width;
        ID3D12Device_CreateConstantBufferView(device, &miss_view_desc, cpu_handle);
        cpu_handle.ptr += descriptor_size;
    }

    indirect_buffer = NULL;
    command_signature = NULL;
    command_signature_cs = NULL;
    if (mode == TEST_MODE_INDIRECT)
        command_signature = create_command_signature(device, D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS);
    command_signature_cs = create_command_signature(device, D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH);

    ID3D12GraphicsCommandList4_SetComputeRootSignature(command_list4, global_rs);
    ID3D12GraphicsCommandList4_SetPipelineState1(command_list4, rt_pso);
    ID3D12GraphicsCommandList4_SetDescriptorHeaps(command_list4, 1, &descriptor_heap);
    ID3D12GraphicsCommandList4_SetComputeRootDescriptorTable(command_list4, 0, gpu_handle);

    if (sbt)
    {
        D3D12_DISPATCH_RAYS_DESC desc;
        uint32_t flags;

        memset(&desc, 0, sizeof(desc));
        desc.Width = NUM_GEOM_DESC * NUM_UNMASKED_INSTANCES + 1;
        desc.Height = 1;
        desc.Depth = 1;

        desc.MissShaderTable.StartAddress = ID3D12Resource_GetGPUVirtualAddress(sbt);
        desc.MissShaderTable.SizeInBytes = 64;
        desc.MissShaderTable.StrideInBytes = 64;

        desc.HitGroupTable.StartAddress = desc.MissShaderTable.StartAddress + desc.MissShaderTable.SizeInBytes;
        desc.HitGroupTable.StrideInBytes = 64;
        desc.HitGroupTable.SizeInBytes = 64 * NUM_GEOM_DESC * NUM_UNMASKED_INSTANCES;

        desc.RayGenerationShaderRecord.SizeInBytes = 64;
        desc.RayGenerationShaderRecord.StartAddress = desc.HitGroupTable.StartAddress + desc.HitGroupTable.SizeInBytes;

        flags = test_mode_to_trace_flags(mode);
        ID3D12GraphicsCommandList4_SetComputeRoot32BitConstant(command_list4, 1, flags, 0);

        if (mode != TEST_MODE_INDIRECT)
        {
            ID3D12GraphicsCommandList4_DispatchRays(command_list4, &desc);
        }
        else
        {
            indirect_buffer = create_upload_buffer(device, sizeof(D3D12_DISPATCH_RAYS_DESC), &desc);
            ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature, 1, indirect_buffer, 0, NULL, 0);
        }
    }

    if (mode == TEST_MODE_PLAIN)
    {
        const D3D12_DISPATCH_ARGUMENTS args = { 2, 1, 1 };
#include "shaders/rt/headers/default_cs.h"
        cs_pso = create_compute_pipeline_state(context.context.device, global_rs, default_cs_dxil);
        cs_indirect = create_upload_buffer(device, sizeof(D3D12_DISPATCH_ARGUMENTS), &args);
        uav_barrier(context.context.list, NULL);
        ID3D12GraphicsCommandList_SetPipelineState(command_list, cs_pso);
        ID3D12GraphicsCommandList_ExecuteIndirect(command_list, command_signature_cs, 1, cs_indirect, 0, NULL, 0);
    }

    transition_resource_state(command_list, ray_colors, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(ray_colors, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);
    {
        float expected_x, expected_y;
        bool expect_hit, use_aabb;
        bool culled;
        float x, y;

        for (instance = 0; instance < NUM_UNMASKED_INSTANCES; instance++)
        {
            for (geom = 0; geom < NUM_GEOM_DESC; geom++)
            {
                use_aabb = instance_index_is_aabb(instance);
                i = NUM_GEOM_DESC * instance + geom;
                x = get_readback_float(&rb, 2 * (i + 1), 0);
                y = get_readback_float(&rb, 2 * (i + 1) + 1, 0);

                if (mode == TEST_MODE_TRACE_RAY_FORCE_OPAQUE)
                    expect_hit = true;
                else if (mode == TEST_MODE_TRACE_RAY_FORCE_NON_OPAQUE)
                    expect_hit = false;
                else if (instance == 0 || instance == 2)
                    expect_hit = true; /* Force opaque on instance desc. */
                else if (instance == 1 || instance == 3)
                    expect_hit = false; /* Force non-opaque on instance desc, any hit -> miss will trigger. */
                else
                    expect_hit = !!(geom & 1); /* Geom desc has either NONE or OPAQUE. */

                if (!use_aabb && (i & 3) == 0)
                    expect_hit = true; /* Use an SBT without any-hit shader, so opaque flags don't matter. */

                if ((use_aabb && (mode == TEST_MODE_TRACE_RAY_SKIP_AABBS || mode == TEST_MODE_PSO_SKIP_AABBS)) ||
                        (!use_aabb && (mode == TEST_MODE_TRACE_RAY_SKIP_TRIANGLES || mode == TEST_MODE_PSO_SKIP_TRIANGLES)))
                {
                    expect_hit = false;
                    culled = true;
                }
                else
                    culled = false;

                if (expect_hit)
                {
                    expected_x = sbt_colors[i + 1][0];
                    expected_y = sbt_colors[i + 1][1];
                }
                else
                {
                    expected_x = sbt_colors[0][0]; /* Miss shader is run. */
                    expected_y = sbt_colors[0][1];
                    if (!culled)
                    {
                        if (use_aabb)
                            expected_y += 1.0f; /* Any-hit shader accumulates .y by 1.0f here. */
                        else
                            expected_x += 1.0f; /* Any-hit shader accumulates .x by 1.0f here. */
                    }
                }

                if (mode == TEST_MODE_PLAIN)
                {
                    /* Validate that we can do indirect CS after indirect RT.
                     * Check for a driver bug we encountered in the wild. */
                    if (i < 16)
                    {
                        expected_x += 1.0f;
                        expected_y += 2.0f;
                    }
                }

                ok(x == expected_x, "Ray color [%u].x mismatch (%f != %f).\n", i, x, expected_x);
                ok(y == expected_y, "Ray color [%u].y mismatch (%f != %f).\n", i, y, expected_y);
            }
        }

        x = get_readback_float(&rb, 0, 0);
        y = get_readback_float(&rb, 1, 0);
        expected_x = sbt_colors[0][0]; /* Only miss shader is run. */
        expected_y = sbt_colors[0][1];
        ok(x == expected_x, "Miss ray color.x mismatch (%f != %f).\n", x, expected_x);
        ok(y == expected_y, "Miss ray color.y mismatch (%f != %f).\n", y, expected_y);
    }

    release_resource_readback(&rb);

    {
        struct post_info
        {
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE_DESC compacted;
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_CURRENT_SIZE_DESC current;
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION_DESC serialize;
        } top[2], bottom[2];

        uint64_t *mapped;
        hr = ID3D12Resource_Map(postbuild_readback, 0, NULL, (void **)&mapped);
        ok(SUCCEEDED(hr), "Failed to map postbuild readback.\n");
        if (SUCCEEDED(hr))
        {
            memcpy(&bottom[0], mapped + 0, sizeof(struct post_info));
            memcpy(&top[0], mapped + 4, sizeof(struct post_info));

            memcpy(&bottom[1].compacted, mapped + 8, sizeof(bottom[1].compacted));
            memcpy(&top[1].compacted, mapped + 9, sizeof(top[1].compacted));
            memcpy(&bottom[1].current, mapped + 10, sizeof(bottom[1].current));
            memcpy(&top[1].current, mapped + 11, sizeof(top[1].current));
            memcpy(&bottom[1].serialize, mapped + 12, sizeof(bottom[1].serialize));
            memcpy(&top[1].serialize, mapped + 14, sizeof(top[1].serialize));

            ok(memcmp(&top[0], &top[1], sizeof(top[0])) == 0, "Size mismatch.\n");
            ok(memcmp(&bottom[0], &bottom[1], sizeof(bottom[0])) == 0, "Size mismatch.\n");

            /* First sanity check that output from BuildRTAS and EmitPostbuildInfo() match up. */
            ok(bottom[0].compacted.CompactedSizeInBytes > 0, "Compacted size for bottom acceleration structure is %u.\n", (unsigned int)bottom[0].compacted.CompactedSizeInBytes);
            ok(top[0].compacted.CompactedSizeInBytes > 0, "Compacted size for top acceleration structure is %u.\n", (unsigned int)top[0].compacted.CompactedSizeInBytes);

            /* Requires maintenance1. */
            ok(bottom[0].current.CurrentSizeInBytes > 0, "Current size for bottom acceleration structure is %u.\n", (unsigned int)bottom[0].current.CurrentSizeInBytes);
            ok(top[0].current.CurrentSizeInBytes > 0, "Current size for top acceleration structure is %u.\n", (unsigned int)top[0].current.CurrentSizeInBytes);

            /* Compacted size must be less-or-equal to current size. Cannot pass since we don't have current size. */
            ok(bottom[0].compacted.CompactedSizeInBytes <= bottom[0].current.CurrentSizeInBytes,
                    "Compacted size %u > Current size %u\n", (unsigned int)bottom[0].compacted.CompactedSizeInBytes, (unsigned int)bottom[0].current.CurrentSizeInBytes);
            ok(top[0].compacted.CompactedSizeInBytes <= top[0].current.CurrentSizeInBytes,
                    "Compacted size %u > Current size %u\n", (unsigned int)top[0].compacted.CompactedSizeInBytes, (unsigned int)top[0].current.CurrentSizeInBytes);

            ok(bottom[0].serialize.SerializedSizeInBytes > 0, "Serialized size for bottom acceleration structure is %u.\n", (unsigned int)bottom[0].serialize.SerializedSizeInBytes);
            ok(bottom[0].serialize.NumBottomLevelAccelerationStructurePointers == 0, "NumBottomLevel pointers is %u.\n", (unsigned int)bottom[0].serialize.NumBottomLevelAccelerationStructurePointers);
            ok(top[0].serialize.SerializedSizeInBytes > 0, "Serialized size for top acceleration structure is %u.\n", (unsigned int)top[0].serialize.SerializedSizeInBytes);
            ok(top[0].serialize.NumBottomLevelAccelerationStructurePointers == NUM_UNMASKED_INSTANCES + 1,
                    "NumBottomLevel pointers is %u.\n", (unsigned int)top[0].serialize.NumBottomLevelAccelerationStructurePointers);

            ID3D12Resource_Unmap(postbuild_readback, 0, NULL);
        }
    }

    destroy_test_geometry(&test_geom);
    destroy_rt_geometry(&test_rtases);
    if (sbt_colors_buffer)
        ID3D12Resource_Release(sbt_colors_buffer);
    ID3D12RootSignature_Release(global_rs);
    ID3D12RootSignature_Release(local_rs);
    ID3D12RootSignature_Release(local_rs_table);

    if (rt_pso)
        ID3D12StateObject_Release(rt_pso);
    if (cs_pso)
        ID3D12PipelineState_Release(cs_pso);
    if (cs_indirect)
        ID3D12Resource_Release(cs_indirect);
    if (rt_object_library_tri)
        ID3D12StateObject_Release(rt_object_library_tri);
    if (rt_object_library_aabb)
        ID3D12StateObject_Release(rt_object_library_aabb);
    ID3D12Resource_Release(ray_colors);
    ID3D12Resource_Release(ray_positions);
    ID3D12DescriptorHeap_Release(descriptor_heap);
    if (sbt)
        ID3D12Resource_Release(sbt);
    ID3D12Resource_Release(postbuild_readback);
    ID3D12Resource_Release(postbuild_buffer);
    if (command_signature)
        ID3D12CommandSignature_Release(command_signature);
    if (command_signature_cs)
        ID3D12CommandSignature_Release(command_signature_cs);
    if (indirect_buffer)
        ID3D12Resource_Release(indirect_buffer);

    destroy_raytracing_test_context(&context);
}

void test_raytracing(void)
{
    struct test
    {
        enum rt_test_mode mode;
        D3D12_RAYTRACING_TIER minimum_tier;
        const char *desc;
    };

    static const struct test tests[] = {
        { TEST_MODE_PLAIN, D3D12_RAYTRACING_TIER_1_0, "Plain" },
        { TEST_MODE_TRACE_RAY_FORCE_OPAQUE, D3D12_RAYTRACING_TIER_1_0, "TraceRayForceOpaque" },
        { TEST_MODE_TRACE_RAY_FORCE_NON_OPAQUE, D3D12_RAYTRACING_TIER_1_0, "TraceRayForceNonOpaque" },
        { TEST_MODE_TRACE_RAY_SKIP_TRIANGLES, D3D12_RAYTRACING_TIER_1_1, "TraceRaySkipTriangles" },
        { TEST_MODE_TRACE_RAY_SKIP_AABBS, D3D12_RAYTRACING_TIER_1_1, "TraceRaySkipAABBs" },
        { TEST_MODE_PSO_SKIP_TRIANGLES, D3D12_RAYTRACING_TIER_1_1, "PSOSkipTriangles" },
        { TEST_MODE_PSO_SKIP_AABBS, D3D12_RAYTRACING_TIER_1_1, "PSOSkipAABBs" },
        { TEST_MODE_INDIRECT, D3D12_RAYTRACING_TIER_1_1, "Indirect" },
        { TEST_MODE_PSO_ADD_TO_STATE_OBJECT, D3D12_RAYTRACING_TIER_1_1, "AddToStateObject" },
    };

    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test: %s", tests[i].desc);
        test_raytracing_pipeline(tests[i].mode, tests[i].minimum_tier);
    }
    vkd3d_test_set_context(NULL);
}

static void test_rayquery_pipeline(enum rt_test_mode mode)
{
    /* Intended to mirror the test case for RTPSO closely. */
#define THREADGROUP_SIZE 64
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_ROOT_PARAMETER root_parameters[4];
    ID3D12GraphicsCommandList *command_list;
    struct raytracing_test_context context;
    struct test_rt_geometry test_rtases;
    ID3D12RootSignature *root_signature;
    const float miss_color_x = 1000.0f;
    const float miss_color_y = 2000.0f;
    struct test_geometry test_geom;
    unsigned int i, instance, geom;
    ID3D12Resource *ray_positions;
    ID3D12Resource *ray_results;
    struct resource_readback rb;
    ID3D12CommandQueue *queue;
    ID3D12PipelineState *pso;
    ID3D12Device *device;
    HRESULT hr;

    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_1))
        return;

    device = context.context.device;
    command_list = context.context.list;
    queue = context.context.queue;

    memset(&root_signature_desc, 0, sizeof(root_signature_desc));
    root_signature_desc.NumParameters = ARRAY_SIZE(root_parameters);
    root_signature_desc.pParameters = root_parameters;

    root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[0].Descriptor.ShaderRegister = 0;
    root_parameters[0].Descriptor.RegisterSpace = 0;
    root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[1].Descriptor.ShaderRegister = 1;
    root_parameters[1].Descriptor.RegisterSpace = 0;
    root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[2].Descriptor.ShaderRegister = 0;
    root_parameters[2].Descriptor.RegisterSpace = 0;
    root_parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    root_parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    root_parameters[3].Constants.Num32BitValues = 3;
    root_parameters[3].Constants.RegisterSpace = 0;
    root_parameters[3].Constants.ShaderRegister = 0;

    hr = create_root_signature(device, &root_signature_desc, &root_signature);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);
    pso = create_compute_pipeline_state(device, root_signature, get_rayquery_shader());

    init_test_geometry(device, &test_geom);
    init_rt_geometry(&context, &test_rtases, &test_geom,
            NUM_GEOM_DESC, GEOM_OFFSET_X,
            NUM_UNMASKED_INSTANCES, INSTANCE_GEOM_SCALE, INSTANCE_OFFSET_Y, 0);

    {
        /* For test, we want to hit miss shader, then hit group indices in order. */
        float ray_pos[THREADGROUP_SIZE][2];
        unsigned int x, y;

        memset(ray_pos, 0, sizeof(ray_pos));

        /* Should hit instance 2, but gets masked out. */
        ray_pos[0][0] = 0.0f;
        ray_pos[0][1] = -INSTANCE_OFFSET_Y;

        for (y = 0; y < NUM_UNMASKED_INSTANCES; y++)
        {
            for (x = 0; x < NUM_GEOM_DESC; x++)
            {
                ray_pos[y * NUM_GEOM_DESC + x + 1][0] = INSTANCE_GEOM_SCALE * GEOM_OFFSET_X * (float)x; /* Instance transform will scale X offset from 10 * index to 5 * index. */
                ray_pos[y * NUM_GEOM_DESC + x + 1][1] = INSTANCE_OFFSET_Y * (float)y;
            }
        }

        ray_positions = create_upload_buffer(device, sizeof(ray_pos), ray_pos);
        ray_results = create_default_buffer(device, sizeof(ray_pos),
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, root_signature);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, pso);
    ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(command_list, 0,
            ID3D12Resource_GetGPUVirtualAddress(test_rtases.top_acceleration_structures[0]));
    ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(command_list, 1,
            ID3D12Resource_GetGPUVirtualAddress(ray_positions));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list, 2,
            ID3D12Resource_GetGPUVirtualAddress(ray_results));
    {
        struct { float miss_color[2]; uint32_t flags; } data;
        data.miss_color[0] = miss_color_x;
        data.miss_color[1] = miss_color_y;
        data.flags = test_mode_to_trace_flags(mode);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(command_list, 3, 3, &data, 0);
    }
    ID3D12GraphicsCommandList_Dispatch(command_list, 1, 1, 1);

    transition_resource_state(command_list, ray_results,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_buffer_readback_with_command_list(ray_results, DXGI_FORMAT_R32_FLOAT, &rb, queue, command_list);

    {
        float expected_x, expected_y;
        bool expect_hit, use_aabb;
        bool culled;
        float x, y;

        for (instance = 0; instance < NUM_UNMASKED_INSTANCES; instance++)
        {
            for (geom = 0; geom < NUM_GEOM_DESC; geom++)
            {
                use_aabb = instance_index_is_aabb(instance);
                i = NUM_GEOM_DESC * instance + geom;
                x = get_readback_float(&rb, 2 * (i + 1), 0);
                y = get_readback_float(&rb, 2 * (i + 1) + 1, 0);

                if (mode == TEST_MODE_TRACE_RAY_FORCE_OPAQUE)
                    expect_hit = true;
                else if (mode == TEST_MODE_TRACE_RAY_FORCE_NON_OPAQUE)
                    expect_hit = false;
                else if (instance == 0 || instance == 2)
                    expect_hit = true; /* Force opaque on instance desc. */
                else if (instance == 1 || instance == 3)
                    expect_hit = false; /* Force non-opaque on instance desc, any hit -> miss will trigger. */
                else
                    expect_hit = !!(geom & 1); /* Geom desc has either NONE or OPAQUE. */

                if ((use_aabb && mode == TEST_MODE_TRACE_RAY_SKIP_AABBS) ||
                    (!use_aabb && mode == TEST_MODE_TRACE_RAY_SKIP_TRIANGLES))
                {
                    expect_hit = false;
                    culled = true;
                }
                else
                    culled = false;

                if (expect_hit)
                {
                    expected_x = (float)i + 1.0f; /* Closest hit case. */
                    expected_y = (float)i + 2.0f;
                }
                else
                {
                    expected_x = 1000.0f; /* Miss case */
                    expected_y = 2000.0f;

                    if (!culled)
                    {
                        if (use_aabb)
                            expected_y += 1.0f;
                        else
                            expected_x += 1.0f;
                    }
                }
                ok(x == expected_x, "Ray color [%u].x mismatch (%f != %f).\n", i, x, expected_x);
                ok(y == expected_y, "Ray color [%u].y mismatch (%f != %f).\n", i, y, expected_y);
            }
        }

        x = get_readback_float(&rb, 0, 0);
        y = get_readback_float(&rb, 1, 0);
        expected_x = 1000.0f;
        expected_y = 2000.0f;
        ok(x == expected_x, "Miss ray color.x mismatch (%f != %f).\n", x, expected_x);
        ok(y == expected_y, "Miss ray color.y mismatch (%f != %f).\n", y, expected_y);
    }

    release_resource_readback(&rb);
    destroy_test_geometry(&test_geom);
    destroy_rt_geometry(&test_rtases);
    ID3D12Resource_Release(ray_positions);
    ID3D12Resource_Release(ray_results);
    ID3D12RootSignature_Release(root_signature);
    ID3D12PipelineState_Release(pso);
    destroy_raytracing_test_context(&context);
}

void test_rayquery(void)
{
    struct test
    {
        enum rt_test_mode mode;
        const char *desc;
    };

    static const struct test tests[] = {
        { TEST_MODE_PLAIN, "Plain" },
        { TEST_MODE_TRACE_RAY_FORCE_OPAQUE, "TraceRayForceOpaque" },
        { TEST_MODE_TRACE_RAY_FORCE_NON_OPAQUE, "TraceRayForceNonOpaque" },
        { TEST_MODE_TRACE_RAY_SKIP_TRIANGLES, "TraceRaySkipTriangles" },
        { TEST_MODE_TRACE_RAY_SKIP_AABBS, "TraceRaySkipAABBs" },
    };

    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        vkd3d_test_set_context("Test: %s", tests[i].desc);
        test_rayquery_pipeline(tests[i].mode);
    }
    vkd3d_test_set_context(NULL);
}

static void test_raytracing_local_rs_static_sampler_inner(bool use_libraries)
{
    ID3D12GraphicsCommandList4 *command_list4;
    ID3D12GraphicsCommandList *command_list;
    struct raytracing_test_context context;
    struct test_rt_geometry test_rtases;
    ID3D12RootSignature *local_rs[2];
    struct test_geometry test_geom;
    ID3D12RootSignature *global_rs;
    struct resource_readback rb;
    ID3D12DescriptorHeap *heap;
    ID3D12StateObject *rt_pso;
    ID3D12Device *device;
    ID3D12Resource *srv;
    ID3D12Resource *uav;
    ID3D12Resource *sbt;

    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_0))
        return;

    device = context.context.device;
    command_list = context.context.list;
    command_list4 = context.list4;

    init_test_geometry(device, &test_geom);
    init_rt_geometry(&context, &test_rtases, &test_geom,
            2, 10.0f, 2, 1.0f, 10.0f, 0);

    /* Global root signature */
    {
        D3D12_ROOT_PARAMETER root_param[1];
        D3D12_ROOT_SIGNATURE_DESC rs_desc;
        D3D12_DESCRIPTOR_RANGE ranges[2];

        memset(&rs_desc, 0, sizeof(rs_desc));
        memset(root_param, 0, sizeof(root_param));
        memset(ranges, 0, sizeof(ranges));

        rs_desc.NumParameters = ARRAY_SIZE(root_param);
        rs_desc.pParameters = root_param;

        root_param[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_param[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root_param[0].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(ranges);
        root_param[0].DescriptorTable.pDescriptorRanges = ranges;

        ranges[0].NumDescriptors = 2;
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;

        ranges[1].NumDescriptors = 1;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].OffsetInDescriptorsFromTableStart = 2;

        create_root_signature(device, &rs_desc, &global_rs);
    }

    /* Local root signatures. */
    {
        D3D12_STATIC_SAMPLER_DESC sampler_descs[2];
        D3D12_ROOT_SIGNATURE_DESC rs_desc;

        memset(&rs_desc, 0, sizeof(rs_desc));
        memset(sampler_descs, 0, sizeof(sampler_descs));

        rs_desc.NumStaticSamplers = ARRAY_SIZE(sampler_descs);
        rs_desc.pStaticSamplers = sampler_descs;
        rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

        /* First closesthit shader reads s0 and s2. Second one reads s1 and s2. s2 is shared, and must be defined
         * equal across the PSO. */
        sampler_descs[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        sampler_descs[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler_descs[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler_descs[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler_descs[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler_descs[0].ShaderRegister = 0;
        sampler_descs[1] = sampler_descs[0];
        sampler_descs[1].ShaderRegister = 2;
        sampler_descs[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

        create_root_signature(device, &rs_desc, &local_rs[0]);

        sampler_descs[0].ShaderRegister = 1;
        sampler_descs[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_descs[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler_descs[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        create_root_signature(device, &rs_desc, &local_rs[1]);
    }

    /* Create PSO. */
    if (use_libraries)
    {
        /* Test that we can deal with local samplers in collections.
         * We can only deal with this as long as the collections are compatible,
         * but at least do what we can. */
        D3D12_EXPORT_DESC collection_export_descs[2] = { { u"RayClosest1" }, { u"RayClosest2" } };
        D3D12_EXPORT_DESC export_descs[2] = { { u"RayGen" }, { u"RayMiss" } };
        ID3D12StateObject *collections[2];
        D3D12_HIT_GROUP_DESC hit_group[2];
        struct rt_pso_factory factory;
        unsigned local_index[2];
        unsigned int i;

        memset(hit_group, 0, sizeof(hit_group));
        hit_group[0].Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hit_group[0].HitGroupExport = u"RayHit1";
        hit_group[0].ClosestHitShaderImport = u"RayClosest1";
        hit_group[1].Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hit_group[1].HitGroupExport = u"RayHit2";
        hit_group[1].ClosestHitShaderImport = u"RayClosest2";

        for (i = 0; i < 2; i++)
        {
            rt_pso_factory_init(&factory);
            rt_pso_factory_add_dxil_library(&factory, get_static_sampler_rt_lib(), 1, &collection_export_descs[i]);
            rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_NONE);
            rt_pso_factory_add_pipeline_config(&factory, 1);
            rt_pso_factory_add_shader_config(&factory, 8, 4);
            rt_pso_factory_add_global_root_signature(&factory, global_rs);
            rt_pso_factory_add_local_root_signature(&factory, local_rs[i]);
            rt_pso_factory_add_hit_group(&factory, &hit_group[i]);
            collections[i] = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_COLLECTION);
        }

        rt_pso_factory_init(&factory);
        for (i = 0; i < 2; i++)
            rt_pso_factory_add_existing_collection(&factory, collections[i], 0, NULL);
        rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_NONE);
        rt_pso_factory_add_pipeline_config(&factory, 1);
        rt_pso_factory_add_shader_config(&factory, 8, 4);
        rt_pso_factory_add_global_root_signature(&factory, global_rs);
        rt_pso_factory_add_dxil_library(&factory, get_static_sampler_rt_lib(), ARRAY_SIZE(export_descs), export_descs);
        rt_pso = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        /* Currently, we expect this to fail on vkd3d-proton since the local sampler sets definitions diverge
         * in the different collections. */
        todo ok(!!rt_pso, "Failed to compile RTPSO.\n");
        for (i = 0; i < 2; i++)
            ID3D12StateObject_Release(collections[i]);

        if (!rt_pso)
        {
            /* Try again, but now with both local sets in one collection, which we can make work. */
            rt_pso_factory_init(&factory);
            rt_pso_factory_add_dxil_library(&factory, get_static_sampler_rt_lib(),
                    ARRAY_SIZE(collection_export_descs), collection_export_descs);
            rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_NONE);
            rt_pso_factory_add_pipeline_config(&factory, 1);
            rt_pso_factory_add_shader_config(&factory, 8, 4);
            rt_pso_factory_add_global_root_signature(&factory, global_rs);
            for (i = 0; i < 2; i++)
            {
                rt_pso_factory_add_hit_group(&factory, &hit_group[i]);
                local_index[i] = rt_pso_factory_add_local_root_signature(&factory, local_rs[i]);
                rt_pso_factory_add_subobject_to_exports_association(&factory, local_index[i], 1, &hit_group[i].HitGroupExport);
            }
            collections[0] = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_COLLECTION);

            rt_pso_factory_init(&factory);
            rt_pso_factory_add_existing_collection(&factory, collections[0], 0, NULL);
            rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_NONE);
            rt_pso_factory_add_pipeline_config(&factory, 1);
            rt_pso_factory_add_shader_config(&factory, 8, 4);
            rt_pso_factory_add_global_root_signature(&factory, global_rs);
            rt_pso_factory_add_dxil_library(&factory, get_static_sampler_rt_lib(), ARRAY_SIZE(export_descs), export_descs);
            rt_pso = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
            ok(!!rt_pso, "Failed to compile RTPSO.\n");
            ID3D12StateObject_Release(collections[0]);
        }
    }
    else
    {
        D3D12_HIT_GROUP_DESC hit_group[2];
        struct rt_pso_factory factory;
        unsigned local_index[2];

        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_static_sampler_rt_lib(), 0, NULL);
        rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_NONE);
        rt_pso_factory_add_pipeline_config(&factory, 1);
        rt_pso_factory_add_shader_config(&factory, 8, 4);
        rt_pso_factory_add_global_root_signature(&factory, global_rs);
        local_index[0] = rt_pso_factory_add_local_root_signature(&factory, local_rs[0]);
        local_index[1] = rt_pso_factory_add_local_root_signature(&factory, local_rs[1]);

        memset(hit_group, 0, sizeof(hit_group));
        hit_group[0].Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hit_group[0].HitGroupExport = u"RayHit1";
        hit_group[0].ClosestHitShaderImport = u"RayClosest1";
        hit_group[1].Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hit_group[1].HitGroupExport = u"RayHit2";
        hit_group[1].ClosestHitShaderImport = u"RayClosest2";

        rt_pso_factory_add_hit_group(&factory, &hit_group[0]);
        rt_pso_factory_add_hit_group(&factory, &hit_group[1]);

        rt_pso_factory_add_subobject_to_exports_association(&factory, local_index[0],
                1, &hit_group[0].HitGroupExport);
        rt_pso_factory_add_subobject_to_exports_association(&factory, local_index[1],
                1, &hit_group[1].HitGroupExport);

        rt_pso = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
    }

    if (rt_pso)
    {
        uint8_t sbt_data[D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT * 6];
        const void *hit1, *hit2, *gen, *miss;
        ID3D12StateObjectProperties *props;

        ID3D12StateObject_QueryInterface(rt_pso, &IID_ID3D12StateObjectProperties, (void**)&props);
        hit1 = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"RayHit1");
        hit2 = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"RayHit2");
        gen = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"RayGen");
        miss = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"RayMiss");

        memcpy(sbt_data + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT * 0, gen, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        memcpy(sbt_data + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT * 1, miss, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        /* Hit SBTs for 2x2 quads. */
        memcpy(sbt_data + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT * 2, hit1, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        memcpy(sbt_data + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT * 3, hit2, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        memcpy(sbt_data + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT * 4, hit2, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        memcpy(sbt_data + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT * 5, hit1, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        ID3D12StateObjectProperties_Release(props);

        sbt = create_upload_buffer(device, sizeof(sbt_data), sbt_data);
    }
    else
        sbt = NULL;

    srv = create_default_texture2d(device, 2, 2, 1, 1,
            DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    uav = create_default_buffer(device, 4 * sizeof(float),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    {
        const float data[] = { 10.0f, 20.0f, 30.0f, 40.0f };
        D3D12_SUBRESOURCE_DATA srv_data;

        srv_data.pData = data;
        srv_data.RowPitch = 2 * sizeof(float);
        srv_data.SlicePitch = 4 * sizeof(float);
        upload_texture_data(srv, &srv_data, 1, context.context.queue, command_list);
        reset_command_list(command_list, context.context.allocator);
    }

    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
        D3D12_CPU_DESCRIPTOR_HANDLE h;

        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.RaytracingAccelerationStructure.Location =
                ID3D12Resource_GetGPUVirtualAddress(test_rtases.top_acceleration_structures[0]);

        h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        ID3D12Device_CreateShaderResourceView(device, NULL, &srv_desc, h);
    }

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
        D3D12_CPU_DESCRIPTOR_HANDLE h;

        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2D.MipLevels = 1;

        h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        h.ptr += ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12Device_CreateShaderResourceView(device, srv, &srv_desc, h);
    }

    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
        D3D12_CPU_DESCRIPTOR_HANDLE h;

        memset(&uav_desc, 0, sizeof(uav_desc));
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.Buffer.NumElements = 4;
        uav_desc.Buffer.StructureByteStride = 4;

        h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
        h.ptr += 2 * ID3D12Device_GetDescriptorHandleIncrementSize(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12Device_CreateUnorderedAccessView(device, uav, NULL, &uav_desc, h);
    }

    ID3D12GraphicsCommandList4_SetComputeRootSignature(command_list4, global_rs);
    ID3D12GraphicsCommandList4_SetPipelineState1(command_list4, rt_pso);
    ID3D12GraphicsCommandList4_SetDescriptorHeaps(command_list4, 1, &heap);
    ID3D12GraphicsCommandList4_SetComputeRootDescriptorTable(command_list4, 0,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));

    if (rt_pso)
    {
        D3D12_DISPATCH_RAYS_DESC ray_desc;
        memset(&ray_desc, 0, sizeof(ray_desc));
        ray_desc.Width = 2;
        ray_desc.Height = 2;
        ray_desc.Depth = 1;

        ray_desc.RayGenerationShaderRecord.StartAddress =
                ID3D12Resource_GetGPUVirtualAddress(sbt) + 0 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        ray_desc.RayGenerationShaderRecord.SizeInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

        ray_desc.MissShaderTable.StartAddress =
                ID3D12Resource_GetGPUVirtualAddress(sbt) + 1 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        ray_desc.MissShaderTable.SizeInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        ray_desc.MissShaderTable.StrideInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

        ray_desc.HitGroupTable.StartAddress =
                ID3D12Resource_GetGPUVirtualAddress(sbt) + 2 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        ray_desc.HitGroupTable.SizeInBytes = 4 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        ray_desc.HitGroupTable.StrideInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

        ID3D12GraphicsCommandList4_DispatchRays(command_list4, &ray_desc);
    }

    transition_resource_state(command_list, uav, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(uav, DXGI_FORMAT_R32_FLOAT, &rb, context.context.queue, command_list);

    {
        static const float expected[] = { 60.0f, 30.0f, 30.0f, 60.0f };
        unsigned int i;

        for (i = 0; i < 4; i++)
        {
            ok(expected[i] == get_readback_float(&rb, i, 0), "Sampled value #%u: %f != reference %f\n",
                    i, get_readback_float(&rb, i, 0), expected[i]);
        }
    }

    release_resource_readback(&rb);

    ID3D12Resource_Release(srv);
    ID3D12Resource_Release(uav);
    if (sbt)
        ID3D12Resource_Release(sbt);
    ID3D12DescriptorHeap_Release(heap);
    destroy_test_geometry(&test_geom);
    destroy_rt_geometry(&test_rtases);
    ID3D12RootSignature_Release(local_rs[0]);
    ID3D12RootSignature_Release(local_rs[1]);
    ID3D12RootSignature_Release(global_rs);
    if (rt_pso)
        ID3D12StateObject_Release(rt_pso);

    destroy_raytracing_test_context(&context);
}

void test_raytracing_local_rs_static_sampler(void)
{
    test_raytracing_local_rs_static_sampler_inner(false);
}

void test_raytracing_local_rs_static_sampler_collection(void)
{
    test_raytracing_local_rs_static_sampler_inner(true);
}

void test_raytracing_no_global_root_signature(void)
{
    struct raytracing_test_context context;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12RootSignature *local_rs;
    struct rt_pso_factory factory;
    D3D12_ROOT_PARAMETER param;
    ID3D12StateObject *object;
    ID3D12Device *device;
    unsigned int i;

    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_0))
        return;

    ID3D12Device5_QueryInterface(context.device5, &IID_ID3D12Device, (void **)&device);

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &param;
    memset(&param, 0, sizeof(param));
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    create_root_signature(device, &rs_desc, &local_rs);

    /* Just verify that we can correctly create such pipelines. */
    for (i = 0; i < 2; i++)
    {
        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_dummy_raygen_rt_lib(), 0, NULL);
        rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_NONE);
        rt_pso_factory_add_pipeline_config(&factory, 1);
        rt_pso_factory_add_shader_config(&factory, 8, 4);
        rt_pso_factory_add_local_root_signature(&factory, local_rs);
        object = rt_pso_factory_compile(&context, &factory, i ? D3D12_STATE_OBJECT_TYPE_COLLECTION : D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        ok(!!object, "Failed to create RTPSO object.\n");
        if (object)
            ID3D12StateObject_Release(object);
    }

    ID3D12RootSignature_Release(local_rs);
    ID3D12Device_Release(device);
    destroy_raytracing_test_context(&context);
}

void test_raytracing_default_association_tiebreak(void)
{
    struct raytracing_test_context context;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12RootSignature *local_rs0;
    ID3D12RootSignature *local_rs1;
    struct rt_pso_factory factory;
    D3D12_ROOT_PARAMETER param;
    ID3D12StateObject *object;
    unsigned int rs_index0;
    unsigned int rs_index1;
    ID3D12Device *device;
    unsigned int i;

    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_0))
        return;

    ID3D12Device5_QueryInterface(context.device5, &IID_ID3D12Device, (void **)&device);

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &param;
    memset(&param, 0, sizeof(param));
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    create_root_signature(device, &rs_desc, &local_rs0);
    param.Descriptor.RegisterSpace = 1;
    create_root_signature(device, &rs_desc, &local_rs1);

    /* If we declare a local root signature without association and one with explicit NULL, they are both default,
     * but the NULL association should win. */
    for (i = 0; i < 2; i++)
    {
        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_dummy_raygen_rt_lib(), 0, NULL);
        rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_NONE);
        rt_pso_factory_add_pipeline_config(&factory, 1);
        rt_pso_factory_add_shader_config(&factory, 8, 4);
        rs_index0 = rt_pso_factory_add_local_root_signature(&factory, local_rs0);
        rs_index1 = rt_pso_factory_add_local_root_signature(&factory, local_rs1);
        rt_pso_factory_add_subobject_to_exports_association(&factory, i ? rs_index1 : rs_index0, 0, NULL);
        object = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        if (i == 0)
            ok(!!object, "Failed to create RTPSO object.\n");
        else
            ok(!object, "Unexpected success.\n");

        if (object)
            ID3D12StateObject_Release(object);
    }

    ID3D12RootSignature_Release(local_rs0);
    ID3D12RootSignature_Release(local_rs1);
    ID3D12Device_Release(device);
    destroy_raytracing_test_context(&context);
}

void test_raytracing_missing_required_objects(void)
{
    struct raytracing_test_context context;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    struct rt_pso_factory factory;
    ID3D12RootSignature *local_rs;
    D3D12_ROOT_PARAMETER param;
    ID3D12StateObject *object;
    ID3D12Device *device;
    unsigned int i, j;

    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_0))
        return;

    ID3D12Device5_QueryInterface(context.device5, &IID_ID3D12Device, (void **)&device);

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &param;
    memset(&param, 0, sizeof(param));
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    create_root_signature(device, &rs_desc, &local_rs);

    /* Just verify that we can correctly create and validate missing parameters. */
    for (i = 0; i < 2; i++)
    {
        for (j = 0; j < 4; j++)
        {
            rt_pso_factory_init(&factory);
            rt_pso_factory_add_dxil_library(&factory, get_dummy_raygen_rt_lib(), 0, NULL);
            if (j != 1)
                rt_pso_factory_add_pipeline_config(&factory, 1);
            if (j != 2)
                rt_pso_factory_add_shader_config(&factory, 8, 4);
            if (j != 3)
                rt_pso_factory_add_local_root_signature(&factory, local_rs);
            object = rt_pso_factory_compile(&context, &factory, i ? D3D12_STATE_OBJECT_TYPE_COLLECTION : D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

            if (j == 0)
                ok(!!object, "Failed to create RTPSO object.\n");
            else
                ok(!object, "Successfully created RTPSO object which is not expected.\n");

            if (object)
                ID3D12StateObject_Release(object);
        }
    }

    ID3D12RootSignature_Release(local_rs);
    ID3D12Device_Release(device);
    destroy_raytracing_test_context(&context);
}

void test_raytracing_reject_duplicate_objects(void)
{
    struct raytracing_test_context context;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12RootSignature *local_rs_alt;
    struct rt_pso_factory factory;
    ID3D12RootSignature *local_rs;
    D3D12_ROOT_PARAMETER param;
    unsigned int pconfig_index;
    unsigned int sconfig_index;
    ID3D12StateObject *object;
    unsigned int lrs_index;
    ID3D12Device *device;
    unsigned int i, j;


    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_0))
        return;

    ID3D12Device5_QueryInterface(context.device5, &IID_ID3D12Device, (void **)&device);

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &param;
    memset(&param, 0, sizeof(param));
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    create_root_signature(device, &rs_desc, &local_rs);

    param.Descriptor.RegisterSpace = 1;
    create_root_signature(device, &rs_desc, &local_rs_alt);

    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            rt_pso_factory_init(&factory);
            rt_pso_factory_add_dxil_library(&factory, get_dummy_raygen_rt_lib(), 0, NULL);
            pconfig_index = rt_pso_factory_add_pipeline_config(&factory, 1);
            sconfig_index = rt_pso_factory_add_shader_config(&factory, 8, 4);
            lrs_index = rt_pso_factory_add_local_root_signature(&factory, local_rs);

            if (i == 0)
            {
                /* Duplicate definitions don't cause issues. */
                if (j == 0)
                    rt_pso_factory_add_pipeline_config(&factory, 1);
                if (j == 1)
                    rt_pso_factory_add_shader_config(&factory, 8, 4);
                if (j == 2)
                    rt_pso_factory_add_local_root_signature(&factory, local_rs);
            }
            else
            {
                /* Conflicting ones do however. To make this work, we need subobject associations. */
                if (j == 0)
                {
                    rt_pso_factory_add_pipeline_config(&factory, 2);
                    if (i == 2)
                        rt_pso_factory_add_subobject_to_exports_association(&factory, pconfig_index, 0, NULL);
                }

                if (j == 1)
                {
                    rt_pso_factory_add_shader_config(&factory, 12, 8);
                    if (i == 2)
                        rt_pso_factory_add_subobject_to_exports_association(&factory, sconfig_index, 0, NULL);
                }

                if (j == 2)
                {
                    rt_pso_factory_add_local_root_signature(&factory, local_rs_alt);
                    if (i == 2)
                        rt_pso_factory_add_subobject_to_exports_association(&factory, lrs_index, 0, NULL);
                }
            }

            object = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
            if (i == 0 || i == 2)
                ok(!!object, "Failed to create state object.\n");
            else
                ok(!object, "Successfully created RTPSO object which is not expected.\n");
            if (object)
                ID3D12StateObject_Release(object);
        }
    }

    ID3D12RootSignature_Release(local_rs);
    ID3D12RootSignature_Release(local_rs_alt);
    ID3D12Device_Release(device);
    destroy_raytracing_test_context(&context);
}

void test_raytracing_embedded_subobjects(void)
{
    struct raytracing_test_context context;
    struct rt_pso_factory factory;
    ID3D12StateObject *object;
    ID3D12Device *device;

    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_0))
        return;

    ID3D12Device5_QueryInterface(context.device5, &IID_ID3D12Device, (void **)&device);

    {
        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_embedded_subobject_rt_lib(), 0, NULL);
        object = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        ok(!!object, "Failed to create state object.\n");
        if (object)
            ID3D12StateObject_Release(object);
    }

    /* Subobjects are not exposed to API side if they are not part of the export.
     * This will fail compilation since RayGen cannot observe its dependent objects. */
    {
        D3D12_EXPORT_DESC export_desc;
        memset(&export_desc, 0, sizeof(export_desc));
        export_desc.Name = u"RayGen";

        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_embedded_subobject_rt_lib(), 1, &export_desc);
        object = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        ok(!object, "Should fail RTPSO creation.\n");
        if (object)
            ID3D12StateObject_Release(object);
    }

    /* Try to compile RayGen, and only export our dependencies. */
    {
        D3D12_EXPORT_DESC export_desc[5];
        memset(export_desc, 0, sizeof(export_desc));
        export_desc[0].Name = u"RayGen";
        export_desc[1].Name = u"grs";
        export_desc[2].Name = u"config";
        export_desc[3].Name = u"pconfig";
        export_desc[4].Name = u"lrs_raygen";

        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_embedded_subobject_rt_lib(), ARRAY_SIZE(export_desc), export_desc);
        object = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        ok(!!object, "Failed to create state object.\n");
        if (object)
            ID3D12StateObject_Release(object);
    }

    /* Try to break our object associations since RayGenAlt != RayGen.
     * However, this does compile oddly enough. */
    {
        D3D12_EXPORT_DESC export_desc[5];
        memset(export_desc, 0, sizeof(export_desc));
        export_desc[0].Name = u"RayGenAlt";
        export_desc[0].ExportToRename = u"RayGen";
        export_desc[1].Name = u"grs";
        export_desc[2].Name = u"config";
        export_desc[3].Name = u"pconfig";
        export_desc[4].Name = u"lrs_raygen";

        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_embedded_subobject_rt_lib(), ARRAY_SIZE(export_desc), export_desc);
        object = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        ok(!!object, "Failed to create state object.\n");
        if (object)
            ID3D12StateObject_Release(object);
    }

    /* Verify that subobject associations trump any DXIL associations.
     * Default associate a subobject.
     * Apparently, this is supposed to override even explicit associations inherited from DXIL. */
    {
        D3D12_EXPORT_DESC export_desc[6];
        memset(export_desc, 0, sizeof(export_desc));
        export_desc[0].Name = u"RayGen";
        export_desc[1].Name = u"grs";
        export_desc[2].Name = u"config";
        export_desc[3].Name = u"pconfig";
        export_desc[4].Name = u"lrs_raygen";
        export_desc[5].Name = u"lrs_other1";

        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_embedded_subobject_rt_lib(), ARRAY_SIZE(export_desc), export_desc);
        rt_pso_factory_add_dxil_subobject_to_exports_association(&factory, u"lrs_other1", 0, NULL);
        object = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        ok(!object, "Unexpected compilation success.\n");
        if (object)
            ID3D12StateObject_Release(object);
    }

    /* Verify that just declaring a subobject overrides (which is not referenced by any other association) module associations. */
    {
        D3D12_ROOT_SIGNATURE_DESC rs_desc;
        D3D12_EXPORT_DESC export_desc[5];
        ID3D12RootSignature *local_rs;
        D3D12_ROOT_PARAMETER param;

        memset(export_desc, 0, sizeof(export_desc));
        export_desc[0].Name = u"RayGen";
        export_desc[1].Name = u"grs";
        export_desc[2].Name = u"config";
        export_desc[3].Name = u"pconfig";
        export_desc[4].Name = u"lrs_raygen";

        memset(&rs_desc, 0, sizeof(rs_desc));
        rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        rs_desc.NumParameters = 1;
        rs_desc.pParameters = &param;
        memset(&param, 0, sizeof(param));
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        param.Descriptor.RegisterSpace = 5;
        create_root_signature(device, &rs_desc, &local_rs);

        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_embedded_subobject_rt_lib(), ARRAY_SIZE(export_desc), export_desc);
        rt_pso_factory_add_local_root_signature(&factory, local_rs);
        object = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        ok(!object, "Unexpected compilation success.\n");
        if (object)
            ID3D12StateObject_Release(object);
        ID3D12RootSignature_Release(local_rs);
    }

    /* Verify that duplicate embedded subobjects fail. */
    {
        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_embedded_subobject_dupe_rt_lib(), 0, NULL);
        object = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        ok(!object, "Unexpected compilation success.\n");
        if (object)
            ID3D12StateObject_Release(object);
    }

    /* Verify that it works if we only export one of each. */
    {
        D3D12_EXPORT_DESC export_desc[3];
        memset(export_desc, 0, sizeof(export_desc));
        export_desc[0].Name = u"RayGen";
        export_desc[1].Name = u"config";
        export_desc[2].Name = u"pconfig";

        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_embedded_subobject_dupe_rt_lib(), ARRAY_SIZE(export_desc), export_desc);
        object = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        ok(!!object, "Failed to create pipeline.\n");
        if (object)
            ID3D12StateObject_Release(object);
    }

    /* Verify that it works if we associate explicitly. */
    {
        LPCWSTR raygen = u"RayGen";

        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_embedded_subobject_dupe_rt_lib(), 0, NULL);
        rt_pso_factory_add_dxil_subobject_to_exports_association(&factory, u"config", 1, &raygen);
        rt_pso_factory_add_dxil_subobject_to_exports_association(&factory, u"pconfig", 1, &raygen);
        object = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        ok(!!object, "Failed to create pipeline.\n");
        if (object)
            ID3D12StateObject_Release(object);
    }

    ID3D12Device_Release(device);
    destroy_raytracing_test_context(&context);
}

void test_raytracing_collection_identifiers(void)
{
    uint8_t collection_identifier[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES] = { 0 };
    uint8_t rtpso_identifier[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES] = { 0 };
    struct raytracing_test_context context;
    ID3D12RootSignature *root_signature;
    ID3D12StateObjectProperties *props;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12StateObject *full_object;
    struct rt_pso_factory factory;
    D3D12_ROOT_PARAMETER param;
    ID3D12StateObject *object;
    ID3D12Device *device;
    const void *ident;
    HRESULT hr;

    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_0))
        return;

    ID3D12Device5_QueryInterface(context.device5, &IID_ID3D12Device, (void **)&device);

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.pParameters = &param;
    rs_desc.NumParameters = 1;
    memset(&param, 0, sizeof(param));
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    create_root_signature(device, &rs_desc, &root_signature);

    rt_pso_factory_init(&factory);
    rt_pso_factory_add_dxil_library(&factory, get_dummy_raygen_rt_lib(), 0, NULL);
    rt_pso_factory_add_pipeline_config(&factory, 1);
    rt_pso_factory_add_shader_config(&factory, 8, 4);
    rt_pso_factory_add_global_root_signature(&factory, root_signature);
    object = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_COLLECTION);
    ok(!!object, "Failed to create collection.\n");

    hr = ID3D12StateObject_QueryInterface(object, &IID_ID3D12StateObjectProperties, (void **)&props);
    ok(SUCCEEDED(hr), "Failed to query props interface, hr #%x.\n", hr);
    ident = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"main");
    ok(!!ident, "Failed to query identifier for COLLECTION.\n");
    if (ident)
        memcpy(collection_identifier, ident, sizeof(collection_identifier));
    ID3D12StateObjectProperties_Release(props);

    rt_pso_factory_init(&factory);
    rt_pso_factory_add_existing_collection(&factory, object, 0, NULL);
    rt_pso_factory_add_pipeline_config(&factory, 1);
    rt_pso_factory_add_shader_config(&factory, 8, 4);
    full_object = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
    ok(!!full_object, "Failed to create RTPSO.\n");

    hr = ID3D12StateObject_QueryInterface(full_object, &IID_ID3D12StateObjectProperties, (void **)&props);
    ident = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"main");
    ok(!!ident, "Failed to query identifier for COLLECTION.\n");
    if (ident)
        memcpy(rtpso_identifier, ident, sizeof(collection_identifier));
    ID3D12StateObjectProperties_Release(props);

    ok(memcmp(collection_identifier, rtpso_identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) == 0, "COLLECTION identifier does not match RTPSO identifier.\n");

    if (object)
        ID3D12StateObject_Release(object);
    if (full_object)
        ID3D12StateObject_Release(full_object);
    ID3D12RootSignature_Release(root_signature);
    ID3D12Device_Release(device);
    destroy_raytracing_test_context(&context);
}

void test_raytracing_object_assignment_ignore_default(void)
{
    struct raytracing_test_context context;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12RootSignature *root_sigs[4];
    unsigned int root_sig_indices[4];
    D3D12_ROOT_PARAMETER params[2];
    struct rt_pso_factory factory;
    ID3D12StateObject *pso;
    ID3D12Device *device;
    unsigned int i;

    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_0))
        return;
    ID3D12Device5_QueryInterface(context.device5, &IID_ID3D12Device, (void **)&device);

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(params, 0, sizeof(params));

    rs_desc.pParameters = params;
    rs_desc.NumParameters = 1;
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    create_root_signature(device, &rs_desc, &root_sigs[0]);
    params[0].Descriptor.ShaderRegister = 1;
    create_root_signature(device, &rs_desc, &root_sigs[1]);

    rs_desc.Flags = 0;
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Descriptor.ShaderRegister = 2;
    params[1] = params[0];
    params[1].Descriptor.ShaderRegister = 3;
    rs_desc.NumParameters = 2;

    /* FIXME: We cannot handle divergent global root signatures yet.
     * We can still test the rules for local root signatures, so just pretend that the root signatures
     * are a union of each other to workaround this for now.
     * Ideally, root_sigs[2] contains u2 and [3] contains u3. */
    create_root_signature(device, &rs_desc, &root_sigs[2]);
    create_root_signature(device, &rs_desc, &root_sigs[3]);

    /* Test that default assignment tie breaks to a root signature that has not been explicitly assigned to anything. */
    {
        static LPCWSTR exports[] = { u"Entry1", u"Entry2", u"Entry3", u"Entry4" };

        rt_pso_factory_init(&factory);

        for (i = 0; i < ARRAY_SIZE(root_sigs); i++)
        {
            if (i < 2)
                root_sig_indices[i] = rt_pso_factory_add_local_root_signature(&factory, root_sigs[i]);
            else
                root_sig_indices[i] = rt_pso_factory_add_global_root_signature(&factory, root_sigs[i]);
        }

        rt_pso_factory_add_dxil_library(&factory, get_default_assignment_bindings_rt_lib(), 0, NULL);
        rt_pso_factory_add_pipeline_config(&factory, 1);
        rt_pso_factory_add_shader_config(&factory, 8, 4);

        rt_pso_factory_add_subobject_to_exports_association(&factory, root_sig_indices[0], 1, &exports[0]);
        /* The second local root signature is chosen as default since it is the only one that isn't used as part of an assignment. */
        rt_pso_factory_add_subobject_to_exports_association(&factory, root_sig_indices[2], 1, &exports[2]);
        /* The second global root signature is chosen as default since it is the only one that isn't used as part of an assignment. */

        pso = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        ok(!!pso, "Failed to compile PSO.\n");
        if (pso)
            ID3D12StateObject_Release(pso);
    }

    /* Test that multiple default assignment candidates forces a NULL assignment, which should lead to compilation failure. */
    {
        static D3D12_EXPORT_DESC export_descs[] = {
            { u"Entry2", NULL, 0 },
            { u"Entry4", NULL, 0 },
        };

        rt_pso_factory_init(&factory);

        for (i = 0; i < ARRAY_SIZE(root_sigs); i++)
        {
            if (i < 2)
                root_sig_indices[i] = rt_pso_factory_add_local_root_signature(&factory, root_sigs[i]);
            else
                root_sig_indices[i] = rt_pso_factory_add_global_root_signature(&factory, root_sigs[i]);
        }

        rt_pso_factory_add_dxil_library(&factory, get_default_assignment_bindings_rt_lib(), ARRAY_SIZE(export_descs), export_descs);
        rt_pso_factory_add_pipeline_config(&factory, 1);
        rt_pso_factory_add_shader_config(&factory, 8, 4);

        /* Same as previous test, but don't assign to Entry1 and Entry3 this time.
         * Now we'll fail, because both LRS and GRS will collapse to NULL. */

        pso = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
        ok(!pso, "Unexpected success in compiling PSO.\n");
        if (pso)
            ID3D12StateObject_Release(pso);
    }

    for (i = 0; i < ARRAY_SIZE(root_sigs); i++)
        ID3D12RootSignature_Release(root_sigs[i]);
    ID3D12Device_Release(device);
    destroy_raytracing_test_context(&context);
}

void test_raytracing_root_signature_from_subobject(void)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC cs_desc;
    struct raytracing_test_context context;
    D3D12_SHADER_BYTECODE lib;
    ID3D12PipelineState *cs;
    ID3D12RootSignature *rs;
    HRESULT hr;

    static const BYTE cs_code_dxil[] =
    {
        0x44, 0x58, 0x42, 0x43, 0xbf, 0x52, 0x25, 0x19, 0x91, 0xa2, 0xa8, 0xed, 0x24, 0x40, 0x73, 0x4f, 0xb2, 0xdb, 0xe6, 0x67, 0x01, 0x00, 0x00, 0x00, 0xd0, 0x05, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
        0x38, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00, 0xd0, 0x00, 0x00, 0x00, 0xec, 0x00, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4f, 0x53, 0x47, 0x31, 0x08, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x50, 0x53, 0x56, 0x30, 0x60, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x41, 0x53, 0x48, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc8, 0xf6, 0x5d, 0x3e,
        0xc4, 0x31, 0xe6, 0x0e, 0x1d, 0x6d, 0xf1, 0xb0, 0xa5, 0xe6, 0xe4, 0x18, 0x44, 0x58, 0x49, 0x4c, 0xdc, 0x04, 0x00, 0x00, 0x60, 0x00, 0x05, 0x00, 0x37, 0x01, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c,
        0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0xc4, 0x04, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x2e, 0x01, 0x00, 0x00, 0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91, 0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19, 0x1e, 0x04, 0x8b, 0x62, 0x80, 0x14, 0x45, 0x02,
        0x42, 0x92, 0x0b, 0x42, 0xa4, 0x10, 0x32, 0x14, 0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x52, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5, 0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90,
        0x91, 0x22, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x29, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x1b, 0x8c, 0xe0, 0xff, 0xff, 0xff, 0xff, 0x07,
        0x40, 0x02, 0xaa, 0x0d, 0x84, 0xf0, 0xff, 0xff, 0xff, 0xff, 0x03, 0x20, 0x01, 0x00, 0x00, 0x00, 0x49, 0x18, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x82, 0x60, 0x42, 0x20, 0x00, 0x00, 0x00,
        0x89, 0x20, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x32, 0x22, 0x48, 0x09, 0x20, 0x64, 0x85, 0x04, 0x93, 0x22, 0xa4, 0x84, 0x04, 0x93, 0x22, 0xe3, 0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x8a, 0x8c,
        0x0b, 0x84, 0xa4, 0x4c, 0x10, 0x48, 0x23, 0x00, 0x25, 0x00, 0x14, 0xe6, 0x08, 0x10, 0x1a, 0xf7, 0x0c, 0x97, 0x3f, 0x61, 0x0f, 0x21, 0xf9, 0x21, 0xd0, 0x0c, 0x0b, 0x81, 0x02, 0x32, 0x47, 0x00,
        0x06, 0x73, 0x04, 0x41, 0x31, 0x8a, 0x19, 0xc6, 0x1c, 0x42, 0x33, 0x00, 0x45, 0x01, 0xa6, 0x18, 0xa3, 0x94, 0x52, 0x83, 0xd6, 0x40, 0xc0, 0x30, 0x02, 0xa1, 0xcc, 0xb4, 0x06, 0xe3, 0xc0, 0x0e,
        0xe1, 0x30, 0x0f, 0xf3, 0xe0, 0x06, 0xb2, 0x70, 0x0b, 0xb3, 0x40, 0x0f, 0xf2, 0x50, 0x0f, 0xe3, 0x40, 0x0f, 0xf5, 0x20, 0x0f, 0xe5, 0x40, 0x0e, 0xa2, 0x50, 0x0f, 0xe6, 0x60, 0x0e, 0xe5, 0x20,
        0x0f, 0x7c, 0x60, 0x0f, 0xe5, 0x30, 0x0e, 0xf4, 0xf0, 0x0e, 0xf2, 0xc0, 0x07, 0xe6, 0xc0, 0x0e, 0xef, 0x10, 0x0e, 0xf4, 0xc0, 0x06, 0x60, 0x40, 0x07, 0x7e, 0x00, 0x06, 0x7e, 0x80, 0x02, 0x47,
        0x6f, 0x8e, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e,
        0x7a, 0x50, 0x0e, 0x6d, 0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20,
        0x07, 0x6d, 0x90, 0x0e, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74,
        0xd0, 0x06, 0xe6, 0x10, 0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07,
        0x6d, 0xe0, 0x0e, 0x78, 0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x43, 0x9e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86,
        0x3c, 0x08, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x79, 0x16, 0x20, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc8, 0x02, 0x01, 0x09, 0x00, 0x00, 0x00,
        0x32, 0x1e, 0x98, 0x10, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x32, 0x25, 0x30, 0x02, 0x50, 0x0c, 0x85, 0x51, 0x20, 0x85, 0x40, 0x67, 0x04, 0x80, 0x62, 0x81, 0x10,
        0x9c, 0x01, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x3b, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0x44, 0x35, 0x18, 0x63, 0x0b, 0x73, 0x3b, 0x03, 0xb1, 0x2b, 0x93, 0x9b, 0x4b,
        0x7b, 0x73, 0x03, 0x99, 0x71, 0xb9, 0x01, 0x41, 0xa1, 0x0b, 0x3b, 0x9b, 0x7b, 0x91, 0x2a, 0x62, 0x2a, 0x0a, 0x9a, 0x2a, 0xfa, 0x9a, 0xb9, 0x81, 0x79, 0x31, 0x4b, 0x73, 0x0b, 0x63, 0x4b, 0xd9,
        0x10, 0x04, 0x13, 0x04, 0x63, 0x98, 0x20, 0x18, 0xc4, 0x06, 0x61, 0x20, 0x26, 0x08, 0x46, 0xb1, 0x41, 0x18, 0x0c, 0x0a, 0x63, 0x73, 0x1b, 0x06, 0xc4, 0x20, 0x26, 0x08, 0xd0, 0x42, 0x60, 0x82,
        0x60, 0x18, 0x13, 0x84, 0x23, 0x99, 0x20, 0x18, 0xc7, 0x06, 0x61, 0x70, 0x36, 0x2c, 0x84, 0xb2, 0x10, 0xc4, 0xc0, 0x34, 0x4d, 0xf3, 0x6c, 0x08, 0xa0, 0x0d, 0x04, 0x10, 0x01, 0xc0, 0x04, 0x41,
        0x00, 0x48, 0xb4, 0x85, 0xa5, 0xb9, 0x4d, 0x10, 0x22, 0x65, 0x82, 0x60, 0x20, 0x1b, 0x86, 0x61, 0x18, 0x36, 0x10, 0x44, 0x65, 0x5d, 0x1b, 0x8a, 0x89, 0x02, 0x24, 0xac, 0x0a, 0x1b, 0x9b, 0x5d,
        0x9b, 0x4b, 0x1a, 0x59, 0x99, 0x1b, 0xdd, 0x94, 0x20, 0xa8, 0x42, 0x86, 0xe7, 0x62, 0x57, 0x26, 0x37, 0x97, 0xf6, 0xe6, 0x36, 0x25, 0x20, 0x9a, 0x90, 0xe1, 0xb9, 0xd8, 0x85, 0xb1, 0xd9, 0x95,
        0xc9, 0x4d, 0x09, 0x8c, 0x3a, 0x64, 0x78, 0x2e, 0x73, 0x68, 0x61, 0x64, 0x65, 0x72, 0x4d, 0x6f, 0x64, 0x65, 0x6c, 0x53, 0x02, 0xa4, 0x0c, 0x19, 0x9e, 0x8b, 0x5c, 0xd9, 0xdc, 0x5b, 0x9d, 0xdc,
        0x58, 0xd9, 0xdc, 0x94, 0x20, 0xaa, 0x43, 0x86, 0xe7, 0x52, 0xe6, 0x46, 0x27, 0x97, 0x07, 0xf5, 0x96, 0xe6, 0x46, 0x37, 0x37, 0x25, 0xc0, 0x00, 0x79, 0x18, 0x00, 0x00, 0x49, 0x00, 0x00, 0x00,
        0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88, 0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10,
        0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce, 0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03,
        0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48, 0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e,
        0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e, 0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b,
        0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b, 0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90,
        0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78, 0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e,
        0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca,
        0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39, 0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82,
        0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x8c, 0xc8, 0x21, 0x07, 0x7c, 0x70, 0x03, 0x72, 0x10, 0x87, 0x73, 0x70, 0x03, 0x7b, 0x08, 0x07, 0x79, 0x60, 0x87, 0x70, 0xc8, 0x87, 0x77, 0xa8, 0x07, 0x7a,
        0x00, 0x00, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x26, 0x50, 0x0d, 0x97, 0xef, 0x3c, 0x7e, 0x40, 0x15, 0x05, 0x11, 0xb1, 0x93, 0x13, 0x11, 0x7e, 0x71, 0xdb, 0x16, 0x20,
        0x0d, 0x97, 0xef, 0x3c, 0xbe, 0x10, 0x11, 0xc0, 0x44, 0x84, 0x40, 0x33, 0x2c, 0x84, 0x01, 0x10, 0x0c, 0x80, 0x34, 0x00, 0x61, 0x20, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c,
        0x10, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x34, 0x4a, 0xa0, 0x3c, 0xc8, 0x94, 0x5c, 0x29, 0x06, 0x90, 0x1a, 0x23, 0x00, 0x41, 0x10, 0xc4, 0xbf, 0x31, 0x02, 0x10, 0x04, 0x41, 0x10, 0x0c,
        0xc6, 0x08, 0x40, 0x10, 0x04, 0x49, 0x30, 0x18, 0x23, 0x00, 0x41, 0x10, 0x44, 0xc1, 0x00, 0x00, 0x23, 0x06, 0x09, 0x00, 0x82, 0x60, 0x80, 0x4c, 0x06, 0x02, 0x41, 0xcb, 0x88, 0x41, 0x03, 0x80,
        0x20, 0x18, 0x2c, 0x93, 0x11, 0x44, 0x51, 0x41, 0x0c, 0x02, 0x82, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_0))
        return;

    /* Unambiguous case. */
    lib = get_embedded_root_signature_subobject_rt_lib();
    hr = ID3D12Device5_CreateRootSignature(context.device5, 0, lib.pShaderBytecode, lib.BytecodeLength, &IID_ID3D12RootSignature, (void **)&rs);
    ok(SUCCEEDED(hr), "Failed to create root signature, hr #%x.\n", hr);

    if (SUCCEEDED(hr))
    {
        memset(&cs_desc, 0, sizeof(cs_desc));
        cs_desc.CS.pShaderBytecode = cs_code_dxil;
        cs_desc.CS.BytecodeLength = sizeof(cs_code_dxil);
        cs_desc.pRootSignature = rs;
        hr = ID3D12Device5_CreateComputePipelineState(context.device5, &cs_desc, &IID_ID3D12PipelineState, (void **)&cs);
        ok(SUCCEEDED(hr), "Failed to create pipeline state, hr #%x.\n", hr);
        if (SUCCEEDED(hr))
            ID3D12PipelineState_Release(cs);
        ID3D12RootSignature_Release(rs);
    }

    /* Two global root signatures are declared in the lib. This results in failure. */
    lib = get_embedded_root_signature_subobject_rt_lib_conflict();
    hr = ID3D12Device5_CreateRootSignature(context.device5, 0, lib.pShaderBytecode, lib.BytecodeLength, &IID_ID3D12RootSignature, (void **)&rs);
    ok(hr == E_INVALIDARG, "Unexpected return value in creating root signature, hr #%x.\n", hr);
    if (SUCCEEDED(hr))
        ID3D12RootSignature_Release(rs);

    /* Two global root signatures are declared in the lib using mixed [RootSignature] and subobject declaration. This results in ... success? DXC seems to drop that root sig ... */
    lib = get_embedded_root_signature_subobject_rt_lib_conflict_mixed();
    hr = ID3D12Device5_CreateRootSignature(context.device5, 0, lib.pShaderBytecode, lib.BytecodeLength, &IID_ID3D12RootSignature, (void **)&rs);
    ok(SUCCEEDED(hr), "Unexpected success in creating root signature.\n");
    if (SUCCEEDED(hr))
    {
        memset(&cs_desc, 0, sizeof(cs_desc));
        cs_desc.CS.pShaderBytecode = cs_code_dxil;
        cs_desc.CS.BytecodeLength = sizeof(cs_code_dxil);
        cs_desc.pRootSignature = rs;
        hr = ID3D12Device5_CreateComputePipelineState(context.device5, &cs_desc, &IID_ID3D12PipelineState, (void **)&cs);
        ok(hr == E_INVALIDARG, "Unexpected success in creating pipeline state, hr #%x.\n", hr);
        if (SUCCEEDED(hr))
            ID3D12PipelineState_Release(cs);
        ID3D12RootSignature_Release(rs);
    }

    destroy_raytracing_test_context(&context);
}

void test_raytracing_multi_global_rs(void)
{
    static const LPCWSTR collection_export_names[] = { u"Col0", u"Col1", u"Col2", u"Col3" };
    ID3D12StateObject *collections[4] = { NULL };
    D3D12_STATIC_SAMPLER_DESC static_sampler;
    struct raytracing_test_context context;
    D3D12_DESCRIPTOR_RANGE desc_range[2];
    ID3D12Resource *sbt_buffer = NULL;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    ID3D12DescriptorHeap *desc_heap;
    struct rt_pso_factory factory;
    D3D12_ROOT_PARAMETER rs_param;
    D3D12_EXPORT_DESC exports[3];
    struct resource_readback rb;
    unsigned int rs_indices[3];
    ID3D12RootSignature *rs[4];
    ID3D12Resource *resource;
    ID3D12Resource *output;
    ID3D12StateObject *pso;
    ID3D12Device *device;
    unsigned int i;

    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_0))
        return;

    device = context.context.device;

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(&rs_param, 0, sizeof(rs_param));
    memset(desc_range, 0, sizeof(desc_range));

    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &rs_param;
    rs_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_param.DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(desc_range);
    rs_param.DescriptorTable.pDescriptorRanges = desc_range;

    memset(&static_sampler, 0, sizeof(static_sampler));
    static_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    static_sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_desc.pStaticSamplers = &static_sampler;
    rs_desc.NumStaticSamplers = 1;

    for (i = 0; i < ARRAY_SIZE(rs); i++)
    {
        desc_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        desc_range[0].NumDescriptors = 1;
        desc_range[0].OffsetInDescriptorsFromTableStart = i;
        desc_range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        desc_range[1].NumDescriptors = 1;
        desc_range[1].OffsetInDescriptorsFromTableStart = 4 + i;

        static_sampler.AddressU = (i & 1) ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        static_sampler.AddressV = (i & 2) ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        static_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

        create_root_signature(device, &rs_desc, &rs[i]);
    }

    memset(exports, 0, sizeof(exports));

    for (i = 0; i < ARRAY_SIZE(collections); i++)
    {
        exports[0].Name = collection_export_names[i];
        exports[0].ExportToRename = u"RayGenCol";
        if (i == ARRAY_SIZE(exports))
        {
            exports[1].Name = u"RayGen3";
            exports[1].ExportToRename = u"RayGen";
        }
        rt_pso_factory_init(&factory);
        rt_pso_factory_add_global_root_signature(&factory, rs[i]);
        rt_pso_factory_add_pipeline_config(&factory, 1);
        rt_pso_factory_add_shader_config(&factory, 4, 4);
        rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_NONE);
        rt_pso_factory_add_dxil_library(&factory, get_multi_rs_lib(), i == ARRAY_SIZE(exports) ? 2 : 1, &exports[0]);
        collections[i] = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_COLLECTION);
        ok(!!collections[i], "Failed to create collection.\n");
        if (!collections[i])
            goto pso_error;
    }

    rt_pso_factory_init(&factory);
    for (i = 0; i < ARRAY_SIZE(rs_indices); i++)
        rs_indices[i] = rt_pso_factory_add_global_root_signature(&factory, rs[i]);
    rt_pso_factory_add_pipeline_config(&factory, 1);
    rt_pso_factory_add_shader_config(&factory, 4, 4);
    rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_NONE);
    for (i = 0; i < ARRAY_SIZE(collections); i++)
        rt_pso_factory_add_existing_collection(&factory, collections[i], 0, NULL);

    /* Re-export the same entry point with different names. */
    exports[0].Name = u"RayGen0";
    exports[0].ExportToRename = u"RayGen";
    exports[1].Name = u"RayGen1";
    exports[1].ExportToRename = u"RayGen";
    exports[2].Name = u"RayGen2";
    exports[2].ExportToRename = u"RayGen";
    /* Intentionally let rs[3] variant exist only through import from collection. RayGen3 will come from the COLLECTION instead. */
    rt_pso_factory_add_dxil_library(&factory, get_multi_rs_lib(), ARRAY_SIZE(exports), exports);

    for (i = 0; i < ARRAY_SIZE(exports); i++)
        rt_pso_factory_add_subobject_to_exports_association(&factory, rs_indices[i], 1, &exports[i].Name);

    pso = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);
    ok(!!pso, "Failed to compile PSO.\n");
    if (!pso)
        goto pso_error;

    if (pso)
    {
        uint8_t sbt[ARRAY_SIZE(rs) * 2][D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT];
        ID3D12StateObjectProperties *props;

        ID3D12StateObject_QueryInterface(pso, &IID_ID3D12StateObjectProperties, (void **)&props);
        for (i = 0; i < ARRAY_SIZE(exports); i++)
        {
            memcpy(sbt[2 * i + 0], ID3D12StateObjectProperties_GetShaderIdentifier(props, exports[i].Name),
                    D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        }
        memcpy(sbt[2 * 3 + 0], ID3D12StateObjectProperties_GetShaderIdentifier(props, u"RayGen3"),
                D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

        for (i = 0; i < ARRAY_SIZE(collections); i++)
        {
            memcpy(sbt[2 * i + 1], ID3D12StateObjectProperties_GetShaderIdentifier(props, collection_export_names[i]),
                    D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        }
        ID3D12StateObjectProperties_Release(props);

        sbt_buffer = create_upload_buffer(device, sizeof(sbt), sbt);
    }

    desc_heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ARRAY_SIZE(rs) * 2);

    resource = create_default_texture2d(device, 2, 2, 8, 1, DXGI_FORMAT_R8_UNORM,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    output = create_default_buffer(device, ARRAY_SIZE(rs) * sizeof(uint32_t),
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    for (i = 0; i < ARRAY_SIZE(rs); i++)
    {
        const uint8_t data[] = { 4 * i + 1, 4 * i + 2, 4 * i + 3, 4 * i + 4 };
        D3D12_SUBRESOURCE_DATA subresource;

        subresource.pData = data;
        subresource.RowPitch = 2;
        subresource.SlicePitch = 4;
        upload_texture_data_base(resource, &subresource, i, 1, context.context.queue, context.context.list);
        reset_command_list(context.context.list, context.context.allocator);
    }

    for (i = 0; i < ARRAY_SIZE(rs); i++)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;

        memset(&srv_desc, 0, sizeof(srv_desc));
        memset(&uav_desc, 0, sizeof(uav_desc));

        srv_desc.Format = DXGI_FORMAT_R8_UNORM;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2DArray.MipLevels = 1;
        srv_desc.Texture2DArray.FirstArraySlice = i;
        srv_desc.Texture2DArray.ArraySize = 1;

        uav_desc.Format = DXGI_FORMAT_UNKNOWN;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.FirstElement = i;
        uav_desc.Buffer.NumElements = 1;
        uav_desc.Buffer.StructureByteStride = sizeof(uint32_t);

        ID3D12Device_CreateShaderResourceView(device, resource, &srv_desc,
            get_cpu_handle(device, desc_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, i));
        ID3D12Device_CreateUnorderedAccessView(device, output, NULL, &uav_desc,
            get_cpu_handle(device, desc_heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4 + i));
    }

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.context.list, 1, &desc_heap);
    ID3D12GraphicsCommandList4_SetPipelineState1(context.list4, pso);

    /* It is valid to use different global root signatures as long as we only dispatch shader groups
     * which are compatible. :< */
    for (i = 0; i < ARRAY_SIZE(rs); i++)
    {
        D3D12_DISPATCH_RAYS_DESC ray_desc;
        ID3D12GraphicsCommandList_SetComputeRootSignature(context.context.list, rs[i]);
        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.context.list, 0,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(desc_heap));

        memset(&ray_desc, 0, sizeof(ray_desc));
        ray_desc.Width = 1;
        ray_desc.Height = 1;
        ray_desc.Depth = 1;

        /* Dispatch RayGen(*), then Col(*). */
        ray_desc.RayGenerationShaderRecord.StartAddress =
                ID3D12Resource_GetGPUVirtualAddress(sbt_buffer) + 2 * i * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        ray_desc.RayGenerationShaderRecord.SizeInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        ID3D12GraphicsCommandList4_DispatchRays(context.list4, &ray_desc);
        ray_desc.RayGenerationShaderRecord.StartAddress += D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        ID3D12GraphicsCommandList4_DispatchRays(context.list4, &ray_desc);
    }

    transition_resource_state(context.context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output, DXGI_FORMAT_R32_UINT, &rb, context.context.queue, context.context.list);

    for (i = 0; i < ARRAY_SIZE(rs); i++)
    {
        const uint32_t expected = (1 + 5 * i) * (10 + 1);
        uint32_t value;
        value = get_readback_uint(&rb, i, 0, 0);
        ok(value == expected, "Dispatch %u: Expected %u, got %u.\n", i, expected, value);
    }

    release_resource_readback(&rb);

    ID3D12DescriptorHeap_Release(desc_heap);
    ID3D12Resource_Release(output);
    ID3D12Resource_Release(resource);
    ID3D12Resource_Release(sbt_buffer);
    ID3D12StateObject_Release(pso);
pso_error:
    for (i = 0; i < ARRAY_SIZE(rs); i++)
        ID3D12RootSignature_Release(rs[i]);
    for (i = 0; i < ARRAY_SIZE(collections); i++)
        if (collections[i])
            ID3D12StateObject_Release(collections[i]);
    destroy_raytracing_test_context(&context);
}

void test_raytracing_deferred_compilation(void)
{
    struct raytracing_test_context context;
    ID3D12StateObjectProperties *props;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_DESCRIPTOR_RANGE rs_range;
    D3D12_ROOT_PARAMETER rs_param;
    struct rt_pso_factory factory;
    ID3D12StateObject *collection;
    ID3D12RootSignature *rs_alt;
    const void *invariant_ident;
    ID3D12StateObject *rtpso;
    ID3D12RootSignature *rs;
    ID3D12Device *device;
    const void *ident;

    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_0))
        return;

    device = context.context.device;

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &rs_param;
    memset(&rs_param, 0, sizeof(rs_param));
    rs_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    create_root_signature(device, &rs_desc, &rs);

    rs_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_param.DescriptorTable.NumDescriptorRanges = 1;
    rs_param.DescriptorTable.pDescriptorRanges = &rs_range;

    memset(&rs_range, 0, sizeof(rs_range));
    rs_range.BaseShaderRegister = 1;
    rs_range.NumDescriptors = 4;
    rs_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    create_root_signature(device, &rs_desc, &rs_alt);

    /* Native behavior: Any shader group that can be successfully compiled will be compiled and you can query identifiers.
     * If there are unsatisfied dependencies, the compilation is deferred until link time. */
    {
        D3D12_EXPORT_DESC exp = { NULL };
        unsigned int index;
        LPCWSTR name;

        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_dummy_raygen_rt_lib(), 0, NULL);
        rt_pso_factory_add_shader_config(&factory, 4, 4);
        rt_pso_factory_add_pipeline_config(&factory, 1);
        rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_ALLOW_LOCAL_DEPENDENCIES_ON_EXTERNAL_DEFINITIONS);
        exp.Name = u"Entry3";
        rt_pso_factory_add_dxil_library(&factory, get_default_assignment_bindings_rt_lib(), 1, &exp);
        /* We must be careful not to give "main" export an association, or we cannot override it later. */
        index = rt_pso_factory_add_local_root_signature(&factory, rs_alt);
        name = u"Entry3";
        rt_pso_factory_add_subobject_to_exports_association(&factory, index, 1, &name);
        collection = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_COLLECTION);

        ok(!!collection, "Failed to create collection.\n");

        if (collection)
        {
            ID3D12StateObject_QueryInterface(collection, &IID_ID3D12StateObjectProperties, (void **)&props);
            ident = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"main");
            ok(!ident, "Did not expect identifier in collection.\n");
            invariant_ident = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"Entry3");
            /* Unclear from spec if this is okay or not. */
            todo ok(!!invariant_ident, "Expected identifier in collection.\n");
            ID3D12StateObjectProperties_Release(props);

            rt_pso_factory_init(&factory);
            rt_pso_factory_add_existing_collection(&factory, collection, 0, NULL);
            rt_pso_factory_add_local_root_signature(&factory, rs);
            rtpso = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

            ok(!!rtpso, "Failed to create RTPSO.\n");

            if (rtpso)
            {
                ID3D12StateObject_QueryInterface(rtpso, &IID_ID3D12StateObjectProperties, (void **)&props);
                ident = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"main");
                ok(!!ident, "Expected valid identifier.\n");
                ident = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"Entry3");
                ok(!!ident, "Expected valid identifier.\n");
                bug_if(is_amd_windows_device(device))
                    ok(!invariant_ident || ident == invariant_ident, "Expected invariant identifier for Entry3.\n");
                ID3D12StateObjectProperties_Release(props);
                ID3D12StateObject_Release(rtpso);
            }
            ID3D12StateObject_Release(collection);
        }
    }

    ID3D12RootSignature_Release(rs_alt);

    memset(&rs_range, 0, sizeof(rs_range));
    rs_range.BaseShaderRegister = 0;
    rs_range.RegisterSpace = 1;
    rs_range.NumDescriptors = 2;
    rs_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    create_root_signature(device, &rs_desc, &rs_alt);

    {
        D3D12_HIT_GROUP_DESC hit_desc = { 0 };
        D3D12_EXPORT_DESC exp[2] = {{ 0 }};
        unsigned int index;
        LPCWSTR name;

        rt_pso_factory_init(&factory);
        rt_pso_factory_add_dxil_library(&factory, get_dummy_raygen_rt_lib(), 0, NULL);

        exp[0].ExportToRename = u"RayClosest";
        exp[0].Name = u"Closest";
        exp[1].ExportToRename = u"RayAnyTriangle";
        exp[1].Name = u"AnyHit";

        rt_pso_factory_add_dxil_library(&factory, get_default_rt_lib(), ARRAY_SIZE(exp), exp);

        hit_desc.AnyHitShaderImport = u"AnyHit";
        hit_desc.ClosestHitShaderImport = u"Closest";
        hit_desc.HitGroupExport = u"HitGroup";
        hit_desc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        rt_pso_factory_add_hit_group(&factory, &hit_desc);

        /* Have to provide the local root signature here, or runtime complains that Closest / AnyHit see conflicting associations, which is kinda bogus ... */
        index = rt_pso_factory_add_local_root_signature(&factory, rs_alt);
        name = u"Closest";
        rt_pso_factory_add_subobject_to_exports_association(&factory, index, 1, &name);

        rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_ALLOW_LOCAL_DEPENDENCIES_ON_EXTERNAL_DEFINITIONS);
        collection = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_COLLECTION);

        ok(!!collection, "Failed to create collection.\n");

        if (collection)
        {
            rt_pso_factory_init(&factory);
            rt_pso_factory_add_shader_config(&factory, 16, 16);
            rt_pso_factory_add_pipeline_config(&factory, 1);
            rt_pso_factory_add_existing_collection(&factory, collection, 0, NULL);
            index = rt_pso_factory_add_local_root_signature(&factory, rs);
            name = u"main";
            rt_pso_factory_add_subobject_to_exports_association(&factory, index, 1, &name);

            rtpso = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

            ok(!!rtpso, "Failed to create RTPSO.\n");

            if (rtpso)
            {
                ID3D12StateObject_QueryInterface(rtpso, &IID_ID3D12StateObjectProperties, (void **)&props);
                ident = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"main");
                ok(!!ident, "Expected valid identifier.\n");
                ident = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"HitGroup");
                ok(!!ident, "Expected valid identifier.\n");
                ID3D12StateObjectProperties_Release(props);
                ID3D12StateObject_Release(rtpso);
            }
            ID3D12StateObject_Release(collection);
        }
    }

    ID3D12RootSignature_Release(rs);
    ID3D12RootSignature_Release(rs_alt);
    destroy_raytracing_test_context(&context);
}

void test_raytracing_mismatch_global_rs_link(void)
{
    struct raytracing_test_context context;
    struct test_rt_geometry test_rtases;
    const void *collection_miss_handle;
    ID3D12StateObjectProperties *props;
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[3];
    const void *rtpso_raygen_handle;
    struct test_geometry test_geom;
    ID3D12Resource *output_buffer;
    const void *rtpso_miss_handle;
    struct rt_pso_factory factory;
    ID3D12StateObject *collection;
    ID3D12RootSignature *rs_alt;
    struct resource_readback rb;
    ID3D12Resource *sbt_buffer;
    ID3D12StateObject *rtpso;
    ID3D12RootSignature *rs;
    D3D12_EXPORT_DESC exp;
    ID3D12Device *device;

    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_1))
        return;

    device = context.context.device;

    init_test_geometry(device, &test_geom);
    init_rt_geometry(&context, &test_rtases, &test_geom,
        NUM_GEOM_DESC, GEOM_OFFSET_X,
        NUM_UNMASKED_INSTANCES, INSTANCE_GEOM_SCALE, INSTANCE_OFFSET_Y, 0);

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].Descriptor.RegisterSpace = 0;
    rs_params[0].Descriptor.ShaderRegister = 0;
    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[1].Descriptor.RegisterSpace = 0;
    rs_params[1].Descriptor.ShaderRegister = 1;
    rs_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rs_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[2].Descriptor.RegisterSpace = 0;
    rs_params[2].Descriptor.ShaderRegister = 0;

    create_root_signature(context.context.device, &rs_desc, &rs);
    /* Create a trivially incompatible global RS. */
    rs_params[1].Descriptor.ShaderRegister = 0;
    rs_params[0].Descriptor.ShaderRegister = 1;
    create_root_signature(context.context.device, &rs_desc, &rs_alt);

    {
        rt_pso_factory_init(&factory);
        memset(&exp, 0, sizeof(exp));
        exp.Name = u"MissShader";
        rt_pso_factory_add_dxil_library(&factory, get_misfire_lib(), 1, &exp);
        rt_pso_factory_add_pipeline_config(&factory, 1);
        rt_pso_factory_add_shader_config(&factory, 8, 8);
        rt_pso_factory_add_global_root_signature(&factory, rs_alt);
        rt_pso_factory_add_state_object_config(&factory,
            D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS |
            D3D12_STATE_OBJECT_FLAG_ALLOW_LOCAL_DEPENDENCIES_ON_EXTERNAL_DEFINITIONS);
        collection = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_COLLECTION);

        ok(!!collection, "Failed to compile collection.\n");

        ID3D12StateObject_QueryInterface(collection, &IID_ID3D12StateObjectProperties, (void **)&props);
        collection_miss_handle = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"MissShader");
        ok(!!collection_miss_handle, "Failed to query miss handle from COLLECTION.\n");
        ID3D12StateObjectProperties_Release(props);
    }

    /* Relink, but now with a different, incompatible RS. */
    {
        rt_pso_factory_init(&factory);
        memset(&exp, 0, sizeof(exp));
        exp.Name = u"GenShader";
        rt_pso_factory_add_dxil_library(&factory, get_misfire_lib(), 1, &exp);
        rt_pso_factory_add_pipeline_config(&factory, 1);
        rt_pso_factory_add_shader_config(&factory, 8, 8);
        rt_pso_factory_add_global_root_signature(&factory, rs);
        rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS);
        rt_pso_factory_add_existing_collection(&factory, collection, 0, NULL);
        rtpso = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

        ok(!!rtpso, "Failed to compile RTPSO.\n");
        ID3D12StateObject_QueryInterface(rtpso, &IID_ID3D12StateObjectProperties, (void **)&props);
        rtpso_miss_handle = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"MissShader");
        rtpso_raygen_handle = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"GenShader");
        ok(!!rtpso_miss_handle, "Failed to query miss handle from RTPSO.\n");
        ok(!!rtpso_raygen_handle, "Failed to query raygen handle from RTPSO.\n");
        ID3D12StateObjectProperties_Release(props);

        /* Runtime does not recompile, or at least the handles remain invariant.
         * AMD bug: handles are not invariant as prescribed, but the content is at least invariant. */
        bug_if(is_amd_windows_device(device)) ok(rtpso_miss_handle == collection_miss_handle, "Mismatch in miss handles.\n");
        ok(memcmp(rtpso_miss_handle, collection_miss_handle, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES) == 0,
            "COLLECTION and RTPSO handles did not compare equal.\n");
    }

    output_buffer = create_default_buffer(device, sizeof(uint32_t) * 2, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    {
        uint8_t sbt_data[D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT * 2] = { 0 };
        memcpy(sbt_data, rtpso_raygen_handle, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        memcpy(sbt_data + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, rtpso_miss_handle, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        sbt_buffer = create_upload_buffer(device, sizeof(sbt_data), sbt_data);
    }

    ID3D12GraphicsCommandList4_SetPipelineState1(context.list4, rtpso);
    ID3D12GraphicsCommandList4_SetComputeRootSignature(context.list4, rs);
    ID3D12GraphicsCommandList4_SetComputeRootUnorderedAccessView(context.list4, 0, ID3D12Resource_GetGPUVirtualAddress(output_buffer) + 0);
    ID3D12GraphicsCommandList4_SetComputeRootUnorderedAccessView(context.list4, 1, ID3D12Resource_GetGPUVirtualAddress(output_buffer) + sizeof(uint32_t));
    ID3D12GraphicsCommandList4_SetComputeRootShaderResourceView(context.list4, 2, ID3D12Resource_GetGPUVirtualAddress(test_rtases.top_rtas.rtas));
    {
        D3D12_DISPATCH_RAYS_DESC dispatch_desc;
        memset(&dispatch_desc, 0, sizeof(dispatch_desc));

        dispatch_desc.Width = 1;
        dispatch_desc.Height = 1;
        dispatch_desc.Depth = 1;

        dispatch_desc.RayGenerationShaderRecord.SizeInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        dispatch_desc.RayGenerationShaderRecord.StartAddress = ID3D12Resource_GetGPUVirtualAddress(sbt_buffer);

        dispatch_desc.MissShaderTable.SizeInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        dispatch_desc.MissShaderTable.StrideInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        dispatch_desc.MissShaderTable.StartAddress = ID3D12Resource_GetGPUVirtualAddress(sbt_buffer) +
            D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

        ID3D12GraphicsCommandList4_DispatchRays(context.list4, &dispatch_desc);
    }

    transition_resource_state(context.context.list, output_buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output_buffer, DXGI_FORMAT_R32_UINT, &rb, context.context.queue, context.context.list);

    {
        /* NV driver seems to happily trace rays with incompatible RSes. They just view the root parameters differently.
         * AMD does not, but it seems to be robust against crashes in this test at least. */
        uint32_t v0, v1;
        v0 = get_readback_uint(&rb, 0, 0, 0);
        v1 = get_readback_uint(&rb, 1, 0, 0);
        ok(v0 == 1000 + 200 || v0 == 1000, "Expected v0 == 1200 or 1000, got %u\n", v0);
        ok(v1 == 2000 + 100 || v1 == 2000, "Expected v1 == 2100 or 2000, got %u\n", v1);
    }

    ID3D12Resource_Release(sbt_buffer);
    ID3D12Resource_Release(output_buffer);
    release_resource_readback(&rb);
    ID3D12StateObject_Release(collection);
    ID3D12StateObject_Release(rtpso);
    destroy_test_geometry(&test_geom);
    destroy_rt_geometry(&test_rtases);
    ID3D12RootSignature_Release(rs);
    ID3D12RootSignature_Release(rs_alt);
    destroy_raytracing_test_context(&context);
}

void test_raytracing_null_rtas(void)
{
    uint8_t sbt_data[D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT * 2];
    struct raytracing_test_context context;
    D3D12_SHADER_RESOURCE_VIEW_DESC desc;
    ID3D12StateObjectProperties *props;
    D3D12_DESCRIPTOR_RANGE rs_range[1];
    D3D12_ROOT_SIGNATURE_DESC rs_desc;
    D3D12_ROOT_PARAMETER rs_params[3];
    struct rt_pso_factory factory;
    D3D12_DISPATCH_RAYS_DESC rays;
    struct resource_readback rb;
    ID3D12DescriptorHeap *heap;
    const void *raygen_handle;
    ID3D12StateObject *rtpso;
    const void *miss_handle;
    ID3D12Resource *output;
    ID3D12Device *device;
    ID3D12Resource *sbt;

    if (!init_raytracing_test_context(&context, D3D12_RAYTRACING_TIER_1_0))
        return;

    device = context.context.device;

    memset(&rs_desc, 0, sizeof(rs_desc));
    memset(rs_params, 0, sizeof(rs_params));
    memset(rs_range, 0, sizeof(rs_range));
    rs_desc.NumParameters = ARRAY_SIZE(rs_params);
    rs_desc.pParameters = rs_params;
    rs_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rs_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[0].Descriptor.RegisterSpace = 0;
    rs_params[0].Descriptor.ShaderRegister = 0;
    rs_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    rs_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[1].Descriptor.RegisterSpace = 0;
    rs_params[1].Descriptor.ShaderRegister = 1;
    rs_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rs_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rs_params[2].DescriptorTable.NumDescriptorRanges = ARRAY_SIZE(rs_range);
    rs_params[2].DescriptorTable.pDescriptorRanges = rs_range;
    rs_range[0].NumDescriptors = 1;
    rs_range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

    create_root_signature(context.context.device, &rs_desc, &context.context.root_signature);

    rt_pso_factory_init(&factory);
    rt_pso_factory_add_dxil_library(&factory, get_misfire_lib(), 0, NULL);
    rt_pso_factory_add_pipeline_config(&factory, 1);
    rt_pso_factory_add_shader_config(&factory, 8, 8);
    rt_pso_factory_add_global_root_signature(&factory, context.context.root_signature);
    rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_NONE);
    rtpso = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

    ID3D12StateObject_QueryInterface(rtpso, &IID_ID3D12StateObjectProperties, (void **)&props);
    miss_handle = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"MissShader");
    ok(!!miss_handle, "Failed to query miss handle.\n");
    raygen_handle = ID3D12StateObjectProperties_GetShaderIdentifier(props, u"GenShader");
    ok(!!raygen_handle, "Failed to query raygen handle.\n");
    ID3D12StateObjectProperties_Release(props);

    output = create_default_buffer(device, 2 * sizeof(uint32_t), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

    memcpy(sbt_data + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT * 0, raygen_handle, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    memcpy(sbt_data + D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT * 1, miss_handle, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
    sbt = create_upload_buffer(device, sizeof(sbt_data), sbt_data);

    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.RaytracingAccelerationStructure.Location = 0;
    ID3D12Device_CreateShaderResourceView(device, NULL, &desc, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));

    ID3D12GraphicsCommandList_SetDescriptorHeaps(context.context.list, 1, &heap);
    ID3D12GraphicsCommandList4_SetPipelineState1(context.list4, rtpso);
    ID3D12GraphicsCommandList_SetComputeRootSignature(context.context.list, context.context.root_signature);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.context.list, 0, ID3D12Resource_GetGPUVirtualAddress(output) + 0);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(context.context.list, 1, ID3D12Resource_GetGPUVirtualAddress(output) + 4);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(context.context.list, 2, ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
    memset(&rays, 0, sizeof(rays));
    rays.Width = 1;
    rays.Height = 1;
    rays.Depth = 1;
    rays.RayGenerationShaderRecord.StartAddress = ID3D12Resource_GetGPUVirtualAddress(sbt) + 0 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    rays.RayGenerationShaderRecord.SizeInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    rays.MissShaderTable.StartAddress = ID3D12Resource_GetGPUVirtualAddress(sbt) + 1 * D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    rays.MissShaderTable.SizeInBytes = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

    ID3D12GraphicsCommandList4_DispatchRays(context.list4, &rays);
    transition_resource_state(context.context.list, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(output, DXGI_FORMAT_UNKNOWN, &rb, context.context.queue, context.context.list);

    ok(get_readback_uint(&rb, 0, 0, 0) == 1100, "Expected 1100, got %u\n", get_readback_uint(&rb, 0, 0, 0));
    ok(get_readback_uint(&rb, 1, 0, 0) == 2200, "Expected 2200, got %u\n", get_readback_uint(&rb, 1, 0, 0));

    release_resource_readback(&rb);
    ID3D12Resource_Release(sbt);
    ID3D12Resource_Release(output);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12StateObject_Release(rtpso);
    destroy_raytracing_test_context(&context);
}
