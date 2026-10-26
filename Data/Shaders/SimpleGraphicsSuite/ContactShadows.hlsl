// Screen-space contact shadows, multiplied into the scene before anti-aliasing
// so TAA/DLAA resolve them. Toward the sun outdoors, toward the nearest point
// lights indoors. The output is a shadow factor; the blend state multiplies it.

#define MAX_LIGHTS 4

cbuffer ContactShadowData : register(b0)
{
	float4 Lights[MAX_LIGHTS];  // xyz world pos, w range
	float  LightCount;
	float  Interior;
	float  Strength;
	float  FrameIndex;  // varies the dither under temporal AA, 0 otherwise
	float3 SunDirection;  // world space, toward the sun
	float  pad0;
	float  CameraNear;
	float  CameraFar;
	float2 RenderSize;  // pixels, under dynamic resolution too
};

cbuffer PerFrame : register(b12)
{
	row_major float4x4 ViewMatrix : packoffset(c0);
	row_major float4x4 ProjMatrix : packoffset(c4);
	row_major float4x4 InvProjMatrix : packoffset(c36);
};

Texture2D<float> Depth : register(t0);

static const int   STEPS = 16;
static const float LENGTH_FRAC = 0.03;
static const float START_FRAC = 0.0025;
static const float THICKNESS_FRAC = 0.02;
static const float BIAS_FRAC = 0.004;
static const float EDGE_FADE = 0.3;
static const float FADE_START = 1500.0;
static const float FADE_END = 4000.0;

// Interleaved gradient noise, offset per frame so temporal AA averages it.
float Dither(float2 a_pixel)
{
	a_pixel += 5.588238 * FrameIndex;
	return frac(52.9829189 * frac(dot(a_pixel, float2(0.06711056, 0.00583715))));
}

float Linearize(float a_depth)
{
	return CameraFar * CameraNear / (CameraFar - a_depth * (CameraFar - CameraNear));
}

float LoadDepth(float2 a_uv)
{
	int2 pixel = clamp(int2(a_uv * RenderSize), 0, int2(RenderSize) - 1);
	return Depth.Load(int3(pixel, 0));
}

// Occlusion in [0,1], from the first surface the ray passes behind.
float March(float3 a_origin, float3 a_direction, float a_length, float a_dither)
{
	float start = length(a_origin) * START_FRAC;
	float step = a_length / STEPS;

	[loop]
	for (int i = 0; i < STEPS; ++i) {
		float  t = start + (i + a_dither) * step;
		float4 clip = mul(ProjMatrix, float4(a_origin + a_direction * t, 1.0));
		if (clip.w <= 1e-5)
			break;
		float2 uv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
		if (any(uv < 0.0) || any(uv > 1.0))
			break;

		float sceneDepth = LoadDepth(uv);
		if (sceneDepth >= 1.0)
			continue;

		float rayDist = Linearize(saturate(clip.z / clip.w));
		float diff = rayDist - Linearize(sceneDepth);
		float bias = rayDist * BIAS_FRAC;
		if (diff > bias) {
			float thickness = rayDist * THICKNESS_FRAC;
			if (diff >= thickness)
				return 0.0;
			float mid = 0.5 * (bias + thickness);
			float window = smoothstep(0.0, 1.0, 1.0 - saturate(abs(diff - mid) / max(mid - bias, 1e-4)));
			return window * saturate((1.0 - t / a_length) / EDGE_FADE);
		}
	}
	return 0.0;
}

float4 main(float4 a_position : SV_Position) : SV_Target
{
	float2 pixel = a_position.xy;
	float  depth = Depth.Load(int3(pixel, 0));
	if (depth >= 1.0)
		return 1.0;

	float2 uv = pixel / RenderSize;
	float4 originH = mul(InvProjMatrix, float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0));
	float3 origin = originH.xyz / originH.w;

	float distance = length(origin);
	float fade = 1.0 - saturate((distance - FADE_START) / (FADE_END - FADE_START));
	if (fade <= 0.0)
		return 1.0;

	float reach = distance * LENGTH_FRAC;
	float dither = Dither(pixel);
	float occlusion = 0.0;

	if (Interior > 0.5) {
		float weightSum = 0.0;
		[loop]
		for (int i = 0; i < (int)LightCount; ++i) {
			float3 toLight = mul(ViewMatrix, float4(Lights[i].xyz, 1.0)).xyz - origin;
			float  lightDistance = length(toLight);
			if (lightDistance < 1e-3 || lightDistance > Lights[i].w)
				continue;

			float weight = saturate(1.0 - lightDistance / Lights[i].w);
			weight *= weight;
			occlusion += weight * March(origin, toLight / lightDistance, min(reach, lightDistance), dither);
			weightSum += weight;
		}
		occlusion = weightSum > 0.0 ? occlusion / weightSum : 0.0;
	} else {
		float3 direction = mul(ViewMatrix, float4(SunDirection, 0.0)).xyz;
		float  len = length(direction);
		if (len < 1e-4)
			return 1.0;
		occlusion = March(origin, direction / len, reach, dither);
	}

	return 1.0 - occlusion * Strength * fade;
}
