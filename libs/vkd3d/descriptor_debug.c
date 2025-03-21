/*
 * Copyright 2020 Hans-Kristian Arntzen for Valve Corporation
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

#include "vkd3d_descriptor_debug.h"
#include "vkd3d_threads.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static pthread_once_t debug_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t debug_lock = PTHREAD_MUTEX_INITIALIZER;
static bool descriptor_debug_active_descriptor_checks;
static bool descriptor_debug_active_instruction_checks;
static bool descriptor_debug_active_log;
static FILE *descriptor_debug_file;

struct vkd3d_descriptor_qa_global_info
{
    struct vkd3d_descriptor_qa_global_buffer_data *payload_data;

    VkDescriptorBufferInfo payload_descriptor;
    VkBuffer vk_payload_buffer;
    struct vkd3d_device_memory_allocation payload_device_allocation;

    uint32_t *control_data;
    VkBuffer vk_control_buffer;
    struct vkd3d_device_memory_allocation control_device_allocation;
    VkDescriptorBufferInfo control_descriptor;

    unsigned int num_cookies;

    pthread_t ring_thread;
    pthread_mutex_t ring_lock;
    pthread_cond_t ring_cond;
    bool active;

    struct vkd3d_shader_hash_range *qa_ranges;
    size_t qa_range_size;
    size_t qa_range_count;

    uint32_t sync_val_cookie_atomic;

    bool needs_sync_validation;
};

static const char *debug_descriptor_type(vkd3d_descriptor_qa_flags type_flags)
{
    bool has_raw_va = !!(type_flags & VKD3D_DESCRIPTOR_QA_TYPE_RAW_VA_BIT);

    switch (type_flags & ~VKD3D_DESCRIPTOR_QA_TYPE_RAW_VA_BIT)
    {
        case VKD3D_DESCRIPTOR_QA_TYPE_SAMPLER_BIT: return "SAMPLER";
        case VKD3D_DESCRIPTOR_QA_TYPE_SAMPLED_IMAGE_BIT: return "SAMPLED_IMAGE";
        case VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_IMAGE_BIT: return "STORAGE_IMAGE";
        case VKD3D_DESCRIPTOR_QA_TYPE_UNIFORM_BUFFER_BIT: return "UNIFORM_BUFFER";
        case VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_BUFFER_BIT: return "STORAGE_BUFFER";
        case VKD3D_DESCRIPTOR_QA_TYPE_UNIFORM_TEXEL_BUFFER_BIT: return "UNIFORM_TEXEL_BUFFER";
        case VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_TEXEL_BUFFER_BIT: return "STORAGE_TEXEL_BUFFER";

        case VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_TEXEL_BUFFER_BIT | VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_BUFFER_BIT:
            return has_raw_va ? "STORAGE_TEXEL_BUFFER / STORAGE_BUFFER (w/ counter)" : "STORAGE_TEXEL_BUFFER / STORAGE_BUFFER";

        case VKD3D_DESCRIPTOR_QA_TYPE_UNIFORM_TEXEL_BUFFER_BIT | VKD3D_DESCRIPTOR_QA_TYPE_STORAGE_BUFFER_BIT:
            return has_raw_va ? "UNIFORM_TEXEL_BUFFER / STORAGE_BUFFER (w/ counter)" : "UNIFORM_TEXEL_BUFFER / STORAGE_BUFFER";

        case VKD3D_DESCRIPTOR_QA_TYPE_RT_ACCELERATION_STRUCTURE_BIT:
            return "RTAS";

        case 0:
            return "NONE";

        default: return "?";
    }
}

static void vkd3d_descriptor_debug_init_once(void)
{
    char env[VKD3D_PATH_MAX];
    vkd3d_get_env_var("VKD3D_DESCRIPTOR_QA_LOG", env, sizeof(env));

    if (strlen(env) > 0)
    {
        INFO("Enabling VKD3D_DESCRIPTOR_QA_LOG\n");
        descriptor_debug_file = fopen(env, "w");
        if (!descriptor_debug_file)
            ERR("Failed to open file: %s.\n", env);
        else
            descriptor_debug_active_log = true;
    }

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_DESCRIPTOR_QA_CHECKS)
    {
        INFO("Enabling descriptor QA checks!\n");
        descriptor_debug_active_descriptor_checks = true;
    }
    else if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_INSTRUCTION_QA_CHECKS)
    {
        INFO("Enabling instruction-level QA checks!\n");
        descriptor_debug_active_instruction_checks = true;
    }
}

void vkd3d_descriptor_debug_init(void)
{
    pthread_once(&debug_once, vkd3d_descriptor_debug_init_once);
}

bool vkd3d_descriptor_debug_active_log(void)
{
    return descriptor_debug_active_log;
}

bool vkd3d_descriptor_debug_active_instruction_qa_checks(void)
{
    return descriptor_debug_active_instruction_checks;
}

bool vkd3d_descriptor_debug_active_descriptor_qa_checks(void)
{
    return descriptor_debug_active_descriptor_checks;
}

VkDeviceSize vkd3d_descriptor_debug_heap_info_size(unsigned int num_descriptors)
{
    return offsetof(struct vkd3d_descriptor_qa_heap_buffer_data, desc) + num_descriptors *
            sizeof(struct vkd3d_descriptor_qa_cookie_descriptor);
}

static void vkd3d_descriptor_debug_set_live_status_bit(
        struct vkd3d_descriptor_qa_global_info *global_info, uint64_t cookie)
{
    if (!global_info || !descriptor_debug_active_descriptor_checks ||
            !global_info->active || !global_info->payload_data)
        return;

    if (cookie < global_info->num_cookies)
    {
        vkd3d_atomic_uint32_or(&global_info->payload_data->live_status_table[cookie / 32],
                1u << (cookie & 31), vkd3d_memory_order_relaxed);
    }
    else
        INFO("Cookie index %"PRIu64" is out of range, cannot be tracked.\n", cookie);
}

static void vkd3d_descriptor_debug_unset_live_status_bit(
        struct vkd3d_descriptor_qa_global_info *global_info, uint64_t cookie)
{
    if (!global_info || !descriptor_debug_active_descriptor_checks ||
            !global_info->active || !global_info->payload_data)
        return;

    if (cookie < global_info->num_cookies)
    {
        vkd3d_atomic_uint32_and(&global_info->payload_data->live_status_table[cookie / 32],
                ~(1u << (cookie & 31)), vkd3d_memory_order_relaxed);
    }
}

void vkd3d_descriptor_debug_sync_validation_barrier(
        struct vkd3d_descriptor_qa_global_info *global_info,
        struct d3d12_device *device, VkCommandBuffer vk_cmd_buffer)
{
    const struct vkd3d_vk_device_procs *vk_procs;
    VkMemoryBarrier2 vk_barrier;
    VkDependencyInfo dep;

    if (!global_info || !global_info->needs_sync_validation)
        return;

    vk_procs = &device->vk_procs;

    memset(&dep, 0, sizeof(dep));
    memset(&vk_barrier, 0, sizeof(vk_barrier));
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &vk_barrier;

    /* Fully serialize dispatches. This makes it easier to catch sync bugs
     * since we're guaranteed that a racing read/write in subsequent dispatch will
     * observe hazards. */
    vk_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    vk_barrier.srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    vk_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    vk_barrier.dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    vk_barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

    VK_CALL(vkCmdPipelineBarrier2(vk_cmd_buffer, &dep));
}

