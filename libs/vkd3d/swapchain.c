/*
 * Copyright 2022 Hans-Kristian Arntzen for Valve Corporation
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

#ifdef _WIN32
#include "vkd3d_win32.h"
#endif
#include "vkd3d_private.h"

static inline struct dxgi_vk_swap_chain_factory *impl_from_IDXGIVkSwapChainFactory(IDXGIVkSwapChainFactory *iface)
{
    return CONTAINING_RECORD(iface, struct dxgi_vk_swap_chain_factory, IDXGIVkSwapChainFactory_iface);
}

static ULONG STDMETHODCALLTYPE dxgi_vk_swap_chain_factory_AddRef(IDXGIVkSwapChainFactory *iface)
{
    struct dxgi_vk_swap_chain_factory *chain = impl_from_IDXGIVkSwapChainFactory(iface);
    TRACE("iface %p\n", iface);
    return ID3D12CommandQueue_AddRef(&chain->queue->ID3D12CommandQueue_iface);
}

static ULONG STDMETHODCALLTYPE dxgi_vk_swap_chain_factory_Release(IDXGIVkSwapChainFactory *iface)
{
    struct dxgi_vk_swap_chain_factory *chain = impl_from_IDXGIVkSwapChainFactory(iface);
    TRACE("iface %p\n", iface);
    return ID3D12CommandQueue_Release(&chain->queue->ID3D12CommandQueue_iface);
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_factory_QueryInterface(IDXGIVkSwapChainFactory *iface, REFIID riid, void **object)
{
    struct dxgi_vk_swap_chain_factory *chain = impl_from_IDXGIVkSwapChainFactory(iface);
    TRACE("iface %p\n", iface);
    return ID3D12CommandQueue_QueryInterface(&chain->queue->ID3D12CommandQueue_iface, riid, object);
}

struct dxgi_vk_swap_chain_present_request
{
    uint64_t begin_frame_time_ns;
    uint32_t user_index;
    uint32_t target_min_image_count;
    DXGI_FORMAT dxgi_format;
    DXGI_COLOR_SPACE_TYPE dxgi_color_space_type;
    DXGI_VK_HDR_METADATA dxgi_hdr_metadata;
    uint32_t swap_interval;
    bool modifies_hdr_metadata;
};

struct present_wait_entry
{
    uint64_t id;
    uint64_t begin_frame_time_ns;
};

struct dxgi_vk_swap_chain
{
    IDXGIVkSwapChain IDXGIVkSwapChain_iface;
    struct d3d12_command_queue *queue;

    LONG refcount;
    DXGI_SWAP_CHAIN_DESC1 desc;

    vkd3d_native_sync_handle frame_latency_event;
    vkd3d_native_sync_handle frame_latency_event_internal;
    vkd3d_native_sync_handle present_request_done_event;
    bool outstanding_present_request;

    UINT frame_latency;
    UINT frame_latency_internal;
    bool frame_latency_internal_is_static;
    VkSurfaceKHR vk_surface;

    bool debug_latency;

    struct
    {
        /* When resizing user buffers or emit commands internally,
         * we need to make sure all pending blits have completed on GPU. */
        VkSemaphore vk_internal_blit_semaphore;
        VkSemaphore vk_complete_semaphore;
        uint64_t internal_blit_count;
        uint64_t complete_count;

        /* PresentID or frame latency fence is used depending on features and if we're really presenting on-screen. */
        ID3D12Fence1 *frame_latency_fence;
        uint64_t frame_latency_count;
        uint64_t present_id;
        bool present_id_valid;

        /* Atomically updated after a PRESENT queue command has processed.
         * We don't care about wrap around.
         * We just care about equality check so we can atomically check if all outstanding present events have completed on CPU timeline.
         * This is used to implement occlusion check. */
        uint32_t present_count;

        /* For blits. Use simple VkFences since we have to use binary semaphores with WSI release anyways.
         * We don't need to wait on these fences on main thread. */
        VkCommandPool vk_blit_command_pool;
        VkCommandBuffer vk_blit_command_buffers[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        uint64_t backbuffer_blit_timelines[DXGI_MAX_SWAP_CHAIN_BUFFERS];

        VkSwapchainKHR vk_swapchain;
        VkImage vk_backbuffer_images[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        VkImageView vk_backbuffer_image_views[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        VkSemaphore vk_release_semaphores[DXGI_MAX_SWAP_CHAIN_BUFFERS];

        VkSemaphore vk_acquire_semaphore[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        bool acquire_semaphore_signalled[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        uint64_t acquire_semaphore_consumed_at_blit[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        uint32_t acquire_semaphore_index;

        uint32_t current_backbuffer_index;
        uint32_t backbuffer_width;
        uint32_t backbuffer_height;
        uint32_t backbuffer_count;
        VkFormat backbuffer_format;

        struct vkd3d_swapchain_info pipeline;

        uint32_t is_occlusion_state; /* Updated atomically. */

        /* State tracking in present tasks on how to deal with swapchain recreation. */
        bool force_swapchain_recreation;
        bool is_surface_lost;
    } present;

    struct dxgi_vk_swap_chain_present_request request, request_ring[DXGI_MAX_SWAP_CHAIN_BUFFERS];

    struct
    {
        struct d3d12_resource *backbuffers[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        VkImageView vk_image_views[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        uint64_t blit_count;
        uint32_t present_count;
        UINT index;

        DXGI_COLOR_SPACE_TYPE dxgi_color_space_type;
        DXGI_VK_HDR_METADATA dxgi_hdr_metadata;
        bool modifies_hdr_metadata;
        uint64_t begin_frame_time_ns;
    } user;

    struct
    {
        VkSurfaceFormatKHR *formats;
		size_t formats_size;
        uint32_t format_count;
    } properties;

    /* If present_wait is supported. */
    struct
    {
        pthread_t thread;
        struct present_wait_entry *wait_queue;
        size_t wait_queue_size;
        size_t wait_queue_count;
        pthread_cond_t cond;
        pthread_mutex_t lock;
        bool active;
        bool skip_waits;
    } wait_thread;
};

static void dxgi_vk_swap_chain_drain_internal_blit_semaphore(struct dxgi_vk_swap_chain *chain, uint64_t value);

static void dxgi_vk_swap_chain_wait_acquire_semaphore(struct dxgi_vk_swap_chain *chain,
        VkSemaphore vk_semaphore, bool blocking)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkSemaphoreSubmitInfo signal_info;
    VkSemaphoreSubmitInfo wait_info;
    VkSubmitInfo2 submit_info;
    VkQueue vk_queue;
    VkResult vr;

    memset(&submit_info, 0, sizeof(submit_info));
    memset(&wait_info, 0, sizeof(wait_info));
    memset(&signal_info, 0, sizeof(signal_info));
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.waitSemaphoreInfoCount = 1;
    submit_info.pWaitSemaphoreInfos = &wait_info;

    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_info.semaphore = vk_semaphore;
    wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    submit_info.pSignalSemaphoreInfos = &signal_info;
    submit_info.signalSemaphoreInfoCount = 1;
    signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info.semaphore = chain->present.vk_internal_blit_semaphore;
    signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signal_info.value = ++chain->present.internal_blit_count;

    vk_queue = vkd3d_queue_acquire(chain->queue->vkd3d_queue);
    vr = VK_CALL(vkQueueSubmit2(vk_queue, 1, &submit_info, VK_NULL_HANDLE));
    if (vr < 0)
    {
        ERR("Failed to submit, vr %d\n", vr);
        VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(chain->queue->device, vr == VK_ERROR_DEVICE_LOST);
    }
    vkd3d_queue_release(chain->queue->vkd3d_queue);

    if (vr == VK_SUCCESS && blocking)
        dxgi_vk_swap_chain_drain_internal_blit_semaphore(chain, chain->present.internal_blit_count);
}

static void dxgi_vk_swap_chain_drain_queue(struct dxgi_vk_swap_chain *chain)
{
    unsigned int i;

    /* This functions as a DRAIN of the D3D12 queue.
     * All CPU operations that were queued must have been submitted to Vulkan now.
     * We intend to submit directly to the queue now and timeline signals must come in the proper order. */
    if (vkd3d_acquire_vk_queue(&chain->queue->ID3D12CommandQueue_iface))
        vkd3d_release_vk_queue(&chain->queue->ID3D12CommandQueue_iface);

    /* If we have a lingering semaphore acquire that never went anywhere, ensure it is waited on. */
    for (i = 0; i < ARRAY_SIZE(chain->present.vk_acquire_semaphore); i++)
        if (chain->present.vk_acquire_semaphore[i] && chain->present.acquire_semaphore_signalled[i])
            dxgi_vk_swap_chain_wait_acquire_semaphore(chain, chain->present.vk_acquire_semaphore[i], false);

    /* Ensures that all pending ReleaseSemaphore() calls are also made.
     * This happens on the fence waiter queues, so it's not enough to call vkQueueWaitIdle to be 100% sure.
     * The fence waiter thread processes requests in-order,
     * so if we observe that an EVENT has been signalled,
     * we know all pending semaphore signals have happened as well. */

    chain->present.frame_latency_count += 1;
    d3d12_command_queue_signal_inline(chain->queue, chain->present.frame_latency_fence, chain->present.frame_latency_count);
    d3d12_fence_set_event_on_completion(impl_from_ID3D12Fence1(chain->present.frame_latency_fence),
            chain->present.frame_latency_count, NULL);
}

static void dxgi_vk_swap_chain_wait_semaphore(struct dxgi_vk_swap_chain *chain,
        VkSemaphore vk_timeline, uint64_t value)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkSemaphoreWaitInfo wait_info;
    VkResult vr;

    if (!value)
        return;

    memset(&wait_info, 0, sizeof(wait_info));
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait_info.pSemaphores = &vk_timeline;
    wait_info.pValues = &value;
    wait_info.semaphoreCount = 1;
    vr = VK_CALL(vkWaitSemaphores(chain->queue->device->vk_device, &wait_info, UINT64_MAX));
    if (vr)
        ERR("Failed to wait for present semaphore, vr %d.\n", vr);
}

static void dxgi_vk_swap_chain_drain_complete_semaphore(struct dxgi_vk_swap_chain *chain, uint64_t value)
{
    dxgi_vk_swap_chain_wait_semaphore(chain, chain->present.vk_complete_semaphore, value);
}

static void dxgi_vk_swap_chain_drain_internal_blit_semaphore(struct dxgi_vk_swap_chain *chain, uint64_t value)
{
    dxgi_vk_swap_chain_wait_semaphore(chain, chain->present.vk_internal_blit_semaphore, value);
}

static void dxgi_vk_swap_chain_drain_user_images(struct dxgi_vk_swap_chain *chain)
{
    dxgi_vk_swap_chain_drain_complete_semaphore(chain, chain->user.blit_count);
}

static void dxgi_vk_swap_chain_push_present_id(struct dxgi_vk_swap_chain *chain, uint64_t present_id, uint64_t begin_frame_time_ns)
{
    struct present_wait_entry *entry;
    pthread_mutex_lock(&chain->wait_thread.lock);
    vkd3d_array_reserve((void **)&chain->wait_thread.wait_queue, &chain->wait_thread.wait_queue_size,
            chain->wait_thread.wait_queue_count + 1, sizeof(*chain->wait_thread.wait_queue));
    entry = &chain->wait_thread.wait_queue[chain->wait_thread.wait_queue_count++];
    entry->id = present_id;
    entry->begin_frame_time_ns = begin_frame_time_ns;
    pthread_cond_signal(&chain->wait_thread.cond);
    pthread_mutex_unlock(&chain->wait_thread.lock);
}

static void dxgi_vk_swap_chain_cleanup(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    UINT i;

    if (chain->wait_thread.active)
    {
        dxgi_vk_swap_chain_push_present_id(chain, 0, 0);
        pthread_join(chain->wait_thread.thread, NULL);
        pthread_mutex_destroy(&chain->wait_thread.lock);
        pthread_cond_destroy(&chain->wait_thread.cond);
    }
    vkd3d_free(chain->wait_thread.wait_queue);

    if (chain->present.frame_latency_fence)
        ID3D12Fence1_Release(chain->present.frame_latency_fence);

    vkd3d_native_sync_handle_destroy(chain->frame_latency_event);
    vkd3d_native_sync_handle_destroy(chain->frame_latency_event_internal);

    if (chain->outstanding_present_request)
    {
        vkd3d_native_sync_handle_acquire(chain->present_request_done_event);
        chain->outstanding_present_request = false;
    }
    vkd3d_native_sync_handle_destroy(chain->present_request_done_event);

    VK_CALL(vkDestroySemaphore(chain->queue->device->vk_device, chain->present.vk_internal_blit_semaphore, NULL));
    VK_CALL(vkDestroySemaphore(chain->queue->device->vk_device, chain->present.vk_complete_semaphore, NULL));
    VK_CALL(vkDestroyCommandPool(chain->queue->device->vk_device, chain->present.vk_blit_command_pool, NULL));
    for (i = 0; i < ARRAY_SIZE(chain->present.vk_release_semaphores); i++)
        VK_CALL(vkDestroySemaphore(chain->queue->device->vk_device, chain->present.vk_release_semaphores[i], NULL));
    for (i = 0; i < ARRAY_SIZE(chain->present.vk_backbuffer_image_views); i++)
        VK_CALL(vkDestroyImageView(chain->queue->device->vk_device, chain->present.vk_backbuffer_image_views[i], NULL));
    for (i = 0; i < ARRAY_SIZE(chain->present.vk_acquire_semaphore); i++)
        VK_CALL(vkDestroySemaphore(chain->queue->device->vk_device, chain->present.vk_acquire_semaphore[i], NULL));

    VK_CALL(vkDestroySwapchainKHR(chain->queue->device->vk_device, chain->present.vk_swapchain, NULL));

    for (i = 0; i < ARRAY_SIZE(chain->user.backbuffers); i++)
    {
        if (chain->user.backbuffers[i])
            vkd3d_resource_decref((ID3D12Resource *)&chain->user.backbuffers[i]->ID3D12Resource_iface);
        VK_CALL(vkDestroyImageView(chain->queue->device->vk_device, chain->user.vk_image_views[i], NULL));
    }

    vkd3d_free(chain->properties.formats);

    VK_CALL(vkDestroySurfaceKHR(chain->queue->device->vkd3d_instance->vk_instance,
            chain->vk_surface, NULL));
}

static inline struct dxgi_vk_swap_chain *impl_from_IDXGIVkSwapChain(IDXGIVkSwapChain *iface)
{
    return CONTAINING_RECORD(iface, struct dxgi_vk_swap_chain, IDXGIVkSwapChain_iface);
}

static ULONG STDMETHODCALLTYPE dxgi_vk_swap_chain_AddRef(IDXGIVkSwapChain *iface)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    UINT refcount = InterlockedIncrement(&chain->refcount);
    TRACE("iface %p, refcount %u\n", iface, refcount);
    return refcount;
}

static ULONG STDMETHODCALLTYPE dxgi_vk_swap_chain_Release(IDXGIVkSwapChain *iface)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    struct d3d12_command_queue *queue = chain->queue;
    UINT refcount;

    refcount = InterlockedDecrement(&chain->refcount);
    TRACE("iface %p, refcount %u\n", iface, refcount);

    if (!refcount)
    {
        dxgi_vk_swap_chain_drain_queue(chain);
        dxgi_vk_swap_chain_cleanup(chain);
        vkd3d_free(chain);
        ID3D12CommandQueue_Release(&queue->ID3D12CommandQueue_iface);
    }
    return refcount;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_QueryInterface(IDXGIVkSwapChain *iface, REFIID riid, void **object)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p\n", iface);
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDXGIVkSwapChain))
    {
        dxgi_vk_swap_chain_AddRef(&chain->IDXGIVkSwapChain_iface);
        *object = iface;
        return S_OK;
    }

    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_GetDesc(IDXGIVkSwapChain *iface, DXGI_SWAP_CHAIN_DESC1 *pDesc)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p, pDesc %p\n", iface, pDesc);
    *pDesc = chain->desc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_GetAdapter(IDXGIVkSwapChain *iface, REFIID riid, void **object)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p\n", iface);
    return IUnknown_QueryInterface(chain->queue->device->parent, riid, object);
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_GetDevice(IDXGIVkSwapChain *iface, REFIID riid, void **object)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p\n", iface);
    return ID3D12Device12_QueryInterface(&chain->queue->device->ID3D12Device_iface, riid, object);
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_GetImage(IDXGIVkSwapChain *iface, UINT BufferId, REFIID riid, void **object)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p, BufferId %u.\n", iface, BufferId);
    if (BufferId >= chain->desc.BufferCount)
        return E_INVALIDARG;
    return ID3D12Resource2_QueryInterface(&chain->user.backbuffers[BufferId]->ID3D12Resource_iface, riid, object);
}

