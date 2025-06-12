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

/* The stream format is used for internal magic cache.
 * In this scheme, we optimize for append performance rather than read performance.
 * TODO: This is a stepping stone for Fossilize integration, which would allow e.g. Steam to provide us with
 * pre-primed caches. */
enum vkd3d_serialized_pipeline_stream_entry_type
{
    VKD3D_SERIALIZED_PIPELINE_STREAM_ENTRY_SPIRV = 0,
    VKD3D_SERIALIZED_PIPELINE_STREAM_ENTRY_DRIVER_CACHE = 1,
    VKD3D_SERIALIZED_PIPELINE_STREAM_ENTRY_PIPELINE = 2,
    VKD3D_SERIALIZED_PIPELINE_STREAM_MAX_INT = 0x7fffffff,
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

#define VKD3D_CACHE_BLOB_VERSION MAKE_MAGIC('V','K','B',4)

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
    /* VkShaderStage is stored in upper 16 bits. */
    VKD3D_PIPELINE_BLOB_CHUNK_TYPE_SHADER_IDENTIFIER = 6,
    VKD3D_PIPELINE_BLOB_CHUNK_TYPE_MASK = 0xffff,
    VKD3D_PIPELINE_BLOB_CHUNK_INDEX_SHIFT = 16,
};

struct vkd3d_pipeline_blob_chunk
{
    uint32_t type; /* vkd3d_pipeline_blob_chunk_type with extra data in upper bits. */
    uint32_t size; /* size of data. Does not include size of header. */
    uint8_t data[] vkd3d_counted_by(size); /* struct vkd3d_pipeline_blob_chunk_*. */
};

struct vkd3d_pipeline_blob_chunk_spirv
{
    uint32_t decompressed_spirv_size;
    uint32_t compressed_spirv_size; /* Size of data[]. */
    uint8_t data[] vkd3d_counted_by(compressed_spirv_size);
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
    uint8_t data[]; /* vkd3d_pipeline_blob_chunks laid out one after the other with u64 alignment. */
};

/* Used for de-duplicated pipeline cache and SPIR-V hashmaps. */
struct vkd3d_pipeline_blob_internal
{
    uint32_t checksum; /* Simple checksum for data[] as a sanity check. */
    uint8_t data[]; /* Either raw uint8_t for pipeline cache, or vkd3d_pipeline_blob_chunk_spirv. */
};

STATIC_ASSERT(offsetof(struct vkd3d_pipeline_blob, data) == (32 + VK_UUID_SIZE));
STATIC_ASSERT(offsetof(struct vkd3d_pipeline_blob, data) == sizeof(struct vkd3d_pipeline_blob));

struct vkd3d_serialized_pipeline_stream_entry
{
    uint64_t hash;
    uint64_t checksum; /* Checksum of the rest of this header + data. */
    uint32_t size;
    enum vkd3d_serialized_pipeline_stream_entry_type type;
    uint8_t data[];
};
STATIC_ASSERT(sizeof(struct vkd3d_serialized_pipeline_stream_entry) ==
        offsetof(struct vkd3d_serialized_pipeline_stream_entry, data));
STATIC_ASSERT(sizeof(struct vkd3d_serialized_pipeline_stream_entry) == 24);

static uint32_t vkd3d_pipeline_blob_compute_data_checksum(const uint8_t *data, size_t size)
{
    const struct vkd3d_shader_code code = { data, size };
    vkd3d_shader_hash_t h;

    h = vkd3d_shader_hash(&code);
    return hash_uint64(h);
}

static uint64_t vkd3d_serialized_pipeline_stream_entry_compute_checksum(const uint8_t *data,
        const struct vkd3d_serialized_pipeline_stream_entry *entry)
{
    const struct vkd3d_shader_code code = { data, entry->size };
    vkd3d_shader_hash_t h;

    h = vkd3d_shader_hash(&code);
    h = hash_fnv1_iterate_u64(h, entry->hash);
    h = hash_fnv1_iterate_u32(h, entry->size);
    h = hash_fnv1_iterate_u32(h, entry->type);
    return h;
}

static bool vkd3d_serialized_pipeline_stream_entry_validate(const uint8_t *data,
        const struct vkd3d_serialized_pipeline_stream_entry *entry)
{
    uint64_t checksum = vkd3d_serialized_pipeline_stream_entry_compute_checksum(data, entry);
    return checksum == entry->checksum;
}

static const struct vkd3d_pipeline_blob_chunk *find_blob_chunk_masked(const struct vkd3d_pipeline_blob_chunk *chunk,
        size_t size, uint32_t type, uint32_t mask)
{
    uint32_t aligned_chunk_size;

    while (size >= sizeof(struct vkd3d_pipeline_blob_chunk))
    {
        aligned_chunk_size = align(chunk->size + sizeof(struct vkd3d_pipeline_blob_chunk),
                VKD3D_PIPELINE_BLOB_CHUNK_ALIGN);
        if (aligned_chunk_size > size)
            return NULL;
        if ((chunk->type & mask) == type)
            return chunk;

        chunk = (const struct vkd3d_pipeline_blob_chunk *)&chunk->data[align(chunk->size, VKD3D_PIPELINE_BLOB_CHUNK_ALIGN)];
        size -= aligned_chunk_size;
    }

    return NULL;
}

static const struct vkd3d_pipeline_blob_chunk *find_blob_chunk(const struct vkd3d_pipeline_blob_chunk *chunk,
        size_t size, uint32_t type)
{
    return find_blob_chunk_masked(chunk, size, type, ~0u);
}

static uint32_t d3d12_cached_pipeline_state_to_flags(const struct d3d12_cached_pipeline_state *state)
{
    uint32_t pipeline_library_flags;

    if (state->library)
        pipeline_library_flags = state->library->flags;
    else if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_GLOBAL_PIPELINE_CACHE)
        pipeline_library_flags = 0;
    else
    {
        pipeline_library_flags = VKD3D_PIPELINE_LIBRARY_FLAG_USE_PIPELINE_CACHE_UUID |
                VKD3D_PIPELINE_LIBRARY_FLAG_SAVE_PSO_BLOB;
    }

    return pipeline_library_flags;
}

