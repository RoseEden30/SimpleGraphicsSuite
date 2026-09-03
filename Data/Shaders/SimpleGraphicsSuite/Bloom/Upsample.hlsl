// Widens the level below and adds it to this one. Writing to a third target
// keeps the read and the write off the same resource, so no blend state is
// needed - see Bloom.cpp.
#include "Bloom.hlsli"

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
	float3 lower = UpsampleTent(uv, SGS_TexelSize, 1.0);
	float3 here = BloomSource.Sample(BloomSampler, uv).rgb;
	return float4(here + lower, 1.0);
}
