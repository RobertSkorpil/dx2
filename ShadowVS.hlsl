#include "common.hlsli"

float4 main(vs_input input) : SV_POSITION
{
    return mul(light_mat, mul(object, input.position));
}