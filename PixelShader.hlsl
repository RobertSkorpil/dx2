#include "common.hlsli"

struct ps_output
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};

ps_output main(vs_output v)
{
    ps_output output;

    float b = min(v.tex.r, min(v.tex.g, v.tex.b));

    b = smoothstep(0.1, 0, b);

    float4 l = mul(world, -light);
    float4 p = normalize(float4(v.position.x / 300 - 1, v.position.y / 300 - 1, 1, 0));
    float4 n = mul(world, v.normal);
    float4 s = normalize(-p + l);

    float spec = pow(smoothstep(0.95, 1, mul(n, s)), 12);
    
    output.color = mul(v.normal, -light) * float4(1, 0, 0, 1) + spec * float4(1, 1, 1, 1);;
    output.depth = v.position.z;
    return output;
}