uint32_t vkd3d_descriptor_debug_clear_bloom_filter(
        struct vkd3d_descriptor_qa_global_info *global_info,
        struct d3d12_device *device, VkCommandBuffer vk_cmd_buffer)
{
    const struct vkd3d_vk_device_procs *vk_procs;
    uint32_t control_size;
    uint32_t sync_cookie;

    if (!global_info || !global_info->needs_sync_validation)
        return 0;

    vk_procs = &device->vk_procs;

    /* Fully barrier everything. */
    vkd3d_descriptor_debug_sync_validation_barrier(global_info, device, vk_cmd_buffer);
    control_size = (global_info->num_cookies / 16) * sizeof(uint32_t);
    /* This can get rather expensive, but whatever it takes to catch impossible sync bugs ...
     * Don't clear the last two values of the control buffer which hold misc cookie data that we don't need to clear. */
    VK_CALL(vkCmdFillBuffer(vk_cmd_buffer,
            global_info->control_descriptor.buffer, control_size,
            global_info->control_descriptor.range - control_size - 2 * sizeof(uint32_t), 0));

    sync_cookie = vkd3d_atomic_uint32_increment(&global_info->sync_val_cookie_atomic, vkd3d_memory_order_relaxed);

    /* Update the dispatch counter. */
    VK_CALL(vkCmdFillBuffer(vk_cmd_buffer,
            global_info->control_descriptor.buffer, global_info->control_descriptor.range - 2 * sizeof(uint32_t),
            sizeof(uint32_t), sync_cookie));

    vkd3d_descriptor_debug_sync_validation_barrier(global_info, device, vk_cmd_buffer);
    return sync_cookie;
}

