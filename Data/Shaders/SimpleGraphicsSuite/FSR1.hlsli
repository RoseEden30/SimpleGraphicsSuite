// AMD FidelityFX FSR1: EASU (edge-adaptive spatial upscale) + RCAS (robust
// contrast adaptive sharpening), ported from ffx_fsr1.h (MIT license,
// https://github.com/GPUOpen-Effects/FidelityFX-FSR). Native rcp()/rsqrt()/
// saturate() are used in place of FSR1's fast bit-trick approximations -
// those exist for old/mobile GPU performance, not needed on desktop.
#ifndef SIMPLEGRAPHICSSUITE_FSR1_HLSLI
#define SIMPLEGRAPHICSSUITE_FSR1_HLSLI

// One tap of the 12-tap EASU kernel: rotates the tap offset into the local
// edge direction, weights it by an approximated Lanczos2 window, and
// accumulates.
void SGS_FsrEasuTap(inout float3 a_accumColor, inout float a_accumWeight, float2 a_offset, float2 a_dir,
	float2 a_len, float a_lobe, float a_clip, float3 a_tapColor)
{
	float2 v;
	v.x = a_offset.x * a_dir.x + a_offset.y * a_dir.y;
	v.y = a_offset.x * -a_dir.y + a_offset.y * a_dir.x;
	v *= a_len;

	float d2 = min(v.x * v.x + v.y * v.y, a_clip);
	float wB = (2.0 / 5.0) * d2 - 1.0;
	float wA = a_lobe * d2 - 1.0;
	wB *= wB;
	wA *= wA;
	wB = (25.0 / 16.0) * wB - (25.0 / 16.0 - 1.0);
	float w = wB * wA;

	a_accumColor += a_tapColor * w;
	a_accumWeight += w;
}

// Accumulates the local gradient direction/length from one of the four
// bilinear corners around the resolve position.
void SGS_FsrEasuSet(inout float2 a_dir, inout float a_len, float2 a_pp, bool a_biS, bool a_biT, bool a_biU,
	bool a_biV, float a_lA, float a_lB, float a_lC, float a_lD, float a_lE)
{
	float w = 0.0;
	if (a_biS)
		w = (1.0 - a_pp.x) * (1.0 - a_pp.y);
	if (a_biT)
		w = a_pp.x * (1.0 - a_pp.y);
	if (a_biU)
		w = (1.0 - a_pp.x) * a_pp.y;
	if (a_biV)
		w = a_pp.x * a_pp.y;

	float dc = a_lD - a_lC;
	float cb = a_lC - a_lB;
	float lenX = rcp(max(abs(dc), abs(cb)));
	float dirX = a_lD - a_lB;
	a_dir.x += dirX * w;
	lenX = saturate(abs(dirX) * lenX);
	lenX *= lenX;
	a_len += lenX * w;

	float ec = a_lE - a_lC;
	float ca = a_lC - a_lA;
	float lenY = rcp(max(abs(ec), abs(ca)));
	float dirY = a_lE - a_lA;
	a_dir.y += dirY * w;
	lenY = saturate(abs(dirY) * lenY);
	lenY *= lenY;
	a_len += lenY * w;
}