static UINT STDMETHODCALLTYPE dxgi_vk_swap_chain_GetImageIndex(IDXGIVkSwapChain *iface)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p.\n", iface);
    return chain->user.index;
}

static UINT STDMETHODCALLTYPE dxgi_vk_swap_chain_GetFrameLatency(IDXGIVkSwapChain *iface)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p.\n", iface);
    return chain->frame_latency;
}

static HANDLE STDMETHODCALLTYPE dxgi_vk_swap_chain_GetFrameLatencyEvent(IDXGIVkSwapChain *iface)
{
    struct dxgi_vk_swap_chain *swapchain = impl_from_IDXGIVkSwapChain(iface);
    HANDLE duplicated_handle;
    VKD3D_UNUSED int fd;

    TRACE("iface %p.\n", iface);

    if (!vkd3d_native_sync_handle_is_valid(swapchain->frame_latency_event))
        return NULL;

#ifdef _WIN32
    /* Based on observation, this handle can be waited on, but ReleaseSemaphore() is not allowed.
     * Verified that NtQueryObject returns 0x100000 access mask (SYNCHRONIZE only). */
    if (!DuplicateHandle(GetCurrentProcess(), swapchain->frame_latency_event.handle,
            GetCurrentProcess(), &duplicated_handle,
            SYNCHRONIZE, FALSE, 0))
    {
        ERR("Failed to duplicate waitable handle.\n");
        return NULL;
    }
#else
    /* Ensure that we don't return fd 0 which would confuse the caller. */
    fd = dup(swapchain->frame_latency_event.fd);
    if (fd == 0)
    {
        fd = dup(fd);
        close(fd);
    }

    duplicated_handle = fd >= 0 ? (HANDLE)(intptr_t)fd : NULL;
#endif

    return duplicated_handle;
}

static HRESULT dxgi_vk_swap_chain_allocate_user_buffer(struct dxgi_vk_swap_chain *chain,
        const DXGI_SWAP_CHAIN_DESC1 *pDesc, struct d3d12_resource **ppResource)
{
    struct d3d12_device *device = chain->queue->device;
    D3D12_RESOURCE_DESC1 resource_desc;
    D3D12_HEAP_PROPERTIES heap_props;

    memset(&resource_desc, 0, sizeof(resource_desc));
    memset(&heap_props, 0, sizeof(heap_props));

    resource_desc.Width = pDesc->Width;
    resource_desc.Height = pDesc->Height;
    resource_desc.Format = pDesc->Format;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.MipLevels = 1;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap_props.CreationNodeMask = 1;
    heap_props.VisibleNodeMask = 1;

    return d3d12_resource_create_committed(device, &resource_desc, &heap_props, D3D12_HEAP_FLAG_NONE,
            D3D12_RESOURCE_STATE_PRESENT, NULL, 0, NULL, NULL, ppResource);
}

