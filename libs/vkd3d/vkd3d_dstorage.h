/*
 * vkd3d_dstorage.h — vkd3d-proton DirectStorage integration API
 *
 * This header defines the interface between dstoragecore.dll and
 * vkd3d-proton (d3d12.dll). It exposes:
 *
 *   1. VkBuffer extraction from D3D12 resources:
 *      DirectStorage reads into GPU buffers, so we need to get the
 *      underlying VkBuffer handle from the game's ID3D12Resource.
 *
 *   2. VkDevice/VkQueue access:
 *      For dispatching GDeflate compute shaders and copying data,
 *      we need the Vulkan device handle and a compute-capable queue.
 *
 *   3. Fence signaling:
 *      When I/O completes, we signal the game's ID3D12Fence by
 *      writing to the underlying Vulkan timeline semaphore.
 *
 *   4. Buffer export for dma-buf:
 *      For zero-copy I/O with io_uring, we need to export Vulkan
 *      buffers as dma-buf file descriptors.
 *
 * vkd3d-proton would implement these functions and export them
 * so that dstoragecore.dll can call them via GetProcAddress.
 *
 * Reference:
 *   vkd3d-proton: https://github.com/HansKristian-Work/vkd3d-proton
 *   d3d12_resource: vkd3d-proton/libs/vkd3d/vkd3d_private.h
 *   d3d12_fence: vkd3d-proton/libs/vkd3d/queue_timeline.c
 */

#ifndef VKD3D_DSTORAGE_H
#define VKD3D_DSTORAGE_H

#include <windows.h>
#include <d3d12.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * Version check
 *
 * DirectStorage integration requires vkd3d-proton 2.13+ or newer.
 * The version is encoded as a single DWORD: (major << 16) | minor.
 * ------------------------------------------------------------------ */
#define VKD3D_DSTORAGE_VERSION 0x0002000D  /* 2.13 */

/*
 * Check if vkd3d-proton supports the DirectStorage integration API.
 * Returns the version number, or 0 if not supported.
 *
 * Implementation in vkd3d-proton:
 *   Propose addition to vkd3d_proton_private.h and export via .def file.
 */
DWORD WINAPI vkd3d_dstorage_get_version(void);

/* ------------------------------------------------------------------
 * Buffer Extraction (Item 3: Buffer Sharing)
 *
 * DirectStorage needs to write decompressed data directly into
 * the game's D3D12 buffers. This requires extracting the underlying
 * VkBuffer handle and GPU virtual address.
 *
 * Usage pattern:
 *   1. Game calls IDStorageQueue::EnqueueRequest with a BUFFER destination
 *   2. dstoragecore.dll extracts VkBuffer from ID3D12Resource
 *   3. After I/O + decompression, data is in the VkBuffer
 *   4. vkd3d-proton uses the buffer in subsequent draw/dispatch calls
 *
 * The vkd3d_proton resource structure (d3d12_resource) stores:
 *   struct vkd3d_unique_resource {
 *       union { VkBuffer vk_buffer; VkImage vk_image; };
 *       VkDeviceAddress va;         // GPU virtual address
 *       VkDeviceSize size;
 *   };
 *
 * This function returns the VkBuffer from that union.
 * ------------------------------------------------------------------ */

/*
 * Extract the VkBuffer handle from a D3D12 resource.
 *
 * @param resource   The D3D12 resource (must be a BUFFER, not TEXTURE)
 * @param vk_buffer  [out] Receives the VkBuffer handle
 * @param va         [out] Receives the GPU virtual address (optional, may be NULL)
 * @param size       [out] Receives the buffer size in bytes (optional, may be NULL)
 *
 * @return S_OK on success, E_INVALIDARG if resource is not a buffer,
 *         E_NOTIMPL if vkd3d-proton doesn't expose this function.
 *
 * Implementation notes:
 *   In vkd3d-proton, d3d12_resource is defined in vkd3d_private.h.
 *   The VkBuffer is stored in resource->res.vk_buffer.
 *   This function would be added to vkd3d_proton_private.h and
 *   implemented in resource.c.
 *
 *   For textures, DirectStorage uses DSTORAGE_REQUEST_DESTINATION_TEXTURE_REGION
 *   which requires a different path (copy via vkCmdCopyBufferToImage).
 *   This function handles only BUFFER destinations.
 */
HRESULT WINAPI vkd3d_dstorage_get_vk_buffer(
    ID3D12Resource *resource,
    VkBuffer *vk_buffer,
    VkDeviceAddress *va,
    VkDeviceSize *size);

/* ------------------------------------------------------------------
 * Device/Queue Access
 *
 * DirectStorage needs Vulkan device handles for:
 *   - Creating staging buffers for I/O
 *   - Dispatching GDeflate compute shaders
 *   - Copying data between staging and destination buffers
 *   - Creating/using timeline semaphores for fence signaling
 *
 * The queue is used for submitting compute operations. The returned
 * queue must support VK_QUEUE_COMPUTE_BIT.
 * ------------------------------------------------------------------ */

/*
 * Get the Vulkan device handle from a D3D12 device.
 *
 * @param device   The D3D12 device
 * @param vk_device [out] Receives the VkDevice handle
 *
 * @return S_OK on success.
 *
 * Implementation:
 *   vkd3d-proton's d3d12_device struct has a 'vk_device' field.
 *   This is the VkDevice passed to vkCreateDevice.
 */
HRESULT WINAPI vkd3d_dstorage_get_vk_device(
    ID3D12Device *device,
    VkDevice *vk_device);

