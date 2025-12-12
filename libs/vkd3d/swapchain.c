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
#include "vkd3d_timestamp_profiler.h"

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

struct low_latency_state
{
    bool mode;
    bool boost;
    uint32_t minimum_interval_us;
};

struct anti_lag_state
{
    bool mode;
    uint32_t max_fps;
};

struct dxgi_vk_swap_chain_present_request
{
    uint64_t begin_frame_time_ns;
    uint32_t user_index;
    DXGI_FORMAT dxgi_format;
    DXGI_COLOR_SPACE_TYPE dxgi_color_space_type;
    DXGI_VK_HDR_METADATA dxgi_hdr_metadata;
    uint32_t swap_interval;
    uint64_t low_latency_frame_id;
    union
    {
        struct low_latency_state requested_low_latency_state;
        struct anti_lag_state requested_anti_lag_state;
    };
    bool low_latency_update_requested;
    bool modifies_hdr_metadata;
};

struct present_wait_entry
{
    uint64_t id;
    uint64_t present_count;
    uint64_t begin_frame_time_ns;
    bool present_timing_enabled;
};

#if defined(_WIN32)
typedef UINT (WINAPI *PFN_NtDelayExecution) (BOOL, LARGE_INTEGER*);
typedef UINT (WINAPI *PFN_NtQueryTimerResolution) (ULONG*, ULONG*, ULONG*);
typedef UINT (WINAPI *PFN_NtSetTimerResolution) (ULONG, BOOL, ULONG*);
#endif

struct platform_sleep_state
{
#if defined(_WIN32)
    PFN_NtDelayExecution NtDelayExecution;
    PFN_NtQueryTimerResolution NtQueryTimerResolution;
    PFN_NtSetTimerResolution NtSetTimerResolution;
#endif

    uint64_t sleep_threshold_ns;
};

struct dxgi_vk_swap_chain
{
    IDXGIVkSwapChain2 IDXGIVkSwapChain_iface;
    struct d3d12_command_queue *queue;

    LONG refcount;
    LONG internal_refcount;
    DXGI_SWAP_CHAIN_DESC1 desc;

    vkd3d_native_sync_handle frame_latency_event;
    vkd3d_native_sync_handle frame_latency_event_internal;
    bool outstanding_present_request;
    uint32_t frame_latency_event_internal_wait_counts;

    UINT frame_latency;
    UINT frame_latency_internal;
    VkSurfaceKHR vk_surface;

    struct low_latency_state requested_low_latency_state;
    bool low_latency_update_requested;

    bool debug_latency;
    bool swapchain_maintenance1;

    struct
    {
        pthread_mutex_t lock;

        /* When requesting feedback, use a specific one if implementation supports multiple. */
        VkPresentStageFlagsEXT present_stage;

        /* If an implementation wants us to keep track of more than 16 time domains
         * at one time, just ignore the extra ones, as that is getting rather ridiculous. */
        uint64_t time_domain_ids[16];
        VkTimeDomainKHR time_domains[16];
        uint64_t calibration[16][2];
        uint32_t time_domains_count;
        uint64_t time_domain_update_count;

        /* Polling functions are extern-sync so defer this to submit thread,
         * not present-wait threads. Wait thread notifies submit thread that it
         * needs to repoll. */
        uint32_t need_properties_repoll_atomic;

        /* Current knowledge of refresh rates. Also allows us to determine VRR. */
        uint64_t refresh_duration;
        uint64_t refresh_interval;
        uint64_t time_properties_update_count;

        /* For implementations which don't support relative timing,
         * we need to do our own accumulation estimates. */
        uint64_t last_absolute_time;
        uint64_t last_absolute_time_domain_id;
        int64_t negative_error;

        /* Feedback so that submit thread knows how to compute future present requests.
         * Wait thread will write to these while holding locks. */
        struct
        {
            uint64_t present_time_domain_id; /* This should be in the time_domain_ids list. */
            uint64_t present_time;
            uint64_t present_count; /* Not the ID, since ID can increment in surprising ways. */
        } feedback;
    } timing;

    struct
    {
        /* When resizing user buffers or emit commands internally,
         * we need to make sure all pending blits have completed on GPU. */
        VkSemaphore vk_internal_blit_semaphore;
        VkSemaphore vk_complete_semaphore;
        uint64_t internal_blit_count;

        /* PresentID is used depending on features and if we're really presenting on-screen. */
        uint64_t present_id;
        bool present_id_valid;
        bool present_target_enabled;

        /* Atomically updated after a PRESENT queue command has processed. Used to atomically check if
         * all outstanding present events have completed on CPU timeline. */
        UINT64 present_count;

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

