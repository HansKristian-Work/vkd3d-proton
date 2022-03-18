/*
 * Copyright 2020 Philip Rebohle for Valve Corporation
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

#include "vkd3d_private.h"
#include "vkd3d_shader.h"

struct vkd3d_cached_pipeline_key
{
    size_t name_length;
    const void *name;
    uint64_t internal_key_hash; /* Used for internal keys which are just hashes. Used if name_length is 0. */
};

struct vkd3d_cached_pipeline_data
{
    const void *blob;
    size_t blob_length;
    size_t is_new; /* Avoid padding issues. */
    /* Need to internally hold a PSO and hand out the same one on subsequent LoadLibrary.
     * This is a good performance boost for applications which load PSOs from library directly
     * multiple times throughout the lifetime of an application. */
    struct d3d12_pipeline_state *state;
};

struct vkd3d_cached_pipeline_entry
{
    struct hash_map_entry entry;
    struct vkd3d_cached_pipeline_key key;
    struct vkd3d_cached_pipeline_data data;
};

#define VKD3D_PIPELINE_BLOB_CHUNK_SIZE(type) \
    align(sizeof(struct vkd3d_pipeline_blob_chunk) + sizeof(struct vkd3d_pipeline_blob_chunk_##type), \
    VKD3D_PIPELINE_BLOB_CHUNK_ALIGN)
#define VKD3D_PIPELINE_BLOB_CHUNK_SIZE_RAW(extra) \
    align(sizeof(struct vkd3d_pipeline_blob_chunk) + (extra), \
    VKD3D_PIPELINE_BLOB_CHUNK_ALIGN)
#define VKD3D_PIPELINE_BLOB_CHUNK_SIZE_VARIABLE(type, extra) \
    align(sizeof(struct vkd3d_pipeline_blob_chunk) + sizeof(struct vkd3d_pipeline_blob_chunk_##type) + (extra), \
    VKD3D_PIPELINE_BLOB_CHUNK_ALIGN)

#define CAST_CHUNK_BASE(blob) ((struct vkd3d_pipeline_blob_chunk *)((blob)->data))
#define CONST_CAST_CHUNK_BASE(blob) ((const struct vkd3d_pipeline_blob_chunk *)((blob)->data))
#define CAST_CHUNK_DATA(chunk, type) ((struct vkd3d_pipeline_blob_chunk_##type *)((chunk)->data))
#define CONST_CAST_CHUNK_DATA(chunk, type) ((const struct vkd3d_pipeline_blob_chunk_##type *)((chunk)->data))

#define VKD3D_PIPELINE_BLOB_ALIGN 8
#define VKD3D_PIPELINE_BLOB_CHUNK_ALIGN 8

static size_t vkd3d_compute_size_varint(const uint32_t *words, size_t word_count)
{
    size_t size = 0;
    uint32_t w;
    size_t i;

    for (i = 0; i < word_count; i++)
    {
        w = words[i];
        if (w < (1u << 7))
            size += 1;
        else if (w < (1u << 14))
            size += 2;
        else if (w < (1u << 21))
            size += 3;
        else if (w < (1u << 28))
            size += 4;
        else
            size += 5;
    }
    return size;
}

static uint8_t *vkd3d_encode_varint(uint8_t *buffer, const uint32_t *words, size_t word_count)
{
    uint32_t w;
    size_t i;
    for (i = 0; i < word_count; i++)
    {
        w = words[i];
        if (w < (1u << 7))
            *buffer++ = w;
        else if (w < (1u << 14))
        {
            *buffer++ = 0x80u | ((w >> 0) & 0x7f);
            *buffer++ = (w >> 7) & 0x7f;
        }
        else if (w < (1u << 21))
        {
            *buffer++ = 0x80u | ((w >> 0) & 0x7f);
            *buffer++ = 0x80u | ((w >> 7) & 0x7f);
            *buffer++ = (w >> 14) & 0x7f;
        }
        else if (w < (1u << 28))
        {
            *buffer++ = 0x80u | ((w >> 0) & 0x7f);
            *buffer++ = 0x80u | ((w >> 7) & 0x7f);
            *buffer++ = 0x80u | ((w >> 14) & 0x7f);
            *buffer++ = (w >> 21) & 0x7f;
        }
        else
        {
            *buffer++ = 0x80u | ((w >> 0) & 0x7f);
            *buffer++ = 0x80u | ((w >> 7) & 0x7f);
            *buffer++ = 0x80u | ((w >> 14) & 0x7f);
            *buffer++ = 0x80u | ((w >> 21) & 0x7f);
            *buffer++ = (w >> 28) & 0x7f;
        }
    }

    return buffer;
}

static bool vkd3d_decode_varint(uint32_t *words, size_t words_size, const uint8_t *buffer, size_t buffer_size)
{
    size_t offset = 0;
    uint32_t shift;
    uint32_t *w;
    size_t i;

    for (i = 0; i < words_size; i++)
    {
        w = &words[i];
        *w = 0;

        shift = 0;
        do
        {
            if (offset >= buffer_size || shift >= 32u)
                return false;

            *w |= (buffer[offset] & 0x7f) << shift;
            shift += 7;
        } while (buffer[offset++] & 0x80);
    }

    return buffer_size == offset;
}

VkResult vkd3d_create_pipeline_cache(struct d3d12_device *device,
        size_t size, const void *data, VkPipelineCache *cache)
{
    const struct vkd3d_vk_device_procs *vk_procs = &device->vk_procs;

    VkPipelineCacheCreateInfo info;
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    info.pNext = NULL;
    info.flags = 0;
    info.initialDataSize = size;
    info.pInitialData = data;

    return VK_CALL(vkCreatePipelineCache(device->vk_device, &info, NULL, cache));
}

#define VKD3D_CACHE_BLOB_VERSION MAKE_MAGIC('V','K','B',3)

enum vkd3d_pipeline_blob_chunk_type
{
    /* VkPipelineCache blob data. */
    VKD3D_PIPELINE_BLOB_CHUNK_TYPE_PIPELINE_CACHE = 0,
    /* VkShaderStage is stored in upper 16 bits. */
    VKD3D_PIPELINE_BLOB_CHUNK_TYPE_VARINT_SPIRV = 1,
    /* For when a blob is stored inside a pipeline library, we can reference blobs by hash instead
     * to achieve de-dupe. We'll need to maintain the older path as well however since we need to support GetCachedBlob()
     * as a standalone thing as well. */
    VKD3D_PIPELINE_BLOB_CHUNK_TYPE_PIPELINE_CACHE_LINK = 2,
    /* VkShaderStage is stored in upper 16 bits. */
    VKD3D_PIPELINE_BLOB_CHUNK_TYPE_VARINT_SPIRV_LINK = 3,
    /* VkShaderStage is stored in upper 16 bits. */
    VKD3D_PIPELINE_BLOB_CHUNK_TYPE_SHADER_META = 4,
    VKD3D_PIPELINE_BLOB_CHUNK_TYPE_PSO_COMPAT = 5,
    VKD3D_PIPELINE_BLOB_CHUNK_TYPE_MASK = 0xffff,
    VKD3D_PIPELINE_BLOB_CHUNK_INDEX_SHIFT = 16,
};

struct vkd3d_pipeline_blob_chunk
{
    uint32_t type; /* vkd3d_pipeline_blob_chunk_type with extra data in upper bits. */
    uint32_t size; /* size of data. Does not include size of header. */
    uint8_t data[]; /* struct vkd3d_pipeline_blob_chunk_*. */
};

struct vkd3d_pipeline_blob_chunk_spirv
{
    uint32_t decompressed_spirv_size;
    uint32_t compressed_spirv_size; /* Size of data[]. */
    uint8_t data[];
};

struct vkd3d_pipeline_blob_chunk_link
{
    uint64_t hash;
};

struct vkd3d_pipeline_blob_chunk_shader_meta
{
    struct vkd3d_shader_meta meta;
};

struct vkd3d_pipeline_blob_chunk_pso_compat
{
    struct vkd3d_pipeline_cache_compatibility compat;
};

STATIC_ASSERT(sizeof(struct vkd3d_pipeline_blob_chunk) == 8);
STATIC_ASSERT(offsetof(struct vkd3d_pipeline_blob_chunk, data) == 8);
STATIC_ASSERT(sizeof(struct vkd3d_pipeline_blob_chunk_spirv) == 8);
STATIC_ASSERT(sizeof(struct vkd3d_pipeline_blob_chunk_spirv) == offsetof(struct vkd3d_pipeline_blob_chunk_spirv, data));

struct vkd3d_pipeline_blob
{
    uint32_t version;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t checksum; /* Simple checksum for data[] as a sanity check. uint32_t because it conveniently packs here. */
    uint64_t vkd3d_build;
    uint64_t vkd3d_shader_interface_key;
    uint8_t cache_uuid[VK_UUID_SIZE];
    uint8_t data[]; /* vkd3d_pipeline_blob_chunks laid out one after the other with u32 alignment. */
};

/* Used for de-duplicated pipeline cache and SPIR-V hashmaps. */
struct vkd3d_pipeline_blob_internal
{
    uint32_t checksum; /* Simple checksum for data[] as a sanity check. */
    uint8_t data[]; /* Either raw uint8_t for pipeline cache, or vkd3d_pipeline_blob_chunk_spirv. */
};

STATIC_ASSERT(offsetof(struct vkd3d_pipeline_blob, data) == (32 + VK_UUID_SIZE));
STATIC_ASSERT(offsetof(struct vkd3d_pipeline_blob, data) == sizeof(struct vkd3d_pipeline_blob));

static uint32_t vkd3d_pipeline_blob_compute_data_checksum(const uint8_t *data, size_t size)
{
    const struct vkd3d_shader_code code = { data, size };
    vkd3d_shader_hash_t h;

    h = vkd3d_shader_hash(&code);
    return hash_uint64(h);
}

static const struct vkd3d_pipeline_blob_chunk *find_blob_chunk(const struct vkd3d_pipeline_blob_chunk *chunk,
        size_t size, uint32_t type)
{
    uint32_t aligned_chunk_size;

    while (size >= sizeof(struct vkd3d_pipeline_blob_chunk))
    {
        aligned_chunk_size = align(chunk->size + sizeof(struct vkd3d_pipeline_blob_chunk),
                VKD3D_PIPELINE_BLOB_CHUNK_ALIGN);
        if (aligned_chunk_size > size)
            return NULL;
        if (chunk->type == type)
            return chunk;

        chunk = (const struct vkd3d_pipeline_blob_chunk *)&chunk->data[align(chunk->size, VKD3D_PIPELINE_BLOB_CHUNK_ALIGN)];
        size -= aligned_chunk_size;
    }

    return NULL;
}

HRESULT d3d12_cached_pipeline_state_validate(struct d3d12_device *device,
        const struct d3d12_cached_pipeline_state *state,
        const struct vkd3d_pipeline_cache_compatibility *compat)
{
    const VkPhysicalDeviceProperties *device_properties = &device->device_info.properties2.properties;
    const struct vkd3d_pipeline_blob *blob = state->blob.pCachedBlob;
    const struct vkd3d_pipeline_blob_chunk_pso_compat *pso_compat;
    const struct vkd3d_pipeline_blob_chunk *chunk;
    size_t payload_size;
    uint32_t checksum;

    /* Avoid E_INVALIDARG with an invalid header size, since that may confuse some games */
    if (state->blob.CachedBlobSizeInBytes < sizeof(*blob) || blob->version != VKD3D_CACHE_BLOB_VERSION)
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;

    payload_size = state->blob.CachedBlobSizeInBytes - offsetof(struct vkd3d_pipeline_blob, data);

    /* Indicate that the cached data is not useful if we're running on a different device or driver */
    if (blob->vendor_id != device_properties->vendorID || blob->device_id != device_properties->deviceID)
        return D3D12_ERROR_ADAPTER_NOT_FOUND;

    /* Check the vkd3d-proton build since the shader compiler itself may change,
     * and the driver since that will affect the generated pipeline cache.
     * Based on global configuration flags, which extensions are available, etc,
     * the generated shaders may also change, so key on that as well. */
    if (blob->vkd3d_build != vkd3d_build ||
            blob->vkd3d_shader_interface_key != device->shader_interface_key ||
            memcmp(blob->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE) != 0)
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;

    checksum = vkd3d_pipeline_blob_compute_data_checksum(blob->data, payload_size);

    if (checksum != blob->checksum)
    {
        ERR("Corrupt PSO cache blob entry found!\n");
        /* Same rationale as above, avoid E_INVALIDARG, since that may confuse some games */
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
    }

    /* Fetch compat info. */
    chunk = find_blob_chunk(CONST_CAST_CHUNK_BASE(blob), payload_size, VKD3D_PIPELINE_BLOB_CHUNK_TYPE_PSO_COMPAT);
    if (!chunk || chunk->size != sizeof(*pso_compat))
    {
        ERR("Corrupt PSO cache blob entry found! Invalid PSO compat blob size.\n");
        /* Same rationale as above, avoid E_INVALIDARG, since that may confuse some games */
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
    }

    /* Beyond this point, we are required to validate that input descriptions match the pipeline blob,
     * and we are required to return E_INVALIDARG if we find errors. */

    pso_compat = CONST_CAST_CHUNK_DATA(chunk, pso_compat);

    /* Verify the expected PSO state that was used. This must match, or we have to fail compilation as per API spec. */
    if (compat->state_desc_compat_hash != pso_compat->compat.state_desc_compat_hash)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("PSO compatibility hash mismatch.\n");
        else
            WARN("PSO compatibility hash mismatch.\n");
        return E_INVALIDARG;
    }

    /* Verify the expected root signature that was used to generate the SPIR-V. */
    if (compat->root_signature_compat_hash != pso_compat->compat.root_signature_compat_hash)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("Root signature compatibility hash mismatch.\n");
        else
            WARN("Root signature compatibility hash mismatch.\n");
        return E_INVALIDARG;
    }

    /* Verify that DXBC shader blobs match. */
    if (memcmp(compat->dxbc_blob_hashes, pso_compat->compat.dxbc_blob_hashes, sizeof(compat->dxbc_blob_hashes)) != 0)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("DXBC blob hash mismatch.\n");
        else
            WARN("DXBC blob hash mismatch.\n");
        return E_INVALIDARG;
    }

    return S_OK;
}