/*
 * Get a compute-capable VkQueue from a D3D12 device.
 *
 * @param device        The D3D12 device
 * @param vk_queue      [out] Receives the VkQueue handle
 * @param family_index  [out] Receives the queue family index (may be NULL)
 *
 * @return S_OK on success, E_FAIL if no compute queue is available.
 *
 * Implementation:
 *   vkd3d-proton has multiple queue family types:
 *     VKD3D_QUEUE_FAMILY_COMPUTE = 0      (primary, may have GRAPHICS)
 *     VKD3D_QUEUE_FAMILY_INTERNAL_COMPUTE  (internal memory transfers)
 *     VKD3D_QUEUE_FAMILY_ASYNC_COMPUTE     (async compute)
 *     VKD3D_QUEUE_FAMILY_COPY              (dedicated copy queue)
 *
 *   We prefer VKD3D_QUEUE_FAMILY_ASYNC_COMPUTE if available, then
 *   the primary queue (which also supports compute).
 */
HRESULT WINAPI vkd3d_dstorage_get_compute_queue(
    ID3D12Device *device,
    VkQueue *vk_queue,
    uint32_t *family_index);

/*
 * Buffer export for dma-buf (zero-copy I/O).
 *
 * Exports a Vulkan buffer's memory as a Linux dma-buf file descriptor.
 * The fd can then be used with io_uring for direct I/O from NVMe to GPU memory.
 *
 * @param vk_device     The Vulkan device
 * @param vk_buffer     The Vulkan buffer to export
 * @param fd            [out] Receives the dma-buf file descriptor
 *
 * @return S_OK on success.
 *
 * Required Vulkan extensions:
 *   VK_KHR_external_memory_fd
 *   VK_EXT_external_memory_dma_buf
 *
 * The buffer must have been created with:
 *   VkExportMemoryAllocateInfo {
 *       handleTypes: VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
 *   }
 *
 * For staging buffers used by DirectStorage, we create them with
 * export handles so io_uring can read directly into them.
 */
HRESULT WINAPI vkd3d_dstorage_export_dma_buf(
    VkDevice vk_device,
    VkBuffer vk_buffer,
    int *fd);

/* ------------------------------------------------------------------
 * Fence Signaling (Item 4: Fence Integration)
 *
 * DirectStorage queues signal ID3D12Fence objects when I/O completes.
 * vkd3d-proton implements D3D12 fences as Vulkan timeline semaphores.
 *
 * The d3d12_fence struct (vkd3d-proton queue_timeline.c):
 *   struct d3d12_fence {
 *       VkSemaphore timeline_semaphore;   // The Vulkan semaphore
 *       uint64_t virtual_value;
 *       ...
 *   };
 *
 * When an I/O operation completes, we need to signal this semaphore
 * to unblock the game's waiting command queue.
 *
 * There are two approaches:
 *
 *   A. Direct vkSignalSemaphore call:
 *      Pro: Simple, low latency
 *      Con: Must be called from a thread with an active VkQueue
 *           submission (or we need the timeline semaphore's fd)
 *
 *   B. Thread-safe signal via vkd3d-proton's fence worker:
 *      Pro: Thread-safe, no Vulkan queue needed
 *      Con: Requires vkd3d-proton to expose a signal function
 *
 * We use Approach A for latency-sensitive I/O completion.
 * The completion thread calls vkSignalSemaphore directly.
 * ------------------------------------------------------------------ */

/*
 * Signal a D3D12 fence from the I/O completion thread.
 *
 * @param device    The D3D12 device (needed to get VkDevice)
 * @param fence     The D3D12 fence to signal
 * @param value     The value to write to the fence
 *
 * @return S_OK on success.
 *
 * Implementation:
 *   Extracts the VkSemaphore from d3d12_fence->timeline_semaphore
 *   and calls vkSignalSemaphore with VK_SEMAPHORE_TYPE_TIMELINE.
 *
 *   Thread safety: The fence's timeline semaphore is thread-safe
 *   for signaling (Vulkan specifies this explicitly).
 */
HRESULT WINAPI vkd3d_dstorage_signal_fence(
    ID3D12Device *device,
    ID3D12Fence *fence,
    uint64_t value);

/* ------------------------------------------------------------------
 * Queue Submission for GPU Decompression (Item 6)
 *
 * After GDeflate compute shader dispatch, we need to submit the
 * command buffer to a Vulkan queue. This is done through vkd3d-proton
 * to ensure proper synchronization with the D3D12 pipeline.
 *
 * For VK_NV_memory_decompression, the flow is simpler:
 *   vkCmdDecompressMemoryNV(commandBuffer, regions)
 * This doesn't need a compute shader dispatch at all.
 * ------------------------------------------------------------------ */

/*
 * Submit a command buffer containing GDeflate decompression operations
 * to a compute queue and signal a fence on completion.
 *
 * @param device       The D3D12 device
 * @param queue_type   The queue type (ASYNC_COMPUTE or COMPUTE)
 * @param command_buffer The VkCommandBuffer to submit
 * @param fence        Fence to signal on completion
 * @param fence_value  Value to write to the fence
 *
 * @return S_OK on success.
 */
HRESULT WINAPI vkd3d_dstorage_submit_compute(
    ID3D12Device *device,
    VkCommandBuffer command_buffer,
    ID3D12Fence *fence,
    uint64_t fence_value);

#ifdef __cplusplus
}
#endif

#endif /* VKD3D_DSTORAGE_H */
