/*
 * Copyright (c) 2022 Hans-Kristian Arntzen for Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

/* Reuses implementation in dxbc_container.cpp from RenderDoc. */

/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2019-2022 Baldur Karlsson
 * Copyright (c) 2014 Crytek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_SHADER

#include "vkd3d_shader_private.h"
#include "3rdparty/md5/md5.h"

#define DXBC_HASHABLE_PAYLOAD_OFFSET 20

void vkd3d_compute_dxbc_checksum(const void *dxbc, size_t size, uint32_t checksum[4])
{
    uint32_t leftover_length;
    uint32_t padding_bytes;
    const uint8_t *data;
    uint32_t block[16];
    uint32_t num_bits2;
    uint32_t num_bits;
    MD5_CTX md5_ctx;
    size_t length;

    /* This is always called from our code. */
    assert(size > DXBC_HASHABLE_PAYLOAD_OFFSET);

    memset(&md5_ctx, 0, sizeof(md5_ctx));
    memset(block, 0, sizeof(block));
    MD5_Init(&md5_ctx);

    data = (const uint8_t *)dxbc + DXBC_HASHABLE_PAYLOAD_OFFSET;
    length = size - DXBC_HASHABLE_PAYLOAD_OFFSET;

    num_bits = length * 8;
    num_bits2 = (num_bits >> 2) | 1;

    leftover_length = length % 64;
    MD5_Update(&md5_ctx, data, length - leftover_length);

    data += length - leftover_length;

    if (leftover_length >= 56)
    {
        MD5_Update(&md5_ctx, data, leftover_length);

        block[0] = 0x80;
        MD5_Update(&md5_ctx, block, 64 - leftover_length);

        block[0] = num_bits;
        block[15] = num_bits2;

        MD5_Update(&md5_ctx, block, 64);
    }
    else
    {
        MD5_Update(&md5_ctx, &num_bits, sizeof(num_bits));

        if (leftover_length)
            MD5_Update(&md5_ctx, data, leftover_length);

        padding_bytes = 64 - leftover_length - 4;
        block[0] = 0x80;
        memcpy((uint8_t *)block + padding_bytes - 4, &num_bits2, 4);
        MD5_Update(&md5_ctx, block, padding_bytes);
    }

    checksum[0] = md5_ctx.a;
    checksum[1] = md5_ctx.b;
    checksum[2] = md5_ctx.c;
    checksum[3] = md5_ctx.d;
}
