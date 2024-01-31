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

#ifndef DEBUG_CHANNEL_H_
#define DEBUG_CHANNEL_H_

#extension GL_EXT_buffer_reference : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_ballot : require
#ifdef DEBUG_CHANNEL_HELPER_LANES
#extension GL_EXT_demote_to_helper_invocation : require
#endif

layout(buffer_reference, std430, buffer_reference_align = 4) coherent buffer ControlBlock
{
	uint message_counter;
	uint instance_counter;
};

layout(buffer_reference, std430, buffer_reference_align = 4) coherent buffer RingBuffer
{
	uint data[];
};

layout(constant_id = 0) const uint64_t DEBUG_SHADER_HASH = 0;
layout(constant_id = 1) const uint64_t DEBUG_SHADER_ATOMIC_BDA = 0;
layout(constant_id = 2) const uint64_t DEBUG_SHADER_RING_BDA = 0;
layout(constant_id = 3) const uint DEBUG_SHADER_RING_SIZE = 0;
const uint DEBUG_SHADER_RING_MASK = DEBUG_SHADER_RING_SIZE - 1;
const bool DEBUG_SHADER_RING_ACTIVE = DEBUG_SHADER_ATOMIC_BDA != 0;

const uint DEBUG_CHANNEL_FMT_HEX = 0;
const uint DEBUG_CHANNEL_FMT_I32 = 1;
const uint DEBUG_CHANNEL_FMT_F32 = 2;
const uint DEBUG_CHANNEL_FMT_HEX_ALL = DEBUG_CHANNEL_FMT_HEX * 0x55555555u;
const uint DEBUG_CHANNEL_FMT_I32_ALL = DEBUG_CHANNEL_FMT_I32 * 0x55555555u;
const uint DEBUG_CHANNEL_FMT_F32_ALL = DEBUG_CHANNEL_FMT_F32 * 0x55555555u;
const uint DEBUG_CHANNEL_WORD_COOKIE = 0xdeadca70u; /* Let host fish for this cookie in device lost scenarios. */

uint DEBUG_CHANNEL_INSTANCE_COUNTER;
uvec3 DEBUG_CHANNEL_ID;

/* Need to make sure the elected subgroup can have side effects. */
#ifdef DEBUG_CHANNEL_HELPER_LANES
bool DEBUG_CHANNEL_ELECT()
{
	bool elected = false;
	if (!helperInvocationEXT())
		elected = subgroupElect();
	return elected;
}
#else
bool DEBUG_CHANNEL_ELECT()
{
	return subgroupElect();
}
#endif

void DEBUG_CHANNEL_INIT(uvec3 id)
{
	if (!DEBUG_SHADER_RING_ACTIVE)
		return;
	DEBUG_CHANNEL_ID = id;
	uint inst;
#ifdef DEBUG_CHANNEL_HELPER_LANES
	if (!helperInvocationEXT())
	{
		/* Elect and broadcast must happen without helper lanes here.
		 * We must perform the instance increment with side effects,
		 * and broadcast first must pick the elected lane. */
		if (subgroupElect())
			inst = atomicAdd(ControlBlock(DEBUG_SHADER_ATOMIC_BDA).instance_counter, 1u);
		DEBUG_CHANNEL_INSTANCE_COUNTER = subgroupBroadcastFirst(inst);
	}
	/* Helper lanes cannot write debug messages, since they cannot have side effects.
	 * Leave it undefined, and we should ensure SGPR propagation either way ... */
#else
	if (DEBUG_CHANNEL_ELECT())
		inst = atomicAdd(ControlBlock(DEBUG_SHADER_ATOMIC_BDA).instance_counter, 1u);
	DEBUG_CHANNEL_INSTANCE_COUNTER = subgroupBroadcastFirst(inst);
#endif
}

