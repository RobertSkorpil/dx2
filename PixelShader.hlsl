#include "common.hlsli"

struct ps_output
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};

ps_output main(vs_output v)
{
    ps_output output;

    double zr = 0, zi = 0;
    
    float b = min(v.tex.r, min(v.tex.g, v.tex.b));

    b = smoothstep(0.1, 0, b);
    
    output.color = float4(b, b, b, 1);
    output.depth = v.position.z;
    return output;
}