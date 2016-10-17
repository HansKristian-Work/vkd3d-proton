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

#define INITGUID
#include "vkd3d_private.h"

HRESULT vkd3d_create_device(const struct vkd3d_device_create_info *create_info,
        REFIID riid, void **device)
{
    struct d3d12_device *object;
    HRESULT hr;

    TRACE("create_info %p, riid %s, device %p.\n", create_info, debugstr_guid(riid), device);

    if (create_info->minimum_feature_level < D3D_FEATURE_LEVEL_11_0
            || !is_valid_feature_level(create_info->minimum_feature_level))
    {
        WARN("Invalid feature level %#x.\n", create_info->minimum_feature_level);
        return E_INVALIDARG;
    }

    if (!check_feature_level_support(create_info->minimum_feature_level))
    {
        FIXME("Unsupported feature level %#x.\n", create_info->minimum_feature_level);
        return E_INVALIDARG;
    }

    if (FAILED(hr = d3d12_device_create(create_info, &object)))
        return hr;

    return return_interface((IUnknown *)&object->ID3D12Device_iface, &IID_ID3D12Device,
            riid, device);
}

