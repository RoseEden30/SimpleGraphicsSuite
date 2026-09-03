// Bright-pass into the top of the chain, at half resolution. SGS_UVScale maps
// the full output onto whatever part of the source the scene was rendered
// into, so a reduced render scale doesn't drag the empty region in.
#include "Bloom.hlsli"

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
	float3 color = Downsample13(uv * SGS_UVScale, SGS_TexelSize, true);
	return float4(Prefilter(color), 1.0);
}
