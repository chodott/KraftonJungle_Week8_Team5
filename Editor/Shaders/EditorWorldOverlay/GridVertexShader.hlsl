cbuffer FrameData : register(b0)
{
	float4x4 View;
	float4x4 Projection;
};

cbuffer CameraBuffer : register(b2)
{
	float GridSize;
	float LineThickness;
	float2 Padding0;
	float4 GridAxisU;
	float4 GridAxisV;
	float4 ViewForward;
};

struct VS_OUTPUT
{
	float4 Pos : SV_POSITION;
	float3 WorldPos : TEXCOORD0;
};

static const uint VerticesPerQuad = 6;
static const float Range = 1000.0f;

float2 GetQuadCorner(uint CornerIndex)
{
	float2 corners[VerticesPerQuad] =
	{
		float2(-1.0f, -1.0f),
		float2(1.0f, -1.0f),
		float2(-1.0f, 1.0f),
		float2(1.0f, -1.0f),
		float2(-1.0f, 1.0f),
		float2(1.0f, 1.0f)
	};

	return corners[CornerIndex];
}

VS_OUTPUT main(uint id : SV_VertexID)
{
	VS_OUTPUT output;

	const float2 quadCorner = GetQuadCorner(id % VerticesPerQuad);
	const float3 gridAxisU = normalize(GridAxisU.xyz);
	const float3 gridAxisV = normalize(GridAxisV.xyz);
	const float3 localPos = gridAxisU * quadCorner.x + gridAxisV * quadCorner.y;
	const float3 worldPosition = localPos * Range;

	output.WorldPos = worldPosition;
	output.Pos = mul(float4(worldPosition, 1.0f), mul(View, Projection));
	return output;
}
