// Directional screen-space contact shadows outdoors (ray toward the sun) and
// point-light contact shadows indoors (short ray toward each nearby light).
#ifndef SIMPLEGRAPHICSSUITE_CONTACTSHADOWS_HLSLI
#define SIMPLEGRAPHICSSUITE_CONTACTSHADOWS_HLSLI

#ifndef VR

#define SGS_CS_MAX_LIGHTS 4

// Elements 0..MAX-1: nearby point lights (xyz world pos, w range).
// Element MAX: x = light count, y = interior flag.
Buffer<float4> SGS_CSLightData : register(t10);

static const int   SGS_CS_STEPS = 16;
static const float SGS_CS_LENGTH_FRAC = 0.03;
static const float SGS_CS_START_FRAC = 0.0025;
static const float SGS_CS_THICKNESS_FRAC = 0.02;
static const float SGS_CS_BIAS_FRAC = 0.004;
static const float SGS_CS_EDGE_FADE = 0.3;
static const float SGS_CS_FADE_START = 1500.0;
static const float SGS_CS_FADE_END = 4000.0;

float SGS_CS_Dither(float2 pixel)
{
	return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// Non-reversed depth: 0=near, 1=far.
float SGS_CS_Linearize(float d, float near, float far)
{
	return (far * near) / (far - d * (far - near));
}

// March from a view-space point toward a_dirVS and return occlusion in [0,1].
// Stops at the first occluder it goes behind so shadows stay grounded contacts.
float SGS_CS_March(
	Texture2D<float4> a_depthTex, SamplerState a_depthSampler,
	float2 a_depthUVScale, float2 a_depthUVClamp,
	float3 a_originViewPos, float3 a_dirVS, float a_maxLen, float a_dither,
	row_major float4x4 a_projMatrix, float a_near, float a_far)
{
	float originDist = length(a_originViewPos);
	float startT = originDist * SGS_CS_START_FRAC;
	float stepLength = a_maxLen / float(SGS_CS_STEPS);

	[loop]
	for (int i = 0; i < SGS_CS_STEPS; ++i) {
		float  t = startT + (float(i) + a_dither) * stepLength;
		float3 samplePos = a_originViewPos + a_dirVS * t;

		float4 clip = mul(a_projMatrix, float4(samplePos, 1.0));
		if (clip.w <= 1e-5)
			break;
		float2 sampleUV = (clip.xy / clip.w) * float2(0.5, -0.5) + 0.5;
		if (any(sampleUV < 0.0) || any(sampleUV > 1.0))
			break;

		float2 sampleDepthUV = min(a_depthUVClamp, max(0.0, a_depthUVScale * sampleUV));
		float  sceneDepth = a_depthTex.SampleLevel(a_depthSampler, sampleDepthUV, 0).x;
		if (sceneDepth >= 1.0)
			continue;

		float rayDist = SGS_CS_Linearize(saturate(clip.z / clip.w), a_near, a_far);
		float sceneDist = SGS_CS_Linearize(sceneDepth, a_near, a_far);
		float diff = rayDist - sceneDist;

		float bias = rayDist * SGS_CS_BIAS_FRAC;
		if (diff > bias) {
			float thickness = rayDist * SGS_CS_THICKNESS_FRAC;
			float occ = 0.0;
			if (diff < thickness) {
				float mid = 0.5 * (bias + thickness);
				float window = smoothstep(0.0, 1.0, 1.0 - saturate(abs(diff - mid) / max(mid - bias, 1e-4)));
				float leave = saturate((1.0 - t / max(a_maxLen, 1e-4)) / SGS_CS_EDGE_FADE);
				occ = window * leave;
			}
			return occ;  // blocked; stop before the shadow can trail past the edge
		}
	}
	return 0.0;
}

float SGS_ContactShadow(
	Texture2D<float4> a_depthTex, SamplerState a_depthSampler,
	float2 a_uv, float2 a_pixel, float2 a_depthUVScale, float2 a_depthUVClamp,
	float3 a_lightDirWorld,
	row_major float4x4 a_viewMatrix, row_major float4x4 a_projMatrix, row_major float4x4 a_invProjMatrix,
	float a_near, float a_far, float a_strength)
{
	if (a_strength <= 0.0 || !isfinite(a_near) || !isfinite(a_far) || a_near <= 0.0 || a_far <= a_near)
		return 1.0;

	float2 originDepthUV = min(a_depthUVClamp, max(0.0, a_depthUVScale * a_uv));
	float  originDepth = a_depthTex.SampleLevel(a_depthSampler, originDepthUV, 0).x;
	if (originDepth >= 1.0)
		return 1.0;

	float2 originNDC = a_uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
	float4 originH = mul(a_invProjMatrix, float4(originNDC, originDepth, 1.0));
	if (abs(originH.w) < 1e-6)
		return 1.0;
	float3 originViewPos = originH.xyz / originH.w;

	float originDist = length(originViewPos);
	float distFade = 1.0 - saturate((originDist - SGS_CS_FADE_START) / (SGS_CS_FADE_END - SGS_CS_FADE_START));
	if (distFade <= 0.0)
		return 1.0;

	float reach = originDist * SGS_CS_LENGTH_FRAC;
	float dither = SGS_CS_Dither(a_pixel);
	float occlusion = 0.0;

	float4 info = SGS_CSLightData[SGS_CS_MAX_LIGHTS];
	if (info.y > 0.5) {
		int count = min((int)info.x, SGS_CS_MAX_LIGHTS);
		float weightSum = 0.0;
		float occSum = 0.0;
		[loop]
		for (int i = 0; i < count; ++i) {
			float4 light = SGS_CSLightData[i];
			float3 lightViewPos = mul(a_viewMatrix, float4(light.xyz, 1.0)).xyz;
			float3 toLight = lightViewPos - originViewPos;
			float  dist = length(toLight);
			float  range = light.w;
			if (dist < 1e-3 || dist > range)
				continue;

			float weight = saturate(1.0 - dist / range);
			weight *= weight;
			float occ = SGS_CS_March(a_depthTex, a_depthSampler, a_depthUVScale, a_depthUVClamp,
				originViewPos, toLight / dist, min(reach, dist), dither, a_projMatrix, a_near, a_far);
			weightSum += weight;
			occSum += weight * occ;
		}
		occlusion = weightSum > 0.0 ? occSum / weightSum : 0.0;
	} else {
		float3 lightViewDir = mul(a_viewMatrix, float4(a_lightDirWorld, 0.0)).xyz;
		float  len = length(lightViewDir);
		if (len < 1e-4)
			return 1.0;
		occlusion = SGS_CS_March(a_depthTex, a_depthSampler, a_depthUVScale, a_depthUVClamp,
			originViewPos, lightViewDir / len, reach, dither, a_projMatrix, a_near, a_far);
	}

	return 1.0 - occlusion * a_strength * distFade;
}

#else

float SGS_ContactShadow(
	Texture2D<float4> a_depthTex, SamplerState a_depthSampler,
	float2 a_uv, float2 a_pixel, float2 a_depthUVScale, float2 a_depthUVClamp,
	float3 a_lightDirWorld,
	row_major float4x4 a_viewMatrix, row_major float4x4 a_projMatrix, row_major float4x4 a_invProjMatrix,
	float a_near, float a_far, float a_strength)
{
	return 1.0;
}

#endif

#endif
