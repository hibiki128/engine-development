// ディザ・レトロゲーム機風。画素を大きくし、少ない色（パレット）へディザで寄せる
#include "PostFxCommon.hlsli"

cbuffer DitherParams : register(b0)
{
    POSTFX_FRAME_HEADER
    int gPalette;      // 0=携帯ゲーム機(緑4色) / 1=白黒2色 / 2=RGBの段を減らす / 3=CGA(4色) / 4=PICO-8風(16色)
    float gPixelSize;  // 1画素の大きさ（ピクセル）
    float gDither;     // ディザの強さ
    float gLevels;     // RGBの段（パレット2のとき）
    int gMatrix;       // 0=4x4 / 1=8x8
    float gContrast;   // コントラスト
    float gStrength;   // 元の色との混ぜ具合
    float gPadding;
};

static const float kBayer8[64] = {
    0, 32, 8, 40, 2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44, 4, 36, 14, 46, 6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
    3, 35, 11, 43, 1, 33, 9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47, 7, 39, 13, 45, 5, 37,
    63, 31, 55, 23, 61, 29, 53, 21};

static const float3 kGameBoy[4] = {
    float3(15, 56, 15) / 255.0f, float3(48, 98, 48) / 255.0f, float3(139, 172, 15) / 255.0f, float3(155, 188, 15) / 255.0f};
static const float3 kCga[4] = {float3(0, 0, 0), float3(0.33f, 1, 1), float3(1, 0.33f, 1), float3(1, 1, 1)};
static const float3 kPico8[16] = {
    float3(0, 0, 0) / 255.0f, float3(29, 43, 83) / 255.0f, float3(126, 37, 83) / 255.0f, float3(0, 135, 81) / 255.0f,
    float3(171, 82, 54) / 255.0f, float3(95, 87, 79) / 255.0f, float3(194, 195, 199) / 255.0f, float3(255, 241, 232) / 255.0f,
    float3(255, 0, 77) / 255.0f, float3(255, 163, 0) / 255.0f, float3(255, 236, 39) / 255.0f, float3(0, 228, 54) / 255.0f,
    float3(41, 173, 255) / 255.0f, float3(131, 118, 156) / 255.0f, float3(255, 119, 168) / 255.0f, float3(255, 204, 170) / 255.0f};

float BayerThreshold(int2 p)
{
    if (gMatrix == 1)
    {
        return kBayer8[(p.y & 7) * 8 + (p.x & 7)] / 64.0f - 0.5f;
    }
    int2 q = p & 3;
    // 8x8 の左上 4x4 を間引いた物は 4x4 の Bayer と同じ並びになる
    return kBayer8[(q.y * 2) * 8 + q.x * 2] / 64.0f - 0.5f;
}

float3 Nearest(float3 c, int palette)
{
    float3 best = c;
    float bestDistance = 1e9f;
    if (palette == 0)
    {
        // 明るさだけで4段を選ぶ（本物と同じく色相は持たない）
        float l = Luminance(c);
        return kGameBoy[clamp(int(l * 4.0f), 0, 3)];
    }
    if (palette == 3)
    {
        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            float d = dot(c - kCga[i], c - kCga[i]);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = kCga[i];
            }
        }
        return best;
    }
    [unroll]
    for (int j = 0; j < 16; ++j)
    {
        float3 diff = (c - kPico8[j]) * float3(0.30f, 0.59f, 0.11f) * 3.0f;
        float d = dot(diff, diff) + dot(c - kPico8[j], c - kPico8[j]) * 0.3f;
        if (d < bestDistance)
        {
            bestDistance = d;
            best = kPico8[j];
        }
    }
    return best;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float size = max(gPixelSize, 1.0f);
    int2 block = int2(floor(float2(pixel) / size));
    float2 uv = (float2(block) + 0.5f) * size / float2(gTextureSize);
    float4 source = LoadSource(pixel);
    float3 c = EncodeDisplay(SampleSource(uv).rgb);
    c = saturate((c - 0.5f) * gContrast + 0.5f);

    float threshold = BayerThreshold(block) * gDither;
    float3 result;
    if (gPalette == 1)
    {
        result = (Luminance(c) + threshold > 0.5f) ? float3(1, 1, 1) : float3(0, 0, 0);
    }
    else if (gPalette == 2)
    {
        float levels = max(gLevels, 2.0f) - 1.0f;
        result = floor(saturate(c + threshold / levels) * levels + 0.5f) / levels;
    }
    else
    {
        float spread = (gPalette == 0) ? 0.25f : (gPalette == 3) ? 0.5f : 0.2f;
        result = Nearest(saturate(c + threshold * spread), gPalette);
    }
    gOutput[pixel] = float4(lerp(source.rgb, DecodeDisplay(result), gStrength), source.a);
}
