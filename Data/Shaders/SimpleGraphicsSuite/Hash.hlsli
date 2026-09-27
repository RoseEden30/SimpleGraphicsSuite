#ifndef SIMPLEGRAPHICSSUITE_HASH_HLSLI
#define SIMPLEGRAPHICSSUITE_HASH_HLSLI

// Dave Hoskins, "Hash without Sine" (MIT).
float SGS_Hash12(float2 p)
{
	float3 p3 = frac(p.xyx * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return frac((p3.x + p3.y) * p3.z);
}

float SGS_Hash13(float3 p3)
{
	p3 = frac(p3 * 0.1031);
	p3 += dot(p3, p3.zyx + 31.32);
	return frac((p3.x + p3.y) * p3.z);
}

#endif
