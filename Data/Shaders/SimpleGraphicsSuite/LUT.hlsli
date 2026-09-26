#ifndef SIMPLEGRAPHICSSUITE_LUT_HLSLI
#define SIMPLEGRAPHICSSUITE_LUT_HLSLI

// a_size is the LUT's edge length. Filtered by hand, so no sampler is bound.
float3 SGS_ApplyLUT(Texture3D<float3> a_lut, float3 a_color, float a_strength, float a_size)
{
	if (a_strength <= 0.0 || a_size <= 1.0)
		return a_color;

	float3 p = saturate(a_color) * (a_size - 1.0);
	int3   i0 = (int3)floor(p);
	int3   i1 = min(i0 + 1, (int)a_size - 1);
	float3 f = p - i0;

	float3 c00 = lerp(a_lut.Load(int4(i0.x, i0.y, i0.z, 0)), a_lut.Load(int4(i1.x, i0.y, i0.z, 0)), f.x);
	float3 c10 = lerp(a_lut.Load(int4(i0.x, i1.y, i0.z, 0)), a_lut.Load(int4(i1.x, i1.y, i0.z, 0)), f.x);
	float3 c01 = lerp(a_lut.Load(int4(i0.x, i0.y, i1.z, 0)), a_lut.Load(int4(i1.x, i0.y, i1.z, 0)), f.x);
	float3 c11 = lerp(a_lut.Load(int4(i0.x, i1.y, i1.z, 0)), a_lut.Load(int4(i1.x, i1.y, i1.z, 0)), f.x);
	float3 graded = lerp(lerp(c00, c10, f.y), lerp(c01, c11, f.y), f.z);
	return lerp(a_color, graded, a_strength);
}

#endif