        VkFence vk_swapchain_fences[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        bool vk_swapchain_fences_signalled[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        uint32_t swapchain_fence_index;

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

        VkPresentModeKHR unlocked_present_mode;
        bool compatible_unlocked_present_mode;
        bool present_mode_forces_fifo;

        /* Info about the current low latency state of the swapchain */
        uint32_t low_latency_present_mode_count;
        VkPresentModeKHR low_latency_present_modes[16];

        pthread_mutex_t low_latency_swapchain_lock;
        pthread_mutex_t low_latency_state_update_lock;

        VkSemaphore low_latency_sem;
        uint64_t low_latency_sem_value;

        struct low_latency_state low_latency_state;

        bool wait; /* If any kind of present wait is supported. */
        bool wait2;
        bool timing;
        bool timing_relative;
        bool timing_absolute;
    } present;

    struct dxgi_vk_swap_chain_present_request request, request_ring[DXGI_MAX_SWAP_CHAIN_BUFFERS];

    struct
    {
        struct d3d12_resource *backbuffers[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        VkImageView vk_image_views[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        uint64_t blit_count;
        uint64_t present_count;
        UINT index;

        DXGI_COLOR_SPACE_TYPE dxgi_color_space_type;
        DXGI_VK_HDR_METADATA dxgi_hdr_metadata;
        bool modifies_hdr_metadata;
        uint64_t begin_frame_time_ns;
    } user;

    struct
    {
        spinlock_t lock;

        uint64_t count;
        uint64_t time;
    } frame_statistics;

    struct
    {
        pthread_mutex_t lock;

        bool enable;
        bool has_user_override;
        uint64_t target_interval_ns;
        uint64_t next_deadline_ns;
        uint64_t heuristic_frame_time_ns;
        uint32_t heuristic_frame_count;

        struct platform_sleep_state sleep_state;
    } frame_rate_limit;

    struct
    {
        VkSurfaceFormatKHR *formats;
		size_t formats_size;
        uint32_t format_count;
        pthread_mutex_t lock;
    } properties;

    struct
    {
        pthread_t thread;
        struct present_wait_entry *wait_queue;
        size_t wait_queue_size;
        size_t wait_queue_count;
        pthread_cond_t cond;
        pthread_mutex_t lock;
        bool skip_waits;

        struct
        {
            uint64_t present_id;
            uint64_t present_count;
        } id_correlation[16];
        unsigned int id_correlation_count;

        /* Detect when we need to signal for repoll. Only
         * accessed by wait thread. */
        uint64_t timing_domain_counter;
        uint64_t timing_property_counter;
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

static void dxgi_vk_swap_chain_ensure_unsignaled_swapchain_fence(struct dxgi_vk_swap_chain *chain, uint32_t index)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkFenceCreateInfo fence_info;
    VkResult vr;

    if (chain->present.vk_swapchain_fences[index])
    {
        if (chain->present.vk_swapchain_fences_signalled[index])
        {
            vr = VK_CALL(vkWaitForFences(chain->queue->device->vk_device,
                    1, &chain->present.vk_swapchain_fences[index], VK_TRUE, UINT64_MAX));

            if (vr)
            {
                ERR("Failed to wait for fences, vr %d\n", vr);
            }
            else
            {
                VK_CALL(vkResetFences(chain->queue->device->vk_device,
                        1, &chain->present.vk_swapchain_fences[index]));
                chain->present.vk_swapchain_fences_signalled[index] = false;
            }
        }
    }
    else
    {
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.pNext = NULL;
        fence_info.flags = 0;
        vr = VK_CALL(vkCreateFence(chain->queue->device->vk_device,
                &fence_info, NULL, &chain->present.vk_swapchain_fences[index]));
        if (vr)
            ERR("Failed to create swapchain fence, vr %d.\n", vr);
    }
}

static void dxgi_vk_swap_chain_drain_swapchain_fences(struct dxgi_vk_swap_chain *chain)
{
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(chain->present.vk_swapchain_fences); i++)
        if (chain->present.vk_swapchain_fences_signalled[i])
            dxgi_vk_swap_chain_ensure_unsignaled_swapchain_fence(chain, i);
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

    /* Wait for pending blits to complete on the GPU */
    dxgi_vk_swap_chain_drain_complete_semaphore(chain, chain->user.present_count);

    if (chain->swapchain_maintenance1)
        dxgi_vk_swap_chain_drain_swapchain_fences(chain);
}

static void dxgi_vk_swap_chain_push_present_id(struct dxgi_vk_swap_chain *chain,
        uint64_t present_count, uint64_t present_id, uint64_t begin_frame_time_ns, bool present_timing_enabled)
{
    struct present_wait_entry *entry;
    pthread_mutex_lock(&chain->wait_thread.lock);
    vkd3d_array_reserve((void **)&chain->wait_thread.wait_queue, &chain->wait_thread.wait_queue_size,
            chain->wait_thread.wait_queue_count + 1, sizeof(*chain->wait_thread.wait_queue));
    entry = &chain->wait_thread.wait_queue[chain->wait_thread.wait_queue_count++];
    entry->id = present_id;
    entry->present_count = present_count;
    entry->begin_frame_time_ns = begin_frame_time_ns;
    entry->present_timing_enabled = present_timing_enabled;
    pthread_cond_signal(&chain->wait_thread.cond);
    pthread_mutex_unlock(&chain->wait_thread.lock);
}

static void dxgi_vk_swap_chain_cleanup_frame_rate_limiter(struct dxgi_vk_swap_chain *chain)
{
    pthread_mutex_destroy(&chain->frame_rate_limit.lock);
}

static void dxgi_vk_swap_chain_cleanup_low_latency(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    if (chain->queue->device->vk_info.NV_low_latency2)
    {
        VK_CALL(vkDestroySemaphore(chain->queue->device->vk_device, chain->present.low_latency_sem, NULL));
        pthread_mutex_destroy(&chain->present.low_latency_swapchain_lock);
        pthread_mutex_destroy(&chain->present.low_latency_state_update_lock);
    }
}

static void dxgi_vk_swap_chain_cleanup_waiter_thread(struct dxgi_vk_swap_chain *chain)
{
    dxgi_vk_swap_chain_push_present_id(chain, 0, 0, 0, true);
    pthread_join(chain->wait_thread.thread, NULL);
    pthread_mutex_destroy(&chain->wait_thread.lock);
    pthread_cond_destroy(&chain->wait_thread.cond);
    vkd3d_free(chain->wait_thread.wait_queue);
}

static void dxgi_vk_swap_chain_cleanup_surface(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VK_CALL(vkDestroySurfaceKHR(chain->queue->device->vkd3d_instance->vk_instance,
            chain->vk_surface, NULL));
    vkd3d_free(chain->properties.formats);
    pthread_mutex_destroy(&chain->properties.lock);
    pthread_mutex_destroy(&chain->timing.lock);
}

static void dxgi_vk_swap_chain_cleanup_sync_objects(struct dxgi_vk_swap_chain *chain)
{
    vkd3d_native_sync_handle_destroy(chain->frame_latency_event);
    vkd3d_native_sync_handle_destroy(chain->frame_latency_event_internal);
}

static void dxgi_vk_swap_chain_cleanup_common(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    unsigned int i;

    VK_CALL(vkDestroySemaphore(chain->queue->device->vk_device, chain->present.vk_internal_blit_semaphore, NULL));
    VK_CALL(vkDestroySemaphore(chain->queue->device->vk_device, chain->present.vk_complete_semaphore, NULL));
    VK_CALL(vkDestroyCommandPool(chain->queue->device->vk_device, chain->present.vk_blit_command_pool, NULL));
    for (i = 0; i < ARRAY_SIZE(chain->present.vk_release_semaphores); i++)
        VK_CALL(vkDestroySemaphore(chain->queue->device->vk_device, chain->present.vk_release_semaphores[i], NULL));
    for (i = 0; i < ARRAY_SIZE(chain->present.vk_backbuffer_image_views); i++)
        VK_CALL(vkDestroyImageView(chain->queue->device->vk_device, chain->present.vk_backbuffer_image_views[i], NULL));
    for (i = 0; i < ARRAY_SIZE(chain->present.vk_acquire_semaphore); i++)
        VK_CALL(vkDestroySemaphore(chain->queue->device->vk_device, chain->present.vk_acquire_semaphore[i], NULL));
    for (i = 0; i < ARRAY_SIZE(chain->present.vk_swapchain_fences); i++)
        VK_CALL(vkDestroyFence(chain->queue->device->vk_device, chain->present.vk_swapchain_fences[i], NULL));

    VK_CALL(vkDestroySwapchainKHR(chain->queue->device->vk_device, chain->present.vk_swapchain, NULL));

    for (i = 0; i < ARRAY_SIZE(chain->user.backbuffers); i++)
    {
        if (chain->user.backbuffers[i])
            vkd3d_resource_decref((ID3D12Resource *)&chain->user.backbuffers[i]->ID3D12Resource_iface);
        VK_CALL(vkDestroyImageView(chain->queue->device->vk_device, chain->user.vk_image_views[i], NULL));
    }
}

static void dxgi_vk_swap_chain_cleanup(struct dxgi_vk_swap_chain *chain)
{
    /* Join with waiter thread first to avoid any sync issues. */
    dxgi_vk_swap_chain_cleanup_waiter_thread(chain);

    dxgi_vk_swap_chain_cleanup_frame_rate_limiter(chain);
    dxgi_vk_swap_chain_cleanup_low_latency(chain);
    dxgi_vk_swap_chain_cleanup_sync_objects(chain);
    dxgi_vk_swap_chain_cleanup_common(chain);

    /* Surface must be destroyed after swapchain. */
    dxgi_vk_swap_chain_cleanup_surface(chain);
}

static inline struct dxgi_vk_swap_chain *impl_from_IDXGIVkSwapChain(IDXGIVkSwapChain2 *iface)
{
    return CONTAINING_RECORD(iface, struct dxgi_vk_swap_chain, IDXGIVkSwapChain_iface);
}

static ULONG STDMETHODCALLTYPE dxgi_vk_swap_chain_AddRef(IDXGIVkSwapChain2 *iface)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    UINT refcount = InterlockedIncrement(&chain->refcount);

    TRACE("iface %p, refcount %u\n", iface, refcount);

    if (refcount == 1)
    {
        dxgi_vk_swap_chain_incref(chain);
        ID3D12CommandQueue_AddRef(&chain->queue->ID3D12CommandQueue_iface);
        d3d12_device_register_swapchain(chain->queue->device, chain);
    }

    return refcount;
}

static ULONG STDMETHODCALLTYPE dxgi_vk_swap_chain_Release(IDXGIVkSwapChain2 *iface)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    struct d3d12_device *device = chain->queue->device;
    struct d3d12_command_queue *queue = chain->queue;
    UINT refcount;

    refcount = InterlockedDecrement(&chain->refcount);
    TRACE("iface %p, refcount %u\n", iface, refcount);

    if (!refcount)
    {
        /* Calling this from the submission thread will result in a deadlock, so
         * drain the swapchain queue now. */
        dxgi_vk_swap_chain_drain_queue(chain);

        if (device->vk_info.NV_low_latency2)
            d3d12_device_remove_swapchain(device, chain);

        dxgi_vk_swap_chain_decref(chain);
        ID3D12CommandQueue_Release(&queue->ID3D12CommandQueue_iface);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_QueryInterface(IDXGIVkSwapChain2 *iface, REFIID riid, void **object)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p\n", iface);
    if (IsEqualGUID(riid, &IID_IUnknown) ||
            IsEqualGUID(riid, &IID_IDXGIVkSwapChain) ||
            IsEqualGUID(riid, &IID_IDXGIVkSwapChain1) ||
            IsEqualGUID(riid, &IID_IDXGIVkSwapChain2))
    {
        dxgi_vk_swap_chain_AddRef(&chain->IDXGIVkSwapChain_iface);
        *object = iface;
        return S_OK;
    }

    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_GetDesc(IDXGIVkSwapChain2 *iface, DXGI_SWAP_CHAIN_DESC1 *pDesc)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p, pDesc %p\n", iface, pDesc);
    *pDesc = chain->desc;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_GetAdapter(IDXGIVkSwapChain2 *iface, REFIID riid, void **object)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p\n", iface);
    return IUnknown_QueryInterface(chain->queue->device->parent, riid, object);
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_GetDevice(IDXGIVkSwapChain2 *iface, REFIID riid, void **object)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p\n", iface);
    return ID3D12Device12_QueryInterface(&chain->queue->device->ID3D12Device_iface, riid, object);
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_GetImage(IDXGIVkSwapChain2 *iface, UINT BufferId, REFIID riid, void **object)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p, BufferId %u.\n", iface, BufferId);
    if (BufferId >= chain->desc.BufferCount)
        return E_INVALIDARG;
    return ID3D12Resource2_QueryInterface(&chain->user.backbuffers[BufferId]->ID3D12Resource_iface, riid, object);
}

static UINT STDMETHODCALLTYPE dxgi_vk_swap_chain_GetImageIndex(IDXGIVkSwapChain2 *iface)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p.\n", iface);
    return chain->user.index;
}

static UINT STDMETHODCALLTYPE dxgi_vk_swap_chain_GetFrameLatency(IDXGIVkSwapChain2 *iface)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p.\n", iface);
    return chain->frame_latency;
}

static HANDLE STDMETHODCALLTYPE dxgi_vk_swap_chain_GetFrameLatencyEvent(IDXGIVkSwapChain2 *iface)
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

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_ChangeProperties(IDXGIVkSwapChain2 *iface, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
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

    if (chain->user.index >= chain->desc.BufferCount)
    {
        /* Need to reset the user index in case the buffer count is lowered.
         * It is unclear if we're allowed to always reset, but employ principle of least surprise. */
        chain->user.index = 0;
    }

#ifndef _WIN32
    /* Non-Win32 platforms (e.g. Wayland) may not be able to detect resizes on its own.
     * We have drained the present queue at this point, so it's safe to poke this bool.
     * If using native builds, it's up to the application to use ResizeBuffers properly. */
    chain->present.force_swapchain_recreation = true;
#endif

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_SetPresentRegion(IDXGIVkSwapChain2 *iface, const RECT *pRegion)
{
    FIXME("iface %p, pRegion %p stub!\n", iface, pRegion);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_SetGammaControl(IDXGIVkSwapChain2 *iface, UINT NumControlPoints, const DXGI_RGB *pControlPoints)
{
    FIXME("iface %p, NumControlPoints %u, pControlPoints %p stub!\n", iface, NumControlPoints, pControlPoints);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_SetFrameLatency(IDXGIVkSwapChain2 *iface, UINT MaxLatency)
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

#ifdef _WIN32
static bool dxgi_vk_swap_chain_present_task_is_idle(struct dxgi_vk_swap_chain *chain)
{
    uint64_t presented_count = vkd3d_atomic_uint64_load_explicit(&chain->present.present_count, vkd3d_memory_order_acquire);
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
#endif

static bool dxgi_vk_swap_chain_present_is_occluded(struct dxgi_vk_swap_chain *chain)
{
#ifdef _WIN32
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
#else
	/* Irrelevant on native build. */
	(void)chain;
	return false;
#endif
}

static void dxgi_vk_swap_chain_present_callback(void *chain);

static void dxgi_vk_swap_chain_wait_internal_handle(struct dxgi_vk_swap_chain *chain, bool low_latency_enable)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    bool non_blocking_internal_handle_wait = low_latency_enable;
    uint64_t completed_submissions = 0;
    uint64_t user_submissions = 0;

    chain->frame_latency_event_internal_wait_counts++;

    if (non_blocking_internal_handle_wait)
    {
        /* If we're using low latency mode, we expect that applications sleep on their own in LatencySleep.
         * If we start sleeping ourselves here, we sometimes end up fighting with NV's LL2 implementation over
         * which sleep cycle gets to dominate. This can manifest as a random pumping pattern.
         *
         * If our sleep dominates, we end up in an unstable situation where LL2 may think we're
         * more CPU bound than we actually are.
         *
         * In a FIFO bound scenario however where GPU completes long before vblank hits,
         * we should rely on frame latency sleeps.
         *
         * Use a very simple heuristic. If the blit timeline semaphore lags behind by 2+ frames, assume we're
         * fully GPU bound and we should back off and let low latency deal with it more gracefully. */
        user_submissions = chain->user.blit_count;

        if (VK_CALL(vkGetSemaphoreCounterValue(chain->queue->device->vk_device,
                chain->present.vk_complete_semaphore,
                &completed_submissions)) == VK_SUCCESS)
        {
            /* We just submitted frame N. If N - 2 is already complete, it means there is <= 2 frames worth of GPU work
             * queued up. For a FIFO bound or CPU bound game, this is the case we expect, so we should use latency fences here.
             * If we're GPU bound with <= 2 frames queued up, we'll likely not block in our own latency handles anyway. */
            if (completed_submissions + 2 >= user_submissions)
            {
                non_blocking_internal_handle_wait = false;
            }
            else if (chain->debug_latency)
            {
                INFO("Completed count: %"PRIu64", submitted count: %"PRIu64". GPU queue is too deep, deferring to low latency sleep.\n",
                        completed_submissions, user_submissions);
            }
        }
        else
        {
            ERR("Failed to query semaphore complete value.\n");
            non_blocking_internal_handle_wait = false;
        }
    }

    if (non_blocking_internal_handle_wait)
    {
        /* Just make sure the counter doesn't get unbounded. */
        while (chain->frame_latency_event_internal_wait_counts &&
                vkd3d_native_sync_handle_acquire_timeout(chain->frame_latency_event_internal, 0))
        {
            chain->frame_latency_event_internal_wait_counts--;
        }
    }
    else
    {
        while (chain->frame_latency_event_internal_wait_counts)
        {
            vkd3d_native_sync_handle_acquire(chain->frame_latency_event_internal);
            chain->frame_latency_event_internal_wait_counts--;
        }
    }
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_Present(IDXGIVkSwapChain2 *iface, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    struct dxgi_vk_swap_chain_present_request *request;
    struct vkd3d_queue_timeline_trace_cookie cookie;
    bool low_latency_enable;

    TRACE("iface %p, SyncInterval %u, PresentFlags #%x, pPresentParameters %p.\n",
            iface, SyncInterval, PresentFlags, pPresentParameters);
    (void)pPresentParameters;

    if (dxgi_vk_swap_chain_present_is_occluded(chain))
        return DXGI_STATUS_OCCLUDED;
    if (PresentFlags & DXGI_PRESENT_TEST)
        return S_OK;

    assert(chain->user.index < chain->desc.BufferCount);

    /* The present iteration on present thread has a similar counter and it will pick up the request from the ring. */
    chain->user.present_count += 1;
    request = &chain->request_ring[chain->user.present_count % ARRAY_SIZE(chain->request_ring)];

    request->swap_interval = SyncInterval;
    request->dxgi_format = chain->user.backbuffers[chain->user.index]->desc.Format;
    request->user_index = chain->user.index;
    request->dxgi_color_space_type = chain->user.dxgi_color_space_type;
    request->dxgi_hdr_metadata = chain->user.dxgi_hdr_metadata;
    request->modifies_hdr_metadata = chain->user.modifies_hdr_metadata;
    request->begin_frame_time_ns = chain->user.begin_frame_time_ns;
    request->low_latency_frame_id = vkd3d_atomic_uint64_load_explicit(
            &chain->queue->device->frame_markers.present, vkd3d_memory_order_acquire);

    if (chain->debug_latency && request->low_latency_frame_id)
        INFO("Presenting with low latency frame ID: %"PRIu64".\n", request->low_latency_frame_id);

    chain->user.modifies_hdr_metadata = false;

    if (chain->queue->device->vk_info.NV_low_latency2)
    {
        pthread_mutex_lock(&chain->present.low_latency_state_update_lock);
        request->requested_low_latency_state = chain->requested_low_latency_state;
        request->low_latency_update_requested = chain->low_latency_update_requested;
        chain->low_latency_update_requested = false;
        low_latency_enable = chain->requested_low_latency_state.mode;
        pthread_mutex_unlock(&chain->present.low_latency_state_update_lock);
    }
    else if (chain->queue->device->device_info.anti_lag_amd.antiLag)
    {
        spinlock_acquire(&chain->queue->device->low_latency_swapchain_spinlock);
        request->requested_anti_lag_state.mode = chain->queue->device->swapchain_info.mode;
        request->requested_anti_lag_state.max_fps = chain->queue->device->swapchain_info.max_fps;
        low_latency_enable = request->requested_anti_lag_state.mode;
        spinlock_release(&chain->queue->device->low_latency_swapchain_spinlock);
    }
    else
    {
        memset(&request->requested_low_latency_state, 0, sizeof(request->requested_low_latency_state));
        request->low_latency_update_requested = false;
        low_latency_enable = false;
    }

    /* Need to process this task in queue thread to deal with wait-before-signal.
     * All interesting works happens in the callback. */
    chain->user.blit_count += 1;
    d3d12_command_queue_enqueue_callback(chain->queue, dxgi_vk_swap_chain_present_callback, chain);

    chain->user.index = (chain->user.index + 1) % chain->desc.BufferCount;

    cookie = vkd3d_queue_timeline_trace_register_present_block(
            &chain->queue->device->queue_timeline_trace,
            chain->queue->device->frame_markers.present ?
                    chain->queue->device->frame_markers.present :
                    chain->user.blit_count);

    /* Relevant if application does not use latency fence, or we force a lower latency through VKD3D_SWAPCHAIN_FRAME_LATENCY overrides. */
    if (vkd3d_native_sync_handle_is_valid(chain->frame_latency_event_internal))
        dxgi_vk_swap_chain_wait_internal_handle(chain, low_latency_enable);

    /* For latency debug purposes. Consider a frame to begin when we return from Present() with the next user index set.
     * This isn't necessarily correct if the application does WaitSingleObject() on the latency right after this call.
     * That call can take up a frame, so the real latency will be lower than the one reported.
     * Otherwise, the estimate should match up with the internal latency fence. */
    if (chain->debug_latency)
        chain->user.begin_frame_time_ns = vkd3d_get_current_time_ns();

    vkd3d_queue_timeline_trace_complete_present_block(
            &chain->queue->device->queue_timeline_trace, cookie);

    return S_OK;
}

static VkColorSpaceKHR convert_color_space(DXGI_COLOR_SPACE_TYPE dxgi_color_space);
static bool dxgi_vk_swap_chain_update_formats_locked(struct dxgi_vk_swap_chain *chain, bool force_requery);

static bool dxgi_vk_swap_chain_supports_color_space(struct dxgi_vk_swap_chain *chain, DXGI_COLOR_SPACE_TYPE ColorSpace)
{
    VkColorSpaceKHR vk_color_space;
    bool ret = false;
    uint32_t i;

    vk_color_space = convert_color_space(ColorSpace);

    pthread_mutex_lock(&chain->properties.lock);
    dxgi_vk_swap_chain_update_formats_locked(chain, false);
    for (i = 0; i < chain->properties.format_count && !ret; i++)
        if (chain->properties.formats[i].colorSpace == vk_color_space)
            ret = true;
    pthread_mutex_unlock(&chain->properties.lock);
    return ret;
}

static UINT STDMETHODCALLTYPE dxgi_vk_swap_chain_CheckColorSpaceSupport(IDXGIVkSwapChain2 *iface, DXGI_COLOR_SPACE_TYPE ColorSpace)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    UINT support_flags = 0;
    if (dxgi_vk_swap_chain_supports_color_space(chain, ColorSpace))
        support_flags |= DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT;
    TRACE("iface %p, ColorSpace %u, SupportFlags #%x.\n", iface, ColorSpace, support_flags);
    return support_flags;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_SetColorSpace(IDXGIVkSwapChain2 *iface, DXGI_COLOR_SPACE_TYPE ColorSpace)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p, ColorSpace %u.\n", iface, ColorSpace);
    if (!dxgi_vk_swap_chain_supports_color_space(chain, ColorSpace))
        return E_INVALIDARG;

    chain->user.dxgi_color_space_type = ColorSpace;
    chain->user.modifies_hdr_metadata = true;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_vk_swap_chain_SetHDRMetaData(IDXGIVkSwapChain2 *iface, const DXGI_VK_HDR_METADATA *pMetaData)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);
    TRACE("iface %p, pMetadata %p.\n", iface, pMetaData);
    chain->user.dxgi_hdr_metadata = *pMetaData;
    chain->user.modifies_hdr_metadata = true;
    return S_OK;
}

static void STDMETHODCALLTYPE dxgi_vk_swap_chain_GetLastPresentCount(IDXGIVkSwapChain2 *iface, UINT64 *present_count)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);

    TRACE("iface %p, present_count %p.\n", iface, present_count);

    /* Represents the number of times Present has been called successfully */
    *present_count = chain->user.present_count;
}

static void STDMETHODCALLTYPE dxgi_vk_swap_chain_GetFrameStatistics(IDXGIVkSwapChain2 *iface,
        DXGI_VK_FRAME_STATISTICS *frame_statistics)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);

    TRACE("iface %p, frame_statistics %p!\n", iface, frame_statistics);

    /* Returns the total number of frames presented, and the
     * timestamp when the last present has completed. */
    spinlock_acquire(&chain->frame_statistics.lock);
    frame_statistics->PresentCount = chain->frame_statistics.count;
    frame_statistics->PresentQPCTime = chain->frame_statistics.time;
    spinlock_release(&chain->frame_statistics.lock);
}

