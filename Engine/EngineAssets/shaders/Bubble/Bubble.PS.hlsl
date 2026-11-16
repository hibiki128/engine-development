#include "Bubble.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    // 頂点の色をリニアへ変換して乗算
    float3 colorLinear = pow(max(input.color.rgb, 0.0f), 2.2f);
    float4 result = float4(colorLinear * textureColor.rgb, input.color.a * textureColor.a);
    if (result.a <= 0.0f)
    {
        discard;
    }
    return result;
}
