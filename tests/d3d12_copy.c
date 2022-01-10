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

void test_copy_texture(void)
{
    D3D12_TEXTURE_COPY_LOCATION src_location, dst_location;
    ID3D12Resource *src_texture, *dst_texture;
    ID3D12GraphicsCommandList *command_list;
    D3D12_SUBRESOURCE_DATA texture_data;
    struct depth_stencil_resource ds;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int x, y, i;
    D3D12_BOX box;

    static const unsigned int clear_data[] =
    {
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    };
    static const unsigned int bitmap_data[] =
    {
        0xff00ff00, 0xff00ff01, 0xff00ff02, 0xff00ff03,
        0xff00ff10, 0xff00ff11, 0xff00ff12, 0xff00ff13,
        0xff00ff20, 0xff00ff21, 0xff00ff22, 0xff00ff23,
        0xff00ff30, 0xff00ff31, 0xff00ff32, 0xff00ff33,
    };
    static const unsigned int result_data[] =
    {
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0xff00ff00, 0xff00ff01, 0x00000000,
        0x00000000, 0xff00ff10, 0xff00ff11, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    };
    static const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const DWORD ps_code[] =
    {
#if 0
        Texture2D<float> t;

        float main(float4 position : SV_Position) : SV_Target
        {
            return t[int2(position.x, position.y)];
        }
#endif
        0x43425844, 0x0beace24, 0x5e10b05b, 0x742de364, 0xb2b65d2b, 0x00000001, 0x00000140, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x7469736f, 0x006e6f69,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x00000e01, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x000000a4, 0x00000040,
        0x00000029, 0x04001858, 0x00107000, 0x00000000, 0x00005555, 0x04002064, 0x00101032, 0x00000000,
        0x00000001, 0x03000065, 0x00102012, 0x00000000, 0x02000068, 0x00000001, 0x0500001b, 0x00100032,
        0x00000000, 0x00101046, 0x00000000, 0x08000036, 0x001000c2, 0x00000000, 0x00004002, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x0700002d, 0x001000f2, 0x00000000, 0x00100e46, 0x00000000,
        0x00107e46, 0x00000000, 0x05000036, 0x00102012, 0x00000000, 0x0010000a, 0x00000000, 0x0100003e,
    };
    static const D3D12_SHADER_BYTECODE ps = {ps_code, sizeof(ps_code)};

    struct depth_copy_test
    {
        float depth_value;
        UINT stencil_value;
        DXGI_FORMAT ds_format;
        DXGI_FORMAT ds_view_format;
        DXGI_FORMAT color_format;
        bool stencil;
        bool roundtrip;
    };
    static const struct depth_copy_test depth_copy_tests[] = {
        { 0.0f, 0, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, false, false },
        { 0.0f, 0, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, false, true },
        { 0.5f, 10, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R32_FLOAT, false, false },
        { 0.5f, 10, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R32_FLOAT, false, true },
        { 0.2f, 11, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R8_UINT, true, false },
        { 0.7f, 0, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, false, false },
        { 0.7f, 0, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, false, true },
        { 0.4f, 20, DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R32_FLOAT, false, false },
        { 0.4f, 20, DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R32_FLOAT, false, true },
        { 1.0f, 21, DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R8_UINT, true, false },
    };

    static const D3D12_RESOURCE_STATES resource_states[] =
    {
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_GENERIC_READ,
    };

    memset(&desc, 0, sizeof(desc));
    desc.rt_format = DXGI_FORMAT_R32_FLOAT;
    desc.no_root_signature = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    for (i = 0; i < ARRAY_SIZE(resource_states); ++i)
    {
        src_texture = create_default_texture(device, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM,
                0, D3D12_RESOURCE_STATE_COPY_DEST);
        texture_data.pData = bitmap_data;
        texture_data.RowPitch = 4 * sizeof(*bitmap_data);
        texture_data.SlicePitch = texture_data.RowPitch * 4;
        upload_texture_data(src_texture, &texture_data, 1, queue, command_list);
        reset_command_list(command_list, context.allocator);

        dst_texture = create_default_texture(device, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM,
                0, D3D12_RESOURCE_STATE_COPY_DEST);
        texture_data.pData = clear_data;
        texture_data.RowPitch = 4 * sizeof(*clear_data);
        texture_data.SlicePitch = texture_data.RowPitch * 4;
        upload_texture_data(dst_texture, &texture_data, 1, queue, command_list);
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, src_texture,
                D3D12_RESOURCE_STATE_COPY_DEST, resource_states[i]);

        src_location.pResource = src_texture;
        src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src_location.SubresourceIndex = 0;
        dst_location.pResource = dst_texture;
        dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_location.SubresourceIndex = 0;
        set_box(&box, 0, 0, 0, 2, 2, 1);
        ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
                &dst_location, 1, 1, 0, &src_location, &box);

        transition_resource_state(command_list, dst_texture,
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_texture_readback_with_command_list(dst_texture, 0, &rb, queue, command_list);
        for (y = 0; y < 4; ++y)
        {
            for (x = 0; x < 4; ++x)
            {
                unsigned int color = get_readback_uint(&rb, x, y, 0);
                unsigned int expected = result_data[y * 4 + x];

                ok(color == expected,
                        "Got unexpected color 0x%08x at (%u, %u), expected 0x%08x.\n",
                        color, x, y, expected);
            }
        }
        release_resource_readback(&rb);
        ID3D12Resource_Release(src_texture);
        ID3D12Resource_Release(dst_texture);
        reset_command_list(command_list, context.allocator);
    }

    for (i = 0; i < ARRAY_SIZE(resource_states); ++i)
    {
        src_texture = create_default_texture(device, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM,
                0, D3D12_RESOURCE_STATE_COPY_DEST);
        texture_data.pData = bitmap_data;
        texture_data.RowPitch = 4 * sizeof(*bitmap_data);
        texture_data.SlicePitch = texture_data.RowPitch * 4;
        upload_texture_data(src_texture, &texture_data, 1, queue, command_list);
        reset_command_list(command_list, context.allocator);

        dst_texture = create_default_texture(device, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM,
                0, D3D12_RESOURCE_STATE_COPY_DEST);
        texture_data.pData = clear_data;
        texture_data.RowPitch = 4 * sizeof(*clear_data);
        texture_data.SlicePitch = texture_data.RowPitch * 4;
        upload_texture_data(dst_texture, &texture_data, 1, queue, command_list);
        reset_command_list(command_list, context.allocator);
        transition_resource_state(command_list, src_texture,
                D3D12_RESOURCE_STATE_COPY_DEST, resource_states[i]);

        src_location.pResource = src_texture;
        src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src_location.SubresourceIndex = 0;
        dst_location.pResource = dst_texture;
        dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_location.SubresourceIndex = 0;
        ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
                &dst_location, 0, 0, 0, &src_location, 0);

        transition_resource_state(command_list, dst_texture,
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        get_texture_readback_with_command_list(dst_texture, 0, &rb, queue, command_list);
        for (y = 0; y < 4; ++y)
        {
            for (x = 0; x < 4; ++x)
            {
                unsigned int color = get_readback_uint(&rb, x, y, 0);
                unsigned int expected = bitmap_data[y * 4 + x];

                ok(color == expected,
                        "Got unexpected color 0x%08x at (%u, %u), expected 0x%08x.\n",
                        color, x, y, expected);
            }
        }
        release_resource_readback(&rb);
        ID3D12Resource_Release(src_texture);
        ID3D12Resource_Release(dst_texture);
        reset_command_list(command_list, context.allocator);
    }

    context.root_signature = create_texture_root_signature(device,
            D3D12_SHADER_VISIBILITY_PIXEL, 0, 0);
    context.pipeline_state = create_pipeline_state(device,
            context.root_signature, context.render_target_desc.Format, NULL, &ps, NULL);

    heap = create_gpu_descriptor_heap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    for (i = 0; i < ARRAY_SIZE(depth_copy_tests); ++i)
    {
        init_depth_stencil(&ds, device, context.render_target_desc.Width,
                context.render_target_desc.Height, 1, 1, depth_copy_tests[i].ds_format,
                depth_copy_tests[i].ds_view_format, NULL);

        if (depth_copy_tests[i].stencil)
        {
            ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
                D3D12_CLEAR_FLAG_STENCIL, 0.0f, depth_copy_tests[i].stencil_value, 0, NULL);
        }
        else
        {
            ID3D12GraphicsCommandList_ClearDepthStencilView(command_list, ds.dsv_handle,
                    D3D12_CLEAR_FLAG_DEPTH, depth_copy_tests[i].depth_value, 0, 0, NULL);
        }
        transition_sub_resource_state(command_list, ds.texture, depth_copy_tests[i].stencil ? 1 : 0,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, resource_states[i % ARRAY_SIZE(resource_states)]);

        dst_texture = create_default_texture(device, 32, 32, depth_copy_tests[i].color_format,
                0, D3D12_RESOURCE_STATE_COPY_DEST);
        ID3D12Device_CreateShaderResourceView(device, dst_texture, NULL,
                ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap));

        src_location.pResource = ds.texture;
        src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src_location.SubresourceIndex = depth_copy_tests[i].stencil ? 1 : 0;
        dst_location.pResource = dst_texture;
        dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_location.SubresourceIndex = 0;
        ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &dst_location, 0, 0, 0,
                &src_location, NULL);

        if (depth_copy_tests[i].roundtrip)
        {
            /* Test color to depth copy. */
            D3D12_TEXTURE_COPY_LOCATION tmp_src_location = dst_location;
            D3D12_TEXTURE_COPY_LOCATION tmp_dst_location = src_location;
            transition_sub_resource_state(command_list, dst_texture, 0,
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            transition_sub_resource_state(command_list, ds.texture, 0,
                    resource_states[i % ARRAY_SIZE(resource_states)], D3D12_RESOURCE_STATE_COPY_DEST);
            ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &tmp_dst_location, 0, 0, 0,
                    &tmp_src_location, NULL);
            transition_sub_resource_state(command_list, dst_texture, 0,
                    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
            transition_sub_resource_state(command_list, ds.texture, 0,
                    D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &dst_location, 0, 0, 0,
                    &src_location, NULL);
        }

        transition_sub_resource_state(command_list, dst_texture, 0,
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        ID3D12GraphicsCommandList_ClearRenderTargetView(command_list, context.rtv, white, 0, NULL);
        ID3D12GraphicsCommandList_OMSetRenderTargets(command_list, 1, &context.rtv, false, NULL);
        ID3D12GraphicsCommandList_RSSetViewports(command_list, 1, &context.viewport);
        ID3D12GraphicsCommandList_RSSetScissorRects(command_list, 1, &context.scissor_rect);

        ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, &heap);

        ID3D12GraphicsCommandList_SetPipelineState(command_list, context.pipeline_state);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, context.root_signature);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, 0,
                ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(heap));
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(command_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_DrawInstanced(command_list, 3, 1, 0, 0);

        transition_sub_resource_state(command_list, context.render_target, 0,
                D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);

        if (depth_copy_tests[i].stencil)
        {
            check_sub_resource_uint(context.render_target, 0, queue, command_list, depth_copy_tests[i].stencil_value, 0);
        }
        else
        {
            check_sub_resource_float(context.render_target, 0, queue, command_list, depth_copy_tests[i].depth_value, 2);
        }

        destroy_depth_stencil(&ds);
        ID3D12Resource_Release(dst_texture);

        reset_command_list(command_list, context.allocator);
        transition_sub_resource_state(command_list, context.render_target, 0,
                D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    ID3D12DescriptorHeap_Release(heap);
    destroy_test_context(&context);
}

void test_copy_texture_buffer(void)
{
    D3D12_TEXTURE_COPY_LOCATION src_location, dst_location;
    ID3D12GraphicsCommandList *command_list;
    D3D12_SUBRESOURCE_DATA texture_data;
    ID3D12Resource *dst_buffers[4];
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12Resource *src_texture;
    unsigned int got, expected;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int x, y;
    unsigned int *ptr;
    unsigned int i;
    D3D12_BOX box;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    ptr = calloc(64 * 32, sizeof(*ptr));
    ok(ptr, "Failed to allocate memory.\n");

    for (i = 0; i < 64 * 32; ++i)
        ptr[i] = i;

    src_texture = create_default_texture(device,
            64, 32, DXGI_FORMAT_R32_UINT, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    texture_data.pData = ptr;
    texture_data.RowPitch = 64 * sizeof(*ptr);
    texture_data.SlicePitch = texture_data.RowPitch * 32;
    upload_texture_data(src_texture, &texture_data, 1, queue, command_list);
    reset_command_list(command_list, context.allocator);
    transition_resource_state(command_list, src_texture,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    free(ptr);

    for (i = 0; i < ARRAY_SIZE(dst_buffers); ++i)
    {
        dst_buffers[i] = create_default_buffer(device,
                64 * 32 * sizeof(*ptr), 0, D3D12_RESOURCE_STATE_COPY_DEST);
    }

    dst_location.pResource = dst_buffers[0];
    dst_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_location.PlacedFootprint.Offset = 0;
    dst_location.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_UINT;
    dst_location.PlacedFootprint.Footprint.Width = 64;
    dst_location.PlacedFootprint.Footprint.Height = 32;
    dst_location.PlacedFootprint.Footprint.Depth = 1;
    dst_location.PlacedFootprint.Footprint.RowPitch = 64 * sizeof(*ptr);

    src_location.pResource = src_texture;
    src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_location.SubresourceIndex = 0;

    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 0, 0, 0, &src_location, NULL);

    dst_location.pResource = dst_buffers[1];
    for (y = 0; y < 32; ++y)
    {
        set_box(&box, 0, y, 0, 64, y + 1, 1);
        ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
                &dst_location, 0, 31 - y, 0, &src_location, &box);
    }

    dst_location.pResource = dst_buffers[2];
    for (x = 0; x < 64; ++x)
    {
        set_box(&box, x, 0, 0, x + 1, 32, 1);
        ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
                &dst_location, 63 - x, 0, 0, &src_location, &box);
    }

    dst_location.pResource = dst_buffers[3];
    set_box(&box, 0, 0, 0, 32, 32, 1);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 0, 0, 0, &src_location, &box);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 32, 0, 0, &src_location, &box);

    /* empty box */
    set_box(&box, 128, 0, 0, 32, 32, 1);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 0, 0, 0, &src_location, &box);

    for (i = 0; i < ARRAY_SIZE(dst_buffers); ++i)
    {
        transition_resource_state(command_list, dst_buffers[i],
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    }

    got = expected = 0;
    get_buffer_readback_with_command_list(dst_buffers[0], DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    for (i = 0; i < 64 * 32; ++i)
    {
        got = get_readback_uint(&rb, i, 0, 0);
        expected = i;

        if (got != expected)
            break;
    }
    release_resource_readback(&rb);
    ok(got == expected, "Got unexpected value 0x%08x at %u, expected 0x%08x.\n", got, i, expected);

    reset_command_list(command_list, context.allocator);
    got = expected = 0;
    get_buffer_readback_with_command_list(dst_buffers[1], DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    for (y = 0; y < 32; ++y)
    {
        for (x = 0; x < 64; ++x)
        {
            got = get_readback_uint(&rb, 64 * y + x, 0, 0);
            expected = 64 * (31 - y) + x;

            if (got != expected)
                break;
        }
        if (got != expected)
            break;
    }
    release_resource_readback(&rb);
    ok(got == expected, "Got unexpected value 0x%08x at (%u, %u), expected 0x%08x.\n", got, x, y, expected);

    reset_command_list(command_list, context.allocator);
    got = expected = 0;
    get_buffer_readback_with_command_list(dst_buffers[2], DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    for (y = 0; y < 32; ++y)
    {
        for (x = 0; x < 64; ++x)
        {
            got = get_readback_uint(&rb, 64 * y + x, 0, 0);
            expected = 64 * y + 63 - x;

            if (got != expected)
                break;
        }
        if (got != expected)
            break;
    }
    release_resource_readback(&rb);
    ok(got == expected, "Got unexpected value 0x%08x at (%u, %u), expected 0x%08x.\n", got, x, y, expected);

    reset_command_list(command_list, context.allocator);
    got = expected = 0;
    get_buffer_readback_with_command_list(dst_buffers[3], DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    for (y = 0; y < 32; ++y)
    {
        for (x = 0; x < 64; ++x)
        {
            got = get_readback_uint(&rb, 64 * y + x, 0, 0);
            expected = 64 * y + x % 32;

            if (got != expected)
                break;
        }
        if (got != expected)
            break;
    }
    release_resource_readback(&rb);
    ok(got == expected, "Got unexpected value 0x%08x at (%u, %u), expected 0x%08x.\n", got, x, y, expected);

    ID3D12Resource_Release(src_texture);
    for (i = 0; i < ARRAY_SIZE(dst_buffers); ++i)
        ID3D12Resource_Release(dst_buffers[i]);
    destroy_test_context(&context);
}

void test_copy_buffer_to_depth_stencil(void)
{
    ID3D12GraphicsCommandList *command_list;
    struct resource_readback rb_stencil;
    ID3D12Resource *src_buffer_stencil;
    struct resource_readback rb_depth;
    ID3D12Resource *src_buffer_depth;
    struct test_context_desc desc;
    struct test_context context;
    ID3D12Resource *dst_texture;
    ID3D12CommandQueue *queue;
    ID3D12Device *device;
    unsigned int i, x, y;

    struct test
    {
        DXGI_FORMAT format;
        uint32_t input_depth;
        uint32_t output_depth_24;
        D3D12_RESOURCE_FLAGS flags;
        bool stencil;
    };
#define UNORM24_1 ((1 << 24) - 1)
#define FP32_1 0x3f800000
#define FP32_MASK24 (FP32_1 & UNORM24_1)
    /* The footprint is 32-bit and AMD and NV seem to behave differently.
     * The footprint for 24-bit depth is actually FP32 w/ rounding or something weird like that, not sure what is going on.
     * Either way, there are two correct results we can expect here. */
    static const struct test tests[] =
    {
        { DXGI_FORMAT_D24_UNORM_S8_UINT, UNORM24_1, UNORM24_1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true },
        { DXGI_FORMAT_D24_UNORM_S8_UINT, FP32_1, FP32_MASK24, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true },
        { DXGI_FORMAT_D24_UNORM_S8_UINT, FP32_1, FP32_MASK24, D3D12_RESOURCE_FLAG_NONE, true },
        { DXGI_FORMAT_D32_FLOAT, FP32_1, FP32_1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, false },
        { DXGI_FORMAT_D32_FLOAT_S8X24_UINT, FP32_1, FP32_1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true },
        { DXGI_FORMAT_R24_UNORM_X8_TYPELESS, UNORM24_1, UNORM24_1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, false },
        { DXGI_FORMAT_R24_UNORM_X8_TYPELESS, FP32_1, FP32_MASK24, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, false },
        { DXGI_FORMAT_R24_UNORM_X8_TYPELESS, FP32_1, FP32_MASK24, D3D12_RESOURCE_FLAG_NONE, false },
        { DXGI_FORMAT_R24G8_TYPELESS, UNORM24_1, UNORM24_1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true },
        { DXGI_FORMAT_R24G8_TYPELESS, FP32_1, FP32_MASK24, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true },
        { DXGI_FORMAT_R24G8_TYPELESS, FP32_1, FP32_MASK24, D3D12_RESOURCE_FLAG_NONE, true },
        { DXGI_FORMAT_R32_TYPELESS, FP32_1, FP32_1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, false },
        { DXGI_FORMAT_R32_TYPELESS, FP32_1, FP32_1, D3D12_RESOURCE_FLAG_NONE, false },
        { DXGI_FORMAT_R32G8X24_TYPELESS, FP32_1, FP32_1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, true },
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    for (i = 0; i < ARRAY_SIZE(tests); i++)
    {
        uint32_t depth_data[(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT / 4) * 4];
        uint8_t stencil_data[D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * 4];
        D3D12_TEXTURE_COPY_LOCATION dst, src;
        D3D12_BOX src_box;

        vkd3d_test_set_context("Test %u", i);
        dst_texture = create_default_texture2d(device, 2, 2, 1, 1,
                   tests[i].format, tests[i].flags, D3D12_RESOURCE_STATE_COPY_DEST);

        memset(depth_data, 0, sizeof(depth_data));
        depth_data[0] = tests[i].input_depth;
        depth_data[1] = tests[i].input_depth;
        depth_data[D3D12_TEXTURE_DATA_PITCH_ALIGNMENT / 4] = tests[i].input_depth;
        depth_data[D3D12_TEXTURE_DATA_PITCH_ALIGNMENT / 4 + 1] = tests[i].input_depth;

        src_buffer_depth = create_upload_buffer(device, sizeof(depth_data), depth_data);

        if (tests[i].stencil)
        {
            memset(stencil_data, 0, sizeof(stencil_data));
            stencil_data[0] = 0xaa;
            stencil_data[1] = 0xab;
            stencil_data[D3D12_TEXTURE_DATA_PITCH_ALIGNMENT + 0] = 0xac;
            stencil_data[D3D12_TEXTURE_DATA_PITCH_ALIGNMENT + 1] = 0xad;
            src_buffer_stencil = create_upload_buffer(device, sizeof(stencil_data), stencil_data);
        }

        set_box(&src_box, 0, 0, 0, 2, 2, 1);
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.pResource = dst_texture;
        src.PlacedFootprint.Offset = 0;
        src.PlacedFootprint.Footprint.Width = 2;
        src.PlacedFootprint.Footprint.Height = 2;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_TYPELESS;
        dst.SubresourceIndex = 0;
        src.pResource = src_buffer_depth;
        ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &dst, 0, 0, 0, &src, &src_box);

        if (tests[i].stencil)
        {
            src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8_TYPELESS;
            dst.SubresourceIndex = 1;
            src.pResource = src_buffer_stencil;
            ID3D12GraphicsCommandList_CopyTextureRegion(command_list, &dst, 0, 0, 0, &src, &src_box);
        }

        transition_resource_state(command_list, dst_texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

        get_texture_readback_with_command_list(dst_texture, 0, &rb_depth, queue, command_list);
        reset_command_list(command_list, context.allocator);
        if (tests[i].stencil)
        {
            get_texture_readback_with_command_list(dst_texture, 1, &rb_stencil, queue, command_list);
            reset_command_list(command_list, context.allocator);
        }

        for (y = 0; y < 2; y++)
        {
            for (x = 0; x < 2; x++)
            {
                uint32_t v = get_readback_uint(&rb_depth, x, y, 0);
                ok((v & 0xffffffu) == tests[i].output_depth_24 || v == tests[i].input_depth, "Depth is 0x%x\n", v);
            }
        }

        if (tests[i].stencil)
        {
            for (y = 0; y < 2; y++)
            {
                for (x = 0; x < 2; x++)
                {
                    uint8_t v = get_readback_uint8(&rb_stencil, x, y);
                    ok(v == 0xaa + y * 2 + x, "Stencil is 0x%x\n", v);
                }
            }
        }

        release_resource_readback(&rb_depth);
        ID3D12Resource_Release(src_buffer_depth);
        ID3D12Resource_Release(dst_texture);
        if (tests[i].stencil)
        {
            ID3D12Resource_Release(src_buffer_stencil);
            release_resource_readback(&rb_stencil);
        }
    }
    vkd3d_test_set_context(NULL);

    destroy_test_context(&context);
}

void test_copy_buffer_texture(void)
{
    D3D12_TEXTURE_COPY_LOCATION src_location, dst_location;
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12Resource *zero_buffer;
    ID3D12Resource *dst_texture;
    ID3D12Resource *src_buffer;
    unsigned int got, expected;
    ID3D12CommandQueue *queue;
    unsigned int buffer_size;
    ID3D12Device *device;
    unsigned int x, y, z;
    unsigned int *ptr;
    unsigned int i;
    D3D12_BOX box;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    buffer_size = 128 * 100 * 64;

    zero_buffer = create_upload_buffer(device, buffer_size * sizeof(*ptr) + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, NULL);
    hr = ID3D12Resource_Map(zero_buffer, 0, NULL, (void **)&ptr);
    ok(hr == S_OK, "Failed to map buffer, hr %#x.\n", hr);
    memset(ptr, 0, buffer_size * sizeof(*ptr) + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    for (i = 0; i < D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT / sizeof(*ptr); ++i)
        ptr[i] = 0xdeadbeef;
    ID3D12Resource_Unmap(zero_buffer, 0, NULL);

    src_buffer = create_upload_buffer(device, buffer_size * sizeof(*ptr), NULL);
    hr = ID3D12Resource_Map(src_buffer, 0, NULL, (void **)&ptr);
    ok(hr == S_OK, "Failed to map buffer, hr %#x.\n", hr);
    for (z = 0; z < 64; ++z)
    {
        for (y = 0; y < 100; ++y)
        {
            for (x = 0; x < 128; ++x)
            {
                ptr[z * 128 * 100 + y * 128 + x] = (z + 1) << 16 | (y + 1) << 8 | (x + 1);
            }
        }
    }
    ID3D12Resource_Unmap(src_buffer, 0, NULL);

    dst_texture = create_default_texture3d(device, 128, 100, 64, 2,
            DXGI_FORMAT_R32_UINT, 0, D3D12_RESOURCE_STATE_COPY_DEST);

    dst_location.pResource = dst_texture;
    dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_location.SubresourceIndex = 0;

    src_location.pResource = zero_buffer;
    src_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_location.PlacedFootprint.Offset = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
    src_location.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_UINT;
    src_location.PlacedFootprint.Footprint.Width = 128;
    src_location.PlacedFootprint.Footprint.Height = 100;
    src_location.PlacedFootprint.Footprint.Depth = 64;
    src_location.PlacedFootprint.Footprint.RowPitch = 128 * sizeof(*ptr);

    /* fill with 0 */
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 0, 0, 0, &src_location, NULL);

    src_location.pResource = src_buffer;
    src_location.PlacedFootprint.Offset = 0;

    /* copy region 1 */
    set_box(&box, 64, 16, 8, 128, 100, 64);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 64, 16, 8, &src_location, &box);

    /* empty boxes */
    for (z = 0; z < 2; ++z)
    {
        for (y = 0; y < 4; ++y)
        {
            for (x = 0; x < 8; ++x)
            {
                set_box(&box, x, y, z, x, y, z);
                ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
                        &dst_location, 0, 0, 0, &src_location, &box);
                ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
                        &dst_location, x, y, z, &src_location, &box);
            }
        }
    }

    /* copy region 2 */
    set_box(&box, 0, 0, 0, 4, 4, 4);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 2, 2, 2, &src_location, &box);

    /* fill sub-resource 1 */
    dst_location.SubresourceIndex = 1;
    set_box(&box, 0, 0, 0, 64, 50, 32);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 0, 0, 0, &src_location, &box);

    transition_resource_state(command_list, dst_texture,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    got = expected = 0;
    get_texture_readback_with_command_list(dst_texture, 0, &rb, queue, command_list);
    for (z = 0; z < 64; ++z)
    {
        for (y = 0; y < 100; ++y)
        {
            for (x = 0; x < 128; ++x)
            {
                got = get_readback_uint(&rb, x, y, z);

                if (2 <= x && x < 6 && 2 <= y && y < 6 && 2 <= z && z < 6)
                    expected = (z - 1) << 16 | (y - 1) << 8 | (x - 1); /* copy region 1 */
                else if (64 <= x && 16 <= y && 8 <= z)
                    expected = (z + 1) << 16 | (y + 1) << 8 | (x + 1); /* copy region 2 */
                else
                    expected = 0;

                if (got != expected)
                    break;
            }
            if (got != expected)
                break;
        }
        if (got != expected)
            break;
    }
    release_resource_readback(&rb);
    ok(got == expected,
            "Got unexpected value 0x%08x at (%u, %u, %u), expected 0x%08x.\n",
            got, x, y, z, expected);

    reset_command_list(command_list, context.allocator);
    got = expected = 0;
    get_texture_readback_with_command_list(dst_texture, 1, &rb, queue, command_list);
    for (z = 0; z < 32; ++z)
    {
        for (y = 0; y < 50; ++y)
        {
            for (x = 0; x < 64; ++x)
            {
                got = get_readback_uint(&rb, x, y, z);
                expected = (z + 1) << 16 | (y + 1) << 8 | (x + 1);

                if (got != expected)
                    break;
            }
            if (got != expected)
                break;
        }
        if (got != expected)
            break;
    }
    release_resource_readback(&rb);
    ok(got == expected,
            "Got unexpected value 0x%08x at (%u, %u, %u), expected 0x%08x.\n",
            got, x, y, z, expected);

    ID3D12Resource_Release(dst_texture);
    ID3D12Resource_Release(src_buffer);
    ID3D12Resource_Release(zero_buffer);
    destroy_test_context(&context);
}

void test_copy_block_compressed_texture(void)
{
    D3D12_TEXTURE_COPY_LOCATION src_location, dst_location;
    ID3D12Resource *dst_buffer, *src_buffer;
    ID3D12GraphicsCommandList *command_list;
    struct test_context_desc desc;
    unsigned int x, y, block_id;
    struct test_context context;
    struct resource_readback rb;
    struct uvec4 got, expected;
    ID3D12CommandQueue *queue;
    ID3D12Resource *texture;
    ID3D12Device *device;
    unsigned int *ptr;
    D3D12_BOX box;
    HRESULT hr;

    memset(&desc, 0, sizeof(desc));
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;
    device = context.device;
    command_list = context.list;
    queue = context.queue;

    dst_buffer = create_default_buffer(device, 4096, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    src_buffer = create_upload_buffer(device, 4096, NULL);
    hr = ID3D12Resource_Map(src_buffer, 0, NULL, (void **)&ptr);
    ok(hr == S_OK, "Failed to map buffer, hr %#x.\n", hr);
    for (x = 0; x < 4096 / format_size(DXGI_FORMAT_BC2_UNORM); ++x)
    {
        block_id = x << 8;
        *ptr++ = block_id | 0;
        *ptr++ = block_id | 1;
        *ptr++ = block_id | 2;
        *ptr++ = block_id | 3;
    }
    ID3D12Resource_Unmap(src_buffer, 0, NULL);

    texture = create_default_texture2d(device, 8, 8, 1, 4, DXGI_FORMAT_BC2_UNORM,
            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);

    /* copy from buffer to texture */
    dst_location.pResource = texture;
    dst_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_location.SubresourceIndex = 0;

    src_location.pResource = src_buffer;
    src_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_location.PlacedFootprint.Offset = 0;
    src_location.PlacedFootprint.Footprint.Format = DXGI_FORMAT_BC2_UNORM;
    src_location.PlacedFootprint.Footprint.Width = 32;
    src_location.PlacedFootprint.Footprint.Height = 32;
    src_location.PlacedFootprint.Footprint.Depth = 1;
    src_location.PlacedFootprint.Footprint.RowPitch
            = 32 / format_block_width(DXGI_FORMAT_BC2_UNORM) * format_size(DXGI_FORMAT_BC2_UNORM);
    src_location.PlacedFootprint.Footprint.RowPitch
            = align(src_location.PlacedFootprint.Footprint.RowPitch, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

    set_box(&box, 4, 4, 0, 8, 8, 1);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 0, 0, 0, &src_location, &box);
    set_box(&box, 28, 0, 0, 32, 4, 1);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 4, 0, 0, &src_location, &box);
    set_box(&box, 0, 24, 0, 4, 28, 1);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 0, 4, 0, &src_location, &box);
    set_box(&box, 16, 16, 0, 20, 20, 1);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 4, 4, 0, &src_location, &box);

    /* miplevels smaller than 4x4 */
    dst_location.SubresourceIndex = 2;
    set_box(&box, 4, 0, 0, 8, 4, 1);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 0, 0, 0, &src_location, &box);
    dst_location.SubresourceIndex = 3;
    set_box(&box, 8, 0, 0, 12, 4, 1);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 0, 0, 0, &src_location, &box);

    transition_resource_state(command_list, texture,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    /* copy from texture to buffer */
    dst_location.pResource = dst_buffer;
    dst_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_location.PlacedFootprint.Offset = 0;
    dst_location.PlacedFootprint.Footprint.Format = DXGI_FORMAT_BC2_UNORM;
    dst_location.PlacedFootprint.Footprint.Width = 8;
    dst_location.PlacedFootprint.Footprint.Height = 24;
    dst_location.PlacedFootprint.Footprint.Depth = 1;
    dst_location.PlacedFootprint.Footprint.RowPitch
            = 8 / format_block_width(DXGI_FORMAT_BC2_UNORM) * format_size(DXGI_FORMAT_BC2_UNORM);
    dst_location.PlacedFootprint.Footprint.RowPitch
            = align(dst_location.PlacedFootprint.Footprint.RowPitch, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

    src_location.pResource = texture;
    src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_location.SubresourceIndex = 0;

    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 0, 0, 0, &src_location, NULL);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 0, 8, 0, &src_location, NULL);
    set_box(&box, 0, 0, 0, 8, 8, 1);
    ID3D12GraphicsCommandList_CopyTextureRegion(command_list,
            &dst_location, 0, 16, 0, &src_location, &box);

    transition_resource_state(command_list, dst_buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);

    get_texture_readback_with_command_list(texture, 0, &rb, queue, command_list);
    for (y = 0; y < 8 / format_block_height(DXGI_FORMAT_BC2_UNORM); ++y)
    {
        for (x = 0; x < 8 / format_block_width(DXGI_FORMAT_BC2_UNORM); ++x)
        {
            if (x == 0 && y == 0)
                block_id = 33;
            else if (x == 1 && y == 0)
                block_id = 7;
            else if (x == 0 && y == 1)
                block_id = 192;
            else
                block_id = 132;

            expected.x = block_id << 8 | 0;
            expected.y = block_id << 8 | 1;
            expected.z = block_id << 8 | 2;
            expected.w = block_id << 8 | 3;
            got = *get_readback_uvec4(&rb, x, y);

            if (!compare_uvec4(&got, &expected))
                break;
        }
        if (!compare_uvec4(&got, &expected))
            break;
    }
    release_resource_readback(&rb);
    ok(compare_uvec4(&got, &expected),
            "Got {0x%08x, 0x%08x, 0x%08x, 0x%08x} at (%u, %u), expected {0x%08x, 0x%08x, 0x%08x, 0x%08x}.\n",
            got.x, got.y, got.z, got.w, x, y, expected.x, expected.y, expected.z, expected.w);

    reset_command_list(command_list, context.allocator);
    get_texture_readback_with_command_list(texture, 2, &rb, queue, command_list);
    block_id = 1;
    expected.x = block_id << 8 | 0;
    expected.y = block_id << 8 | 1;
    expected.z = block_id << 8 | 2;
    expected.w = block_id << 8 | 3;
    got = *get_readback_uvec4(&rb, 0, 0);
    release_resource_readback(&rb);
    ok(compare_uvec4(&got, &expected),
            "Got {0x%08x, 0x%08x, 0x%08x, 0x%08x}, expected {0x%08x, 0x%08x, 0x%08x, 0x%08x}.\n",
            got.x, got.y, got.z, got.w, expected.x, expected.y, expected.z, expected.w);

    reset_command_list(command_list, context.allocator);
    get_texture_readback_with_command_list(texture, 3, &rb, queue, command_list);
    block_id = 2;
    expected.x = block_id << 8 | 0;
    expected.y = block_id << 8 | 1;
    expected.z = block_id << 8 | 2;
    expected.w = block_id << 8 | 3;
    got = *get_readback_uvec4(&rb, 0, 0);
    release_resource_readback(&rb);
    ok(compare_uvec4(&got, &expected),
            "Got {0x%08x, 0x%08x, 0x%08x, 0x%08x}, expected {0x%08x, 0x%08x, 0x%08x, 0x%08x}.\n",
            got.x, got.y, got.z, got.w, expected.x, expected.y, expected.z, expected.w);

    reset_command_list(command_list, context.allocator);
    get_buffer_readback_with_command_list(dst_buffer, DXGI_FORMAT_R32_UINT, &rb, queue, command_list);
    for (y = 0; y < 24 / format_block_height(DXGI_FORMAT_BC2_UNORM); ++y)
    {
        unsigned int row_offset = dst_location.PlacedFootprint.Footprint.RowPitch / sizeof(got) * y;

        for (x = 0; x < 4 / format_block_width(DXGI_FORMAT_BC2_UNORM); ++x)
        {
            if (x == 0 && y % 2 == 0)
                block_id = 33;
            else if (x == 1 && y % 2 == 0)
                block_id = 7;
            else if (x == 0 && y % 2 == 1)
                block_id = 192;
            else
                block_id = 132;

            expected.x = block_id << 8 | 0;
            expected.y = block_id << 8 | 1;
            expected.z = block_id << 8 | 2;
            expected.w = block_id << 8 | 3;
            got = *get_readback_uvec4(&rb, x + row_offset, 0);

            if (!compare_uvec4(&got, &expected))
                break;
        }
        if (!compare_uvec4(&got, &expected))
            break;
    }
    release_resource_readback(&rb);
    ok(compare_uvec4(&got, &expected),
            "Got {0x%08x, 0x%08x, 0x%08x, 0x%08x} at (%u, %u), expected {0x%08x, 0x%08x, 0x%08x, 0x%08x}.\n",
            got.x, got.y, got.z, got.w, x, y, expected.x, expected.y, expected.z, expected.w);

    ID3D12Resource_Release(texture);
    ID3D12Resource_Release(src_buffer);
    ID3D12Resource_Release(dst_buffer);
    destroy_test_context(&context);
}

