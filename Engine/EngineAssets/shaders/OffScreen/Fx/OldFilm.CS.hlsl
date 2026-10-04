// 古いフィルム。セピア・縦の傷・ほこり・明るさのちらつき・コマのがたつき・粒子・周辺減光。
// コマ送りの速さを落とすと昔の映写機のようなカクカクした動きになる
#include "PostFxCommon.hlsli"

cbuffer OldFilmParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gSepia;     // セピアの強さ
    float gScratches; // 縦の傷の多さ
    float gDust;      // ほこりの多さ
    float gFlicker;   // 明るさのちらつき
    float gVignette;  // 周辺減光
    float gJitter;    // コマのがたつき
    float gGrain;     // 粒子
    float gFrameRate; // コマの速さ（傷・ほこり・がたつきが変わる回数/秒）
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float frame = floor(gTime * max(gFrameRate, 1.0f));

    // コマのがたつき（縦に少し跳ねる）
    float2 jitter = float2(Hash11(frame * 1.3f) - 0.5f, Hash11(frame * 2.1f) - 0.5f) * gJitter * float2(0.002f, 0.01f);
    float3 color = EncodeDisplay(SampleSourceMirror(uv + jitter).rgb);

    // セピア
    float l = Luminance(color);
    float3 sepia = float3(l * 1.07f, l * 0.88f, l * 0.68f);
    color = lerp(color, sepia, gSepia);

    // 明るさのちらつき
    color *= 1.0f + (Hash11(frame * 3.7f) - 0.5f) * gFlicker * 0.3f;

    // 縦の傷（コマごとに場所が変わる）
    [unroll]
    for (int k = 0; k < 3; ++k)
    {
        float present = step(1.0f - gScratches, Hash11(frame * 5.1f + k * 17.0f));
        float x = Hash11(frame * 7.7f + k * 3.0f);
        float width = 0.0006f + Hash11(frame + k) * 0.0008f;
        float scratch = smoothstep(width, 0.0f, abs(uv.x - x - sin(uv.y * 10.0f + k) * 0.002f)) * present;
        // 途中で途切れる
        scratch *= step(0.3f, ValueNoise(float2(uv.y * 8.0f, frame + k)));
        color = lerp(color, (k == 0) ? float3(0.9f, 0.9f, 0.85f) : float3(0.1f, 0.08f, 0.06f), scratch * 0.7f);
    }

    // ほこり（黒い小さな点）
    float2 dustCell = floor(float2(pixel) / 6.0f);
    float dustRandom = Hash12(dustCell + frame * 31.0f);
    if (dustRandom > 1.0f - gDust * 0.004f)
    {
        float2 local = frac(float2(pixel) / 6.0f) - 0.5f;
        float blob = smoothstep(0.5f, 0.1f, length(local) + ValueNoise(local * 4.0f + frame) * 0.3f);
        color = lerp(color, float3(0.05f, 0.04f, 0.03f), blob);
    }

    // 粒子
    color += (Hash12(float2(pixel) + frac(gTime * 17.0f) * 300.0f) - 0.5f) * gGrain;

    // 周辺減光
    float2 v = uv * (1.0f - uv);
    color *= lerp(1.0f, pow(saturate(v.x * v.y * 16.0f), 0.4f), gVignette);

    gOutput[pixel] = float4(DecodeDisplay(saturate(color)), 1.0f);
}