static void STDMETHODCALLTYPE dxgi_vk_swap_chain_SetTargetFrameRate(IDXGIVkSwapChain2 *iface, double frame_rate)
{
    struct dxgi_vk_swap_chain *chain = impl_from_IDXGIVkSwapChain(iface);

    TRACE("iface %p, frame_rate %lf.\n", iface, frame_rate);

    /* Env var takes priority over the display mode and config option */
    if (chain->frame_rate_limit.has_user_override)
        return;

    pthread_mutex_lock(&chain->frame_rate_limit.lock);

    if (frame_rate != 0.0)
    {
        uint64_t target_interval_ns;
        bool reset_stats;
        bool enable;

        /* Target frame rate may be negative, in which case we should only engage the limiter
         * if the measured frame rate is higher than expected, e.g. due to the actual display
         * refresh rate not matching the display mode used for the swap chain. */
        INFO("Set target frame rate to %.1lf FPS.\n", fabs(frame_rate));

        target_interval_ns = (uint64_t)(1.0e9 / fabs(frame_rate));
        enable = frame_rate > 0.0;
        reset_stats = chain->frame_rate_limit.enable != enable ||
                chain->frame_rate_limit.target_interval_ns != target_interval_ns;

        chain->frame_rate_limit.enable = enable;

        if (reset_stats)
        {
            chain->frame_rate_limit.target_interval_ns = target_interval_ns;
            chain->frame_rate_limit.next_deadline_ns = 0;
            chain->frame_rate_limit.heuristic_frame_time_ns = 0;
            chain->frame_rate_limit.heuristic_frame_count = 0;
        }
    }
    else
    {
        if (chain->frame_rate_limit.enable)
            INFO("Disabling frame rate limit.\n");

        chain->frame_rate_limit.enable = false;
        chain->frame_rate_limit.target_interval_ns = 0;
    }

    pthread_mutex_unlock(&chain->frame_rate_limit.lock);
}

static CONST_VTBL struct IDXGIVkSwapChain2Vtbl dxgi_vk_swap_chain_vtbl =
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
    /* IDXGIVkSwapChain1 methods */
    dxgi_vk_swap_chain_GetLastPresentCount,
    dxgi_vk_swap_chain_GetFrameStatistics,
    /* IDXGIVkSwapChain2 methods */
    dxgi_vk_swap_chain_SetTargetFrameRate,
};

static bool dxgi_vk_swap_chain_update_formats_locked(struct dxgi_vk_swap_chain *chain, bool force_requery)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkPhysicalDevice vk_physical_device = chain->queue->device->vk_physical_device;
    VkResult vr;

    if (chain->properties.format_count != 0 && !force_requery)
        return true;

    if ((vr = VK_CALL(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device, chain->vk_surface,
            &chain->properties.format_count, NULL))) < 0)
    {
        ERR("Failed to query surface formats.\n");
        chain->properties.format_count = 0;
        return false;
    }

    if (!vkd3d_array_reserve((void**)&chain->properties.formats, &chain->properties.formats_size,
            chain->properties.format_count, sizeof(*chain->properties.formats)))
    {
        ERR("Failed to allocate memory.\n");
        chain->properties.format_count = 0;
        return false;
    }

    if ((vr = VK_CALL(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device, chain->vk_surface,
            &chain->properties.format_count, chain->properties.formats))) < 0)
    {
        ERR("Failed to query surface formats.\n");
        chain->properties.format_count = 0;
        return false;
    }

    return true;
}

static void dxgi_vk_swap_chain_update_wait_timing_capabilities(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    struct d3d12_device *device = chain->queue->device;
    VkPresentTimingSurfaceCapabilitiesEXT present_timing_caps;
    VkSurfaceCapabilitiesPresentWait2KHR wait2_caps;
    VkPhysicalDeviceSurfaceInfo2KHR surface_info2;
    VkSurfaceCapabilitiesPresentId2KHR id2_caps;
    VkPhysicalDevice vk_physical_device;
    VkSurfaceCapabilities2KHR caps2;

    const VkPresentStageFlagsEXT useful_present_stages =
        VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT |
        VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT |
        VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT;

    vk_physical_device = device->vk_physical_device;

    /* Query present timing and present id2/wait2 support.
     * We can fallback to present wait1, but we lose timing support. */
    surface_info2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
    surface_info2.pNext = NULL;
    surface_info2.surface = chain->vk_surface;
    memset(&caps2, 0, sizeof(caps2));
    caps2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;

    if (device->device_info.present_timing_features.presentTiming)
    {
        memset(&present_timing_caps, 0, sizeof(present_timing_caps));
        present_timing_caps.sType = VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT;
        vk_prepend_struct(&caps2, &present_timing_caps);
    }

    if (device->device_info.present_id2_features.presentId2)
    {
        memset(&id2_caps, 0, sizeof(id2_caps));
        id2_caps.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR;
        vk_prepend_struct(&caps2, &id2_caps);
    }

    if (device->device_info.present_wait2_features.presentWait2)
    {
        memset(&wait2_caps, 0, sizeof(wait2_caps));
        wait2_caps.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_WAIT_2_KHR;
        vk_prepend_struct(&caps2, &wait2_caps);
    }

    VK_CALL(vkGetPhysicalDeviceSurfaceCapabilities2KHR(vk_physical_device, &surface_info2, &caps2));

    chain->present.wait2 = id2_caps.presentId2Supported && wait2_caps.presentWait2Supported;
    chain->present.timing = chain->present.wait2 && present_timing_caps.presentTimingSupported &&
            (present_timing_caps.presentStageQueries & useful_present_stages);
    chain->present.timing_relative = chain->present.timing && present_timing_caps.presentAtRelativeTimeSupported;
    chain->present.timing_absolute = chain->present.timing && present_timing_caps.presentAtAbsoluteTimeSupported;

    chain->present.wait =
            chain->queue->device->device_info.present_wait_features.presentWait ||
            chain->present.wait2;

    if (!chain->present.wait)
        FIXME_ONCE("Implementation supports neither present_wait1 or present_wait2. Latency will increase.\n");

    /* When querying for time, decide on a particular stage.
     * Prefer PIXEL_OUT over PIXEL_VISIBLE, since as far as we know, DXGI does not takes time-to-light into account. */
    if (present_timing_caps.presentStageQueries & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT)
        chain->timing.present_stage = VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT;
    else if (present_timing_caps.presentStageQueries & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT)
        chain->timing.present_stage = VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT;
    else if (present_timing_caps.presentStageQueries & VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT)
        chain->timing.present_stage = VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT;
}

static HRESULT dxgi_vk_swap_chain_create_surface(struct dxgi_vk_swap_chain *chain, IDXGIVkSurfaceFactory *pFactory)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    struct d3d12_device *device = chain->queue->device;
    VkPhysicalDevice vk_physical_device;
    VkInstance vk_instance;
    VkBool32 supported;
    VkResult vr;

    vk_instance = device->vkd3d_instance->vk_instance;
    vk_physical_device = device->vk_physical_device;
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

    pthread_mutex_init(&chain->properties.lock, NULL);
    pthread_mutex_init(&chain->timing.lock, NULL);
    return S_OK;
}

static HRESULT dxgi_vk_swap_chain_init_sync_objects(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkSemaphoreTypeCreateInfoKHR type_info;
    VkSemaphoreCreateInfo create_info;
    unsigned long latency_override;
    VkResult vr;
    char env[8];
    HRESULT hr;

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

    /* Applications can easily forget to increment their latency handles.
     * This means we don't keep latency objects in sync anymore.
     * Maintain our own latency fence to keep things under control.
     * If we don't have present wait, we will sync with present_request_done_event (below) instead.
     * Adding more sync against internal blit fences is meaningless
     * and can cause weird pumping issues in some cases since we're simultaneously syncing
     * against two different timelines.
     * While we used to deduce latency based on BufferCount by default,
     * it was causing various issues, especially on Deck.
     * With 2 frame latency where CPU and GPU are decently subscribed,
     * the present wait event will come in with VBlank-alignment, and that extra delay can be enough to nudge CPU timings to the point
     * where GPU goes falsely idle. This ends up dropping GPU clocks, and we have bad feedback loop effects.
     * This effect has been observed in enough games now that it's too risky to enable that behavior by default. */
    chain->frame_latency_internal = DEFAULT_FRAME_LATENCY;

    if (vkd3d_get_env_var("VKD3D_SWAPCHAIN_LATENCY_FRAMES", env, sizeof(env)))
    {
        latency_override = strtoul(env, NULL, 0);
        if (latency_override >= 1 && latency_override <= DXGI_MAX_SWAP_CHAIN_BUFFERS)
            chain->frame_latency_internal = latency_override;
    }

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
    /* Make sure wait thread is not waiting on anything before we destroy swapchain. */
    pthread_mutex_lock(&chain->wait_thread.lock);

    /* Skip ahead if there are multiple frames queued. */
    chain->wait_thread.skip_waits = true;
    while (chain->wait_thread.wait_queue_count)
        pthread_cond_wait(&chain->wait_thread.cond, &chain->wait_thread.lock);
    chain->wait_thread.skip_waits = false;
    pthread_mutex_unlock(&chain->wait_thread.lock);
}

static void dxgi_vk_swap_chain_destroy_swapchain_in_present_task(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkQueue vk_queue;
    unsigned int i;

    if (!chain->present.vk_swapchain)
        return;

    /* If we are going to destroy the swapchain and the device supports VK_NV_low_latency2
     * take the low latency lock. This ensures none of the other NV low latency functions
     * will attempt to use the stale swapchain handle. */
    if (chain->queue->device->vk_info.NV_low_latency2)
        pthread_mutex_lock(&chain->present.low_latency_swapchain_lock);

    if (chain->swapchain_maintenance1)
    {
        dxgi_vk_swap_chain_drain_swapchain_fences(chain);
    }
    else
    {
        /* Best effort workaround. */
        vk_queue = vkd3d_queue_acquire(chain->queue->vkd3d_queue);
        VK_CALL(vkQueueWaitIdle(vk_queue));
        vkd3d_queue_release(chain->queue->vkd3d_queue);
    }

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
    chain->present.present_target_enabled = false;
    chain->present.present_id = 0;
    chain->present.current_backbuffer_index = UINT32_MAX;

    if (chain->queue->device->vk_info.NV_low_latency2)
        pthread_mutex_unlock(&chain->present.low_latency_swapchain_lock);
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
    bool ret = false;
    uint32_t i;

    pthread_mutex_lock(&chain->properties.lock);
    dxgi_vk_swap_chain_update_formats_locked(chain, false);
    for (i = 0; i < chain->properties.format_count && !ret; i++)
    {
        if (dxgi_vk_swap_chain_accept_format(&chain->properties.formats[i], vk_format) &&
                chain->properties.formats[i].colorSpace == color_space)
        {
            *format = chain->properties.formats[i];
            ret = true;
        }
    }
    pthread_mutex_unlock(&chain->properties.lock);
    return ret;
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

static bool dxgi_vk_swap_chain_find_compatible_unlocked_present_mode(
        struct dxgi_vk_swap_chain *chain,
        VkPresentModeKHR *vk_present_mode,
        uint32_t *vk_min_image_count)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkPhysicalDeviceSurfaceInfo2KHR surface_info2;
    VkSurfacePresentModeCompatibilityKHR compat;
    VkSurfacePresentModeKHR present_mode;
    VkPresentModeKHR present_modes[32];
    VkSurfaceCapabilities2KHR caps2;
    bool has_compatible = false;
    uint32_t i;

    /* swapchain maintenance implies surface maintenance */
    if (!chain->swapchain_maintenance1)
        return false;

    surface_info2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
    surface_info2.pNext = NULL;
    surface_info2.surface = chain->vk_surface;
    present_mode.sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_KHR;
    present_mode.pNext = NULL;
    present_mode.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    vk_prepend_struct(&surface_info2, &present_mode);

    caps2.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
    caps2.pNext = NULL;
    compat.sType = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_KHR;
    compat.pNext = NULL;
    compat.presentModeCount = ARRAY_SIZE(present_modes);
    compat.pPresentModes = present_modes;
    vk_prepend_struct(&caps2, &compat);

    VK_CALL(vkGetPhysicalDeviceSurfaceCapabilities2KHR(chain->queue->device->vk_physical_device,
            &surface_info2, &caps2));

    for (i = 0; !has_compatible && i < compat.presentModeCount; i++)
    {
        if (compat.pPresentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)
        {
            *vk_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            has_compatible = true;
        }
    }

    for (i = 0; !has_compatible && i < compat.presentModeCount; i++)
    {
        if (compat.pPresentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            *vk_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            has_compatible = true;
        }
    }

    if (!has_compatible)
        return false;

    /* This is the count for FIFO specifically. */
    *vk_min_image_count = caps2.surfaceCapabilities.minImageCount;

    caps2.pNext = NULL;
    present_mode.presentMode = *vk_present_mode;
    VK_CALL(vkGetPhysicalDeviceSurfaceCapabilities2KHR(chain->queue->device->vk_physical_device,
            &surface_info2, &caps2));

    /* Query for IMMEDIATE/MAILBOX specifically. */
    *vk_min_image_count = max(*vk_min_image_count, caps2.surfaceCapabilities.minImageCount);
    return true;
}

