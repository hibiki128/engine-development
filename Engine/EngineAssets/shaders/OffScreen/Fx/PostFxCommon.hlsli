// =============================================================
// ポストエフェクト（コンピュートシェーダー版）の共通部品
//
// OffScreen/Fx/ 以下のエフェクトはすべてこれを読み込む。
//   ・定数バッファの先頭は POSTFX_FRAME_HEADER（解像度・経過時間）にそろえる。
//     C++ 側の PostFxFrameHeader と同じ並び。
//   ・入力 t0 はそのエフェクトへの入力画像（HDR のリニア色。トーンマップ前）。
//   ・出力 u0 はピンポンバッファ（FP16）。
//   ・s0 は線形クランプ、s1 はポイントクランプ（PostEffectRenderer がこの並びで差す）。
//
// 入力は HDR なので 1 を超える値が普通に来る。
// 「見た目の明るさ」で判断したい処理（2値化・階調を減らす等）は
// EncodeDisplay で 0〜1 の見た目の空間へ移してから行い、DecodeDisplay で戻す。
// 見た目の空間は「既定のトーンマップ（ACES・露出1）を掛けてガンマを掛けた後」に合わせてあるので、
// その空間で作った色はほぼそのままの色で画面に出る。
// =============================================================

#ifndef POSTFX_COMMON_HLSLI
#define POSTFX_COMMON_HLSLI

// 定数バッファの先頭に置く4つ（C++ の PostFxFrameHeader と一致させること）
#define POSTFX_FRAME_HEADER \
    int2 gTextureSize;      \
    float gTime;            \
    float gDeltaTime;

Texture2D<float4> gSource : register(t0);
RWTexture2D<float4> gOutput : register(u0);
SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);

static const float kPi = 3.14159265f;
static const float kTwoPi = 6.28318531f;

// ---------- 色 ----------

/// 人の目に合わせた明るさ（リニア色のまま）
float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

/// ACES（近似）のトーンマップカーブ。ToneMap.PS.hlsl と同じ式
float3 AcesCurve(float3 x)
{
    return saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
}

/// AcesCurve の逆（2次方程式を解く）。y は 0〜0.99 に収めてから渡す
float3 AcesCurveInverse(float3 y)
{
    float3 a = 2.43f * y - 2.51f;
    float3 b = 0.59f * y - 0.03f;
    float3 c = 0.14f * y;
    return (-b - sqrt(max(b * b - 4.0f * a * c, 0.0f))) / (2.0f * a);
}

/// HDR のリニア色を 0〜1 の「見た目の空間」へ移す（ACES＋ガンマ）
float3 EncodeDisplay(float3 color)
{
    return pow(AcesCurve(max(color, 0.0f)), 1.0f / 2.2f);
}

/// EncodeDisplay の逆。見た目の空間で作った色を HDR のリニア色へ戻す
float3 DecodeDisplay(float3 display)
{
    float3 y = min(pow(saturate(display), 2.2f), 0.99f);
    return AcesCurveInverse(y);
}

/// 見た目の明るさ（0〜1）
float DisplayLuminance(float3 color)
{
    return pow(AcesCurve(max(Luminance(color), 0.0f).xxx).x, 1.0f / 2.2f);
}

float3 RgbToHsv(float3 c)
{
    float4 K = float4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
    float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10f;
    return float3(abs(q.z + (q.w - q.y) / (6.0f * d + e)), d / (q.x + e), q.x);
}

float3 HsvToRgb(float3 c)
{
    float3 p = abs(frac(c.xxx + float3(1.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f);
    return c.z * lerp(float3(1.0f, 1.0f, 1.0f), saturate(p - 1.0f), c.y);
}

// ---------- 乱数・ノイズ ----------

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float2 Hash22(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float Hash11(float p)
{
    p = frac(p * 0.1031f);
    p *= p + 33.33f;
    p *= p + p;
    return frac(p);
}

/// なめらかな値ノイズ（0〜1）
float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = Hash12(i);
    float b = Hash12(i + float2(1.0f, 0.0f));
    float c = Hash12(i + float2(0.0f, 1.0f));
    float d = Hash12(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

/// 何段か重ねたノイズ（0〜1）
float Fbm(float2 p, int octaves)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    float total = 0.0f;
    for (int i = 0; i < octaves; ++i)
    {
        sum += ValueNoise(p) * amplitude;
        total += amplitude;
        p = p * 2.03f + float2(17.1f, 9.7f);
        amplitude *= 0.5f;
    }
    return sum / max(total, 1e-4f);
}

// ---------- 座標 ----------

/// 入力画像の大きさ（定数バッファより前に宣言される関数からも使えるよう、テクスチャから引く）
int2 SourceSize()
{
    uint width, height;
    gSource.GetDimensions(width, height);
    return int2(width, height);
}

/// ピクセル中心の UV
float2 PixelToUv(int2 pixel)
{
    return (float2(pixel) + 0.5f) / float2(SourceSize());
}

/// 画面の横 / 縦
float AspectRatio()
{
    int2 size = SourceSize();
    return float(size.x) / max(float(size.y), 1.0f);
}

/// 縦横比をそろえた座標（縦が 0〜1、横は 0〜aspect）
float2 AspectUv(float2 uv)
{
    return float2(uv.x * AspectRatio(), uv.y);
}

float2 Rotate2D(float2 v, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2(v.x * c - v.y * s, v.x * s + v.y * c);
}

// ---------- 読み出し ----------

float4 SampleSource(float2 uv)
{
    return gSource.SampleLevel(gLinearSampler, uv, 0.0f);
}

float4 LoadSource(int2 pixel)
{
    return gSource.Load(int3(clamp(pixel, int2(0, 0), SourceSize() - int2(1, 1)), 0));
}

/// 画面の外へはみ出した UV を、写し込むか（mirror=1）黒で埋めるか（0）
float4 SampleSourceBorder(float2 uv, float4 borderColor)
{
    if (any(uv < 0.0f) || any(uv > 1.0f))
    {
        return borderColor;
    }
    return SampleSource(uv);
}

/// 左右・上下を折り返して読む（歪みで画面外を読んだときの継ぎ目を目立たせない）
float4 SampleSourceMirror(float2 uv)
{
    float2 m = abs(frac(uv * 0.5f) * 2.0f - 1.0f);
    float2 folded = 1.0f - m;
    return SampleSource(folded);
}

/// 範囲外のスレッドを弾く（numthreads の端数）
#define POSTFX_EARLY_OUT(pixel)                                      \
    if (pixel.x >= gTextureSize.x || pixel.y >= gTextureSize.y)      \
    {                                                                \
        return;                                                      \
    }

#endif // POSTFX_COMMON_HLSLI
