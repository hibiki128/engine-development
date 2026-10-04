// カラーオーバーレイ。単色・直線・円形のグラデーションを、合成モードを選んで画面に重ねる
#include "PostFxCommon.hlsli"

cbuffer ColorOverlayParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float4 gColorA;   // 色A（aは濃さ）
    float4 gColorB;   // 色B（グラデーションの終わり）
    int gBlendMode;   // 0=通常 1=乗算 2=スクリーン 3=オーバーレイ 4=加算 5=ソフトライト
    float gAngle;     // 直線グラデーションの向き（度）
    int gShape;       // 0=単色 1=直線 2=円形
    float gStrength;  // 全体の濃さ
};

float3 Blend(float3 base, float3 layer, int mode)
{
    if (mode == 1)
        return base * layer;
    if (mode == 2)
        return 1.0f - (1.0f - base) * (1.0f - layer);
    if (mode == 3)
        return select(base < 0.5f, 2.0f * base * layer, 1.0f - 2.0f * (1.0f - base) * (1.0f - layer));
    if (mode == 4)
        return base + layer;
    if (mode == 5)
        return (1.0f - 2.0f * layer) * base * base + 2.0f * layer * base;
    return layer;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float4 source = LoadSource(pixel);

    float t = 0.0f;
    if (gShape == 1)
    {
        float2 dir = float2(cos(radians(gAngle)), sin(radians(gAngle)));
        float2 p = uv - 0.5f;
        float extent = 0.5f * (abs(dir.x) + abs(dir.y));
        t = saturate(dot(p, dir) / max(extent, 1e-4f) * 0.5f + 0.5f);
    }
    else if (gShape == 2)
    {
        float2 p = AspectUv(uv) - float2(AspectRatio() * 0.5f, 0.5f);
        t = saturate(length(p) / length(float2(AspectRatio() * 0.5f, 0.5f)));
    }
    float4 layer = lerp(gColorA, gColorB, t);

    float3 base = EncodeDisplay(source.rgb);
    float3 blended = saturate(Blend(base, layer.rgb, gBlendMode));
    float amount = saturate(layer.a * gStrength);
    gOutput[pixel] = float4(DecodeDisplay(lerp(base, blended, amount)), source.a);
}