static HRESULT dxgi_vk_swap_chain_reallocate_user_buffers(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    struct d3d12_resource *old_resources[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    VkImageViewCreateInfo view_info;
    unsigned int i;
    VkResult vr;
    HRESULT hr;

    if (chain->desc.BufferCount > DXGI_MAX_SWAP_CHAIN_BUFFERS)
        return E_INVALIDARG;

    for (i = 0; i < DXGI_MAX_SWAP_CHAIN_BUFFERS; i++)
    {
        old_resources[i] = chain->user.backbuffers[i];
        chain->user.backbuffers[i] = NULL;
        VK_CALL(vkDestroyImageView(chain->queue->device->vk_device, chain->user.vk_image_views[i], NULL));
        chain->user.vk_image_views[i] = VK_NULL_HANDLE;
    }

    memset(&view_info, 0, sizeof(view_info));
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    for (i = 0; i < chain->desc.BufferCount; i++)
    {
        if (FAILED(hr = dxgi_vk_swap_chain_allocate_user_buffer(chain, &chain->desc, &chain->user.backbuffers[i])))
            goto err;

        /* We need to hold a private reference to the resource, not a public one. */
        vkd3d_resource_incref((ID3D12Resource *)&chain->user.backbuffers[i]->ID3D12Resource_iface);
        ID3D12Resource2_Release(&chain->user.backbuffers[i]->ID3D12Resource_iface);

        view_info.format = chain->user.backbuffers[i]->format->vk_format;
        view_info.image = chain->user.backbuffers[i]->res.vk_image;
        vr = VK_CALL(vkCreateImageView(chain->queue->device->vk_device, &view_info, NULL, &chain->user.vk_image_views[i]));
        if (vr < 0)
        {
            ERR("Failed to create image view for user image %u.\n", i);
            hr = E_OUTOFMEMORY;
            goto err;
        }
    }

    for (i = 0; i < DXGI_MAX_SWAP_CHAIN_BUFFERS; i++)
        if (old_resources[i])
            vkd3d_resource_decref((ID3D12Resource *)&old_resources[i]->ID3D12Resource_iface);

    return S_OK;

err:
    for (i = 0; i < DXGI_MAX_SWAP_CHAIN_BUFFERS; i++)
    {
        if (chain->user.backbuffers[i])
            vkd3d_resource_decref((ID3D12Resource *)&chain->user.backbuffers[i]->ID3D12Resource_iface);
        chain->user.backbuffers[i] = old_resources[i];
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_ChangeProperties(IDXGIVkSwapChain *iface, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
        const UINT *pNodeMasks, IUnknown *const *ppPresentQueues)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    DXGI_SWAP_CHAIN_DESC1 old_desc = chain->desc;
    HRESULT hr;
    UINT i;

    TRACE("iface %p, pDesc %p, pNodeMasks %p, ppPresentQueues %p!\n", iface, pDesc, pNodeMasks, ppPresentQueues);

    /* TODO: Validate pNodeMasks and ppPresentQueues. */

    /* Public ref-counts must be 0 for this to be allowed. */
    for (i = 0; i < chain->desc.BufferCount; i++)
        if (chain->user.backbuffers[i]->refcount != 0)
            return DXGI_ERROR_INVALID_CALL;

    chain->desc = *pDesc;

    /* Don't do anything in this case. */
    if (old_desc.Width == chain->desc.Width &&
            old_desc.Height == chain->desc.Height &&
            old_desc.BufferCount == chain->desc.BufferCount &&
            old_desc.Format == chain->desc.Format &&
            old_desc.Flags == chain->desc.Flags)
    {
        return S_OK;
    }

    /* Waits for any outstanding present event to complete, including the work it takes to blit to screen. */
    dxgi_vk_swap_chain_drain_user_images(chain);

    INFO("Reallocating swapchain (%u x %u), BufferCount = %u.\n",
            chain->desc.Width, chain->desc.Height, chain->desc.BufferCount);

    if (FAILED(hr = dxgi_vk_swap_chain_reallocate_user_buffers(chain)))
    {
        chain->desc = old_desc;
        return hr;
    }

    /* If BufferCount changes, so does expectations about latency. */
    if (vkd3d_native_sync_handle_is_valid(chain->frame_latency_event_internal) &&
            !chain->frame_latency_internal_is_static)
    {
        if (chain->desc.BufferCount > chain->frame_latency_internal)
        {
            vkd3d_native_sync_handle_release(chain->frame_latency_event_internal,
                    chain->desc.BufferCount - chain->frame_latency_internal);
            chain->frame_latency_internal = chain->desc.BufferCount;
        }
        else
        {
            while (chain->frame_latency_internal > chain->desc.BufferCount)
            {
                vkd3d_native_sync_handle_acquire(chain->frame_latency_event_internal);
                chain->frame_latency_internal--;
            }
        }
    }

    if (chain->user.index >= chain->desc.BufferCount)
    {
        /* Need to reset the user index in case the buffer count is lowered.
         * It is unclear if we're allowed to always reset, but employ principle of least surprise. */
        chain->user.index = 0;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_SetPresentRegion(IDXGIVkSwapChain *iface, const RECT *pRegion)
{
    FIXME("iface %p, pRegion %p stub!\n", iface, pRegion);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_SetGammaControl(IDXGIVkSwapChain *iface, UINT NumControlPoints, const DXGI_RGB *pControlPoints)
{
    FIXME("iface %p, NumControlPoints %u, pControlPoints %p stub!\n", iface, NumControlPoints, pControlPoints);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_SetFrameLatency(IDXGIVkSwapChain *iface, UINT MaxLatency)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p, MaxLatency %u.\n", iface, MaxLatency);

    if (!MaxLatency || MaxLatency > DXGI_MAX_SWAP_CHAIN_BUFFERS)
    {
        WARN("Invalid maximum frame latency %u.\n", MaxLatency);
        return DXGI_ERROR_INVALID_CALL;
    }

    /* Max frame latency without WAITABLE_OBJECT is always 3,
     * even if set on the device, according to docs. */
    if (!(chain->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
    {
        WARN("DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT not set for swap chain %p.\n", iface);
        return DXGI_ERROR_INVALID_CALL;
    }

    /* Only increasing the latency is handled here; apparently it is
     * the application's responsibility to reduce the semaphore value
     * in case the latency gets reduced. */
    if (MaxLatency > chain->frame_latency)
        vkd3d_native_sync_handle_release(chain->frame_latency_event, MaxLatency - chain->frame_latency);

    chain->frame_latency = MaxLatency;
    return S_OK;
}

static VkXYColorEXT convert_xy_color(UINT16 *dxgi_color)
{
    return (VkXYColorEXT) { dxgi_color[0] / 50000.0f, dxgi_color[1] / 50000.0f };
}

static float convert_max_luminance(UINT dxgi_luminance)
{
    /* The documentation says this is in *whole* nits, but this
     * contradicts the HEVC standard it claims to mirror,
     * and the sample's behaviour.
     * We should come back and validate this once
     * https://github.com/microsoft/DirectX-Graphics-Samples/issues/796
     * has an answer. */
    return (float)dxgi_luminance;
}

static float convert_min_luminance(UINT dxgi_luminance)
{
    return dxgi_luminance * 0.0001f;
}

static float convert_level(UINT16 dxgi_level)
{
    return (float)dxgi_level;
}

static VkHdrMetadataEXT convert_hdr_metadata_hdr10(DXGI_HDR_METADATA_HDR10 *dxgi_metadata)
{
    VkHdrMetadataEXT vulkan_metadata = { VK_STRUCTURE_TYPE_HDR_METADATA_EXT };
    vulkan_metadata.displayPrimaryRed = convert_xy_color(dxgi_metadata->RedPrimary);
    vulkan_metadata.displayPrimaryGreen = convert_xy_color(dxgi_metadata->GreenPrimary);
    vulkan_metadata.displayPrimaryBlue = convert_xy_color(dxgi_metadata->BluePrimary);
    vulkan_metadata.whitePoint = convert_xy_color(dxgi_metadata->WhitePoint);
    vulkan_metadata.maxLuminance = convert_max_luminance(dxgi_metadata->MaxMasteringLuminance);
    vulkan_metadata.minLuminance = convert_min_luminance(dxgi_metadata->MinMasteringLuminance);
    vulkan_metadata.maxContentLightLevel = convert_level(dxgi_metadata->MaxContentLightLevel);
    vulkan_metadata.maxFrameAverageLightLevel = convert_level(dxgi_metadata->MaxFrameAverageLightLevel);
    return vulkan_metadata;
}

static void dxgi_vk_swap_chain_set_hdr_metadata(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkHdrMetadataEXT hdr_metadata;

    if (!chain->queue->device->vk_info.EXT_hdr_metadata ||
            !chain->present.vk_swapchain ||
            chain->request.dxgi_hdr_metadata.Type != DXGI_HDR_METADATA_TYPE_HDR10)
    {
        return;
    }

    hdr_metadata = convert_hdr_metadata_hdr10(&chain->request.dxgi_hdr_metadata.HDR10);
    VK_CALL(vkSetHdrMetadataEXT(chain->queue->device->vk_device, 1, &chain->present.vk_swapchain, &hdr_metadata));
}

static bool dxgi_vk_swap_chain_present_task_is_idle(struct dxgi_vk_swap_chain *chain)
{
    uint32_t presented_count = vkd3d_atomic_uint32_load_explicit(&chain->present.present_count, vkd3d_memory_order_acquire);
    return presented_count == chain->user.present_count;
}

static bool dxgi_vk_swap_chain_is_occluded(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkPhysicalDevice vk_physical_device = chain->queue->device->vk_physical_device;
    VkSurfaceCapabilitiesKHR surface_caps;

    VK_CALL(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_physical_device, chain->vk_surface, &surface_caps));
    /* Win32 jank, when these are 0 we cannot create a swapchain. */
    return surface_caps.maxImageExtent.width == 0 || surface_caps.maxImageExtent.height == 0;
}

static bool dxgi_vk_swap_chain_present_is_occluded(struct dxgi_vk_swap_chain *chain)
{
    if (dxgi_vk_swap_chain_present_task_is_idle(chain))
    {
        /* Query the surface directly. */
        chain->present.is_occlusion_state = dxgi_vk_swap_chain_is_occluded(chain);
        return chain->present.is_occlusion_state != 0;
    }
    else
    {
        /* If presentation requests are pending it is not safe to access the surface directly
         * without adding tons of locks everywhere,
         * so rely on observed behavior from presentation thread. */
        return vkd3d_atomic_uint32_load_explicit(&chain->present.is_occlusion_state, vkd3d_memory_order_relaxed) != 0;
    }
}

static void dxgi_vk_swap_chain_present_callback(void *chain);

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_Present(IDXGIVkSwapChain *iface, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    struct dxgi_vk_swap_chain_present_request *request;
    TRACE("iface %p, SyncInterval %u, PresentFlags #%x, pPresentParameters %p.\n",
            iface, SyncInterval, PresentFlags, pPresentParameters);
    (void)pPresentParameters;

    if (dxgi_vk_swap_chain_present_is_occluded(chain))
        return DXGI_STATUS_OCCLUDED;
    if (PresentFlags & DXGI_PRESENT_TEST)
        return S_OK;

    /* If we missed the event signal last frame, we have to wait for it now.
     * Otherwise, we end up in a floating state where our waits and thread signals might not stay in sync anymore. */
    if (chain->outstanding_present_request)
    {
        vkd3d_native_sync_handle_acquire(chain->present_request_done_event);
        chain->outstanding_present_request = false;
    }

    assert(chain->user.index < chain->desc.BufferCount);

    /* The present iteration on present thread has a similar counter and it will pick up the request from the ring. */
    chain->user.present_count += 1;
    request = &chain->request_ring[chain->user.present_count % ARRAY_SIZE(chain->request_ring)];

    request->swap_interval = SyncInterval;
    request->dxgi_format = chain->user.backbuffers[chain->user.index]->desc.Format;
    request->user_index = chain->user.index;
    /* If we don't have wait thread, BufferCount needs to be tweaked to control latency.
     * Some games that create BufferCount = 3 swapchains expect us to absorb a lot of latency or we start
     * starving. If we have waiter thread, we'll block elsewhere. */
    request->target_min_image_count = chain->wait_thread.active ? 0 : chain->desc.BufferCount + 1;
    request->dxgi_color_space_type = chain->user.dxgi_color_space_type;
    request->dxgi_hdr_metadata = chain->user.dxgi_hdr_metadata;
    request->modifies_hdr_metadata = chain->user.modifies_hdr_metadata;
    request->begin_frame_time_ns = chain->user.begin_frame_time_ns;
    chain->user.modifies_hdr_metadata = false;

    /* Need to process this task in queue thread to deal with wait-before-signal.
     * All interesting works happens in the callback. */
    chain->user.blit_count += 1;
    d3d12_command_queue_enqueue_callback(chain->queue, dxgi_vk_swap_chain_present_callback, chain);

    chain->user.index = (chain->user.index + 1) % chain->desc.BufferCount;

    /* Relevant if application does not use latency fence, or we force a lower latency through VKD3D_SWAPCHAIN_FRAME_LATENCY overrides. */
    if (vkd3d_native_sync_handle_is_valid(chain->frame_latency_event_internal))
        vkd3d_native_sync_handle_acquire(chain->frame_latency_event_internal);

    if (vkd3d_native_sync_handle_is_valid(chain->present_request_done_event))
    {
        /* For non-present wait path where we are trying to simulate KHR_present_wait in a poor man's way.
         * Block here until we have processed the present and acquired the next image. This is equivalent
         * to a frame latency of swapchain.imageCount - 1. (Usually 2).
         * To combat deadlocks, we can add a small timeout.
         * Failing the timeout is not severe. It will manifest as stutter, but it is better than spurious deadlock.
         * When KHR_present_wait is supported, this path is never taken.
         * If we're heavily GPU bound, we will generally end up blocking on GPU-completion fences in game code instead.
         * When we are present bound, we will generally always render at > 15 Hz. */
        if (!vkd3d_native_sync_handle_acquire_timeout(chain->present_request_done_event, 80))
        {
            WARN("Detected excessively slow Present() processing. Potential causes: resize, wait-before-signal.\n");
            /* Remember to wait for this next present. */
            chain->outstanding_present_request = true;
        }
    }

    /* For latency debug purposes. Consider a frame to begin when we return from Present() with the next user index set.
     * This isn't necessarily correct if the application does WaitSingleObject() on the latency right after this call.
     * That call can take up a frame, so the real latency will be lower than the one reported.
     * Otherwise, the estimate should match up with the internal latency fence. */
    if (chain->debug_latency)
        chain->user.begin_frame_time_ns = vkd3d_get_current_time_ns();

    return S_OK;
}

static VkColorSpaceKHR convert_color_space(DXGI_COLOR_SPACE_TYPE dxgi_color_space);
static bool dxgi_vk_swap_chain_update_formats(struct dxgi_vk_swap_chain *chain);

static bool dxgi_vk_swap_chain_supports_color_space(struct dxgi_vk_swap_chain *chain, DXGI_COLOR_SPACE_TYPE ColorSpace)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkPhysicalDevice vk_physical_device = chain->queue->device->vk_physical_device;
    VkSurfaceFormatKHR *formats = NULL;
    VkColorSpaceKHR vk_color_space;
    uint32_t format_count;
    bool ret = false;
    VkResult vr;
    uint32_t i;

    vk_color_space = convert_color_space(ColorSpace);

    if (dxgi_vk_swap_chain_present_task_is_idle(chain))
    {
        /* This cannot race, just update the internal array. */
        dxgi_vk_swap_chain_update_formats(chain);
        for (i = 0; i < chain->properties.format_count; i++)
            if (chain->properties.formats[i].colorSpace == vk_color_space)
                return true;
    }
    else
    {
        if ((vr = VK_CALL(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device, chain->vk_surface,
                &format_count, NULL))) < 0)
        {
            ERR("Failed to query surface formats, vr %d.\n", vr);
            goto out;
        }

        if (!(formats = vkd3d_malloc(format_count * sizeof(*formats))))
        {
            ERR("Failed to allocate format list.\n");
            goto out;
        }

        if ((vr = VK_CALL(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device, chain->vk_surface,
                &format_count, formats))) < 0)
        {
            ERR("Failed to query surface formats, vr %d.\n", vr);
            goto out;
        }

        for (i = 0; i < format_count; i++)
        {
            if (formats[i].colorSpace == vk_color_space)
            {
                ret = true;
                break;
            }
        }
    }

out:
    vkd3d_free(formats);
    return ret;
}

static UINT STDMETHODCALLTYPE dxgi_vk_swap_chain_CheckColorSpaceSupport(IDXGIVkSwapChain *iface, DXGI_COLOR_SPACE_TYPE ColorSpace)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    UINT support_flags = 0;
    if (dxgi_vk_swap_chain_supports_color_space(chain, ColorSpace))
        support_flags |= DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;
    TRACE("iface %p, ColorSpace %u, SupportFlags #%x.\n", iface, ColorSpace, support_flags);
    return support_flags;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_SetColorSpace(IDXGIVkSwapChain *iface, DXGI_COLOR_SPACE_TYPE ColorSpace)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p, ColorSpace %u.\n", iface, ColorSpace);
    if (!dxgi_vk_swap_chain_supports_color_space(chain, ColorSpace))
        return E_INVALIDARG;

    chain->user.dxgi_color_space_type = ColorSpace;
    chain->user.modifies_hdr_metadata = true;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_SetHDRMetaData(IDXGIVkSwapChain *iface, const DXGI_VK_HDR_METADATA *pMetaData)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p, pMetadata %p.\n", iface, pMetaData);
    chain->user.dxgi_hdr_metadata = *pMetaData;
    chain->user.modifies_hdr_metadata = true;
    return S_OK;
}

static CONST_VTBL struct IDXGIVkSwapChainVtbl dxgi_vk_swap_chain_vtbl =
{
    /* IUnknown methods */
    dxgi_vk_swap_chain_QueryInterface,
    dxgi_vk_swap_chain_AddRef,
    dxgi_vk_swap_chain_Release,

    /* IDXGIVkSwapChain methods */
    dxgi_vk_swap_chain_GetDesc,
    dxgi_vk_swap_chain_GetAdapter,
    dxgi_vk_swap_chain_GetDevice,
    dxgi_vk_swap_chain_GetImage,
    dxgi_vk_swap_chain_GetImageIndex,
    dxgi_vk_swap_chain_GetFrameLatency,
    dxgi_vk_swap_chain_GetFrameLatencyEvent,
    dxgi_vk_swap_chain_ChangeProperties,
    dxgi_vk_swap_chain_SetPresentRegion,
    dxgi_vk_swap_chain_SetGammaControl,
    dxgi_vk_swap_chain_SetFrameLatency,
    dxgi_vk_swap_chain_Present,
    dxgi_vk_swap_chain_CheckColorSpaceSupport,
    dxgi_vk_swap_chain_SetColorSpace,
    dxgi_vk_swap_chain_SetHDRMetaData,
};

static bool dxgi_vk_swap_chain_update_formats(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkPhysicalDevice vk_physical_device = chain->queue->device->vk_physical_device;
    VkResult vr;

    if ((vr = VK_CALL(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device, chain->vk_surface,
            &chain->properties.format_count, NULL))) < 0)
    {
        ERR("Failed to query surface formats.\n");
        return false;
    }

    if (!vkd3d_array_reserve((void**)&chain->properties.formats, &chain->properties.formats_size,
            chain->properties.format_count, sizeof(*chain->properties.formats)))
    {
        ERR("Failed to allocate memory.\n");
        return false;
    }

    if ((vr = VK_CALL(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device, chain->vk_surface,
            &chain->properties.format_count, chain->properties.formats))) < 0)
    {
        ERR("Failed to query surface formats.\n");
        return false;
    }

    return true;
}

