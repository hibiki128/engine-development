#include "FullScreen.hlsli"

// FXAA（高速近似アンチエイリアス）。
// 描いた後の絵だけを見て輪郭のギザギザを見つけ、その向きに沿ってぼかす。
// MSAA と違って描画コストが増えないかわりに、細い線が少し甘くなる。

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct FxaaParameters
{
    float edgeThreshold;    // これ以上の明るさの差を輪郭とみなす
    float edgeThresholdMin; // 暗い場所での下限（ノイズを輪郭と誤認しないため）
    float subpixelBlend;    // 細かい部分をどれだけ馴染ませるか (0〜1)
    float pad;
};
ConstantBuffer<FxaaParameters> gFxaa : register(b0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

/// 明るさ。輪郭の判定はすべてこの値で行う
float Luma(float3 color)
{
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    uint width = 0;
    uint height = 0;
    gTexture.GetDimensions(width, height);
    const float2 texelSize = 1.0f / float2(max(width, 1u), max(height, 1u));

    const float2 uv = input.texcoord;
    const float3 centerColor = gTexture.Sample(gSampler, uv).rgb;

    // 上下左右の明るさを見て、輪郭があるかどうかを調べる
    const float lumaCenter = Luma(centerColor);
    const float lumaUp = Luma(gTexture.Sample(gSampler, uv + float2(0.0f, -texelSize.y)).rgb);
    const float lumaDown = Luma(gTexture.Sample(gSampler, uv + float2(0.0f, texelSize.y)).rgb);
    const float lumaLeft = Luma(gTexture.Sample(gSampler, uv + float2(-texelSize.x, 0.0f)).rgb);
    const float lumaRight = Luma(gTexture.Sample(gSampler, uv + float2(texelSize.x, 0.0f)).rgb);

    const float lumaMin = min(lumaCenter, min(min(lumaUp, lumaDown), min(lumaLeft, lumaRight)));
    const float lumaMax = max(lumaCenter, max(max(lumaUp, lumaDown), max(lumaLeft, lumaRight)));
    const float lumaRange = lumaMax - lumaMin;

    // 差が小さいところは平坦なのでそのまま返す（ぼかすと無駄に甘くなる）
    if (lumaRange < max(gFxaa.edgeThresholdMin, lumaMax * gFxaa.edgeThreshold))
    {
        output.color = float4(centerColor, 1.0f);
        return output;
    }

    // 斜めの成分も拾うため四隅も見る
    const float lumaUpLeft = Luma(gTexture.Sample(gSampler, uv + float2(-texelSize.x, -texelSize.y)).rgb);
    const float lumaUpRight = Luma(gTexture.Sample(gSampler, uv + float2(texelSize.x, -texelSize.y)).rgb);
    const float lumaDownLeft = Luma(gTexture.Sample(gSampler, uv + float2(-texelSize.x, texelSize.y)).rgb);
    const float lumaDownRight = Luma(gTexture.Sample(gSampler, uv + float2(texelSize.x, texelSize.y)).rgb);

    // 輪郭が横向きか縦向きかを、明るさの変化の大きさで決める
    const float edgeHorizontal =
        abs(lumaUpLeft + lumaUpRight - 2.0f * lumaUp) * 2.0f +
        abs(lumaLeft + lumaRight - 2.0f * lumaCenter) * 4.0f +
        abs(lumaDownLeft + lumaDownRight - 2.0f * lumaDown) * 2.0f;
    const float edgeVertical =
        abs(lumaUpLeft + lumaDownLeft - 2.0f * lumaLeft) * 2.0f +
        abs(lumaUp + lumaDown - 2.0f * lumaCenter) * 4.0f +
        abs(lumaUpRight + lumaDownRight - 2.0f * lumaRight) * 2.0f;
    const bool isHorizontal = edgeHorizontal >= edgeVertical;

    // 輪郭をまたぐ向き（横の輪郭なら上下）に、明るさの差が大きい側へずらして読む
    const float luma1 = isHorizontal ? lumaUp : lumaLeft;
    const float luma2 = isHorizontal ? lumaDown : lumaRight;
    const float gradient1 = abs(luma1 - lumaCenter);
    const float gradient2 = abs(luma2 - lumaCenter);
    const float stepLength = isHorizontal ? texelSize.y : texelSize.x;
    const float offsetSign = (gradient1 >= gradient2) ? -1.0f : 1.0f;

    // 周囲の平均とのずれから、どれだけ馴染ませるかを決める
    const float lumaAverage =
        (2.0f * (lumaUp + lumaDown + lumaLeft + lumaRight) +
         (lumaUpLeft + lumaUpRight + lumaDownLeft + lumaDownRight)) / 12.0f;
    const float subpixel = saturate(abs(lumaAverage - lumaCenter) / max(lumaRange, 0.0001f));
    const float blend = subpixel * subpixel * saturate(gFxaa.subpixelBlend) * 0.5f;

    float2 sampleUv = uv;
    if (isHorizontal)
        sampleUv.y += offsetSign * stepLength * blend;
    else
        sampleUv.x += offsetSign * stepLength * blend;

    output.color = float4(gTexture.Sample(gSampler, sampleUv).rgb, 1.0f);
    return output;
}
