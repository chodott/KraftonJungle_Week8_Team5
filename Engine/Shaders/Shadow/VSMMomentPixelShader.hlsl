struct VSMMomentPSInput
{
    float4 Position : SV_POSITION;
};

float2 main(VSMMomentPSInput Input) : SV_Target0
{
    float depth = saturate(Input.Position.z);

    float dx = ddx(depth);
    float dy = ddy(depth);

    float moment1 = depth;
    float moment2 = depth * depth + 0.25f * (dx * dx + dy * dy);

    return float2(moment1, moment2);
}