static HRESULT dxgi_vk_swap_chain_create_surface(struct dxgi_vk_swap_chain *chain, IDXGIVkSurfaceFactory *pFactory)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkPhysicalDevice vk_physical_device;
    VkInstance vk_instance;
    VkBool32 supported;
    VkResult vr;

    vk_instance = chain->queue->device->vkd3d_instance->vk_instance;
    vk_physical_device = chain->queue->device->vk_physical_device;
    vr = IDXGIVkSurfaceFactory_CreateSurface(pFactory, vk_instance, vk_physical_device, &chain->vk_surface);

    if (vr < 0)
    {
        ERR("Failed to create surface, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    vr = VK_CALL(vkGetPhysicalDeviceSurfaceSupportKHR(vk_physical_device, chain->queue->vkd3d_queue->vk_family_index, chain->vk_surface, &supported));
    if (vr < 0)
    {
        ERR("Failed to query for surface support, vr %d.\n", vr);
        return hresult_from_vk_result(vr);
    }

    if (!supported)
    {
        ERR("Surface is not supported for presentation.\n");
        return E_INVALIDARG;
    }

    return S_OK;
}

static HRESULT dxgi_vk_swap_chain_init_sync_objects(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkSemaphoreTypeCreateInfoKHR type_info;
    VkSemaphoreCreateInfo create_info;
    VkResult vr;
    char env[8];
    HRESULT hr;

    if (FAILED(hr = ID3D12Device12_CreateFence(&chain->queue->device->ID3D12Device_iface, 0,
            D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence1, (void**)&chain->present.frame_latency_fence)))
    {
        WARN("Failed to create frame latency fence, hr %#x.\n", hr);
        return hr;
    }

    if (chain->desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
    {
        INFO("Enabling frame latency handles.\n");
        chain->frame_latency = 1;

        if (FAILED(hr = vkd3d_native_sync_handle_create(chain->frame_latency,
                VKD3D_NATIVE_SYNC_HANDLE_TYPE_SEMAPHORE, &chain->frame_latency_event)))
        {
            WARN("Failed to create frame latency semaphore, hr %#x.\n", hr);
            return hr;
        }
    }
    else
    {
#define DEFAULT_FRAME_LATENCY 3
        /* 3 frames is the default in the API, so that's what we should report to application if asks. */
        chain->frame_latency = DEFAULT_FRAME_LATENCY;
    }

    if (chain->queue->device->device_info.present_wait_features.presentWait)
    {
        /* Applications can easily forget to increment their latency handles.
         * This means we don't keep latency objects in sync anymore.
         * Maintain our own latency fence to keep things under control.
         * If we don't have present wait, we will sync with present_request_done_event (below) instead.
         * Adding more sync against internal blit fences is meaningless
         * and can cause weird pumping issues in some cases since we're simultaneously syncing
         * against two different timelines. */
        if (vkd3d_get_env_var("VKD3D_SWAPCHAIN_LATENCY_FRAMES", env, sizeof(env)))
        {
            chain->frame_latency_internal = strtoul(env, NULL, 0);
            chain->frame_latency_internal_is_static = true;
        }
        else
        {
            /* If we don't specify an explicit number of latency, we deduce it based
             * on BufferCount. */
            chain->frame_latency_internal = chain->desc.BufferCount;
        }

        if (chain->frame_latency_internal >= 1 && chain->frame_latency_internal < DXGI_MAX_SWAP_CHAIN_BUFFERS)
        {
            INFO("Ensure maximum latency of %u frames with KHR_present_wait.\n",
                    chain->frame_latency_internal);

            /* On the first frame, we are supposed to acquire,
             * but we only acquire after a Present, so do the implied one here.
             * We consume a count after Present(), i.e. start of next frame,
             * starting with one less takes care of this. */
            if (FAILED(hr = vkd3d_native_sync_handle_create(chain->frame_latency_internal - 1,
                    VKD3D_NATIVE_SYNC_HANDLE_TYPE_SEMAPHORE, &chain->frame_latency_event_internal)))
            {
                WARN("Failed to create internal frame latency semaphore, hr %#x.\n", hr);
                return hr;
            }
        }
    }

    if (!chain->queue->device->device_info.present_wait_features.presentWait &&
            FAILED(hr = vkd3d_native_sync_handle_create(0, VKD3D_NATIVE_SYNC_HANDLE_TYPE_EVENT,
                    &chain->present_request_done_event)))
    {
        WARN("Failed to create internal present done event, hr %#x.\n", hr);
        return hr;
    }

    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    create_info.pNext = &type_info;
    create_info.flags = 0;
    type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
    type_info.pNext = NULL;
    type_info.initialValue = 0;
    type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;

    vr = VK_CALL(vkCreateSemaphore(chain->queue->device->vk_device, &create_info,
            NULL, &chain->present.vk_complete_semaphore));
    if (vr < 0)
    {
        ERR("Failed to create timeline semaphore, vr %d.\n", vr);
        return hresult_from_vkd3d_result(vr);
    }

    vr = VK_CALL(vkCreateSemaphore(chain->queue->device->vk_device, &create_info,
            NULL, &chain->present.vk_internal_blit_semaphore));
    if (vr < 0)
    {
        ERR("Failed to create timeline semaphore, vr %d.\n", vr);
        return hresult_from_vkd3d_result(vr);
    }

    return S_OK;
}

static void dxgi_vk_swap_chain_drain_waiter(struct dxgi_vk_swap_chain *chain)
{
    if (chain->wait_thread.active)
    {
        /* Make sure wait thread is not waiting on anything before we destroy swapchain. */
        pthread_mutex_lock(&chain->wait_thread.lock);

        /* Skip ahead if there are multiple frames queued. */
        chain->wait_thread.skip_waits = true;
        while (chain->wait_thread.wait_queue_count)
            pthread_cond_wait(&chain->wait_thread.cond, &chain->wait_thread.lock);
        chain->wait_thread.skip_waits = false;
        pthread_mutex_unlock(&chain->wait_thread.lock);
    }
}

static void dxgi_vk_swap_chain_destroy_swapchain_in_present_task(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkQueue vk_queue;
    unsigned int i;

    if (!chain->present.vk_swapchain)
        return;

    /* TODO: Can replace this stall with VK_KHR_present_wait,
     * but when destroying vk_release_semaphore we might be in a state where we submitted blit command buffer,
     * but never waited on the semaphore in vkQueuePresent, so we would still need this WaitIdle() most likely. */
    vk_queue = vkd3d_queue_acquire(chain->queue->vkd3d_queue);
    VK_CALL(vkQueueWaitIdle(vk_queue));
    vkd3d_queue_release(chain->queue->vkd3d_queue);

    dxgi_vk_swap_chain_drain_waiter(chain);

    for (i = 0; i < ARRAY_SIZE(chain->present.vk_backbuffer_image_views); i++)
        VK_CALL(vkDestroyImageView(chain->queue->device->vk_device, chain->present.vk_backbuffer_image_views[i], NULL));
    for (i = 0; i < ARRAY_SIZE(chain->present.vk_release_semaphores); i++)
        VK_CALL(vkDestroySemaphore(chain->queue->device->vk_device, chain->present.vk_release_semaphores[i], NULL));
    memset(chain->present.vk_backbuffer_images, 0, sizeof(chain->present.vk_backbuffer_images));
    memset(chain->present.vk_backbuffer_image_views, 0, sizeof(chain->present.vk_backbuffer_image_views));
    memset(chain->present.vk_release_semaphores, 0, sizeof(chain->present.vk_release_semaphores));

    VK_CALL(vkDestroySwapchainKHR(chain->queue->device->vk_device, chain->present.vk_swapchain, NULL));
    chain->present.vk_swapchain = VK_NULL_HANDLE;
    chain->present.backbuffer_width = 0;
    chain->present.backbuffer_height = 0;
    chain->present.backbuffer_format = VK_FORMAT_UNDEFINED;
    chain->present.backbuffer_count = 0;
    chain->present.force_swapchain_recreation = false;
    chain->present.present_id_valid = false;
    chain->present.present_id = 0;
    chain->present.current_backbuffer_index = UINT32_MAX;
}

static VkColorSpaceKHR convert_color_space(DXGI_COLOR_SPACE_TYPE dxgi_color_space)
{
    switch (dxgi_color_space)
    {
        case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
            return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
            return VK_COLOR_SPACE_HDR10_ST2084_EXT;
        case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
            return VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
        default:
            WARN("Unhandled color space %#x. Falling back to sRGB.\n", dxgi_color_space);
            return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }
}

static bool dxgi_vk_swap_chain_accept_format(const VkSurfaceFormatKHR *format, VkFormat vk_format)
{
    if (vk_format == VK_FORMAT_UNDEFINED)
    {
        switch (format->format)
        {
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
                return true;

            default:
                return false;
        }
    }
    else
    {
        return format->format == vk_format;
    }
}

static bool dxgi_vk_swap_chain_find_surface_format(struct dxgi_vk_swap_chain *chain, VkFormat vk_format,
        VkColorSpaceKHR color_space, VkSurfaceFormatKHR *format)
{
    uint32_t i;

    for (i = 0; i < chain->properties.format_count; i++)
    {
        if (dxgi_vk_swap_chain_accept_format(&chain->properties.formats[i], vk_format) &&
                chain->properties.formats[i].colorSpace == color_space)
        {
            *format = chain->properties.formats[i];
            return true;
        }
    }

    return false;
}

static bool dxgi_vk_swap_chain_select_format(struct dxgi_vk_swap_chain *chain, VkSurfaceFormatKHR *format)
{
    VkFormat vk_format = vkd3d_get_format(chain->queue->device, chain->request.dxgi_format, false)->vk_format;
    VkColorSpaceKHR vk_color_space = convert_color_space(chain->request.dxgi_color_space_type);

    if (dxgi_vk_swap_chain_find_surface_format(chain, vk_format, vk_color_space, format))
        return true;

    /* If we're using sRGB swapchains, we can fallback.
     * Usually happens for RGBA8 or 10-bit UNORM and display does not support it as a present format.
     * This can be trivially worked around by selecting e.g. BGRA8. */
    if (vk_color_space == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
    {
        return dxgi_vk_swap_chain_find_surface_format(chain, VK_FORMAT_UNDEFINED, vk_color_space, format);
    }
    else
    {
        /* Refuse to present unsupported HDR since it will look completely bogus. */
        return false;
    }
}

static bool dxgi_vk_swap_chain_check_present_mode_support(struct dxgi_vk_swap_chain *chain, VkPresentModeKHR present_mode)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkPhysicalDevice vk_physical_device = chain->queue->device->vk_physical_device;
    VkPresentModeKHR supported_modes[16];
    uint32_t mode_count;
    uint32_t i;

    mode_count = ARRAY_SIZE(supported_modes);
    VK_CALL(vkGetPhysicalDeviceSurfacePresentModesKHR(vk_physical_device, chain->vk_surface, &mode_count, supported_modes));
    for (i = 0; i < mode_count; i++)
        if (supported_modes[i] == present_mode)
            return true;
    return false;
}

static void dxgi_vk_swap_chain_init_blit_pipeline(struct dxgi_vk_swap_chain *chain)
{
    struct d3d12_device *device = chain->queue->device;
    struct vkd3d_swapchain_pipeline_key key;
    HRESULT hr;

    key.bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    key.filter = chain->desc.Scaling == DXGI_SCALING_NONE ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    key.format = chain->present.backbuffer_format;

    if (FAILED(hr = vkd3d_meta_get_swapchain_pipeline(&device->meta_ops, &key, &chain->present.pipeline)))
        ERR("Failed to initialize swapchain pipeline.\n");
}

static void dxgi_vk_swap_chain_recreate_swapchain_in_present_task(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkPhysicalDevice vk_physical_device = chain->queue->device->vk_physical_device;
    VkDevice vk_device = chain->queue->device->vk_device;
    VkCommandPoolCreateInfo command_pool_create_info;
    VkSwapchainCreateInfoKHR swapchain_create_info;
    VkSurfaceCapabilitiesKHR surface_caps;
    VkSurfaceFormatKHR surface_format;
    VkImageViewCreateInfo view_info;
    VkPresentModeKHR present_mode;
    uint32_t override_image_count;
    bool new_occlusion_state;
    char count_env[16];
    VkResult vr;
    uint32_t i;

    dxgi_vk_swap_chain_destroy_swapchain_in_present_task(chain);

    /* Don't bother if we've observed ERROR_SURFACE_LOST. */
    if (chain->present.is_surface_lost)
        return;

    /* If we fail to query formats we are hosed, treat it as a SURFACE_LOST scenario. */
    if (!dxgi_vk_swap_chain_update_formats(chain))
    {
        chain->present.is_surface_lost = true;
        return;
    }

    VK_CALL(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_physical_device, chain->vk_surface, &surface_caps));

    /* Win32 quirk. Minimized windows have maximum extents of zero. */
    new_occlusion_state = surface_caps.maxImageExtent.width == 0 || surface_caps.maxImageExtent.height == 0;
    vkd3d_atomic_uint32_store_explicit(&chain->present.is_occlusion_state, (uint32_t)new_occlusion_state, vkd3d_memory_order_relaxed);

    /* There is nothing to do. We'll do a dummy present. */
    if (new_occlusion_state)
        return;

    /* Sanity check, this cannot happen on Win32 surfaces, but could happen on Wayland. */
    if (surface_caps.currentExtent.width == UINT32_MAX || surface_caps.currentExtent.height == UINT32_MAX)
        return;

    /* No format to present to yet. Can happen in transition states for HDR.
     * Where we have modified color space, but not yet changed user backbuffer format. */
    if (!dxgi_vk_swap_chain_select_format(chain, &surface_format))
        return;

    present_mode = chain->request.swap_interval > 0 ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (!dxgi_vk_swap_chain_check_present_mode_support(chain, present_mode))
    {
        if (present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR &&
                dxgi_vk_swap_chain_check_present_mode_support(chain, VK_PRESENT_MODE_MAILBOX_KHR))
        {
            present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        }
        else
            return;
    }

    memset(&swapchain_create_info, 0, sizeof(swapchain_create_info));
    swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.surface = chain->vk_surface;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageColorSpace = surface_format.colorSpace;
    swapchain_create_info.imageFormat = surface_format.format;
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchain_create_info.presentMode = present_mode;
    swapchain_create_info.clipped = VK_TRUE;

    /* We don't block directly on Present(), so there's no reason to use more than 3 images if even application requests more.
     * We could get away with 2 if we used WSI acquire semaphore and async acquire was supported, but e.g. Mesa does not support that.
     * However, without present-wait, we'll have to inherit quirky behavior from legacy swapchain
     * and use minImageCount = BufferCount + 1. */
    swapchain_create_info.minImageCount = max(max(chain->request.target_min_image_count, 3u),
            surface_caps.minImageCount);

    vkd3d_get_env_var("VKD3D_SWAPCHAIN_IMAGES", count_env, sizeof(count_env));
    if (strlen(count_env) > 0)
    {
        override_image_count = strtoul(count_env, NULL, 0);
        swapchain_create_info.minImageCount = max(surface_caps.minImageCount, override_image_count);
        INFO("Overriding swapchain images to %u.\n", swapchain_create_info.minImageCount);
    }

    if (surface_caps.maxImageCount && swapchain_create_info.minImageCount > surface_caps.maxImageCount)
        swapchain_create_info.minImageCount = surface_caps.maxImageCount;

    swapchain_create_info.imageExtent = surface_caps.currentExtent;
    swapchain_create_info.imageExtent.width = max(swapchain_create_info.imageExtent.width, surface_caps.minImageExtent.width);
    swapchain_create_info.imageExtent.width = min(swapchain_create_info.imageExtent.width, surface_caps.maxImageExtent.width);
    swapchain_create_info.imageExtent.height = max(swapchain_create_info.imageExtent.height, surface_caps.minImageExtent.height);
    swapchain_create_info.imageExtent.height = min(swapchain_create_info.imageExtent.height, surface_caps.maxImageExtent.height);

    vr = VK_CALL(vkCreateSwapchainKHR(vk_device, &swapchain_create_info, NULL, &chain->present.vk_swapchain));
    if (vr < 0)
    {
        ERR("Failed to create swapchain, vr %d.\n", vr);
        chain->present.vk_swapchain = VK_NULL_HANDLE;
        return;
    }

    chain->present.backbuffer_count = ARRAY_SIZE(chain->present.vk_backbuffer_images);
    VK_CALL(vkGetSwapchainImagesKHR(vk_device, chain->present.vk_swapchain, &chain->present.backbuffer_count, chain->present.vk_backbuffer_images));

    INFO("Got %u swapchain images.\n", chain->present.backbuffer_count);

    memset(&view_info, 0, sizeof(view_info));
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.format = swapchain_create_info.imageFormat;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.layerCount = 1;
    view_info.subresourceRange.levelCount = 1;

    for (i = 0; i < chain->present.backbuffer_count; i++)
    {
        view_info.image = chain->present.vk_backbuffer_images[i];
        VK_CALL(vkCreateImageView(vk_device, &view_info, NULL, &chain->present.vk_backbuffer_image_views[i]));
    }

    chain->present.backbuffer_width = swapchain_create_info.imageExtent.width;
    chain->present.backbuffer_height = swapchain_create_info.imageExtent.height;
    chain->present.backbuffer_format = swapchain_create_info.imageFormat;
    chain->present.current_backbuffer_index = UINT32_MAX;

    if (!chain->present.vk_blit_command_pool)
    {
        command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        command_pool_create_info.pNext = NULL;
        command_pool_create_info.queueFamilyIndex = chain->queue->vkd3d_queue->vk_family_index;
        command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CALL(vkCreateCommandPool(vk_device, &command_pool_create_info, NULL, &chain->present.vk_blit_command_pool));
    }

    dxgi_vk_swap_chain_init_blit_pipeline(chain);
    dxgi_vk_swap_chain_set_hdr_metadata(chain);
}

static bool request_needs_swapchain_recreation(const struct dxgi_vk_swap_chain_present_request *request,
        const struct dxgi_vk_swap_chain_present_request *last_request)
{
    return request->dxgi_color_space_type != last_request->dxgi_color_space_type ||
        request->dxgi_format != last_request->dxgi_format ||
        request->target_min_image_count != last_request->target_min_image_count ||
        (!!request->swap_interval) != (!!last_request->swap_interval);
}

static void dxgi_vk_swap_chain_present_signal_blit_semaphore(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkSemaphoreSubmitInfo signal_semaphore_info;
    VkSubmitInfo2 submit_info;
    VkQueue vk_queue;
    VkResult vr;

    /* Could have used the internal timeline for this, but it complicates user thread
     * waiting. It expects lock-step timelines, so we need to guarantee a 1:1 ratio of Present() calls
     * to increments here. */
    chain->present.complete_count += 1;

    memset(&signal_semaphore_info, 0, sizeof(signal_semaphore_info));
    signal_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_semaphore_info.semaphore = chain->present.vk_complete_semaphore;
    signal_semaphore_info.value = chain->present.complete_count;
    signal_semaphore_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.signalSemaphoreInfoCount = 1;
    submit_info.pSignalSemaphoreInfos = &signal_semaphore_info;

    vk_queue = vkd3d_queue_acquire(chain->queue->vkd3d_queue);
    vr = VK_CALL(vkQueueSubmit2(vk_queue, 1, &submit_info, VK_NULL_HANDLE));
    vkd3d_queue_release(chain->queue->vkd3d_queue);

    if (vr)
    {
        ERR("Failed to submit present discard, vr = %d.\n", vr);
        VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(chain->queue->device, vr == VK_ERROR_DEVICE_LOST);
    }
}

static void dxgi_vk_swap_chain_record_render_pass(struct dxgi_vk_swap_chain *chain, VkCommandBuffer vk_cmd, uint32_t swapchain_index)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkRenderingAttachmentInfo attachment_info;
    VkImageMemoryBarrier2 image_barrier;
    VkDescriptorImageInfo image_info;
    VkWriteDescriptorSet write_info;
    struct d3d12_resource *resource;
    VkRenderingInfo rendering_info;
    VkDependencyInfo dep_info;
    VkViewport viewport;
    bool blank_present;

    /* If application intends to present before we have rendered to it,
     * it is valid, but we need to ignore the blit, just clear backbuffer. */
    resource = chain->user.backbuffers[chain->request.user_index];
    blank_present = vkd3d_atomic_uint32_load_explicit(&resource->initial_layout_transition, vkd3d_memory_order_relaxed) != 0;

    if (blank_present)
        WARN("Application is presenting user index %u, but it has never been rendered to.\n", chain->request.user_index);

    memset(&attachment_info, 0, sizeof(attachment_info));
    attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    attachment_info.imageView = chain->present.vk_backbuffer_image_views[swapchain_index];
    attachment_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    if (chain->desc.Scaling == DXGI_SCALING_NONE || blank_present)
        attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

    memset(&rendering_info, 0, sizeof(rendering_info));
    rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    rendering_info.renderArea.extent.width = chain->present.backbuffer_width;
    rendering_info.renderArea.extent.height = chain->present.backbuffer_height;
    rendering_info.layerCount = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments = &attachment_info;

    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    if (chain->desc.Scaling == DXGI_SCALING_NONE)
    {
        viewport.width = (float)chain->desc.Width;
        viewport.height = (float)chain->desc.Height;
    }
    else
    {
        viewport.width = chain->present.backbuffer_width;
        viewport.height = chain->present.backbuffer_height;
    }

    memset(&dep_info, 0, sizeof(dep_info));
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.imageMemoryBarrierCount = 1;
    dep_info.pImageMemoryBarriers = &image_barrier;

    /* srcStage = COLOR_ATTACHMENT to link up to acquire semaphore. */
    memset(&image_barrier, 0, sizeof(image_barrier));
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.image = chain->present.vk_backbuffer_images[swapchain_index];
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.layerCount = 1;

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS) &&
            chain->queue->device->vk_info.EXT_debug_utils)
    {
        VkDebugUtilsLabelEXT label;
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pNext = NULL;
        label.pLabelName = "BlitSwapChain";
        label.color[0] = 1.0f;
        label.color[1] = 1.0f;
        label.color[2] = 1.0f;
        label.color[3] = 1.0f;
        VK_CALL(vkCmdBeginDebugUtilsLabelEXT(vk_cmd, &label));
    }

    VK_CALL(vkCmdPipelineBarrier2(vk_cmd, &dep_info));
    VK_CALL(vkCmdBeginRendering(vk_cmd, &rendering_info));

    if (!blank_present)
    {
        VK_CALL(vkCmdSetViewport(vk_cmd, 0, 1, &viewport));
        VK_CALL(vkCmdSetScissor(vk_cmd, 0, 1, &rendering_info.renderArea));
        VK_CALL(vkCmdBindPipeline(vk_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    chain->present.pipeline.vk_pipeline));

        write_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_info.pNext = NULL;
        write_info.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write_info.pBufferInfo = NULL;
        write_info.dstSet = VK_NULL_HANDLE;
        write_info.pTexelBufferView = NULL;
        write_info.pImageInfo = &image_info;
        write_info.dstBinding = 0;
        write_info.dstArrayElement = 0;
        write_info.descriptorCount = 1;
        image_info.imageView = chain->user.vk_image_views[chain->request.user_index];
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.sampler = VK_NULL_HANDLE;

        VK_CALL(vkCmdPushDescriptorSetKHR(vk_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    chain->present.pipeline.vk_pipeline_layout, 0, 1, &write_info));

        VK_CALL(vkCmdDraw(vk_cmd, 3, 1, 0, 0));
    }

    VK_CALL(vkCmdEndRendering(vk_cmd));

    image_barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    image_barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    image_barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    image_barrier.dstAccessMask = VK_ACCESS_2_NONE;
    image_barrier.oldLayout = image_barrier.newLayout;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VK_CALL(vkCmdPipelineBarrier2(vk_cmd, &dep_info));

    if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_DEBUG_UTILS) &&
            chain->queue->device->vk_info.EXT_debug_utils)
    {
        VK_CALL(vkCmdEndDebugUtilsLabelEXT(vk_cmd));
    }
}

