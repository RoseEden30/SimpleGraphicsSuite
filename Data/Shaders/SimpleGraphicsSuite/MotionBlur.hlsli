// Per-object motion blur: reads the engine's own TAA motion vector buffer -
// includes actor/object animation, not just camera movement, since that's
// exactly what the engine needs it for.
#ifndef SIMPLEGRAPHICSSUITE_MOTIONBLUR_HLSLI
#define SIMPLEGRAPHICSSUITE_MOTIONBLUR_HLSLI

#ifndef MOTION_BLUR_AMOUNT
#define MOTION_BLUR_AMOUNT 0.0
#endif

float4 SGS_ApplyMotionBlurPerObject(
	Texture2D<float4> a_colorTexture, SamplerState a_colorSampler,
	Texture2D<float2> a_motionVectorTexture, SamplerState a_motionVectorSampler,
	Texture2D<float4> a_depthTexture, SamplerState a_depthSampler,
	float2 a_uv, float2 a_uvScale, float2 a_uvClamp, float4 a_currentColor)
{
	// Color/motion vectors/depth are rendered at Dynamic Resolution's
	// internal size, not the full output size a_uv is in.
	float2 sampleUV = min(a_uvClamp, max(0.0, a_uvScale * a_uv));
	float2 motion = a_motionVectorTexture.SampleLevel(a_motionVectorSampler, sampleUV, 0).xy;
	float2 velocity = motion * MOTION_BLUR_AMOUNT;
	float  centerDepth = a_depthTexture.SampleLevel(a_depthSampler, sampleUV, 0).x;

	float4 sum = a_currentColor;
	float  weightSum = 1.0;
	[unroll]
	for (int i = 1; i <= 6; ++i) {
		float2 uv = saturate(a_uv - velocity * (float(i) / 6.0));
		float2 sampleStepUV = min(a_uvClamp, max(0.0, a_uvScale * uv));
		float  tapDepth = a_depthTexture.SampleLevel(a_depthSampler, sampleStepUV, 0).x;

		// A tap from a different surface than the center pixel (e.g. the
		// background behind a moving character's silhouette) would smear
		// that surface's color across the character instead of blurring
		// the character itself - reject/de-weight those taps.
		float weight = 1.0 - saturate(abs(tapDepth - centerDepth) * 400.0);
		sum += a_colorTexture.SampleLevel(a_colorSampler, sampleStepUV, 0) * weight;
		weightSum += weight;
	}
	return sum / weightSum;
}

#endif
