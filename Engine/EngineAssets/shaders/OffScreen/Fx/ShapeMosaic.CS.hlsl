// 形モザイク。六角形・円（ドット）・三角形・LED・ひし形のマスで画面を塗り分ける
#include "PostFxCommon.hlsli"

cbuffer ShapeMosaicParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float3 gBackgroundColor; // マスの隙間の色
    float gCellSize;         // マスの大きさ（ピクセル）
    int gShape;              // 0=六角形 / 1=円 / 2=三角形 / 3=LED / 4=ひし形
    float gGap;              // マスの隙間（0〜1）
    float gGlow;             // LEDの光のにじみ
    float gStrength;         // 元の色との混ぜ具合
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 p = float2(pixel) + 0.5f;
    float cell = max(gCellSize, 2.0f);
    float4 source = LoadSource(pixel);
    float2 centerPixel = p;
    float inside = 1.0f;

    if (gShape == 0)
    {
        // 六角形: 2つずらした格子のうち近い方の中心を使う（隣の中心までの距離が1マス）
        const float2 s = float2(1.0f, 1.7320508f);
        float2 q = p / cell;
        float4 hc = floor(float4(q, q - float2(0.5f, 1.0f)) / s.xyxy) + 0.5f;
        float2 a = hc.xy * s;
        float2 b = (hc.zw + 0.5f) * s;
        float2 center = (dot(q - a, q - a) < dot(q - b, q - b)) ? a : b;
        centerPixel = center * cell;
        float2 local = abs(q - center);
        // 尖った頂点が上下に来る六角形。辺までの距離が 0.5
        float hexDistance = max(dot(local, float2(0.5f, 0.8660254f)), local.x);
        float edgeDistance = 0.5f * (1.0f - gGap);
        inside = 1.0f - smoothstep(edgeDistance - 0.03f, edgeDistance, hexDistance);
    }
    else if (gShape == 1 || gShape == 3)
    {
        centerPixel = (floor(p / cell) + 0.5f) * cell;
        float d = length(p - centerPixel) / (cell * 0.5f);
        float radius = 1.0f - gGap;
        inside = 1.0f - smoothstep(radius - 0.08f, radius, d);
        if (gShape == 3)
        {
            // LED: 芯は明るく、まわりへ光がにじむ
            inside = saturate(inside * 1.2f + exp(-d * d * 3.0f) * gGlow);
        }
    }
    else if (gShape == 2)
    {
        // 三角形: 正方形のマスを対角線で2つに割り、それぞれの重心で色を取る
        float2 index = floor(p / cell);
        float2 local = frac(p / cell);
        bool upper = (local.x + local.y) < 1.0f;
        centerPixel = (index + (upper ? float2(1.0f / 3.0f, 1.0f / 3.0f) : float2(2.0f / 3.0f, 2.0f / 3.0f))) * cell;
        float edge = upper ? min(min(local.x, local.y), 1.0f - local.x - local.y) : min(min(1.0f - local.x, 1.0f - local.y), local.x + local.y - 1.0f);
        inside = smoothstep(gGap * 0.25f, gGap * 0.25f + 0.03f, edge);
    }
    else
    {
        // ひし形: 45度回した格子
        float2 r = Rotate2D(p, 0.785398f) / (cell * 0.7071f);
        float2 index = floor(r);
        float2 local = frac(r) - 0.5f;
        centerPixel = Rotate2D((index + 0.5f) * cell * 0.7071f, -0.785398f);
        float d = max(abs(local.x), abs(local.y)) * 2.0f;
        inside = 1.0f - smoothstep(1.0f - gGap - 0.06f, 1.0f - gGap, d);
    }

    float3 cellColor = SampleSource(centerPixel / float2(gTextureSize)).rgb;
    if (gShape == 3)
    {
        cellColor *= 1.0f + gGlow;
    }
    float3 color = lerp(gBackgroundColor, cellColor, inside);
    gOutput[pixel] = float4(lerp(source.rgb, color, gStrength), source.a);
}