static void dxgi_vk_swap_chain_set_low_latency_state(struct dxgi_vk_swap_chain *chain, struct low_latency_state *low_latency_state)
{
    /* It is possible that the Vulkan swapchain does not exist when the application sets
     * the low latency state. If that is the case, just update the present latency state
     * and it will be set during dxgi_vk_swap_chain_recreate_swapchain_in_present_task. */
    if (chain->present.vk_swapchain)
    {
        const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
        VkLatencySleepModeInfoNV latency_sleep_mode_info;

        memset(&latency_sleep_mode_info, 0, sizeof(latency_sleep_mode_info));
        latency_sleep_mode_info.sType = VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV;
        latency_sleep_mode_info.pNext = NULL;

        latency_sleep_mode_info.lowLatencyMode = low_latency_state->mode;
        latency_sleep_mode_info.lowLatencyBoost = low_latency_state->boost;
        latency_sleep_mode_info.minimumIntervalUs = low_latency_state->minimum_interval_us;

        VK_CALL(vkSetLatencySleepModeNV(chain->queue->device->vk_device, chain->present.vk_swapchain, &latency_sleep_mode_info));
    }

    chain->present.low_latency_state = *low_latency_state;
}

static void dxgi_vk_swap_chain_low_latency_state_update(struct dxgi_vk_swap_chain *chain)
{
    if (chain->request.low_latency_update_requested)
    {
        if (chain->present.low_latency_state.mode != chain->request.requested_low_latency_state.mode ||
                chain->present.low_latency_state.boost != chain->request.requested_low_latency_state.boost ||
                chain->present.low_latency_state.minimum_interval_us != chain->request.requested_low_latency_state.minimum_interval_us)
        {
            dxgi_vk_swap_chain_set_low_latency_state(chain, &chain->request.requested_low_latency_state);
        }
    }

    if (chain->debug_latency)
    {
        INFO("chain: %p, low latency mode: %s%s (%u us).\n",
                (void *)chain,
                chain->present.low_latency_state.mode ? "ON" : "OFF",
                chain->present.low_latency_state.boost ? " (+ boost)" : "",
                chain->present.low_latency_state.minimum_interval_us);
    }
}

static void dxgi_vk_swap_chain_anti_lag_state_update(struct dxgi_vk_swap_chain *chain)
{
    struct d3d12_device *device = chain->queue->device;

    if (chain->request.low_latency_frame_id)
    {
        const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
        VkAntiLagPresentationInfoAMD present_info;
        VkAntiLagDataAMD anti_lag;
        bool should_signal;

        memset(&anti_lag, 0, sizeof(anti_lag));
        memset(&present_info, 0, sizeof(present_info));
        anti_lag.sType = VK_STRUCTURE_TYPE_ANTI_LAG_DATA_AMD;
        anti_lag.mode = chain->request.requested_anti_lag_state.mode ?
                VK_ANTI_LAG_MODE_ON_AMD : VK_ANTI_LAG_MODE_OFF_AMD;
        anti_lag.maxFPS = chain->request.requested_anti_lag_state.max_fps;
        anti_lag.pPresentationInfo = &present_info;

        present_info.sType = VK_STRUCTURE_TYPE_ANTI_LAG_PRESENTATION_INFO_AMD;
        present_info.frameIndex = chain->request.low_latency_frame_id;
        present_info.stage = VK_ANTI_LAG_STAGE_PRESENT_AMD;

        /* Don't submit the same frame marker over and over. This will probably matter for frame-gen. */
        spinlock_acquire(&chain->queue->device->low_latency_swapchain_spinlock);
        should_signal = present_info.frameIndex > device->frame_markers.consumed_present_id;
        if (should_signal)
            device->frame_markers.consumed_present_id = present_info.frameIndex;
        spinlock_release(&chain->queue->device->low_latency_swapchain_spinlock);

        /* There is no strong requirement that frame index is submitted monotonically,
         * so it should be fine to drop the lock while calling UpdateAMD.
         * We don't want to hold a lock while calling AntiLagUpdateAMD.
         * This is only a theoretical problem if there are two physical queues that concurrently call
         * QueuePresentKHR. */
        if (should_signal)
        {
            TRACE("AntiLag present timeline, frame %"PRIu64".\n", present_info.frameIndex);
            VK_CALL(vkAntiLagUpdateAMD(device->vk_device, &anti_lag));
        }
    }
}

static void dxgi_vk_swap_chain_poll_time_properties(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkSwapchainTimingPropertiesEXT props;

    memset(&props, 0, sizeof(props));
    props.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_TIMING_PROPERTIES_EXT;
    if (VK_CALL(vkGetSwapchainTimingPropertiesEXT(chain->queue->device->vk_device,
            chain->present.vk_swapchain, &props, &chain->timing.time_properties_update_count)) != VK_SUCCESS)
    {
        /* This is expected to happen for the first few frames when running on e.g. Wayland. */

        chain->timing.refresh_duration = 0;
        chain->timing.refresh_interval = 0;
        /* Keep repolling until we get something useful. */
        vkd3d_atomic_uint32_store_explicit(&chain->timing.need_properties_repoll_atomic, 1, vkd3d_memory_order_relaxed);
        return;
    }

    chain->timing.refresh_duration = props.refreshDuration;
    chain->timing.refresh_interval = props.refreshInterval;
}

static void dxgi_vk_swap_chain_poll_time_domains(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkSwapchainTimeDomainPropertiesEXT props;
    uint32_t i;

    memset(&props, 0, sizeof(props));
    props.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT;
    props.timeDomainCount = ARRAY_SIZE(chain->timing.time_domains);
    props.pTimeDomains = chain->timing.time_domains;
    props.pTimeDomainIds = chain->timing.time_domain_ids;
    chain->timing.time_domains_count = 0;

    if (VK_CALL(vkGetSwapchainTimeDomainPropertiesEXT(chain->queue->device->vk_device,
            chain->present.vk_swapchain, &props, &chain->timing.time_domain_update_count)) < 0)
    {
        WARN("Failed to query time domain properties for swapchain.\n");
        return;
    }

    /* VK_TIME_DOMAIN_DEVICE is a little iffy. Prefer using something else so we don't have to
     * consider the current spec holes around timestamps (period and valid bits) and present timing.
     * Only NV proprietary exposes DOMAIN_DEVICE for presentation timings. They support multiple domains anyway,
     * and we can safely ignore it.
     * Ignore QPC domain too for now until we're forced to,
     * since it gets kinda messy w.r.t. setting timestamps. */
    for (i = 0; i < props.timeDomainCount; i++)
    {
        if (chain->timing.time_domains[i] != VK_TIME_DOMAIN_DEVICE_KHR &&
                chain->timing.time_domain_ids[i] != VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR)
        {
            chain->timing.time_domains[chain->timing.time_domains_count] = chain->timing.time_domains[i];
            chain->timing.time_domain_ids[chain->timing.time_domains_count] = chain->timing.time_domain_ids[i];
            chain->timing.time_domains_count++;
        }
    }
}

static void dxgi_vk_swap_chain_poll_calibration(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkSwapchainCalibratedTimestampInfoEXT swapchain_info;
    VkCalibratedTimestampInfoKHR infos[2];
    uint64_t max_deviation;
    unsigned int i;

#ifdef _WIN32
    const VkTimeDomainKHR domain = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR;
#else
    const VkTimeDomainKHR domain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR;
#endif

    memset(infos, 0, sizeof(infos));
    infos[0].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
    infos[0].timeDomain = domain;

    for (i = 0; i < chain->timing.time_domains_count; i++)
    {
        infos[1].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
        infos[1].timeDomain = chain->timing.time_domains[i];

        if (infos[1].timeDomain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT ||
                infos[1].timeDomain == VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT)
        {
            memset(&swapchain_info, 0, sizeof(swapchain_info));
            swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CALIBRATED_TIMESTAMP_INFO_EXT;
            swapchain_info.swapchain = chain->present.vk_swapchain;
            swapchain_info.timeDomainId = chain->timing.time_domain_ids[i];
            swapchain_info.presentStage = chain->timing.present_stage;
            infos[1].pNext = &swapchain_info;
        }

        VK_CALL(vkGetCalibratedTimestampsKHR(chain->queue->device->vk_device, 2, infos,
                chain->timing.calibration[i], &max_deviation));
    }
}

