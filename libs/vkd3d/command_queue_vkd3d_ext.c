/*
 * * Copyright 2023 NVIDIA Corporation
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

static inline struct d3d12_command_queue *d3d12_command_queue_from_ID3D12CommandQueueExt(d3d12_command_queue_vkd3d_ext_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_command_queue, ID3D12CommandQueueExt_iface);
}

extern ULONG STDMETHODCALLTYPE d3d12_command_queue_AddRef(d3d12_command_queue_iface *iface);

ULONG STDMETHODCALLTYPE d3d12_command_queue_vkd3d_ext_AddRef(d3d12_command_queue_vkd3d_ext_iface *iface)
{
    struct d3d12_command_queue *command_queue = d3d12_command_queue_from_ID3D12CommandQueueExt(iface);
    return d3d12_command_queue_AddRef(&command_queue->ID3D12CommandQueue_iface);
}

extern ULONG STDMETHODCALLTYPE d3d12_command_queue_Release(d3d12_command_queue_iface *iface);

static ULONG STDMETHODCALLTYPE d3d12_command_queue_vkd3d_ext_Release(d3d12_command_queue_vkd3d_ext_iface *iface)
{
    struct d3d12_command_queue *command_queue = d3d12_command_queue_from_ID3D12CommandQueueExt(iface);
    return d3d12_command_queue_Release(&command_queue->ID3D12CommandQueue_iface);
}

extern HRESULT STDMETHODCALLTYPE d3d12_command_queue_QueryInterface(d3d12_command_queue_iface *iface,
        REFIID iid, void **object);

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_vkd3d_ext_QueryInterface(d3d12_command_queue_vkd3d_ext_iface *iface,
        REFIID iid, void **out)
{
    struct d3d12_command_queue *command_queue = d3d12_command_queue_from_ID3D12CommandQueueExt(iface);
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return d3d12_command_queue_QueryInterface(&command_queue->ID3D12CommandQueue_iface, iid, out);
}

static HRESULT STDMETHODCALLTYPE d3d12_command_queue_vkd3d_ext_NotifyOutOfBandCommandQueue(d3d12_command_queue_vkd3d_ext_iface *iface, D3D12_OUT_OF_BAND_CQ_TYPE type)
{
    struct d3d12_command_queue *command_queue;
    VkOutOfBandQueueTypeNV vk_queue_type;
    int i;

    command_queue = d3d12_command_queue_from_ID3D12CommandQueueExt(iface);

    if (!command_queue->device->vk_info.NV_low_latency2)
        return E_NOTIMPL;

    if (type != D3D12_OUT_OF_BAND_CQ_TYPE_RENDER && type != D3D12_OUT_OF_BAND_CQ_TYPE_PRESENT)
        return E_INVALIDARG;

    vk_queue_type = (VkOutOfBandQueueTypeNV)type;

    for (i = 0; i < VKD3D_QUEUE_FAMILY_COUNT; i++)
    {
        if (command_queue->device->queue_families[i]->vk_family_index == command_queue->vkd3d_queue->vk_family_index &&
                command_queue->device->queue_families[i]->out_of_band_queue)
        {
            struct vkd3d_queue *out_of_band_queue = command_queue->device->queue_families[i]->out_of_band_queue;

            if (command_queue->last_submission_timeline_value != 0)
                FIXME("Trying to assign a queue to be out of band, but it has been submitted to before. This may not work as intended.\n");

            /* Migrate the command queue to the out of band queue, so that it cannot conflict
             * with other queues from scheduling PoV.
             * The assumption is that NvAPI notifies before the queue has actually been used
             * for anything meaningful. */
            d3d12_device_unmap_vkd3d_queue(command_queue->vkd3d_queue, command_queue);
            pthread_mutex_lock(&out_of_band_queue->command_queue_mutex);
            {
                out_of_band_queue->virtual_queue_count++;
                vkd3d_array_reserve(
                        (void**)&out_of_band_queue->command_queues, &out_of_band_queue->command_queue_size,
                        out_of_band_queue->command_queue_count + 1,
                        sizeof(*out_of_band_queue->command_queues));
                out_of_band_queue->command_queues[out_of_band_queue->command_queue_count++] = command_queue;
            }
            pthread_mutex_unlock(&out_of_band_queue->command_queue_mutex);

            vkd3d_set_queue_out_of_band(command_queue->device, out_of_band_queue, vk_queue_type);
            command_queue->vkd3d_queue = out_of_band_queue;
            break;
        }
    }

    return S_OK;
}

CONST_VTBL struct ID3D12CommandQueueExtVtbl d3d12_command_queue_vkd3d_ext_vtbl =
{
    /* IUnknown methods */
    d3d12_command_queue_vkd3d_ext_QueryInterface,
    d3d12_command_queue_vkd3d_ext_AddRef,
    d3d12_command_queue_vkd3d_ext_Release,

    /* ID3D12CommandQueueExt methods */
    d3d12_command_queue_vkd3d_ext_NotifyOutOfBandCommandQueue
};

