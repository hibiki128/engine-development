// めまい・酔い。画面がゆっくり揺れ、二重に見え、色がにじむ（毒・混乱・泥酔の表現）
#include "PostFxCommon.hlsli"

cbuffer DizzyParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gStrength;     // 全体の強さ
    float gSpeed;        // 揺れの速さ
    float gDoubleVision; // 二重に見えるずれ（ピクセル）
    float gTintAmount;   // 色の濁り
    float3 gTint;        // 濁りの色（毒なら緑など）
    float gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float t = gTime * gSpeed;

    // 画面全体がゆっくり傾いて回る
    float2 p = (uv - 0.5f) * float2(AspectRatio(), 1.0f);
    float angle = sin(t * 0.7f) * 0.04f * gStrength;
    float zoom = 1.0f + sin(t * 0.5f) * 0.03f * gStrength;
    p = Rotate2D(p, angle) / zoom;
    float2 baseUv = p / float2(AspectRatio(), 1.0f) + 0.5f;
    // 波打ち
    baseUv += float2(sin(uv.y * 6.0f + t * 1.3f), cos(uv.x * 5.0f + t)) * 0.004f * gStrength;

    // 二重に見える（左右にずれた像を重ねる）
    float2 ghost = float2(cos(t * 0.9f), sin(t * 1.1f)) * gDoubleVision * gStrength / float2(gTextureSize);
    float4 a = SampleSourceMirror(baseUv + ghost);
    float4 b = SampleSourceMirror(baseUv - ghost);
    float4 color = (a + b) * 0.5f;
    color.r = lerp(color.r, SampleSourceMirror(baseUv + ghost * 2.0f).r, 0.5f);

    color.rgb = lerp(color.rgb, color.rgb * gTint * 1.5f, gTintAmount * gStrength * (0.75f + 0.25f * sin(t * 2.0f)));
    gOutput[pixel] = color;
}