static void dxgi_vk_swap_chain_recreate_swapchain_in_present_task(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkPhysicalDevice vk_physical_device = chain->queue->device->vk_physical_device;
    VkSwapchainLatencyCreateInfoNV swapchain_latency_create_info;
    VkSwapchainPresentModesCreateInfoKHR present_modes_info;
    VkDevice vk_device = chain->queue->device->vk_device;
    VkCommandPoolCreateInfo command_pool_create_info;
    VkSwapchainCreateInfoKHR swapchain_create_info;
    VkPresentModeKHR present_mode_group[2];
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
    pthread_mutex_lock(&chain->properties.lock);
    /* This is only called on an event where we have to recreate the swapchain,
     * re-query format support at this time. It should not be called in the steady state. */
    if (!dxgi_vk_swap_chain_update_formats_locked(chain, true))
    {
        chain->present.is_surface_lost = true;
        pthread_mutex_unlock(&chain->properties.lock);
        return;
    }
    pthread_mutex_unlock(&chain->properties.lock);

    VK_CALL(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_physical_device, chain->vk_surface, &surface_caps));
    dxgi_vk_swap_chain_update_wait_timing_capabilities(chain);

    /* Win32 quirk. Minimized windows have maximum extents of zero. */
    new_occlusion_state = surface_caps.maxImageExtent.width == 0 || surface_caps.maxImageExtent.height == 0;
    vkd3d_atomic_uint32_store_explicit(&chain->present.is_occlusion_state, (uint32_t)new_occlusion_state, vkd3d_memory_order_relaxed);

    /* There is nothing to do. We'll do a dummy present. */
    if (new_occlusion_state)
        return;

    /* Sanity check, this cannot happen on Win32 surfaces, but could happen on Wayland. */
    if (surface_caps.currentExtent.width == UINT32_MAX || surface_caps.currentExtent.height == UINT32_MAX)
    {
        /* TODO: Can add extended interface to query surface size. */
        surface_caps.currentExtent.width = chain->desc.Width;
        surface_caps.currentExtent.height = chain->desc.Height;
    }

    /* No format to present to yet. Can happen in transition states for HDR.
     * Where we have modified color space, but not yet changed user backbuffer format. */
    if (!dxgi_vk_swap_chain_select_format(chain, &surface_format))
        return;

    chain->present.compatible_unlocked_present_mode =
            dxgi_vk_swap_chain_find_compatible_unlocked_present_mode(chain,
                    &chain->present.unlocked_present_mode,
                    &surface_caps.minImageCount);

    memset(&swapchain_create_info, 0, sizeof(swapchain_create_info));
    swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;

    if (chain->present.compatible_unlocked_present_mode)
    {
        /* Just start out in FIFO, we will change it at-will later. */
        present_mode = VK_PRESENT_MODE_FIFO_KHR;

        present_mode_group[0] = present_mode;
        present_mode_group[1] = chain->present.unlocked_present_mode;
        present_modes_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_KHR;
        present_modes_info.pNext = NULL;
        present_modes_info.pPresentModes = present_mode_group;
        present_modes_info.presentModeCount = ARRAY_SIZE(present_mode_group);
        vk_prepend_struct(&swapchain_create_info, &present_modes_info);
        chain->present.present_mode_forces_fifo = false;
    }
    else
    {
        present_mode = chain->request.swap_interval > 0 ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;

        /* Prefer IMMEDIATE over MAILBOX. FIFO is guaranteed to be supported. */
        if (present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR &&
                !dxgi_vk_swap_chain_check_present_mode_support(chain, present_mode))
        {
            if (dxgi_vk_swap_chain_check_present_mode_support(chain, VK_PRESENT_MODE_MAILBOX_KHR))
                present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            else
                present_mode = VK_PRESENT_MODE_FIFO_KHR;
        }

        chain->present.present_mode_forces_fifo = present_mode == VK_PRESENT_MODE_FIFO_KHR;
    }

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
     * We could get away with 2 if we used WSI acquire semaphore and async acquire was supported, but e.g. Mesa does not support that. */
    swapchain_create_info.minImageCount = max(3u, surface_caps.minImageCount);

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

    if (chain->queue->device->vk_info.NV_low_latency2)
    {
        memset(&swapchain_latency_create_info, 0, sizeof(swapchain_latency_create_info));
        swapchain_latency_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV;
        swapchain_latency_create_info.pNext = NULL;
        swapchain_latency_create_info.latencyModeEnable = true;
        vk_prepend_struct(&swapchain_create_info, &swapchain_latency_create_info);
    }

    if (chain->queue->device->vk_info.NV_low_latency2)
        pthread_mutex_lock(&chain->present.low_latency_swapchain_lock);

    if (chain->present.wait2)
        swapchain_create_info.flags |= VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR | VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR;
    if (chain->present.timing)
        swapchain_create_info.flags |= VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT;

    vr = VK_CALL(vkCreateSwapchainKHR(vk_device, &swapchain_create_info, NULL, &chain->present.vk_swapchain));
    if (vr < 0)
    {
        ERR("Failed to create swapchain, vr %d.\n", vr);
        chain->present.vk_swapchain = VK_NULL_HANDLE;
        if (chain->queue->device->vk_info.NV_low_latency2)
            pthread_mutex_unlock(&chain->present.low_latency_swapchain_lock);
        return;
    }

    /* We'll be polling this in the present wait thread, so the queue size can be small,
     * but just use something reasonable to ensure it's never filled. */
    if (chain->present.timing)
    {
        chain->timing.feedback.present_time = 0;
        chain->timing.feedback.present_count = 0;
        chain->timing.feedback.present_time_domain_id = 0;

        dxgi_vk_swap_chain_poll_time_properties(chain);
        dxgi_vk_swap_chain_poll_time_domains(chain);
        dxgi_vk_swap_chain_poll_calibration(chain);

        /* For the first timing requests, ensure we're requesting a sensible time domain. */
        if (chain->timing.time_domains_count)
            chain->timing.feedback.present_time_domain_id = chain->timing.time_domain_ids[0];

        /* This could lead to problems if we have IMMEDIATE, but we ignore present timing for IMMEDIATE or MAILBOX,
         * so there is no real risk of overflow. */
        if (VK_CALL(vkSetSwapchainPresentTimingQueueSizeEXT(vk_device, chain->present.vk_swapchain,
                DXGI_MAX_SWAP_CHAIN_BUFFERS)) != VK_SUCCESS)
        {
            ERR("Failed to set swapchain queue size.\n");
        }
    }

    /* If low latency is supported restore the current low latency state now */
    if (chain->queue->device->vk_info.NV_low_latency2)
    {
        dxgi_vk_swap_chain_set_low_latency_state(chain, &chain->present.low_latency_state);
        pthread_mutex_unlock(&chain->present.low_latency_swapchain_lock);
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

static bool dxgi_vk_swap_chain_request_needs_swapchain_recreation(
        const struct dxgi_vk_swap_chain *chain,
        const struct dxgi_vk_swap_chain_present_request *request,
        const struct dxgi_vk_swap_chain_present_request *last_request)
{
    return request->dxgi_color_space_type != last_request->dxgi_color_space_type ||
            request->dxgi_format != last_request->dxgi_format ||
            ((!!request->swap_interval) != (!!last_request->swap_interval) &&
                    !chain->present.compatible_unlocked_present_mode);
}

static void dxgi_vk_swap_chain_present_signal_blit_semaphore(struct dxgi_vk_swap_chain *chain, uint64_t present_count)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    struct vkd3d_queue_timeline_trace_cookie cookie;
    VkSemaphoreSubmitInfo signal_semaphore_info;
    struct vkd3d_fence_wait_info fence_info;
    VkSubmitInfo2 submit_info;
    VkQueue vk_queue;
    VkResult vr;

    /* Guarantee a 1:1 ratio of Present() calls to increments here. This timeline is
     * used for user thread waiting as well as to delay  */
    memset(&signal_semaphore_info, 0, sizeof(signal_semaphore_info));
    signal_semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_semaphore_info.semaphore = chain->present.vk_complete_semaphore;
    signal_semaphore_info.value = present_count;
    signal_semaphore_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.signalSemaphoreInfoCount = 1;
    submit_info.pSignalSemaphoreInfos = &signal_semaphore_info;

    vk_queue = vkd3d_queue_acquire(chain->queue->vkd3d_queue);
    vr = VK_CALL(vkQueueSubmit2(vk_queue, 1, &submit_info, VK_NULL_HANDLE));
    vkd3d_queue_release(chain->queue->vkd3d_queue);

    /* Mark frame boundary. */
    cookie = vkd3d_queue_timeline_trace_register_swapchain_blit(
            &chain->queue->device->queue_timeline_trace,
            chain->present.present_id_valid ?
                    chain->present.present_id : present_count);

    if (vkd3d_queue_timeline_trace_cookie_is_valid(cookie))
    {
        memset(&fence_info, 0, sizeof(fence_info));
        fence_info.vk_semaphore = chain->present.vk_complete_semaphore;
        fence_info.vk_semaphore_value = chain->present.present_count;

        vkd3d_enqueue_timeline_semaphore(&chain->queue->fence_worker, &fence_info, &cookie);
    }

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
        image_info.imageLayout = d3d12_device_supports_unified_layouts(chain->queue->device) ?
                VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
    VkSemaphoreSubmitInfo signal_semaphore_info[3];
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

    vk_queue = vkd3d_queue_acquire(chain->queue->vkd3d_queue);

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

    /* External submission */
    signal_semaphore_info[2].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_semaphore_info[2].semaphore = chain->queue->vkd3d_queue->submission_timeline;
    signal_semaphore_info[2].value = ++chain->queue->vkd3d_queue->submission_timeline_count;
    signal_semaphore_info[2].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

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
    submit_infos[1].signalSemaphoreInfoCount = 2;
    submit_infos[1].pSignalSemaphoreInfos = &signal_semaphore_info[1];

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

static bool dxgi_vk_swap_chain_setup_present_timing_request(
        struct dxgi_vk_swap_chain *chain, uint64_t present_count, VkPresentTimingInfoEXT *timing_info)
{
    bool frame_limiter_is_floating_cycle = false;
    bool use_present_timing_target;
    uint64_t frame_limiter_ns = 0;

    pthread_mutex_lock(&chain->frame_rate_limit.lock);
    frame_limiter_ns = chain->frame_rate_limit.target_interval_ns;
    pthread_mutex_unlock(&chain->frame_rate_limit.lock);

    timing_info->presentStageQueries = chain->timing.present_stage;
    /* If we're using VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL. */
    timing_info->targetTimeDomainPresentStage = chain->timing.present_stage;

    pthread_mutex_lock(&chain->timing.lock);

    /* This applies to requested feedback as well as targetTime.
     * After swapchain creation, we will use the first supported domain ID. */
    timing_info->timeDomainId = chain->timing.feedback.present_time_domain_id;

    /* If the time domain changed, we cannot trust our accumulator anymore. Restart from feedback. */
    if (chain->timing.last_absolute_time_domain_id != timing_info->timeDomainId)
    {
        chain->timing.last_absolute_time = 0;
        chain->timing.negative_error = 0;
        chain->timing.last_absolute_time_domain_id = timing_info->timeDomainId;
    }

    /* Account for driver bugs (NV on X11, minimized) where images are presented way too soon. */
    chain->timing.last_absolute_time -= chain->timing.negative_error;
    chain->timing.negative_error = 0;

#define DELTA_NS 500000

    /* Present timing targets only work on FIFO modes.
     * Don't bother trying to get timing feedback for non-FIFO modes. */
    use_present_timing_target = chain->request.swap_interval > 0 &&
            present_count > chain->timing.feedback.present_count &&
            chain->timing.feedback.present_count &&
            chain->timing.refresh_duration >= DELTA_NS &&
            (chain->present.timing_absolute || chain->present.timing_relative);

    if (use_present_timing_target && frame_limiter_ns > chain->timing.refresh_duration)
    {
        uint64_t effective_interval = chain->timing.refresh_duration;
        uint64_t align;

        if (chain->timing.refresh_interval != 0 && chain->timing.refresh_interval != UINT64_MAX)
            effective_interval = chain->timing.refresh_interval;

        /* If the limiter requests a rate close enough, we accept that. */
        align = frame_limiter_ns % effective_interval;
        if (align > effective_interval / 256 && align < 255 * effective_interval / 256)
            frame_limiter_is_floating_cycle = true;

        /* If we need to latch onto a fractional cycle and pretend we have
         * done a proper mode change, we might not be able to effectively use present timing targets. */
        if (frame_limiter_is_floating_cycle &&
                chain->timing.refresh_interval != UINT64_MAX &&
                !chain->present.timing_absolute)
        {
            use_present_timing_target = false;
            FIXME_ONCE("Cannot implement fractional present timing with current setup, falling back to CPU limiter.\n");
        }
    }

    if (use_present_timing_target)
    {
        /* It's possible for implementation to report a 60 Hz display that is limited to 30 Hz (think e.g. gamescope).
         * In this case duration is 33.3ms, while interval is 16.6ms.
         * For present interval purposes, we want to use the 16.6ms as base interval. */
        uint64_t effective_interval = chain->timing.refresh_duration;
        if (chain->timing.refresh_interval != 0 && chain->timing.refresh_interval != UINT64_MAX)
            effective_interval = chain->timing.refresh_interval;

        timing_info->targetTime = effective_interval * chain->request.swap_interval;

        pthread_mutex_lock(&chain->frame_rate_limit.lock);
        timing_info->targetTime = max(timing_info->targetTime, chain->frame_rate_limit.target_interval_ns);
        pthread_mutex_unlock(&chain->frame_rate_limit.lock);

        if (chain->timing.refresh_interval == UINT64_MAX)
            frame_limiter_is_floating_cycle = true;

        /* Don't compensate anything for VRR or fractional cycle limiter. */
        if (!frame_limiter_is_floating_cycle)
        {
            if (chain->timing.refresh_interval == 0)
            {
                /* Unknown if VRR or FRR. Assume FRR and add a safety marging for rounding errors,
                 * but don't get wildly off target if it's VRR. */

                if (frame_limiter_ns)
                {
                    /* If we're limiting frame rate at exact cycle rate, we need strict accumulation.
                     * Both VRR and FRR should work here. */
                    timing_info->flags |= VK_PRESENT_TIMING_INFO_PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT_EXT;
                }
                else
                    timing_info->targetTime -= DELTA_NS;
            }
            else
            {
                /* FRR. Use the dedicated "snap to refresh cycle" implementation. */
                timing_info->flags |= VK_PRESENT_TIMING_INFO_PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT_EXT;
            }
        }

        if (!chain->present.timing_relative || (chain->present.timing_absolute && frame_limiter_is_floating_cycle))
        {
            uint64_t minimum_previous_time;
            uint64_t compensation_cycles;
            uint64_t align;

            compensation_cycles = present_count - 1 - chain->timing.feedback.present_count;

            /* It's impossible for the previous present to have completed before this time. */
            minimum_previous_time =
                    chain->timing.feedback.present_time + compensation_cycles * chain->timing.refresh_duration;

            minimum_previous_time = max(minimum_previous_time, chain->timing.last_absolute_time);
            timing_info->targetTime += minimum_previous_time;
            chain->timing.last_absolute_time = timing_info->targetTime;

            align = (chain->timing.last_absolute_time - chain->timing.feedback.present_time) % effective_interval;

            /* Re-align over time to clean up clock drift. For confirmed VRR we don't care, and want a pure pace. */
            if (!frame_limiter_is_floating_cycle)
            {
                if (align > effective_interval / 2)
                    chain->timing.last_absolute_time += min((effective_interval - align) / 8, DELTA_NS);
                else
                    chain->timing.last_absolute_time -= min(align / 8, DELTA_NS);
            }
        }
        else
        {
            timing_info->flags |= VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT;
        }
    }

    pthread_mutex_unlock(&chain->timing.lock);
    return use_present_timing_target;
}

static void dxgi_vk_swap_chain_present_iteration(struct dxgi_vk_swap_chain *chain, uint64_t present_count, unsigned int retry_counter)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkSwapchainPresentFenceInfoKHR present_fence_info;
    VkSwapchainPresentModeInfoKHR present_mode_info;
    VkPresentTimingsInfoEXT timings_info;
    VkPresentTimingInfoEXT timing_info;
    VkPresentModeKHR present_mode;
    VkPresentInfoKHR present_info;
    uint64_t minimum_present_id;
    VkPresentId2KHR present_id2;
    VkPresentIdKHR present_id;
    uint32_t swapchain_index;
    bool swapchain_is_fifo;
    bool use_present_id;
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
            dxgi_vk_swap_chain_present_iteration(chain, present_count, retry_counter + 1);
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

    /* Only mark anti-lag update after we have submitted blit
     * so we don't get wrong associations. */
    if (chain->queue->device->device_info.anti_lag_amd.antiLag)
        dxgi_vk_swap_chain_anti_lag_state_update(chain);

    memset(&present_info, 0, sizeof(present_info));
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pSwapchains = &chain->present.vk_swapchain;
    present_info.swapchainCount = 1;
    present_info.pImageIndices = &swapchain_index;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &chain->present.vk_release_semaphores[swapchain_index];
    present_info.pResults = &vk_result;

    /* Even if application requests IMMEDIATE mode, the WSI implementation may not support it.
     * In this case, we should still opt for using frame latency object to avoid catastrophic latency. */
    swapchain_is_fifo = chain->request.swap_interval > 0 || chain->present.present_mode_forces_fifo;

    /* Only bother with present wait path for FIFO swapchains.
     * Non-FIFO swapchains will pump their frame latency handles through the fallback path of blit command being done.
     * Especially on Xwayland, the present ID is updated when images actually hit on-screen due to MAILBOX behavior.
     * This would unnecessarily stall our progress. */
    if (chain->present.wait && !chain->present.present_id_valid &&
        (swapchain_is_fifo || chain->present.low_latency_state.mode))
    {
        minimum_present_id = chain->present.present_id + 1;
        if (chain->present.low_latency_state.mode)
            chain->present.present_id = chain->request.low_latency_frame_id;
        else
            chain->present.present_id = present_count;

        /* Ensure present ID is increasing monotonically.
         * If application is exceptionally weird, i.e. does not set markers at all,
         * low latency will not work as intended. */
        chain->present.present_id = max(chain->present.present_id, minimum_present_id);

        /* We've now reached the point where any further submissions to any queue cannot affect this frame.
         * wait-before-signal is already resolved. Use a globally monotonic counter for low-latency swapchains. */
        if (chain->present.low_latency_state.mode)
        {
            struct vkd3d_device_frame_markers *markers = &chain->queue->device->frame_markers;
            spinlock_acquire(&chain->queue->device->low_latency_swapchain_spinlock);
            chain->present.present_id = max(chain->present.present_id, markers->consumed_present_id + 1);
            markers->consumed_present_id = chain->present.present_id;
            spinlock_release(&chain->queue->device->low_latency_swapchain_spinlock);
        }

        if (chain->debug_latency)
            INFO("Presenting with frame ID: %"PRIu64".\n", chain->present.present_id);

        if (chain->present.wait2)
        {
            present_id2.sType = VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR;
            present_id2.pNext = NULL;
            present_id2.swapchainCount = 1;
            present_id2.pPresentIds = &chain->present.present_id;
            vk_prepend_struct(&present_info, &present_id2);
        }
        else
        {
            present_id.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
            present_id.pNext = NULL;
            present_id.swapchainCount = 1;
            present_id.pPresentIds = &chain->present.present_id;
            vk_prepend_struct(&present_info, &present_id);
        }

        use_present_id = true;
    }
    else
        use_present_id = false;

    if (chain->present.timing && chain->timing.time_domains_count)
    {
        memset(&timings_info, 0, sizeof(timings_info));
        memset(&timing_info, 0, sizeof(timing_info));

        timings_info.sType = VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT;
        timing_info.sType = VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT;
        timings_info.swapchainCount = 1;
        timings_info.pTimingInfos = &timing_info;

        if (dxgi_vk_swap_chain_setup_present_timing_request(chain, present_count, &timing_info))
            chain->present.present_target_enabled = true;

        if (use_present_id)
            vk_prepend_struct(&present_info, &timings_info);
    }

    if (chain->swapchain_maintenance1)
    {
        chain->present.swapchain_fence_index = (chain->present.swapchain_fence_index + 1) %
                ARRAY_SIZE(chain->present.vk_swapchain_fences);

        present_fence_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR;
        present_fence_info.swapchainCount = 1;
        present_fence_info.pFences = &chain->present.vk_swapchain_fences[chain->present.swapchain_fence_index];
        present_fence_info.pNext = NULL;
        vk_prepend_struct(&present_info, &present_fence_info);

        dxgi_vk_swap_chain_ensure_unsignaled_swapchain_fence(chain, chain->present.swapchain_fence_index);
        chain->present.vk_swapchain_fences_signalled[chain->present.swapchain_fence_index] = true;

        if (chain->present.compatible_unlocked_present_mode)
        {
            present_mode_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_KHR;
            present_mode_info.pNext = NULL;
            present_mode_info.swapchainCount = 1;
            present_mode_info.pPresentModes = &present_mode;
            present_mode = chain->request.swap_interval > 0 ?
                    VK_PRESENT_MODE_FIFO_KHR : chain->present.unlocked_present_mode;
            vk_prepend_struct(&present_info, &present_mode_info);
        }
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

    if (use_present_id && vr >= 0)
        chain->present.present_id_valid = true;

    vkd3d_queue_timeline_trace_register_instantaneous(&chain->queue->device->queue_timeline_trace,
            VKD3D_QUEUE_TIMELINE_TRACE_STATE_TYPE_QUEUE_PRESENT,
            chain->present.present_id_valid ? chain->present.present_id : 0);

    /* Handle any errors and retry as needed. If we cannot make meaningful forward progress, just give up and retry later. */
    if (vr == VK_SUBOPTIMAL_KHR || vr < 0)
        chain->present.force_swapchain_recreation = true;
    if (vr < 0)
        dxgi_vk_swap_chain_destroy_swapchain_in_present_task(chain);

    if (vr == VK_ERROR_OUT_OF_DATE_KHR)
    {
        if (retry_counter < 3)
            dxgi_vk_swap_chain_present_iteration(chain, present_count, retry_counter + 1);
    }
    else if (vr == VK_ERROR_SURFACE_LOST_KHR)
    {
        /* If the surface is lost, we cannot expect to get forward progress. Just keep rendering to nothing. */
        chain->present.is_surface_lost = true;
    }
}

static void dxgi_vk_swap_chain_signal_waitable_handle(struct dxgi_vk_swap_chain *chain, uint64_t present_count)
{
    uint64_t present_id = chain->present.present_id_valid ? chain->present.present_id : 0;

    dxgi_vk_swap_chain_push_present_id(chain, present_count, present_id, chain->request.begin_frame_time_ns,
            chain->present.present_target_enabled);
}

static void dxgi_vk_swap_chain_delay_next_frame(struct dxgi_vk_swap_chain *chain, uint64_t current_time_ns);

static void dxgi_vk_swap_chain_update_present_timing(struct dxgi_vk_swap_chain *chain)
{
    if (!chain->present.vk_swapchain)
        return;

    /* Wait thread can signal that counters changed, so we should repoll if refresh rates change, etc.
     * These calls are extern-sync, so we cannot do them on the thread easily. */
    if (vkd3d_atomic_uint32_exchange_explicit(&chain->timing.need_properties_repoll_atomic, 0, vkd3d_memory_order_acquire))
    {
        dxgi_vk_swap_chain_poll_time_properties(chain);

        /* Waiter thread looks like time domains and calibration state. */
        pthread_mutex_lock(&chain->timing.lock);
        dxgi_vk_swap_chain_poll_time_domains(chain);
        dxgi_vk_swap_chain_poll_calibration(chain);
        pthread_mutex_unlock(&chain->timing.lock);
    }
    else if (chain->present.present_count % 64 == 0)
    {
        /* Regularly recalibrate. */
        pthread_mutex_lock(&chain->timing.lock);
        dxgi_vk_swap_chain_poll_calibration(chain);
        pthread_mutex_unlock(&chain->timing.lock);
    }
}

static void dxgi_vk_swap_chain_present_callback(void *chain_)
{
    const struct dxgi_vk_swap_chain_present_request *next_request;
    struct dxgi_vk_swap_chain *chain = chain_;
    uint64_t next_present_count;

    next_present_count = chain->present.present_count + 1;
    next_request = &chain->request_ring[next_present_count % ARRAY_SIZE(chain->request_ring)];
    if (dxgi_vk_swap_chain_request_needs_swapchain_recreation(chain, next_request, &chain->request))
        chain->present.force_swapchain_recreation = true;

    chain->request = *next_request;
    if (chain->request.modifies_hdr_metadata)
        dxgi_vk_swap_chain_set_hdr_metadata(chain);

    if (chain->queue->device->vk_info.NV_low_latency2)
        dxgi_vk_swap_chain_low_latency_state_update(chain);

    if (chain->present.timing)
        dxgi_vk_swap_chain_update_present_timing(chain);

    /* If no QueuePresentKHRs successfully commits a present ID, we'll fallback to a normal queue signal. */
    chain->present.present_id_valid = false;
    chain->present.present_target_enabled = false;

    /* A present iteration may or may not render to backbuffer. We'll apply best effort here.
     * Forward progress must be ensured, so if we cannot get anything on-screen in a reasonable amount of retries, ignore it. */
    dxgi_vk_swap_chain_present_iteration(chain, next_present_count, 0);

    /* When this is signalled, lets main thread know that it's safe to free user buffers.
     * Signal this just once on the outside since we might have retries, which complicates command buffer recycling. */
    dxgi_vk_swap_chain_present_signal_blit_semaphore(chain, next_present_count);

    /* Signal latency fence. */
    dxgi_vk_swap_chain_signal_waitable_handle(chain, next_present_count);

    /* Signal main thread that we are done with all CPU work.
     * No need to signal a condition variable, main thread can poll to deduce. */
    vkd3d_atomic_uint64_store_explicit(&chain->present.present_count, next_present_count, vkd3d_memory_order_release);

#ifdef VKD3D_ENABLE_BREADCRUMBS
    vkd3d_breadcrumb_tracer_update_barrier_hashes(&chain->queue->device->breadcrumb_tracer);
#endif

#ifdef VKD3D_ENABLE_PROFILING
    vkd3d_timestamp_profiler_mark_frame_boundary(chain->queue->device->timestamp_profiler);
#endif
}

static void dxgi_vk_swap_chain_update_past_presentation(struct dxgi_vk_swap_chain *chain,
        uint64_t present_id, uint64_t time, VkTimeDomainKHR time_domain, uint64_t time_domain_id,
        uint64_t time_domain_counter)
{
    uint64_t present_count = UINT64_MAX;
    bool valid_time_domain;
    unsigned int i;

    if (present_id)
    {
        for (i = 0; i < chain->wait_thread.id_correlation_count; i++)
        {
            if (chain->wait_thread.id_correlation[i].present_id == present_id)
            {
                present_count = chain->wait_thread.id_correlation[i].present_count;
                chain->wait_thread.id_correlation[i] =
                        chain->wait_thread.id_correlation[--chain->wait_thread.id_correlation_count];
                break;
            }
        }
    }

    /* Unfortunately, we're not allowed to calibrate timestamps here due to unfortunate externsync rules.
     * Expect that we have obtained correlations from present threads.
     * This can only freak out if the implementation is changing time domains under our feet,
     * which should only happen in extreme circumstances and intermittently. */
    pthread_mutex_lock(&chain->timing.lock);

    if (present_count != UINT64_MAX)
    {
        chain->timing.feedback.present_time = time;
        chain->timing.feedback.present_count = present_count;

        /* This will generally match the domain ID that we passed into QueuePresentKHR,
         * but it doesn't necessarily have to. The new times are in terms of that domain ID
         * and the QueuePresentKHR thread will repoll the time domains as needed. */
        chain->timing.feedback.present_time_domain_id = time_domain_id;
    }
    else
    {
        if (present_id != 0)
        {
            FIXME("Could not correlate present ID with present count.\n");
        }
        else
        {
            /* If we're doing IMMEDIATE, we drop the use of present timing.
             * Wait until we re-latch to actual FIFO. */
            chain->timing.feedback.present_time = 0;
            chain->timing.feedback.present_count = 0;
            chain->timing.feedback.present_time_domain_id = 0;
        }

        goto unlock;
    }

    /* NV bug: Time domain counter is always returned as 0, but it's fine. */
    valid_time_domain = (time_domain_counter == 0 &&
            chain->queue->device->device_info.vulkan_1_2_properties.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY) ||
            time_domain_counter == chain->timing.time_domain_update_count;

    if (!valid_time_domain)
        FIXME_ONCE("Time domain for feedback is not valid, cannot get accurate timestamp.\n");

    for (i = 0; i < chain->timing.time_domains_count; i++)
    {
        if (chain->timing.time_domain_ids[i] == time_domain_id && chain->timing.time_domains[i] == time_domain)
        {
            int64_t delta;

            /* This can happen. */
            if (time == 0)
            {
                FIXME_ONCE("Proper time is not returned for presentation time query.\n");
                break;
            }

            delta = (int64_t)time - (int64_t)chain->timing.calibration[i][1];

#ifdef _WIN32
            {
                LARGE_INTEGER li;
                QueryPerformanceFrequency(&li);
                delta = (int64_t)((double)delta * ((double)li.QuadPart / 1e9));
            }
#endif

            time = chain->timing.calibration[i][0] + delta;

            if (present_count > chain->frame_statistics.count)
            {
                /* GetPastPresentationTime is by default in-order with QueuePresentKHR calls,
                 * but if we have committed to a present count due to fallbacks or similar,
                 * don't override it. */
                spinlock_acquire(&chain->frame_statistics.lock);
                chain->frame_statistics.count = present_count;
                chain->frame_statistics.time = max(chain->frame_statistics.time, time);
                spinlock_release(&chain->frame_statistics.lock);
            }
            break;
        }
    }

    if (i == chain->timing.time_domains_count)
        FIXME_ONCE("Implementation reported timestamps in a time domain we don't know about yet.\n");

unlock:
    pthread_mutex_unlock(&chain->timing.lock);
}

static void dxgi_vk_swap_chain_poll_past_presentation(struct dxgi_vk_swap_chain *chain)
{
    VkPastPresentationTimingEXT timings[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    VkPresentStageTimeEXT times[DXGI_MAX_SWAP_CHAIN_BUFFERS];
    const struct vkd3d_vk_device_procs *vk_procs;
    VkPastPresentationTimingPropertiesEXT props;
    VkPastPresentationTimingInfoEXT timing_info;
    struct d3d12_device *device;
    bool request_props_repoll;
    VkResult vr;
    uint32_t i;

    device = chain->queue->device;
    vk_procs = &device->vk_procs;

    memset(&timing_info, 0, sizeof(timing_info));
    timing_info.sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT;
    timing_info.swapchain = chain->present.vk_swapchain;

    memset(&props, 0, sizeof(props));
    props.sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT;
    props.presentationTimingCount = ARRAY_SIZE(timings);
    props.pPresentationTimings = timings;

    for (i = 0; i < ARRAY_SIZE(timings); i++)
    {
        timings[i].sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_EXT;
        timings[i].pNext = NULL;

        /* We only request feedback for a single stage. */
        timings[i].presentStageCount = 1;
        timings[i].pPresentStages = &times[i];
    }

    vr = VK_CALL(vkGetPastPresentationTimingEXT(device->vk_device, &timing_info, &props));
    if (vr < 0)
        return;

    /* Workaround NV driver bug. They will report timeDomainsCounter = 0 and timingPropertiesCounter = 0 here.
     * Don't force repoll when that happens. */
    request_props_repoll = (props.timeDomainsCounter != 0 || props.timingPropertiesCounter != 0) &&
            (props.timeDomainsCounter != chain->wait_thread.timing_domain_counter ||
             props.timingPropertiesCounter != chain->wait_thread.timing_property_counter);

    if (request_props_repoll)
    {
        chain->wait_thread.timing_domain_counter = props.timeDomainsCounter;
        chain->wait_thread.timing_property_counter = props.timingPropertiesCounter;
        vkd3d_atomic_uint32_store_explicit(&chain->timing.need_properties_repoll_atomic, 1, vkd3d_memory_order_release);
    }

    for (i = 0; i < props.presentationTimingCount; i++)
    {
        if (!timings[i].reportComplete)
        {
            /* This really shouldn't happen. */
            ERR("Implementation bug, report is not marked complete.\n");
            continue;
        }

        if (!chain->present.timing_relative && timings[i].targetTime)
        {
            int64_t error_ns = times[i].time - timings[i].targetTime;
            if (chain->debug_latency)
                INFO("Absolute timing error: %.3f ms.\n", (double)error_ns * 1e-6);

            if (error_ns < 0)
            {
                if (error_ns < -10000000)
                    FIXME_ONCE("Driver bug, targetTime reported is way early.\n");

                /* NV driver bug when minimizing. It seems to ignore targetTime in this case and happily
                 * blasts ahead at full rate. */
                pthread_mutex_lock(&chain->timing.lock);
                chain->timing.negative_error = -error_ns;
                pthread_mutex_unlock(&chain->timing.lock);
            }
        }

        if (times[i].stage != chain->timing.present_stage)
        {
            /* This really shouldn't happen. */
            FIXME("Present stage recieved is different from requested stage.\n");
            continue;
        }

        dxgi_vk_swap_chain_update_past_presentation(chain,
                timings[i].presentId, times[i].time, timings[i].timeDomain, timings[i].timeDomainId,
                props.timeDomainsCounter);
    }
}

static void dxgi_vk_swap_chain_update_frame_statistics(struct dxgi_vk_swap_chain *chain,
        uint64_t present_count, uint64_t present_id)
{
    uint64_t time;

    if (chain->present.timing)
    {
        dxgi_vk_swap_chain_poll_past_presentation(chain);
        if (chain->frame_statistics.count < present_count && present_id)
        {
            /* Vulkan spec does not guarantee this, but it's unclear if we are allowed to
             * skip or arbitrarily delay reports in DXGI.
             * The implementations we have tested support this guarantee, however.
             * We can probably improve this situation if need be. */
            FIXME_ONCE("Implementation does not seem to support immediate reports of frame statistics.\n");
        }
    }

    if (chain->frame_statistics.count < present_count)
    {
        /* Fallback to sampling on CPU if we didn't get the actual timestamp. */
#ifdef _WIN32
        /* DXGI returns the raw QPC value */
        LARGE_INTEGER li;
        QueryPerformanceCounter(&li);
        time = li.QuadPart;
#else
        time = vkd3d_get_current_time_ns();
#endif

        spinlock_acquire(&chain->frame_statistics.lock);
        chain->frame_statistics.count = present_count;
        chain->frame_statistics.time = max(chain->frame_statistics.time, time);
        spinlock_release(&chain->frame_statistics.lock);
    }
}

static void dxgi_vk_swap_chain_platform_sleep_for_ns(struct platform_sleep_state *sleep_state, uint64_t duration_ns)
{
#if defined(_WIN32)
    LARGE_INTEGER ticks;

    if (sleep_state->NtDelayExecution)
    {
        /* Ticks are in units of 100ns, with negative values indicating a
         * duration and positive values an absolute time in tick counts. */
        ticks.QuadPart = (int64_t)duration_ns / -100;

        /* Set alertable = false to avoid wineserver round-trips */
        sleep_state->NtDelayExecution(FALSE, &ticks);
    }
    else
    {
        Sleep(duration_ns / 1000000);
    }
#else
    struct timespec duration;

    (void)sleep_state;

    duration.tv_sec = duration_ns / 1000000000;
    duration.tv_nsec = duration_ns % 1000000000;

    /* ignore interrupts */
    nanosleep(&duration, NULL);
#endif
}

static void dxgi_vk_swap_chain_delay_next_frame(struct dxgi_vk_swap_chain *chain, uint64_t current_time_ns)
{
    struct platform_sleep_state *sleep_state = &chain->frame_rate_limit.sleep_state;
    uint64_t window_start_time_ns, window_total_time_ns, window_expected_time_ns;
    uint32_t frame_count_min, frame_count_max, frame_count;
    uint64_t sleep_threshold_ns, sleep_duration_ns;
    uint32_t frame_latency = DEFAULT_FRAME_LATENCY;
    static const uint32_t max_window_size = 128u;
    static const uint32_t min_window_size = 8u;
    uint64_t next_deadline_ns = 0;

    pthread_mutex_lock(&chain->frame_rate_limit.lock);

    if (chain->frame_rate_limit.target_interval_ns)
    {
        if (!chain->frame_rate_limit.enable)
        {
            /* Ignore app-provided frame latency since it may not be reliable */
            if (chain->present.wait)
                frame_latency = chain->frame_latency_internal;

            frame_count = chain->frame_rate_limit.heuristic_frame_count;

            if (frame_count >= min_window_size)
            {
                window_start_time_ns = chain->frame_rate_limit.heuristic_frame_time_ns;
                window_total_time_ns = current_time_ns - window_start_time_ns;

                window_expected_time_ns = frame_count * chain->frame_rate_limit.target_interval_ns;

                frame_count_min = frame_count - 1;
                frame_count_max = frame_count + frame_latency;

                if ((frame_count_max * window_total_time_ns) < (frame_count * window_expected_time_ns))
                {
                    /* Frame delivery has been faster than the refresh rate even
                     * accounting for swap chain buffering, enable limiter. */
                    chain->frame_rate_limit.enable = true;

                    INFO("Measured frame rate of %.1lf FPS exceeds desired refresh rate of %.1lf Hz, enabling limiter.\n",
                            1.0e9 / (double)(window_total_time_ns) * (double)(frame_count),
                            1.0e9 / (double)(chain->frame_rate_limit.target_interval_ns));
                }
                else if ((frame_count_min * window_total_time_ns) > (frame_count * window_expected_time_ns) ||
                        (frame_count >= max_window_size))
                {
                    /* Frame rate has been lower than the refresh rate, reset frame window. */
                    chain->frame_rate_limit.heuristic_frame_count = 0;
                    chain->frame_rate_limit.heuristic_frame_time_ns = 0;
                }
            }
        }

        if (chain->frame_rate_limit.enable)
        {
            next_deadline_ns = chain->frame_rate_limit.next_deadline_ns;

            /* Each frame is assigned a time window [t0,t1) during which presentation is expected to
             * complete, with its duration being equal to one frame interval. If a frame completes
             * before t1, just advance the next frame's time window by one frame interval and sleep
             * as necessary, otherwise reset the sequence using the current time as a starting point,
             * which should roughly line up with a display refresh. */
            if (current_time_ns < next_deadline_ns + chain->frame_rate_limit.target_interval_ns)
                chain->frame_rate_limit.next_deadline_ns += chain->frame_rate_limit.target_interval_ns;
            else
                chain->frame_rate_limit.next_deadline_ns = current_time_ns + chain->frame_rate_limit.target_interval_ns;
        }
        else
        {
            /* Advance or initialize the frame window used to measure the current frame rate. */
            if (!chain->frame_rate_limit.heuristic_frame_time_ns)
                chain->frame_rate_limit.heuristic_frame_time_ns = current_time_ns;

            chain->frame_rate_limit.heuristic_frame_count++;
        }
    }

    pthread_mutex_unlock(&chain->frame_rate_limit.lock);

    if (current_time_ns >= next_deadline_ns)
        return;

    /* Busy-wait for the last couple of milliseconds for accuracy, only use the
     * platform's sleep function for durations longer than the threshold, which
     * is an estimated error based on the timer interval and sleep duration. */
    sleep_duration_ns = next_deadline_ns - current_time_ns;
    sleep_threshold_ns = sleep_state->sleep_threshold_ns + sleep_duration_ns / 6;

    while (sleep_duration_ns > sleep_threshold_ns)
    {
        dxgi_vk_swap_chain_platform_sleep_for_ns(sleep_state, sleep_duration_ns - sleep_threshold_ns);

        current_time_ns = vkd3d_get_current_time_ns();
        sleep_duration_ns = next_deadline_ns > current_time_ns ? next_deadline_ns - current_time_ns : 0;
    }

    while (current_time_ns < next_deadline_ns)
    {
        vkd3d_pause();
        current_time_ns = vkd3d_get_current_time_ns();
    }
}

static void *dxgi_vk_swap_chain_wait_worker(void *chain_)
{
    struct dxgi_vk_swap_chain *chain = chain_;

    struct vkd3d_queue_timeline_trace *timeline_trace = &chain->queue->device->queue_timeline_trace;
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    struct vkd3d_queue_timeline_trace_cookie cookie;
    struct present_wait_entry entry;
    uint64_t end_frame_time_ns = 0;
    int previous_semaphore;

    vkd3d_set_thread_name("vkd3d-swapchain-sync");

    for (;;)
    {
        pthread_mutex_lock(&chain->wait_thread.lock);
        while (!chain->wait_thread.wait_queue_count)
            pthread_cond_wait(&chain->wait_thread.cond, &chain->wait_thread.lock);
        entry = chain->wait_thread.wait_queue[0];
        pthread_mutex_unlock(&chain->wait_thread.lock);

        /* Sentinel for swapchain teardown. */
        if (!entry.present_count)
            break;

        if (entry.id)
        {
            cookie = vkd3d_queue_timeline_trace_register_present_wait(timeline_trace, entry.id);
            /* In skip wait mode we just need to make sure that we signal latency fences properly. */
            if (!chain->wait_thread.skip_waits)
            {
                /* We don't really care if we observed OUT_OF_DATE or something here. */
                if (chain->present.wait2)
                {
                    VkPresentWait2InfoKHR wait_info;
                    memset(&wait_info, 0, sizeof(wait_info));
                    wait_info.sType = VK_STRUCTURE_TYPE_PRESENT_WAIT_2_INFO_KHR;
                    wait_info.presentId = entry.id;
                    wait_info.timeout = UINT64_MAX;
                    VK_CALL(vkWaitForPresent2KHR(chain->queue->device->vk_device, chain->present.vk_swapchain,
                            &wait_info));
                }
                else
                {
                    VK_CALL(vkWaitForPresentKHR(chain->queue->device->vk_device, chain->present.vk_swapchain,
                            entry.id, UINT64_MAX));
                }
            }
            vkd3d_queue_timeline_trace_complete_present_wait(timeline_trace, cookie);
        }
        else
        {
            dxgi_vk_swap_chain_drain_complete_semaphore(chain, entry.present_count);
        }

        end_frame_time_ns = vkd3d_get_current_time_ns();

        if (chain->present.wait && !entry.present_timing_enabled)
            dxgi_vk_swap_chain_delay_next_frame(chain, end_frame_time_ns);

        /* If we're rendering with IMMEDIATE, we ignore present timing. */
        if (chain->present.timing && entry.id)
        {
            if (chain->wait_thread.id_correlation_count == ARRAY_SIZE(chain->wait_thread.id_correlation))
            {
                /* Shouldn't really happen, but if it does for whatever reason, just nuke the list. */
                FIXME("ID correlation list filled. Flushing ... Are present timing requests not properly returned by implementation?\n");
                chain->wait_thread.id_correlation_count = 0;
            }

            chain->wait_thread.id_correlation[chain->wait_thread.id_correlation_count].present_count = entry.present_count;
            chain->wait_thread.id_correlation[chain->wait_thread.id_correlation_count].present_id = entry.id;
            chain->wait_thread.id_correlation_count++;
        }

        dxgi_vk_swap_chain_update_frame_statistics(chain, entry.present_count, entry.id);

        if (vkd3d_native_sync_handle_is_valid(chain->frame_latency_event))
        {
            if ((previous_semaphore = vkd3d_native_sync_handle_release(chain->frame_latency_event, 1)) >= 0)
            {
                /* Some applications can keep using MaxLatency of 1,
                 * but just avoid acquiring the semaphore a few times to achieve higher latency.
                 * Only warn if the counter becomes unreasonably large, e.g. 4+ frames.
                 * That's a good sign the application is actually not behaving correctly. */
                if (previous_semaphore >= (int)chain->frame_latency + 4)
                {
                    TRACE("Incrementing frame latency semaphore beyond max latency. "
                            "Did application forget to acquire? (new count = %d, max latency = %u)\n",
                            previous_semaphore + 1, chain->frame_latency);
                }
            }
            else
                TRACE("Failed to increment swapchain semaphore. Did application forget to acquire?\n");
        }

        if (vkd3d_native_sync_handle_is_valid(chain->frame_latency_event_internal))
            vkd3d_native_sync_handle_release(chain->frame_latency_event_internal, 1);

        if (entry.id && entry.begin_frame_time_ns)
            INFO("vkWaitForPresentKHR frame latency: %.3f ms.\n", 1e-6 * (end_frame_time_ns - entry.begin_frame_time_ns));

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

    spinlock_init(&chain->frame_statistics.lock);

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

    return S_OK;
}

static HRESULT dxgi_vk_swap_chain_init_low_latency(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkPhysicalDevice vk_physical_device = chain->queue->device->vk_physical_device;

    VkLatencySurfaceCapabilitiesNV latency_surface_caps;
    VkSemaphoreTypeCreateInfoKHR semaphore_type_info;
    VkPhysicalDeviceSurfaceInfo2KHR surface_info;
    VkSurfaceCapabilities2KHR surface_caps;
    VkSemaphoreCreateInfo semaphore_info;
    VkResult vr;

    chain->present.low_latency_present_mode_count = 0;

    chain->present.low_latency_sem = VK_NULL_HANDLE;
    chain->present.low_latency_sem_value = 0;

    chain->present.low_latency_state.mode = false;
    chain->present.low_latency_state.boost = false;
    chain->present.low_latency_state.minimum_interval_us = 0;

    if (chain->queue->device->vk_info.NV_low_latency2)
    {
        memset(&surface_info, 0, sizeof(surface_info));
        surface_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
        surface_info.pNext = NULL;
        surface_info.surface = chain->vk_surface;

        memset(&latency_surface_caps, 0, sizeof(latency_surface_caps));
        latency_surface_caps.sType = VK_STRUCTURE_TYPE_LATENCY_SURFACE_CAPABILITIES_NV;
        latency_surface_caps.presentModeCount = ARRAY_SIZE(chain->present.low_latency_present_modes);
        latency_surface_caps.pPresentModes = chain->present.low_latency_present_modes;

        memset(&surface_caps, 0, sizeof(surface_caps));
        surface_caps.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
        surface_caps.pNext = &latency_surface_caps;

        if ((vr = VK_CALL(vkGetPhysicalDeviceSurfaceCapabilities2KHR(vk_physical_device, &surface_info,
                &surface_caps))) < 0)
        {
            ERR("Failed to query latency surface capabilities count, vr %d.\n", vr);
            return hresult_from_vk_result(vr);
        }

        chain->present.low_latency_present_mode_count = latency_surface_caps.presentModeCount;

        memset(&semaphore_type_info, 0, sizeof(semaphore_type_info));
        semaphore_type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
        semaphore_type_info.pNext = NULL;
        semaphore_type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
        semaphore_type_info.initialValue = 0;

        memset(&semaphore_info, 0, sizeof(semaphore_info));
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_info.pNext = &semaphore_type_info;
        semaphore_info.flags = 0;

        if ((vr = VK_CALL(vkCreateSemaphore(chain->queue->device->vk_device, &semaphore_info,
                NULL, &chain->present.low_latency_sem))) < 0)
        {
            ERR("Failed to create semaphore, vr %d.\n", vr);
            return hresult_from_vk_result(vr);
        }

        pthread_mutex_init(&chain->present.low_latency_swapchain_lock, NULL);
        pthread_mutex_init(&chain->present.low_latency_state_update_lock, NULL);
    }

    return S_OK;
}

static HRESULT dxgi_vk_swap_chain_init_sleep_state(struct platform_sleep_state *sleep_state)
{
#if defined(_WIN32)
    uint64_t sleep_granularity_ns;
    ULONG min, max, cur;
    HMODULE ntdll;
    bool is_wine;

    is_wine = !!GetModuleHandleW(L"winevulkan.dll");

    if ((ntdll = GetModuleHandleW(L"ntdll.dll")))
    {
        sleep_state->NtQueryTimerResolution = (void*)GetProcAddress(ntdll, "NtQueryTimerResolution");
        sleep_state->NtSetTimerResolution = (void*)GetProcAddress(ntdll, "NtSetTimerResolution");
        sleep_state->NtDelayExecution = (void*)GetProcAddress(ntdll, "NtDelayExecution");
    }

    /* Older versions of Wine do not implement these functions, be robust here. */
    if (sleep_state->NtQueryTimerResolution && !sleep_state->NtQueryTimerResolution(&min, &max, &cur))
    {
        sleep_granularity_ns = 100 * cur;

        if (sleep_state->NtSetTimerResolution && !sleep_state->NtSetTimerResolution(max, TRUE, &cur))
            sleep_granularity_ns = 100 * max;

        INFO("Timer interval is %.1lf ms.\n", (double)sleep_granularity_ns / 1.0e6);
    }
    else
    {
        sleep_granularity_ns = 1000000;  /* 1ms */
    }

    /* This should always be available, however we can fall back to plain old Sleep() if not. */
    if (!sleep_state->NtDelayExecution)
        FIXME("NtDelayExecution not found in ntdll.\n");

    sleep_state->sleep_threshold_ns = (is_wine ? 1 : 4) * sleep_granularity_ns;
    return S_OK;
#else
    /* On native builds, we use nanosleep. Assume reasonable accuracy down to 1ms. */
    sleep_state->sleep_threshold_ns = 1000000;
    return S_OK;
#endif
}

static HRESULT dxgi_vk_swap_chain_init_frame_rate_limiter(struct dxgi_vk_swap_chain *chain)
{
    double target_frame_rate;
    char env[16];
    HRESULT hr;

    pthread_mutex_init(&chain->frame_rate_limit.lock, NULL);

    if (vkd3d_get_env_var("VKD3D_FRAME_RATE", env, sizeof(env)))
    {
        target_frame_rate = strtod(env, NULL);

        if (target_frame_rate > 0.0)
        {
            INFO("Set frame rate limit to %.1lf FPS via environment.\n", target_frame_rate);

            chain->frame_rate_limit.enable = true;
            chain->frame_rate_limit.has_user_override = true;
            chain->frame_rate_limit.target_interval_ns = (uint64_t)(1.0e9 / target_frame_rate);
        }
    }

    hr = dxgi_vk_swap_chain_init_sleep_state(&chain->frame_rate_limit.sleep_state);
    if (FAILED(hr))
        pthread_mutex_destroy(&chain->frame_rate_limit.lock);
    return hr;
}

static HRESULT dxgi_vk_swap_chain_init(struct dxgi_vk_swap_chain *chain, IDXGIVkSurfaceFactory *pFactory,
        const DXGI_SWAP_CHAIN_DESC1 *pDesc, struct d3d12_command_queue *queue)
{
    HRESULT hr;

    chain->IDXGIVkSwapChain_iface.lpVtbl = &dxgi_vk_swap_chain_vtbl;
    chain->refcount = 1;
    chain->internal_refcount = 1;
    chain->queue = queue;
    chain->desc = *pDesc;

    chain->swapchain_maintenance1 =
            queue->device->device_info.swapchain_maintenance1_features.swapchainMaintenance1 == VK_TRUE;

    INFO("Creating swapchain (%u x %u), BufferCount = %u.\n",
            pDesc->Width, pDesc->Height, pDesc->BufferCount);

    if (FAILED(hr = dxgi_vk_swap_chain_reallocate_user_buffers(chain)))
        goto cleanup_common;

    if (FAILED(hr = dxgi_vk_swap_chain_create_surface(chain, pFactory)))
        goto cleanup_common;

    if (FAILED(hr = dxgi_vk_swap_chain_init_sync_objects(chain)))
        goto cleanup_surface;

    if (FAILED(hr = dxgi_vk_swap_chain_init_waiter_thread(chain)))
        goto cleanup_sync_objects;

    if (FAILED(hr = dxgi_vk_swap_chain_init_low_latency(chain)))
        goto cleanup_waiter_thread;

    if (FAILED(hr = dxgi_vk_swap_chain_init_frame_rate_limiter(chain)))
        goto cleanup_low_latency;

    ID3D12CommandQueue_AddRef(&queue->ID3D12CommandQueue_iface);
    return S_OK;

cleanup_low_latency:
    dxgi_vk_swap_chain_cleanup_low_latency(chain);
cleanup_waiter_thread:
    dxgi_vk_swap_chain_cleanup_waiter_thread(chain);
cleanup_sync_objects:
    dxgi_vk_swap_chain_cleanup_sync_objects(chain);
cleanup_surface:
    dxgi_vk_swap_chain_cleanup_surface(chain);
cleanup_common:
    dxgi_vk_swap_chain_cleanup_common(chain);
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

    if (chain->queue->device->vk_info.NV_low_latency2)
        d3d12_device_register_swapchain(chain->queue->device, chain);

    *ppSwapchain = (IDXGIVkSwapChain*)&chain->IDXGIVkSwapChain_iface;
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

bool dxgi_vk_swap_chain_low_latency_enabled(struct dxgi_vk_swap_chain *chain)
{
    return chain->present.low_latency_state.mode;
}

void dxgi_vk_swap_chain_latency_sleep(struct dxgi_vk_swap_chain *chain)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    struct vkd3d_queue_timeline_trace_cookie cookie;
    VkLatencySleepInfoNV latency_sleep_info;
    VkSemaphoreWaitInfo sem_wait_info;
    bool should_sleep = false;

    /* Increment the low latency sem value before the wait */
    chain->present.low_latency_sem_value++;

    memset(&latency_sleep_info, 0, sizeof(latency_sleep_info));
    latency_sleep_info.sType = VK_STRUCTURE_TYPE_LATENCY_SLEEP_INFO_NV;
    latency_sleep_info.pNext = NULL;
    latency_sleep_info.signalSemaphore = chain->present.low_latency_sem;
    latency_sleep_info.value = chain->present.low_latency_sem_value;

    pthread_mutex_lock(&chain->present.low_latency_swapchain_lock);

    if (chain->present.vk_swapchain)
    {
        should_sleep = true;
        VK_CALL(vkLatencySleepNV(chain->queue->device->vk_device, chain->present.vk_swapchain, &latency_sleep_info));
    }

    pthread_mutex_unlock(&chain->present.low_latency_swapchain_lock);

    if (should_sleep)
    {
        memset(&sem_wait_info, 0, sizeof(sem_wait_info));
        sem_wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        sem_wait_info.pNext = NULL;
        sem_wait_info.flags = 0;
        sem_wait_info.semaphoreCount = 1;
        sem_wait_info.pSemaphores = &chain->present.low_latency_sem;
        sem_wait_info.pValues = &chain->present.low_latency_sem_value;

        cookie = vkd3d_queue_timeline_trace_register_low_latency_sleep(
                &chain->queue->device->queue_timeline_trace, chain->present.low_latency_sem_value);
        VK_CALL(vkWaitSemaphores(chain->queue->device->vk_device, &sem_wait_info, UINT64_MAX));
        vkd3d_queue_timeline_trace_complete_low_latency_sleep(
                &chain->queue->device->queue_timeline_trace, cookie);
    }
}

void dxgi_vk_swap_chain_set_latency_sleep_mode(struct dxgi_vk_swap_chain *chain, bool low_latency_mode,
	bool low_latency_boost, uint32_t minimum_interval_us)
{
    pthread_mutex_lock(&chain->present.low_latency_state_update_lock);

    chain->requested_low_latency_state.mode = low_latency_mode;
    chain->requested_low_latency_state.boost = low_latency_boost;
    chain->requested_low_latency_state.minimum_interval_us = minimum_interval_us;

    /* The actual call to vkSetLatencySleepModeNV will happen
     * when the application calls Present and the requested low
     * latency state is passed to the present task. */
    chain->low_latency_update_requested = true;

    pthread_mutex_unlock(&chain->present.low_latency_state_update_lock);
}

void dxgi_vk_swap_chain_set_latency_marker(struct dxgi_vk_swap_chain *chain, uint64_t frameID, VkLatencyMarkerNV marker)
{
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    VkSetLatencyMarkerInfoNV latency_marker_info;

    memset(&latency_marker_info, 0, sizeof(latency_marker_info));
    latency_marker_info.sType = VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV;
    latency_marker_info.pNext = NULL;
    latency_marker_info.presentID = frameID;
    latency_marker_info.marker = marker;

    if (chain->debug_latency && marker == VK_LATENCY_MARKER_PRESENT_START_NV)
        INFO("Setting present frame marker %"PRIu64".\n", frameID);

    pthread_mutex_lock(&chain->present.low_latency_swapchain_lock);

    if (chain->present.vk_swapchain)
        VK_CALL(vkSetLatencyMarkerNV(chain->queue->device->vk_device, chain->present.vk_swapchain, &latency_marker_info));

    pthread_mutex_unlock(&chain->present.low_latency_swapchain_lock);
}

void dxgi_vk_swap_chain_get_latency_info(struct dxgi_vk_swap_chain *chain, D3D12_LATENCY_RESULTS *latency_results)
{
    VkLatencyTimingsFrameReportNV frame_reports[ARRAY_SIZE(latency_results->frame_reports)];
    const struct vkd3d_vk_device_procs *vk_procs = &chain->queue->device->vk_procs;
    const VkLatencyTimingsFrameReportNV *last_effective_report = NULL;
    VkGetLatencyMarkerInfoNV marker_info;
    uint32_t i;

    /* There is no natural count, return blank output for missing output. */
    memset(latency_results->frame_reports, 0, sizeof(latency_results->frame_reports));

    pthread_mutex_lock(&chain->present.low_latency_swapchain_lock);

    if (chain->present.vk_swapchain)
    {
        memset(&marker_info, 0, sizeof(marker_info));
        marker_info.sType = VK_STRUCTURE_TYPE_GET_LATENCY_MARKER_INFO_NV;

        VK_CALL(vkGetLatencyTimingsNV(chain->queue->device->vk_device, chain->present.vk_swapchain, &marker_info));

        /* Apparently we have to report all 64 entries, or nothing. */
        if (marker_info.timingCount >= ARRAY_SIZE(frame_reports))
        {
            memset(frame_reports, 0, sizeof(frame_reports));
            marker_info.timingCount = min(marker_info.timingCount, ARRAY_SIZE(frame_reports));
            for (i = 0; i < marker_info.timingCount; i++)
                frame_reports[i].sType = VK_STRUCTURE_TYPE_LATENCY_TIMINGS_FRAME_REPORT_NV;
            marker_info.pTimings = frame_reports;

            VK_CALL(vkGetLatencyTimingsNV(chain->queue->device->vk_device, chain->present.vk_swapchain, &marker_info));

            for (i = 0; i < marker_info.timingCount; i++)
            {
                D3D12_FRAME_REPORT *report;

                /* If the frame ID isn't a natural aligned value,
                 * we assume it's a fake frame that the application never submitted a marker for.
                 * Ignore it. */
                if (frame_reports[i].presentID % VKD3D_LOW_LATENCY_FRAME_ID_STRIDE != 0 || frame_reports[i].presentID == 0)
                {
                    /* We either have to report all frames, or nothing. */
                    memset(latency_results->frame_reports, 0, sizeof(latency_results->frame_reports));
                    goto unlock_out;
                }

                report = &latency_results->frame_reports[i];

                report->frameID = frame_reports[i].presentID / VKD3D_LOW_LATENCY_FRAME_ID_STRIDE;
                report->inputSampleTime = frame_reports[i].inputSampleTimeUs;
                report->simStartTime = frame_reports[i].simStartTimeUs;
                report->simEndTime = frame_reports[i].simEndTimeUs;
                report->renderSubmitStartTime = frame_reports[i].renderSubmitStartTimeUs;
                report->renderSubmitEndTime = frame_reports[i].renderSubmitEndTimeUs;
                report->presentStartTime = frame_reports[i].presentStartTimeUs;
                report->presentEndTime = frame_reports[i].presentEndTimeUs;
                report->driverStartTime = frame_reports[i].driverStartTimeUs;
                report->driverEndTime = frame_reports[i].driverEndTimeUs;
                report->osRenderQueueStartTime = frame_reports[i].osRenderQueueStartTimeUs;
                report->osRenderQueueEndTime = frame_reports[i].osRenderQueueEndTimeUs;
                report->gpuRenderStartTime = frame_reports[i].gpuRenderStartTimeUs;
                report->gpuRenderEndTime = frame_reports[i].gpuRenderEndTimeUs;
                report->gpuActiveRenderTimeUs = frame_reports[i].gpuRenderEndTimeUs - frame_reports[i].gpuRenderStartTimeUs;

                if (last_effective_report)
                    report->gpuFrameTimeUs = frame_reports[i].gpuRenderEndTimeUs - last_effective_report->gpuRenderEndTimeUs;
                else
                    report->gpuFrameTimeUs = 0;

                last_effective_report = &frame_reports[i];
            }
        }
    }

unlock_out:
    pthread_mutex_unlock(&chain->present.low_latency_swapchain_lock);
}

ULONG dxgi_vk_swap_chain_incref(struct dxgi_vk_swap_chain *chain)
{
    ULONG refcount = InterlockedIncrement(&chain->internal_refcount);

    TRACE("%p increasing refcount to %u.\n", chain, refcount);

    return refcount;
}

ULONG dxgi_vk_swap_chain_decref(struct dxgi_vk_swap_chain *chain)
{
    ULONG refcount = InterlockedDecrement(&chain->internal_refcount);

    TRACE("%p decreasing refcount to %u.\n", chain, refcount);

    if (!refcount)
    {
        dxgi_vk_swap_chain_cleanup(chain);
        vkd3d_free(chain);
    }

    return refcount;
}

HRESULT dxgi_vk_swap_chain_factory_init(struct d3d12_command_queue *queue, struct dxgi_vk_swap_chain_factory *chain)
{
    chain->IDXGIVkSwapChainFactory_iface.lpVtbl = &dxgi_vk_swap_chain_factory_vtbl;
    chain->queue = queue;
    return S_OK;
}
