#ifndef CS_WORKGRAPH_DATA_STRUCTURES_H_
#define CS_WORKGRAPH_DATA_STRUCTURES_H_

struct NodeCounts
{
	uint fused;
	uint total;
};

// 64 bytes per node, nicely aligns to a cache line.
struct IndirectCommands
{
	uvec3 primary_execute;
	uint primary_linear_offset; // Read by node as input metadata.
	uvec3 secondary_execute;
	uint secondary_linear_offset; // Read by node as input metadata.
	uvec3 expander_execute;
	uint end_elements; // Read by node as input metadata in coalesce / thread mode.
	uint linear_offset_atomic; // Used by expander to write unrolled data.
	uint total_fused_elements;
	uint padding0;
	uint padding1;
};

#endif

