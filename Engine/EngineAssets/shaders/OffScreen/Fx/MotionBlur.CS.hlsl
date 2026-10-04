// カメラのモーションブラー。深度からワールド座標を戻し、前のフレームのカメラで写したときの位置との差の向きにぼかす。
// カメラを速く振ったとき・高速移動のときの流れる感じを出す
#include "PostFxCommon.hlsli"

cbuffer MotionBlurParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float4x4 gInverseViewProjection;  // 今のフレームの NDC → ワールド
    float4x4 gPreviousViewProjection; // 前のフレームのワールド → クリップ
    float gStrength;                  // 強さ（シャッターの開いている長さ）
    int gSamples;                     // ぼかしの分割数
    float gMaxBlur;                   // ぼけの長さの上限（画面の幅=1）
    int gHasPrevious;                 // 前のフレームの行列があるか
};

Texture2D<float> gDepth : register(t1);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float4 source = LoadSource(pixel);
    if (gHasPrevious == 0)
    {
        gOutput[pixel] = source;
        return;
    }

    float2 uv = PixelToUv(pixel);
    float depth = gDepth.Load(int3(pixel, 0));
    float4 ndc = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 world = mul(ndc, gInverseViewProjection);
    world /= (abs(world.w) < 1e-6f) ? 1e-6f : world.w;

    float4 previousClip = mul(float4(world.xyz, 1.0f), gPreviousViewProjection);
    if (previousClip.w <= 1e-4f)
    {
        gOutput[pixel] = source;
        return;
    }
    float2 previousNdc = previousClip.xy / previousClip.w;
    float2 previousUv = float2(previousNdc.x * 0.5f + 0.5f, 0.5f - previousNdc.y * 0.5f);

    float2 velocity = (uv - previousUv) * gStrength;
    float speed = length(velocity);
    if (speed > gMaxBlur)
    {
        velocity *= gMaxBlur / speed;
    }

    int samples = clamp(gSamples, 2, 32);
    float4 sum = 0.0f;
    for (int i = 0; i < samples; ++i)
    {
        // 今の位置を真ん中にして前後へ広げる
        float t = float(i) / float(samples - 1) - 0.5f;
        sum += SampleSource(uv - velocity * t);
    }
    gOutput[pixel] = sum / samples;
}
