// 暗視ゴーグル。暗い所を持ち上げて緑の単色にし、ざらつき・走査線・覗き窓を足す
#include "PostFxCommon.hlsli"

cbuffer NightVisionParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float3 gTint;      // 色（緑）
    float gGain;       // 明るさの増幅
    float gNoise;      // ざらつき
    float gScanline;   // 走査線
    float gScopeSize;  // 覗き窓の大きさ
    int gScopeMode;    // 0=なし / 1=双眼 / 2=単眼
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float4 source = LoadSource(pixel);

    float l = DisplayLuminance(source.rgb * gGain);
    float noise = Hash12(float2(pixel) * 0.73f + frac(gTime * 7.31f) * 431.0f) - 0.5f;
    l = saturate(l + noise * gNoise);
    l *= 1.0f - gScanline * (0.5f + 0.5f * sin(float(pixel.y) * kPi));
    // わずかにちらつかせる
    l *= 1.0f + 0.03f * sin(gTime * 37.0f);

    float3 color = l * gTint;

    if (gScopeMode != 0)
    {
        float2 p = AspectUv(uv) - float2(AspectRatio() * 0.5f, 0.5f);
        float d;
        if (gScopeMode == 1)
        {
            float offset = gScopeSize * 0.55f;
            d = min(length(p - float2(-offset, 0.0f)), length(p - float2(offset, 0.0f)));
        }
        else
        {
            d = length(p);
        }
        float mask = 1.0f - smoothstep(gScopeSize * 0.92f, gScopeSize, d);
        color *= mask;
    }
    gOutput[pixel] = float4(DecodeDisplay(saturate(color)), source.a);
}
