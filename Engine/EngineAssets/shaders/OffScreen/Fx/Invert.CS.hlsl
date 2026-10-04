// 反転・ソラリゼーション。ネガのように色を反転する／明るい所だけ反転する
#include "PostFxCommon.hlsli"

cbuffer InvertParams : register(b0)
{
    POSTFX_FRAME_HEADER
    int gMode;        // 0=色を反転 / 1=ソラリゼーション（しきい値より明るい所だけ反転） / 2=明るさだけ反転
    float gStrength;  // 元の色との混ぜ具合
    float gThreshold; // ソラリゼーションのしきい値
    float gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float4 source = LoadSource(pixel);
    float3 display = EncodeDisplay(source.rgb);
    float3 result = display;
    if (gMode == 0)
    {
        result = 1.0f - display;
    }
    else if (gMode == 1)
    {
        result = select(display > gThreshold, 1.0f - display, display);
    }
    else
    {
        // 色相・彩度は保ったまま明るさだけ裏返す
        float3 hsv = RgbToHsv(display);
        hsv.z = 1.0f - hsv.z;
        result = HsvToRgb(hsv);
    }
    gOutput[pixel] = float4(lerp(source.rgb, DecodeDisplay(result), gStrength), source.a);
}