void DEBUG_CHANNEL_INIT_IMPLICIT_INSTANCE(uvec3 id, uint inst)
{
	if (!DEBUG_SHADER_RING_ACTIVE)
		return;
	DEBUG_CHANNEL_ID = id;
	DEBUG_CHANNEL_INSTANCE_COUNTER = inst;
}

void DEBUG_CHANNEL_UNLOCK_MESSAGE(RingBuffer buf, uint offset, uint num_words)
{
	memoryBarrierBuffer();
	/* Make sure this word is made visible last. This way the ring thread can avoid reading bogus messages.
	 * If the host thread observed a num_word of 0, we know a message was allocated, but we don't necessarily
	 * have a complete write yet.
	 * In a device lost scenario, we can try to fish for valid messages. */
	buf.data[(offset + 0) & DEBUG_SHADER_RING_MASK] = num_words | DEBUG_CHANNEL_WORD_COOKIE;
	memoryBarrierBuffer();
}

void DEBUG_CHANNEL_WRITE_HEADER(RingBuffer buf, uint offset, uint fmt)
{
	buf.data[(offset + 1) & DEBUG_SHADER_RING_MASK] = uint(DEBUG_SHADER_HASH);
	buf.data[(offset + 2) & DEBUG_SHADER_RING_MASK] = uint(DEBUG_SHADER_HASH >> 32);
	buf.data[(offset + 3) & DEBUG_SHADER_RING_MASK] = DEBUG_CHANNEL_INSTANCE_COUNTER;
	buf.data[(offset + 4) & DEBUG_SHADER_RING_MASK] = DEBUG_CHANNEL_ID.x;
	buf.data[(offset + 5) & DEBUG_SHADER_RING_MASK] = DEBUG_CHANNEL_ID.y;
	buf.data[(offset + 6) & DEBUG_SHADER_RING_MASK] = DEBUG_CHANNEL_ID.z;
	buf.data[(offset + 7) & DEBUG_SHADER_RING_MASK] = fmt;
}

uint DEBUG_CHANNEL_ALLOCATE(uint words)
{
	uint offset = atomicAdd(ControlBlock(DEBUG_SHADER_ATOMIC_BDA).message_counter, words);
	return offset;
}

void DEBUG_CHANNEL_MSG_()
{
	if (!DEBUG_SHADER_RING_ACTIVE)
		return;
	uint words = 8;
	uint offset = DEBUG_CHANNEL_ALLOCATE(words);
	RingBuffer buf = RingBuffer(DEBUG_SHADER_RING_BDA);
	DEBUG_CHANNEL_WRITE_HEADER(buf, offset, 0);
	DEBUG_CHANNEL_UNLOCK_MESSAGE(buf, offset, words);
}

void DEBUG_CHANNEL_MSG_(uint fmt, uint v0)
{
	if (!DEBUG_SHADER_RING_ACTIVE)
		return;
	RingBuffer buf = RingBuffer(DEBUG_SHADER_RING_BDA);
	uint words = 9;
	uint offset = DEBUG_CHANNEL_ALLOCATE(words);
	DEBUG_CHANNEL_WRITE_HEADER(buf, offset, fmt);
	buf.data[(offset + 8) & DEBUG_SHADER_RING_MASK] = v0;
	DEBUG_CHANNEL_UNLOCK_MESSAGE(buf, offset, words);
}

void DEBUG_CHANNEL_MSG_(uint fmt, uint v0, uint v1)
{
	if (!DEBUG_SHADER_RING_ACTIVE)
		return;
	RingBuffer buf = RingBuffer(DEBUG_SHADER_RING_BDA);
	uint words = 10;
	uint offset = DEBUG_CHANNEL_ALLOCATE(words);
	DEBUG_CHANNEL_WRITE_HEADER(buf, offset, fmt);
	buf.data[(offset + 8) & DEBUG_SHADER_RING_MASK] = v0;
	buf.data[(offset + 9) & DEBUG_SHADER_RING_MASK] = v1;
	DEBUG_CHANNEL_UNLOCK_MESSAGE(buf, offset, words);
}

