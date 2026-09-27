// Per-object motion blur: reads the engine's own TAA motion vector buffer -
// includes actor/object animation, not just camera movement, since that's
// exactly what the engine needs it for.
#ifndef SIMPLEGRAPHICSSUITE_MOTIONBLUR_HLSLI
#define SIMPLEGRAPHICSSUITE_MOTIONBLUR_HLSLI

#ifndef MOTION_BLUR_AMOUNT
#define MOTION_BLUR_AMOUNT 0.0
#endif

// Depth difference, relative to the center's distance, past which a tap counts as another surface.
static const float SGS_MB_DEPTH_TOLERANCE = 0.02;

float SGS_MB_LinearDepth(float a_depth, float a_near, float a_far)
{
	return (a_far * a_near) / (a_far - a_depth * (a_far - a_near));
}

float4 SGS_ApplyMotionBlurPerObject(
	Texture2D<float4> a_colorTexture, SamplerState a_colorSampler,
	Texture2D<float2> a_motionVectorTexture, SamplerState a_motionVectorSampler,
	Texture2D<float4> a_depthTexture, SamplerState a_depthSampler,
	float2 a_uv, float2 a_uvScale, float2 a_uvClamp, float4 a_currentColor, float a_near, float a_far)
{
	// Color/motion vectors/depth are rendered at Dynamic Resolution's
	// internal size, not the full output size a_uv is in.
	float2 sampleUV = min(a_uvClamp, max(0.0, a_uvScale * a_uv));

	float2 velocity = a_motionVectorTexture.SampleLevel(a_motionVectorSampler, sampleUV, 0).xy * MOTION_BLUR_AMOUNT;

	// No blur under half a pixel (McGuire et al. 2012).
	float2 velocityPixels = velocity / float2(SCREEN_INV_WIDTH, SCREEN_INV_HEIGHT);
	if (dot(velocityPixels, velocityPixels) < 0.25)
		return a_currentColor;

	const bool depthValid = isfinite(a_near) && isfinite(a_far) && a_near > 0.0 && a_far > a_near;
	float      centerDepth = depthValid ?
	                             SGS_MB_LinearDepth(a_depthTexture.SampleLevel(a_depthSampler, sampleUV, 0).x, a_near, a_far) :
	                             0.0;
	float      depthExtent = max(centerDepth * SGS_MB_DEPTH_TOLERANCE, 1e-3);

	float4 sum = a_currentColor;
	float  weightSum = 1.0;
	[unroll]
	for (int i = 0; i < 6; ++i) {
		float2 uv = saturate(a_uv + velocity * ((float(i) - 2.5) / 6.0));
		float2 sampleStepUV = min(a_uvClamp, max(0.0, a_uvScale * uv));

		// A tap from a different surface than the center pixel (e.g. the
		// background behind a moving character's silhouette) would smear
		// that surface's color across the character instead of blurring
		// the character itself - reject/de-weight those taps.
		float weight = 1.0;
		if (depthValid) {
			float tapDepth = SGS_MB_LinearDepth(a_depthTexture.SampleLevel(a_depthSampler, sampleStepUV, 0).x, a_near, a_far);
			weight = 1.0 - saturate(abs(tapDepth - centerDepth) / depthExtent);
		}
		sum += a_colorTexture.SampleLevel(a_colorSampler, sampleStepUV, 0) * weight;
		weightSum += weight;
	}
	return sum / weightSum;
}

#endif