HRESULT d3d12_cached_pipeline_state_validate(struct d3d12_device *device,
        const struct d3d12_cached_pipeline_state *state,
        const struct vkd3d_pipeline_cache_compatibility *compat)
{
    const VkPhysicalDeviceProperties *device_properties = &device->device_info.properties2.properties;
    const struct vkd3d_pipeline_blob *blob = state->blob.pCachedBlob;
    const struct vkd3d_pipeline_blob_chunk_pso_compat *pso_compat;
    const struct vkd3d_pipeline_blob_chunk *chunk;
    uint32_t pipeline_library_flags;
    size_t payload_size;
    uint32_t checksum;

    pipeline_library_flags = d3d12_cached_pipeline_state_to_flags(state);

    /* Avoid E_INVALIDARG with an invalid header size, since that may confuse some games */
    if (state->blob.CachedBlobSizeInBytes < sizeof(*blob) || blob->version != VKD3D_CACHE_BLOB_VERSION)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("Invalid PSO blob detected, returning D3D12_ERROR_DRIVER_VERSION_MISMATCH.\n");
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
    }

    payload_size = state->blob.CachedBlobSizeInBytes - offsetof(struct vkd3d_pipeline_blob, data);

    /* Indicate that the cached data is not useful if we're running on a different device or driver */
    if (blob->vendor_id != device_properties->vendorID || blob->device_id != device_properties->deviceID)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
            INFO("Unexpected vendor detected, returning D3D12_ERROR_ADAPTER_NOT_FOUND.\n");
        return D3D12_ERROR_ADAPTER_NOT_FOUND;
    }

    /* Check the vkd3d-proton build since the shader compiler itself may change,
     * and the driver since that will affect the generated pipeline cache.
     * Based on global configuration flags, which extensions are available, etc,
     * the generated shaders may also change, so key on that as well. */
    if (blob->vkd3d_build != vkd3d_build ||
            blob->vkd3d_shader_interface_key != device->shader_interface_key)
    {
        if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
        {
            if (blob->vkd3d_build != vkd3d_build)
            {
                INFO("Unexpected build version (got %"PRIx64", expected %"PRIx64"), returning D3D12_ERROR_DRIVER_VERSION_MISMATCH.\n",
                        blob->vkd3d_build, vkd3d_build);
            }

            if (blob->vkd3d_shader_interface_key != device->shader_interface_key)
            {
                INFO("Unexpected shader interface key (got %"PRIx64", expected %"PRIx64"), returning D3D12_ERROR_DRIVER_VERSION_MISMATCH.\n",
                        blob->vkd3d_shader_interface_key, device->shader_interface_key);
            }
        }
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
    }

    /* Only verify pipeline cache UUID if we're going to read anything from it. */
    if (pipeline_library_flags & VKD3D_PIPELINE_LIBRARY_FLAG_USE_PIPELINE_CACHE_UUID)
    {
        if (memcmp(blob->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE) != 0)
        {
            if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
                INFO("Unexpected pipelineCacheUUID, returning D3D12_ERROR_DRIVER_VERSION_MISMATCH.\n");
            return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
        }
    }

    if (pipeline_library_flags & VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER)
    {
        if (memcmp(blob->cache_uuid,
                device->device_info.shader_module_identifier_properties.shaderModuleIdentifierAlgorithmUUID,
                VK_UUID_SIZE) != 0)
        {
            if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
                INFO("Unexpected shaderModuleIdentifierAlgorithmUUID, returning D3D12_ERROR_DRIVER_VERSION_MISMATCH.\n");
            return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
        }
    }

    /* In stream archives, we perform checksums ahead of time before accepting a stream blob into internal cache.
     * No need to do redundant work. */
    if (!(pipeline_library_flags & VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE))
    {
        checksum = vkd3d_pipeline_blob_compute_data_checksum(blob->data, payload_size);

        if (checksum != blob->checksum)
        {
            ERR("Corrupt PSO cache blob entry found!\n");
            /* Same rationale as above, avoid E_INVALIDARG, since that may confuse some games */
            return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
        }
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

bool d3d12_cached_pipeline_state_is_dummy(const struct d3d12_cached_pipeline_state *state)
{
    const struct vkd3d_pipeline_blob *blob = state->blob.pCachedBlob;
    const struct vkd3d_pipeline_blob_chunk *chunk;
    size_t payload_size;

    if (!state->blob.CachedBlobSizeInBytes)
        return true;

    if (state->blob.CachedBlobSizeInBytes < sizeof(*blob) || blob->version != VKD3D_CACHE_BLOB_VERSION)
        return true;
    payload_size = state->blob.CachedBlobSizeInBytes - offsetof(struct vkd3d_pipeline_blob, data);

    chunk = CONST_CAST_CHUNK_BASE(blob);

    /* Try to find any PSO cache or SPIR-V entry. If they exist, this is not a dummy blob. */
    if (find_blob_chunk(chunk, payload_size, VKD3D_PIPELINE_BLOB_CHUNK_TYPE_PIPELINE_CACHE))
        return false;

    if (find_blob_chunk(chunk, payload_size, VKD3D_PIPELINE_BLOB_CHUNK_TYPE_PIPELINE_CACHE_LINK))
        return false;

    if (find_blob_chunk_masked(chunk, payload_size,
            VKD3D_PIPELINE_BLOB_CHUNK_TYPE_VARINT_SPIRV,
            VKD3D_PIPELINE_BLOB_CHUNK_TYPE_MASK))
        return false;

    if (find_blob_chunk_masked(chunk, payload_size,
            VKD3D_PIPELINE_BLOB_CHUNK_TYPE_VARINT_SPIRV_LINK,
            VKD3D_PIPELINE_BLOB_CHUNK_TYPE_MASK))
        return false;

    if (find_blob_chunk_masked(chunk, payload_size,
            VKD3D_PIPELINE_BLOB_CHUNK_TYPE_SHADER_IDENTIFIER,
            VKD3D_PIPELINE_BLOB_CHUNK_TYPE_MASK))
        return false;

    return true;
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

        /* In stream archives, checksums are handled at the outer layer, just ignore them here. */
        if (!(pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE))
        {
            checksum = vkd3d_pipeline_blob_compute_data_checksum(*data, *size);
            if (checksum != internal->checksum)
            {
                FIXME("Checksum mismatch.\n");
                goto out;
            }
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
    uint32_t pipeline_library_flags;
    size_t payload_size;
    const void *data;
    size_t size;
    VkResult vr;

    pipeline_library_flags = d3d12_cached_pipeline_state_to_flags(state);

    if (!(pipeline_library_flags & VKD3D_PIPELINE_LIBRARY_FLAG_SAVE_PSO_BLOB))
    {
        vr = vkd3d_create_pipeline_cache(device, 0, NULL, cache);
        return hresult_from_vk_result(vr);
    }

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
        struct vkd3d_shader_code *spirv_code,
        VkPipelineShaderStageModuleIdentifierCreateInfoEXT *identifier)
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

    if (state->library && (state->library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER))
    {
        /* Only return identifier if we can use it. */
        chunk = find_blob_chunk(CONST_CAST_CHUNK_BASE(blob), payload_size,
                VKD3D_PIPELINE_BLOB_CHUNK_TYPE_SHADER_IDENTIFIER | (stage << VKD3D_PIPELINE_BLOB_CHUNK_INDEX_SHIFT));

        if (chunk && chunk->size <= VK_MAX_SHADER_MODULE_IDENTIFIER_SIZE_EXT)
        {
            identifier->identifierSize = chunk->size;
            identifier->pIdentifier = chunk->data;
            spirv_code->size = 0;
            spirv_code->code = NULL;
            return S_OK;
        }
    }

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

static void vkd3d_shader_code_serialize_identifier(struct d3d12_pipeline_library *pipeline_library,
        const struct vkd3d_shader_code *code,
        const VkShaderModuleIdentifierEXT *identifier, VkShaderStageFlagBits stage,
        struct vkd3d_pipeline_blob_chunk **inout_chunk)
{
    struct vkd3d_pipeline_blob_chunk *chunk = *inout_chunk;
    struct vkd3d_pipeline_blob_chunk_shader_meta *meta;

    if (!identifier->identifierSize)
        return;

    /* Store identifier. */
    chunk->type = VKD3D_PIPELINE_BLOB_CHUNK_TYPE_SHADER_IDENTIFIER | (stage << VKD3D_PIPELINE_BLOB_CHUNK_INDEX_SHIFT);
    chunk->size = identifier->identifierSize;
    memcpy(chunk->data, identifier->identifier, chunk->size);
    chunk = finish_and_iterate_blob_chunk(chunk);

    /* Store meta information for SPIR-V. */
    chunk->type = VKD3D_PIPELINE_BLOB_CHUNK_TYPE_SHADER_META | (stage << VKD3D_PIPELINE_BLOB_CHUNK_INDEX_SHIFT);
    chunk->size = sizeof(*meta);
    meta = CAST_CHUNK_DATA(chunk, shader_meta);
    meta->meta = code->meta;
    chunk = finish_and_iterate_blob_chunk(chunk);

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

        /* In stream archives, checksums are handled at the outer layer, just ignore them here. */
        if (!pipeline_library || !(pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE))
            internal->checksum = vkd3d_pipeline_blob_compute_data_checksum(internal->data, wrapped_varint_size);
        else
            internal->checksum = 0;

        /* For duplicate, we won't insert. Just free the blob. */
        if (!d3d12_pipeline_library_insert_hash_map_blob_internal(pipeline_library,
                &pipeline_library->spirv_cache_map, &entry))
        {
            vkd3d_free(internal);
        }
        else if (pipeline_library->disk_cache_listener)
        {
            vkd3d_pipeline_library_disk_cache_notify_blob_insert(pipeline_library->disk_cache_listener,
                    entry.key.internal_key_hash,
                    VKD3D_SERIALIZED_PIPELINE_STREAM_ENTRY_SPIRV,
                    entry.data.blob, entry.data.blob_length);
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

    if (!state->pso_is_loaded_from_cached_blob)
    {
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

    if (state->vk_pso_cache && (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_SAVE_PSO_BLOB))
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

        /* In stream archives, checksums are handled at the outer layer, just ignore them here. */
        if (!pipeline_library || !(pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE))
            internal->checksum = vkd3d_pipeline_blob_compute_data_checksum(blob.code, blob.size);
        else
            internal->checksum = 0;

        /* For duplicate, we won't insert. Just free the blob. */
        if (!d3d12_pipeline_library_insert_hash_map_blob_internal(pipeline_library,
                &pipeline_library->driver_cache_map, &entry))
        {
            vkd3d_free(internal);
        }
        else if (pipeline_library->disk_cache_listener)
        {
            vkd3d_pipeline_library_disk_cache_notify_blob_insert(pipeline_library->disk_cache_listener,
                    entry.key.internal_key_hash,
                    VKD3D_SERIALIZED_PIPELINE_STREAM_ENTRY_DRIVER_CACHE,
                    entry.data.blob, entry.data.blob_length);
        }

        /* Store PSO cache, or link to it if using pipeline cache. */
        chunk->type = VKD3D_PIPELINE_BLOB_CHUNK_TYPE_PIPELINE_CACHE_LINK;
        chunk->size = sizeof(*link);
        link = CAST_CHUNK_DATA(chunk, link);
        link->hash = entry.key.internal_key_hash;

        chunk = finish_and_iterate_blob_chunk(chunk);
    }

    if ((pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_SAVE_FULL_SPIRV) &&
            !state->pso_is_loaded_from_cached_blob)
    {
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
    }

    if (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER)
    {
        if (d3d12_pipeline_state_is_graphics(state))
        {
            for (i = 0; i < state->graphics.stage_count; i++)
            {
                vkd3d_shader_code_serialize_identifier(pipeline_library,
                        &state->graphics.code[i],
                        &state->graphics.identifiers[i], state->graphics.stages[i].stage,
                        &chunk);
            }
        }
        else if (d3d12_pipeline_state_is_compute(state))
        {
            vkd3d_shader_code_serialize_identifier(pipeline_library,
                    &state->compute.code,
                    &state->compute.identifier, VK_SHADER_STAGE_COMPUTE_BIT,
                    &chunk);
        }
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

    if (state->vk_pso_cache && (!pipeline_library || (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_SAVE_PSO_BLOB)))
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

    if ((!pipeline_library || (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_SAVE_FULL_SPIRV)) &&
            !state->pso_is_loaded_from_cached_blob)
    {
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
    }

    if (pipeline_library && (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER))
    {
        if (d3d12_pipeline_state_is_graphics(state))
        {
            for (i = 0; i < state->graphics.stage_count; i++)
            {
                if (state->graphics.identifiers[i].identifierSize)
                {
                    vk_blob_size += VKD3D_PIPELINE_BLOB_CHUNK_SIZE_RAW(state->graphics.identifiers[i].identifierSize);
                    vk_blob_size += VKD3D_PIPELINE_BLOB_CHUNK_SIZE(shader_meta);
                }
            }
        }
        else if (d3d12_pipeline_state_is_compute(state))
        {
            if (state->compute.identifier.identifierSize)
            {
                vk_blob_size += VKD3D_PIPELINE_BLOB_CHUNK_SIZE_RAW(state->compute.identifier.identifierSize);
                vk_blob_size += VKD3D_PIPELINE_BLOB_CHUNK_SIZE(shader_meta);
            }
        }
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

        if (pipeline_library && (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER))
        {
            memcpy(blob->cache_uuid,
                    pipeline_library->device->device_info.shader_module_identifier_properties.shaderModuleIdentifierAlgorithmUUID,
                    VK_UUID_SIZE);
        }
        else if (!pipeline_library || (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_USE_PIPELINE_CACHE_UUID))
            memcpy(blob->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE);
        else
            memset(blob->cache_uuid, 0, VK_UUID_SIZE);

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

        /* In stream archives, checksums are handled at the outer layer, just ignore them here. */
        if (!pipeline_library || !(pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE))
            blob->checksum = vkd3d_pipeline_blob_compute_data_checksum(blob->data, vk_blob_size);
        else
            blob->checksum = 0;
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

#define VKD3D_PIPELINE_LIBRARY_VERSION_TOC MAKE_MAGIC('V','K','L',4)
#define VKD3D_PIPELINE_LIBRARY_VERSION_STREAM MAKE_MAGIC('V','K','S',4)

struct vkd3d_serialized_pipeline_library_toc
{
    uint32_t version;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t spirv_count;
    uint32_t driver_cache_count;
    uint32_t pipeline_count;
    uint64_t vkd3d_build;
    uint64_t vkd3d_shader_interface_key;
    /* Refers to pipelineCacheUUID if we're using VkPipelineCache blobs,
     * or shaderModuleIdentifierAlgorithmUUID if using shader module identifiers.
     * With the nature of UUIDs, we don't risk random mismatches if
     * a blob cache UUID is consumed by shader module identifiers and vice versa. */
    uint8_t cache_uuid[VK_UUID_SIZE];
    struct vkd3d_serialized_pipeline_toc_entry entries[];
};
STATIC_ASSERT(sizeof(struct vkd3d_serialized_pipeline_library_toc) ==
        offsetof(struct vkd3d_serialized_pipeline_library_toc, entries));
STATIC_ASSERT(sizeof(struct vkd3d_serialized_pipeline_library_toc) == 40 + VK_UUID_SIZE);

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

/* The stream variant. Blobs are emitted one after the other with header + data. */

/* It's possible to just make this header into a bucket hash for e.g. Fossilize. */
struct vkd3d_serialized_pipeline_library_stream
{
    uint32_t version;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t reserved;
    uint64_t vkd3d_build;
    uint64_t vkd3d_shader_interface_key;
    /* Mostly irrelevant since we use GLOBAL_PIPELINE_CACHE by default for stream archives. */
    uint8_t cache_uuid[VK_UUID_SIZE];
    uint8_t entries[];
};
STATIC_ASSERT(sizeof(struct vkd3d_serialized_pipeline_library_stream) ==
        offsetof(struct vkd3d_serialized_pipeline_library_stream, entries));
STATIC_ASSERT(sizeof(struct vkd3d_serialized_pipeline_library_stream) == 32 + VK_UUID_SIZE);

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

    hash_map_free(map);
}

static void d3d12_pipeline_library_cleanup(struct d3d12_pipeline_library *pipeline_library, struct d3d12_device *device)
{
    d3d12_pipeline_library_cleanup_map(&pipeline_library->pso_map);
    d3d12_pipeline_library_cleanup_map(&pipeline_library->driver_cache_map);
    d3d12_pipeline_library_cleanup_map(&pipeline_library->spirv_cache_map);

    d3d_destruction_notifier_free(&pipeline_library->destruction_notifier);
    vkd3d_private_store_destroy(&pipeline_library->private_store);
    rwlock_destroy(&pipeline_library->mutex);
    rwlock_destroy(&pipeline_library->internal_hashmap_mutex);
}

static HRESULT STDMETHODCALLTYPE d3d12_pipeline_library_QueryInterface(d3d12_pipeline_library_iface *iface,
        REFIID riid, void **object)
{
    struct d3d12_pipeline_library *pipeline_library = impl_from_ID3D12PipelineLibrary(iface);

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (!object)
        return E_POINTER;

    if (IsEqualGUID(riid, &IID_ID3D12PipelineLibrary)
            || IsEqualGUID(riid, &IID_ID3D12PipelineLibrary1)
            || IsEqualGUID(riid, &IID_ID3D12DeviceChild)
            || IsEqualGUID(riid, &IID_ID3D12Object)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        ID3D12PipelineLibrary1_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    if (IsEqualGUID(riid, &IID_ID3DDestructionNotifier))
    {
        ID3DDestructionNotifier_AddRef(&pipeline_library->destruction_notifier.ID3DDestructionNotifier_iface);
        *object = &pipeline_library->destruction_notifier.ID3DDestructionNotifier_iface;
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
        d3d_destruction_notifier_notify(&pipeline_library->destruction_notifier);

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
    if (!(new_name = vkd3d_malloc(entry.key.name_length)))
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

    if (!(new_blob = vkd3d_malloc(entry.data.blob_length)))
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
                pipeline_cache_compat.root_signature_compat_hash = root_signature->pso_compatibility_hash;
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

    /* Stream archives are not serialized as a monolithic blob. */
    if (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE)
        return 0;

    total_size += sizeof(struct vkd3d_serialized_pipeline_library_toc);
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

static void d3d12_pipeline_library_serialize_stream_archive_header(struct d3d12_pipeline_library *pipeline_library,
        struct vkd3d_serialized_pipeline_library_stream *header)
{
    const VkPhysicalDeviceProperties *device_properties = &pipeline_library->device->device_info.properties2.properties;
    header->version = VKD3D_PIPELINE_LIBRARY_VERSION_STREAM;
    header->vendor_id = device_properties->vendorID;
    header->device_id = device_properties->deviceID;
    header->reserved = 0;
    header->vkd3d_build = vkd3d_build;
    header->vkd3d_shader_interface_key = pipeline_library->device->shader_interface_key;

    if (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER)
    {
        memcpy(header->cache_uuid,
                pipeline_library->device->device_info.shader_module_identifier_properties.shaderModuleIdentifierAlgorithmUUID,
                VK_UUID_SIZE);
    }
    else if (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_USE_PIPELINE_CACHE_UUID)
        memcpy(header->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE);
    else
        memset(header->cache_uuid, 0, VK_UUID_SIZE);
}

static HRESULT d3d12_pipeline_library_serialize(struct d3d12_pipeline_library *pipeline_library,
        void *data, size_t data_size)
{
    const VkPhysicalDeviceProperties *device_properties = &pipeline_library->device->device_info.properties2.properties;
    struct vkd3d_serialized_pipeline_library_toc *header = data;
    struct vkd3d_serialized_pipeline_toc_entry *toc_entries;
    uint64_t driver_cache_size;
    uint8_t *serialized_data;
    size_t total_toc_entries;
    size_t required_size;
    uint64_t spirv_size;
    size_t name_offset;
    size_t blob_offset;
    uint64_t pso_size;
    void *output_data;

    /* Stream archives are not serialized as a monolithic blob. */
    if (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE)
        return E_INVALIDARG;

    required_size = d3d12_pipeline_library_get_serialized_size(pipeline_library);
    if (data_size < required_size)
        return E_INVALIDARG;

    output_data = NULL;

    if (pipeline_library->input_blob_length)
    {
        /* Check if we need a workaround.
         * FF XVI unserializes a pipeline library with pointer A, but then re-serializes it back to pointer A.
         * We read straight from pointer A when unserializing since the pointer has to remain alive.
         * https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device1-createpipelinelibrary
         * > The pointer provided as input to this method must remain valid for the lifetime of the object returned.
         * > For efficiency reasons, the data is not copied.
         * However, this game seems to rely on re-serialized data to remain bit-exact in same order,
         * which our implementation does not guarantee, and we end up generating a corrupt name table.
         * If there is memory overlap, serialize to a temp buffer instead.
         * */
        uintptr_t overlap_lo, overlap_hi;
        uintptr_t output_lo, output_hi;
        uintptr_t input_lo, input_hi;

        input_lo = (uintptr_t)pipeline_library->input_blob;
        input_hi = input_lo + pipeline_library->input_blob_length - 1;

        output_lo = (uintptr_t)data;
        output_hi = output_lo + data_size - 1;

        overlap_lo = max(input_lo, output_lo);
        overlap_hi = min(input_hi, output_hi);

        if (overlap_lo <= overlap_hi)
        {
            WARN("Invalid API usage. Application attempts to serialize to memory owned by this pipeline library. Falling back.\n");
            output_data = data;
            data = vkd3d_malloc(required_size);
            header = data;
        }
    }

    header->version = VKD3D_PIPELINE_LIBRARY_VERSION_TOC;
    header->vendor_id = device_properties->vendorID;
    header->device_id = device_properties->deviceID;
    header->pipeline_count = pipeline_library->pso_map.used_count;
    header->spirv_count = pipeline_library->spirv_cache_map.used_count;
    header->driver_cache_count = pipeline_library->driver_cache_map.used_count;
    header->vkd3d_build = vkd3d_build;
    header->vkd3d_shader_interface_key = pipeline_library->device->shader_interface_key;

    if (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER)
    {
        memcpy(header->cache_uuid,
                pipeline_library->device->device_info.shader_module_identifier_properties.shaderModuleIdentifierAlgorithmUUID,
                VK_UUID_SIZE);
    }
    else if (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_USE_PIPELINE_CACHE_UUID)
        memcpy(header->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE);
    else
        memset(header->cache_uuid, 0, VK_UUID_SIZE);

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

    if (output_data)
    {
        memcpy(output_data, data, required_size);
        vkd3d_free(data);
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

static HRESULT d3d12_pipeline_library_validate_stream_format_header(struct d3d12_pipeline_library *pipeline_library,
        struct d3d12_device *device, const void *blob, size_t blob_length)
{
    const VkPhysicalDeviceProperties *device_properties = &device->device_info.properties2.properties;
    const struct vkd3d_serialized_pipeline_library_stream *header = blob;

    if (blob_length < sizeof(*header) || header->version != VKD3D_PIPELINE_LIBRARY_VERSION_STREAM)
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;

    if (header->device_id != device_properties->deviceID || header->vendor_id != device_properties->vendorID)
        return D3D12_ERROR_ADAPTER_NOT_FOUND;

    if (header->vkd3d_build != vkd3d_build)
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;

    if (header->vkd3d_shader_interface_key != device->shader_interface_key)
        return D3D12_ERROR_DRIVER_VERSION_MISMATCH;

    /* If we never store pipeline caches, we don't have to care about pipeline cache UUID. */
    if (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_USE_PIPELINE_CACHE_UUID)
        if (memcmp(header->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE) != 0)
            return D3D12_ERROR_DRIVER_VERSION_MISMATCH;

    if (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER)
    {
        if (memcmp(header->cache_uuid,
                device->device_info.shader_module_identifier_properties.shaderModuleIdentifierAlgorithmUUID,
                VK_UUID_SIZE) != 0)
        {
            return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
        }
    }

    return S_OK;
}

static HRESULT d3d12_pipeline_library_read_blob_stream_format(struct d3d12_pipeline_library *pipeline_library,
        struct d3d12_device *device, const void *blob, size_t blob_length)
{
    const struct vkd3d_serialized_pipeline_library_stream *header = blob;
    const struct vkd3d_serialized_pipeline_stream_entry *entries;
    struct vkd3d_cached_pipeline_entry entry;
    uint64_t blob_length_saved = blob_length;
    uint32_t driver_cache_count = 0;
    uint32_t pipeline_count = 0;
    bool early_teardown = false;
    uint32_t spirv_count = 0;
    uint32_t aligned_size;
    struct hash_map *map;
    HRESULT hr;

    if (FAILED(hr = d3d12_pipeline_library_validate_stream_format_header(pipeline_library, device, blob, blob_length)))
        return hr;

    entries = (const struct vkd3d_serialized_pipeline_stream_entry *)header->entries;
    blob_length -= offsetof(struct vkd3d_serialized_pipeline_library_stream, entries);

    while (blob_length >= sizeof(*entries))
    {
        /* Parsing this can take a long time. Tear down as quick as we can. */
        if (vkd3d_atomic_uint32_load_explicit(&pipeline_library->stream_archive_cancellation_point,
                vkd3d_memory_order_relaxed))
        {
            INFO("Device teardown request received, stopping parse early.\n");
            early_teardown = true;
            break;
        }

        blob_length -= sizeof(*entries);
        aligned_size = align(entries->size, VKD3D_PIPELINE_BLOB_ALIGN);

        /* Sliced files are expected to work since application may terminate in the middle of writing. */
        if (blob_length < aligned_size)
        {
            INFO("Sliced stream cache entry detected. Ignoring rest of archive.\n");
            break;
        }

        if (!vkd3d_serialized_pipeline_stream_entry_validate(entries->data, entries))
        {
            INFO("Corrupt stream cache entry detected. Ignoring rest of archive.\n");
            break;
        }

        entry.key.name_length = 0;
        entry.key.name = NULL;
        entry.key.internal_key_hash = entries->hash;
        entry.data.blob_length = entries->size;
        entry.data.blob = entries->data;
        /* The read-only portion of the stream archive is backed by mmap so we avoid committing too much memory.
         * Similar idea as normal application pipeline libraries. */
        entry.data.is_new = 0;
        entry.data.state = NULL;

        switch (entries->type)
        {
            case VKD3D_SERIALIZED_PIPELINE_STREAM_ENTRY_SPIRV:
                map = &pipeline_library->spirv_cache_map;
                spirv_count++;
                break;

            case VKD3D_SERIALIZED_PIPELINE_STREAM_ENTRY_DRIVER_CACHE:
                map = &pipeline_library->driver_cache_map;
                driver_cache_count++;
                break;

            case VKD3D_SERIALIZED_PIPELINE_STREAM_ENTRY_PIPELINE:
                map = &pipeline_library->pso_map;
                pipeline_count++;
                break;

            default:
                FIXME("Unrecognized type %u.\n", entries->type);
                map = NULL;
                break;
        }

        if (map)
        {
            /* If async flag is set it means we're parsing from a thread, and we must lock since application
             * might be busy trying to create pipelines at this time.
             * If we're parsing at device init, we don't need to lock. */
            if (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE_PARSE_ASYNC)
            {
                if (entries->type == VKD3D_SERIALIZED_PIPELINE_STREAM_ENTRY_PIPELINE)
                {
                    /* Pipeline entries are handled with the main mutex. */
                    rwlock_lock_write(&pipeline_library->mutex);
                    d3d12_pipeline_library_insert_hash_map_blob_locked(pipeline_library, map, &entry);
                    rwlock_unlock_write(&pipeline_library->mutex);
                }
                else
                {
                    /* Non-PSO caches use the internal lock implicitly here. */
                    d3d12_pipeline_library_insert_hash_map_blob_internal(pipeline_library, map, &entry);
                }
            }
            else
                d3d12_pipeline_library_insert_hash_map_blob_locked(pipeline_library, map, &entry);
        }

        blob_length -= aligned_size;
        entries = (const struct vkd3d_serialized_pipeline_stream_entry *)&entries->data[aligned_size];
    }

    if (!early_teardown && (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG))
    {
        INFO("Loading stream pipeline library (%"PRIu64" bytes):\n"
                "  D3D12 PSO count: %u\n"
                "  Unique SPIR-V count: %u\n"
                "  Unique VkPipelineCache count: %u\n",
                blob_length_saved,
                pipeline_count,
                spirv_count,
                driver_cache_count);
    }

    return S_OK;
}

static HRESULT d3d12_pipeline_library_read_blob_toc_format(struct d3d12_pipeline_library *pipeline_library,
        struct d3d12_device *device, const void *blob, size_t blob_length)
{
    const VkPhysicalDeviceProperties *device_properties = &device->device_info.properties2.properties;
    const struct vkd3d_serialized_pipeline_library_toc *header = blob;
    const uint8_t *serialized_data_base;
    size_t serialized_data_size;
    const uint8_t *name_table;
    size_t header_entry_size;
    size_t total_toc_entries;
    uint32_t i;
    HRESULT hr;

    /* For reference later when we serialize. Need a workaround. */
    pipeline_library->input_blob = blob;
    pipeline_library->input_blob_length = blob_length;

    /* Same logic as for pipeline blobs, indicate that the app needs
     * to rebuild the pipeline library in case vkd3d itself or the
     * underlying device/driver changed */
    if (blob_length < sizeof(*header) || header->version != VKD3D_PIPELINE_LIBRARY_VERSION_TOC)
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

    /* If we never store pipeline caches, we don't have to care about pipeline cache UUID. */
    if (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_USE_PIPELINE_CACHE_UUID)
    {
        if (memcmp(header->cache_uuid, device_properties->pipelineCacheUUID, VK_UUID_SIZE) != 0)
        {
            if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
                INFO("Rejecting pipeline library due to pipelineCacheUUID mismatch.\n");
            return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
        }
    }

    if (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER)
    {
        if (memcmp(header->cache_uuid,
                device->device_info.shader_module_identifier_properties.shaderModuleIdentifierAlgorithmUUID,
                VK_UUID_SIZE) != 0)
        {
            if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_LOG)
                INFO("Rejecting pipeline library due to shaderModuleIdentifierAlgorithmUUID mismatch.\n");
            return D3D12_ERROR_DRIVER_VERSION_MISMATCH;
        }
    }

    total_toc_entries = header->pipeline_count + header->spirv_count + header->driver_cache_count;

    header_entry_size = offsetof(struct vkd3d_serialized_pipeline_library_toc, entries) +
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

static HRESULT d3d12_pipeline_library_read_blob(struct d3d12_pipeline_library *pipeline_library,
        struct d3d12_device *device, const void *blob, size_t blob_length)
{
    /* For stream archives, we are expected to call the appropriate parsing function after creation. */
    if (pipeline_library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE)
        return E_INVALIDARG;

    /* For app-visible pipeline library, this format moves all TOC information to beginning of blob
     * which optimizes for parsing speed. */
    return d3d12_pipeline_library_read_blob_toc_format(pipeline_library, device, blob, blob_length);
}

static HRESULT d3d12_pipeline_library_init(struct d3d12_pipeline_library *pipeline_library,
        struct d3d12_device *device, const void *blob, size_t blob_length, uint32_t flags)
{
    bool internal_keys;
    HRESULT hr;
    int rc;

    memset(pipeline_library, 0, sizeof(*pipeline_library));
    pipeline_library->ID3D12PipelineLibrary_iface.lpVtbl = &d3d12_pipeline_library_vtbl;
    pipeline_library->refcount = 1;
    pipeline_library->internal_refcount = 1;
    pipeline_library->flags = flags;

    /* Mutually exclusive features. */
    if ((flags & VKD3D_PIPELINE_LIBRARY_FLAG_USE_PIPELINE_CACHE_UUID) &&
            (flags & VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER))
        return E_INVALIDARG;

    /* Mutually exclusive features. */
    if ((flags & VKD3D_PIPELINE_LIBRARY_FLAG_SAVE_FULL_SPIRV) &&
            (flags & VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER))
        return E_INVALIDARG;

    if (!blob_length && blob)
        return E_INVALIDARG;

    if ((rc = rwlock_init(&pipeline_library->mutex)))
        return hresult_from_errno(rc);

    if ((rc = rwlock_init(&pipeline_library->internal_hashmap_mutex)))
    {
        rwlock_destroy(&pipeline_library->mutex);
        return hresult_from_errno(rc);
    }

    internal_keys = !!(flags & VKD3D_PIPELINE_LIBRARY_FLAG_INTERNAL_KEYS);

    hash_map_init(&pipeline_library->spirv_cache_map, vkd3d_cached_pipeline_hash_internal,
            vkd3d_cached_pipeline_compare_internal, sizeof(struct vkd3d_cached_pipeline_entry));
    hash_map_init(&pipeline_library->driver_cache_map, vkd3d_cached_pipeline_hash_internal,
            vkd3d_cached_pipeline_compare_internal, sizeof(struct vkd3d_cached_pipeline_entry));
    hash_map_init(&pipeline_library->pso_map,
            internal_keys ? vkd3d_cached_pipeline_hash_internal : vkd3d_cached_pipeline_hash_name,
            internal_keys ? vkd3d_cached_pipeline_compare_internal : vkd3d_cached_pipeline_compare_name,
            sizeof(struct vkd3d_cached_pipeline_entry));

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

    d3d_destruction_notifier_init(&pipeline_library->destruction_notifier, (IUnknown*)&pipeline_library->ID3D12PipelineLibrary_iface);
    d3d12_device_add_ref(pipeline_library->device = device);
    return hr;

cleanup_hash_map:
    hash_map_free(&pipeline_library->pso_map);
    hash_map_free(&pipeline_library->spirv_cache_map);
    hash_map_free(&pipeline_library->driver_cache_map);
cleanup_mutex:
    rwlock_destroy(&pipeline_library->mutex);
    return hr;
}

HRESULT d3d12_pipeline_library_create(struct d3d12_device *device, const void *blob,
        size_t blob_length, uint32_t flags, struct d3d12_pipeline_library **pipeline_library)
{
    struct d3d12_pipeline_library *object;
    HRESULT hr;

    if (!(object = vkd3d_malloc(sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d12_pipeline_library_init(object, device, blob, blob_length, flags)))
    {
        if (hr == E_OUTOFMEMORY)
            ERR("d3d12_pipeline_library_init failed with E_OUTOFMEMORY. This is likely a symptom of corrupt blob.\n");
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
        &desc->as,
        &desc->ms,
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
        H32(desc->rasterizer_state.LineRasterizationMode);
        H32(desc->rasterizer_state.ForcedSampleCount);
        H32(desc->rasterizer_state.ConservativeRaster);

        /* Depth-stencil state. */
        H32(desc->depth_stencil_state.DepthEnable);
        H32(desc->depth_stencil_state.DepthWriteMask);
        H32(desc->depth_stencil_state.DepthFunc);
        H32(desc->depth_stencil_state.StencilEnable);
        H32(desc->depth_stencil_state.FrontFace.StencilFailOp);
        H32(desc->depth_stencil_state.FrontFace.StencilDepthFailOp);
        H32(desc->depth_stencil_state.FrontFace.StencilPassOp);
        H32(desc->depth_stencil_state.FrontFace.StencilFunc);
        H32(desc->depth_stencil_state.FrontFace.StencilReadMask);
        H32(desc->depth_stencil_state.FrontFace.StencilWriteMask);
        H32(desc->depth_stencil_state.BackFace.StencilFailOp);
        H32(desc->depth_stencil_state.BackFace.StencilDepthFailOp);
        H32(desc->depth_stencil_state.BackFace.StencilPassOp);
        H32(desc->depth_stencil_state.BackFace.StencilFunc);
        H32(desc->depth_stencil_state.BackFace.StencilReadMask);
        H32(desc->depth_stencil_state.BackFace.StencilWriteMask);
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

uint64_t vkd3d_pipeline_cache_compatibility_condense(const struct vkd3d_pipeline_cache_compatibility *compat)
{
    unsigned int i;
    uint64_t h;

    h = hash_fnv1_init();
    h = hash_fnv1_iterate_u64(h, compat->state_desc_compat_hash);
    h = hash_fnv1_iterate_u64(h, compat->root_signature_compat_hash);
    for (i = 0; i < ARRAY_SIZE(compat->dxbc_blob_hashes); i++)
        h = hash_fnv1_iterate_u64(h, compat->dxbc_blob_hashes[i]);
    return h;
}

static HRESULT vkd3d_pipeline_library_disk_cache_save_pipeline_state(struct vkd3d_pipeline_library_disk_cache *cache,
        const struct vkd3d_pipeline_library_disk_cache_item *item)
{
    struct d3d12_pipeline_library *library = cache->library;
    struct vkd3d_cached_pipeline_entry entry;
    void *new_blob;
    VkResult vr;
    int rc;

    /* Try to avoid taking writer locks until we're absolutely forced to.
     * It's fairly likely we'll see duplicates here, so we should avoid stalling
     * when multiple threads are hammering us with PSO creation. */

    entry.key.name_length = 0;
    entry.key.name = NULL;
    entry.key.internal_key_hash = vkd3d_pipeline_cache_compatibility_condense(&item->state->pipeline_cache_compat);

    if ((rc = rwlock_lock_read(&library->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return hresult_from_errno(rc);
    }

    if (hash_map_find(&library->pso_map, &entry.key))
    {
        /* This could happen if a parallel thread tried to create the same PSO.
         * In a single threaded scenario we would find the PSO when creating the PSO,
         * and we would never try to enter this path. */
        rwlock_unlock_read(&library->mutex);
        return E_INVALIDARG;
    }

    if (FAILED(vr = vkd3d_serialize_pipeline_state(library, item->state, &entry.data.blob_length, NULL)))
    {
        rwlock_unlock_read(&library->mutex);
        return hresult_from_vk_result(vr);
    }

    if (!(new_blob = vkd3d_malloc(entry.data.blob_length)))
    {
        rwlock_unlock_read(&library->mutex);
        return E_OUTOFMEMORY;
    }

    if (FAILED(vr = vkd3d_serialize_pipeline_state(library, item->state, &entry.data.blob_length, new_blob)))
    {
        vkd3d_free(new_blob);
        rwlock_unlock_read(&library->mutex);
        return hresult_from_vk_result(vr);
    }

    entry.data.blob = new_blob;
    entry.data.is_new = 1;
    /* We cannot hand the same object out again, since this is not part of the ID3D12PipelineLibrary interface. */
    entry.data.state = NULL;

    /* Now is the time to promote to a writer lock. */
    rwlock_unlock_read(&library->mutex);

    if ((rc = rwlock_lock_write(&library->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        vkd3d_free(new_blob);
        return hresult_from_errno(rc);
    }

    if (!d3d12_pipeline_library_insert_hash_map_blob_locked(library, &library->pso_map, &entry))
    {
        /* Found duplicate. */
        vkd3d_free(new_blob);
        rwlock_unlock_write(&library->mutex);
        return E_OUTOFMEMORY;
    }

    rwlock_unlock_write(&library->mutex);

    if (library->disk_cache_listener)
    {
        vkd3d_pipeline_library_disk_cache_notify_blob_insert(library->disk_cache_listener,
                entry.key.internal_key_hash,
                VKD3D_SERIALIZED_PIPELINE_STREAM_ENTRY_PIPELINE,
                entry.data.blob, entry.data.blob_length);
    }

    return S_OK;
}

HRESULT vkd3d_pipeline_library_store_pipeline_to_disk_cache(
        struct vkd3d_pipeline_library_disk_cache *cache,
        struct d3d12_pipeline_state *state)
{
    /* Push new work to disk cache thread. */
    d3d12_pipeline_state_inc_ref(state);
    pthread_mutex_lock(&cache->lock);
    vkd3d_array_reserve((void**)&cache->items, &cache->items_size,
            cache->items_count + 1, sizeof(*cache->items));
    cache->items[cache->items_count].state = state;
    cache->items_count++;
    condvar_reltime_signal(&cache->cond);
    pthread_mutex_unlock(&cache->lock);

    return S_OK;
}

HRESULT vkd3d_pipeline_library_find_cached_blob_from_disk_cache(struct vkd3d_pipeline_library_disk_cache *cache,
        const struct vkd3d_pipeline_cache_compatibility *compat,
        struct d3d12_cached_pipeline_state *cached_state)
{
    struct d3d12_pipeline_library *library = cache->library;
    const struct vkd3d_cached_pipeline_entry *e;
    struct vkd3d_cached_pipeline_key key;
    int rc;

    if ((rc = rwlock_lock_read(&library->mutex)))
    {
        ERR("Failed to lock mutex, rc %d.\n", rc);
        return hresult_from_errno(rc);
    }

    key.name_length = 0;
    key.name = NULL;
    key.internal_key_hash = vkd3d_pipeline_cache_compatibility_condense(compat);

    if (!(e = (const struct vkd3d_cached_pipeline_entry*)hash_map_find(&library->pso_map, &key)))
    {
        rwlock_unlock_read(&library->mutex);
        return E_INVALIDARG;
    }

    cached_state->blob.CachedBlobSizeInBytes = e->data.blob_length;
    cached_state->blob.pCachedBlob = e->data.blob;
    cached_state->library = library;
    rwlock_unlock_read(&library->mutex);
    return S_OK;
}

static void *vkd3d_pipeline_library_disk_thread_main(void *userarg);

struct disk_cache_entry_key
{
    uint64_t hash;
    enum vkd3d_serialized_pipeline_stream_entry_type type;
};

struct disk_cache_entry
{
    struct hash_map_entry entry;
    struct disk_cache_entry_key key;
};

static uint32_t disk_cache_entry_key_cb(const void *key_)
{
    const struct disk_cache_entry_key *key = key_;
    return hash_combine(hash_uint64(key->hash), key->type);
}

static bool disk_cache_entry_compare_cb(const void *key, const struct hash_map_entry *entry)
{
    const struct disk_cache_entry_key *new_key = key;
    const struct disk_cache_entry_key *old_key = &((const struct disk_cache_entry *)entry)->key;
    return new_key->hash == old_key->hash && new_key->type == old_key->type;
}

static void vkd3d_pipeline_library_disk_cache_merge(struct vkd3d_pipeline_library_disk_cache *cache,
        const char *read_path, const char *write_path)
{
    const struct vkd3d_serialized_pipeline_library_stream *write_cache_header;
    const struct vkd3d_serialized_pipeline_stream_entry *write_entries;
    struct vkd3d_serialized_pipeline_stream_entry stream_entry;
    struct vkd3d_serialized_pipeline_library_stream header;
    struct vkd3d_memory_mapped_file mapped_write_cache;
    char merge_path[VKD3D_PATH_MAX];
    unsigned int existing_entries;
    struct disk_cache_entry entry;
    uint8_t *tmp_buffer = NULL;
    size_t tmp_buffer_size = 0;
    unsigned int new_entries;
    size_t write_cache_size;
    struct hash_map map;
    size_t aligned_size;
    FILE *merge_file;
    int64_t off;
    HRESULT hr;

    memset(&mapped_write_cache, 0, sizeof(mapped_write_cache));
    snprintf(merge_path, sizeof(merge_path), "%s.merge", read_path);
    hash_map_init(&map, disk_cache_entry_key_cb, disk_cache_entry_compare_cb, sizeof(struct disk_cache_entry));
    merge_file = NULL;

    /* If we only have a write-only cache, but no read-only one, this will succeed.
     * We're done. */
    if (vkd3d_file_rename_no_replace(write_path, read_path))
    {
        INFO("Promoting write cache to read cache. No need to merge any disk caches.\n");
        goto out;
    }

    /* We're going to fwrite the mapped data directly. */
    if (!vkd3d_file_map_read_only(write_path, &mapped_write_cache))
    {
        INFO("No write cache exists. No need to merge any disk caches.\n");
        goto out;
    }

    /* If the write cache is out of date, just nuke it and move on. Nothing to do. */
    if (FAILED(hr = d3d12_pipeline_library_validate_stream_format_header(cache->library,
            cache->library->device, mapped_write_cache.mapped, mapped_write_cache.mapped_size)))
    {
        INFO("Write cache is invalid (hr #%x), nuking it.\n", hr);
        goto out;
    }

    /* If we fail here, there is either a stale merge file lying around (which we will clean up at the end),
     * or some other process is merging caches concurrently.
     * It is somewhat unpredictable what will happen with all the atomic renames and deletions in flight,
     * but we'll end up in a consistent state either way.
     * The expectation is that games are loaded with one instance. */
    if (vkd3d_file_rename_no_replace(read_path, merge_path))
    {
        INFO("Merging disk caches.\n");
        merge_file = fopen(merge_path, "rb+");

        /* Shouldn't happen, but can happen if another process races us and deletes the merge file
         * in the interim. */
        if (!merge_file)
        {
            INFO("Cannot re-open merge cache. Likely a race condition with multiple processes.\n");
            goto out;
        }

        existing_entries = 0;
        new_entries = 0;

        /* If we have a read-only cache, atomically move to the merge path.
         * If we win, the merge-only file is "owned" by this thread, and we consider it safe to append to it.
         * Here, we will open the file in append mode, seek to the last whole blob entry,
         * and then append the write-only portion. */

        if (fread(&header, sizeof(header), 1, merge_file) != 1 ||
                FAILED(hr = d3d12_pipeline_library_validate_stream_format_header(cache->library,
                        cache->library->device, &header, sizeof(header))))
        {
            INFO("Read-only cache is out of date, discarding it.\n");
            /* Just promote the write cache to read cache and call it a day. */
            fclose(merge_file);
            merge_file = NULL;
            vkd3d_file_delete(merge_path);
            if (vkd3d_file_rename_overwrite(write_path, read_path))
                INFO("Successfully promoted write cache to read cache.\n");
        }
        else
        {
            /* Find the end of the read cache which contains whole and sane entries.
             * At the same time, add entries to the hash map, so that we don't insert duplicates. */
            off = _ftelli64(merge_file);

            /* From a cold disk cache, this can be quite slow. Poll the teardown atomic. */
            while (fread(&stream_entry, sizeof(stream_entry), 1, merge_file) == 1)
            {
                /* Don't want to throw away the disk caches here. Try again next time. */
                if (vkd3d_atomic_uint32_load_explicit(&cache->library->stream_archive_cancellation_point,
                        vkd3d_memory_order_relaxed))
                {
                    INFO("Device teardown request received, stopping parse early.\n");
                    /* Move the file back, don't delete anything. */
                    fclose(merge_file);
                    merge_file = NULL;
                    vkd3d_file_rename_overwrite(merge_path, read_path);
                    goto out_cancellation;
                }

                aligned_size = align(stream_entry.size, VKD3D_PIPELINE_BLOB_ALIGN);

                /* Before accepting this as a valid entry, ensure checksums are correct.
                 * Ideally we'd have mmap going here, but appending while mmaping a file is ... dubious :) */
                if (!vkd3d_array_reserve((void**)&tmp_buffer, &tmp_buffer_size,
                        aligned_size, 1))
                    break;

                if (fread(tmp_buffer, 1, aligned_size, merge_file) != aligned_size)
                {
                    INFO("Read-only archive entry is sliced. Ignoring rest of archive.\n");
                    break;
                }

                if (!vkd3d_serialized_pipeline_stream_entry_validate(tmp_buffer, &stream_entry))
                {
                    INFO("Found corrupt entry in read-only archive. Ignoring rest of archive.\n");
                    break;
                }

                entry.key.hash = stream_entry.hash;
                entry.key.type = stream_entry.type;
                if (!hash_map_find(&map, &entry.key) && hash_map_insert(&map, &entry.key, &entry.entry))
                    existing_entries++;
                off = _ftelli64(merge_file);
            }

            if (_fseeki64(merge_file, off, SEEK_SET) == 0)
            {
                /* Merge entries. */
                write_cache_header = mapped_write_cache.mapped;
                write_cache_size = mapped_write_cache.mapped_size;
                write_entries = (const struct vkd3d_serialized_pipeline_stream_entry *)write_cache_header->entries;
                write_cache_size -= sizeof(*write_cache_header);

                while (write_cache_size >= sizeof(*write_entries))
                {
                    /* Don't want to throw away the disk caches here. Try again next time. */
                    if (vkd3d_atomic_uint32_load_explicit(&cache->library->stream_archive_cancellation_point,
                            vkd3d_memory_order_relaxed))
                    {
                        INFO("Device teardown request received, stopping parse early.\n");
                        /* Move the file back, don't delete anything. */
                        fclose(merge_file);
                        merge_file = NULL;
                        vkd3d_file_rename_overwrite(merge_path, read_path);
                        goto out_cancellation;
                    }

                    write_cache_size -= sizeof(*write_entries);
                    aligned_size = align(write_entries->size, VKD3D_PIPELINE_BLOB_ALIGN);

                    if (write_cache_size < aligned_size)
                    {
                        INFO("Write-only archive entry is sliced. Ignoring rest of archive.\n");
                        break;
                    }

                    if (!vkd3d_serialized_pipeline_stream_entry_validate(write_entries->data, write_entries))
                    {
                        INFO("Found corrupt entry in write-only archive. Ignoring rest of archive.\n");
                        break;
                    }

                    entry.key.hash = write_entries->hash;
                    entry.key.type = write_entries->type;
                    if (!hash_map_find(&map, &entry.key) && hash_map_insert(&map, &entry.key, &entry.entry))
                    {
                        if (fwrite(write_entries, sizeof(*write_entries), 1, merge_file) != 1 ||
                                fwrite(write_entries->data, 1, aligned_size, merge_file) != aligned_size)
                        {
                            ERR("Failed to append blob to read-cache.\n");
                            break;
                        }
                        new_entries++;
                    }

                    write_cache_size -= aligned_size;
                    write_entries = (const struct vkd3d_serialized_pipeline_stream_entry *)&write_entries->data[aligned_size];
                }
            }

            INFO("Done merging shader caches, existing entries: %u, new entries: %u.\n",
                    existing_entries, new_entries);

            fclose(merge_file);
            merge_file = NULL;
            if (vkd3d_file_rename_overwrite(merge_path, read_path))
                INFO("Successfully replaced shader cache with merged cache.\n");
            else
                INFO("Failed to replace shader cache.\n");
        }
    }

out:
    /* There shouldn't be any write cache left after merging. */
    vkd3d_file_unmap(&mapped_write_cache);
    vkd3d_file_delete(write_path);

    /* If we have a stale merge file lying around, we might have been killed at some point
     * when we tried to merge the read-only cache earlier.
     * We might lose cache data this way, but this shouldn't happen except in extreme circumstances.
     * We need to ensure that some thread will eventually be able exclusively create the merge file,
     * otherwise, we'll never be able to promote new blobs to the read-only cache.
     * In a normal situation, merge_path will never exist at this point. */
    vkd3d_file_delete(merge_path);

out_cancellation:
    vkd3d_file_unmap(&mapped_write_cache);
    if (merge_file)
        fclose(merge_file);
    hash_map_free(&map);
    vkd3d_free(tmp_buffer);
}

static void vkd3d_pipeline_library_disk_cache_initial_setup(struct vkd3d_pipeline_library_disk_cache *cache)
{
    uint64_t begin_ts;
    uint64_t end_ts;
    HRESULT hr;

    begin_ts = vkd3d_get_current_time_ns();

    /* Fairly complex operation. Ideally, Steam handles this.
     * After this operation, only read_path should remain, and write_path (and temporary merge path) is deleted. */
    vkd3d_pipeline_library_disk_cache_merge(cache, cache->read_path, cache->write_path);

    end_ts = vkd3d_get_current_time_ns();

    INFO("Merging pipeline libraries took %.3f ms.\n", 1e-6 * (double)(end_ts - begin_ts));

    begin_ts = vkd3d_get_current_time_ns();

    if (!vkd3d_file_map_read_only(cache->read_path, &cache->mapped_file))
    {
        INFO("Failed to map read-only cache: %s.\n", cache->read_path);
    }
    else
    {
        end_ts = vkd3d_get_current_time_ns();
        INFO("Mapping read-only cache took %.3f ms.\n", 1e-6 * (double)(end_ts - begin_ts));

        begin_ts = vkd3d_get_current_time_ns();
        hr = d3d12_pipeline_library_read_blob_stream_format(cache->library, cache->library->device,
                cache->mapped_file.mapped, cache->mapped_file.mapped_size);
        end_ts = vkd3d_get_current_time_ns();
        INFO("Parsing stream archive took %.3f ms.\n", 1e-6 * (double)(end_ts - begin_ts));

        if (hr == D3D12_ERROR_DRIVER_VERSION_MISMATCH)
            INFO("Cannot load existing on-disk cache due to driver version mismatch.\n");
        else if (hr == D3D12_ERROR_ADAPTER_NOT_FOUND)
            INFO("Cannot load existing on-disk cache due to driver version mismatch.\n");
        else if (FAILED(hr))
            INFO("Failed to load driver cache with hr #%x, falling back to empty cache.\n", hr);
    }

    /* When we add new internal blobs from this point,
     * we'll be notified where we can write out a stream blob to disk.
     * This all happens within the disk$ thread. */
    cache->library->disk_cache_listener = cache;
}

HRESULT vkd3d_pipeline_library_init_disk_cache(struct vkd3d_pipeline_library_disk_cache *cache,
        struct d3d12_device *device)
{
    const char *app_name_str = NULL;
    char path_buf[VKD3D_PATH_MAX];
    char app_name[VKD3D_PATH_MAX];
    VKD3D_UNUSED size_t i, n;
    const char *separator;
    const char *path;
    uint32_t flags;
    HRESULT hr;
    int rc;

    memset(cache, 0, sizeof(*cache));

    if (vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_APP_CACHE_ONLY)
        return S_OK;

    /* Match DXVK style here. The environment variable is a directory.
     * If not set, it is in current working directory. */
    vkd3d_get_env_var("VKD3D_SHADER_CACHE_PATH", path_buf, sizeof(path_buf));
    path = *path_buf != '\0' ? path_buf : NULL;

    if (path)
    {
        separator = &path[strlen(path) - 1];
        separator = (*separator == '/' || *separator == '\\') ? "" : "/";

        /* If we're using explicit cache directory, multiple games are likely pointing to it,
         * so split the caches up by name. */
        if (vkd3d_get_program_name(app_name))
            app_name_str = app_name;
    }
    else
        separator = "";

#ifdef _WIN32
    /* Wine has some curious bugs when it comes to MoveFileA, DeleteFileA and RenameFileA.
     * RenameFileA only works if we use Windows style paths with back-slashes (not forward slashes) for whatever reason.
     * Seems to be related to drive detection (Rename fails with drive mismatch in some cases for example).
     * Manually remap Unix style paths to conservative Win32.
     * Normally Wine accepts Unix style paths, but not here for whatever reason. */

    if (path && path[0] == '/')
        snprintf(cache->read_path, sizeof(cache->read_path), "Z:\\%s%svkd3d-proton", path + 1, separator);
    else if (path)
        snprintf(cache->read_path, sizeof(cache->read_path), "%s%svkd3d-proton", path, separator);
    else
        strcpy(cache->read_path, "vkd3d-proton");

    for (i = 0, n = strlen(cache->read_path); i < n; i++)
        if (cache->read_path[i] == '/')
            cache->read_path[i] = '\\';
#else
    if (path)
        snprintf(cache->read_path, sizeof(cache->read_path), "%s%svkd3d-proton", path, separator);
    else
        strcpy(cache->read_path, "vkd3d-proton");
#endif

    if (app_name_str)
    {
        vkd3d_strlcat(cache->read_path, sizeof(cache->read_path), ".");
        vkd3d_strlcat(cache->read_path, sizeof(cache->read_path), app_name_str);
    }
    vkd3d_strlcat(cache->read_path, sizeof(cache->read_path), ".cache");

#ifdef _WIN32
    INFO("Remapping VKD3D_SHADER_CACHE to: %s.\n", cache->read_path);
#endif

    INFO("Attempting to load disk cache from: %s.\n", cache->read_path);

    /* Split the reader and writer. */
    snprintf(cache->write_path, sizeof(cache->write_path), "%s.write", cache->read_path);

    flags = VKD3D_PIPELINE_LIBRARY_FLAG_INTERNAL_KEYS | VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE;

    /* This flag is mostly for debug. Normally we want to do shader cache management in disk thread. */
    if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_SHADER_CACHE_SYNC))
        flags |= VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE_PARSE_ASYNC;

    if (device->device_info.shader_module_identifier_features.shaderModuleIdentifier)
        flags |= VKD3D_PIPELINE_LIBRARY_FLAG_SHADER_IDENTIFIER;
    else if (!(vkd3d_config_flags & VKD3D_CONFIG_FLAG_PIPELINE_LIBRARY_NO_SERIALIZE_SPIRV))
        flags |= VKD3D_PIPELINE_LIBRARY_FLAG_SAVE_FULL_SPIRV;

    /* For internal caches, we're mostly just concerned with caching SPIR-V.
     * We expect the driver cache deals with PSO blobs. */
    hr = d3d12_pipeline_library_create(device, NULL, 0, flags, &cache->library);

    /* Start with an empty pipeline library, we'll parse stream archive and append. */

    if (SUCCEEDED(hr))
    {
        /* This is held internally, so make sure it's held alive by private references. */
        d3d12_pipeline_library_inc_ref(cache->library);
        d3d12_pipeline_library_dec_public_ref(cache->library);

        if (!(flags & VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE_PARSE_ASYNC))
            vkd3d_pipeline_library_disk_cache_initial_setup(cache);

        cache->thread_active = true;
        if ((rc = pthread_mutex_init(&cache->lock, NULL)) < 0)
            goto mutex_fail;
        if ((rc = condvar_reltime_init(&cache->cond)) < 0)
            goto cond_fail;
        if ((rc = pthread_create(&cache->thread, NULL, vkd3d_pipeline_library_disk_thread_main, cache)) < 0)
            goto thread_fail;
    }

    return hr;

thread_fail:
    condvar_reltime_destroy(&cache->cond);
cond_fail:
    pthread_mutex_destroy(&cache->lock);
mutex_fail:
    ERR("Failed to start pipeline library disk thread.\n");
    if (cache->library)
        d3d12_pipeline_library_dec_ref(cache->library);
    cache->library = NULL;
    cache->thread_active = false;
    return hr;
}

void vkd3d_pipeline_library_flush_disk_cache(struct vkd3d_pipeline_library_disk_cache *cache)
{
    /* Ask disk thread to tear down as quick as possible if it's busy parsing stuff
     * in the disk$ thread. */
    if (cache->library)
    {
        vkd3d_atomic_uint32_store_explicit(&cache->library->stream_archive_cancellation_point, 1,
                vkd3d_memory_order_relaxed);
    }

    if (cache->thread_active)
    {
        pthread_mutex_lock(&cache->lock);
        cache->thread_active = false;
        condvar_reltime_signal(&cache->cond);
        pthread_mutex_unlock(&cache->lock);

        pthread_join(cache->thread, NULL);
        condvar_reltime_destroy(&cache->cond);
        pthread_mutex_destroy(&cache->lock);
    }

    vkd3d_free(cache->items);
    cache->items = NULL;
    cache->items_count = 0;
    cache->items_size = 0;

    if (cache->library)
    {
        cache->library->stream_archive_cancellation_point = 0;
        d3d12_pipeline_library_dec_ref(cache->library);
    }

    vkd3d_file_unmap(&cache->mapped_file);
}

void vkd3d_pipeline_library_disk_cache_notify_blob_insert(struct vkd3d_pipeline_library_disk_cache *disk_cache,
        uint64_t hash, uint32_t type /* vkd3d_serialized_pipeline_stream_entry_type */,
        const void *data, size_t size)
{
    /* Always called from disk$ thread, so we don't have to consider thread safety. */
    struct vkd3d_serialized_pipeline_library_stream header;
    struct vkd3d_serialized_pipeline_stream_entry entry;
    uint8_t zero_array[VKD3D_PIPELINE_BLOB_ALIGN];
    uint32_t padding_size;

    /* On first write (new blob), create a new file. */
    if (!disk_cache->stream_archive_attempted_write)
    {
        disk_cache->stream_archive_attempted_write = true;

        /* Fails if multiple processes run the same application, but this doesn't really happen in practice. */
        disk_cache->stream_archive_write_file = vkd3d_file_open_exclusive_write(disk_cache->write_path);
        if (disk_cache->stream_archive_write_file)
        {
            d3d12_pipeline_library_serialize_stream_archive_header(disk_cache->library, &header);
            if (fwrite(&header, sizeof(header), 1, disk_cache->stream_archive_write_file) != 1)
            {
                ERR("Failed to write stream archive header.\n");
                fclose(disk_cache->stream_archive_write_file);
                disk_cache->stream_archive_write_file = NULL;
            }
        }
        else
            ERR("Failed to open stream archive write file exclusively: %s.\n", disk_cache->write_path);
    }

    if (disk_cache->stream_archive_write_file)
    {
        entry.hash = hash;
        entry.type = type;
        entry.size = size;
        entry.checksum = vkd3d_serialized_pipeline_stream_entry_compute_checksum(data, &entry);

        if (fwrite(&entry, sizeof(entry), 1, disk_cache->stream_archive_write_file) != 1)
            ERR("Failed to write entry header.\n");
        if (fwrite(data, 1, size, disk_cache->stream_archive_write_file) != size)
            ERR("Failed to write blob data.\n");

        /* Write padding data. */
        padding_size = align(size, VKD3D_PIPELINE_BLOB_ALIGN) - size;
        if (padding_size)
        {
            memset(zero_array, 0, padding_size);
            if (fwrite(zero_array, 1, padding_size, disk_cache->stream_archive_write_file) != padding_size)
                ERR("Failed to write padding.\n");
        }

        /* Defer fflush until things quiet down. No need to spam fflush 1000s of times per second. */
    }
}

static void *vkd3d_pipeline_library_disk_thread_main(void *userarg)
{
    struct vkd3d_pipeline_library_disk_cache_item *tmp_items = NULL;
    struct vkd3d_pipeline_library_disk_cache *cache = userarg;
    unsigned int wakeup_counter = 0;
    size_t tmp_items_count = 0;
    size_t tmp_items_size = 0;
    bool active = true;
    bool dirty = false;
    HRESULT hr;
    size_t i;
    int rc;

    vkd3d_set_thread_name("vkd3d-disk$");

    if (cache->library->flags & VKD3D_PIPELINE_LIBRARY_FLAG_STREAM_ARCHIVE_PARSE_ASYNC)
    {
        /* If device is nuked while parsing, we will return early since we poll an atomic. */
        INFO("Performing async setup of stream archive ...\n");
        vkd3d_pipeline_library_disk_cache_initial_setup(cache);
        INFO("Done performing async setup of stream archive.\n");
    }

    while (active)
    {
        pthread_mutex_lock(&cache->lock);

        /* If new pipelines haven't been queued up for a while, flush out the disk cache. */
        active = cache->thread_active;

        if (active)
            rc = condvar_reltime_wait_timeout_seconds(&cache->cond, &cache->lock, 1);
        else
            rc = 0;

        /* For debug purposes, it's useful to know how hard we're being hammered. */
        wakeup_counter++;

        /* Should have a local array so we can serialize without being in a lock. */
        if (cache->items_count > 0)
        {
            vkd3d_array_reserve((void**)&tmp_items, &tmp_items_size,
                    tmp_items_count + cache->items_count, sizeof(*tmp_items));
            memcpy(tmp_items + tmp_items_count, cache->items, cache->items_count * sizeof(*cache->items));
            tmp_items_count += cache->items_count;
            cache->items_count = 0;
        }

        pthread_mutex_unlock(&cache->lock);

        for (i = 0; i < tmp_items_count; i++)
        {
            if (FAILED(hr = vkd3d_pipeline_library_disk_cache_save_pipeline_state(cache, &tmp_items[i])))
            {
                /* INVALIDARG is expected for duplicates. */
                if (hr != E_INVALIDARG)
                    ERR("Failed to serialize pipeline to disk cache, hr #%x.\n", hr);
            }
            else if (!dirty)
            {
                dirty = true;
                INFO("Pipeline cache marked dirty. Flush is scheduled.\n");
            }

            d3d12_pipeline_state_dec_ref(tmp_items[i].state);
        }
        tmp_items_count = 0;

        if (rc > 0)
        {
            /* Timeout, try to flush. */
            if (dirty)
            {
                INFO("Flushing disk cache (wakeup counter since last flush = %u). "
                     "It seems like application has stopped creating new PSOs for the time being.\n",
                     wakeup_counter);

                if (cache->stream_archive_write_file)
                    fflush(cache->stream_archive_write_file);
                wakeup_counter = 0;
                dirty = false;
            }
        }
        else if (rc < 0)
        {
            ERR("Error waiting for condition variable in library disk thread.\n");
            break;
        }
    }

    /* Teardown path. */
    for (i = 0; i < tmp_items_count; i++)
    {
        if (FAILED(hr = vkd3d_pipeline_library_disk_cache_save_pipeline_state(cache, &tmp_items[i])))
        {
            /* INVALIDARG is expected for duplicates. */
            if (hr != E_INVALIDARG)
                ERR("Failed to serialize pipeline to disk cache, hr #%x.\n", hr);
        }
        else
            dirty = true;

        d3d12_pipeline_state_dec_ref(tmp_items[i].state);
    }

    for (i = 0; i < cache->items_count; i++)
    {
        if (FAILED(hr = vkd3d_pipeline_library_disk_cache_save_pipeline_state(cache, &cache->items[i])))
        {
            /* INVALIDARG is expected for duplicates. */
            if (hr != E_INVALIDARG)
                ERR("Failed to serialize pipeline to disk cache, hr #%x.\n", hr);
        }
        else
            dirty = true;

        d3d12_pipeline_state_dec_ref(cache->items[i].state);
    }

    if (cache->stream_archive_write_file)
    {
        fclose(cache->stream_archive_write_file);
        cache->stream_archive_write_file = NULL;
    }

    vkd3d_free(tmp_items);
    return NULL;
}
