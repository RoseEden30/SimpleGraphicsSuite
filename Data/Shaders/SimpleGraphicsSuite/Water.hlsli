#ifndef SIMPLEGRAPHICSSUITE_WATER_HLSLI
#define SIMPLEGRAPHICSSUITE_WATER_HLSLI

static const float SGS_TwoPi = 6.28318531;

// Dave Hoskins, "Hash without Sine" (MIT).
float SGS_Hash12(float2 p)
{
	float3 p3 = frac(p.xyx * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return frac((p3.x + p3.y) * p3.z);
}

// Speeds are whole cycles per 1000 s, so SGS_GrainTime wrapping doesn't jump.
float2 SGS_WaterWarp(float2 uv, float time, float2 frequency, float2 speed, float amplitude)
{
	float2 phase = uv.yx * frequency + time * SGS_TwoPi * speed;
	return float2(sin(phase.x), cos(phase.y)) * amplitude;
}

// One grid of drops sliding down, as a refraction offset in uv units.
float2 SGS_DropLayer(float2 uv, float scale, float time, float amount, float seed)
{
	const float  aspect = SCREEN_INV_HEIGHT / SCREEN_INV_WIDTH;
	const float2 cellSize = float2(1.0, 3.0);
	const float  radius = 0.25;
	const float  trailLength = 1.2;

	float2 p = uv * float2(aspect, 1.0) * scale / cellSize;
	float2 cell = floor(p) + seed;
	float  h = SGS_Hash12(cell);

	float presence = saturate((amount - h) * 10.0);
	if (presence <= 0.0)
		return 0.0;

	float  speed = lerp(0.15, 0.45, SGS_Hash12(cell + 7.1));
	float2 drop = float2(lerp(0.3, 0.7, SGS_Hash12(cell + 3.7)), frac(h * 13.0 + time * speed));
	float2 d = (frac(p) - drop) * cellSize;

	presence *= smoothstep(0.0, 0.1, drop.y) * smoothstep(1.0, 0.9, drop.y);

	float inside = smoothstep(radius, radius * 0.6, length(d));
	float trail = smoothstep(0.08, 0.0, abs(d.x)) * saturate(1.0 + d.y / trailLength) * step(d.y, 0.0) *
	              smoothstep(0.0, 0.15, frac(p.y));

	float2 offset = -d * inside * 0.5 + float2(-d.x * trail * 0.25, 0.0);
	return offset * presence / (scale * float2(aspect, 1.0));
}

float2 SGS_ApplyWater(float2 uv, float time, float warp, float splash, float film, float drops, float dropsTime)
{
	if (warp > 0.0) {
		uv += SGS_WaterWarp(uv, time, float2(23.0, 17.0), float2(0.5, 0.7), warp * 0.003);
		uv += SGS_WaterWarp(uv, time, float2(61.0, 47.0), float2(1.1, 0.9), warp * 0.002);
	}

	if (splash > 0.0)
		uv += SGS_WaterWarp(uv, time, float2(89.0, 71.0), float2(1.3, 1.7), splash * splash * 0.012);

	if (film > 0.0) {
		float film2 = film * film;
		uv += SGS_WaterWarp(uv, time, float2(23.0, 17.0), float2(0.5, 0.7), film2 * film2 * 0.01);
	}

	if (drops > 0.0)
		uv += SGS_DropLayer(uv, 10.0, dropsTime, drops, 0.0) + SGS_DropLayer(uv, 18.0, dropsTime, drops, 41.0);

	return saturate(uv);
}

#endif
