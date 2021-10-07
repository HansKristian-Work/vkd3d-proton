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

static bool init_raytracing_test_context(struct raytracing_test_context *context)
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
                opts5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
        {
            skip("Raytracing tier 1.0 is not supported on this device. Skipping RT test.\n");
            ID3D12Device5_Release(context->device5);
            ID3D12GraphicsCommandList4_Release(context->list4);
            destroy_test_context(&context->context);
            return false;
        }
    }

    return true;
}

static D3D12_SHADER_BYTECODE get_default_rt_lib(void)
{
    /* Compile with -Tlib_6_3 in DXC. */
    static const BYTE rt_lib_dxil[] =
    {
#if 0
        RaytracingAccelerationStructure AS : register(t0);
        StructuredBuffer<float2> RayPositions : register(t1);
        RWStructuredBuffer<float2> Buf : register(u0);

        struct RayPayload
        {
                float2 color;
        };

        cbuffer LocalConstants : register(b0, space1)
        {
                float local_value0;
        };

        cbuffer LocalConstants2 : register(b1, space1)
        {
                float local_value1;
        };

        [shader("miss")]
        void RayMiss(inout RayPayload payload)
        {
                payload.color.x = local_value0;
                payload.color.y = local_value1;
        }

        [shader("closesthit")]
        void RayClosest(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attribs)
        {
                payload.color.x = local_value0;
                payload.color.y = local_value1;
        }

        [shader("raygeneration")]
        void RayGen()
        {
                RayPayload payload;
                payload.color = float2(0.0, 0.0);

                uint index = DispatchRaysIndex().x;

                RayDesc ray;
                ray.Origin = float3(RayPositions[index], 1.0);
                ray.Direction = float3(0.0, 0.0, -1.0);
                ray.TMin = 0;
                ray.TMax = 10;

                TraceRay(AS, RAY_FLAG_NONE,
                        0x01, // mask
                        0, // HitGroup offset
                        1, // geometry contribution multiplier
                        0, // miss shader index
                        ray, payload);

                Buf[index] = payload.color;
        }
#endif
        0x44, 0x58, 0x42, 0x43, 0x65, 0xc2, 0x0c, 0xbe, 0xc8, 0x0e, 0x31, 0x4f, 0x2a, 0x6b, 0xc2, 0x17, 0x84, 0x95, 0x44, 0x43, 0x01, 0x00, 0x00, 0x00, 0x08, 0x10, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        0x2c, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x94, 0x02, 0x00, 0x00, 0x53, 0x46, 0x49, 0x30, 0x08, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x52, 0x44, 0x41, 0x54,
        0x50, 0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0xe8, 0x00, 0x00, 0x00, 0x98, 0x01, 0x00, 0x00, 0x2c, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0xc8, 0x00, 0x00, 0x00, 0x00, 0x4c, 0x6f, 0x63, 0x61, 0x6c, 0x43, 0x6f, 0x6e, 0x73, 0x74, 0x61, 0x6e, 0x74, 0x73, 0x00, 0x4c, 0x6f, 0x63, 0x61, 0x6c, 0x43, 0x6f, 0x6e, 0x73, 0x74, 0x61, 0x6e,
        0x74, 0x73, 0x32, 0x00, 0x41, 0x53, 0x00, 0x52, 0x61, 0x79, 0x50, 0x6f, 0x73, 0x69, 0x74, 0x69, 0x6f, 0x6e, 0x73, 0x00, 0x42, 0x75, 0x66, 0x00, 0x01, 0x3f, 0x52, 0x61, 0x79, 0x4d, 0x69, 0x73,
        0x73, 0x40, 0x40, 0x59, 0x41, 0x58, 0x55, 0x52, 0x61, 0x79, 0x50, 0x61, 0x79, 0x6c, 0x6f, 0x61, 0x64, 0x40, 0x40, 0x40, 0x5a, 0x00, 0x52, 0x61, 0x79, 0x4d, 0x69, 0x73, 0x73, 0x00, 0x01, 0x3f,
        0x52, 0x61, 0x79, 0x43, 0x6c, 0x6f, 0x73, 0x65, 0x73, 0x74, 0x40, 0x40, 0x59, 0x41, 0x58, 0x55, 0x52, 0x61, 0x79, 0x50, 0x61, 0x79, 0x6c, 0x6f, 0x61, 0x64, 0x40, 0x40, 0x55, 0x42, 0x75, 0x69,
        0x6c, 0x74, 0x49, 0x6e, 0x54, 0x72, 0x69, 0x61, 0x6e, 0x67, 0x6c, 0x65, 0x49, 0x6e, 0x74, 0x65, 0x72, 0x73, 0x65, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x41, 0x74, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74,
        0x65, 0x73, 0x40, 0x40, 0x40, 0x5a, 0x00, 0x52, 0x61, 0x79, 0x43, 0x6c, 0x6f, 0x73, 0x65, 0x73, 0x74, 0x00, 0x01, 0x3f, 0x52, 0x61, 0x79, 0x47, 0x65, 0x6e, 0x40, 0x40, 0x59, 0x41, 0x58, 0x58,
        0x5a, 0x00, 0x52, 0x61, 0x79, 0x47, 0x65, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xa8, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x0d, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x8c, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x0b, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x60, 0x00, 0x0b, 0x00, 0x5a, 0x00, 0x00, 0x00, 0xa3, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x0a, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
        0x60, 0x00, 0x0a, 0x00, 0xae, 0x00, 0x00, 0x00, 0xbe, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x63, 0x00, 0x07, 0x00, 0x02, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x6c, 0x0d, 0x00, 0x00, 0x63, 0x00, 0x06, 0x00,
        0x5b, 0x03, 0x00, 0x00, 0x44, 0x58, 0x49, 0x4c, 0x03, 0x01, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x54, 0x0d, 0x00, 0x00, 0x42, 0x43, 0xc0, 0xde, 0x21, 0x0c, 0x00, 0x00, 0x52, 0x03, 0x00, 0x00,
        0x0b, 0x82, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x07, 0x81, 0x23, 0x91, 0x41, 0xc8, 0x04, 0x49, 0x06, 0x10, 0x32, 0x39, 0x92, 0x01, 0x84, 0x0c, 0x25, 0x05, 0x08, 0x19,
        0x1e, 0x04, 0x8b, 0x62, 0x80, 0x18, 0x45, 0x02, 0x42, 0x92, 0x0b, 0x42, 0xc4, 0x10, 0x32, 0x14, 0x38, 0x08, 0x18, 0x4b, 0x0a, 0x32, 0x62, 0x88, 0x48, 0x90, 0x14, 0x20, 0x43, 0x46, 0x88, 0xa5,
        0x00, 0x19, 0x32, 0x42, 0xe4, 0x48, 0x0e, 0x90, 0x11, 0x23, 0xc4, 0x50, 0x41, 0x51, 0x81, 0x8c, 0xe1, 0x83, 0xe5, 0x8a, 0x04, 0x31, 0x46, 0x06, 0x51, 0x18, 0x00, 0x00, 0x5d, 0x00, 0x00, 0x00,
        0x1b, 0x8c, 0x20, 0x00, 0x12, 0x60, 0xd9, 0x00, 0x1e, 0xc2, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x80, 0x44, 0x90, 0x43, 0x3a, 0xcc, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0x43, 0x1b, 0xd0, 0x43, 0x38,
        0xa4, 0x03, 0x3b, 0xb4, 0xc1, 0x38, 0x84, 0x03, 0x3b, 0xb0, 0xc3, 0x3c, 0x00, 0xe6, 0x10, 0x0e, 0xec, 0x30, 0x0f, 0xe5, 0x00, 0x10, 0xec, 0x50, 0x0e, 0xf3, 0x30, 0x0f, 0x6d, 0x00, 0x0f, 0xf2,
        0x50, 0x0e, 0xe3, 0x90, 0x0e, 0xf3, 0x50, 0x0e, 0x6d, 0x60, 0x0e, 0xf0, 0xd0, 0x0e, 0xe1, 0x40, 0x0e, 0x80, 0x39, 0x84, 0x03, 0x3b, 0xcc, 0x43, 0x39, 0x00, 0x84, 0x3b, 0xbc, 0x43, 0x1b, 0x98,
        0x83, 0x3c, 0x84, 0x43, 0x3b, 0x94, 0x43, 0x1b, 0xc0, 0xc3, 0x3b, 0xa4, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xc8, 0x43, 0x1b, 0x94, 0x03, 0x3b, 0xa4, 0x43, 0x3b, 0x00, 0xe6, 0x10, 0x0e, 0xec, 0x30,
        0x0f, 0xe5, 0x00, 0x10, 0xee, 0xf0, 0x0e, 0x6d, 0x90, 0x0e, 0xee, 0x60, 0x0e, 0xf3, 0xd0, 0x06, 0xe6, 0x00, 0x0f, 0x6d, 0xd0, 0x0e, 0xe1, 0x40, 0x0f, 0xe8, 0x00, 0x98, 0x43, 0x38, 0xb0, 0xc3,
        0x3c, 0x94, 0x03, 0x40, 0xb8, 0xc3, 0x3b, 0xb4, 0x81, 0x3b, 0x84, 0x83, 0x3b, 0xcc, 0x43, 0x1b, 0x98, 0x03, 0x3c, 0xb4, 0x41, 0x3b, 0x84, 0x03, 0x3d, 0xa0, 0x03, 0x60, 0x0e, 0xe1, 0xc0, 0x0e,
        0xf3, 0x50, 0x0e, 0xc0, 0xe0, 0x0e, 0xef, 0xd0, 0x06, 0xf2, 0x50, 0x0e, 0xe1, 0xc0, 0x0e, 0xe9, 0x70, 0x0e, 0xee, 0xd0, 0x06, 0xf3, 0x40, 0x0f, 0xe1, 0x30, 0x0e, 0xeb, 0x00, 0x10, 0xf3, 0x40,
        0x0f, 0xe1, 0x30, 0x0e, 0xeb, 0xd0, 0x06, 0xf0, 0x20, 0x0f, 0xef, 0x40, 0x0f, 0xe5, 0x30, 0x0e, 0xf4, 0xf0, 0x0e, 0xf2, 0xd0, 0x06, 0xe2, 0x50, 0x0f, 0xe6, 0x60, 0x0e, 0xe5, 0x20, 0x0f, 0x6d,
        0x30, 0x0f, 0xe9, 0xa0, 0x0f, 0xe5, 0x00, 0xc0, 0x01, 0x40, 0xd4, 0x83, 0x3b, 0xcc, 0x43, 0x38, 0x98, 0x43, 0x39, 0xb4, 0x81, 0x39, 0xc0, 0x43, 0x1b, 0xb4, 0x43, 0x38, 0xd0, 0x03, 0x3a, 0x00,
        0xe6, 0x10, 0x0e, 0xec, 0x30, 0x0f, 0xe5, 0x00, 0x10, 0xf5, 0x30, 0x0f, 0xe5, 0xd0, 0x06, 0xf3, 0xf0, 0x0e, 0xe6, 0x40, 0x0f, 0x6d, 0x60, 0x0e, 0xec, 0xf0, 0x0e, 0xe1, 0x40, 0x0f, 0x80, 0x39,
        0x84, 0x03, 0x3b, 0xcc, 0x43, 0x39, 0x00, 0x1b, 0x8c, 0x41, 0x00, 0x16, 0x80, 0xda, 0x60, 0x10, 0xff, 0xff, 0xff, 0xff, 0x3f, 0x00, 0x12, 0x50, 0x6d, 0x30, 0x8a, 0xff, 0xff, 0xff, 0xff, 0x1f,
        0x00, 0x09, 0xa0, 0x36, 0x10, 0xc6, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x80, 0xb4, 0x81, 0x38, 0x20, 0xe0, 0x0c, 0x00, 0x00, 0x49, 0x18, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x13, 0x84, 0x40, 0x98,
        0x30, 0x04, 0x83, 0x30, 0x21, 0x10, 0x26, 0x04, 0xc4, 0x84, 0xa0, 0x98, 0x10, 0x18, 0x13, 0x82, 0x03, 0x00, 0x00, 0x00, 0x89, 0x20, 0x00, 0x00, 0x72, 0x00, 0x00, 0x00, 0x32, 0x22, 0x88, 0x09,
        0x20, 0x64, 0x85, 0x04, 0x13, 0x23, 0xa4, 0x84, 0x04, 0x13, 0x23, 0xe3, 0x84, 0xa1, 0x90, 0x14, 0x12, 0x4c, 0x8c, 0x8c, 0x0b, 0x84, 0xc4, 0x4c, 0x10, 0xd8, 0xc1, 0x1c, 0x01, 0x18, 0x9c, 0x19,
        0x48, 0x53, 0x44, 0x09, 0x93, 0xbf, 0x02, 0xd8, 0x14, 0x01, 0x02, 0xd2, 0x18, 0x9a, 0x20, 0x10, 0x0b, 0x11, 0x01, 0x13, 0xe2, 0x34, 0xec, 0x14, 0x51, 0xc2, 0x44, 0x45, 0x04, 0x0a, 0x00, 0x0a,
        0x66, 0x00, 0x86, 0x11, 0x84, 0x61, 0xa6, 0x34, 0x18, 0x07, 0x76, 0x08, 0x87, 0x79, 0x98, 0x07, 0x37, 0x98, 0x05, 0x7a, 0x90, 0x87, 0x7a, 0x18, 0x07, 0x7a, 0xa8, 0x07, 0x79, 0x28, 0x07, 0x72,
        0x10, 0x85, 0x7a, 0x30, 0x07, 0x73, 0x28, 0x07, 0x79, 0xe0, 0x03, 0x7b, 0x28, 0x87, 0x71, 0xa0, 0x87, 0x77, 0x90, 0x07, 0x3e, 0x30, 0x07, 0x76, 0x78, 0x87, 0x70, 0xa0, 0x07, 0x36, 0x00, 0x03,
        0x39, 0xf0, 0x03, 0x30, 0xf0, 0x03, 0x14, 0x10, 0x54, 0xcc, 0xb4, 0x06, 0xe3, 0xc0, 0x0e, 0xe1, 0x30, 0x0f, 0xf3, 0xe0, 0x06, 0xb2, 0x70, 0x0b, 0xb3, 0x40, 0x0f, 0xf2, 0x50, 0x0f, 0xe3, 0x40,
        0x0f, 0xf5, 0x20, 0x0f, 0xe5, 0x40, 0x0e, 0xa2, 0x50, 0x0f, 0xe6, 0x60, 0x0e, 0xe5, 0x20, 0x0f, 0x7c, 0x60, 0x0f, 0xe5, 0x30, 0x0e, 0xf4, 0xf0, 0x0e, 0xf2, 0xc0, 0x07, 0xe6, 0xc0, 0x0e, 0xef,
        0x10, 0x0e, 0xf4, 0xc0, 0x06, 0x60, 0x20, 0x07, 0x7e, 0x00, 0x06, 0x7e, 0x80, 0x02, 0x82, 0x8e, 0x73, 0x4a, 0x47, 0x00, 0x16, 0xce, 0x69, 0xa4, 0x09, 0x68, 0x26, 0x09, 0x05, 0x03, 0x25, 0xf7,
        0x94, 0x8e, 0x00, 0x2c, 0x9c, 0xd3, 0x48, 0x13, 0xd0, 0x4c, 0x92, 0x8d, 0x82, 0x81, 0x96, 0x11, 0x80, 0x8b, 0xa4, 0x29, 0xa2, 0x84, 0xc9, 0x5f, 0x01, 0x2c, 0x05, 0xb0, 0xc5, 0x01, 0x06, 0x14,
        0x10, 0xe4, 0x14, 0xa1, 0x79, 0x08, 0x3a, 0x36, 0x90, 0xa6, 0x88, 0x12, 0x26, 0x7f, 0xa3, 0x90, 0x65, 0x12, 0x9b, 0x36, 0x42, 0x80, 0xc6, 0x58, 0x08, 0xb1, 0x99, 0x88, 0x48, 0x22, 0x84, 0x09,
        0x71, 0x1a, 0x6d, 0x9a, 0x22, 0x24, 0xa0, 0x26, 0x42, 0x42, 0x01, 0x41, 0x52, 0x19, 0x9a, 0x67, 0x22, 0xaa, 0x04, 0x0d, 0x59, 0x47, 0x0d, 0x97, 0x3f, 0x61, 0x0f, 0x21, 0xf9, 0xdc, 0x46, 0x15,
        0x2b, 0x31, 0xf9, 0xc5, 0x6d, 0x23, 0x62, 0x18, 0x86, 0x61, 0x8e, 0x00, 0xa1, 0xec, 0x9e, 0xe1, 0xf2, 0x27, 0xec, 0x21, 0x24, 0x3f, 0x04, 0x9a, 0x61, 0x21, 0x50, 0xa0, 0x15, 0x02, 0x03, 0x36,
        0x80, 0xb8, 0x32, 0x00, 0x40, 0x46, 0x5e, 0x59, 0x1a, 0x60, 0x03, 0x80, 0x61, 0x18, 0x86, 0x0c, 0x20, 0xf0, 0xa6, 0xe1, 0xf2, 0x27, 0xec, 0x21, 0x24, 0x7f, 0x25, 0xa4, 0x95, 0x98, 0xfc, 0xe2,
        0xb6, 0x51, 0x31, 0x0c, 0xc3, 0x00, 0x94, 0x43, 0x04, 0x36, 0x00, 0xc8, 0x00, 0x1a, 0x4b, 0xd4, 0x00, 0x1b, 0x00, 0x00, 0x00, 0x30, 0x0c, 0xc3, 0x30, 0x0c, 0xc3, 0xf0, 0x50, 0x59, 0x86, 0x0d,
        0x48, 0xe8, 0x2c, 0xc3, 0x06, 0x2c, 0x94, 0x96, 0x61, 0x03, 0x0a, 0x5a, 0xcb, 0xb0, 0x01, 0x01, 0xb5, 0x65, 0xd8, 0x80, 0x83, 0xde, 0x81, 0x80, 0x39, 0x82, 0x60, 0x8e, 0x00, 0x14, 0x68, 0x20,
        0x02, 0x00, 0x00, 0x00, 0x13, 0x14, 0x72, 0xc0, 0x87, 0x74, 0x60, 0x87, 0x36, 0x68, 0x87, 0x79, 0x68, 0x03, 0x72, 0xc0, 0x87, 0x0d, 0xaf, 0x50, 0x0e, 0x6d, 0xd0, 0x0e, 0x7a, 0x50, 0x0e, 0x6d,
        0x00, 0x0f, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x71, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x78, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e,
        0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe9, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x90, 0x0e, 0x76, 0x40, 0x07, 0x7a, 0x60, 0x07, 0x74, 0xd0, 0x06, 0xe6, 0x10,
        0x07, 0x76, 0xa0, 0x07, 0x73, 0x20, 0x07, 0x6d, 0x60, 0x0e, 0x73, 0x20, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xd0, 0x06, 0xe6, 0x60, 0x07, 0x74, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x6d, 0xe0, 0x0e, 0x78,
        0xa0, 0x07, 0x71, 0x60, 0x07, 0x7a, 0x30, 0x07, 0x72, 0xa0, 0x07, 0x76, 0x40, 0x07, 0x3a, 0x0f, 0x84, 0x90, 0x21, 0x23, 0x45, 0x44, 0x00, 0xc6, 0x00, 0x80, 0x59, 0x03, 0x00, 0xe6, 0x0d, 0x00,
        0x98, 0x39, 0x00, 0x00, 0xee, 0x00, 0x00, 0x86, 0x3c, 0x10, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x79, 0x28, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x18, 0xf2, 0x58, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0xe4, 0xe1, 0x80, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xc8, 0xe3, 0x01, 0x01,
        0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x90, 0x07, 0x0c, 0x80, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0xc8, 0x33, 0x06, 0x40, 0x00, 0x10, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x30, 0xe4, 0x29, 0x03, 0x20, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xf2, 0x9c, 0x01, 0x10, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0c, 0x79, 0xd2, 0x00, 0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86, 0x3c, 0x6b, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x43, 0x9e, 0x36, 0x00,
        0x02, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x21, 0xcf, 0x1b, 0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x16, 0x08, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
        0x32, 0x1e, 0x98, 0x18, 0x19, 0x11, 0x4c, 0x90, 0x8c, 0x09, 0x26, 0x47, 0xc6, 0x04, 0x43, 0x02, 0x4a, 0xa0, 0x0c, 0x0a, 0xa1, 0x18, 0x46, 0x00, 0x0a, 0xa4, 0x30, 0x0a, 0xa2, 0x08, 0x8a, 0xa2,
        0x1c, 0x4a, 0xa1, 0x2c, 0x48, 0x1e, 0x01, 0xa0, 0xb9, 0x40, 0x01, 0x11, 0x00, 0x00, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x95, 0x00, 0x00, 0x00, 0x1a, 0x03, 0x4c, 0x90, 0x46, 0x02, 0x13, 0xc4,
        0x88, 0x0c, 0x6f, 0xec, 0xed, 0x4d, 0x0c, 0x44, 0x06, 0x26, 0x26, 0xc7, 0x05, 0xa6, 0xc6, 0x05, 0x06, 0x66, 0x43, 0x10, 0x4c, 0x10, 0x00, 0x69, 0x82, 0x00, 0x4c, 0x1b, 0x84, 0x81, 0x98, 0x20,
        0x00, 0xd4, 0x06, 0x61, 0x30, 0x38, 0xb0, 0xa5, 0x89, 0x4d, 0x10, 0x80, 0x6a, 0xc3, 0x80, 0x24, 0xc4, 0x04, 0x01, 0xb0, 0x26, 0x08, 0x02, 0x40, 0x21, 0x68, 0x6a, 0x82, 0x00, 0x5c, 0x1b, 0x84,
        0xc5, 0xd8, 0x90, 0x2c, 0x4c, 0xb3, 0x2c, 0x83, 0xb3, 0x3c, 0x13, 0x04, 0x23, 0x20, 0x23, 0x15, 0x96, 0x07, 0xf5, 0x36, 0x97, 0x46, 0x97, 0xf6, 0xe6, 0x36, 0x37, 0x41, 0x00, 0xb0, 0x09, 0x02,
        0x90, 0x6d, 0x10, 0x06, 0x6a, 0x43, 0x32, 0x44, 0xd2, 0x32, 0x0c, 0xd3, 0x52, 0x6d, 0x10, 0x20, 0x6b, 0x82, 0x80, 0x08, 0x1c, 0x84, 0xea, 0xcc, 0x26, 0x08, 0x72, 0xf0, 0x6d, 0x58, 0x16, 0x2c,
        0x5b, 0x96, 0x61, 0xd2, 0x34, 0xad, 0xda, 0x10, 0x6c, 0x13, 0x04, 0x65, 0xa0, 0xc3, 0xf4, 0x36, 0x16, 0xc6, 0x36, 0xf4, 0xe6, 0x36, 0x47, 0x17, 0xe6, 0x46, 0x37, 0xb7, 0x01, 0x59, 0x3a, 0x6f,
        0x58, 0x06, 0x03, 0x98, 0x20, 0x30, 0x04, 0x1f, 0xa6, 0xb7, 0xb1, 0x30, 0xb6, 0xa1, 0x37, 0xb7, 0x39, 0xba, 0x30, 0x37, 0xba, 0x39, 0x99, 0x0d, 0xc8, 0x00, 0x06, 0x61, 0x30, 0x0c, 0x83, 0x01,
        0x6c, 0x10, 0x3e, 0x31, 0xd8, 0x40, 0x5c, 0xdc, 0x18, 0x00, 0x13, 0x84, 0xa8, 0xd8, 0x00, 0x6c, 0x18, 0x06, 0x33, 0x30, 0x83, 0x09, 0x02, 0xa0, 0x6d, 0x18, 0xd0, 0xc0, 0x0c, 0xcc, 0x60, 0x83,
        0x70, 0x06, 0x69, 0x30, 0x41, 0xa8, 0x8c, 0x0d, 0xc3, 0x62, 0x06, 0x66, 0xb0, 0x61, 0x38, 0x83, 0x34, 0x60, 0x83, 0x09, 0xc2, 0x75, 0x6c, 0x08, 0xce, 0x60, 0xc3, 0x31, 0x94, 0x81, 0x1a, 0xac,
        0x41, 0x1b, 0xb8, 0xc1, 0x1b, 0x10, 0x98, 0x20, 0xcc, 0x01, 0x18, 0x6c, 0x10, 0x16, 0x39, 0xd8, 0x50, 0x00, 0x71, 0x00, 0x90, 0xc1, 0x1c, 0x10, 0x15, 0x02, 0x7e, 0xa4, 0xc2, 0xf2, 0x86, 0xd8,
        0xde, 0xe6, 0xca, 0xe6, 0xe8, 0x80, 0x80, 0xb2, 0x82, 0xb0, 0xaa, 0xa4, 0xc2, 0xf2, 0xa0, 0xc2, 0xf2, 0xd8, 0xde, 0xc2, 0xc8, 0x80, 0x80, 0xaa, 0x84, 0xea, 0xd2, 0xd8, 0xe8, 0x92, 0xdc, 0xa8,
        0xe4, 0xd2, 0xc2, 0xdc, 0xce, 0xd8, 0xca, 0x92, 0xdc, 0xe8, 0xca, 0xe4, 0xe6, 0xca, 0xc6, 0xe8, 0xd2, 0xde, 0xdc, 0x82, 0xe8, 0xe8, 0xe4, 0xd2, 0xc4, 0xea, 0xe8, 0xca, 0xe6, 0x80, 0x80, 0x80,
        0xb4, 0x26, 0x08, 0xc0, 0x36, 0x41, 0x00, 0xb8, 0x09, 0x02, 0xd0, 0x6d, 0x08, 0x96, 0x0d, 0x08, 0x65, 0x07, 0x09, 0x75, 0x07, 0x14, 0x1e, 0xe4, 0xc1, 0x86, 0x62, 0x0d, 0xea, 0x00, 0x00, 0xf4,
        0x80, 0x4f, 0xc0, 0x8f, 0x54, 0x58, 0xde, 0x51, 0x99, 0x1b, 0x10, 0x50, 0x56, 0x10, 0x16, 0x96, 0xd6, 0x06, 0x82, 0xba, 0x03, 0x3c, 0xc8, 0x83, 0x0d, 0x85, 0x1b, 0xf0, 0x01, 0x00, 0xf4, 0x01,
        0xbb, 0x80, 0x1f, 0xa9, 0xb0, 0xbc, 0xa6, 0xb4, 0xb9, 0x39, 0x20, 0xa0, 0xac, 0x20, 0xac, 0x2a, 0xa9, 0xb0, 0x3c, 0xa8, 0xb0, 0x3c, 0xb6, 0xb7, 0x30, 0x32, 0x20, 0x20, 0x20, 0xad, 0x09, 0x02,
        0xe0, 0x6d, 0x30, 0x28, 0x50, 0x48, 0x28, 0x3c, 0xc8, 0x83, 0x0d, 0x45, 0x19, 0xfc, 0x01, 0x00, 0x84, 0x42, 0x15, 0x36, 0x36, 0xbb, 0x36, 0x97, 0x34, 0xb2, 0x32, 0x37, 0xba, 0x29, 0x41, 0x50,
        0x85, 0x0c, 0xcf, 0xc5, 0xae, 0x4c, 0x6e, 0x2e, 0xed, 0xcd, 0x6d, 0x4a, 0x40, 0x34, 0x21, 0xc3, 0x73, 0xb1, 0x0b, 0x63, 0xb3, 0x2b, 0x93, 0x9b, 0x12, 0x18, 0x75, 0xc8, 0xf0, 0x5c, 0xe6, 0xd0,
        0xc2, 0xc8, 0xca, 0xe4, 0x9a, 0xde, 0xc8, 0xca, 0xd8, 0xa6, 0x04, 0x49, 0x19, 0x32, 0x3c, 0x17, 0xb9, 0xb2, 0xb9, 0xb7, 0x3a, 0xb9, 0xb1, 0xb2, 0xb9, 0x29, 0xc1, 0x18, 0x54, 0x22, 0xc3, 0x73,
        0xa1, 0xcb, 0x83, 0x2b, 0x0b, 0x72, 0x73, 0x7b, 0xa3, 0x0b, 0xa3, 0x4b, 0x7b, 0x73, 0x9b, 0x9b, 0x12, 0xbc, 0x41, 0x1d, 0x32, 0x3c, 0x97, 0x32, 0x37, 0x3a, 0xb9, 0x3c, 0xa8, 0xb7, 0x34, 0x37,
        0xba, 0xb9, 0x29, 0xc4, 0x1c, 0xe8, 0x41, 0x1f, 0x84, 0x02, 0x00, 0x00, 0x79, 0x18, 0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x33, 0x08, 0x80, 0x1c, 0xc4, 0xe1, 0x1c, 0x66, 0x14, 0x01, 0x3d, 0x88,
        0x43, 0x38, 0x84, 0xc3, 0x8c, 0x42, 0x80, 0x07, 0x79, 0x78, 0x07, 0x73, 0x98, 0x71, 0x0c, 0xe6, 0x00, 0x0f, 0xed, 0x10, 0x0e, 0xf4, 0x80, 0x0e, 0x33, 0x0c, 0x42, 0x1e, 0xc2, 0xc1, 0x1d, 0xce,
        0xa1, 0x1c, 0x66, 0x30, 0x05, 0x3d, 0x88, 0x43, 0x38, 0x84, 0x83, 0x1b, 0xcc, 0x03, 0x3d, 0xc8, 0x43, 0x3d, 0x8c, 0x03, 0x3d, 0xcc, 0x78, 0x8c, 0x74, 0x70, 0x07, 0x7b, 0x08, 0x07, 0x79, 0x48,
        0x87, 0x70, 0x70, 0x07, 0x7a, 0x70, 0x03, 0x76, 0x78, 0x87, 0x70, 0x20, 0x87, 0x19, 0xcc, 0x11, 0x0e, 0xec, 0x90, 0x0e, 0xe1, 0x30, 0x0f, 0x6e, 0x30, 0x0f, 0xe3, 0xf0, 0x0e, 0xf0, 0x50, 0x0e,
        0x33, 0x10, 0xc4, 0x1d, 0xde, 0x21, 0x1c, 0xd8, 0x21, 0x1d, 0xc2, 0x61, 0x1e, 0x66, 0x30, 0x89, 0x3b, 0xbc, 0x83, 0x3b, 0xd0, 0x43, 0x39, 0xb4, 0x03, 0x3c, 0xbc, 0x83, 0x3c, 0x84, 0x03, 0x3b,
        0xcc, 0xf0, 0x14, 0x76, 0x60, 0x07, 0x7b, 0x68, 0x07, 0x37, 0x68, 0x87, 0x72, 0x68, 0x07, 0x37, 0x80, 0x87, 0x70, 0x90, 0x87, 0x70, 0x60, 0x07, 0x76, 0x28, 0x07, 0x76, 0xf8, 0x05, 0x76, 0x78,
        0x87, 0x77, 0x80, 0x87, 0x5f, 0x08, 0x87, 0x71, 0x18, 0x87, 0x72, 0x98, 0x87, 0x79, 0x98, 0x81, 0x2c, 0xee, 0xf0, 0x0e, 0xee, 0xe0, 0x0e, 0xf5, 0xc0, 0x0e, 0xec, 0x30, 0x03, 0x62, 0xc8, 0xa1,
        0x1c, 0xe4, 0xa1, 0x1c, 0xcc, 0xa1, 0x1c, 0xe4, 0xa1, 0x1c, 0xdc, 0x61, 0x1c, 0xca, 0x21, 0x1c, 0xc4, 0x81, 0x1d, 0xca, 0x61, 0x06, 0xd6, 0x90, 0x43, 0x39, 0xc8, 0x43, 0x39, 0x98, 0x43, 0x39,
        0xc8, 0x43, 0x39, 0xb8, 0xc3, 0x38, 0x94, 0x43, 0x38, 0x88, 0x03, 0x3b, 0x94, 0xc3, 0x2f, 0xbc, 0x83, 0x3c, 0xfc, 0x82, 0x3b, 0xd4, 0x03, 0x3b, 0xb0, 0xc3, 0x8c, 0xcc, 0x21, 0x07, 0x7c, 0x70,
        0x03, 0x74, 0x60, 0x07, 0x37, 0x90, 0x87, 0x72, 0x98, 0x87, 0x77, 0xa8, 0x07, 0x79, 0x18, 0x87, 0x72, 0x70, 0x83, 0x70, 0xa0, 0x07, 0x7a, 0x90, 0x87, 0x74, 0x10, 0x87, 0x7a, 0xa0, 0x87, 0x72,
        0x00, 0x00, 0x00, 0x00, 0x71, 0x20, 0x00, 0x00, 0x9c, 0x00, 0x00, 0x00, 0x05, 0xa0, 0x06, 0x81, 0x5f, 0x70, 0x0a, 0x04, 0xce, 0xaa, 0xd2, 0x70, 0x9e, 0x2e, 0x0f, 0x8f, 0xd3, 0xee, 0x73, 0x70,
        0x3c, 0x2e, 0xb3, 0xcb, 0xf2, 0x30, 0x3d, 0xfd, 0x76, 0x4f, 0xe9, 0xf2, 0xfa, 0x98, 0x5e, 0x97, 0x97, 0x81, 0xc0, 0x60, 0x09, 0xc4, 0x41, 0xe0, 0x27, 0xac, 0x9b, 0x81, 0xc0, 0x99, 0xf5, 0x47,
        0x92, 0x5e, 0xa7, 0x74, 0x79, 0x7d, 0x4c, 0xaf, 0xcb, 0xcb, 0x64, 0x61, 0xdd, 0x6c, 0x2e, 0xcb, 0x81, 0xd6, 0x1f, 0xc9, 0x5e, 0x1e, 0xd3, 0xdf, 0x72, 0x60, 0x93, 0x04, 0x8b, 0x01, 0x81, 0x40,
        0x60, 0xb0, 0x0c, 0x50, 0x21, 0xf0, 0x93, 0x86, 0xf3, 0x43, 0xf6, 0x7b, 0x5e, 0x9e, 0xd3, 0x81, 0xc0, 0x6c, 0x10, 0x5b, 0x95, 0x86, 0xf3, 0xd0, 0x70, 0x9e, 0xfd, 0x0e, 0x93, 0x81, 0xc0, 0xaa,
        0xb0, 0x9e, 0x66, 0xd3, 0x93, 0x6e, 0xaa, 0x3c, 0x1d, 0x76, 0x9f, 0xd9, 0xe5, 0xa4, 0x9b, 0x5e, 0x96, 0xcf, 0xcb, 0x63, 0x7a, 0xfa, 0xed, 0x0e, 0xd2, 0xe9, 0xf2, 0xb4, 0xb8, 0x4e, 0x2f, 0xcf,
        0x81, 0x40, 0xa0, 0xb6, 0x0e, 0x9e, 0xc0, 0x4f, 0x1a, 0xce, 0x1f, 0xcb, 0x6e, 0x20, 0x30, 0x1b, 0xc4, 0x62, 0xb5, 0x55, 0xd0, 0x05, 0x7e, 0xd2, 0x70, 0xbe, 0x99, 0x9e, 0xcf, 0x81, 0xc0, 0x6c,
        0x10, 0x5b, 0x95, 0x86, 0xf3, 0xd0, 0x70, 0x9e, 0xfd, 0x0e, 0x93, 0x81, 0x40, 0xa0, 0xb6, 0x02, 0xf0, 0x20, 0xf0, 0x93, 0x86, 0xf3, 0xd0, 0xf7, 0x3c, 0x4d, 0x4f, 0xbf, 0xdd, 0x73, 0x20, 0x70,
        0x66, 0xfd, 0x91, 0xa6, 0x74, 0x79, 0x7d, 0x4c, 0xaf, 0xcb, 0xcb, 0x64, 0x61, 0xdd, 0x6c, 0x2e, 0xcb, 0x81, 0xd6, 0x1f, 0xc9, 0x5e, 0x1e, 0xd3, 0xdf, 0x72, 0x60, 0x93, 0x04, 0x8b, 0x01, 0x81,
        0x40, 0x60, 0xd0, 0x06, 0x9c, 0xd2, 0x11, 0x80, 0x85, 0x73, 0x1a, 0x69, 0x02, 0x9a, 0x49, 0x32, 0x82, 0xa7, 0x74, 0x04, 0x60, 0xe1, 0x9c, 0x46, 0x9a, 0x80, 0x66, 0x92, 0x6c, 0x43, 0xd8, 0x86,
        0xcb, 0x77, 0x1e, 0x5f, 0x08, 0xa8, 0xa2, 0x20, 0xa2, 0xd2, 0x01, 0x86, 0x92, 0x30, 0x00, 0x01, 0xf3, 0x8b, 0xdb, 0xb6, 0x86, 0x33, 0x18, 0x2e, 0xdf, 0x79, 0x7c, 0x21, 0x22, 0x80, 0x89, 0x08,
        0x81, 0x66, 0x58, 0x88, 0xcf, 0x89, 0x4a, 0x24, 0xf0, 0x4b, 0x47, 0x00, 0x16, 0xce, 0x69, 0xa4, 0x09, 0x68, 0x26, 0xc9, 0x1c, 0xd0, 0x60, 0xb8, 0x7c, 0xe7, 0xf1, 0x85, 0x88, 0x00, 0x26, 0x22,
        0x04, 0x9a, 0x61, 0x21, 0x3e, 0x27, 0x2a, 0x91, 0xc0, 0x2f, 0x1d, 0x01, 0x58, 0x38, 0xa7, 0x91, 0x26, 0xa0, 0x99, 0x24, 0xbb, 0x22, 0x48, 0x81, 0x8c, 0x77, 0xbd, 0xe1, 0xae, 0xb1, 0xbc, 0x1c,
        0xa6, 0x97, 0x91, 0x61, 0x37, 0x99, 0x5d, 0x36, 0xbe, 0xe5, 0xcc, 0xb4, 0xd8, 0x35, 0x66, 0x87, 0xe7, 0x73, 0x97, 0xf4, 0x3a, 0xa5, 0xcb, 0xeb, 0x63, 0x7a, 0x5d, 0x5e, 0x26, 0x0b, 0xeb, 0x66,
        0x73, 0x59, 0xce, 0xb3, 0x97, 0xc7, 0xf4, 0xb7, 0x9c, 0x67, 0x66, 0xbf, 0xc3, 0x74, 0x16, 0x48, 0xe6, 0x03, 0xf9, 0xea, 0x41, 0x14, 0xc8, 0x78, 0xd7, 0x1b, 0xee, 0x1a, 0xcb, 0xcb, 0x61, 0x7a,
        0x19, 0x19, 0x76, 0x93, 0xd9, 0x65, 0xe3, 0x5b, 0xce, 0x4c, 0x8b, 0x5d, 0x63, 0x76, 0x78, 0x3e, 0x77, 0x4d, 0xe9, 0xf2, 0xfa, 0x98, 0x5e, 0x97, 0x97, 0xc9, 0xc2, 0xba, 0xd9, 0x5c, 0x96, 0xf3,
        0xec, 0xe5, 0x31, 0xfd, 0x2d, 0xe7, 0x99, 0xd9, 0xef, 0x30, 0x9d, 0x05, 0x92, 0xf9, 0x40, 0x3e, 0x83, 0xf8, 0x83, 0xe1, 0xf2, 0x9d, 0xc7, 0x17, 0x22, 0x02, 0x98, 0x88, 0x10, 0x68, 0x86, 0x85,
        0xf8, 0x9c, 0xa8, 0x44, 0x02, 0x5f, 0x9a, 0x22, 0x4a, 0x98, 0xfc, 0x15, 0xc0, 0xa6, 0x08, 0x10, 0x90, 0xc6, 0xd0, 0x04, 0x81, 0x58, 0x88, 0x08, 0x98, 0x10, 0xa7, 0x61, 0xa7, 0x88, 0x12, 0x26,
        0x2a, 0x22, 0x2c, 0x61, 0x1b, 0x2e, 0xdf, 0x79, 0xfc, 0x01, 0x91, 0x1e, 0x60, 0x12, 0x8e, 0x15, 0xc0, 0x24, 0xb1, 0x19, 0x88, 0xcb, 0x47, 0x6e, 0xdb, 0x16, 0xae, 0xe1, 0xf2, 0x9d, 0xc7, 0x8f,
        0x00, 0x6b, 0xa3, 0x8a, 0x82, 0x88, 0x4a, 0x07, 0x18, 0xfc, 0xe2, 0xb6, 0x4d, 0x01, 0x1b, 0x2e, 0xdf, 0x79, 0xfc, 0x08, 0xb0, 0x36, 0xaa, 0x28, 0x88, 0x88, 0x9d, 0x9c, 0x88, 0xf0, 0x8b, 0xdb,
        0x36, 0x06, 0x30, 0x18, 0x2e, 0xdf, 0x79, 0xfc, 0x29, 0x02, 0x04, 0x62, 0x05, 0x30, 0x5f, 0x9a, 0x22, 0x4a, 0x98, 0xfc, 0x15, 0xc0, 0x52, 0x00, 0x5b, 0x1c, 0x60, 0x00, 0x61, 0x20, 0x00, 0x00,
        0x1e, 0x00, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x14, 0xb0, 0x40, 0xd9, 0x01, 0x00, 0x00, 0x04, 0x06, 0xcb, 0x20, 0x31, 0x48, 0xc6, 0x88,
        0x81, 0x01, 0x80, 0x20, 0x18, 0xa4, 0x01, 0x46, 0x08, 0x23, 0x06, 0x06, 0x00, 0x82, 0x60, 0x70, 0x06, 0x5a, 0x21, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x01, 0x07, 0x06, 0x45, 0x20, 0x8d, 0x26,
        0x04, 0xc0, 0x72, 0x88, 0x84, 0xa2, 0xa8, 0x61, 0x03, 0x22, 0x10, 0x06, 0x60, 0xc4, 0xe0, 0x00, 0x40, 0x10, 0x0c, 0xb8, 0x31, 0x40, 0x8a, 0x6a, 0x34, 0x21, 0x00, 0x96, 0x43, 0x30, 0xd7, 0xb5,
        0x0d, 0x1b, 0x10, 0x81, 0x30, 0x00, 0x18, 0x0e, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x16, 0x72, 0x3c, 0x00, 0xb6, 0x38, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61, 0x20, 0x00, 0x00,
        0x20, 0x00, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x14, 0xb0, 0x40, 0xd9, 0x01, 0x00, 0x00, 0x14, 0x06, 0xcb, 0xa0, 0x31, 0x48, 0xc6, 0x88,
        0x81, 0x01, 0x80, 0x20, 0x18, 0xa4, 0x41, 0x46, 0x08, 0x23, 0x06, 0x06, 0x00, 0x82, 0x60, 0x70, 0x06, 0x5b, 0x21, 0x8c, 0x18, 0x1c, 0x00, 0x08, 0x82, 0x01, 0x17, 0x06, 0x45, 0x30, 0x8d, 0x26,
        0x04, 0xc0, 0x72, 0x08, 0xa5, 0xaa, 0xaa, 0x61, 0x03, 0x22, 0x10, 0x06, 0x60, 0xc4, 0xe0, 0x00, 0x40, 0x10, 0x0c, 0x38, 0x32, 0x40, 0x0a, 0x6b, 0x34, 0x21, 0x00, 0x96, 0x43, 0x34, 0x18, 0xc6,
        0x0d, 0x1b, 0x10, 0x81, 0x30, 0x00, 0x18, 0x0e, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x26, 0x72, 0x00, 0xd3, 0x14, 0x21, 0x81, 0x64, 0x21, 0xc7, 0x03, 0x60, 0x8b, 0x03, 0x0c, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x61, 0x20, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x13, 0x04, 0x41, 0x2c, 0x10, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x04, 0x14, 0xb0, 0x40, 0x89, 0x0a, 0x94, 0xa5,
        0x40, 0xe9, 0x0a, 0x14, 0xa6, 0x00, 0xcd, 0x25, 0x30, 0x02, 0x40, 0xd9, 0x08, 0x40, 0x19, 0xd0, 0x30, 0x46, 0x00, 0x82, 0x20, 0x28, 0x83, 0x01, 0x11, 0x23, 0x00, 0x34, 0x8c, 0x11, 0x80, 0x20,
        0x08, 0xe2, 0xbf, 0x30, 0x46, 0x00, 0x82, 0x20, 0x88, 0x7f, 0x33, 0x00, 0x23, 0x00, 0x00, 0x00, 0x33, 0x11, 0x0e, 0x20, 0x91, 0x02, 0xc1, 0x41, 0x31, 0x48, 0x0e, 0x82, 0x41, 0x71, 0x70, 0x8c,
        0x11, 0x03, 0x03, 0x00, 0x41, 0x30, 0xf0, 0xd6, 0x40, 0x62, 0x46, 0x0c, 0x0c, 0x00, 0x04, 0xc1, 0x60, 0x0d, 0xcc, 0x80, 0x22, 0x46, 0x0c, 0x14, 0x00, 0x04, 0xc1, 0x60, 0x0c, 0xd6, 0x60, 0x0a,
        0x04, 0x30, 0x68, 0xc4, 0x60, 0x34, 0x21, 0x00, 0x46, 0x13, 0x84, 0x60, 0x3b, 0x43, 0x32, 0x06, 0x63, 0x30, 0x6c, 0x40, 0x04, 0x0f, 0x01, 0x8c, 0x18, 0x18, 0x00, 0x08, 0x82, 0x41, 0x1b, 0xa8,
        0x41, 0x86, 0x8c, 0x18, 0x50, 0x07, 0x08, 0x82, 0x41, 0x19, 0xbc, 0xc1, 0x15, 0x94, 0x41, 0x1a, 0x94, 0x41, 0x1a, 0x94, 0x01, 0x31, 0x38, 0x0c, 0xc3, 0x3c, 0xd1, 0x42, 0x02, 0x41, 0x46, 0x0c,
        0x0c, 0x00, 0x04, 0xc1, 0xe0, 0x0d, 0xd6, 0x60, 0x4b, 0xc6, 0x10, 0x04, 0x6b, 0x0c, 0x61, 0xc0, 0x46, 0x0c, 0x1c, 0x00, 0x04, 0xc1, 0x00, 0x0c, 0xea, 0x40, 0x1b, 0x96, 0x34, 0x10, 0x82, 0x28,
        0xb2, 0xd6, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    };

    D3D12_SHADER_BYTECODE code;
    code.pShaderBytecode = rt_lib_dxil;
    code.BytecodeLength = sizeof(rt_lib_dxil);
    return code;
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
    {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tmp_inputs = *inputs;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tmp_prebuild_info;

        /* We should ignore this flag when querying prebuild info.
         * Deduced by: Equal sizes, no validation errors.
         * Vulkan does not seem to care either, and this trips validation
         * errors in neither D3D12 nor Vulkan. */
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
    ID3D12Resource *bottom_acceleration_structures[3];
    ID3D12Resource *top_acceleration_structures[3];
    struct rt_acceleration_structure bottom_rtas;
    struct rt_acceleration_structure top_rtas;
    ID3D12Resource *transform_buffer;
    ID3D12Resource *instance_buffer;
};

static void destroy_rt_geometry(struct test_rt_geometry *rt_geom)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(rt_geom->bottom_acceleration_structures); i++)
        ID3D12Resource_Release(rt_geom->bottom_acceleration_structures[i]);
    for (i = 0; i < ARRAY_SIZE(rt_geom->top_acceleration_structures); i++)
        ID3D12Resource_Release(rt_geom->top_acceleration_structures[i]);
    ID3D12Resource_Release(rt_geom->transform_buffer);
    ID3D12Resource_Release(rt_geom->instance_buffer);
    destroy_acceleration_structure(&rt_geom->bottom_rtas);
    destroy_acceleration_structure(&rt_geom->top_rtas);
}

