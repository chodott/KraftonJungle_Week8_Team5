#ifndef SHADER_COMMON_HLSLI
#define SHADER_COMMON_HLSLI

// b0: 프레임당 1회 (카메라)
cbuffer FrameData : register(b0)
{
	float4x4 View;
	float4x4 Projection;
	float Time;
	float DeltaTime;
	float2 Framepadding;
};

// b1: 오브젝트당
cbuffer ObjectData : register(b1)
{
	float4x4 World;
};

struct VS_INPUT
{
	float3 Position : POSITION;
	float4 Color : COLOR;
	float3 Normal : NORMAL;
	float2 UV : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION;
	float4 Color : COLOR;
	float3 Normal : NORMAL;
	float2 UV : TEXCOORD0;
	float3 WorldPosition : TEXCOORD1;
};

#define DECAL_RENDER_FLAG_BASECOLOR 1u

struct FDecalClusterHeader
{
	uint Offset;
	uint Count;
	uint Pad0;
	uint Pad1;
};

struct FDecalGPUData
{
	float4x4 WorldToDecal;
	float4 AtlasScaleBias;
	float4 BaseColorTint;
	float3 Extents;
	uint TextureIndex;
	float3 AxisXWS;
	uint Flags;
	float3 AxisYWS;
	float NormalBlend;
	float3 AxisZWS;
	float RoughnessBlend;
	float EmissiveBlend;
	float EdgeFade;
	uint PadA;
	uint PadB;
};

cbuffer DecalClusterData : register(b3)
{
	uint DecalClusterCountX;
	uint DecalClusterCountY;
	uint DecalClusterCountZ;
	uint DecalMaxClusterItems;

	float DecalViewportWidth;
	float DecalViewportHeight;
	float DecalNearZ;
	float DecalFarZ;

	float DecalLogZScale;
	float DecalLogZBias;
	float DecalTileWidth;
	float DecalTileHeight;
};

StructuredBuffer<FDecalClusterHeader> DecalClusterHeaders : register(t10);
StructuredBuffer<uint> DecalClusterIndices : register(t11);
StructuredBuffer<FDecalGPUData> DecalDataBuffer : register(t12);
Texture2D DecalBaseColorTexture : register(t13);

float ComputeDecalEdgeFade(float3 LocalPosition, float3 Extents, float EdgeFade)
{
	float3 SafeExtents = max(Extents, float3(1.0e-4f, 1.0e-4f, 1.0e-4f));
	float3 DistanceToEdge = 1.0f - abs(LocalPosition) / SafeExtents;
	float EdgeFactor = min(DistanceToEdge.x, min(DistanceToEdge.y, DistanceToEdge.z));
	return saturate(EdgeFactor * max(EdgeFade, 0.0f));
}

uint ComputeDecalClusterId(VS_OUTPUT Input)
{
	if (DecalClusterCountX == 0 ||
		DecalClusterCountY == 0 ||
		DecalClusterCountZ == 0 ||
		DecalTileWidth <= 0.0f ||
		DecalTileHeight <= 0.0f ||
		DecalNearZ <= 0.0f ||
		DecalFarZ <= DecalNearZ)
	{
		return 0xFFFFFFFFu;
	}
	
	const float ViewDepth = mul(float4(Input.WorldPosition, 1.0f), View).x;
	if (ViewDepth < DecalNearZ || ViewDepth > DecalFarZ)
	{
		return 0xFFFFFFFFu;
	}

	const uint TileX = (uint) clamp(
		(int) floor(Input.Position.x / DecalTileWidth),
		0,
		(int) DecalClusterCountX - 1);

	const uint TileY = (uint) clamp(
		(int) floor(Input.Position.y / DecalTileHeight),
		0,
		(int) DecalClusterCountY - 1);

	const uint SliceZ = (uint) clamp(
		(int) floor(log(max(ViewDepth, DecalNearZ)) * DecalLogZScale + DecalLogZBias),
		0,
		(int) DecalClusterCountZ - 1);

	return TileX
		+ TileY * DecalClusterCountX
		+ SliceZ * DecalClusterCountX * DecalClusterCountY;
}

float4 ApplyBaseColorDecals(VS_OUTPUT Input, float4 BaseColor, SamplerState DecalSampler)
{
	const uint ClusterId = ComputeDecalClusterId(Input);
	if (ClusterId == 0xFFFFFFFFu)
	{
		return BaseColor;
	}

	const FDecalClusterHeader Header = DecalClusterHeaders[ClusterId];
	if (Header.Count == 0)
	{
		return BaseColor;
	}

	float3 ResultColor = BaseColor.rgb;
	const uint DecalCount = min(Header.Count, DecalMaxClusterItems);

	[loop]
	for (uint DecalListIndex = 0; DecalListIndex < DecalCount; ++DecalListIndex)
	{
		const uint DecalIndex = DecalClusterIndices[Header.Offset + DecalListIndex];
		const FDecalGPUData Decal = DecalDataBuffer[DecalIndex];

		if ((Decal.Flags & DECAL_RENDER_FLAG_BASECOLOR) == 0u)
		{
			continue;
		}

		const float3 SafeExtents = max(Decal.Extents, float3(1.0e-4f, 1.0e-4f, 1.0e-4f));
		const float3 LocalPosition = mul(float4(Input.WorldPosition, 1.0f), Decal.WorldToDecal).xyz;
		if (any(abs(LocalPosition) > SafeExtents))
		{
			continue;
		}

		float2 DecalUV = LocalPosition.yz / (SafeExtents.yz * 2.0f) + float2(0.5f, 0.5f);
		DecalUV = DecalUV * Decal.AtlasScaleBias.xy + Decal.AtlasScaleBias.zw;
		if (any(DecalUV < float2(0.0f, 0.0f)) || any(DecalUV > float2(1.0f, 1.0f)))
		{
			continue;
		}

		const float4 DecalColor = DecalBaseColorTexture.Sample(DecalSampler, DecalUV) * Decal.BaseColorTint;
		const float BlendAlpha = saturate(DecalColor.a * ComputeDecalEdgeFade(LocalPosition, SafeExtents, Decal.EdgeFade));
		ResultColor = lerp(ResultColor, DecalColor.rgb, BlendAlpha);
	}

	return float4(ResultColor, BaseColor.a);
}

#endif
