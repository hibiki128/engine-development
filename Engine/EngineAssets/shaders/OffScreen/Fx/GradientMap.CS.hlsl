// グラデーションマップ（デュオトーン）。明るさを2〜3色のグラデーションへ置き換える
#include "PostFxCommon.hlsli"

cbuffer GradientMapParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float3 gShadowColor;    // 暗い所の色
    float gStrength;        // 元の色との混ぜ具合
    float3 gMidColor;       // 中間の色（3色のとき）
    float gMidPoint;        // 中間の色を置く明るさ
    float3 gHighlightColor; // 明るい所の色
    int gUseMid;            // 1なら3色
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float4 source = LoadSource(pixel);
    float l = DisplayLuminance(source.rgb);
    float3 mapped;
    if (gUseMid != 0)
    {
        float mid = clamp(gMidPoint, 0.01f, 0.99f);
        mapped = (l < mid) ? lerp(gShadowColor, gMidColor, l / mid)
                           : lerp(gMidColor, gHighlightColor, (l - mid) / (1.0f - mid));
    }
    else
    {
        mapped = lerp(gShadowColor, gHighlightColor, l);
    }
    gOutput[pixel] = float4(lerp(source.rgb, DecodeDisplay(mapped), gStrength), source.a);
}
