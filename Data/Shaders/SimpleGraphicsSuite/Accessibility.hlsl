// Runs over the fully composited back buffer right before Present - see
// Accessibility.cpp.

Texture2D<float4> TextureColor : register(t0);
SamplerState       TextureColorSampler : register(s0);

cbuffer AccessibilitySettings : register(b0)
{
	float SGS_ColorblindMode;      // 0=Off, 1=Protanopia, 2=Deuteranopia, 3=Tritanopia, 4=Grayscale (debug)
	float SGS_ColorblindStrength;
	float SGS_HighContrastStrength;  // 0.0-1.0, 0 = off
	float SGS_InputLinear;  // sRGB or float back buffer
};

float3 SRGBToLinear(float3 c)
{
	return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float3 LinearToSRGB(float3 c)
{
	c = max(c, 0.0);
	return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

// Linear RGB in and out. Machado, Oliveira and Fernandes 2009 simulation at
// severity 1.0, then the Fidaner, Lin and Ozguven 2005 error shift.
float3 Daltonize(float3 color, float mode, float strength)
{
	// Grayscale, a debug mode.
	if (mode > 3.5)
		return lerp(color, dot(color, float3(0.2126, 0.7152, 0.0722)).xxx, strength);

	float3x3 sim;
	if (mode < 1.5)       // Protanopia
		sim = float3x3(0.152286, 1.052583, -0.204868,
		               0.114503, 0.786281, 0.099216,
		               -0.003882, -0.048116, 1.051998);
	else if (mode < 2.5)  // Deuteranopia
		sim = float3x3(0.367322, 0.860646, -0.227968,
		               0.280085, 0.672501, 0.047413,
		               -0.011820, 0.042940, 0.968881);
	else                  // Tritanopia
		sim = float3x3(1.255528, -0.076749, -0.178779,
		               -0.078411, 0.930809, 0.147602,
		               0.004733, 0.691367, 0.303900);

	float3 error = color - mul(sim, color);

	static const float3x3 shift = float3x3(0.0, 0.0, 0.0,
	                                        0.7, 1.0, 0.0,
	                                        0.7, 0.0, 1.0);
	float3 corrected = max(color + mul(shift, error), 0.0);
	return lerp(color, corrected, strength);
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
	color = HighContrast(color, uv, SGS_HighContrastStrength);
	if (SGS_ColorblindMode > 0.5) {
		if (SGS_InputLinear > 0.5)
			color = Daltonize(color, SGS_ColorblindMode, SGS_ColorblindStrength);
		else
			color = LinearToSRGB(Daltonize(SRGBToLinear(saturate(color)), SGS_ColorblindMode, SGS_ColorblindStrength));
	}
	return float4(color, 1.0);
}
