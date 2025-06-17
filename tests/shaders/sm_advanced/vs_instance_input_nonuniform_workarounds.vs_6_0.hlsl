struct CBVData { float4 v; };
ConstantBuffer<CBVData> Bufs[] : register(b0);
StructuredBuffer<uint> Indices : register(t0, space1);

float4 main(uint per_vertex : PERVERTEX, uint per_instance : PERINSTANCE) : SV_Position
{
	uint first = WaveReadLaneFirst(per_instance);
	uint loaded;
	if (WaveActiveAllTrue(first == per_instance))
		loaded = Indices[first];
	else
		loaded = Indices[per_instance];

	// AMD d3d12 drivers auto-promote these nonuniform CBV accesses always,
	// and at least one game accidentally relies on this behavior ...
	// Quite aggressive analysis.
	return Bufs[loaded].v + float4(0, 0, 0, float(per_vertex));
}
