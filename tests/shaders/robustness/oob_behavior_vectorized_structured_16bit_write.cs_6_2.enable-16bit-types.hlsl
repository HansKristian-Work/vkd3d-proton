RWStructuredBuffer<uint16_t1> Writeback1 : register(u0);
RWStructuredBuffer<uint16_t2> Writeback2 : register(u1);
RWStructuredBuffer<uint16_t3> Writeback3 : register(u2);
RWStructuredBuffer<uint16_t4> Writeback4 : register(u3);

RWStructuredBuffer<uint16_t2> WritebackPartial2 : register(u4);
RWStructuredBuffer<uint16_t3> WritebackPartial3 : register(u5);
RWStructuredBuffer<uint16_t4> WritebackPartial4 : register(u6);

[numthreads(64, 1, 1)]
void main(uint thr : SV_DispatchThreadID)
{
	Writeback1[thr] = uint16_t(thr);
	Writeback2[thr] = uint16_t2(2 * thr + uint2(0, 1));
	Writeback3[thr] = uint16_t3(3 * thr + uint3(0, 1, 2));
	Writeback4[thr] = uint16_t4(4 * thr + uint4(0, 1, 2, 3));

	WritebackPartial2[thr / 2][thr % 2] = uint16_t(thr);
	WritebackPartial3[thr / 3][thr % 3] = uint16_t(thr);
	WritebackPartial4[thr / 4][thr % 4] = uint16_t(thr);
}

