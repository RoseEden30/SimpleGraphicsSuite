// Runs over the fully composited back buffer right before Present - see
// Accessibility.cpp.

Texture2D<float4> TextureColor : register(t0);
SamplerState       TextureColorSampler : register(s0);

cbuffer AccessibilitySettings : register(b0)
{
	float SGS_ColorblindMode;      // 0=Off, 1=Protanopia, 2=Deuteranopia, 3=Tritanopia, 4=Grayscale (debug)
	float SGS_ColorblindStrength;
	float SGS_HighContrastStrength;  // 0.0-1.0, 0 = off
	float SGS_Reserved1;
};

// Daltonization: simulate the selected color vision deficiency, then shift
// the resulting error (what that simulation drops) into the blue/red
// channels the deficiency doesn't affect, so hues that were confusable stay
// distinguishable. Simulation matrices are the standard simplified RGB set
// (Machado/Oliveira/Fialho 2009, as commonly reproduced); the error-shift
// matrix is the standard Daltonize correction (Fidaner/Lin/Ozguven 2005).
float3 Daltonize(float3 color, float mode, float strength)
{
	if (mode < 0.5)
		return color;

	// Grayscale debug mode - not a real deficiency, just an obvious way to
	// confirm this pass covers the whole frame, HUD/menus included.
	if (mode > 3.5) {
		float luma = dot(color, float3(0.2125, 0.7154, 0.0721));
		return lerp(color, luma.xxx, strength);
	}

	float3x3 sim;
	if (mode < 1.5)       // Protanopia
		sim = float3x3(0.567, 0.433, 0.000,
		               0.558, 0.442, 0.000,
		               0.000, 0.242, 0.758);
	else if (mode < 2.5)  // Deuteranopia
		sim = float3x3(0.625, 0.375, 0.000,
		               0.700, 0.300, 0.000,
		               0.000, 0.300, 0.700);
	else                  // Tritanopia
		sim = float3x3(0.950, 0.050, 0.000,
		               0.000, 0.433, 0.567,
		               0.000, 0.475, 0.525);

	float3 simulated = mul(sim, color);
	float3 error = color - simulated;

	static const float3x3 shift = float3x3(0.0, 0.0, 0.0,
	                                        0.7, 1.0, 0.0,
	                                        0.7, 0.0, 1.0);
	float3 corrected = color + mul(shift, error);
	return lerp(color, saturate(corrected), strength);
}

// Unsharp mask: boosts local contrast around edges (menus, HUD text, object
// silhouettes) without shifting overall exposure, unlike a global contrast
// curve.
float3 HighContrast(float3 color, float2 uv, float strength)
{
	if (strength <= 0.0)
		return color;

	uint width, height;
	TextureColor.GetDimensions(width, height);
	float2 texel = 1.0 / float2(width, height);

	float3 blur = TextureColor.Sample(TextureColorSampler, uv + texel * float2(-1.0, -1.0)).rgb +
	              TextureColor.Sample(TextureColorSampler, uv + texel * float2( 1.0, -1.0)).rgb +
	              TextureColor.Sample(TextureColorSampler, uv + texel * float2(-1.0,  1.0)).rgb +
	              TextureColor.Sample(TextureColorSampler, uv + texel * float2( 1.0,  1.0)).rgb;
	blur *= 0.25;

	return saturate(color + (color - blur) * strength);
}

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
	float3 color = TextureColor.Sample(TextureColorSampler, uv).rgb;
	color = Daltonize(color, SGS_ColorblindMode, SGS_ColorblindStrength);
	color = HighContrast(color, uv, SGS_HighContrastStrength);
	return float4(color, 1.0);
}