static void *vkd3d_descriptor_debug_qa_check_instruction(void *userdata)
{
    struct vkd3d_descriptor_qa_global_info *global_info = userdata;
    const struct vkd3d_instruction_qa_payload_data *payload_data;
    uint32_t control_words = global_info->num_cookies / 16;
    bool active = true;
    uint32_t i;

    payload_data = (const struct vkd3d_instruction_qa_payload_data *)global_info->payload_data;

    while (active)
    {
        /* Don't spin endlessly, this thread is kicked after a successful fence wait. */
        pthread_mutex_lock(&global_info->ring_lock);
        if (global_info->active)
            pthread_cond_wait(&global_info->ring_cond, &global_info->ring_lock);
        active = global_info->active;
        pthread_mutex_unlock(&global_info->ring_lock);

        for (i = 0; i < control_words; i++)
        {
            uint32_t word, payload_index;
            word = vkd3d_atomic_uint32_load_explicit(&global_info->control_data[i], vkd3d_memory_order_acquire);
            /* The upper 16 bits mark that there is valid data. */
            word &= 0xffff0000u;

            if (word)
            {
                while (word)
                {
                    payload_index = i * 16 + (vkd3d_bitmask_iter32(&word) - 16);
                    ERR("QA: non-normal value || shader %016"PRIx64", inst %u, value #%x / %u.\n",
                            payload_data[payload_index].hash,
                            payload_data[payload_index].instruction,
                            payload_data[payload_index].value,
                            payload_data[payload_index].value);
                }

                vkd3d_atomic_uint32_store_explicit(&global_info->control_data[i], 0, vkd3d_memory_order_release);
            }
        }
    }

    return NULL;
}

static void vkd3d_descriptor_debug_qa_check_report_fault(
        struct vkd3d_descriptor_qa_global_info *global_info);

static void *vkd3d_descriptor_debug_qa_check_entry(void *userdata)
{
    struct vkd3d_descriptor_qa_global_info *global_info = userdata;
    bool active = true;

    while (active)
    {
        /* Don't spin endlessly, this thread is kicked after a successful fence wait. */
        pthread_mutex_lock(&global_info->ring_lock);
        if (global_info->active)
            pthread_cond_wait(&global_info->ring_cond, &global_info->ring_lock);
        active = global_info->active;
        pthread_mutex_unlock(&global_info->ring_lock);

        if (global_info->payload_data->fault_type != 0)
        {
            vkd3d_descriptor_debug_qa_check_report_fault(global_info);
            ERR("Num failed checks: %u\n", global_info->payload_data->fault_atomic);

            /* Reset the latch so we can get more reports. */
            vkd3d_atomic_uint32_store_explicit(&global_info->payload_data->fault_type, 0, vkd3d_memory_order_relaxed);
            vkd3d_atomic_uint32_store_explicit(&global_info->payload_data->fault_atomic, 0, vkd3d_memory_order_release);
        }
    }

    return NULL;
}

void vkd3d_descriptor_debug_kick_qa_check(struct vkd3d_descriptor_qa_global_info *global_info)
{
    if (global_info && global_info->active)
        pthread_cond_signal(&global_info->ring_cond);
}

const VkDescriptorBufferInfo *vkd3d_descriptor_debug_get_payload_info_descriptor(
        struct vkd3d_descriptor_qa_global_info *global_info)
{
    if (global_info)
        return &global_info->payload_descriptor;
    else
        return NULL;
}

const VkDescriptorBufferInfo *vkd3d_descriptor_debug_get_control_info_descriptor(
        struct vkd3d_descriptor_qa_global_info *global_info)
{
    if (global_info)
        return &global_info->control_descriptor;
    else
        return NULL;
}

static void vkd3d_descriptor_debug_parse_shader_ranges(
        struct vkd3d_descriptor_qa_global_info *global_info,
        enum vkd3d_shader_hash_range_kind kind)
{
    char env[VKD3D_PATH_MAX];
    FILE *file;

    if (vkd3d_get_env_var("VKD3D_QA_HASHES", env, sizeof(env)))
    {
        file = fopen(env, "r");
        if (file)
        {
            vkd3d_shader_hash_range_parse(file, &global_info->qa_ranges, &global_info->qa_range_size,
                    &global_info->qa_range_count, kind);
            fclose(file);
        }
    }
}

