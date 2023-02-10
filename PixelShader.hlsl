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
Texture2D tex1 : register(t1);
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
        float4 bump = tex1.Sample(smplr, float2(v.tex.x, 1 - v.tex.y));
        float4 baseColor;
        float4 normal;
        
        if(material == 0)
        {
            baseColor = v.color;
            normal = v.normal;
        }
        else
        {
            baseColor = v.color * (1 - length(bump) * 0.2);
            normal = v.normal + bump / 5.0;
        }

        float b = min(v.tex.r, min(v.tex.g, v.tex.b));

        b = smoothstep(0.1, 0, b);

        float4 l = mul(world, -light);
        float4 p = normalize(float4(v.position.x / 300 - 1, v.position.y / 300 - 1, 1, 0));
        float4 n = mul(world, normal);
        float4 s = normalize(-p + l);

        float spec = pow(smoothstep(0.95, 1, mul(n, s)), 12);
    
        float4 lp = mul(light_corr, v.light_position);
        lp /= lp.w;
        if(material == 1)
            lp += float4(bump.x, bump.y, 0, 0) / 200;

        float shadow = 0;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
            {
                float4 shadowDepth = shadowMap.Sample(smplr, float2(lp.x + dx / 1000.0, lp.y + dy / 1000.0));
                shadow += shadowDepth.r > (lp.z - 0.0001f) ? 1 : 0;
            }
        
        output.color = 0.8 * ((shadow / 9.0) * (saturate(mul(normal, -light)) * baseColor + spec * float4(1, 1, 1, 1))) + 0.2 * baseColor;
    }

    output.depth = v.position.z;
    return output;
}