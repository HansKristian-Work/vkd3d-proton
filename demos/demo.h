/*
 * Copyright 2016 Henri Verbeet for CodeWeavers
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

/* Hack for MinGW-w64 headers.
 *
 * We want to use WIDL C inline wrappers because some methods
 * in D3D12 interfaces return aggregate objects. Unfortunately,
 * WIDL C inline wrappers are broken when used with MinGW-w64
 * headers because FORCEINLINE expands to extern inline
 * which leads to the "multiple storage classes in declaration
 * specifiers" compiler error.
 */
#ifdef __MINGW32__
#include <_mingw.h>
# ifdef __MINGW64_VERSION_MAJOR
#  undef __forceinline
#  define __forceinline __inline__ __attribute__((__always_inline__,__gnu_inline__))
# endif
#endif

#include <vkd3d_windows.h>
#define WIDL_C_INLINE_WRAPPERS
#define COBJMACROS
#include <d3d12.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))

struct demo_vec3
{
    float x, y, z;
};

struct demo_vec4
{
    float x, y, z, w;
};

struct demo_swapchain_desc
{
    unsigned int width;
    unsigned int height;
    unsigned int buffer_count;
    DXGI_FORMAT format;
};

static inline void demo_rasterizer_desc_init_default(D3D12_RASTERIZER_DESC *desc)
{
    desc->FillMode = D3D12_FILL_MODE_SOLID;
    desc->CullMode = D3D12_CULL_MODE_BACK;
    desc->FrontCounterClockwise = FALSE;
    desc->DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    desc->DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    desc->SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    desc->DepthClipEnable = TRUE;
    desc->MultisampleEnable = FALSE;
    desc->AntialiasedLineEnable = FALSE;
    desc->ForcedSampleCount = 0;
    desc->ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
}

static inline void demo_blend_desc_init_default(D3D12_BLEND_DESC *desc)
{
    static const D3D12_RENDER_TARGET_BLEND_DESC rt_blend_desc =
    {
        .BlendEnable = FALSE,
        .LogicOpEnable = FALSE,
        .SrcBlend = D3D12_BLEND_ONE,
        .DestBlend = D3D12_BLEND_ZERO,
        .BlendOp = D3D12_BLEND_OP_ADD,
        .SrcBlendAlpha = D3D12_BLEND_ONE,
        .DestBlendAlpha = D3D12_BLEND_ZERO,
        .BlendOpAlpha = D3D12_BLEND_OP_ADD,
        .LogicOp = D3D12_LOGIC_OP_NOOP,
        .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
    };
    unsigned int i;

    desc->AlphaToCoverageEnable = FALSE;
    desc->IndependentBlendEnable = FALSE;
    for (i = 0; i < ARRAY_SIZE(desc->RenderTarget); ++i)
    {
        desc->RenderTarget[i] = rt_blend_desc;
    }
}

#ifdef _WIN32
#include "demo_win32.h"
#else
#include <vkd3d_utils.h>
#include "demo_xcb.h"
#endif