static HRESULT vkd3d_descriptor_debug_alloc_global_info_instructions(
        struct vkd3d_descriptor_qa_global_info **out_global_info,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_descriptor_qa_global_info *global_info;
    VkMemoryPropertyFlags memory_properties;
    const uint32_t num_payloads = 4096;
    D3D12_RESOURCE_DESC1 buffer_desc;
    D3D12_HEAP_PROPERTIES heap_info;
    uint32_t bloom_buffer_size = 0;
    D3D12_HEAP_FLAGS heap_flags;
    bool needs_sync_val;
    unsigned int i;
    VkResult vr;
    HRESULT hr;

    global_info = vkd3d_calloc(1, sizeof(*global_info));
    if (!global_info)
        return E_OUTOFMEMORY;

    memset(&buffer_desc, 0, sizeof(buffer_desc));
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = sizeof(struct vkd3d_instruction_qa_payload_data) * num_payloads;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    /* host-visible device memory */
    memset(&heap_info, 0, sizeof(heap_info));
    heap_info.Type = D3D12_HEAP_TYPE_UPLOAD;
    heap_flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

    /* Create the payload buffer. */
    if (FAILED(hr = vkd3d_create_buffer(device, &heap_info, heap_flags, &buffer_desc,
            "qa-payload", &global_info->vk_payload_buffer)))
    {
        vkd3d_descriptor_debug_free_global_info(global_info, device);
        return hr;
    }

    vkd3d_descriptor_debug_parse_shader_ranges(global_info, VKD3D_SHADER_HASH_RANGE_KIND_QA);

    /* If we need sync validation, we need to add an augmented buffer that lives after the fault feedback atomics.
     * The shader is responsible for figuring this out based on a POT + POT addressing scheme.
     * E.g. if the feedback atomics have size 0x1000 and the bloom buffer has size 0x1000000,
     * we expect a buffer size of 0x1001000. Using findLSB and findMSB we can partition as appropriate.
     * We also add one 64-bit entry to serve as the entry for getting a "unique" ID per invocation,
     * so the effective size is 0x1001001 in this case. */
    needs_sync_val = false;
    for (i = 0; i < global_info->qa_range_count && !needs_sync_val; i++)
    {
        if (global_info->qa_ranges[i].flags &
                (VKD3D_SHADER_HASH_RANGE_QA_FLAG_SYNC | VKD3D_SHADER_HASH_RANGE_QA_FLAG_SYNC_COMPUTE))
        {
            bloom_buffer_size = 1u << vkd3d_env_var_as_uint("VKD3D_BLOOM_BUFFER_SIZE_LOG2", 24);
            INFO("Using bloom buffer size of %u bytes.\n", bloom_buffer_size);
            needs_sync_val = true;
        }
    }

    memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
#ifdef VKD3D_ENABLE_BREADCRUMBS
    memory_properties = vkd3d_debug_buffer_memory_properties(device, memory_properties, needs_sync_val);
#endif

    if (FAILED(hr = vkd3d_allocate_internal_buffer_memory(device, global_info->vk_payload_buffer,
            memory_properties, &global_info->payload_device_allocation)))
    {
        vkd3d_descriptor_debug_free_global_info(global_info, device);
        return hr;
    }

    if ((vr = VK_CALL(vkMapMemory(device->vk_device, global_info->payload_device_allocation.vk_memory,
            0, VK_WHOLE_SIZE, 0, (void**)&global_info->payload_data))))
    {
        ERR("Failed to map buffer, vr %d.\n", vr);
        vkd3d_descriptor_debug_free_global_info(global_info, device);
        return hresult_from_vk_result(vr);
    }

    memset(global_info->payload_data, 0, buffer_desc.Width);

    global_info->payload_descriptor.buffer = global_info->vk_payload_buffer;
    global_info->payload_descriptor.offset = 0;
    global_info->payload_descriptor.range = buffer_desc.Width;

    /* Create the atomic buffer. Keep it device local to keep GPU performance high.
     * We'll eat the insanely slow readback cost on CPU. */
    heap_info.Type = D3D12_HEAP_TYPE_DEFAULT;
    buffer_desc.Width = (num_payloads / 16) * sizeof(uint32_t);

    if (needs_sync_val)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_SINGLE_QUEUE)
        {
            buffer_desc.Width += bloom_buffer_size;
            global_info->needs_sync_validation = true;
        }
        else
        {
            ERR("Sync validation is enabled, but SINGLE_QUEUE is not. Cannot use sync validation properly!\n");
        }
    }

    /* The last two u32s of the control buffer are reserved for:
     * - A unique dispatch counter used to correlate with breadcrumbs
     * - An atomic counter for InvocationID locking in sync-val. */
    buffer_desc.Width += 2 * sizeof(uint32_t);

    if (FAILED(hr = vkd3d_create_buffer(device, &heap_info, heap_flags, &buffer_desc,
            "qa-control", &global_info->vk_control_buffer)))
    {
        vkd3d_descriptor_debug_free_global_info(global_info, device);
        return hr;
    }

    memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
#ifdef VKD3D_ENABLE_BREADCRUMBS
    memory_properties = vkd3d_debug_buffer_memory_properties(device, memory_properties, needs_sync_val);
#endif

    if (FAILED(hr = vkd3d_allocate_internal_buffer_memory(device, global_info->vk_control_buffer,
            memory_properties, &global_info->control_device_allocation)))
    {
        vkd3d_descriptor_debug_free_global_info(global_info, device);
        return hr;
    }

    if ((vr = VK_CALL(vkMapMemory(device->vk_device, global_info->control_device_allocation.vk_memory,
            0, VK_WHOLE_SIZE, 0, (void**)&global_info->control_data))))
    {
        ERR("Failed to map buffer, vr %d.\n", vr);
        vkd3d_descriptor_debug_free_global_info(global_info, device);
        return hresult_from_vk_result(vr);
    }

    memset(global_info->control_data, 0, buffer_desc.Width);

    global_info->control_descriptor.buffer = global_info->vk_control_buffer;
    global_info->control_descriptor.offset = 0;
    global_info->control_descriptor.range = buffer_desc.Width;

    global_info->num_cookies = num_payloads;

    pthread_mutex_init(&global_info->ring_lock, NULL);
    pthread_cond_init(&global_info->ring_cond, NULL);
    global_info->active = true;
    if (pthread_create(&global_info->ring_thread, NULL, vkd3d_descriptor_debug_qa_check_instruction, global_info) != 0)
    {
        vkd3d_descriptor_debug_free_global_info(global_info, device);
        return E_OUTOFMEMORY;
    }

    *out_global_info = global_info;
    return S_OK;
}

