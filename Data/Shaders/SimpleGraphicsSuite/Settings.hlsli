// Continuous sliders (sharpening, grading, bloom, motion blur) read this
// cbuffer every frame instead of being baked in as compile-time macros -
// PostProcessing.cpp updates it directly, no shader recompile needed, so
// dragging a slider in the menu is instant.
#ifndef SIMPLEGRAPHICSSUITE_SETTINGS_HLSLI
#define SIMPLEGRAPHICSSUITE_SETTINGS_HLSLI

cbuffer SimpleGraphicsSuiteSettings : register(b13)
{
	float SGS_Sharpening;
	float SGS_Exposure;
	float SGS_Contrast;
	float SGS_Saturation;
	float SGS_BloomIntensity;
	float SGS_MotionBlurAmount;
	float SGS_UpscalingEnabled;
	float SGS_LUTStrength;
	float SGS_LUTSize;
	float SGS_TonemapMethod;  // 1=channel, 2=peak, 3=average luma, 4=Frostbyte, 5=ACES
	float SGS_Vignette;       // 0.0-1.0, 0=off
	float SGS_PostProcessingEnabled;  // gates only the ENB grading, see main()'s Vanilla() fallback
	float SGS_BloomEnhanced;  // 1 when Bloom.cpp supplies TextureBloom
	float SGS_Reserved2;      // pad to a 16-byte cbuffer row - keep in sync with SettingsCB
	float SGS_Reserved3;
	float SGS_Reserved4;
};

#define SHARPENING_AMOUNT SGS_Sharpening
#define SETTING_UIHCG_Exposure SGS_Exposure
#define SETTING_UIHCG_Contrast SGS_Contrast
#define SETTING_UIHCG_Saturation SGS_Saturation
// The slider is 0-1; doubling keeps its midpoint on the original's own 1.0.
#define SETTING_UIB_BloomIntensity (SGS_BloomIntensity * 2.0)
#define MOTION_BLUR_AMOUNT SGS_MotionBlurAmount

#endif
