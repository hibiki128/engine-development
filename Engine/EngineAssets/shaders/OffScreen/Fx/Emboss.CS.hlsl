// レリーフ（エンボス）。光を斜めから当てたように、明るさの段差を浮き彫りにする
#include "PostFxCommon.hlsli"

cbuffer EmbossParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float gStrength;  // 浮き出しの強さ
    float gAngle;     // 光の向き（度）
    float gColorKeep; // 元の色を残す量（0で灰色の石膏）
    float gDistance;  // 段差を見る距離（ピクセル）
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float2 uv = PixelToUv(pixel);
    float2 dir = float2(cos(radians(gAngle)), sin(radians(gAngle))) * gDistance / float2(gTextureSize);
    float4 source = LoadSource(pixel);
    float a = DisplayLuminance(SampleSource(uv - dir).rgb);
    float b = DisplayLuminance(SampleSource(uv + dir).rgb);
    float relief = saturate(0.5f + (a - b) * gStrength);

    float3 color = EncodeDisplay(source.rgb);
    float3 tinted = lerp(relief.xxx, color * relief * 2.0f, gColorKeep);
    gOutput[pixel] = float4(DecodeDisplay(saturate(tinted)), source.a);
}