uint32_t vkd3d_descriptor_debug_get_shader_interface_flags(
        struct vkd3d_descriptor_qa_global_info *global_info,
        const void *code, size_t size)
{
    struct vkd3d_shader_code dxbc;
    vkd3d_shader_hash_t hash;
    uint32_t flags = 0;
    size_t i;

    if (!global_info || !global_info->active)
        return 0;

    dxbc.code = code;
    dxbc.size = size;
    hash = vkd3d_shader_hash(&dxbc);

    if (global_info->qa_range_count)
    {
        for (i = 0; i < global_info->qa_range_count; i++)
            if (global_info->qa_ranges[i].lo <= hash && hash <= global_info->qa_ranges[i].hi)
                flags |= global_info->qa_ranges[i].flags;
    }
    else
    {
        flags = VKD3D_SHADER_HASH_RANGE_QA_FLAG_ALLOW;
    }

    if (flags & VKD3D_SHADER_HASH_RANGE_QA_FLAG_FULL_QA)
        flags |= VKD3D_SHADER_HASH_RANGE_QA_FLAG_ALLOW;
    if (flags & VKD3D_SHADER_HASH_RANGE_QA_FLAG_DISALLOW)
        flags = 0;

    if (descriptor_debug_active_descriptor_checks)
    {
        if (flags & VKD3D_SHADER_HASH_RANGE_QA_FLAG_ALLOW)
            flags = VKD3D_SHADER_INTERFACE_DESCRIPTOR_QA_BUFFER;
    }
    else if (descriptor_debug_active_instruction_checks)
    {
        if (flags & VKD3D_SHADER_HASH_RANGE_QA_FLAG_FULL_QA)
        {
            flags = VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER_FULL |
                    VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER;
        }
        else if (flags & VKD3D_SHADER_HASH_RANGE_QA_FLAG_FLUSH_NAN)
        {
            flags = VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER_FLUSH_NAN |
                    VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER;
        }
        else if (flags & VKD3D_SHADER_HASH_RANGE_QA_FLAG_EXPECT_ASSUME)
        {
            flags = VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER_EXPECT_ASSUME |
                    VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER;
        }
        else if (flags & VKD3D_SHADER_HASH_RANGE_QA_FLAG_SYNC)
        {
            flags = VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER_SYNC |
                    VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER;
        }
        else if (flags & VKD3D_SHADER_HASH_RANGE_QA_FLAG_SYNC_COMPUTE)
        {
            flags = VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER_SYNC_COMPUTE |
                    VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER;
        }
        else if (flags & VKD3D_SHADER_HASH_RANGE_QA_FLAG_ALLOW)
            flags = VKD3D_SHADER_INTERFACE_INSTRUCTION_QA_BUFFER;
    }
    else
    {
        flags = 0;
    }

    return flags;
}