// a_inputSizeInPixels: size of the texture EASU samples from (the buffer,
// not necessarily the render viewport - matches Dynamic Resolution, where
// only a sub-rect of a full-size buffer holds real data).
// a_renderSizeInPixels: the actual rendered (sub-)resolution being upscaled.
// a_outputUV: 0-1 UV of the pixel being resolved, in the final output image.
float3 SGS_ApplyFSR1EASU(Texture2D<float4> a_tex, SamplerState a_pointSampler, float2 a_inputSizeInPixels,
	float2 a_renderSizeInPixels, float2 a_outputSizeInPixels, float2 a_outputUV)
{
	// a_outputUV is pixel-center UV ((i+0.5)/size); EASU's constants below
	// are derived for AMD's own "ip" convention (plain integer pixel index),
	// so undo that +0.5 here rather than double-counting it.
	float2 outputPixel = a_outputUV * a_outputSizeInPixels - 0.5;

	// Position of 'f' (see the tap diagram below), in input-texture texels.
	float2 pp = outputPixel * (a_renderSizeInPixels / a_outputSizeInPixels) +
	            (0.5 * a_renderSizeInPixels / a_outputSizeInPixels - 0.5);
	float2 fp = floor(pp);
	pp -= fp;

	// Gather sample positions, ported exactly from FsrEasuCon's con1/con2/con3
	// (not re-derived) - these are chosen so a plain D3D Gather4 at each
	// position returns the labelled taps in .x/.y/.z/.w below.
	float2 texelSize = rcp(a_inputSizeInPixels);
	float2 p0 = fp * texelSize + float2(1.0, -1.0) * texelSize;
	float2 p1 = p0 + float2(-1.0, 2.0) * texelSize;
	float2 p2 = p0 + float2(1.0, 2.0) * texelSize;
	float2 p3 = p0 + float2(0.0, 4.0) * texelSize;

	// 12-tap kernel:
	//    b c
	//  e f g h
	//  i j k l
	//    n o
	float4 bczzR = a_tex.GatherRed(a_pointSampler, p0);
	float4 bczzG = a_tex.GatherGreen(a_pointSampler, p0);
	float4 bczzB = a_tex.GatherBlue(a_pointSampler, p0);
	float4 ijfeR = a_tex.GatherRed(a_pointSampler, p1);
	float4 ijfeG = a_tex.GatherGreen(a_pointSampler, p1);
	float4 ijfeB = a_tex.GatherBlue(a_pointSampler, p1);
	float4 klhgR = a_tex.GatherRed(a_pointSampler, p2);
	float4 klhgG = a_tex.GatherGreen(a_pointSampler, p2);
	float4 klhgB = a_tex.GatherBlue(a_pointSampler, p2);
	float4 zzonR = a_tex.GatherRed(a_pointSampler, p3);
	float4 zzonG = a_tex.GatherGreen(a_pointSampler, p3);
	float4 zzonB = a_tex.GatherBlue(a_pointSampler, p3);

	// Component extraction below is copied as-is from FsrEasuF, not
	// re-derived - it's paired with the exact p0/p1/p2/p3 positions above.
	float4 bczzL = bczzB * 0.5 + (bczzR * 0.5 + bczzG);
	float4 ijfeL = ijfeB * 0.5 + (ijfeR * 0.5 + ijfeG);
	float4 klhgL = klhgB * 0.5 + (klhgR * 0.5 + klhgG);
	float4 zzonL = zzonB * 0.5 + (zzonR * 0.5 + zzonG);

	float bL = bczzL.x, cL = bczzL.y;
	float iL = ijfeL.x, jL = ijfeL.y, fL = ijfeL.z, eL = ijfeL.w;
	float kL = klhgL.x, lL = klhgL.y, hL = klhgL.z, gL = klhgL.w;
	float oL = zzonL.z, nL = zzonL.w;

	float2 dir = float2(0.0, 0.0);
	float len = 0.0;
	SGS_FsrEasuSet(dir, len, pp, true, false, false, false, bL, eL, fL, gL, jL);
	SGS_FsrEasuSet(dir, len, pp, false, true, false, false, cL, fL, gL, hL, kL);
	SGS_FsrEasuSet(dir, len, pp, false, false, true, false, fL, iL, jL, kL, nL);
	SGS_FsrEasuSet(dir, len, pp, false, false, false, true, gL, jL, kL, lL, oL);

	float2 dir2 = dir * dir;
	float dirR = dir2.x + dir2.y;
	bool zro = dirR < (1.0 / 32768.0);
	dirR = rsqrt(max(dirR, 1e-8));
	dirR = zro ? 1.0 : dirR;
	dir.x = zro ? 1.0 : dir.x;
	dir *= dirR;

	len = len * 0.5;
	len *= len;
	float stretch = (dir.x * dir.x + dir.y * dir.y) * rcp(max(abs(dir.x), abs(dir.y)));
	float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
	float lob = 0.5 + (1.0 / 4.0 - 0.04 - 0.5) * len;
	float clp = rcp(lob);

	float3 min4 = min(min(float3(ijfeR.z, ijfeG.z, ijfeB.z), float3(klhgR.w, klhgG.w, klhgB.w)),
		min(float3(ijfeR.y, ijfeG.y, ijfeB.y), float3(klhgR.x, klhgG.x, klhgB.x)));
	float3 max4 = max(max(float3(ijfeR.z, ijfeG.z, ijfeB.z), float3(klhgR.w, klhgG.w, klhgB.w)),
		max(float3(ijfeR.y, ijfeG.y, ijfeB.y), float3(klhgR.x, klhgG.x, klhgB.x)));

	float3 aC = float3(0.0, 0.0, 0.0);
	float aW = 0.0;
	SGS_FsrEasuTap(aC, aW, float2(0.0, -1.0) - pp, dir, len2, lob, clp, float3(bczzR.x, bczzG.x, bczzB.x));   // b
	SGS_FsrEasuTap(aC, aW, float2(1.0, -1.0) - pp, dir, len2, lob, clp, float3(bczzR.y, bczzG.y, bczzB.y));   // c
	SGS_FsrEasuTap(aC, aW, float2(-1.0, 1.0) - pp, dir, len2, lob, clp, float3(ijfeR.x, ijfeG.x, ijfeB.x));   // i
	SGS_FsrEasuTap(aC, aW, float2(0.0, 1.0) - pp, dir, len2, lob, clp, float3(ijfeR.y, ijfeG.y, ijfeB.y));    // j
	SGS_FsrEasuTap(aC, aW, float2(0.0, 0.0) - pp, dir, len2, lob, clp, float3(ijfeR.z, ijfeG.z, ijfeB.z));    // f
	SGS_FsrEasuTap(aC, aW, float2(-1.0, 0.0) - pp, dir, len2, lob, clp, float3(ijfeR.w, ijfeG.w, ijfeB.w));   // e
	SGS_FsrEasuTap(aC, aW, float2(1.0, 1.0) - pp, dir, len2, lob, clp, float3(klhgR.x, klhgG.x, klhgB.x));    // k
	SGS_FsrEasuTap(aC, aW, float2(2.0, 1.0) - pp, dir, len2, lob, clp, float3(klhgR.y, klhgG.y, klhgB.y));    // l
	SGS_FsrEasuTap(aC, aW, float2(2.0, 0.0) - pp, dir, len2, lob, clp, float3(klhgR.z, klhgG.z, klhgB.z));    // h
	SGS_FsrEasuTap(aC, aW, float2(1.0, 0.0) - pp, dir, len2, lob, clp, float3(klhgR.w, klhgG.w, klhgB.w));    // g
	SGS_FsrEasuTap(aC, aW, float2(1.0, 2.0) - pp, dir, len2, lob, clp, float3(zzonR.z, zzonG.z, zzonB.z));    // o
	SGS_FsrEasuTap(aC, aW, float2(0.0, 2.0) - pp, dir, len2, lob, clp, float3(zzonR.w, zzonG.w, zzonB.w));    // n

	return min(max4, max(min4, aC * rcp(aW)));
}

