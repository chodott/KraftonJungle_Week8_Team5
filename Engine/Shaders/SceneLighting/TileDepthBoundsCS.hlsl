#include "../LightCommon.hlsli"

Texture2D<float> InputDepthTexture : register(t0);
RWStructuredBuffer<FTileDepthBoundsGPU> OutTileDepthBounds : register(u0);

static const float kEpsilon = 1e-5f;
static const float kDepthSkyThreshold = 0.999999f;

groupshared uint gMinViewZBits;
groupshared uint gMaxViewZBits;
groupshared uint gHasGeometry;

float DeviceDepthToViewZ(float deviceDepth)
{
	float4 clipPos = float4(0.0f, 0.0f, deviceDepth, 1.0f);
	float4 viewPos = mul(clipPos, ClusterInverseProjection);
	return max(viewPos.x / max(viewPos.w, kEpsilon), NearZ);
}

[numthreads(16, 16, 1)]
void main(
	uint3 GroupID : SV_GroupID,
	uint3 GroupThreadID : SV_GroupThreadID,
	uint GroupIndex : SV_GroupIndex)
{
	const uint tileX = GroupID.x;
	const uint tileY = GroupID.y;

	if (tileX >= ClusterCountX || tileY >= ClusterCountY)
		return;

	if (GroupIndex == 0)
	{
		gMinViewZBits = asuint(FarZ);
		gMaxViewZBits = asuint(NearZ);
		gHasGeometry = 0u;
	}

	GroupMemoryBarrierWithGroupSync();

	const uint2 pixelCoord = uint2(tileX * 16u + GroupThreadID.x, tileY * 16u + GroupThreadID.y);
	if (pixelCoord.x < (uint)ScreenParams.x && pixelCoord.y < (uint)ScreenParams.y)
	{
		const float deviceDepth = InputDepthTexture.Load(int3(pixelCoord, 0));
		if (deviceDepth < kDepthSkyThreshold)
		{
			const float viewDepth = DeviceDepthToViewZ(deviceDepth);
			const uint viewDepthBits = asuint(viewDepth);

			InterlockedMin(gMinViewZBits, viewDepthBits);
			InterlockedMax(gMaxViewZBits, viewDepthBits);
			InterlockedOr(gHasGeometry, 1u);
		}
	}

	GroupMemoryBarrierWithGroupSync();

	if (GroupIndex != 0)
		return;

	const uint tileIndex = tileY * ClusterCountX + tileX;

	FTileDepthBoundsGPU tileDepthBounds;
	tileDepthBounds.MinViewZ = asfloat(gMinViewZBits);
	tileDepthBounds.MaxViewZ = asfloat(gMaxViewZBits);
	tileDepthBounds.HasGeometry = gHasGeometry;
	tileDepthBounds.Pad0 = 0u;
	tileDepthBounds.Pad1 = 0u;
	tileDepthBounds.Pad2 = 0u;

	if (gHasGeometry != 0u)
	{
		const uint minSlice = ComputeZSlice(tileDepthBounds.MinViewZ);
		const uint maxSlice = ComputeZSlice(tileDepthBounds.MaxViewZ);

		tileDepthBounds.TileMinSlice = (minSlice > 0u) ? (minSlice - 1u) : 0u;
		tileDepthBounds.TileMaxSlice = min(maxSlice + 1u, ClusterCountZ - 1u);
	}
	else
	{
		tileDepthBounds.TileMinSlice = 0u;
		tileDepthBounds.TileMaxSlice = 0u;
	}

	OutTileDepthBounds[tileIndex] = tileDepthBounds;
}
