#include "Bloom.hlsli"

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
	return float4(Downsample13(uv, SGS_TexelSize, false), 1.0);
}
