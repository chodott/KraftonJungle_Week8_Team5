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

float3 GetCameraPos()
{
	float3x3 rotation = (float3x3)View;
	float3 translation = float3(View[0][3], View[1][3], View[2][3]);
	return -mul(rotation, translation);
}

float4 main(VS_OUTPUT input) : SV_Target
{
	const float3 cameraPos = GetCameraPos();
	const float dist = distance(input.WorldPos, cameraPos);
	const float maxDistance = 1000.0f;
	const float fade = pow(saturate(1.0f - dist / maxDistance), 2.0f);

	const float3 gridAxisU = normalize(GridAxisU.xyz);
	const float3 gridAxisV = normalize(GridAxisV.xyz);
	const float2 gridCoord = float2(dot(input.WorldPos, gridAxisU), dot(input.WorldPos, gridAxisV));
	const float2 derivative = max(fwidth(gridCoord), 0.0001f);
	const float2 coord = gridCoord / GridSize;
	const float2 grid = abs(frac(coord - 0.5f) - 0.5f) / max(derivative / GridSize, 0.0001f);
	const float lineAlpha = saturate(LineThickness - min(grid.x, grid.y)) * fade;

	if (lineAlpha < 0.01f)
	{
		discard;
	}

	return float4(float3(0.5f, 0.5f, 0.5f), lineAlpha);
}
