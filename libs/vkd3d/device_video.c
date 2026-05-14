/*
 * Copyright 2026 Lauri Räsänen
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

static inline struct d3d12_device *d3d12_device_from_ID3D12VideoDevice(d3d12_video_device_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_device, ID3D12VideoDevice_iface);
}

extern HRESULT STDMETHODCALLTYPE d3d12_device_QueryInterface(d3d12_device_iface *iface,
        REFIID riid, void **object);

static HRESULT STDMETHODCALLTYPE d3d12_video_device_QueryInterface(d3d12_video_device_iface *iface,
        REFIID iid, void **out)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12VideoDevice(iface);
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return d3d12_device_QueryInterface(&device->ID3D12Device_iface, iid, out);
}

ULONG STDMETHODCALLTYPE d3d12_video_device_AddRef(d3d12_video_device_iface *iface)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12VideoDevice(iface);
    return d3d12_device_add_ref(device);
}

static ULONG STDMETHODCALLTYPE d3d12_video_device_Release(d3d12_video_device_iface *iface)
{
    struct d3d12_device *device = d3d12_device_from_ID3D12VideoDevice(iface);
    return d3d12_device_release(device);
}

static HRESULT STDMETHODCALLTYPE d3d12_video_device_CheckFeatureSupport(d3d12_video_device_iface *iface,
        D3D12_FEATURE_VIDEO feature, void *feature_data, UINT feature_data_size)
{
    FIXME("iface %p, feature %d, feature_data %p, feature_data_size %zu stub!\n", iface, feature, feature_data, feature_data_size);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_video_device_CreateVideoDecoder(d3d12_video_device_iface *iface,
        const D3D12_VIDEO_DECODER_DESC* pDesc, REFIID riid, void** ppVideoDecoder)
{
    FIXME("iface %p, pDesc %p, riid %s, ppVideoDecoder %p stub!\n", iface, pDesc, debugstr_guid(riid), ppVideoDecoder);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_video_device_CreateVideoDecoderHeap(d3d12_video_device_iface *iface,
        const D3D12_VIDEO_DECODER_HEAP_DESC* pVideoDecoderHeapDesc, REFIID riid, void** ppVideoDecoderHeap)
{
    FIXME("iface %p, pVideoDecoderHeapDesc %p, riid %s, ppVideoDecoderHeap %p stub!\n", iface, pVideoDecoderHeapDesc, debugstr_guid(riid), ppVideoDecoderHeap);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d3d12_video_device_CreateVideoProcessor(d3d12_video_device_iface *iface,
        UINT NodeMask, const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC* pOutputStreamDesc,
        UINT NumInputStreamDescs, const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC *pInputStreamDescs,
        REFIID riid, void **ppVideoProcessor)
{
    FIXME("iface %p, NodeMask %zu, pOutputStreamDesc %p, NumInputStreamDescs %zu, pInputStreamDescs %p, riid %s, ppVideoProcessor %p stub!\n", iface, NodeMask, pOutputStreamDesc, NumInputStreamDescs, pInputStreamDescs, debugstr_guid(riid), ppVideoProcessor);
    return E_NOTIMPL;
}

CONST_VTBL struct ID3D12VideoDeviceVtbl d3d12_video_device_vtbl =
{
    /* IUnknown methods */
    d3d12_video_device_QueryInterface,
    d3d12_video_device_AddRef,
    d3d12_video_device_Release,

    /* ID3DVideoDevice methods */
    d3d12_video_device_CheckFeatureSupport,
    d3d12_video_device_CreateVideoDecoder,
    d3d12_video_device_CreateVideoDecoderHeap,
    d3d12_video_device_CreateVideoProcessor
};