static bool dxgi_vk_swap_chain_submit_blit(struct dxgi_vk_swap_chain *chain, uint32_t swapchain_index)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkDevice vk_device = chain->queue->device->vk_device;
    VkSemaphoreSubmitInfo signal_semaphore_info[2];
    VkSemaphoreCreateInfo semaphore_create_info;
    VkSemaphoreSubmitInfo wait_semaphore_info;
    VkCommandBufferAllocateInfo allocate_info;
    VkCommandBufferSubmitInfo cmd_buffer_info;
    VkCommandBufferBeginInfo cmd_begin_info;
    VkSubmitInfo2 submit_infos[2];
    VkCommandBuffer vk_cmd;
    VkQueue vk_queue;
    VkResult vr;

    /* Create objects on-demand. */
    if (!chain->present.vk_release_semaphores[swapchain_index])
    {
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_create_info.pNext = NULL;
        semaphore_create_info.flags = 0;
        vr = VK_CALL(vkCreateSemaphore(vk_device, &semaphore_create_info,
                    NULL, &chain->present.vk_release_semaphores[swapchain_index]));
        if (vr < 0)
        {
            ERR("Failed to create semaphore, vr %d.\n", vr);
            return false;
        }
    }

    if (!chain->present.vk_blit_command_buffers[swapchain_index])
    {
        memset(&allocate_info, 0, sizeof(allocate_info));
        allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate_info.commandBufferCount = 1;
        allocate_info.commandPool = chain->present.vk_blit_command_pool;
        allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        vr = VK_CALL(vkAllocateCommandBuffers(vk_device, &allocate_info,
                    &chain->present.vk_blit_command_buffers[swapchain_index]));
        if (vr < 0)
        {
            ERR("Failed to allocate command buffers, vr %d\n", vr);
            return false;
        }
    }

    dxgi_vk_swap_chain_drain_internal_blit_semaphore(chain, chain->present.backbuffer_blit_timelines[swapchain_index]);

    vk_cmd = chain->present.vk_blit_command_buffers[swapchain_index];

    VK_CALL(vkResetCommandBuffer(vk_cmd, 0));
    memset(&cmd_begin_info, 0, sizeof(cmd_begin_info));
    cmd_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmd_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CALL(vkBeginCommandBuffer(vk_cmd, &cmd_begin_info));
    dxgi_vk_swap_chain_record_render_pass(chain, vk_cmd, swapchain_index);
    VK_CALL(vkEndCommandBuffer(vk_cmd));

    assert(chain->present.acquire_semaphore_signalled[chain->present.acquire_semaphore_index]);
    memset(&wait_semaphore_info, 0, sizeof(wait_semaphore_info));
    wait_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_semaphore_info.semaphore = chain->present.vk_acquire_semaphore[chain->present.acquire_semaphore_index];
    wait_semaphore_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    memset(signal_semaphore_info, 0, sizeof(signal_semaphore_info));
    signal_semaphore_info[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_semaphore_info[0].semaphore = chain->present.vk_release_semaphores[swapchain_index];
    signal_semaphore_info[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    chain->present.internal_blit_count += 1;
    signal_semaphore_info[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_semaphore_info[1].semaphore = chain->present.vk_internal_blit_semaphore;
    signal_semaphore_info[1].value = chain->present.internal_blit_count;
    signal_semaphore_info[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    memset(&cmd_buffer_info, 0, sizeof(cmd_buffer_info));
    cmd_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmd_buffer_info.commandBuffer = vk_cmd;

    memset(submit_infos, 0, sizeof(submit_infos));
    submit_infos[0].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_infos[0].commandBufferInfoCount = 1;
    submit_infos[0].pCommandBufferInfos = &cmd_buffer_info;
    submit_infos[0].pWaitSemaphoreInfos = &wait_semaphore_info;
    submit_infos[0].waitSemaphoreInfoCount = 1;
    submit_infos[0].signalSemaphoreInfoCount = 1;
    submit_infos[0].pSignalSemaphoreInfos = &signal_semaphore_info[0];

    /* Internal blit semaphore must be signaled after we signal vk_release_semaphores.
     * To guarantee this, the signals must happen in different batches. */
    submit_infos[1].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_infos[1].signalSemaphoreInfoCount = 1;
    submit_infos[1].pSignalSemaphoreInfos = &signal_semaphore_info[1];

    vk_queue = vkd3d_queue_acquire(chain->queue->vkd3d_queue);
    vr = VK_CALL(vkQueueSubmit2(vk_queue, ARRAY_SIZE(submit_infos), submit_infos, VK_NULL_HANDLE));
    vkd3d_queue_release(chain->queue->vkd3d_queue);
    VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(chain->queue->device, vr == VK_ERROR_DEVICE_LOST);

    if (vr < 0)
    {
        ERR("Failed to submit swapchain blit, vr %d.\n", vr);
    }
    else
    {
        chain->present.backbuffer_blit_timelines[swapchain_index] =
                chain->present.internal_blit_count;
        chain->present.acquire_semaphore_consumed_at_blit[chain->present.acquire_semaphore_index] =
                chain->present.internal_blit_count;
        chain->present.acquire_semaphore_signalled[chain->present.acquire_semaphore_index] = false;
    }

    return vr == VK_SUCCESS;
}

static void dxgi_vk_swap_chain_present_recreate_swapchain_if_required(struct dxgi_vk_swap_chain *chain)
{
    if (!chain->present.vk_swapchain || chain->present.force_swapchain_recreation)
        dxgi_vk_swap_chain_recreate_swapchain_in_present_task(chain);
}

static VkResult dxgi_vk_swap_chain_ensure_unsignaled_acquire_semaphore(struct dxgi_vk_swap_chain *chain,
        uint32_t index, bool blocking)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkSemaphoreCreateInfo sem_info;
    VkResult vr = VK_SUCCESS;
    uint64_t drain_count;

    if (blocking)
    {
        /* Any pending wait must have been satisfied before we queue up a new signal. */
        drain_count = chain->present.acquire_semaphore_consumed_at_blit[index];
        if (drain_count)
            dxgi_vk_swap_chain_drain_internal_blit_semaphore(chain, drain_count);
    }

    if (chain->present.acquire_semaphore_signalled[index])
    {
        /* There is no pending wait, so we insert it now. */
        dxgi_vk_swap_chain_wait_acquire_semaphore(chain, chain->present.vk_acquire_semaphore[index], blocking);
        chain->present.acquire_semaphore_consumed_at_blit[index] = chain->present.internal_blit_count;
        chain->present.acquire_semaphore_signalled[index] = false;
    }

    if (!chain->present.vk_acquire_semaphore[index])
    {
        memset(&sem_info, 0, sizeof(sem_info));
        sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        if ((vr = VK_CALL(vkCreateSemaphore(chain->queue->device->vk_device, &sem_info,
                NULL, &chain->present.vk_acquire_semaphore[index]))) != VK_SUCCESS)
        {
            ERR("Failed to create semaphore, vr %d\n", vr);
            chain->present.vk_acquire_semaphore[index] = VK_NULL_HANDLE;
        }
    }

    return vr;
}

static VkResult dxgi_vk_swap_chain_try_acquire_next_image(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkDevice vk_device = chain->queue->device->vk_device;
    uint32_t semaphore_index;
    VkResult vr;

    if (!chain->present.vk_swapchain)
        return VK_ERROR_OUT_OF_DATE_KHR;
    if (chain->present.current_backbuffer_index != UINT32_MAX)
        return VK_SUCCESS;

    /* Ensure that we wait for semaphores before leaving it behind to avoid
     * having to do a blocking wait later. This can happen if we acquired successfully, but got
     * SUBOPTIMAL. That would leave a semaphore floating if we don't wait for it. */
    semaphore_index = chain->present.acquire_semaphore_index;
    vr = dxgi_vk_swap_chain_ensure_unsignaled_acquire_semaphore(chain, semaphore_index, false);
    if (vr != VK_SUCCESS)
        return vr;

    chain->present.acquire_semaphore_index = (chain->present.acquire_semaphore_index + 1) %
            ARRAY_SIZE(chain->present.vk_acquire_semaphore);
    semaphore_index = chain->present.acquire_semaphore_index;

    /* This should never actually block unless something unexpected happens. */
    vr = dxgi_vk_swap_chain_ensure_unsignaled_acquire_semaphore(chain, semaphore_index, true);
    if (vr != VK_SUCCESS)
        return vr;

    vr = VK_CALL(vkAcquireNextImageKHR(vk_device, chain->present.vk_swapchain, UINT64_MAX,
            chain->present.vk_acquire_semaphore[semaphore_index], VK_NULL_HANDLE,
            &chain->present.current_backbuffer_index));

    if (vr < 0)
        chain->present.current_backbuffer_index = UINT32_MAX;
    else
        chain->present.acquire_semaphore_signalled[semaphore_index] = true;

    return vr;
}

static void dxgi_vk_swap_chain_present_iteration(struct dxgi_vk_swap_chain *chain, unsigned int retry_counter)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkPresentInfoKHR present_info;
    VkPresentIdKHR present_id;
    uint32_t swapchain_index;
    VkResult vk_result;
    VkQueue vk_queue;
    VkResult vr;

    VKD3D_REGION_DECL(queue_present);

    dxgi_vk_swap_chain_present_recreate_swapchain_if_required(chain);
    if (!chain->present.vk_swapchain)
        return;

    vr = dxgi_vk_swap_chain_try_acquire_next_image(chain);
    VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(chain->queue->device, vr == VK_ERROR_DEVICE_LOST);

    /* Handle any errors and retry as needed. If we cannot make meaningful forward progress, just give up and retry later. */
    if (vr == VK_SUBOPTIMAL_KHR || vr < 0)
        chain->present.force_swapchain_recreation = true;
    if (vr < 0)
        dxgi_vk_swap_chain_destroy_swapchain_in_present_task(chain);

    if (vr == VK_ERROR_OUT_OF_DATE_KHR)
    {
        if (retry_counter < 3)
            dxgi_vk_swap_chain_present_iteration(chain, retry_counter + 1);
    }
    else if (vr == VK_ERROR_SURFACE_LOST_KHR)
    {
        /* If the surface is lost, we cannot expect to get forward progress. Just keep rendering to nothing. */
        chain->present.is_surface_lost = true;
    }

    if (vr < 0)
        return;

    swapchain_index = chain->present.current_backbuffer_index;
    assert(swapchain_index != UINT32_MAX);

    if (!dxgi_vk_swap_chain_submit_blit(chain, swapchain_index))
        return;

    memset(&present_info, 0, sizeof(present_info));
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pSwapchains = &chain->present.vk_swapchain;
    present_info.swapchainCount = 1;
    present_info.pImageIndices = &swapchain_index;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &chain->present.vk_release_semaphores[swapchain_index];
    present_info.pResults = &vk_result;

    /* Only bother with present wait path for FIFO swapchains.
     * Non-FIFO swapchains will pump their frame latency handles through the fallback path of blit command being done.
     * Especially on Xwayland, the present ID is updated when images actually hit on-screen due to MAILBOX behavior.
     * This would unnecessarily stall our progress. */
    if (chain->wait_thread.active && !chain->present.present_id_valid && chain->request.swap_interval > 0)
    {
        chain->present.present_id += 1;
        present_id.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
        present_id.pNext = NULL;
        present_id.swapchainCount = 1;
        present_id.pPresentIds = &chain->present.present_id;
        present_info.pNext = &present_id;
    }

    vk_queue = vkd3d_queue_acquire(chain->queue->vkd3d_queue);
    VKD3D_REGION_BEGIN(queue_present);
    vr = VK_CALL(vkQueuePresentKHR(vk_queue, &present_info));
    VKD3D_REGION_END(queue_present);
    vkd3d_queue_release(chain->queue->vkd3d_queue);
    VKD3D_DEVICE_REPORT_FAULT_AND_BREADCRUMB_IF(chain->queue->device, vr == VK_ERROR_DEVICE_LOST);

    if (vr == VK_SUCCESS && vk_result != VK_SUCCESS)
        vr = vk_result;

    if (vr >= 0)
        chain->present.current_backbuffer_index = UINT32_MAX;

    if (present_info.pNext && vr >= 0)
        chain->present.present_id_valid = true;

    /* Handle any errors and retry as needed. If we cannot make meaningful forward progress, just give up and retry later. */
    if (vr == VK_SUBOPTIMAL_KHR || vr < 0)
        chain->present.force_swapchain_recreation = true;
    if (vr < 0)
        dxgi_vk_swap_chain_destroy_swapchain_in_present_task(chain);

    if (vr == VK_ERROR_OUT_OF_DATE_KHR)
    {
        if (retry_counter < 3)
            dxgi_vk_swap_chain_present_iteration(chain, retry_counter + 1);
    }
    else if (vr == VK_ERROR_SURFACE_LOST_KHR)
    {
        /* If the surface is lost, we cannot expect to get forward progress. Just keep rendering to nothing. */
        chain->present.is_surface_lost = true;
    }
}

static void dxgi_vk_swap_chain_signal_waitable_handle(struct dxgi_vk_swap_chain *chain)
{
    HRESULT hr;

    if (chain->present.present_id_valid)
    {
        dxgi_vk_swap_chain_push_present_id(chain, chain->present.present_id, chain->request.begin_frame_time_ns);
    }
    else
    {
        chain->present.frame_latency_count += 1;
        d3d12_command_queue_signal_inline(chain->queue, chain->present.frame_latency_fence, chain->present.frame_latency_count);

        if (vkd3d_native_sync_handle_is_valid(chain->frame_latency_event))
        {
            if (FAILED(hr = d3d12_fence_set_native_sync_handle_on_completion(
                    impl_from_ID3D12Fence1(chain->present.frame_latency_fence),
                    chain->present.frame_latency_count, chain->frame_latency_event)))
            {
                ERR("Failed to enqueue frame latency event, hr %#x.\n", hr);
                vkd3d_native_sync_handle_release(chain->frame_latency_event, 1);
            }
        }

        if (vkd3d_native_sync_handle_is_valid(chain->frame_latency_event_internal))
        {
            if (FAILED(hr = d3d12_fence_set_native_sync_handle_on_completion(
                    impl_from_ID3D12Fence1(chain->present.frame_latency_fence),
                    chain->present.frame_latency_count, chain->frame_latency_event_internal)))
            {
                ERR("Failed to enqueue frame latency event, hr %#x.\n", hr);
                vkd3d_native_sync_handle_release(chain->frame_latency_event_internal, 1);
            }
        }
    }
}

static void dxgi_vk_swap_chain_present_callback(void *chain_)
{
    const struct dxgi_vk_swap_chain_present_request *next_request;
    struct dxgi_vk_swap_chain *chain = chain_;
    uint32_t next_present_count;
    uint32_t present_count;
    uint32_t i;

    next_present_count = chain->present.present_count + 1;
    next_request = &chain->request_ring[next_present_count % ARRAY_SIZE(chain->request_ring)];
    if (request_needs_swapchain_recreation(next_request, &chain->request))
        chain->present.force_swapchain_recreation = true;

    chain->request = *next_request;
    if (chain->request.modifies_hdr_metadata)
        dxgi_vk_swap_chain_set_hdr_metadata(chain);

    /* If no QueuePresentKHRs successfully commits a present ID, we'll fallback to a normal queue signal. */
    chain->present.present_id_valid = false;

    /* There is currently no present timing in Vulkan we can rely on, so just duplicate blit them as needed.
     * This happens on a thread, so the blocking should not be a significant problem.
     * TODO: Propose VK_EXT_present_interval. */
    present_count = max(1u, chain->request.swap_interval);

    /* If we hit the legacy way of synchronizing with swapchain, blitting multiple times would be horrible. */
    if (!chain->wait_thread.active)
        present_count = 1;

    for (i = 0; i < present_count; i++)
    {
        /* A present iteration may or may not render to backbuffer. We'll apply best effort here.
         * Forward progress must be ensured, so if we cannot get anything on-screen in a reasonable amount of retries, ignore it. */
        dxgi_vk_swap_chain_present_iteration(chain, 0);
    }

    /* When this is signalled, lets main thread know that it's safe to free user buffers.
     * Signal this just once on the outside since we might have retries, swap_interval > 1, etc, which complicates command buffer recycling. */
    dxgi_vk_swap_chain_present_signal_blit_semaphore(chain);

    /* Signal latency fence. */
    dxgi_vk_swap_chain_signal_waitable_handle(chain);

    /* Signal main thread that we are done with all CPU work.
     * No need to signal a condition variable, main thread can poll to deduce. */
    vkd3d_atomic_uint32_store_explicit(&chain->present.present_count, next_present_count, vkd3d_memory_order_release);

    /* Considerations for implementations without KHR_present_wait which we inherit from the legacy swapchain.
     * To have any hope of achieving reasonable and bounded latency,
     * we need to use the method where we eagerly acquire next image right after present.
     * This avoids any late blocking when presenting, which is a disaster for latency.
     * The present caller must then block on the queue reaching here.
     * To avoid hard deadlocks with present wait-before-signal,
     * a reasonably small timeout is possible.
     * A deadlock in this scenario has only been observed on NVIDIA, which supports present_wait anyways.
     *
     * On implementations with present wait, blocking late is a good idea since it avoids potential
     * GPU bubbles. While blocking here, we cannot submit more work to the GPU on this queue,
     * since we're in a queue callback. */
    if (!chain->wait_thread.active)
    {
        dxgi_vk_swap_chain_try_acquire_next_image(chain);
        /* The old implementation used acquire semaphores, and did not block, so to maintain same behavior,
         * defer blocking on the VkFence. */
        if (vkd3d_native_sync_handle_is_valid(chain->present_request_done_event))
            vkd3d_native_sync_handle_signal(chain->present_request_done_event);
    }

#ifdef VKD3D_ENABLE_BREADCRUMBS
    vkd3d_breadcrumb_tracer_update_barrier_hashes(&chain->queue->device->breadcrumb_tracer);
#endif
}

static void *dxgi_vk_swap_chain_wait_worker(void *chain_)
{
    struct dxgi_vk_swap_chain *chain = chain_;

    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    uint64_t begin_frame_time_ns = 0;
    uint64_t end_frame_time_ns = 0;
    uint64_t next_wait_id = 0;
    int previous_semaphore;

    vkd3d_set_thread_name("vkd3d-swapchain-sync");

    for (;;)
    {
        pthread_mutex_lock(&chain->wait_thread.lock);
        while (!chain->wait_thread.wait_queue_count)
            pthread_cond_wait(&chain->wait_thread.cond, &chain->wait_thread.lock);
        next_wait_id = chain->wait_thread.wait_queue[0].id;
        begin_frame_time_ns = chain->wait_thread.wait_queue[0].begin_frame_time_ns;
        pthread_mutex_unlock(&chain->wait_thread.lock);

        /* Sentinel for swapchain teardown. */
        if (!next_wait_id)
            break;

        /* In skip wait mode we just need to make sure that we signal latency fences properly. */
        if (!chain->wait_thread.skip_waits)
        {
            /* We don't really care if we observed OUT_OF_DATE or something here. */
            VK_CALL(vkWaitForPresentKHR(chain->queue->device->vk_device, chain->present.vk_swapchain,
                    next_wait_id, UINT64_MAX));
        }

        if (begin_frame_time_ns)
            end_frame_time_ns = vkd3d_get_current_time_ns();

        if (vkd3d_native_sync_handle_is_valid(chain->frame_latency_event))
        {
            if ((previous_semaphore = vkd3d_native_sync_handle_release(chain->frame_latency_event, 1)) >= 0)
            {
                if (previous_semaphore >= (int)chain->frame_latency)
                {
                    WARN("Incrementing frame latency semaphore beyond max latency. "
                            "Did application forget to acquire? (new count = %d, max latency = %u)\n",
                            previous_semaphore + 1, chain->frame_latency);
                }
            }
            else
                WARN("Failed to increment swapchain semaphore. Did application forget to acquire?\n");
        }

        if (vkd3d_native_sync_handle_is_valid(chain->frame_latency_event_internal))
            vkd3d_native_sync_handle_release(chain->frame_latency_event_internal, 1);

        if (begin_frame_time_ns)
            INFO("vkWaitForPresentKHR frame latency: %.3f ms.\n", 1e-6 * (end_frame_time_ns - begin_frame_time_ns));

        /* Need to let present tasks know when it's safe to destroy a swapchain.
         * We must have completed all outstanding waits touching VkSwapchainKHR. */
        pthread_mutex_lock(&chain->wait_thread.lock);
        chain->wait_thread.wait_queue_count -= 1;
        memmove(chain->wait_thread.wait_queue, chain->wait_thread.wait_queue + 1,
                chain->wait_thread.wait_queue_count * sizeof(*chain->wait_thread.wait_queue));
        if (chain->wait_thread.wait_queue_count == 0)
            pthread_cond_signal(&chain->wait_thread.cond);
        pthread_mutex_unlock(&chain->wait_thread.lock);
    }

    return NULL;
}

static HRESULT dxgi_vk_swap_chain_init_waiter_thread(struct dxgi_vk_swap_chain *chain)
{
    char env[8];

    if (!chain->queue->device->device_info.present_wait_features.presentWait)
        return S_OK;

    vkd3d_array_reserve((void **)&chain->wait_thread.wait_queue, &chain->wait_thread.wait_queue_size,
            DXGI_MAX_SWAP_CHAIN_BUFFERS, sizeof(*chain->wait_thread.wait_queue));
    pthread_mutex_init(&chain->wait_thread.lock, NULL);
    pthread_cond_init(&chain->wait_thread.cond, NULL);

    /* Have to throw a thread under the bus unfortunately.
     * That thread will only wait on present IDs and release HANDLEs as necessary. */
    if (pthread_create(&chain->wait_thread.thread, NULL, dxgi_vk_swap_chain_wait_worker, chain))
    {
        pthread_mutex_destroy(&chain->wait_thread.lock);
        pthread_cond_destroy(&chain->wait_thread.cond);
        return E_OUTOFMEMORY;
    }

    if (vkd3d_get_env_var("VKD3D_SWAPCHAIN_DEBUG_LATENCY", env, sizeof(env)) && strcmp(env, "1") == 0)
        chain->debug_latency = true;

    INFO("Enabling present wait path for frame latency.\n");
    chain->wait_thread.active = true;
    return S_OK;
}

static HRESULT dxgi_vk_swap_chain_init(struct dxgi_vk_swap_chain *chain, IDXGIVkSurfaceFactory *pFactory,
        const DXGI_SWAP_CHAIN_DESC1 *pDesc, struct d3d12_command_queue *queue)
{
    HRESULT hr;

    chain->IDXGIVkSwapChain_iface.lpVtbl = &dxgi_vk_swap_chain_vtbl;
    chain->refcount = 1;
    chain->queue = queue;
    chain->desc = *pDesc;

    INFO("Creating swapchain (%u x %u), BufferCount = %u.\n",
            pDesc->Width, pDesc->Height, pDesc->BufferCount);

    if (FAILED(hr = dxgi_vk_swap_chain_reallocate_user_buffers(chain)))
        goto err;

    if (FAILED(hr = dxgi_vk_swap_chain_init_sync_objects(chain)))
        goto err;

    if (FAILED(hr = dxgi_vk_swap_chain_create_surface(chain, pFactory)))
        goto err;

    if (FAILED(hr = dxgi_vk_swap_chain_init_waiter_thread(chain)))
        goto err;

    ID3D12CommandQueue_AddRef(&queue->ID3D12CommandQueue_iface);
    return S_OK;

err:
    dxgi_vk_swap_chain_cleanup(chain);
    return hr;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_factory_CreateSwapChain(IDXGIVkSwapChainFactory *iface,
        IDXGIVkSurfaceFactory *pFactory, const DXGI_SWAP_CHAIN_DESC1 *pDesc, IDXGIVkSwapChain **ppSwapchain)
{
    struct dxgi_vk_swap_chain_factory *factory = impl_from_IDXGIVkSwapChainFactory(iface);
    struct dxgi_vk_swap_chain *chain;
    HRESULT hr;

    TRACE("iface %p, pFactory %p, pDesc %p.\n", iface, pFactory, pDesc);

    chain = vkd3d_calloc(1, sizeof(*chain));
    if (!chain)
        return E_OUTOFMEMORY;

    if (FAILED(hr = dxgi_vk_swap_chain_init(chain, pFactory, pDesc, factory->queue)))
    {
        vkd3d_free(chain);
        return hr;
    }

    *ppSwapchain = &chain->IDXGIVkSwapChain_iface;
    return S_OK;
}

static CONST_VTBL struct IDXGIVkSwapChainFactoryVtbl dxgi_vk_swap_chain_factory_vtbl =
{
    /* IUnknown methods */
    dxgi_vk_swap_chain_factory_QueryInterface,
    dxgi_vk_swap_chain_factory_AddRef,
    dxgi_vk_swap_chain_factory_Release,

    /* IDXGIVkSwapChain methods */
    dxgi_vk_swap_chain_factory_CreateSwapChain,
};

HRESULT dxgi_vk_swap_chain_factory_init(struct d3d12_command_queue *queue, struct dxgi_vk_swap_chain_factory *chain)
{
    chain->IDXGIVkSwapChainFactory_iface.lpVtbl = &dxgi_vk_swap_chain_factory_vtbl;
    chain->queue = queue;
    return S_OK;
}
