#include "common.hlsli"

struct ps_output
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};

ps_output main(vs_output v, bool front : SV_IsFrontFace)
{
    ps_output output;
    output.color = saturate(mul(v.normal, -light)) * v.color;
    output.color.a = 1;
    output.depth = v.position.z;

    return output;
}