static HRESULT vkd3d_descriptor_debug_alloc_global_info_descriptors(
        struct vkd3d_descriptor_qa_global_info **out_global_info, unsigned int num_cookies,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;
    struct vkd3d_descriptor_qa_global_info *global_info;
    VkMemoryPropertyFlags memory_properties;
    D3D12_RESOURCE_DESC1 buffer_desc;
    D3D12_HEAP_PROPERTIES heap_info;
    D3D12_HEAP_FLAGS heap_flags;
    VkResult vr;
    HRESULT hr;

    global_info = vkd3d_calloc(1, sizeof(*global_info));
    if (!global_info)
        return E_OUTOFMEMORY;

    memset(&buffer_desc, 0, sizeof(buffer_desc));
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = sizeof(uint32_t) * ((num_cookies + 31) / 32) +
            offsetof(struct vkd3d_descriptor_qa_global_buffer_data, live_status_table);
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buffer_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    /* host-visible device memory */
    memset(&heap_info, 0, sizeof(heap_info));
    heap_info.Type = D3D12_HEAP_TYPE_UPLOAD;

    heap_flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;

    if (FAILED(hr = vkd3d_create_buffer(device, &heap_info, heap_flags, &buffer_desc,
            "qa-buffer", &global_info->vk_payload_buffer)))
    {
        vkd3d_descriptor_debug_free_global_info(global_info, device);
        return hr;
    }

    memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
#ifdef VKD3D_ENABLE_BREADCRUMBS
    memory_properties = vkd3d_debug_buffer_memory_properties(device, memory_properties, false);
#endif

    if (FAILED(hr = vkd3d_allocate_internal_buffer_memory(device, global_info->vk_payload_buffer,
            memory_properties,
            &global_info->payload_device_allocation)))
    {
        vkd3d_descriptor_debug_free_global_info(global_info, device);
        return hr;
    }

    if ((vr = VK_CALL(vkMapMemory(device->vk_device, global_info->payload_device_allocation.vk_memory,
            0, VK_WHOLE_SIZE, 0, (void**)&global_info->payload_data))))
    {
        ERR("Failed to map buffer, vr %d.\n", vr);
        vkd3d_descriptor_debug_free_global_info(global_info, device);
        return hresult_from_vk_result(vr);
    }

    memset(global_info->payload_data, 0, buffer_desc.Width);

    /* The NULL descriptor has cookie 0, and is always considered live. */
    global_info->payload_data->live_status_table[0] = 1u << 0;

    global_info->payload_descriptor.buffer = global_info->vk_payload_buffer;
    global_info->payload_descriptor.offset = 0;
    global_info->payload_descriptor.range = buffer_desc.Width;
    global_info->num_cookies = num_cookies;

    pthread_mutex_init(&global_info->ring_lock, NULL);
    pthread_cond_init(&global_info->ring_cond, NULL);
    global_info->active = true;
    if (pthread_create(&global_info->ring_thread, NULL, vkd3d_descriptor_debug_qa_check_entry, global_info) != 0)
    {
        vkd3d_descriptor_debug_free_global_info(global_info, device);
        return E_OUTOFMEMORY;
    }

    vkd3d_descriptor_debug_parse_shader_ranges(global_info, VKD3D_SHADER_HASH_RANGE_KIND_QA);

    *out_global_info = global_info;
    return S_OK;
}

HRESULT vkd3d_descriptor_debug_alloc_global_info(
        struct vkd3d_descriptor_qa_global_info **out_global_info, unsigned int num_cookies,
        struct d3d12_device *device)
{
    if (descriptor_debug_active_descriptor_checks)
        return vkd3d_descriptor_debug_alloc_global_info_descriptors(out_global_info, num_cookies, device);
    else if (descriptor_debug_active_instruction_checks)
        return vkd3d_descriptor_debug_alloc_global_info_instructions(out_global_info, device);
    else
        return E_INVALIDARG;
}

void vkd3d_descriptor_debug_free_global_info(
        struct vkd3d_descriptor_qa_global_info *global_info,
        struct d3d12_device *device)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    if (!global_info)
        return;

    if (global_info->active)
    {
        pthread_mutex_lock(&global_info->ring_lock);
        global_info->active = false;
        pthread_cond_signal(&global_info->ring_cond);
        pthread_mutex_unlock(&global_info->ring_lock);
        pthread_join(global_info->ring_thread, NULL);
        pthread_mutex_destroy(&global_info->ring_lock);
        pthread_cond_destroy(&global_info->ring_cond);
    }

    vkd3d_free_device_memory(device, &global_info->control_device_allocation);
    VK_CALL(vkDestroyBuffer(device->vk_device, global_info->vk_control_buffer, NULL));

    vkd3d_free_device_memory(device, &global_info->payload_device_allocation);
    VK_CALL(vkDestroyBuffer(device->vk_device, global_info->vk_payload_buffer, NULL));

    vkd3d_free(global_info->qa_ranges);
    vkd3d_free(global_info);
}

#define DECL_BUFFER() \
    char buffer[4096]; \
    char *ptr; \
    ptr = buffer; \
    *ptr = '\0'

#define FLUSH_BUFFER() do { \
    pthread_mutex_lock(&debug_lock); \
    fprintf(descriptor_debug_file, "%s\n", buffer); \
    pthread_mutex_unlock(&debug_lock); \
    fflush(descriptor_debug_file); \
} while (0)

#define APPEND_SNPRINTF(...) do { ptr += strlen(ptr); snprintf(ptr, (buffer + ARRAY_SIZE(buffer)) - ptr, __VA_ARGS__); } while(0)

static void vkd3d_descriptor_debug_qa_check_report_fault(
        struct vkd3d_descriptor_qa_global_info *global_info)
{
    DECL_BUFFER();

    if (global_info->payload_data->fault_type & VKD3D_DESCRIPTOR_FAULT_TYPE_HEAP_OF_OF_RANGE)
        APPEND_SNPRINTF("Fault type: HEAP_OUT_OF_RANGE\n");
    if (global_info->payload_data->fault_type & VKD3D_DESCRIPTOR_FAULT_TYPE_MISMATCH_DESCRIPTOR_TYPE)
        APPEND_SNPRINTF("Fault type: MISMATCH_DESCRIPTOR_TYPE\n");
    if (global_info->payload_data->fault_type & VKD3D_DESCRIPTOR_FAULT_TYPE_DESTROYED_RESOURCE)
        APPEND_SNPRINTF("Fault type: DESTROYED_RESOURCE\n");

