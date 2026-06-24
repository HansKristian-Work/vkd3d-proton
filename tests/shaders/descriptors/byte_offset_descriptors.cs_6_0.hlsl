// Test all variants of {typed, raw, structured} x {srv, uav} x {implicit size vs explicit size}
StructuredBuffer<uint3> ROStruct : register(t0);
ByteAddressBuffer ROBAB : register(t1);
Buffer<uint> ROTexBuffer : register(t2);

StructuredBuffer<uint3> ROStructImplicit : register(t3);
ByteAddressBuffer ROBABImplicit : register(t4);
Buffer<uint> ROTexBufferImplicit : register(t5);

RWStructuredBuffer<uint3> RWStruct : register(u0);
RWByteAddressBuffer RWBAB : register(u1);
RWBuffer<uint> RWTexBuffer : register(u2);

RWStructuredBuffer<uint3> RWStructImplicit : register(u3);
RWByteAddressBuffer RWBABImplicit : register(u4);
RWBuffer<uint> RWTexBufferImplicit : register(u5);

RWByteAddressBuffer Output : register(u6);

[numthreads(1, 1, 1)]
void main()
{
	uint stride;
	uint dims[12];
	uint values[12];

	ROStruct.GetDimensions(dims[0], stride);
	ROBAB.GetDimensions(dims[1]);
	ROTexBuffer.GetDimensions(dims[2]);

	ROStructImplicit.GetDimensions(dims[3], stride);
	ROBABImplicit.GetDimensions(dims[4]);
	ROTexBufferImplicit.GetDimensions(dims[5]);

	RWStruct.GetDimensions(dims[6], stride);
	RWBAB.GetDimensions(dims[7]);
	RWTexBuffer.GetDimensions(dims[8]);

	RWStructImplicit.GetDimensions(dims[9], stride);
	RWBABImplicit.GetDimensions(dims[10]);
	RWTexBufferImplicit.GetDimensions(dims[11]);

	values[0] = ROStruct[1].x;
	values[1] = ROBAB.Load(4);
	values[2] = ROTexBuffer[1];
	values[3] = ROStructImplicit[1].x;
	values[4] = ROBABImplicit.Load(4);
	values[5] = ROTexBufferImplicit[1];

	values[6] = RWStruct[1].x;
	values[7] = RWBAB.Load(4);
	values[8] = RWTexBuffer[1];
	values[9] = RWStructImplicit[1].x;
	values[10] = RWBABImplicit.Load(4);
	values[11] = RWTexBufferImplicit[1];

	RWStruct.IncrementCounter();
	RWStructImplicit.IncrementCounter();
	RWStruct.IncrementCounter();
	RWStructImplicit.IncrementCounter();

	for (int i = 0; i < 12; i++)
		Output.Store2(8 * i, uint2(values[i], dims[i]));
}
