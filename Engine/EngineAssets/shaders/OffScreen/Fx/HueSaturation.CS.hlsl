// 色相・彩度。色相を回す／彩度を上げ下げする／くすんだ色だけ鮮やかにする（自然な彩度）
#include "PostFxCommon.hlsli"

cbuffer HueSaturationParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gHueShift;   // 色相を回す量（度）
    float gSaturation; // 彩度の倍率
    float gVibrance;   // くすんだ色ほど強く彩度を上げる量
    float gBrightness; // 明るさの倍率
    float gHueSpeed;   // 色相を回し続ける速さ（度/秒）
    float gContrast;   // コントラスト
    float2 gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float4 source = LoadSource(pixel);
    float3 display = EncodeDisplay(source.rgb);

    float3 hsv = RgbToHsv(display);
    hsv.x = frac(hsv.x + (gHueShift + gHueSpeed * gTime) / 360.0f);
    // 自然な彩度: すでに鮮やかな色はあまり上げない
    float vibranceBoost = gVibrance * (1.0f - hsv.y);
    hsv.y = saturate(hsv.y * gSaturation * (1.0f + vibranceBoost));
    float3 rgb = HsvToRgb(hsv);
    rgb = (rgb - 0.5f) * gContrast + 0.5f;
    rgb *= gBrightness;

    gOutput[pixel] = float4(DecodeDisplay(saturate(rgb)), source.a);
}