static struct vkd3d_pipeline_blob_chunk *finish_and_iterate_blob_chunk(struct vkd3d_pipeline_blob_chunk *chunk)
{
    uint32_t aligned_size = align(chunk->size, VKD3D_PIPELINE_BLOB_CHUNK_ALIGN);
    /* Ensure we get stable hashes if we need to pad. */
    memset(&chunk->data[chunk->size], 0, aligned_size - chunk->size);
    return (struct vkd3d_pipeline_blob_chunk *)&chunk->data[aligned_size];
}

static bool d3d12_pipeline_library_find_internal_blob(struct d3d12_pipeline_library *pipeline_library,
        const struct hash_map *map, uint64_t hash, const void **data, size_t *size)
{
    const struct vkd3d_pipeline_blob_internal *internal;
    const struct vkd3d_cached_pipeline_entry *entry;
    struct vkd3d_cached_pipeline_key key;
    uint32_t checksum;
    bool ret = false;

    /* We are called from within D3D12 PSO creation, and we won't have read locks active here. */
    if (rwlock_lock_read(&pipeline_library->mutex))
        return false;

    key.name_length = 0;
    key.name = NULL;
    key.internal_key_hash = hash;
    entry = (const struct vkd3d_cached_pipeline_entry *)hash_map_find(map, &key);

    if (entry)
    {
        internal = entry->data.blob;
        if (entry->data.blob_length < sizeof(*internal))
        {
            FIXME("Internal blob length is too small.\n");
            goto out;
        }

        *data = internal->data;
        *size = entry->data.blob_length - sizeof(*internal);
        checksum = vkd3d_pipeline_blob_compute_data_checksum(*data, *size);
        if (checksum != internal->checksum)
        {
            FIXME("Checksum mismatch.\n");
            goto out;
        }

        ret = true;
    }

out:
    rwlock_unlock_read(&pipeline_library->mutex);
    return ret;
}

HRESULT vkd3d_create_pipeline_cache_from_d3d12_desc(struct d3d12_device *device,
        const struct d3d12_cached_pipeline_state *state, VkPipelineCache *cache)
{
    const struct vkd3d_pipeline_blob *blob = state->blob.pCachedBlob;
    const struct vkd3d_pipeline_blob_chunk_link *link;
    const struct vkd3d_pipeline_blob_chunk *chunk;
    size_t payload_size;
    const void *data;
    size_t size;
    VkResult vr;

    if (!state->blob.CachedBlobSizeInBytes)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("No PSO cache was provided, creating empty pipeline cache.\n");
        vr = vkd3d_create_pipeline_cache(device, 0, NULL, cache);
        return hresult_from_vk_result(vr);
    }

    payload_size = state->blob.CachedBlobSizeInBytes - offsetof(struct vkd3d_pipeline_blob, data);
    chunk = find_blob_chunk(CONST_CAST_CHUNK_BASE(blob), payload_size, VKD3D_PIPELINE_BLOB_CHUNK_TYPE_PIPELINE_CACHE);

    /* Try to find embedded cache first, then attempt to find link references. */

    if (chunk)
    {
        data = chunk->data;
        size = chunk->size;
    }
    else if (state->library && (chunk = find_blob_chunk(CONST_CAST_CHUNK_BASE(blob), payload_size,
            VKD3D_PIPELINE_BLOB_CHUNK_TYPE_PIPELINE_CACHE_LINK)))
    {
        link = CONST_CAST_CHUNK_DATA(chunk, link);

        if (!d3d12_pipeline_library_find_internal_blob(state->library,
                &state->library->driver_cache_map, link->hash, &data, &size))
        {
            if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
                INFO("Did not find internal PSO cache reference %016"PRIx64".\n", link->hash);

            data = NULL;
            size = 0;
        }
    }
    else
    {
        data = NULL;
        size = 0;
    }

    vr = vkd3d_create_pipeline_cache(device, size, data, cache);
    return hresult_from_vk_result(vr);
}

HRESULT vkd3d_get_cached_spirv_code_from_d3d12_desc(
        const struct d3d12_cached_pipeline_state *state,
        VkShaderStageFlagBits stage,
        struct vkd3d_shader_code *spirv_code)
{
    const struct vkd3d_pipeline_blob *blob = state->blob.pCachedBlob;
    const struct vkd3d_pipeline_blob_chunk_shader_meta *meta;
    const struct vkd3d_pipeline_blob_chunk_spirv *spirv;
    const struct vkd3d_pipeline_blob_chunk_link *link;
    const struct vkd3d_pipeline_blob_chunk *chunk;
    size_t internal_blob_size;
    size_t payload_size;
    void *duped_code;

    if (!state->blob.CachedBlobSizeInBytes)
        return E_FAIL;

    payload_size = state->blob.CachedBlobSizeInBytes - offsetof(struct vkd3d_pipeline_blob, data);

    /* Fetch and validate shader meta. */
    chunk = find_blob_chunk(CONST_CAST_CHUNK_BASE(blob), payload_size,
            VKD3D_PIPELINE_BLOB_CHUNK_TYPE_SHADER_META | (stage << VKD3D_PIPELINE_BLOB_CHUNK_INDEX_SHIFT));
    if (!chunk || chunk->size != sizeof(*meta))
        return E_FAIL;
    meta = CONST_CAST_CHUNK_DATA(chunk, shader_meta);
    memcpy(&spirv_code->meta, &meta->meta, sizeof(meta->meta));

    /* Aim to pull SPIR-V either from inlined chunk, or a link. */
    chunk = find_blob_chunk(CONST_CAST_CHUNK_BASE(blob), payload_size,
            VKD3D_PIPELINE_BLOB_CHUNK_TYPE_VARINT_SPIRV | (stage << VKD3D_PIPELINE_BLOB_CHUNK_INDEX_SHIFT));

    if (chunk)
    {
        spirv = CONST_CAST_CHUNK_DATA(chunk, spirv);
    }
    else if (state->library && (chunk = find_blob_chunk(CONST_CAST_CHUNK_BASE(blob), payload_size,
            VKD3D_PIPELINE_BLOB_CHUNK_TYPE_VARINT_SPIRV_LINK | (stage << VKD3D_PIPELINE_BLOB_CHUNK_INDEX_SHIFT))))
    {
        link = CONST_CAST_CHUNK_DATA(chunk, link);
        if (!d3d12_pipeline_library_find_internal_blob(state->library, &state->library->spirv_cache_map,
                link->hash, (const void **)&spirv, &internal_blob_size))
        {
            FIXME("Did not find internal SPIR-V reference %016"PRIx64".\n", link->hash);
            spirv = NULL;
        }

        if (spirv)
        {
            if (internal_blob_size < sizeof(struct vkd3d_pipeline_blob_chunk_spirv) + spirv->compressed_spirv_size)
            {
                FIXME("Unexpected low internal blob size.\n");
                spirv = NULL;
            }
        }
    }
    else
        spirv = NULL;

