/*
 * Copyright 2016 Józef Kucia for CodeWeavers
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

    DXGI_FORMAT_R16G16B16A16_FLOAT    = 0x0a,

    DXGI_FORMAT_R32G32_FLOAT          = 0x10,

    DXGI_FORMAT_R10G10B10A2_UNORM     = 0x18,

    DXGI_FORMAT_R11G11B10_FLOAT       = 0x1a,
    DXGI_FORMAT_R8G8B8A8_TYPELESS     = 0x1b,
    DXGI_FORMAT_R8G8B8A8_UNORM        = 0x1c,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB   = 0x1d,

    DXGI_FORMAT_D32_FLOAT             = 0x28,
    DXGI_FORMAT_R32_FLOAT             = 0x29,
    DXGI_FORMAT_R32_UINT              = 0x2a,

    DXGI_FORMAT_R16_FLOAT             = 0x36,
    DXGI_FORMAT_D16_UNORM             = 0x37,
    DXGI_FORMAT_R16_UNORM             = 0x38,
    DXGI_FORMAT_R16_UINT              = 0x39,

    DXGI_FORMAT_R8_UNORM              = 0x3d,
    DXGI_FORMAT_R8_UINT               = 0x3e,

    DXGI_FORMAT_B8G8R8A8_UNORM        = 0x57,
    DXGI_FORMAT_B8G8R8X8_UNORM        = 0x58,

    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB   = 0x5b,

    DXGI_FORMAT_FORCE_DWORD           = 0xffffffff,
} DXGI_FORMAT;

#endif  /* __DXGI_BASE_H */
