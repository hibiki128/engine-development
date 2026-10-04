// セル画風（トゥーン）。明るさを数段にまとめて塗り分け、色の境目に線を引く
#include "PostFxCommon.hlsli"

cbuffer ToonParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float3 gLineColor;   // 線の色
    float gBands;        // 明るさの段の数
    float gLineThreshold; // 線を引く色の差
    float gLineWidth;    // 線の太さ（ピクセル）
    float gLineStrength; // 線の濃さ
    float gSaturation;   // 彩度の倍率
};

float EdgeAt(float2 uv, float2 offset)
{
    // ソーベルフィルタで色の変わり目の強さを取る
    float tl = DisplayLuminance(SampleSource(uv + float2(-offset.x, -offset.y)).rgb);
    float tc = DisplayLuminance(SampleSource(uv + float2(0, -offset.y)).rgb);
    float tr = DisplayLuminance(SampleSource(uv + float2(offset.x, -offset.y)).rgb);
    float ml = DisplayLuminance(SampleSource(uv + float2(-offset.x, 0)).rgb);
    float mr = DisplayLuminance(SampleSource(uv + float2(offset.x, 0)).rgb);
    float bl = DisplayLuminance(SampleSource(uv + float2(-offset.x, offset.y)).rgb);
    float bc = DisplayLuminance(SampleSource(uv + float2(0, offset.y)).rgb);
    float br = DisplayLuminance(SampleSource(uv + float2(offset.x, offset.y)).rgb);
    float gx = (tr + 2.0f * mr + br) - (tl + 2.0f * ml + bl);
    float gy = (bl + 2.0f * bc + br) - (tl + 2.0f * tc + tr);
    return length(float2(gx, gy));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float4 source = LoadSource(pixel);
    float3 display = EncodeDisplay(source.rgb);

    // 明るさ（HSVのV）だけを段にする。色相は保つので塗り分けた色に見える
    float3 hsv = RgbToHsv(display);
    float bands = max(gBands, 1.0f);
    hsv.z = ceil(hsv.z * bands) / bands;
    hsv.y = saturate(hsv.y * gSaturation);
    float3 color = HsvToRgb(hsv);

    float edge = EdgeAt(uv, gLineWidth / float2(gTextureSize));
    float ink = smoothstep(gLineThreshold, gLineThreshold * 1.5f + 0.02f, edge) * gLineStrength;
    color = lerp(color, gLineColor, saturate(ink));

    gOutput[pixel] = float4(DecodeDisplay(color), source.a);
}
