// 万華鏡。中心のまわりを扇に分け、1枚の扇を折り返して並べる
#include "PostFxCommon.hlsli"

cbuffer KaleidoscopeParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float2 gCenter;   // 中心（UV）
    float gSegments;  // 扇の数
    float gRotation;  // 回転（度）
    float gSpeed;     // 回し続ける速さ（度/秒）
    float gZoom;      // 拡大率
    float gStrength;  // 元の絵との混ぜ具合
    float gPadding;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float aspect = AspectRatio();
    float2 p = (uv - gCenter) * float2(aspect, 1.0f);
    float r = length(p) / max(gZoom, 0.01f);
    float a = atan2(p.y, p.x) + radians(gRotation + gSpeed * gTime);

    float segment = kTwoPi / max(floor(gSegments), 1.0f);
    a = fmod(a + kTwoPi * 8.0f, segment);
    // 隣の扇とは鏡写しにして継ぎ目を消す
    a = abs(a - segment * 0.5f);

    float2 q = float2(cos(a), sin(a)) * r;
    float2 sampleUv = q / float2(aspect, 1.0f) + gCenter;
    float4 kaleido = SampleSourceMirror(sampleUv);
    float4 source = LoadSource(pixel);
    gOutput[pixel] = lerp(source, kaleido, gStrength);
}
