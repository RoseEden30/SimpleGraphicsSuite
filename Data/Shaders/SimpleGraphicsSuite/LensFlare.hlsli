#ifndef SIMPLEGRAPHICSSUITE_LENSFLARE_HLSLI
#define SIMPLEGRAPHICSSUITE_LENSFLARE_HLSLI

// Pseudo lens flare (John Chapman, 2013), fed only by pixels above the threshold after exposure.

static const int    SGS_LF_GHOSTS = 5;
static const float  SGS_LF_DISPERSAL = 0.35;
static const float  SGS_LF_HALO_RADIUS = 0.47;
static const float  SGS_LF_CHROMA = 0.004;
static const float  SGS_LF_THRESHOLD = 8.0;
static const float  SGS_LF_INTENSITY = 0.6;
static const float3 SGS_LF_TINT_INNER = float3(1.0, 0.9, 0.8);
static const float3 SGS_LF_TINT_OUTER = float3(0.8, 0.9, 1.0);

float3 SGS_LF_Fetch(Texture2D<float4> a_scene, SamplerState a_sampler, float2 a_uv, float2 a_chroma, float2 a_uvScale,
	float2 a_uvClamp, float a_exposure)
{
	float3 c;
	c.r = a_scene.SampleLevel(a_sampler, min(a_uvClamp, max(0.0, (a_uv + a_chroma) * a_uvScale)), 0).r;
	c.g = a_scene.SampleLevel(a_sampler, min(a_uvClamp, max(0.0, a_uv * a_uvScale)), 0).g;
	c.b = a_scene.SampleLevel(a_sampler, min(a_uvClamp, max(0.0, (a_uv - a_chroma) * a_uvScale)), 0).b;
	c *= a_exposure;

	float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
	return c * (max(lum - SGS_LF_THRESHOLD, 0.0) / max(lum, 1e-4));
}

float SGS_LF_Falloff(float2 a_uv, float a_power)
{
	return pow(saturate(1.0 - length(a_uv - 0.5) * 1.41421356), a_power);
}

float3 SGS_ComputeLensFlare(Texture2D<float4> a_scene, SamplerState a_sampler, float2 a_uv, float2 a_uvScale,
	float2 a_uvClamp, float a_exposure, float a_strength)
{
	if (a_strength <= 0.0 || !(a_exposure > 0.0))
		return 0.0;

	const float2 aspect = float2(SCREEN_INV_HEIGHT / SCREEN_INV_WIDTH, 1.0);

	float2 flipped = 1.0 - a_uv;
	float2 ghostVec = (0.5 - flipped) * SGS_LF_DISPERSAL;
	float  ghostLen = length(ghostVec);
	float2 chroma = ghostLen > 1e-5 ? ghostVec / ghostLen * SGS_LF_CHROMA : 0.0;

	float3 flare = 0.0;
	[unroll]
	for (int i = 0; i < SGS_LF_GHOSTS; ++i) {
		float2 offset = frac(flipped + ghostVec * i);
		flare += SGS_LF_Fetch(a_scene, a_sampler, offset, chroma, a_uvScale, a_uvClamp, a_exposure) * SGS_LF_Falloff(offset, 10.0);
	}

	float2 toCenter = (0.5 - flipped) * aspect;
	float  toCenterLen = length(toCenter);
	if (toCenterLen > 1e-5) {
		float2 haloUV = frac(flipped + toCenter / toCenterLen * SGS_LF_HALO_RADIUS / aspect);
		flare += SGS_LF_Fetch(a_scene, a_sampler, haloUV, chroma, a_uvScale, a_uvClamp, a_exposure) * SGS_LF_Falloff(haloUV, 5.0);
	}

	flare *= lerp(SGS_LF_TINT_INNER, SGS_LF_TINT_OUTER, saturate(length(a_uv - 0.5) * 1.41421356));

	flare /= 1.0 + flare;
	return flare * a_strength * SGS_LF_INTENSITY;
}

#endif