    APPEND_SNPRINTF("CBV_SRV_UAV heap cookie: %u\n", global_info->payload_data->failed_heap);
    APPEND_SNPRINTF("Shader hash and instruction: %"PRIx64" (%u)\n",
            global_info->payload_data->failed_hash, global_info->payload_data->failed_instruction);
    APPEND_SNPRINTF("Accessed resource/view cookie: %u\n", global_info->payload_data->failed_cookie);
    APPEND_SNPRINTF("Shader desired descriptor type: %u (%s)\n",
            global_info->payload_data->failed_descriptor_type_mask,
            debug_descriptor_type(global_info->payload_data->failed_descriptor_type_mask));
    APPEND_SNPRINTF("Found descriptor type in heap: %u (%s)\n",
            global_info->payload_data->actual_descriptor_type_mask,
            debug_descriptor_type(global_info->payload_data->actual_descriptor_type_mask));
    APPEND_SNPRINTF("Failed heap index: %u\n", global_info->payload_data->failed_offset);
    ERR("\n============\n%s==========\n", buffer);
    if (!vkd3d_descriptor_debug_active_log())
        return;
    FLUSH_BUFFER();
}

void vkd3d_descriptor_debug_register_heap(
        struct vkd3d_descriptor_qa_heap_buffer_data *heap, uint64_t cookie,
        const D3D12_DESCRIPTOR_HEAP_DESC *desc)
{
    unsigned int i;
    DECL_BUFFER();

    if (heap)
    {
        heap->num_descriptors = desc->NumDescriptors;
        heap->heap_index = cookie <= UINT32_MAX ? (uint32_t)cookie : 0u;
        for (i = 0; i < desc->NumDescriptors; i++)
        {
            heap->desc[i].cookie = 0;
            heap->desc[i].descriptor_type = ~0u;
        }
    }

    if (!vkd3d_descriptor_debug_active_log())
        return;

    APPEND_SNPRINTF("REGISTER HEAP %"PRIu64" || COUNT = %u", cookie, desc->NumDescriptors);
    if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        APPEND_SNPRINTF(" || SHADER");

    switch (desc->Type)
    {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            APPEND_SNPRINTF(" || CBV_SRV_UAV");
            break;

        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            APPEND_SNPRINTF(" || SAMPLER");
            break;

        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
            APPEND_SNPRINTF(" || RTV");
            break;

        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
            APPEND_SNPRINTF(" || DSV");
            break;

        default:
            APPEND_SNPRINTF(" || ?");
            break;
    }

    FLUSH_BUFFER();
}

void vkd3d_descriptor_debug_unregister_heap(uint64_t cookie)
{
    DECL_BUFFER();
    if (!vkd3d_descriptor_debug_active_log())
        return;

    APPEND_SNPRINTF("DESTROY HEAP %"PRIu64, cookie);
    FLUSH_BUFFER();
}

void vkd3d_descriptor_debug_register_query_heap_cookie(struct vkd3d_descriptor_qa_global_info *global_info,
        uint64_t cookie, const D3D12_QUERY_HEAP_DESC *desc)
{
    const char *type_desc;
    DECL_BUFFER();

    if (!vkd3d_descriptor_debug_active_log())
        return;

    APPEND_SNPRINTF("QUERY HEAP CREATE #%"PRIu64" || ", cookie);

    switch (desc->Type)
    {
        case D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP:
            type_desc = "COPY_QUEUE_TIMESTAMP";
            break;

        case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
            type_desc = "OCCLUSION";
            break;

        case D3D12_QUERY_HEAP_TYPE_TIMESTAMP:
            type_desc = "TIMESTAMP";
            break;

        case D3D12_QUERY_HEAP_TYPE_VIDEO_DECODE_STATISTICS:
            type_desc = "VIDEO_DECODE_STATISTICS";
            break;

        case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
            type_desc = "PIPELINE_STATISTICS";
            break;

        case D3D12_QUERY_HEAP_TYPE_SO_STATISTICS:
            type_desc = "SO_STATISTICS";
            break;

        default:
            type_desc = "???";
            break;
    }

    APPEND_SNPRINTF(" || %s || Count = %u", type_desc, desc->Count);
    FLUSH_BUFFER();
}