static void init_rt_geometry(struct raytracing_test_context *context, struct test_rt_geometry *rt_geom,
        struct test_geometry *geom,
        unsigned int num_geom_desc, float geom_offset_x,
        unsigned int num_unmasked_instances_y, float instance_geom_scale, float instance_offset_y,
        D3D12_GPU_VIRTUAL_ADDRESS postbuild_va)
{
#define NUM_GEOM_TEMPLATES 6
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

    create_acceleration_structure(context, &inputs, &rt_geom->bottom_rtas, postbuild_va);
    /* Update, and now use correct VBO. */
    geom_desc_template[1].Triangles.VertexBuffer.StartAddress =
            ID3D12Resource_GetGPUVirtualAddress(geom->vbo) + offsetof(struct initial_vbo, f32);
    for (i = 1; i < num_geom_desc; i += ARRAY_SIZE(geom_desc_template))
        geom_desc[i].Triangles.VertexBuffer = geom_desc_template[1].Triangles.VertexBuffer;
    update_acceleration_structure(context, &inputs, &rt_geom->bottom_rtas);

    /* Tests CLONE and COMPACTING copies. COMPACTING can never increase size, so it's safe to allocate up front.
     * We test the compacted size later. */
    rt_geom->bottom_acceleration_structures[0] = rt_geom->bottom_rtas.rtas;
    ID3D12Resource_AddRef(rt_geom->bottom_rtas.rtas);
    rt_geom->bottom_acceleration_structures[1] = duplicate_acceleration_structure(context,
            rt_geom->bottom_acceleration_structures[0],
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
    rt_geom->bottom_acceleration_structures[2] = duplicate_acceleration_structure(context,
            rt_geom->bottom_acceleration_structures[1],
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
        instance_desc[i].AccelerationStructure =
                ID3D12Resource_GetGPUVirtualAddress(rt_geom->bottom_acceleration_structures[i & 1]);
    }

    instance_desc[num_unmasked_instances_y].Transform[0][0] = instance_geom_scale;
    instance_desc[num_unmasked_instances_y].Transform[1][1] = instance_geom_scale;
    instance_desc[num_unmasked_instances_y].Transform[2][2] = instance_geom_scale;
    instance_desc[num_unmasked_instances_y].Transform[1][3] = -instance_offset_y;
    instance_desc[num_unmasked_instances_y].InstanceMask = 0xfe; /* This instance will be masked out since shader uses mask of 0x01. */
    instance_desc[num_unmasked_instances_y].InstanceContributionToHitGroupIndex = 0;
    instance_desc[num_unmasked_instances_y].AccelerationStructure =
            ID3D12Resource_GetGPUVirtualAddress(rt_geom->bottom_acceleration_structures[2]);

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

static ID3D12StateObject *rt_pso_factory_compile(struct raytracing_test_context *context,
        struct rt_pso_factory *factory,
        D3D12_STATE_OBJECT_TYPE type)
{
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION *assoc;
    D3D12_STATE_OBJECT_DESC desc;
    ID3D12StateObject *rt_pso;
    HRESULT hr;
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
    hr = ID3D12Device5_CreateStateObject(context->device5, &desc, &IID_ID3D12StateObject, (void **)&rt_pso);
    ok(SUCCEEDED(hr), "Failed to create RT PSO, hr %#x.\n", hr);

    free(factory->subobjects);
    for (i = 0; i < factory->allocs_count; i++)
        free(factory->allocs[i]);
    free(factory->allocs);
    memset(factory, 0, sizeof(*factory));

    return rt_pso;
}

static ID3D12StateObject *create_rt_collection(struct raytracing_test_context *context,
        unsigned int num_exports, D3D12_EXPORT_DESC *exports,
        const D3D12_HIT_GROUP_DESC *hit_group,
        ID3D12RootSignature *global_rs, ID3D12RootSignature *local_rs)
{
    struct rt_pso_factory factory;

    rt_pso_factory_init(&factory);

    rt_pso_factory_add_state_object_config(&factory,
            D3D12_STATE_OBJECT_FLAG_ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS);
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

void test_raytracing(void)
{
#define NUM_GEOM_DESC 6
#define NUM_UNMASKED_INSTANCES 4
#define INSTANCE_OFFSET_Y (100.0f)
#define GEOM_OFFSET_X (10.0f)
#define INSTANCE_GEOM_SCALE (0.5f)

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postbuild_desc[3];
    float sbt_colors[NUM_GEOM_DESC * NUM_UNMASKED_INSTANCES + 1][2];
    D3D12_ROOT_SIGNATURE_DESC root_signature_desc;
    D3D12_DESCRIPTOR_RANGE descriptor_ranges[2];
    ID3D12GraphicsCommandList4 *command_list4;
    D3D12_ROOT_PARAMETER root_parameters[2];
    ID3D12GraphicsCommandList *command_list;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;
    struct raytracing_test_context context;
    ID3D12DescriptorHeap *descriptor_heap;
    ID3D12StateObject *rt_object_library;
    ID3D12RootSignature *local_rs_table;
    struct test_rt_geometry test_rtases;
    D3D12_DESCRIPTOR_RANGE table_range;
    ID3D12Resource *postbuild_readback;
    ID3D12Resource *sbt_colors_buffer;
    ID3D12Resource *postbuild_buffer;
    unsigned int i, descriptor_size;
    ID3D12RootSignature *global_rs;
    struct test_geometry test_geom;
    ID3D12RootSignature *local_rs;
    ID3D12Resource *ray_positions;
    struct resource_readback rb;
    ID3D12Resource *ray_colors;
    ID3D12CommandQueue *queue;
    ID3D12StateObject *rt_pso;
    unsigned int ref_count;
    ID3D12Device *device;
    ID3D12Resource *sbt;
    HRESULT hr;

    if (!init_raytracing_test_context(&context))
        return;

    device = context.context.device;
    command_list = context.context.list;
    command_list4 = context.list4;
    queue = context.context.queue;

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

        root_signature_desc.NumParameters = 1;
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

    /* Create RT collection. */
    {
        D3D12_EXPORT_DESC dxil_exports[1] = {
            { u"XRayClosest", u"RayClosest", 0 },
        };
        D3D12_HIT_GROUP_DESC hit_group;

        memset(&hit_group, 0, sizeof(hit_group));
        hit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hit_group.ClosestHitShaderImport = u"XRayClosest";
        hit_group.HitGroupExport = u"XRayHit";

        rt_object_library = create_rt_collection(&context,
                ARRAY_SIZE(dxil_exports), dxil_exports,
                &hit_group, global_rs, local_rs);
    }

    /* Create RT PSO. */
    if (rt_object_library)
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

        rt_pso_factory_add_state_object_config(&factory, D3D12_STATE_OBJECT_FLAG_NONE);
        rt_pso_factory_add_global_root_signature(&factory, global_rs);
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

        rt_pso_factory_add_existing_collection(&factory, rt_object_library, 0, NULL);

        memset(&hit_group, 0, sizeof(hit_group));
        hit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hit_group.ClosestHitShaderImport = u"XRayClosest";
        hit_group.HitGroupExport = u"XRayHit2";
        rt_pso_factory_add_hit_group(&factory, &hit_group);

        rt_pso = rt_pso_factory_compile(&context, &factory, D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

        /* Docs say there should be ref-count of the collection, but apparently, that refcount is private. */
        ref_count = ID3D12StateObject_AddRef(rt_object_library);
        ok(ref_count == 2, "Collection ref count is %u.\n", ref_count);
        ID3D12StateObject_Release(rt_object_library);
    }
    else
        rt_pso = NULL;

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

        for (i = 0; i < ARRAY_SIZE(sbt_colors); i++)
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
            static const WCHAR ray_closest[] = u"XRayHit::closesthit";
            static const WCHAR ray_anyhit[] = u"XRayHit::anyhit";
            static const WCHAR ray_broken3[] = u"XRayHit::X";
            static const WCHAR ray_broken2[] = u"XRayHit::";
            static const WCHAR ray_broken1[] = u"XRayHit:";
            static const WCHAR ray_broken0[] = u"XRayHit";
            static const WCHAR ray_miss[] = u"XRayMiss";
            static const WCHAR ray_hit2[] = u"XRayHit2";
            static const WCHAR ray_gen[] = u"XRayGen";
            static const WCHAR ray_hit[] = u"XRayHit";
            ID3D12StateObject *tmp_rt_pso;
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

            /* AMD Windows returns 0 here for all stack sizes. There is no well defined return value we expect here,
             * but verify we return something sane. */
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_gen);
            ok(stack_size <= 8, "Stack size %u > 8.\n", stack_size);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_miss);
            ok(stack_size <= 8, "Stack size %u > 8.\n", stack_size);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_closest);
            ok(stack_size <= 8, "Stack size %u > 8.\n", stack_size);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_broken0);
            ok(stack_size == ~0u, "Stack size %u != UINT_MAX.\n", stack_size);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_broken1);
            ok(stack_size == ~0u, "Stack size %u != UINT_MAX.\n", stack_size);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_broken2);
            ok(stack_size == ~0u, "Stack size %u != UINT_MAX.\n", stack_size);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_broken3);
            ok(stack_size == ~0u, "Stack size %u != UINT_MAX.\n", stack_size);
            stack_size = ID3D12StateObjectProperties_GetShaderStackSize(props, ray_anyhit);
            ok(stack_size == ~0u, "Stack size %u != UINT_MAX.\n", stack_size);

            stack_size = ID3D12StateObjectProperties_GetPipelineStackSize(props);
            ok(stack_size <= 8, "Stack size %u < 8.\n", stack_size);

            /* Apparently even if we set stack size here, it will be clamped to the conservative stack size on AMD?
             * Driver behavior on NV and AMD is different here, choose NV behavior as it makes more sense. */
            min_stack_size = stack_size;
            ID3D12StateObjectProperties_SetPipelineStackSize(props, 256);
            stack_size = ID3D12StateObjectProperties_GetPipelineStackSize(props);
            ok(stack_size <= min_stack_size || stack_size == 256, "Stack size %u > %u && %u != 256.\n", stack_size, min_stack_size, stack_size);

            ray_gen_sbt = ID3D12StateObjectProperties_GetShaderIdentifier(props, ray_gen);
            ray_hit_sbt = ID3D12StateObjectProperties_GetShaderIdentifier(props, ray_hit);
            ray_hit_sbt2 = ID3D12StateObjectProperties_GetShaderIdentifier(props, ray_hit2);
            ray_miss_sbt = ID3D12StateObjectProperties_GetShaderIdentifier(props, ray_miss);
            ok(!!ray_gen_sbt, "Failed to get SBT.\n");
            ok(!!ray_hit_sbt, "Failed to get SBT.\n");
            ok(!!ray_hit_sbt2, "Failed to get SBT.\n");
            ok(!!ray_miss_sbt, "Failed to get SBT.\n");

            memcpy(sbt_data, ray_miss_sbt, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            for (i = 0; i < NUM_GEOM_DESC * NUM_UNMASKED_INSTANCES; i++)
                memcpy(sbt_data + (i + 1) * 64, (i & 1 ? ray_hit_sbt : ray_hit_sbt2), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
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

        rtases[0] = ID3D12Resource_GetGPUVirtualAddress(test_rtases.bottom_acceleration_structures[0]);
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

    ID3D12GraphicsCommandList4_SetComputeRootSignature(command_list4, global_rs);
    ID3D12GraphicsCommandList4_SetPipelineState1(command_list4, rt_pso);
    ID3D12GraphicsCommandList4_SetDescriptorHeaps(command_list4, 1, &descriptor_heap);
    ID3D12GraphicsCommandList4_SetComputeRootDescriptorTable(command_list4, 0, gpu_handle);

    if (sbt)
    {
        D3D12_DISPATCH_RAYS_DESC desc;
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

        ID3D12GraphicsCommandList4_DispatchRays(command_list4, &desc);
    }

    transition_resource_state(command_list, ray_colors, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_buffer_readback_with_command_list(ray_colors, DXGI_FORMAT_UNKNOWN, &rb, queue, command_list);
    for (i = 0; i < NUM_GEOM_DESC * NUM_UNMASKED_INSTANCES + 1; i++)
    {
        float x, y;
        x = get_readback_float(&rb, 2 * i, 0);
        y = get_readback_float(&rb, 2 * i + 1, 0);
        ok(x == sbt_colors[i][0], "Ray color [%u].x mismatch (%f != %f).\n", i, sbt_colors[i][0], x);
        ok(y == sbt_colors[i][1], "Ray color [%u].y mismatch (%f != %f).\n", i, sbt_colors[i][1], y);
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
            /* CURRENT_SIZE cannot be queried in Vulkan directly. It should be possible to emulate it with a side buffer which we update on RTAS build,
             * but ignore it for the time being, since it's only really relevant for tools. */
            todo ok(bottom[0].current.CurrentSizeInBytes > 0, "Current size for bottom acceleration structure is %u.\n", (unsigned int)bottom[0].current.CurrentSizeInBytes);
            todo ok(top[0].current.CurrentSizeInBytes > 0, "Current size for top acceleration structure is %u.\n", (unsigned int)top[0].current.CurrentSizeInBytes);

            /* Compacted size must be less-or-equal to current size. Cannot pass since we don't have current size. */
            todo ok(bottom[0].compacted.CompactedSizeInBytes <= bottom[0].current.CurrentSizeInBytes,
                    "Compacted size %u > Current size %u\n", (unsigned int)bottom[0].compacted.CompactedSizeInBytes, (unsigned int)bottom[0].current.CurrentSizeInBytes);
            todo ok(top[0].compacted.CompactedSizeInBytes <= top[0].current.CurrentSizeInBytes,
                    "Compacted size %u > Current size %u\n", (unsigned int)top[0].compacted.CompactedSizeInBytes, (unsigned int)top[0].current.CurrentSizeInBytes);

            ok(bottom[0].serialize.SerializedSizeInBytes > 0, "Serialized size for bottom acceleration structure is %u.\n", (unsigned int)bottom[0].serialize.SerializedSizeInBytes);
            ok(bottom[0].serialize.NumBottomLevelAccelerationStructurePointers == 0, "NumBottomLevel pointers is %u.\n", (unsigned int)bottom[0].serialize.NumBottomLevelAccelerationStructurePointers);
            ok(top[0].serialize.SerializedSizeInBytes > 0, "Serialized size for top acceleration structure is %u.\n", (unsigned int)top[0].serialize.SerializedSizeInBytes);
            todo ok(top[0].serialize.NumBottomLevelAccelerationStructurePointers == 5, "NumBottomLevel pointers is %u.\n", (unsigned int)top[0].serialize.NumBottomLevelAccelerationStructurePointers);

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
    if (rt_object_library)
        ID3D12StateObject_Release(rt_object_library);
    ID3D12Resource_Release(ray_colors);
    ID3D12Resource_Release(ray_positions);
    ID3D12DescriptorHeap_Release(descriptor_heap);
    if (sbt)
        ID3D12Resource_Release(sbt);
    ID3D12Resource_Release(postbuild_readback);
    ID3D12Resource_Release(postbuild_buffer);

    destroy_raytracing_test_context(&context);
}

