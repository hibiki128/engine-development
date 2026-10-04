// 文字アート（ASCIIアート）。画面をマスに分け、明るさに合った文字（. : * o & 8 @ #）で描き直す
#include "PostFxCommon.hlsli"

cbuffer AsciiParams : register(b0)
{
    POSTFX_FRAME_HEADER
    float3 gTextColor;  // 文字の色（単色のとき）
    float gCellSize;    // 1文字の大きさ（ピクセル）
    int gColorMode;     // 0=元の色 / 1=単色 / 2=ターミナル（黒地に緑）
    float gBackground;  // 背景に元の絵を残す量
    float gContrast;    // 明るさの強調
    float gPadding;
};

/// 5x5 の点で描いた文字。n のビットが立っている所が点
float Character(int n, float2 p)
{
    // p は -1〜1。真ん中の 5x5 だけに点を置き、まわりは文字の間の余白にする
    p = floor(p * float2(-4.0f, -4.0f) + 2.5f);
    if (p.x >= 0.0f && p.x <= 4.0f && p.y >= 0.0f && p.y <= 4.0f)
    {
        int bit = int(p.x + 5.0f * p.y);
        if (((n >> bit) & 1) == 1)
        {
            return 1.0f;
        }
    }
    return 0.0f;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    int2 pixel = int2(id.xy);
    POSTFX_EARLY_OUT(pixel)

    float cell = max(gCellSize, 4.0f);
    float2 cellIndex = floor(float2(pixel) / cell);
    float2 cellCenterUv = (cellIndex + 0.5f) * cell / float2(gTextureSize);
    float3 cellColor = SampleSource(cellCenterUv).rgb;
    float gray = saturate((DisplayLuminance(cellColor) - 0.5f) * gContrast + 0.5f);

    int n = 0;
    if (gray > 0.1f) n = 4096;      // .
    if (gray > 0.2f) n = 65600;     // :
    if (gray > 0.3f) n = 332772;    // *
    if (gray > 0.4f) n = 15255086;  // o
    if (gray > 0.5f) n = 23385164;  // &
    if (gray > 0.6f) n = 15252014;  // 8
    if (gray > 0.7f) n = 13199452;  // @
    if (gray > 0.8f) n = 11512810;  // #

    float2 local = (frac(float2(pixel) / cell) - 0.5f) * 2.0f;
    float glyph = Character(n, local);

    float3 text;
    if (gColorMode == 0)
    {
        float3 display = EncodeDisplay(cellColor);
        text = display / max(max(display.r, max(display.g, display.b)), 0.25f);
    }
    else if (gColorMode == 1)
    {
        text = gTextColor;
    }
    else
    {
        text = float3(0.2f, 1.0f, 0.35f);
    }
    float3 background = EncodeDisplay(LoadSource(pixel).rgb) * gBackground;
    float3 color = lerp(background, text, glyph);
    gOutput[pixel] = float4(DecodeDisplay(color), 1.0f);
}
