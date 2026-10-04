// 光芒（画面空間）。画面に写っている太陽（明るい所）から放射状に光の筋を伸ばす。
// シャドウマップを使う「光の筋(レイマーチ)」と違い、空の明るさだけで作るので軽く、どんなシーンでも出る
#include "PostFxCommon.hlsli"

cbuffer ScreenGodRaysParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float2 gLightUv;    // 光源の画面上の位置（UV）
    float gDensity;     // 筋の長さ（光源までのどれだけを辿るか）
    float gWeight;      // 1回あたりの明るさ
    float gDecay;       // 辿るごとに弱める割合
    float gExposure;    // 全体の明るさ
    float gThreshold;   // これより明るい所だけが光る
    int gSamples;       // 辿る回数
    float3 gTint;       // 光の色
    float gVisibility;  // 光源の見え具合（カメラの後ろにあると 0）
    int gSkyOnly;       // 1なら空（何も描かれていない所）だけを光源にする
    float gSunDisk;     // 光源の位置に足す太陽の円の明るさ（空が明るくないシーンでも筋を出すため）
    float gSunSize;     // 太陽の円の大きさ（画面の高さ=1）
    float gPadding;
};

Texture2D<float> gDepth : register(t1);

float3 LightAt(float2 uv)
{
    float3 c = SampleSource(uv).rgb;
    float3 bright = max(c - gThreshold, 0.0f);
    float2 toSun = (uv - gLightUv) * float2(AspectRatio(), 1.0f);
    bright += gSunDisk * smoothstep(max(gSunSize, 1e-4f), 0.0f, length(toSun));
    if (gSkyOnly != 0)
    {
        float depth = gDepth.SampleLevel(gPointSampler, uv, 0.0f);
        bright *= step(0.9999f, depth);
    }
    return bright;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float4 source = LoadSource(pixel);
    if (gVisibility <= 0.0f)
    {
        gOutput[pixel] = source;
        return;
    }

    float2 uv = PixelToUv(pixel);
    int samples = clamp(gSamples, 8, 128);
    float2 delta = (uv - gLightUv) * gDensity / samples;
    float2 p = uv;
    float illumination = 1.0f;
    float3 sum = 0.0f;
    // ピクセルごとに出だしをずらして、段々の縞を目立たなくする
    p -= delta * Hash12(float2(pixel));
    for (int i = 0; i < samples; ++i)
    {
        p -= delta;
        sum += LightAt(p) * illumination * gWeight;
        illumination *= gDecay;
    }
    gOutput[pixel] = float4(source.rgb + sum * gExposure * gTint * gVisibility, source.a);
}
