// ミニチュア風（ティルトシフト）。ピントの帯の外を大きくぼかし、色を鮮やかにして模型のように見せる
#include "PostFxCommon.hlsli"

cbuffer TiltShiftParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gFocusCenter; // ピントの帯の位置（0=上 / 1=下）
    float gFocusWidth;  // ピントの合う帯の幅
    float gBlurRadius;  // いちばん外のぼかしの大きさ（ピクセル）
    float gSaturation;  // 彩度の倍率
    float gContrast;    // コントラスト
    float gAngle;       // 帯の傾き（度）
    float gFalloff;     // 帯からぼけ始めるまでの広さ
    float gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    // 帯からの距離（傾けられる）
    float2 dir = float2(-sin(radians(gAngle)), cos(radians(gAngle)));
    float distanceToBand = abs(dot(uv - float2(0.5f, gFocusCenter), dir));
    float blurAmount = smoothstep(gFocusWidth * 0.5f, gFocusWidth * 0.5f + max(gFalloff, 0.01f), distanceToBand);
    float radius = blurAmount * gBlurRadius;

    float4 sum = LoadSource(pixel);
    float total = 1.0f;
    if (radius > 0.5f)
    {
        // 黄金角のらせんで円盤の中をまんべんなく読む
        const int kTaps = 24;
        float2 texel = 1.0f / float2(gTextureSize);
        for (int i = 1; i <= kTaps; ++i)
        {
            float r = sqrt(float(i) / kTaps) * radius;
            float a = float(i) * 2.39996323f;
            sum += SampleSource(uv + float2(cos(a), sin(a)) * r * texel);
            total += 1.0f;
        }
    }
    float4 color = sum / total;

    float3 display = EncodeDisplay(color.rgb);
    float3 hsv = RgbToHsv(display);
    hsv.y = saturate(hsv.y * gSaturation);
    display = HsvToRgb(hsv);
    display = saturate((display - 0.5f) * gContrast + 0.5f);
    gOutput[pixel] = float4(DecodeDisplay(display), color.a);
}
