cbuffer FrameData : register(b0)
{
	float4x4 View;
	float4x4 Projection;
	float4   CameraPosition;
	float    Time;
	float    DeltaTime;
	float2   FramePadding;
};

cbuffer ObjectData : register(b1)
{
	float4x4 WorldMatrix;
	float4x4 WorldInvTranspose;
	uint     LocalLightListOffset;
	uint     LocalLightListCount;
	uint     ObjectFlags;
	uint     ObjectUUID;
	uint     Pad0; uint Pad1; uint Pad2; uint Pad3;
};

cbuffer ShadowPassData : register(b2)
{
	float4x4 LightViewProj;
	float4   ShadowParams;
};
struct VSInput
{
	float3 PositionOS : POSITION;

};
float4 main(float3 PositionOS : POSITION) : SV_POSITION
{
	// local to world
	float4 PositionWS = mul(float4(PositionOS, 1.0f), WorldMatrix);
    
	// world space to light space
	return mul(PositionWS, LightViewProj);
}