// Prepares the textures DLSS reads through Streamline, same technique
// Community Shaders uses for its own DLSS path (EncodeTexturesCS.hlsl):
//
// - Motion vector dilation: for each pixel, take the longest motion vector
//   among itself and its 5x5 neighbors, but only consider neighbors CLOSER
//   to the camera (smaller depth). A moving foreground object's true motion
//   "bleeds" into the background pixels around its silhouette this way,
//   fixing the ghosting/smearing that shows up around fast-moving edges
//   when DLSS is fed undilated vectors. Blended back toward the raw vector
//   near the camera (nearFactor) since dilation over-smears close geometry
//   like first-person gear.
// - Reactive mask: derived from the engine's own TAA mask, tells DLSS which
//   pixels come from special-case blending (skin, hair, particles, ...)
//   that shouldn't be trusted as much for temporal accumulation.
// - Transparency mask: the .z channel of the engine's own underwater mask
//   render target - the same one its own ISWaterBlend.hlsl reads as
//   "waterMaskTex" for its water-specific temporal history blend. Tells
//   DLSS which pixels are water it shouldn't fully trust temporally either
//   - the actual fix for water shimmer.

cbuffer EncodeData : register(b0)
{
	uint2 RenderSize;
	float CameraNear;
	float CameraFar;
};

Texture2D<float2> InMotionVector : register(t0);
Texture2D<float> InDepth : register(t1);
Texture2D<float2> InTAAMask : register(t2);
Texture2D<float4> InUnderwaterMask : register(t3);

RWTexture2D<float2> OutMotionVector : register(u0);
RWTexture2D<float> OutReactiveMask : register(u1);
RWTexture2D<float> OutTransparencyMask : register(u2);

float GetScreenDepth(float depth)
{
	return (CameraFar * CameraNear) / (-depth * (CameraFar - CameraNear) + CameraFar);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID
                                 : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= RenderSize))
		return;

	int2 center = int2(dispatchID.xy);

	float2 motionVector = InMotionVector[center];
	float  depth = InDepth[center];
	float  nearFactor = smoothstep(4096.0 * 2.5, 0.0, GetScreenDepth(depth));

	float2 longestMotionVector = motionVector;
	float  maxMotionLengthSq = dot(motionVector, motionVector);

	[unroll] for (int y = -2; y <= 2; y++)
	{
		[unroll] for (int x = -2; x <= 2; x++)
		{
			int2 samplePos = center + int2(x, y);
			if (any(samplePos < 0) || any(samplePos >= int2(RenderSize)))
				continue;

			float neighborDepth = InDepth[samplePos];
			if (neighborDepth >= depth)
				continue;  // only pull motion from something closer to camera

			float2 neighborMotionVector = InMotionVector[samplePos];
			float  motionLengthSq = dot(neighborMotionVector, neighborMotionVector);

			if (motionLengthSq > maxMotionLengthSq) {
				maxMotionLengthSq = motionLengthSq;
				longestMotionVector = neighborMotionVector;
			}
		}
	}

	OutMotionVector[dispatchID.xy] = lerp(longestMotionVector, motionVector, nearFactor);

	float2 taaMask = InTAAMask[center];
	OutReactiveMask[dispatchID.xy] = taaMask.x * 0.1 + taaMask.y;

	OutTransparencyMask[dispatchID.xy] = InUnderwaterMask[center].z;
}
