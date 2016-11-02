/*
 * Copyright 2016 Józef Kucia for CodeWeavers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __DXGI_BASE_H
#define __DXGI_BASE_H

#define DXGI_ERROR_INVALID_CALL   _HRESULT_TYPEDEF_(0x887A0001)
#define DXGI_ERROR_DEVICE_REMOVED _HRESULT_TYPEDEF_(0x887A0005)

typedef struct DXGI_SAMPLE_DESC
{
    UINT Count;
    UINT Quality;
} DXGI_SAMPLE_DESC;

typedef enum DXGI_FORMAT
{
    DXGI_FORMAT_UNKNOWN               = 0x00,
    DXGI_FORMAT_R32G32B32A32_TYPELESS = 0x01,
    DXGI_FORMAT_R32G32B32A32_FLOAT    = 0x02,
    DXGI_FORMAT_R32G32B32A32_UINT     = 0x03,
    DXGI_FORMAT_R32G32B32A32_SINT     = 0x04,
    DXGI_FORMAT_R32G32B32_TYPELESS    = 0x05,
    DXGI_FORMAT_R32G32B32_FLOAT       = 0x06,

    DXGI_FORMAT_R8G8B8A8_TYPELESS     = 0x1b,
    DXGI_FORMAT_R8G8B8A8_UNORM        = 0x1c,

    DXGI_FORMAT_D32_FLOAT             = 0x28,
    DXGI_FORMAT_R32_FLOAT             = 0x29,

    DXGI_FORMAT_B8G8R8A8_UNORM        = 0x57,
    DXGI_FORMAT_B8G8R8X8_UNORM        = 0x58,

    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB   = 0x5b,

    DXGI_FORMAT_FORCE_DWORD           = 0xffffffff,
} DXGI_FORMAT;

#endif  /* __DXGI_BASE_H */
