// ブラウン管（CRTモニター）。画面の丸み・走査線・RGBの縦じま（アパーチャーグリル）・にじみ・ちらつき
#include "PostFxCommon.hlsli"

cbuffer CrtParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gCurvature;  // 画面の丸み
    float gScanline;   // 走査線の濃さ
    float gMask;       // RGBの縦じまの濃さ
    float gVignette;   // 四隅の暗さ
    float gFlicker;    // ちらつき
    float gGlow;       // 光のにじみ
    float gChroma;     // 色ずれ（ピクセル）
    float gLineSize;   // 走査線1本の太さ（ピクセル）
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    // 画面の丸み（樽型に膨らませる）
    float2 c = uv * 2.0f - 1.0f;
    c *= 1.0f + (c.yx * c.yx) * gCurvature * 0.25f;
    float2 curvedUv = c * 0.5f + 0.5f;
    if (any(curvedUv < 0.0f) || any(curvedUv > 1.0f))
    {
        gOutput[pixel] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float2 texel = 1.0f / float2(gTextureSize);
    float3 color;
    color.r = SampleSource(curvedUv + float2(gChroma * texel.x, 0.0f)).r;
    color.g = SampleSource(curvedUv).g;
    color.b = SampleSource(curvedUv - float2(gChroma * texel.x, 0.0f)).b;

    // にじみ: まわりを少し足す
    float3 glow = SampleSource(curvedUv + float2(texel.x * 2.0f, 0)).rgb + SampleSource(curvedUv - float2(texel.x * 2.0f, 0)).rgb +
                  SampleSource(curvedUv + float2(0, texel.y * 2.0f)).rgb + SampleSource(curvedUv - float2(0, texel.y * 2.0f)).rgb;
    color += glow * 0.25f * gGlow;

    // 走査線（曲がった画面に沿う）
    float lineSize = max(gLineSize, 1.0f);
    float scan = 0.5f + 0.5f * cos(curvedUv.y * gTextureSize.y / lineSize * kTwoPi);
    color *= lerp(1.0f, scan * 0.6f + 0.4f, gScanline);

    // アパーチャーグリル: 1ピクセルごとに R・G・B の縦じま
    int column = pixel.x % 3;
    float3 mask = (column == 0) ? float3(1.0f, 0.3f, 0.3f) : (column == 1) ? float3(0.3f, 1.0f, 0.3f) : float3(0.3f, 0.3f, 1.0f);
    color *= lerp(float3(1.0f, 1.0f, 1.0f), mask * 1.6f, gMask);

    // 四隅を暗く
    float2 v = curvedUv * (1.0f - curvedUv);
    float vignette = pow(saturate(v.x * v.y * 16.0f), 0.35f);
    color *= lerp(1.0f, vignette, gVignette);

    color *= 1.0f + (sin(gTime * 120.0f) * 0.5f + Hash11(floor(gTime * 30.0f)) - 0.5f) * gFlicker * 0.1f;
    gOutput[pixel] = float4(color, 1.0f);
}