void DEBUG_CHANNEL_MSG_(uint fmt, uint v0, uint v1, uint v2)
{
	if (!DEBUG_SHADER_RING_ACTIVE)
		return;
	RingBuffer buf = RingBuffer(DEBUG_SHADER_RING_BDA);
	uint words = 11;
	uint offset = DEBUG_CHANNEL_ALLOCATE(words);
	DEBUG_CHANNEL_WRITE_HEADER(buf, offset, fmt);
	buf.data[(offset + 8) & DEBUG_SHADER_RING_MASK] = v0;
	buf.data[(offset + 9) & DEBUG_SHADER_RING_MASK] = v1;
	buf.data[(offset + 10) & DEBUG_SHADER_RING_MASK] = v2;
	DEBUG_CHANNEL_UNLOCK_MESSAGE(buf, offset, words);
}

void DEBUG_CHANNEL_MSG_(uint fmt, uint v0, uint v1, uint v2, uint v3)
{
	if (!DEBUG_SHADER_RING_ACTIVE)
		return;
	RingBuffer buf = RingBuffer(DEBUG_SHADER_RING_BDA);
	uint words = 12;
	uint offset = DEBUG_CHANNEL_ALLOCATE(words);
	DEBUG_CHANNEL_WRITE_HEADER(buf, offset, fmt);
	buf.data[(offset + 8) & DEBUG_SHADER_RING_MASK] = v0;
	buf.data[(offset + 9) & DEBUG_SHADER_RING_MASK] = v1;
	buf.data[(offset + 10) & DEBUG_SHADER_RING_MASK] = v2;
	buf.data[(offset + 11) & DEBUG_SHADER_RING_MASK] = v3;
	DEBUG_CHANNEL_UNLOCK_MESSAGE(buf, offset, words);
}

void DEBUG_CHANNEL_MSG()
{
	DEBUG_CHANNEL_MSG_();
}

void DEBUG_CHANNEL_MSG(uint v0)
{
	DEBUG_CHANNEL_MSG_(DEBUG_CHANNEL_FMT_HEX_ALL, v0);
}

void DEBUG_CHANNEL_MSG(uint v0, uint v1)
{
	DEBUG_CHANNEL_MSG_(DEBUG_CHANNEL_FMT_HEX_ALL, v0, v1);
}

void DEBUG_CHANNEL_MSG(uint v0, uint v1, uint v2)
{
	DEBUG_CHANNEL_MSG_(DEBUG_CHANNEL_FMT_HEX_ALL, v0, v1, v2);
}

void DEBUG_CHANNEL_MSG(uint v0, uint v1, uint v2, uint v3)
{
	DEBUG_CHANNEL_MSG_(DEBUG_CHANNEL_FMT_HEX_ALL, v0, v1, v2, v3);
}

void DEBUG_CHANNEL_MSG(int v0)
{
	DEBUG_CHANNEL_MSG_(DEBUG_CHANNEL_FMT_I32_ALL, v0);
}

void DEBUG_CHANNEL_MSG(int v0, int v1)
{
	DEBUG_CHANNEL_MSG_(DEBUG_CHANNEL_FMT_I32_ALL, v0, v1);
}

void DEBUG_CHANNEL_MSG(int v0, int v1, int v2)
{
	DEBUG_CHANNEL_MSG_(DEBUG_CHANNEL_FMT_I32_ALL, v0, v1, v2);
}

void DEBUG_CHANNEL_MSG(int v0, int v1, int v2, int v3)
{
	DEBUG_CHANNEL_MSG_(DEBUG_CHANNEL_FMT_I32_ALL, v0, v1, v2, v3);
}

void DEBUG_CHANNEL_MSG(float v0)
{
	DEBUG_CHANNEL_MSG_(DEBUG_CHANNEL_FMT_F32_ALL, floatBitsToUint(v0));
}

