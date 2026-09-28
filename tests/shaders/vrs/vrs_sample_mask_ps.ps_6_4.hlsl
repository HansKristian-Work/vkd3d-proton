struct PSOut
{
	uint color : SV_Target;
	uint cov : SV_Coverage;
};

cbuffer cbuf : register(b0)
{
	uint outmask;
};

PSOut main(uint mask : SV_Coverage)
{
	PSOut psout = (PSOut)0;
	psout.color = mask;
	psout.cov = outmask;
	return psout;
}

