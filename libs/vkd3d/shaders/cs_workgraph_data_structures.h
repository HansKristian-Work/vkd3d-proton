#ifndef CS_WORKGRAPH_DATA_STRUCTURES_H_
#define CS_WORKGRAPH_DATA_STRUCTURES_H_

// 48 bytes per node.
struct IndirectCommands
{
	uvec3 primary_execute;
	uint primary_linear_offset; // Read by node as input metadata.
	uvec3 secondary_execute;
	uint secondary_linear_offset; // Read by node as input metadata.
	uint end_elements; // Read by node as input metadata in coalesce / thread mode.
	uint linear_offset_atomic; // Used by expander to write unrolled data.
	uint expander_total_groups;
	uint padding0;
};

#endif

