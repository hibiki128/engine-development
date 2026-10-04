// 一色だけ残す（カラーキー）。指定した色に近い所だけ色を残し、他は白黒にする
#include "PostFxCommon.hlsli"

cbuffer ColorIsolationParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float3 gTargetColor;    // 残す色
    float gHueRange;        // 色相の許容幅（度）
    float gSoftness;        // 境目のなめらかさ（度）
    float gOtherSaturation; // 残さない所の彩度（0で白黒）
    float gBoost;           // 残した色の彩度の倍率
    float gMinSaturation;   // これより彩度の低い色（灰色に近い色）は対象外
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float4 source = LoadSource(pixel);
    float3 display = EncodeDisplay(source.rgb);
    float3 hsv = RgbToHsv(display);
    float targetHue = RgbToHsv(saturate(gTargetColor)).x;

    float hueDistance = abs(hsv.x - targetHue);
    hueDistance = min(hueDistance, 1.0f - hueDistance) * 360.0f;
    float keep = 1.0f - smoothstep(gHueRange, gHueRange + max(gSoftness, 0.001f), hueDistance);
    keep *= smoothstep(gMinSaturation, gMinSaturation + 0.1f, hsv.y);

    float3 other = hsv;
    other.y *= gOtherSaturation;
    float3 boosted = hsv;
    boosted.y = saturate(boosted.y * gBoost);
    float3 result = lerp(HsvToRgb(other), HsvToRgb(boosted), keep);
    gOutput[pixel] = float4(DecodeDisplay(result), source.a);
}