void test_multisample_resolve(void)
{
    D3D12_HEAP_PROPERTIES heap_properties;
    ID3D12Resource *ms_render_target_copy;
    D3D12_CPU_DESCRIPTOR_HANDLE ms_rtv;
    D3D12_RESOURCE_DESC resource_desc;
    ID3D12GraphicsCommandList1 *list1;
    ID3D12Resource *ms_render_target;
    ID3D12Resource *render_target;
    struct test_context_desc desc;
    struct test_context context;
    struct resource_readback rb;
    ID3D12DescriptorHeap *heap;
    D3D12_RECT src_rect;
    unsigned int x, y;
    HRESULT hr;

    static const uint32_t reference_color[4 * 4] = {
        0x01010101, 0x02020202, 0x03030303, 0x04040404,
        0x05050505, 0x06060606, 0x07070707, 0x08080808,
        0x09090909, 0x0a0a0a0a, 0x0b0b0b0b, 0x0c0c0c0c,
        0x0d0d0d0d, 0x0e0e0e0e, 0x0f0f0f0f, 0x10101010,
    };

    memset(&desc, 0, sizeof(desc));
    desc.no_root_signature = true;
    desc.no_render_target = true;
    if (!init_test_context(&context, &desc))
        return;

    hr = ID3D12GraphicsCommandList_QueryInterface(context.list, &IID_ID3D12GraphicsCommandList1, (void **)&list1);
    if (FAILED(hr))
    {
        destroy_test_context(&context);
        skip("Failed to query ID3D12GraphicsCommandList1. Skipping tests.\n");
        return;
    }

    heap = create_cpu_descriptor_heap(context.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);

    memset(&heap_properties, 0, sizeof(heap_properties));
    heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

    memset(&resource_desc, 0, sizeof(resource_desc));
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Width = 4 * 2;
    resource_desc.Height = 4;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    resource_desc.SampleDesc.Count = 4;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
        &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, &IID_ID3D12Resource, (void **)&ms_render_target);
    ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);
    resource_desc.Width = 4;
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
        &resource_desc, D3D12_RESOURCE_STATE_RESOLVE_DEST, NULL, &IID_ID3D12Resource, (void **)&ms_render_target_copy);
    ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Height = 8;
    hr = ID3D12Device_CreateCommittedResource(context.device, &heap_properties, D3D12_HEAP_FLAG_NONE,
        &resource_desc, D3D12_RESOURCE_STATE_RESOLVE_DEST, NULL, &IID_ID3D12Resource, (void **)&render_target);
    ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);

    ms_rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(heap);
    ID3D12Device_CreateRenderTargetView(context.device, ms_render_target, NULL, ms_rtv);

    for (y = 0; y < 4; y++)
    {
        for (x = 0; x < 4; x++)
        {
            const FLOAT float_color[4] = {
                (float)((reference_color[y * 4 + x] >> 0) & 0xff) / 255.0f,
                (float)((reference_color[y * 4 + x] >> 8) & 0xff) / 255.0f,
                (float)((reference_color[y * 4 + x] >> 16) & 0xff) / 255.0f,
                (float)((reference_color[y * 4 + x] >> 24) & 0xff) / 255.0f,
            };

            src_rect.left = x;
            src_rect.right = x + 1;
            src_rect.top = y;
            src_rect.bottom = y + 1;
            ID3D12GraphicsCommandList_ClearRenderTargetView(context.list, ms_rtv, float_color, 1, &src_rect);
        }
    }

    transition_resource_state(context.list, ms_render_target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);

    /* First, try decompress directly on the MSAA texture. Apparently, we have to be in RESOURCE_SOURCE here even if dst is used.
     * For us, this should be a no-op. */
    ID3D12GraphicsCommandList1_ResolveSubresourceRegion(list1, ms_render_target, 0, 0, 0, ms_render_target, 0, NULL, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_DECOMPRESS);

    /* Now, DECOMPRESS as in-place copy by decompressing to second half of the subresource. Here we'll have to use GENERAL layout. */
    src_rect.left = 0;
    src_rect.right = 4;
    src_rect.top = 0;
    src_rect.bottom = 4;
    ID3D12GraphicsCommandList1_ResolveSubresourceRegion(list1, ms_render_target, 0, 4, 0, ms_render_target, 0, &src_rect, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_DECOMPRESS);

    /* DECOMPRESS to other resource MSAA <-> MSAA. vkCmdCopyImage2KHR path. */
    ID3D12GraphicsCommandList1_ResolveSubresourceRegion(list1, ms_render_target_copy, 0, 0, 0, ms_render_target, 0, &src_rect, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_DECOMPRESS);
    transition_resource_state(context.list, ms_render_target_copy, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);

    for (y = 0; y < 4; y++)
    {
        for (x = 0; x < 4; x++)
        {
            src_rect.left = x;
            src_rect.right = x + 1;
            src_rect.top = y;
            src_rect.bottom = y + 1;
            /* Test partial resolve with offset. */
            ID3D12GraphicsCommandList1_ResolveSubresourceRegion(list1, render_target, 0, x, y + 4, ms_render_target_copy, 0, &src_rect, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE);
        }
    }
    transition_resource_state(context.list, render_target, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    get_texture_readback_with_command_list(render_target, 0, &rb, context.queue, context.list);
    for (y = 0; y < 4; y++)
    {
        for (x = 0; x < 4; x++)
        {
            uint32_t v, reference;
            v = get_readback_uint(&rb, x, y + 4, 0);
            reference = reference_color[y * 4 + x];
            ok(v == reference, "Pixel %u, %u failed, %x != %x.\n", x, y + 4, v, reference);
        }
    }
    release_resource_readback(&rb);

    ID3D12Resource_Release(ms_render_target_copy);
    ID3D12Resource_Release(ms_render_target);
    ID3D12Resource_Release(render_target);
    ID3D12DescriptorHeap_Release(heap);
    ID3D12GraphicsCommandList1_Release(list1);
    destroy_test_context(&context);
}

