// 窓・レンズの水滴。止まった水滴と、筋を引いて流れ落ちる水滴が景色を屈折させる。
// 水滴の無い所は少しぼかして、ガラス越しに見ているようにする
#include "PostFxCommon.hlsli"

cbuffer RainLensParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gAmount;     // 水滴の量（0〜1）
    float gSize;       // 水滴の大きさ
    float gSpeed;      // 流れ落ちる速さ
    float gBlur;       // ガラスのくもり（ぼかし。ピクセル）
    float gRefraction; // 屈折の強さ
    float3 gPadding;
};

/// 止まっている水滴。返り値の xy は屈折のずれ、z は水滴の濃さ
float3 StaticDrops(float2 uv, float scale)
{
    float2 p = uv * scale;
    float2 cell = floor(p);
    float2 local = frac(p) - 0.5f;
    float2 rnd = Hash22(cell);
    float present = step(1.0f - gAmount, Hash12(cell + 3.1f));
    float2 center = (rnd - 0.5f) * 0.6f;
    float radius = lerp(0.08f, 0.22f, Hash12(cell + 7.7f));
    float2 d = local - center;
    float drop = smoothstep(radius, radius * 0.6f, length(d)) * present;
    return float3(d * drop, drop);
}

/// 流れ落ちる水滴（列ごとに1つ）と、後ろに残る細かい水の筋
float3 FallingDrops(float2 uv, float scale, float t)
{
    float2 p = uv * float2(scale, scale * 0.25f);
    float column = floor(p.x);
    float rnd = Hash11(column * 13.7f);
    float present = step(1.0f - gAmount * 0.8f, rnd);
    float x = frac(p.x) - 0.5f + (Hash11(column) - 0.5f) * 0.4f;
    // 落ちる位置（y は上から下へ）。のこぎり波にゆらぎを足して、止まっては落ちる感じを出す
    float speed = lerp(0.5f, 1.2f, Hash11(column + 1.3f)) * gSpeed;
    float phase = t * speed + rnd * 10.0f;
    float fall = frac(phase) ;
    fall = fall + sin(phase * 6.0f) * 0.02f;
    float y = frac(p.y) - fall;
    float2 d = float2(x * 1.6f, y * 4.0f);
    float drop = smoothstep(0.22f, 0.12f, length(d)) * present;
    // 通り過ぎた跡（上側）に小さな水の粒
    float trail = smoothstep(0.06f, 0.0f, abs(x)) * step(0.0f, -y) * smoothstep(-0.6f, 0.0f, y) * present;
    trail *= step(0.5f, frac(p.y * 8.0f + rnd));
    return float3(d * drop * 0.6f, max(drop, trail * 0.5f));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float2 aspectUv = AspectUv(uv);
    float scale = 12.0f / max(gSize, 0.1f);

    float3 drops = StaticDrops(aspectUv, scale);
    float3 small = StaticDrops(aspectUv + 0.37f, scale * 2.3f);
    float3 falling = FallingDrops(aspectUv, scale * 0.5f, gTime);
    float2 offset = drops.xy + small.xy * 0.5f + falling.xy;
    float mask = saturate(drops.z + small.z + falling.z);

    // 水滴の中は逆さまに近い屈折像（くっきり）、外はくもったガラス（ぼかし）
    float2 refractUv = uv - offset * gRefraction * 0.25f;
    float4 sharp = SampleSourceMirror(refractUv);

    float2 texel = 1.0f / float2(gTextureSize);
    float4 blurred = 0.0f;
    const float2 taps[8] = {float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
                            float2(0.7f, 0.7f), float2(-0.7f, 0.7f), float2(0.7f, -0.7f), float2(-0.7f, -0.7f)};
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        blurred += SampleSource(uv + taps[i] * texel * gBlur);
    }
    blurred = (blurred + LoadSource(pixel)) / 9.0f;

    // 水滴のふちは少し暗く、てっぺんに小さな映り込みの光
    float rim = saturate(mask * (1.0f - mask) * 4.0f) * 0.25f;
    float4 color = lerp(blurred, sharp * (1.0f - rim), mask);
    gOutput[pixel] = color;
}
