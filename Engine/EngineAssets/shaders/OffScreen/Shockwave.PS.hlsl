#include "FullScreen.hlsli"

Texture2D<float4> gTexture : register(t0);
Texture2D<float4> gFlareTex : register(t1);
SamplerState gSampler : register(s0);

cbuffer ShockwaveBuffer : register(b0)
{
    float2 gCenter;
    float gTime;
    float gDuration;
    float gAmplitude;
    float gFrequency;
    float gWaveSpeed;
    float gActive;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float2 uv = input.texcoord;

    if (gActive < 0.5 || gTime > gDuration)
    {
        output.color = gTexture.Sample(gSampler, uv);
        return output;
    }

    const float kAspect = 16.0 / 9.0;
    float t = saturate(gTime / gDuration);
    float attack = saturate(gTime / 0.025);
    float decay = (1.0 - t) * (1.0 - t);
    float envelope = attack * decay;

    // ===== flare.png をメインフラッシュとしてサンプル =====
    // 視覚的に円に見えるよう X 側を kAspect で補正
    float flareScale = lerp(0.20, 0.40, t); // 縦UV半径
    float2 flareUV = (uv - gCenter) / float2(flareScale / kAspect, flareScale) * 0.5 + 0.5;

    float3 flareColor = float3(0.0, 0.0, 0.0);
    if (flareUV.x >= 0.0 && flareUV.x <= 1.0 && flareUV.y >= 0.0 && flareUV.y <= 1.0)
    {
        float4 flareSample = gFlareTex.Sample(gSampler, flareUV);
        // αプリマルチで加算合成に向ける、強さもブースト
        flareColor = flareSample.rgb * flareSample.a * envelope * gAmplitude * 4.0;
    }

    // ===== 補助：色収差のみ（控えめに） =====
    float2 toCenter = (uv - gCenter) * float2(kAspect, 1.0);
    float dist = length(toCenter);
    float2 dirAspect = (dist > 1e-4) ? (toCenter / dist) : float2(0.0, 0.0);
    float2 dir = dirAspect / float2(kAspect, 1.0);
    float caStrength = 0.010 * decay
                     * smoothstep(0.0, 0.05, dist)
                     * (1.0 - smoothstep(0.35, 0.55, dist));
    float3 baseColor;
    baseColor.r = gTexture.Sample(gSampler, uv + dir * caStrength).r;
    baseColor.g = gTexture.Sample(gSampler, uv).g;
    baseColor.b = gTexture.Sample(gSampler, uv - dir * caStrength).b;

    // ===== 加算合成（flare がメイン） =====
    output.color = float4(baseColor + flareColor, 1.0);
    return output;
}
