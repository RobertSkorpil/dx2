#include "common.hlsli"

struct ps_output
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};

struct shadowTexel
{
    float3 color;
    int stencil;
};

Texture2D shadowMap : register(t0);
SamplerState smplr : register(s0);

ps_output main(vs_output v)
{
    ps_output output;

    float4x4 light_corr =
    {
        0.5, 0, 0, 0.5,
        0, -0.5, 0, 0.5,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    if(v.type == 1)
        output.color = float4(.8, .8, .8, 1.0);
    else
    {
        float b = min(v.tex.r, min(v.tex.g, v.tex.b));

        b = smoothstep(0.1, 0, b);

        float4 l = mul(world, -light);
        float4 p = normalize(float4(v.position.x / 300 - 1, v.position.y / 300 - 1, 1, 0));
        float4 n = mul(world, v.normal);
        float4 s = normalize(-p + l);

        float spec = pow(smoothstep(0.95, 1, mul(n, s)), 12);
    
        float4 lp = mul(light_corr, v.light_position);
        lp /= lp.w;
        float shadow = 0;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
            {
                float4 shadowDepth = shadowMap.Sample(smplr, float2(lp.x + dx / 1000.0, lp.y + dy / 1000.0));
                shadow += shadowDepth.r > (lp.z - 0.0001f) ? 1 : 0.3;
            }
        

        output.color = (shadow / 9) * (mul(v.normal, -light) * v.color + spec * float4(1, 1, 1, 1));
    }

    output.depth = v.position.z;
    return output;
}