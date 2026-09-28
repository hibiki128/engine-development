// =============================================================
// 歪み（熱揺らぎ）
//
// 強いオーラのまわりの空気が揺れて、向こう側の景色が歪んで見えるやつ。
//
// 素材（法線テクスチャ）を1枚も使わずに出すために、
// ブルームが作った**パーティクルだけのミップ**をそのままマスクに使う。
// ミップは縮小済みなのでマスクが自然に外側へ滲んでおり、
// 「粒の少し外側まで空気が揺れる」という欲しい形になっている。
//
// 歪ませるのは**背景だけ**で、パーティクル自身は歪ませない。
//   背景     = gBeforeParticle（粒を描く前のシーンの控え）
//   粒の寄与 = シーン - 背景（加算ブレンドなので差がそのまま寄与）
// ずらした背景に、ずらしていない粒の寄与を足し直す。
// こうしないとオーラ本体までぐにゃぐにゃになって「ただの下手なブラー」に見える。
//
// ※ シーンは UAV として同じ画素だけを読み書きするので競合しない。
// =============================================================

cbuffer DistortionParams : register(b0)
{
    int2 gDstSize;      // 出力（全解像度）の寸法
    float gStrength;    // ずらす量（UV空間）
    float gFrequency;   // ゆらぎの細かさ
    float gSpeed;       // ゆらぎが動く速さ
    float gTime;        // 経過時間
    float gMaskGain;    // マスクの効き（明るさ→揺れ量の倍率）
    float gPad0;
};

Texture2D<float4> gBeforeParticle : register(t0); // 粒を描く前のシーン（背景）
Texture2D<float4> gMask : register(t1);           // パーティクルだけのミップ（縮小済み＝ぼけている）
RWTexture2D<float4> gScene : register(u0);        // 粒を描いた後のシーン。ここを書き換える
SamplerState gSampler : register(s0);

float Hash21(float2 p)
{
    return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0f, 0.0f));
    float c = Hash21(i + float2(0.0f, 1.0f));
    float d = Hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// 2オクターブ。熱揺らぎは「大きくゆっくり」が主なので、これ以上は要らない
float Fbm2(float2 p)
{
    return ValueNoise(p) * 0.65f + ValueNoise(p * 2.11f + 13.7f) * 0.35f;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchThreadID.xy);
    if (pixel.x >= gDstSize.x || pixel.y >= gDstSize.y)
    {
        return;
    }

    float2 uv = (float2(pixel) + 0.5f) / float2(gDstSize);

    // ぼけたパーティクルの明るさがそのまま「どれくらい空気が揺れているか」になる
    float3 maskColor = gMask.SampleLevel(gSampler, uv, 0).rgb;
    float mask = saturate(dot(maskColor, float3(0.2126f, 0.7152f, 0.0722f)) * gMaskGain);
    if (mask <= 0.002f)
    {
        return; // 粒が無いところは触らない（画面のほとんどはここで抜ける）
    }

    float4 scene = gScene[pixel];
    float3 background = gBeforeParticle.SampleLevel(gSampler, uv, 0).rgb;
    // 加算ブレンドの粒が足した色。負にならないよう切っておく
    float3 particle = max(scene.rgb - background, 0.0f);

    // 上へ流れる2枚のノイズで x/y のずらし量を作る。
    // x と y で別の位置を引かないと斜め一方向にしか動かず、揺らぎに見えない
    float2 flow = float2(0.0f, gTime * gSpeed);
    float2 offset;
    offset.x = Fbm2(uv * gFrequency + flow) - 0.5f;
    offset.y = Fbm2(uv * gFrequency + flow + float2(37.2f, 11.9f)) - 0.5f;
    offset *= gStrength * mask;

    float3 warped = gBeforeParticle.SampleLevel(gSampler, uv + offset, 0).rgb;

    gScene[pixel] = float4(warped + particle, scene.a);
}
