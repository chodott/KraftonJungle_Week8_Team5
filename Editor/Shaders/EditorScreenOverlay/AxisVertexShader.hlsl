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
	float AxisDistance : TEXCOORD1;
	float AxisVisibility : TEXCOORD2;
	nointerpolation int AxisNo : TEXCOORD3;
};

static const uint VerticesPerQuad = 6;
static const float AxisThickness = 0.001f;
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

float3 GetAxisDirection(uint AxisNo)
{
	if (AxisNo == 0)
	{
		return float3(1.0f, 0.0f, 0.0f);
	}

	if (AxisNo == 1)
	{
		return float3(0.0f, 1.0f, 0.0f);
	}

	return float3(0.0f, 0.0f, 1.0f);
}

float3 GetAxisThicknessDirection(uint AxisNo, uint Variant)
{
	if (AxisNo == 0)
	{
		return (Variant == 0) ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
	}

	if (AxisNo == 1)
	{
		return (Variant == 0) ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
	}

	return (Variant == 0) ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f);
}

VS_OUTPUT main(uint id : SV_VertexID)
{
	VS_OUTPUT output;

	const float2 quadCorner = GetQuadCorner(id % VerticesPerQuad);
	const uint axisQuadIndex = id / VerticesPerQuad;
	const uint currentAxis = axisQuadIndex / 2;
	const uint thicknessVariant = axisQuadIndex % 2;
	const float3 viewForward = normalize(ViewForward.xyz);
	const float3 axisDirection = GetAxisDirection(currentAxis);
	const float3 thicknessDirection = GetAxisThicknessDirection(currentAxis, thicknessVariant);
	const float thickness = quadCorner.y * AxisThickness;
	const float3 localPos = axisDirection * quadCorner.x + thicknessDirection * thickness;
	const float3 worldPosition = localPos * Range;

	output.WorldPos = worldPosition;
	output.AxisDistance = thickness * Range;
	output.AxisVisibility = 1.0f - abs(dot(viewForward, axisDirection));
	output.AxisNo = currentAxis;
	output.Pos = mul(float4(worldPosition, 1.0f), mul(View, Projection));
	return output;
}
