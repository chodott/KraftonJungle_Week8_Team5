#include "ShaderCommon.hlsli"

float4 main(VS_OUTPUT Input) : SV_TARGET
{
	return Input.Color;
}