void vkd3d_descriptor_debug_register_resource_cookie(struct vkd3d_descriptor_qa_global_info *global_info,
        uint64_t cookie, const D3D12_RESOURCE_DESC1 *desc)
{
    const char *fmt;
    DECL_BUFFER();

    vkd3d_descriptor_debug_set_live_status_bit(global_info, cookie);

    if (!vkd3d_descriptor_debug_active_log())
        return;

    APPEND_SNPRINTF("RESOURCE CREATE #%"PRIu64" || ", cookie);

    fmt = debug_dxgi_format(desc->Format);

    switch (desc->Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            APPEND_SNPRINTF("Buffer");
            APPEND_SNPRINTF(" || Size = 0x%"PRIx64" bytes", desc->Width);
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            APPEND_SNPRINTF("Tex1D");
            APPEND_SNPRINTF(" || Format = %s || Levels = %u || Layers = %u || Width = %"PRIu64,
                    fmt, desc->MipLevels, desc->DepthOrArraySize, desc->Width);
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            APPEND_SNPRINTF("Tex2D");
            APPEND_SNPRINTF(" || Format = %s || Levels = %u || Layers = %u || Width = %"PRIu64" || Height = %u",
                    fmt, desc->MipLevels, desc->DepthOrArraySize, desc->Width, desc->Height);
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            APPEND_SNPRINTF("Tex3D");
            APPEND_SNPRINTF(" || Format = %s || Levels = %u || Width = %"PRIu64" || Height = %u || Depth = %u",
                    fmt, desc->MipLevels, desc->Width, desc->Height, desc->DepthOrArraySize);
            break;

        default:
            APPEND_SNPRINTF("Unknown dimension");
            break;
    }

    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
        APPEND_SNPRINTF(" || UAV");
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
        APPEND_SNPRINTF(" || RTV");
    if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
        APPEND_SNPRINTF(" || DSV");

    FLUSH_BUFFER();
}

void vkd3d_descriptor_debug_register_allocation_cookie(
        struct vkd3d_descriptor_qa_global_info *global_info,
        uint64_t cookie, const struct vkd3d_allocate_memory_info *info)
{
    D3D12_RESOURCE_DESC1 desc;

    memset(&desc, 0, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = info->memory_requirements.size;
    vkd3d_descriptor_debug_register_resource_cookie(global_info, cookie, &desc);
}

void vkd3d_descriptor_debug_register_view_cookie(
        struct vkd3d_descriptor_qa_global_info *global_info,
        uint64_t cookie, uint64_t resource_cookie)
{
    DECL_BUFFER();

    vkd3d_descriptor_debug_set_live_status_bit(global_info, cookie);

    if (!vkd3d_descriptor_debug_active_log())
        return;
    APPEND_SNPRINTF("VIEW CREATE #%"PRIu64" <- RESOURCE #%"PRIu64, cookie, resource_cookie);
    FLUSH_BUFFER();
}

void vkd3d_descriptor_debug_unregister_cookie(
        struct vkd3d_descriptor_qa_global_info *global_info,
        uint64_t cookie)
{
    DECL_BUFFER();

    /* Don't unset the null descriptor by mistake. */
    if (cookie != 0)
        vkd3d_descriptor_debug_unset_live_status_bit(global_info, cookie);

    if (!vkd3d_descriptor_debug_active_log())
        return;
    APPEND_SNPRINTF("COOKIE DESTROY #%"PRIu64, cookie);
    FLUSH_BUFFER();
}

void vkd3d_descriptor_debug_write_descriptor(struct vkd3d_descriptor_qa_heap_buffer_data *heap, uint64_t heap_cookie,
        uint32_t offset, vkd3d_descriptor_qa_flags type_flags, uint64_t cookie)
{
    DECL_BUFFER();

    if (heap && offset < heap->num_descriptors)
    {
        /* Should never overflow here except if game is literally spamming allocations every frame and we
         * wait around for hours/days.
         * This case will trigger warnings either way. */
        heap->desc[offset].cookie = cookie <= UINT32_MAX ? (uint32_t)cookie : 0u;
        heap->desc[offset].descriptor_type = type_flags;
    }

    if (!vkd3d_descriptor_debug_active_log())
        return;
    APPEND_SNPRINTF("WRITE HEAP %"PRIu64" || OFFSET = %u || TYPE = %s || COOKIE = #%"PRIu64,
            heap_cookie, offset, debug_descriptor_type(type_flags), cookie);
    FLUSH_BUFFER();
}

void vkd3d_descriptor_debug_copy_descriptor(
        struct vkd3d_descriptor_qa_heap_buffer_data *dst_heap, uint64_t dst_heap_cookie, uint32_t dst_offset,
        struct vkd3d_descriptor_qa_heap_buffer_data *src_heap, uint64_t src_heap_cookie, uint32_t src_offset,
        uint64_t cookie)
{
    DECL_BUFFER();

    if (dst_heap && src_heap && dst_offset < dst_heap->num_descriptors && src_offset < src_heap->num_descriptors)
        dst_heap->desc[dst_offset] = src_heap->desc[src_offset];

    if (!vkd3d_descriptor_debug_active_log())
        return;
    APPEND_SNPRINTF("COPY DST HEAP %"PRIu64" || DST OFFSET = %u || COOKIE = #%"PRIu64" || SRC HEAP %"PRIu64" || SRC OFFSET = %u",
            dst_heap_cookie, dst_offset, cookie, src_heap_cookie, src_offset);
    FLUSH_BUFFER();
}
