// VHS（ビデオテープ）。横の揺れ・色のにじみ・トラッキングの乱れ帯・砂嵐・色あせ
#include "PostFxCommon.hlsli"

cbuffer VhsParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gWobble;      // 横の揺れ（ピクセル）
    float gChromaBleed; // 色のにじみ（ピクセル）
    float gNoise;       // 砂嵐
    float gTracking;    // トラッキングの乱れ帯
    float gSaturation;  // 彩度
    float gBlur;        // 甘さ（横ぼかし。ピクセル）
    float gScanline;    // 走査線
    float gPadding;
};

float3 RgbToYiq(float3 c)
{
    return float3(dot(c, float3(0.299f, 0.587f, 0.114f)), dot(c, float3(0.596f, -0.274f, -0.322f)), dot(c, float3(0.211f, -0.523f, 0.312f)));
}

float3 YiqToRgb(float3 c)
{
    return float3(dot(c, float3(1.0f, 0.956f, 0.621f)), dot(c, float3(1.0f, -0.272f, -0.647f)), dot(c, float3(1.0f, -1.106f, 1.703f)));
}

float3 SampleDisplay(float2 uv)
{
    return EncodeDisplay(SampleSource(uv).rgb);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float2 texel = 1.0f / float2(gTextureSize);

    // ゆっくり下へ流れる乱れ帯
    float bandCenter = frac(gTime * 0.07f);
    float band = smoothstep(0.06f, 0.0f, abs(uv.y - bandCenter)) * gTracking;

    // 行ごとの横揺れ（帯の中は大きく揺れる）
    float wobble = (ValueNoise(float2(uv.y * 40.0f, gTime * 3.0f)) - 0.5f) * gWobble;
    wobble += (Hash11(floor(uv.y * gTextureSize.y) + floor(gTime * 60.0f)) - 0.5f) * band * 30.0f;
    float2 baseUv = uv + float2(wobble * texel.x, 0.0f);

    // 明るさ（Y）は少しだけ、色（IQ）は大きく横へにじむ
    float3 yiq = 0.0f;
    const int kTaps = 5;
    for (int i = -kTaps; i <= kTaps; ++i)
    {
        float t = float(i) / kTaps;
        yiq.x += RgbToYiq(SampleDisplay(baseUv + float2(t * gBlur * texel.x, 0))).x;
        yiq.yz += RgbToYiq(SampleDisplay(baseUv + float2(t * gChromaBleed * texel.x + gChromaBleed * 0.5f * texel.x, 0))).yz;
    }
    yiq /= float(kTaps * 2 + 1);
    yiq.yz *= gSaturation;
    float3 color = YiqToRgb(yiq);

    // 砂嵐（帯の中は強く）
    float noise = Hash12(float2(pixel) + frac(gTime * 31.0f) * 911.0f);
    color += (noise - 0.5f) * (gNoise + band * 0.8f);
    // 白く飛ぶ線
    color += step(0.997f - band * 0.05f, Hash11(floor(uv.y * gTextureSize.y * 0.5f) + floor(gTime * 24.0f))) * 0.6f * gNoise * 4.0f;

    color *= 1.0f - gScanline * (0.5f + 0.5f * sin(float(pixel.y) * kPi));
    gOutput[pixel] = float4(DecodeDisplay(saturate(color)), 1.0f);
}
