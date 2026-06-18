#include "FullScreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer RetroBuffer : register(b0)
{
    float gPixelSize;         // ピクセル化のブロックサイズ（1〜32px）
    float gColorLevels;       // 減色レベル（2〜16、小さいほど色数が減る）
    float gScanlineIntensity; // スキャンライン強度（0〜1）
    float gScanlineCount;     // スキャンラインの本数（100〜600）
    float gVignetteStrength;  // CRT端の暗さ（0〜2）
    float gChromaticOffset;   // 色収差（0〜0.01）
    float gTime;              // フリッカー用（秒）
    float gResolutionX;       // 画面横解像度（px）
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float2 uv = input.texcoord;
    float2 texSize = float2(gResolutionX, gResolutionX * 9.0 / 16.0);

    // 1. ピクセル化（解像度を落として古いゲーム風に）
    float2 pixelUV = floor(uv * texSize / gPixelSize) * gPixelSize / texSize;

    // 2. 色収差（RGBを微妙にずらしてブラウン管の滲みを再現）
    float2 fromCenter = pixelUV - 0.5;
    float dist = length(fromCenter);
    float2 chromaDir = (dist > 0.0001) ? normalize(fromCenter) : float2(0, 0);
    float2 chromaOffset = chromaDir * gChromaticOffset * dist;

    float r = gTexture.Sample(gSampler, pixelUV + chromaOffset).r;
    float g = gTexture.Sample(gSampler, pixelUV).g;
    float b = gTexture.Sample(gSampler, pixelUV - chromaOffset).b;
    float3 color = float3(r, g, b);

    // 3. 減色（カラーパレット削減。ファミコン〜PS1風の段階的な色）
    color = floor(color * gColorLevels + 0.5) / gColorLevels;

    // 4. スキャンライン（CRTモニタの横縞）
    float scanline = sin(uv.y * gScanlineCount * 3.14159265);
    scanline = scanline * 0.5 + 0.5;
    color *= lerp(1.0, scanline, gScanlineIntensity);

    // 5. CRTビネット（画面端の暗さ）
    float vignette = 1.0 - dist * gVignetteStrength;
    vignette = saturate(vignette);
    color *= vignette;

    // 6. 軽いフリッカー（画面がチラつく感じ）
    float flicker = 1.0 + sin(gTime * 60.0) * 0.01;
    color *= flicker;

    output.color = float4(color, 1.0);
    return output;
}
