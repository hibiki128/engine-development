// 膨らみ・へこみ。中心を虫眼鏡のように膨らませる（＋）／吸い込むように縮める（−）
#include "PostFxCommon.hlsli"

cbuffer BulgeParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float2 gCenter;   // 中心（UV）
    float gRadius;    // 範囲（画面の高さ=1）
    float gStrength;  // ＋で膨らむ / −でへこむ
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float aspect = AspectRatio();
    float2 p = (uv - gCenter) * float2(aspect, 1.0f);
    float d = length(p);
    float radius = max(gRadius, 1e-4f);
    if (d < radius)
    {
        // 読む位置を中心へ寄せる（＋）／外へ離す（−）。ふち（t=1）では元の位置に戻るので継ぎ目が出ない
        float t = max(d / radius, 1e-4f);
        p *= pow(t, clamp(gStrength, -0.9f, 4.0f));
    }
    float2 sampleUv = p / float2(aspect, 1.0f) + gCenter;
    gOutput[pixel] = SampleSourceMirror(sampleUv);
}
