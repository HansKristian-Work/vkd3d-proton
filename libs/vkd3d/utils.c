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

#include "vkd3d_private.h"

BOOL is_valid_feature_level(D3D_FEATURE_LEVEL feature_level)
{
    static const D3D_FEATURE_LEVEL valid_feature_levels[] =
    {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(valid_feature_levels); ++i)
    {
        if (valid_feature_levels[i] == feature_level)
            return TRUE;
    }

    return FALSE;
}

BOOL check_feature_level_support(D3D_FEATURE_LEVEL feature_level)
{
    return feature_level <= D3D_FEATURE_LEVEL_11_0;
}

HRESULT return_interface(IUnknown *iface, REFIID iface_riid,
        REFIID requested_riid, void **object)
{
    HRESULT hr;

    if (IsEqualGUID(iface_riid, requested_riid))
    {
        *object = iface;
        return S_OK;
    }

    hr = IUnknown_QueryInterface(iface, requested_riid, object);
    IUnknown_Release(iface);
    return hr;
}
