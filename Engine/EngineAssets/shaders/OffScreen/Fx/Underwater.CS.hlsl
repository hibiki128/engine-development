// 水中。ゆらゆらした歪み・水面から差し込む光の網（コースティクス）・青い色味・奥のかすみを重ねる
#include "PostFxCommon.hlsli"

cbuffer UnderwaterParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float3 gWaterColor;     // 水の色
    float gDistortion;      // 歪みの強さ（ピクセル）
    float gCaustics;        // 光の網の明るさ
    float gTint;            // 水の色の濃さ
    float gScale;           // 模様の大きさ
    float gSpeed;           // 揺れの速さ
};

float Caustics(float2 p, float t)
{
    // 位相をずらした波を重ね、明るい筋だけを取り出す
    float c = 0.0f;
    float2 q = p;
    [unroll]
    for (int i = 0; i < 3; ++i)
    {
        q = float2(q.x + sin(q.y * 1.7f + t * (1.0f + i * 0.3f)), q.y + cos(q.x * 1.3f - t * (0.8f + i * 0.2f)));
        c += 1.0f / (1.0f + abs(sin(q.x) * sin(q.y)) * 18.0f);
    }
    return pow(saturate(c / 3.0f), 3.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float t = gTime * gSpeed;
    float2 p = AspectUv(uv) * gScale;
    float2 offset = float2(sin(p.y * 3.0f + t * 1.7f), cos(p.x * 2.7f + t * 1.3f)) * gDistortion / float2(gTextureSize);
    float4 color = SampleSourceMirror(uv + offset);

    // 上（水面に近い所）ほど光の網を強くする
    float caustic = Caustics(p * 4.0f, t * 1.5f) * gCaustics * (1.2f - uv.y);
    float3 lit = color.rgb + caustic * float3(0.8f, 1.0f, 1.0f);
    // 下へ行くほど暗く青く沈む
    float depthFade = lerp(1.0f, 0.55f, uv.y);
    float3 water = lerp(lit, lit * gWaterColor * 1.5f + gWaterColor * 0.05f, gTint) * depthFade;
    gOutput[pixel] = float4(water, color.a);
}