// RCAS - AMD's refinement of CAS: solves more exactly for the maximum
// sharpness before clipping, and backs off on what its noise detector
// thinks is grain (CAS has no such check, so it over-sharpens noise).
// a_centerColor: the pixel's own already-resolved color (e.g. EASU's output,
// so upscaling quality isn't lost by resampling the low-res buffer for 'e').
// a_sharpness: 0 = maximum sharpening, higher values back off (in stops).
float3 SGS_ApplyFSR1RCAS(Texture2D<float4> a_tex, SamplerState a_pointSampler, float2 a_uv, float2 a_texelSize,
	float3 a_centerColor, float a_sharpness)
{
	float3 b = a_tex.SampleLevel(a_pointSampler, a_uv + float2(0.0, -a_texelSize.y), 0).rgb;
	float3 d = a_tex.SampleLevel(a_pointSampler, a_uv + float2(-a_texelSize.x, 0.0), 0).rgb;
	float3 e = a_centerColor;
	float3 f = a_tex.SampleLevel(a_pointSampler, a_uv + float2(a_texelSize.x, 0.0), 0).rgb;
	float3 h = a_tex.SampleLevel(a_pointSampler, a_uv + float2(0.0, a_texelSize.y), 0).rgb;

	float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
	float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
	float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
	float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
	float hL = h.b * 0.5 + (h.r * 0.5 + h.g);

	// Noise detection - backs off sharpening on what looks like grain.
	float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;
	nz = saturate(abs(nz) * rcp(max(max(max(bL, dL), max(eL, fL)), hL) - min(min(min(bL, dL), min(eL, fL)), hL) + 1e-8));
	nz = 1.0 - 0.5 * nz;

	float3 mn4 = min(min(min(b, d), f), h);
	float3 mx4 = max(max(max(b, d), f), h);

	const float2 peakC = float2(1.0, -4.0);
	float3 hitMin = min(mn4, e) * rcp(4.0 * mx4 + 1e-8);
	float3 hitMax = (peakC.x - max(mx4, e)) * rcp(4.0 * mn4 + peakC.y);
	float3 lobeRGB = max(-hitMin, hitMax);

	const float kFsrRcasLimit = 0.25 - 1.0 / 16.0;
	float lobe = max(-kFsrRcasLimit, min(max(max(lobeRGB.r, lobeRGB.g), lobeRGB.b), 0.0)) * exp2(-a_sharpness);
	lobe *= nz;

	float rcpL = rcp(4.0 * lobe + 1.0);
	return (lobe * b + lobe * d + lobe * h + lobe * f + e) * rcpL;
}

#endif