    if (!spirv)
        return E_FAIL;

    duped_code = vkd3d_malloc(spirv->decompressed_spirv_size);
    if (!duped_code)
        return E_OUTOFMEMORY;

    if (!vkd3d_decode_varint(duped_code,
            spirv->decompressed_spirv_size / sizeof(uint32_t),
            spirv->data, spirv->compressed_spirv_size))
    {
        FIXME("Failed to decode VARINT.\n");
        vkd3d_free(duped_code);
        return E_INVALIDARG;
    }

    spirv_code->code = duped_code;
    spirv_code->size = spirv->decompressed_spirv_size;

    return S_OK;
}

static uint32_t d3d12_cached_pipeline_entry_name_table_size(const struct vkd3d_cached_pipeline_entry *entry)
{
    if (entry->key.name_length)
        return entry->key.name_length;
    else
        return sizeof(entry->key.internal_key_hash);
}

static bool d3d12_pipeline_library_insert_hash_map_blob_locked(struct d3d12_pipeline_library *pipeline_library,
        struct hash_map *map, const struct vkd3d_cached_pipeline_entry *entry)
{
    const struct vkd3d_cached_pipeline_entry *new_entry;
    if ((new_entry = (const struct vkd3d_cached_pipeline_entry*)hash_map_insert(map, &entry->key, &entry->entry)) &&
            new_entry->data.blob == entry->data.blob)
    {
        pipeline_library->total_name_table_size += d3d12_cached_pipeline_entry_name_table_size(entry);
        pipeline_library->total_blob_size += align(entry->data.blob_length, VKD3D_PIPELINE_BLOB_ALIGN);
        return true;
    }
    else
        return false;
}

static bool d3d12_pipeline_library_insert_hash_map_blob_internal(struct d3d12_pipeline_library *pipeline_library,
        struct hash_map *map, const struct vkd3d_cached_pipeline_entry *entry)
{
    /* Used for internal hashmap updates. We expect reasonable amount of duplicates,
     * prefer read -> write promotion. */
    const struct vkd3d_cached_pipeline_entry *new_entry;
    bool ret;
    int rc;

