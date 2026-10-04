// 水彩画風。にじみ（ゆらいだぼかし）・色のたまり（輪郭が濃くなる）・紙の目を重ねる
#include "PostFxCommon.hlsli"

cbuffer WatercolorParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gWobble;      // にじみのゆらぎ（ピクセル）
    float gWobbleScale; // ゆらぎの細かさ
    float gEdgeDarken;  // 輪郭に色がたまる強さ
    float gPaper;       // 紙の目の強さ
    float gBlur;        // ぼかしの大きさ（ピクセル）
    float gLevels;      // 色の段（0で段にしない）
    float gSaturation;  // 彩度の倍率
    float gBrightness;  // 全体を明るくして紙の白を出す
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float2 texel = 1.0f / float2(gTextureSize);

    // 紙に吸われてにじんだように、読む位置をノイズでずらす
    float2 noiseUv = uv * gWobbleScale * float2(AspectRatio(), 1.0f);
    float2 wobble = float2(Fbm(noiseUv, 3), Fbm(noiseUv + 31.7f, 3)) - 0.5f;
    float2 baseUv = uv + wobble * gWobble * texel * 2.0f;

    // 小さな円盤でぼかす
    float3 sum = 0.0f;
    const float2 offsets[8] = {float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
                               float2(0.707f, 0.707f), float2(-0.707f, 0.707f), float2(0.707f, -0.707f), float2(-0.707f, -0.707f)};
    sum += EncodeDisplay(SampleSource(baseUv).rgb) * 2.0f;
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        sum += EncodeDisplay(SampleSource(baseUv + offsets[i] * gBlur * texel).rgb);
    }
    float3 color = sum / 10.0f;

    if (gLevels >= 2.0f)
    {
        color = floor(color * gLevels + 0.5f) / gLevels;
    }

    // 輪郭（明るさの勾配）に絵の具がたまって濃くなる
    float lx = DisplayLuminance(SampleSource(baseUv + float2(texel.x * 2.0f, 0)).rgb) - DisplayLuminance(SampleSource(baseUv - float2(texel.x * 2.0f, 0)).rgb);
    float ly = DisplayLuminance(SampleSource(baseUv + float2(0, texel.y * 2.0f)).rgb) - DisplayLuminance(SampleSource(baseUv - float2(0, texel.y * 2.0f)).rgb);
    float edge = saturate(length(float2(lx, ly)) * 4.0f);
    color *= 1.0f - edge * gEdgeDarken;

    float3 hsv = RgbToHsv(saturate(color));
    hsv.y = saturate(hsv.y * gSaturation);
    color = HsvToRgb(hsv);
    color = lerp(color, 1.0f, gBrightness);

    // 紙の目（細かいノイズ＋粗いむら）
    float paper = Fbm(float2(pixel) * 0.35f, 2) * 0.6f + ValueNoise(uv * 6.0f * float2(AspectRatio(), 1.0f)) * 0.4f;
    color *= 1.0f - (paper - 0.5f) * gPaper;

    gOutput[pixel] = float4(DecodeDisplay(saturate(color)), LoadSource(pixel).a);
}
