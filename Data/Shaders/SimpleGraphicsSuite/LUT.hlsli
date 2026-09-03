#ifndef SIMPLEGRAPHICSSUITE_LUT_HLSLI
#define SIMPLEGRAPHICSSUITE_LUT_HLSLI

// Applies a 3D color grading LUT to a graded, gamma-space color. a_size is
// the LUT's edge length (e.g. 33 for a 33x33x33 .cube) - the half-texel
// offset below maps color 0.0/1.0 exactly onto the LUT's first/last texel
// instead of sampling slightly past them.
float3 SGS_ApplyLUT(Texture3D<float3> a_lut, SamplerState a_sampler, float3 a_color, float a_strength, float a_size)
{
	if (a_strength <= 0.0 || a_size <= 1.0)
		return a_color;

	float3 uvw = saturate(a_color) * (a_size - 1.0) / a_size + 0.5 / a_size;
	float3 graded = a_lut.SampleLevel(a_sampler, uvw, 0);
	return lerp(a_color, graded, a_strength);
}

#endif