    if ((rc = rwlock_lock_read(&pipeline_library->internal_hashmap_mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return false;
    }

    if (hash_map_find(map, &entry->key))
    {
        rwlock_unlock_read(&pipeline_library->internal_hashmap_mutex);
        return false;
    }

    rwlock_unlock_read(&pipeline_library->internal_hashmap_mutex);
    if ((rc = rwlock_lock_write(&pipeline_library->internal_hashmap_mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return false;
    }

    if ((new_entry = (const struct vkd3d_cached_pipeline_entry*)hash_map_insert(map, &entry->key, &entry->entry)) &&
            new_entry->data.blob == entry->data.blob)
    {
        pipeline_library->total_name_table_size += d3d12_cached_pipeline_entry_name_table_size(entry);
        pipeline_library->total_blob_size += align(entry->data.blob_length, VKD3D_PIPELINE_BLOB_ALIGN);
        ret = true;
    }
    else
        ret = false;

    rwlock_unlock_write(&pipeline_library->internal_hashmap_mutex);
    return ret;
}

static size_t vkd3d_shader_code_compute_serialized_size(const struct vkd3d_shader_code *code,
        size_t *out_varint_size, bool inline_spirv)
{
    size_t varint_size = 0;
    size_t blob_size = 0;

    if (code->size && !(code->meta.flags & VKD3D_SHADER_META_FLAG_REPLACED))
    {
        if (out_varint_size || inline_spirv)
            varint_size = vkd3d_compute_size_varint(code->code, code->size / sizeof(uint32_t));

        /* If we have a pipeline library, we will store a reference to the SPIR-V instead. */
        if (inline_spirv)
            blob_size += VKD3D_PIPELINE_BLOB_CHUNK_SIZE_VARIABLE(spirv, varint_size);
        else
            blob_size += VKD3D_PIPELINE_BLOB_CHUNK_SIZE(link);

        blob_size += VKD3D_PIPELINE_BLOB_CHUNK_SIZE(shader_meta);
    }

    if (out_varint_size)
        *out_varint_size = varint_size;
    return blob_size;
}

static void vkd3d_shader_code_serialize_inline(const struct vkd3d_shader_code *code,
        VkShaderStageFlagBits stage, size_t varint_size,
        struct vkd3d_pipeline_blob_chunk **inout_chunk)
{
    struct vkd3d_pipeline_blob_chunk *chunk = *inout_chunk;
    struct vkd3d_pipeline_blob_chunk_shader_meta *meta;
    struct vkd3d_pipeline_blob_chunk_spirv *spirv;

    if (code->size && !(code->meta.flags & VKD3D_SHADER_META_FLAG_REPLACED))
    {
        chunk->type = VKD3D_PIPELINE_BLOB_CHUNK_TYPE_VARINT_SPIRV | (stage << VKD3D_PIPELINE_BLOB_CHUNK_INDEX_SHIFT);
        chunk->size = sizeof(struct vkd3d_pipeline_blob_chunk_spirv) + varint_size;

        spirv = CAST_CHUNK_DATA(chunk, spirv);
        spirv->compressed_spirv_size = varint_size;
        spirv->decompressed_spirv_size = code->size;

        vkd3d_encode_varint(spirv->data, code->code, code->size / sizeof(uint32_t));
        chunk = finish_and_iterate_blob_chunk(chunk);

        /* Store meta information for SPIR-V. */
        chunk->type = VKD3D_PIPELINE_BLOB_CHUNK_TYPE_SHADER_META | (stage << VKD3D_PIPELINE_BLOB_CHUNK_INDEX_SHIFT);
        chunk->size = sizeof(*meta);
        meta = CAST_CHUNK_DATA(chunk, shader_meta);
        meta->meta = code->meta;
        chunk = finish_and_iterate_blob_chunk(chunk);
    }

    *inout_chunk = chunk;
}

static void vkd3d_shader_code_serialize_referenced(struct d3d12_pipeline_library *pipeline_library,
        const struct vkd3d_shader_code *code,
        VkShaderStageFlagBits stage, size_t varint_size,
        struct vkd3d_pipeline_blob_chunk **inout_chunk)
{
    struct vkd3d_pipeline_blob_chunk *chunk = *inout_chunk;
    struct vkd3d_pipeline_blob_chunk_shader_meta *meta;
    struct vkd3d_pipeline_blob_chunk_spirv *spirv;
    struct vkd3d_pipeline_blob_internal *internal;
    struct vkd3d_pipeline_blob_chunk_link *link;
    struct vkd3d_cached_pipeline_entry entry;
    struct vkd3d_shader_code blob;
    size_t wrapped_varint_size;

    if (code->size && !(code->meta.flags & VKD3D_SHADER_META_FLAG_REPLACED))
    {
        entry.key.name_length = 0;
        entry.key.name = NULL;
        entry.data.is_new = 1;
        entry.data.state = NULL;

        wrapped_varint_size = sizeof(struct vkd3d_pipeline_blob_chunk_spirv) + varint_size;
        entry.data.blob_length = sizeof(*internal) + wrapped_varint_size;
        internal = vkd3d_malloc(entry.data.blob_length);

        spirv = CAST_CHUNK_DATA(internal, spirv);
        spirv->compressed_spirv_size = varint_size;
        spirv->decompressed_spirv_size = code->size;
        vkd3d_encode_varint(spirv->data, code->code, code->size / sizeof(uint32_t));

        entry.data.blob = internal;
        blob.code = spirv->data;
        blob.size = varint_size;
        entry.key.internal_key_hash = vkd3d_shader_hash(&blob);

        internal->checksum = vkd3d_pipeline_blob_compute_data_checksum(internal->data, wrapped_varint_size);

        /* For duplicate, we won't insert. Just free the blob. */
        if (!d3d12_pipeline_library_insert_hash_map_blob_internal(pipeline_library,
                &pipeline_library->spirv_cache_map, &entry))
        {
            vkd3d_free(internal);
        }

        chunk->type = VKD3D_PIPELINE_BLOB_CHUNK_TYPE_VARINT_SPIRV_LINK | (stage << VKD3D_PIPELINE_BLOB_CHUNK_INDEX_SHIFT);
        chunk->size = sizeof(*link);

        link = CAST_CHUNK_DATA(chunk, link);
        link->hash = entry.key.internal_key_hash;
        chunk = finish_and_iterate_blob_chunk(chunk);

        /* Store meta information for SPIR-V. */
        chunk->type = VKD3D_PIPELINE_BLOB_CHUNK_TYPE_SHADER_META | (stage << VKD3D_PIPELINE_BLOB_CHUNK_INDEX_SHIFT);
        chunk->size = sizeof(*meta);
        meta = CAST_CHUNK_DATA(chunk, shader_meta);
        meta->meta = code->meta;
        chunk = finish_and_iterate_blob_chunk(chunk);
    }

    *inout_chunk = chunk;
}

static VkResult vkd3d_serialize_pipeline_state_inline(const struct d3d12_pipeline_state *state,
        struct vkd3d_pipeline_blob_chunk *chunk, size_t vk_pipeline_cache_size, const size_t *varint_size)
{
    const struct vkd3d_vk_device_procs *vk_procs = &state->device->vk_procs;
    size_t reference_size;
    unsigned int i;
    VkResult vr;

    if (state->vk_pso_cache)
    {
        /* Store PSO cache, or link to it if using pipeline cache. */
        chunk->type = VKD3D_PIPELINE_BLOB_CHUNK_TYPE_PIPELINE_CACHE;
        chunk->size = vk_pipeline_cache_size;
        reference_size = vk_pipeline_cache_size;
        /* In case driver leaves uninitialized memory for blob data. */
        memset(chunk->data, 0, vk_pipeline_cache_size);
        if ((vr = VK_CALL(vkGetPipelineCacheData(state->device->vk_device, state->vk_pso_cache,
                &reference_size, chunk->data))))
        {
            FIXME("Failed to serialize pipeline cache data, vr %d.\n", vr);
            return vr;
        }

        if (reference_size != vk_pipeline_cache_size)
        {
            FIXME("Mismatch in size for pipeline cache data %u != %u.\n",
                    (unsigned int)reference_size,
                    (unsigned int)vk_pipeline_cache_size);
        }

        chunk = finish_and_iterate_blob_chunk(chunk);
    }

    if (d3d12_pipeline_state_is_graphics(state))
    {
        for (i = 0; i < state->graphics.stage_count; i++)
        {
            vkd3d_shader_code_serialize_inline(&state->graphics.code[i], state->graphics.stages[i].stage,
                    varint_size[i], &chunk);
        }
    }
    else if (d3d12_pipeline_state_is_compute(state))
    {
        vkd3d_shader_code_serialize_inline(&state->compute.code, VK_SHADER_STAGE_COMPUTE_BIT,
                varint_size[0], &chunk);
    }

    return VK_SUCCESS;
}

static VkResult vkd3d_serialize_pipeline_state_referenced(struct d3d12_pipeline_library *pipeline_library,
        const struct d3d12_pipeline_state *state, struct vkd3d_pipeline_blob_chunk *chunk,
        size_t vk_pipeline_cache_size, const size_t *varint_size)
{
    const struct vkd3d_vk_device_procs *vk_procs = &state->device->vk_procs;
    struct vkd3d_pipeline_blob_internal *internal;
    struct vkd3d_pipeline_blob_chunk_link *link;
    struct vkd3d_cached_pipeline_entry entry;
    struct vkd3d_shader_code blob;
    size_t reference_size;
    unsigned int i;
    VkResult vr;

    entry.key.name_length = 0;
    entry.key.name = NULL;
    entry.data.is_new = 1;
    entry.data.state = NULL;

    if (state->vk_pso_cache)
    {
        entry.data.blob_length = sizeof(*internal) + vk_pipeline_cache_size;
        internal = vkd3d_malloc(entry.data.blob_length);
        entry.data.blob = internal;

        reference_size = vk_pipeline_cache_size;
        /* In case driver leaves uninitialized memory for blob data. */
        memset(internal->data, 0, vk_pipeline_cache_size);
        if ((vr = VK_CALL(vkGetPipelineCacheData(state->device->vk_device, state->vk_pso_cache,
                &reference_size, internal->data))))
        {
            FIXME("Failed to serialize pipeline cache data, vr %d.\n", vr);
            return vr;
        }

        if (reference_size != vk_pipeline_cache_size)
        {
            FIXME("Mismatch in size for pipeline cache data %u != %u.\n",
                    (unsigned int)reference_size,
                    (unsigned int)vk_pipeline_cache_size);
        }

        blob.code = internal->data;
        blob.size = vk_pipeline_cache_size;
        entry.key.internal_key_hash = vkd3d_shader_hash(&blob);
        internal->checksum = vkd3d_pipeline_blob_compute_data_checksum(blob.code, blob.size);

        /* For duplicate, we won't insert. Just free the blob. */
        if (!d3d12_pipeline_library_insert_hash_map_blob_internal(pipeline_library,
                &pipeline_library->driver_cache_map, &entry))
        {
            vkd3d_free(internal);
        }

        /* Store PSO cache, or link to it if using pipeline cache. */
        chunk->type = VKD3D_PIPELINE_BLOB_CHUNK_TYPE_PIPELINE_CACHE_LINK;
        chunk->size = sizeof(*link);
        link = CAST_CHUNK_DATA(chunk, link);
        link->hash = entry.key.internal_key_hash;

        chunk = finish_and_iterate_blob_chunk(chunk);
    }

    if (d3d12_pipeline_state_is_graphics(state))
    {
        for (i = 0; i < state->graphics.stage_count; i++)
        {
            vkd3d_shader_code_serialize_referenced(pipeline_library,
                    &state->graphics.code[i], state->graphics.stages[i].stage,
                    varint_size[i], &chunk);
        }
    }
    else if (d3d12_pipeline_state_is_compute(state))
    {
        vkd3d_shader_code_serialize_referenced(pipeline_library,
                &state->compute.code, VK_SHADER_STAGE_COMPUTE_BIT,
                varint_size[0], &chunk);
    }

    return VK_SUCCESS;
}

VkResult vkd3d_serialize_pipeline_state(struct d3d12_pipeline_library *pipeline_library,
        const struct d3d12_pipeline_state *state, size_t *size, void *data)
{
    const VkPhysicalDeviceProperties *device_properties = &state->device->device_info.properties2.properties;
    const struct vkd3d_vk_device_procs *vk_procs = &state->device->vk_procs;
    struct vkd3d_pipeline_blob_chunk_pso_compat *pso_compat;
    size_t varint_size[VKD3D_MAX_SHADER_STAGES];
    struct vkd3d_pipeline_blob *blob = data;
    struct vkd3d_pipeline_blob_chunk *chunk;
    size_t vk_blob_size_pipeline_cache = 0;
    size_t total_size = sizeof(*blob);
    size_t vk_blob_size = 0;
    bool need_blob_sizes;
    unsigned int i;
    VkResult vr;

    need_blob_sizes = !pipeline_library || data;

    /* PSO compatibility information is global to a PSO. */
    vk_blob_size += VKD3D_PIPELINE_BLOB_CHUNK_SIZE(pso_compat);

    if (state->vk_pso_cache)
    {
        if (need_blob_sizes)
        {
            if ((vr = VK_CALL(vkGetPipelineCacheData(state->device->vk_device, state->vk_pso_cache, &vk_blob_size_pipeline_cache, NULL))))
            {
                ERR("Failed to retrieve pipeline cache size, vr %d.\n", vr);
                return vr;
            }
        }

        if (pipeline_library)
            vk_blob_size += VKD3D_PIPELINE_BLOB_CHUNK_SIZE(link);
        else
            vk_blob_size += VKD3D_PIPELINE_BLOB_CHUNK_SIZE_RAW(vk_blob_size_pipeline_cache);
    }

    if (d3d12_pipeline_state_is_graphics(state))
    {
        for (i = 0; i < state->graphics.stage_count; i++)
        {
            vk_blob_size += vkd3d_shader_code_compute_serialized_size(&state->graphics.code[i],
                    need_blob_sizes ? &varint_size[i] : NULL, !pipeline_library);
        }
    }
    else if (d3d12_pipeline_state_is_compute(state))
    {
        vk_blob_size += vkd3d_shader_code_compute_serialized_size(&state->compute.code,
                need_blob_sizes ? &varint_size[0] : NULL, !pipeline_library);
    }

    total_size += vk_blob_size;

    if (blob && *size < total_size)
        return VK_INCOMPLETE;

    if (blob)
    {
        blob->version = VKD3D_CACHE_BLOB_VERSION;
        blob->vendor_id = device_properties->vendorID;
        blob->device_id = device_properties->deviceID;
        blob->vkd3d_shader_interface_key = state->device->shader_interface_key;
        blob->vkd3d_build = vkd3d_build;
        memcpy(blob->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE);
        chunk = CAST_CHUNK_BASE(blob);

        chunk->type = VKD3D_PIPELINE_BLOB_CHUNK_TYPE_PSO_COMPAT;
        chunk->size = sizeof(*pso_compat);
        pso_compat = CAST_CHUNK_DATA(chunk, pso_compat);
        pso_compat->compat = state->pipeline_cache_compat;
        chunk = finish_and_iterate_blob_chunk(chunk);

        if (pipeline_library)
        {
            vkd3d_serialize_pipeline_state_referenced(pipeline_library, state, chunk,
                    vk_blob_size_pipeline_cache, varint_size);
        }
        else
        {
            vkd3d_serialize_pipeline_state_inline(state, chunk,
                    vk_blob_size_pipeline_cache, varint_size);
        }

        blob->checksum = vkd3d_pipeline_blob_compute_data_checksum(blob->data, vk_blob_size);
    }

    *size = total_size;
    return VK_SUCCESS;
}

static uint32_t vkd3d_cached_pipeline_hash_name(const void *key)
{
    const struct vkd3d_cached_pipeline_key *k = key;
    uint32_t hash = 0;
    size_t i;

    for (i = 0; i < k->name_length; i += 4)
    {
        uint32_t accum = 0;
        memcpy(&accum, (const char*)k->name + i,
                min(k->name_length - i, sizeof(accum)));
        hash = hash_combine(hash, accum);
    }

    return hash;
}

static uint32_t vkd3d_cached_pipeline_hash_internal(const void *key)
{
    const struct vkd3d_cached_pipeline_key *k = key;
    return hash_uint64(k->internal_key_hash);
}

static bool vkd3d_cached_pipeline_compare_name(const void *key, const struct hash_map_entry *entry)
{
    const struct vkd3d_cached_pipeline_entry *e = (const struct vkd3d_cached_pipeline_entry*)entry;
    const struct vkd3d_cached_pipeline_key *k = key;

    return k->name_length == e->key.name_length &&
            !memcmp(k->name, e->key.name, k->name_length);
}

static bool vkd3d_cached_pipeline_compare_internal(const void *key, const struct hash_map_entry *entry)
{
    const struct vkd3d_cached_pipeline_entry *e = (const struct vkd3d_cached_pipeline_entry*)entry;
    const struct vkd3d_cached_pipeline_key *k = key;
    return k->internal_key_hash == e->key.internal_key_hash;
}

struct vkd3d_serialized_pipeline_toc_entry
{
    uint64_t blob_offset;
    uint32_t name_length;
    uint32_t blob_length;
};
STATIC_ASSERT(sizeof(struct vkd3d_serialized_pipeline_toc_entry) == 16);

#define VKD3D_PIPELINE_LIBRARY_VERSION MAKE_MAGIC('V','K','L',3)

struct vkd3d_serialized_pipeline_library
{
    uint32_t version;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t spirv_count;
    uint32_t driver_cache_count;
    uint32_t pipeline_count;
    uint64_t vkd3d_build;
    uint64_t vkd3d_shader_interface_key;
    uint8_t cache_uuid[VK_UUID_SIZE];
    struct vkd3d_serialized_pipeline_toc_entry entries[];
};
STATIC_ASSERT(sizeof(struct vkd3d_serialized_pipeline_library) == offsetof(struct vkd3d_serialized_pipeline_library, entries));
STATIC_ASSERT(sizeof(struct vkd3d_serialized_pipeline_library) == 40 + VK_UUID_SIZE);

/* Binary layout:
 * - Header
 * - spirv_count x toc_entries [Varint compressed SPIR-V]
 * - driver_cache_count x toc_entries [VkPipelineCache data]
 * - pipeline_count x toc_entries [Contains references to SPIR-V and VkPipelineCache blobs]
 * - After toc entries, raw data is placed. TOC entries refer to keys (names) and values by offsets into this buffer.
 * - TOC entry offsets for names are implicit. The name lengths are tightly packed from the start of the raw data buffer.
 *   Name lengths of 0 are treated as u64 hashes. Used for SPIR-V cache and VkPipelineCache cache.
 *   Name entries are allocated in toc_entry order.
 * - For blobs, a u64 offset + u32 size pair is added.
 * - After toc entries, we have the name table.
 */

/*
 * A raw blob is treated as a vkd3d_pipeline_blob or vkd3d_pipeline_blob_internal.
 * The full blob type is used for D3D12 PSOs. These contain:
 * - Versioning of various kinds. If there is a mismatch we return the appropriate error.
 * - Checksum is used as sanity check in case we have a corrupt archive.
 * - Chunked data[].
 * - This chunked data is a typical stream of { type, length, data }. A D3D12 PSO stores various information here, such as
 *   - Root signature compatibility
 *   - SPIR-V shader hash references per stage
 *   - SPIR-V shader meta information, which is reflection data that we would otherwise get from vkd3d-shader
 *   - Hash of the VkPipelineCache data
 */

/*
 * An internal blob is just checksum + data.
 * This data does not need versioning information since it's fully internal to the library implementation and is only
 * referenced after the D3D12 blob header is validated.
 */

/* Rationale for this split format is:
 * - It is implied that the pipeline library can be used directly from an mmap-ed on-disk file,
 *   since users cannot free the pointer to library once created.
 *   In this situation, we should scan through just the TOC to begin with to avoid page faulting on potentially 100s of MBs.
 *   It is also more cache friendly this way.
 * - Having a more split TOC structure like this makes it easier to add SPIR-V deduplication and PSO cache deduplication.
 */

/* ID3D12PipelineLibrary */
static inline struct d3d12_pipeline_library *impl_from_ID3D12PipelineLibrary(d3d12_pipeline_library_iface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d12_pipeline_library, ID3D12PipelineLibrary_iface);
}

static void d3d12_pipeline_library_serialize_entry(
        const struct vkd3d_cached_pipeline_entry *entry,
        struct vkd3d_serialized_pipeline_toc_entry *header,
        uint8_t *data, size_t name_offset, size_t blob_offset)
{
    header->blob_offset = blob_offset;
    header->name_length = entry->key.name_length;
    header->blob_length = entry->data.blob_length;

    if (entry->key.name_length)
        memcpy(data + name_offset, entry->key.name, entry->key.name_length);
    else
        memcpy(data + name_offset, &entry->key.internal_key_hash, sizeof(entry->key.internal_key_hash));

    memcpy(data + blob_offset, entry->data.blob, entry->data.blob_length);
}

static void d3d12_pipeline_library_cleanup_map(struct hash_map *map)
{
    size_t i;

    for (i = 0; i < map->entry_count; i++)
    {
        struct vkd3d_cached_pipeline_entry *e = (struct vkd3d_cached_pipeline_entry*)hash_map_get_entry(map, i);

        if (e->entry.flags & HASH_MAP_ENTRY_OCCUPIED)
        {
            if (e->data.is_new)
            {
                vkd3d_free((void*)e->key.name);
                vkd3d_free((void*)e->data.blob);
            }

            if (e->data.state)
                d3d12_pipeline_state_dec_ref(e->data.state);
        }
    }

    hash_map_clear(map);
}

static void d3d12_pipeline_library_cleanup(struct d3d12_pipeline_library *pipeline_library, struct d3d12_device *device)
{
    d3d12_pipeline_library_cleanup_map(&pipeline_library->pso_map);
    d3d12_pipeline_library_cleanup_map(&pipeline_library->driver_cache_map);
    d3d12_pipeline_library_cleanup_map(&pipeline_library->spirv_cache_map);

    vkd3d_private_store_destroy(&pipeline_library->private_store);
    rwlock_destroy(&pipeline_library->mutex);
    rwlock_destroy(&pipeline_library->internal_hashmap_mutex);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_QueryInterface(d3d12_pipeline_library_iface *iface,
        REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D12PipelineLibrary)
            || IsEqualGUID(riid, &IID_ID3D12PipelineLibrary1)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12PipelineLibrary_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

void d3d12_pipeline_library_inc_ref(struct d3d12_pipeline_library *pipeline_library)
{
    InterlockedIncrement(&pipeline_library->internal_refcount);
}

void d3d12_pipeline_library_dec_ref(struct d3d12_pipeline_library *pipeline_library)
{
    ULONG refcount = InterlockedDecrement(&pipeline_library->internal_refcount);
    if (!refcount)
    {
        d3d12_pipeline_library_cleanup(pipeline_library, pipeline_library->device);
        vkd3d_free(pipeline_library);
    }
}

ULONG d3d12_pipeline_library_inc_public_ref(struct d3d12_pipeline_library *pipeline_library)
{
    ULONG refcount = InterlockedIncrement(&pipeline_library->refcount);
    if (refcount == 1)
        d3d12_device_add_ref(pipeline_library->device);
    TRACE("%p increasing refcount to %u.\n", pipeline_library, refcount);
    return refcount;
}

ULONG d3d12_pipeline_library_dec_public_ref(struct d3d12_pipeline_library *pipeline_library)
{
    struct d3d12_device *device = pipeline_library->device;
    ULONG refcount = InterlockedDecrement(&pipeline_library->refcount);
    TRACE("%p decreasing refcount to %u.\n", pipeline_library, refcount);
    if (!refcount)
    {
        d3d12_pipeline_library_dec_ref(pipeline_library);
        d3d12_device_release(device);
    }
    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d12_pipeline_library_AddRef(d3d12_pipeline_library_iface *iface)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    return d3d12_pipeline_library_inc_public_ref(pipeline_library);
}

static ULONG STDMETHODCALLTYPE d3d12_pipeline_library_Release(d3d12_pipeline_library_iface *iface)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    return d3d12_pipeline_library_dec_public_ref(pipeline_library);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_GetPrivateData(d3d12_pipeline_library_iface *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_get_private_data(&pipeline_library->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_SetPrivateData(d3d12_pipeline_library_iface *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return vkd3d_set_private_data(&pipeline_library->private_store, guid, data_size, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_SetPrivateDataInterface(d3d12_pipeline_library_iface *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return vkd3d_set_private_data_interface(&pipeline_library->private_store, guid, data,
            NULL, NULL);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_GetDevice(d3d12_pipeline_library_iface *iface,
        REFIID iid, void **device)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, iid %s, device %p.\n", iface, debugstr_guid(iid), device);

    return d3d12_device_query_interface(pipeline_library->device, iid, device);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_StorePipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, ID3D12PipelineState *pipeline)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    struct d3d12_pipeline_state *pipeline_state = impl_from_ID3D12PipelineState(pipeline);
    struct vkd3d_cached_pipeline_entry entry;
    void *new_name, *new_blob;
    VkResult vr;
    HRESULT hr;
    int rc;

    TRACE("iface %p, name %s, pipeline %p.\n", iface, debugstr_w(name), pipeline);

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
        INFO("Serializing pipeline to library.\n");

    if ((rc = rwlock_lock_read(&pipeline_library->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return hresult_from_errno(rc);
    }

    entry.key.name_length = vkd3d_wcslen(name) * sizeof(WCHAR);
    entry.key.name = name;
    entry.key.internal_key_hash = 0;

    if (hash_map_find(&pipeline_library->pso_map, &entry.key))
    {
        WARN("Pipeline %s already exists.\n", debugstr_w(name));
        rwlock_unlock_read(&pipeline_library->mutex);
        return E_INVALIDARG;
    }

    /* We need to allocate persistent storage for the name */
    if (!(new_name = malloc(entry.key.name_length)))
    {
        rwlock_unlock_read(&pipeline_library->mutex);
        return E_OUTOFMEMORY;
    }

    memcpy(new_name, name, entry.key.name_length);
    entry.key.name = new_name;

    if (FAILED(vr = vkd3d_serialize_pipeline_state(pipeline_library, pipeline_state, &entry.data.blob_length, NULL)))
    {
        vkd3d_free(new_name);
        rwlock_unlock_read(&pipeline_library->mutex);
        return hresult_from_vk_result(vr);
    }

    if (!(new_blob = malloc(entry.data.blob_length)))
    {
        vkd3d_free(new_name);
        rwlock_unlock_read(&pipeline_library->mutex);
        return E_OUTOFMEMORY;
    }

    if (FAILED(vr = vkd3d_serialize_pipeline_state(pipeline_library, pipeline_state, &entry.data.blob_length, new_blob)))
    {
        vkd3d_free(new_name);
        vkd3d_free(new_blob);
        rwlock_unlock_read(&pipeline_library->mutex);
        return hresult_from_vk_result(vr);
    }

    rwlock_unlock_read(&pipeline_library->mutex);

    entry.data.blob = new_blob;
    entry.data.is_new = 1;
    entry.data.state = pipeline_state;

    /* Now is the time to promote to a writer lock. */
    if ((rc = rwlock_lock_write(&pipeline_library->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        vkd3d_free(new_name);
        vkd3d_free(new_blob);
        return hresult_from_errno(rc);
    }

    /* Detected duplicate late, but be accurate in how we report this. */
    if (hash_map_find(&pipeline_library->pso_map, &entry.key))
    {
        WARN("Pipeline %s already exists.\n", debugstr_w(name));
        hr = E_INVALIDARG;
    }
    else if (!d3d12_pipeline_library_insert_hash_map_blob_locked(pipeline_library, &pipeline_library->pso_map, &entry))
    {
        /* This path shouldn't happen unless there are OOM scenarios. */
        hr = E_OUTOFMEMORY;
    }
    else
    {
        /* If we get a subsequent LoadLibrary, we have to hand it back out again.
         * API tests inform us that we need internal ref-count here. */
        d3d12_pipeline_state_inc_ref(pipeline_state);
        hr = S_OK;
    }

    rwlock_unlock_write(&pipeline_library->mutex);

    if (FAILED(hr))
    {
        vkd3d_free(new_name);
        vkd3d_free(new_blob);
    }

    return hr;
}

static HRESULT d3d12_pipeline_library_load_pipeline(struct d3d12_pipeline_library *pipeline_library, LPCWSTR name,
        VkPipelineBindPoint bind_point, struct d3d12_pipeline_state_desc *desc, struct d3d12_pipeline_state **state)
{
    struct vkd3d_pipeline_cache_compatibility pipeline_cache_compat;
    const struct vkd3d_cached_pipeline_entry *e;
    struct d3d12_pipeline_state *existing_state;
    struct d3d12_root_signature *root_signature;
    struct d3d12_pipeline_state *cached_state;
    struct vkd3d_cached_pipeline_key key;
    HRESULT hr;
    int rc;

    if ((rc = rwlock_lock_read(&pipeline_library->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return hresult_from_errno(rc);
    }

    key.name_length = vkd3d_wcslen(name) * sizeof(WCHAR);
    key.name = name;

    if (!(e = (const struct vkd3d_cached_pipeline_entry*)hash_map_find(&pipeline_library->pso_map, &key)))
    {
        WARN("Pipeline %s does not exist.\n", debugstr_w(name));
        rwlock_unlock_read(&pipeline_library->mutex);
        return E_INVALIDARG;
    }

    /* Docs say that applications have to consider thread safety here:
     * https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device1-createpipelinelibrary#thread-safety.
     * However, it seems questionable to rely on that, so just do cmpxchg replacements. */
    cached_state = vkd3d_atomic_ptr_load_explicit(&e->data.state, vkd3d_memory_order_acquire);

    if (cached_state)
    {
        rwlock_unlock_read(&pipeline_library->mutex);

        /* If we have handed out the PSO once, just need to do a quick validation. */
        memset(&pipeline_cache_compat, 0, sizeof(pipeline_cache_compat));
        vkd3d_pipeline_cache_compat_from_state_desc(&pipeline_cache_compat, desc);

        if (desc->root_signature)
        {
            root_signature = impl_from_ID3D12RootSignature(desc->root_signature);
            if (root_signature)
                pipeline_cache_compat.root_signature_compat_hash = root_signature->compatibility_hash;
        }
        else if (cached_state->root_signature_compat_hash_is_dxbc_derived)
        {
            /* If we have no explicit root signature and the existing PSO didn't either,
             * just inherit the compat hash from PSO to avoid comparing them.
             * The hash depends entirely on the DXBC blob either way. */
            pipeline_cache_compat.root_signature_compat_hash = cached_state->pipeline_cache_compat.root_signature_compat_hash;
        }

        if (memcmp(&pipeline_cache_compat, &cached_state->pipeline_cache_compat, sizeof(pipeline_cache_compat)) != 0)
        {
            if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
                INFO("Attempt to load existing PSO from library, but failed argument validation.\n");
            return E_INVALIDARG;
        }

        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("Handing out existing pipeline state object.\n");

        *state = cached_state;
        d3d12_pipeline_state_inc_public_ref(cached_state);
        return S_OK;
    }
    else
    {
        desc->cached_pso.blob.CachedBlobSizeInBytes = e->data.blob_length;
        desc->cached_pso.blob.pCachedBlob = e->data.blob;
        desc->cached_pso.library = pipeline_library;
        rwlock_unlock_read(&pipeline_library->mutex);

        /* Don't hold locks while creating pipeline, it takes *some* time to validate and decompress stuff,
         * and in heavily multi-threaded scenarios we want to go as wide as we can. */
        if (FAILED(hr = d3d12_pipeline_state_create(pipeline_library->device, bind_point, desc, &cached_state)))
            return hr;

        /* These really should not fail ... */
        rwlock_lock_read(&pipeline_library->mutex);
        e = (const struct vkd3d_cached_pipeline_entry*)hash_map_find(&pipeline_library->pso_map, &key);
        existing_state = vkd3d_atomic_ptr_compare_exchange(&e->data.state, NULL, cached_state,
                vkd3d_memory_order_acq_rel, vkd3d_memory_order_acquire);
        rwlock_unlock_read(&pipeline_library->mutex);

        if (!existing_state)
        {
            /* Successfully replaced. */
            d3d12_pipeline_state_inc_ref(cached_state);
            *state = cached_state;
        }
        else
        {
            /* Other thread ended up winning while we were creating the PSO.
             * This shouldn't be legal D3D12 API usage according to docs, but be safe ... */
            WARN("Race condition detected.\n");
            d3d12_pipeline_state_dec_ref(cached_state);
            d3d12_pipeline_state_inc_public_ref(existing_state);
            *state = existing_state;
        }
        return S_OK;
    }
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_LoadGraphicsPipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc, REFIID iid, void **pipeline_state)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    struct d3d12_pipeline_state_desc pipeline_desc;
    struct d3d12_pipeline_state *object;
    HRESULT hr;

    TRACE("iface %p, name %s, desc %p, iid %s, pipeline_state %p.\n", iface,
            debugstr_w(name), desc, debugstr_guid(iid), pipeline_state);

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
        INFO("Attempting LoadGraphicsPipeline.\n");

    if (FAILED(hr = vkd3d_pipeline_state_desc_from_d3d12_graphics_desc(&pipeline_desc, desc)))
        return hr;

    if (FAILED(hr = d3d12_pipeline_library_load_pipeline(pipeline_library,
            name, VK_PIPELINE_BIND_POINT_GRAPHICS, &pipeline_desc, &object)))
        return hr;

    return return_interface(&object->ID3D12PipelineState_iface,
            &IID_ID3D12PipelineState, iid, pipeline_state);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_LoadComputePipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, REFIID iid, void **pipeline_state)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    struct d3d12_pipeline_state_desc pipeline_desc;
    struct d3d12_pipeline_state *object;
    HRESULT hr;

    TRACE("iface %p, name %s, desc %p, iid %s, pipeline_state %p.\n", iface,
            debugstr_w(name), desc, debugstr_guid(iid), pipeline_state);

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
        INFO("Attempting LoadComputePipeline.\n");

    if (FAILED(hr = vkd3d_pipeline_state_desc_from_d3d12_compute_desc(&pipeline_desc, desc)))
        return hr;

    if (FAILED(hr = d3d12_pipeline_library_load_pipeline(pipeline_library,
            name, VK_PIPELINE_BIND_POINT_COMPUTE, &pipeline_desc, &object)))
        return hr;

    return return_interface(&object->ID3D12PipelineState_iface,
            &IID_ID3D12PipelineState, iid, pipeline_state);
}

static size_t d3d12_pipeline_library_get_aligned_name_table_size(struct d3d12_pipeline_library *pipeline_library)
{
    return align(pipeline_library->total_name_table_size, VKD3D_PIPELINE_BLOB_ALIGN);
}

static size_t d3d12_pipeline_library_get_serialized_size(struct d3d12_pipeline_library *pipeline_library)
{
    size_t total_size = 0;

    total_size += sizeof(struct vkd3d_serialized_pipeline_library);
    total_size += sizeof(struct vkd3d_serialized_pipeline_toc_entry) * pipeline_library->pso_map.used_count;
    total_size += sizeof(struct vkd3d_serialized_pipeline_toc_entry) * pipeline_library->spirv_cache_map.used_count;
    total_size += sizeof(struct vkd3d_serialized_pipeline_toc_entry) * pipeline_library->driver_cache_map.used_count;
    total_size += d3d12_pipeline_library_get_aligned_name_table_size(pipeline_library);
    total_size += pipeline_library->total_blob_size;

    return total_size;
}

static SIZE_T STDMETHODCALLTYPE d3d12_pipeline_library_GetSerializedSize(d3d12_pipeline_library_iface *iface)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    size_t total_size;
    int rc;

    TRACE("iface %p.\n", iface);

    if ((rc = rwlock_lock_read(&pipeline_library->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return 0;
    }

    if ((rc = rwlock_lock_read(&pipeline_library->internal_hashmap_mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        rwlock_unlock_read(&pipeline_library->mutex);
        return 0;
    }

    total_size = d3d12_pipeline_library_get_serialized_size(pipeline_library);

    rwlock_unlock_read(&pipeline_library->mutex);
    rwlock_unlock_read(&pipeline_library->internal_hashmap_mutex);
    return total_size;
}

static void d3d12_pipeline_library_serialize_hash_map(const struct hash_map *map,
        struct vkd3d_serialized_pipeline_toc_entry **inout_toc_entries, uint8_t *serialized_data,
        size_t *inout_name_offset, size_t *inout_blob_offset)
{
    struct vkd3d_serialized_pipeline_toc_entry *toc_entries = *inout_toc_entries;
    size_t name_offset = *inout_name_offset;
    size_t blob_offset = *inout_blob_offset;
    uint32_t i;

    for (i = 0; i < map->entry_count; i++)
    {
        struct vkd3d_cached_pipeline_entry *e = (struct vkd3d_cached_pipeline_entry*)hash_map_get_entry(map, i);

        if (e->entry.flags & HASH_MAP_ENTRY_OCCUPIED)
        {
            d3d12_pipeline_library_serialize_entry(e, toc_entries, serialized_data, name_offset, blob_offset);
            toc_entries++;
            name_offset += e->key.name_length ? e->key.name_length : sizeof(e->key.internal_key_hash);
            blob_offset += align(e->data.blob_length, VKD3D_PIPELINE_BLOB_ALIGN);
        }
    }

    *inout_toc_entries = toc_entries;
    *inout_name_offset = name_offset;
    *inout_blob_offset = blob_offset;
}

static HRESULT d3d12_pipeline_library_serialize(struct d3d12_pipeline_library *pipeline_library,
        void *data, size_t data_size)
{
    const VkPhysicalDeviceProperties *device_properties = &pipeline_library->device->device_info.properties2.properties;
    struct vkd3d_serialized_pipeline_library *header = data;
    struct vkd3d_serialized_pipeline_toc_entry *toc_entries;
    uint64_t driver_cache_size;
    uint8_t *serialized_data;
    size_t total_toc_entries;
    size_t required_size;
    uint64_t spirv_size;
    size_t name_offset;
    size_t blob_offset;
    uint64_t pso_size;

    required_size = d3d12_pipeline_library_get_serialized_size(pipeline_library);
    if (data_size < required_size)
        return E_INVALIDARG;

    header->version = VKD3D_PIPELINE_LIBRARY_VERSION;
    header->vendor_id = device_properties->vendorID;
    header->device_id = device_properties->deviceID;
    header->pipeline_count = pipeline_library->pso_map.used_count;
    header->spirv_count = pipeline_library->spirv_cache_map.used_count;
    header->driver_cache_count = pipeline_library->driver_cache_map.used_count;
    header->vkd3d_build = vkd3d_build;
    header->vkd3d_shader_interface_key = pipeline_library->device->shader_interface_key;
    memcpy(header->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE);

    total_toc_entries = header->pipeline_count + header->spirv_count + header->driver_cache_count;

    toc_entries = header->entries;
    serialized_data = (uint8_t *)&toc_entries[total_toc_entries];
    name_offset = 0;
    blob_offset = d3d12_pipeline_library_get_aligned_name_table_size(pipeline_library);

    spirv_size = blob_offset;
    d3d12_pipeline_library_serialize_hash_map(&pipeline_library->spirv_cache_map, &toc_entries,
            serialized_data, &name_offset, &blob_offset);
    spirv_size = blob_offset - spirv_size;

    driver_cache_size = blob_offset;
    d3d12_pipeline_library_serialize_hash_map(&pipeline_library->driver_cache_map, &toc_entries,
            serialized_data, &name_offset, &blob_offset);
    driver_cache_size = blob_offset - driver_cache_size;

    pso_size = blob_offset;
    d3d12_pipeline_library_serialize_hash_map(&pipeline_library->pso_map, &toc_entries,
            serialized_data, &name_offset, &blob_offset);
    pso_size = blob_offset - pso_size;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
    {
        INFO("Serializing pipeline library (%"PRIu64" bytes):\n"
            "  TOC overhead: %"PRIu64" bytes\n"
            "  Name table overhead: %"PRIu64" bytes\n"
            "  D3D12 PSO count: %u (%"PRIu64" bytes)\n"
            "  Unique SPIR-V count: %u (%"PRIu64" bytes)\n"
            "  Unique VkPipelineCache count: %u (%"PRIu64" bytes)\n",
            (uint64_t)data_size,
            (uint64_t)(serialized_data - (const uint8_t*)data),
            (uint64_t)name_offset,
            header->pipeline_count, pso_size,
            header->spirv_count, spirv_size,
            header->driver_cache_count, driver_cache_size);
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_Serialize(d3d12_pipeline_library_iface *iface,
        void *data, SIZE_T data_size)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    HRESULT hr;
    int rc;

    TRACE("iface %p.\n", iface);

    if ((rc = rwlock_lock_read(&pipeline_library->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return E_FAIL;
    }

    if ((rc = rwlock_lock_read(&pipeline_library->internal_hashmap_mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        rwlock_unlock_read(&pipeline_library->mutex);
        return E_FAIL;
    }

    hr = d3d12_pipeline_library_serialize(pipeline_library, data, data_size);
    rwlock_unlock_read(&pipeline_library->mutex);
    rwlock_unlock_read(&pipeline_library->internal_hashmap_mutex);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_LoadPipeline(d3d12_pipeline_library_iface *iface,
        LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC *desc, REFIID iid, void **pipeline_state)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);
    struct d3d12_pipeline_state_desc pipeline_desc;
    struct d3d12_pipeline_state *object;
    VkPipelineBindPoint pipeline_type;
    HRESULT hr;

    TRACE("iface %p, name %s, desc %p, iid %s, pipeline_state %p.\n", iface,
            debugstr_w(name), desc, debugstr_guid(iid), pipeline_state);

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
        INFO("Attempting LoadPipeline.\n");

    if (FAILED(hr = vkd3d_pipeline_state_desc_from_d3d12_stream_desc(&pipeline_desc, desc, &pipeline_type)))
        return hr;

    if (FAILED(hr = d3d12_pipeline_library_load_pipeline(pipeline_library,
            name, pipeline_type, &pipeline_desc, &object)))
        return hr;

    return return_interface(&object->ID3D12PipelineState_iface,
            &IID_ID3D12PipelineState, iid, pipeline_state);
}

static CONST_VTBL struct ID3D12PipelineLibrary1Vtbl d3d12_pipeline_library_vtbl =
{
    /* IUnknown methods */
    d3d12_pipeline_library_QueryInterface,
    d3d12_pipeline_library_AddRef,
    d3d12_pipeline_library_Release,
    /* ID3D12Object methods */
    d3d12_pipeline_library_GetPrivateData,
    d3d12_pipeline_library_SetPrivateData,
    d3d12_pipeline_library_SetPrivateDataInterface,
    (void *)d3d12_object_SetName,
    /* ID3D12DeviceChild methods */
    d3d12_pipeline_library_GetDevice,
    /* ID3D12PipelineLibrary methods */
    d3d12_pipeline_library_StorePipeline,
    d3d12_pipeline_library_LoadGraphicsPipeline,
    d3d12_pipeline_library_LoadComputePipeline,
    d3d12_pipeline_library_GetSerializedSize,
    d3d12_pipeline_library_Serialize,
    /* ID3D12PipelineLibrary1 methods */
    d3d12_pipeline_library_LoadPipeline,
};

static HRESULT d3d12_pipeline_library_unserialize_hash_map(
        struct d3d12_pipeline_library *pipeline_library,
        const struct vkd3d_serialized_pipeline_toc_entry *entries,
        size_t entries_count, struct hash_map *map,
        const uint8_t *serialized_data_base, size_t serialized_data_size,
        const uint8_t **inout_name_table)
{
    const uint8_t *name_table = *inout_name_table;
    uint32_t i;

    /* The application is not allowed to free the blob, so we
     * can safely use pointers without copying the data first. */
    for (i = 0; i < entries_count; i++)
    {
        const struct vkd3d_serialized_pipeline_toc_entry *toc_entry = &entries[i];
        struct vkd3d_cached_pipeline_entry entry;

        entry.key.name_length = toc_entry->name_length;

        if (entry.key.name_length)
        {
            entry.key.name = name_table;
            entry.key.internal_key_hash = 0;
            /* Verify that name table entry does not overflow. */
            if (name_table + entry.key.name_length > serialized_data_base + serialized_data_size)
                return E_INVALIDARG;
            name_table += entry.key.name_length;
        }
        else
        {
            entry.key.name = NULL;
            entry.key.internal_key_hash = 0;
            /* Verify that name table entry does not overflow. */
            if (name_table + sizeof(entry.key.internal_key_hash) > serialized_data_base + serialized_data_size)
                return E_INVALIDARG;
            memcpy(&entry.key.internal_key_hash, name_table, sizeof(entry.key.internal_key_hash));
            name_table += sizeof(entry.key.internal_key_hash);
        }

        /* Verify that blob entry does not overflow. */
        if (toc_entry->blob_offset + toc_entry->blob_length > serialized_data_size)
            return E_INVALIDARG;

        entry.data.blob_length = toc_entry->blob_length;
        entry.data.blob = serialized_data_base + toc_entry->blob_offset;
        entry.data.is_new = 0;
        entry.data.state = NULL;

        if (!d3d12_pipeline_library_insert_hash_map_blob_locked(pipeline_library, map, &entry))
            return E_OUTOFMEMORY;
    }

    *inout_name_table = name_table;
    return S_OK;
}

static HRESULT d3d12_pipeline_library_read_blob(struct d3d12_pipeline_library *pipeline_library,
        struct d3d12_device *device, const void *blob, size_t blob_length)
{
    const VkPhysicalDeviceProperties *device_properties = &device->device_info.properties2.properties;
    const struct vkd3d_serialized_pipeline_library *header = blob;
    const uint8_t *serialized_data_base;
    size_t serialized_data_size;
    const uint8_t *name_table;
    size_t header_entry_size;
    size_t total_toc_entries;
    uint32_t i;
    HRESULT hr;

    /* Same logic as for pipeline blobs, indicate that the app needs
     * to rebuild the pipeline library in case vkd3d itself or the
     * underlying device/driver changed */
    if (blob_length < sizeof(*header) || header->version != VKD3D_PIPELINE_LIBRARY_VERSION)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("Rejecting pipeline library due to invalid header version.\n");
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
    }

    if (header->device_id != device_properties->deviceID || header->vendor_id != device_properties->vendorID)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("Rejecting pipeline library due to vendorID/deviceID mismatch.\n");
        return D3D12_ERROR_ADAPTER_NOT_FOUND;
    }

    if (header->vkd3d_build != vkd3d_build)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("Rejecting pipeline library due to vkd3d-proton build mismatch.\n");
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
    }

    if (header->vkd3d_shader_interface_key != device->shader_interface_key)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("Rejecting pipeline library due to vkd3d-proton shader interface key mismatch.\n");
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
    }

    if (memcmp(header->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE) != 0)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("Rejecting pipeline library due to pipelineCacheUUID mismatch.\n");
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
    }

    total_toc_entries = header->pipeline_count + header->spirv_count + header->driver_cache_count;

    header_entry_size = offsetof(struct vkd3d_serialized_pipeline_library, entries) +
            total_toc_entries * sizeof(struct vkd3d_serialized_pipeline_toc_entry);

    if (blob_length < header_entry_size)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("Rejecting pipeline library due to too small blob length compared to expected size.\n");
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
    }

    serialized_data_size = blob_length - header_entry_size;
    serialized_data_base = (const uint8_t *)&header->entries[total_toc_entries];
    name_table = serialized_data_base;

    i = 0;

    if (FAILED(hr = d3d12_pipeline_library_unserialize_hash_map(pipeline_library,
            &header->entries[i], header->spirv_count,
            &pipeline_library->spirv_cache_map, serialized_data_base, serialized_data_size,
            &name_table)))
        return hr;
    i += header->spirv_count;

    if (FAILED(hr = d3d12_pipeline_library_unserialize_hash_map(pipeline_library,
            &header->entries[i], header->driver_cache_count,
            &pipeline_library->driver_cache_map, serialized_data_base, serialized_data_size,
            &name_table)))
        return hr;
    i += header->driver_cache_count;

    if (FAILED(hr = d3d12_pipeline_library_unserialize_hash_map(pipeline_library,
            &header->entries[i], header->pipeline_count,
            &pipeline_library->pso_map, serialized_data_base, serialized_data_size,
            &name_table)))
        return hr;
    i += header->pipeline_count;

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
    {
        INFO("Loading pipeline library (%"PRIu64" bytes):\n"
             "  D3D12 PSO count: %u\n"
             "  Unique SPIR-V count: %u\n"
             "  Unique VkPipelineCache count: %u\n",
                (uint64_t)blob_length,
                header->pipeline_count,
                header->spirv_count,
                header->driver_cache_count);
    }

    return S_OK;
}

static HRESULT d3d12_pipeline_library_init(struct d3d12_pipeline_library *pipeline_library,
        struct d3d12_device *device, const void *blob, size_t blob_length)
{
    HRESULT hr;
    int rc;

    memset(pipeline_library, 0, sizeof(*pipeline_library));
    pipeline_library->ID3D12PipelineLibrary_iface.lpVtbl = &d3d12_pipeline_library_vtbl;
    pipeline_library->refcount = 1;
    pipeline_library->internal_refcount = 1;

    if (!blob_length && blob)
        return E_INVALIDARG;

    if ((rc = rwlock_init(&pipeline_library->mutex)))
        return hresult_from_errno(rc);

    if ((rc = rwlock_init(&pipeline_library->internal_hashmap_mutex)))
    {
        rwlock_destroy(&pipeline_library->mutex);
        return hresult_from_errno(rc);
    }

    hash_map_init(&pipeline_library->spirv_cache_map, vkd3d_cached_pipeline_hash_internal,
            vkd3d_cached_pipeline_compare_internal, sizeof(struct vkd3d_cached_pipeline_entry));
    hash_map_init(&pipeline_library->driver_cache_map, vkd3d_cached_pipeline_hash_internal,
            vkd3d_cached_pipeline_compare_internal, sizeof(struct vkd3d_cached_pipeline_entry));
    hash_map_init(&pipeline_library->pso_map, vkd3d_cached_pipeline_hash_name,
            vkd3d_cached_pipeline_compare_name, sizeof(struct vkd3d_cached_pipeline_entry));

    if (blob_length)
    {
        hr = d3d12_pipeline_library_read_blob(pipeline_library, device, blob, blob_length);

        if ((vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_IGNORE_MISMATCH_DRIVER) &&
                (hr == D3D12_ERROR_ADAPTER_NOT_FOUND || hr == D3D12_ERROR_DRIVER_VERSION_MISMATCH))
        {
            /* Sigh ... Otherwise, certain games might never create a replacement
             * pipeline library and never serialize out pipeline libraries. */
            INFO("Application provided a pipeline library which does not match with what we expect.\n"
                 "Creating an empty pipeline library instead as a workaround.\n");
            hr = S_OK;
        }

        if (FAILED(hr))
            goto cleanup_hash_map;
    }
    else if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
        INFO("Creating empty pipeline library.\n");

    if (FAILED(hr = vkd3d_private_store_init(&pipeline_library->private_store)))
        goto cleanup_mutex;

    d3d12_device_add_ref(pipeline_library->device = device);
    return hr;

cleanup_hash_map:
    hash_map_clear(&pipeline_library->pso_map);
    hash_map_clear(&pipeline_library->spirv_cache_map);
    hash_map_clear(&pipeline_library->driver_cache_map);
cleanup_mutex:
    rwlock_destroy(&pipeline_library->mutex);
    return hr;
}

HRESULT d3d12_pipeline_library_create(struct d3d12_device *device, const void *blob,
        size_t blob_length, struct d3d12_pipeline_library **pipeline_library)
{
    struct d3d12_pipeline_library *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_pipeline_library_init(object, device, blob, blob_length)))
    {
        vkd3d_free(object);
        return hr;
    }

    TRACE("Created pipeline library %p.\n", object);

    *pipeline_library = object;
    return S_OK;
}

void vkd3d_pipeline_cache_compat_from_state_desc(struct vkd3d_pipeline_cache_compatibility *compat,
        const struct d3d12_pipeline_state_desc *desc)
{
    const D3D12_SHADER_BYTECODE *code_list[] = {
        &desc->vs,
        &desc->hs,
        &desc->ds,
        &desc->gs,
        &desc->ps,
        &desc->cs,
    };
    unsigned int output_index = 0;
    uint64_t state_hash;
    unsigned int i;

    state_hash = hash_fnv1_init();

    /* Combined, all this information should serve as a unique key for a PSO.
     * TODO: Use this to look up cached PSOs in on-disk caches. */
#define H8(v) state_hash = hash_fnv1_iterate_u8(state_hash, v)
#define H32(v) state_hash = hash_fnv1_iterate_u32(state_hash, v)
#define HF32(v) state_hash = hash_fnv1_iterate_f32(state_hash, v)
#define HS(v) state_hash = hash_fnv1_iterate_string(state_hash, v)
    if (!desc->cs.BytecodeLength)
    {
        H32(desc->stream_output.RasterizedStream);
        H32(desc->stream_output.NumEntries);
        H32(desc->stream_output.NumStrides);
        for (i = 0; i < desc->stream_output.NumStrides; i++)
            H32(desc->stream_output.pBufferStrides[i]);
        for (i = 0; i < desc->stream_output.NumEntries; i++)
        {
            H32(desc->stream_output.pSODeclaration[i].ComponentCount);
            H32(desc->stream_output.pSODeclaration[i].OutputSlot);
            H32(desc->stream_output.pSODeclaration[i].SemanticIndex);
            H32(desc->stream_output.pSODeclaration[i].StartComponent);
            H32(desc->stream_output.pSODeclaration[i].Stream);
            HS(desc->stream_output.pSODeclaration[i].SemanticName);
        }
        H32(desc->blend_state.IndependentBlendEnable);
        H32(desc->blend_state.AlphaToCoverageEnable);

        /* Per-RT state */
        H32(desc->rtv_formats.NumRenderTargets);
        for (i = 0; i < desc->rtv_formats.NumRenderTargets; i++)
        {
            H32(desc->blend_state.RenderTarget[i].RenderTargetWriteMask);
            H32(desc->blend_state.RenderTarget[i].BlendEnable);
            H32(desc->blend_state.RenderTarget[i].LogicOpEnable);
            H32(desc->blend_state.RenderTarget[i].DestBlend);
            H32(desc->blend_state.RenderTarget[i].DestBlendAlpha);
            H32(desc->blend_state.RenderTarget[i].SrcBlend);
            H32(desc->blend_state.RenderTarget[i].SrcBlendAlpha);
            H32(desc->blend_state.RenderTarget[i].BlendOp);
            H32(desc->blend_state.RenderTarget[i].BlendOpAlpha);
            H32(desc->blend_state.RenderTarget[i].LogicOpEnable);
            H32(desc->blend_state.RenderTarget[i].LogicOp);
            H32(desc->rtv_formats.RTFormats[i]);
        }

        H32(desc->sample_mask);

        /* Raster state */
        H32(desc->rasterizer_state.FillMode);
        H32(desc->rasterizer_state.CullMode);
        H32(desc->rasterizer_state.FrontCounterClockwise);
        H32(desc->rasterizer_state.DepthBias);
        HF32(desc->rasterizer_state.DepthBiasClamp);
        HF32(desc->rasterizer_state.SlopeScaledDepthBias);
        H32(desc->rasterizer_state.DepthClipEnable);
        H32(desc->rasterizer_state.MultisampleEnable);
        H32(desc->rasterizer_state.AntialiasedLineEnable);
        H32(desc->rasterizer_state.ForcedSampleCount);
        H32(desc->rasterizer_state.ConservativeRaster);

        /* Depth-stencil state. */
        H32(desc->depth_stencil_state.DepthEnable);
        H32(desc->depth_stencil_state.DepthWriteMask);
        H32(desc->depth_stencil_state.DepthFunc);
        H32(desc->depth_stencil_state.StencilEnable);
        H32(desc->depth_stencil_state.StencilReadMask);
        H32(desc->depth_stencil_state.StencilWriteMask);
        H32(desc->depth_stencil_state.FrontFace.StencilFailOp);
        H32(desc->depth_stencil_state.FrontFace.StencilDepthFailOp);
        H32(desc->depth_stencil_state.FrontFace.StencilPassOp);
        H32(desc->depth_stencil_state.FrontFace.StencilFunc);
        H32(desc->depth_stencil_state.BackFace.StencilFailOp);
        H32(desc->depth_stencil_state.BackFace.StencilDepthFailOp);
        H32(desc->depth_stencil_state.BackFace.StencilPassOp);
        H32(desc->depth_stencil_state.BackFace.StencilFunc);
        H32(desc->depth_stencil_state.DepthBoundsTestEnable);

        /* Input layout. */
        H32(desc->input_layout.NumElements);
        for (i = 0; i < desc->input_layout.NumElements; i++)
        {
            HS(desc->input_layout.pInputElementDescs[i].SemanticName);
            H32(desc->input_layout.pInputElementDescs[i].SemanticIndex);
            H32(desc->input_layout.pInputElementDescs[i].Format);
            H32(desc->input_layout.pInputElementDescs[i].InputSlot);
            H32(desc->input_layout.pInputElementDescs[i].AlignedByteOffset);
            H32(desc->input_layout.pInputElementDescs[i].InputSlotClass);
            H32(desc->input_layout.pInputElementDescs[i].InstanceDataStepRate);
        }

        H32(desc->strip_cut_value);
        H32(desc->primitive_topology_type);
        H32(desc->dsv_format);

        /* Sample desc */
        H32(desc->sample_desc.Count);
        H32(desc->sample_desc.Quality);

        /* View instancing */
        H32(desc->view_instancing_desc.ViewInstanceCount);
        for (i = 0; i < desc->view_instancing_desc.ViewInstanceCount; i++)
        {
            H32(desc->view_instancing_desc.pViewInstanceLocations[i].RenderTargetArrayIndex);
            H32(desc->view_instancing_desc.pViewInstanceLocations[i].ViewportArrayIndex);
        }
        H32(desc->view_instancing_desc.Flags);
    }
    H32(desc->node_mask);
    H32(desc->flags);
#undef H8
#undef H32
#undef HF32
#undef HS

    compat->state_desc_compat_hash = state_hash;

    for (i = 0; i < ARRAY_SIZE(code_list) && output_index < ARRAY_SIZE(compat->dxbc_blob_hashes); i++)
    {
        if (code_list[i]->BytecodeLength)
        {
            const struct vkd3d_shader_code dxbc = { code_list[i]->pShaderBytecode, code_list[i]->BytecodeLength };
            compat->dxbc_blob_hashes[output_index] = vkd3d_shader_hash(&dxbc);
            compat->dxbc_blob_hashes[output_index] = hash_fnv1_iterate_u8(compat->dxbc_blob_hashes[output_index], i);
            output_index++;
        }
    }
}