void DEBUG_CHANNEL_MSG(float v0, float v1)
{
	DEBUG_CHANNEL_MSG_(DEBUG_CHANNEL_FMT_F32_ALL, floatBitsToUint(v0), floatBitsToUint(v1));
}

void DEBUG_CHANNEL_MSG(float v0, float v1, float v2)
{
	DEBUG_CHANNEL_MSG_(DEBUG_CHANNEL_FMT_F32_ALL, floatBitsToUint(v0), floatBitsToUint(v1), floatBitsToUint(v2));
}

void DEBUG_CHANNEL_MSG(float v0, float v1, float v2, float v3)
{
	DEBUG_CHANNEL_MSG_(DEBUG_CHANNEL_FMT_F32_ALL, floatBitsToUint(v0), floatBitsToUint(v1), floatBitsToUint(v2), floatBitsToUint(v3));
}

void DEBUG_CHANNEL_MSG_UNIFORM(uint v0)
{
    if (DEBUG_CHANNEL_ELECT())
        DEBUG_CHANNEL_MSG(v0);
    subgroupBarrier();
}

void DEBUG_CHANNEL_MSG_UNIFORM(uint v0, uint v1)
{
    if (DEBUG_CHANNEL_ELECT())
        DEBUG_CHANNEL_MSG(v0, v1);
    subgroupBarrier();
}

void DEBUG_CHANNEL_MSG_UNIFORM(uint v0, uint v1, uint v2)
{
    if (DEBUG_CHANNEL_ELECT())
        DEBUG_CHANNEL_MSG(v0, v1, v2);
    subgroupBarrier();
}

void DEBUG_CHANNEL_MSG_UNIFORM(uint v0, uint v1, uint v2, uint v3)
{
    if (DEBUG_CHANNEL_ELECT())
        DEBUG_CHANNEL_MSG(v0, v1, v2, v3);
    subgroupBarrier();
}

void DEBUG_CHANNEL_MSG_UNIFORM(int v0)
{
    if (DEBUG_CHANNEL_ELECT())
        DEBUG_CHANNEL_MSG(v0);
    subgroupBarrier();
}

void DEBUG_CHANNEL_MSG_UNIFORM(int v0, int v1)
{
    if (DEBUG_CHANNEL_ELECT())
        DEBUG_CHANNEL_MSG(v0, v1);
    subgroupBarrier();
}

void DEBUG_CHANNEL_MSG_UNIFORM(int v0, int v1, int v2)
{
    if (DEBUG_CHANNEL_ELECT())
        DEBUG_CHANNEL_MSG(v0, v1, v2);
    subgroupBarrier();
}

void DEBUG_CHANNEL_MSG_UNIFORM(int v0, int v1, int v2, int v3)
{
    if (DEBUG_CHANNEL_ELECT())
        DEBUG_CHANNEL_MSG(v0, v1, v2, v3);
    subgroupBarrier();
}

void DEBUG_CHANNEL_MSG_UNIFORM(float v0)
{
    if (DEBUG_CHANNEL_ELECT())
        DEBUG_CHANNEL_MSG(v0);
    subgroupBarrier();
}

void DEBUG_CHANNEL_MSG_UNIFORM(float v0, float v1)
{
    if (DEBUG_CHANNEL_ELECT())
        DEBUG_CHANNEL_MSG(v0, v1);
    subgroupBarrier();
}

void DEBUG_CHANNEL_MSG_UNIFORM(float v0, float v1, float v2)
{
    if (DEBUG_CHANNEL_ELECT())
        DEBUG_CHANNEL_MSG(v0, v1, v2);
    subgroupBarrier();
}

void DEBUG_CHANNEL_MSG_UNIFORM(float v0, float v1, float v2, float v3)
{
    if (DEBUG_CHANNEL_ELECT())
        DEBUG_CHANNEL_MSG(v0, v1, v2, v3);
    subgroupBarrier();
}

#endif
