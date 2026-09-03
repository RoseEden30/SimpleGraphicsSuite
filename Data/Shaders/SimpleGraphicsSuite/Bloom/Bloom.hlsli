// Progressive downsample/upsample bloom, following Jorge Jimenez's "Next
// Generation Post Processing in Call of Duty: Advanced Warfare" (SIGGRAPH
// 2014). The engine's own bloom buffer is a single fixed-radius blur of the
// whole frame, which lifts everything evenly; a mip chain gives a wide, soft
// falloff that stays anchored on the highlights.
#ifndef SIMPLEGRAPHICSSUITE_BLOOM_HLSLI
#define SIMPLEGRAPHICSSUITE_BLOOM_HLSLI

Texture2D<float4>  BloomSource : register(t0);
Texture2D<float4>  BloomLower : register(t1);
SamplerState       BloomSampler : register(s0);

cbuffer BloomConstants : register(b0)
{
	float2 SGS_TexelSize;   // 1 / source size
	float  SGS_Threshold;   // bright-pass cutoff in luma
	float  SGS_Knee;        // width of the soft shoulder under the cutoff
	// Under dynamic resolution the scene only fills part of the buffer it is
	// rendered into, so the first pass has to stay inside that region.
	float2 SGS_UVScale;     // maps [0,1] onto the part actually rendered
	float2 SGS_UVMax;       // last tap position still inside it
};

float3 TapSource(float2 a_uv)
{
	return BloomSource.Sample(BloomSampler, min(a_uv, SGS_UVMax)).rgb;
}

static const float3 kLumaWeights = float3(0.2126, 0.7152, 0.0722);

// Karis average: weight each tap by 1/(1+luma) before averaging so a single
// very bright pixel can't dominate the mip and strobe between frames. Only
// worth it on the first downsample, where fireflies still exist.
float KarisWeight(float3 a_color)
{
	return rcp(1.0 + dot(a_color, kLumaWeights));
}

// Quadratic soft knee, the standard bright-pass curve: everything under
// threshold-knee is dropped, everything over threshold+knee passes through,
// and the region between ramps up smoothly instead of leaving a hard edge.
float3 Prefilter(float3 a_color)
{
	float luma = dot(a_color, kLumaWeights);
	float soft = luma - SGS_Threshold + SGS_Knee;
	soft = clamp(soft, 0.0, 2.0 * SGS_Knee);
	soft = soft * soft * rcp(4.0 * SGS_Knee + 1e-6);
	return a_color * max(soft, luma - SGS_Threshold) * rcp(max(luma, 1e-6));
}

// 13-tap downsample: four inner taps at half a texel and a 3x3 ring, weighted
// so the result is free of the pulsing a plain box filter gives on motion.
float3 Downsample13(float2 a_uv, float2 a_texelSize, bool a_karis)
{
	float2 o = a_texelSize;

	float3 a = TapSource(a_uv + float2(-2, 2) * o);
	float3 b = TapSource(a_uv + float2( 0, 2) * o);
	float3 c = TapSource(a_uv + float2( 2, 2) * o);
	float3 d = TapSource(a_uv + float2(-2, 0) * o);
	float3 e = TapSource(a_uv);
	float3 f = TapSource(a_uv + float2( 2, 0) * o);
	float3 g = TapSource(a_uv + float2(-2,-2) * o);
	float3 h = TapSource(a_uv + float2( 0,-2) * o);
	float3 i = TapSource(a_uv + float2( 2,-2) * o);

	float3 j = TapSource(a_uv + float2(-1, 1) * o);
	float3 k = TapSource(a_uv + float2( 1, 1) * o);
	float3 l = TapSource(a_uv + float2(-1,-1) * o);
	float3 m = TapSource(a_uv + float2( 1,-1) * o);

	if (a_karis) {
		// Average each 2x2 group on its own, so the weighting is applied
		// where the fireflies actually are rather than across the whole tap
		// set - the form Jimenez's talk uses.
		float3 groups[5] = {
			(a + b + d + e) * 0.25,
			(b + c + e + f) * 0.25,
			(d + e + g + h) * 0.25,
			(e + f + h + i) * 0.25,
			(j + k + l + m) * 0.25
		};
		const float weights[5] = { 0.125, 0.125, 0.125, 0.125, 0.5 };

		float3 sum = 0.0;
		float  total = 0.0;
		[unroll] for (int n = 0; n < 5; ++n) {
			float w = weights[n] * KarisWeight(groups[n]);
			sum += groups[n] * w;
			total += w;
		}
		return sum * rcp(max(total, 1e-6));
	}

	float3 result = e * 0.125;
	result += (a + c + g + i) * 0.03125;
	result += (b + d + f + h) * 0.0625;
	result += (j + k + l + m) * 0.125;
	return result;
}

// 3x3 tent used on the way back up. Its radius is in texels of the target,
// which is why each level widens the glow instead of just rescaling it.
float3 UpsampleTent(float2 a_uv, float2 a_texelSize, float a_radius)
{
	float2 o = a_texelSize * a_radius;

	float3 sum = BloomLower.Sample(BloomSampler, a_uv + float2(-1, 1) * o).rgb;
	sum += BloomLower.Sample(BloomSampler, a_uv + float2( 0, 1) * o).rgb * 2.0;
	sum += BloomLower.Sample(BloomSampler, a_uv + float2( 1, 1) * o).rgb;
	sum += BloomLower.Sample(BloomSampler, a_uv + float2(-1, 0) * o).rgb * 2.0;
	sum += BloomLower.Sample(BloomSampler, a_uv).rgb * 4.0;
	sum += BloomLower.Sample(BloomSampler, a_uv + float2( 1, 0) * o).rgb * 2.0;
	sum += BloomLower.Sample(BloomSampler, a_uv + float2(-1,-1) * o).rgb;
	sum += BloomLower.Sample(BloomSampler, a_uv + float2( 0,-1) * o).rgb * 2.0;
	sum += BloomLower.Sample(BloomSampler, a_uv + float2( 1,-1) * o).rgb;

	return sum * (1.0 / 16.0);
}

#endif
