Texture2D<uint> SRVTex : register(t0);
Texture2DArray<uint> SRVTexArray : register(t1);
Buffer<uint> SRVTyped : register(t2);
ByteAddressBuffer SRVRaw : register(t3);
StructuredBuffer<uint> SRVStructured : register(t4);

RWTexture2D<uint> UAVTex : register(u5);
RWTexture2DArray<uint> UAVTexArray : register(u6);
RWBuffer<uint> UAVTyped : register(u7);
RWByteAddressBuffer UAVRaw : register(u8);
RWStructuredBuffer<uint> UAVStructured : register(u9);

struct Output
{
	uint width;
	uint height;
	uint layers;
	uint levels;
};
RWStructuredBuffer<Output> Outputs : register(u10);

[numthreads(1, 1, 1)]
void main()
{
	Output output;

	SRVTex.GetDimensions(0, output.width, output.height, output.levels);
	output.layers = ~0;
	Outputs[0] = output;

	SRVTexArray.GetDimensions(0, output.width, output.height, output.layers, output.levels);
	Outputs[1] = output;

	SRVTyped.GetDimensions(output.width);
	output.height = ~0;
	output.layers = ~0;
	output.levels = ~0;
	Outputs[2] = output;

	SRVRaw.GetDimensions(output.width);
	Outputs[3] = output;

	SRVStructured.GetDimensions(output.width, output.height);
	Outputs[4] = output;

	UAVTex.GetDimensions(output.width, output.height);
	output.layers = ~0;
	output.levels = ~0;
	Outputs[5] = output;

	UAVTexArray.GetDimensions(output.width, output.height, output.layers);
	output.levels = ~0;
	Outputs[6] = output;

	UAVTyped.GetDimensions(output.width);
	output.height = ~0;
	output.layers = ~0;
	output.levels = ~0;
	Outputs[7] = output;

	UAVRaw.GetDimensions(output.width);
	Outputs[8] = output;

	UAVStructured.GetDimensions(output.width, output.height);
	Outputs[9] = output;
}
