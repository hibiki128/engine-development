#ifndef HAGINE_SHADOW_SAMPLE_HLSLI
#define HAGINE_SHADOW_SAMPLE_HLSLI

// ============================================================
// シャドウマップの引き方をここ1か所に集約する。
// 前方描画（Object3d.PS）とディファード（DeferredLighting.PS）の
// 両方がこれを使うので、片方だけ影の出方が違うということが起きない。
// ============================================================

// ポアソンディスク。格子状に並べた PCF と違い、点が不規則に散っているので
// ぼかしを広げても縞模様（バンディング）が出にくい。
static const float2 kShadowPoissonDisk[16] = {
    float2(-0.94201624f, -0.39906216f), float2(0.94558609f, -0.76890725f),
    float2(-0.09418410f, -0.92938870f), float2(0.34495938f, 0.29387760f),
    float2(-0.91588581f, 0.45771432f),  float2(-0.81544232f, -0.87912464f),
    float2(-0.38277543f, 0.27676845f),  float2(0.97484398f, 0.75648379f),
    float2(0.44323325f, -0.97511554f),  float2(0.53742981f, -0.47373420f),
    float2(-0.26496911f, -0.41893023f), float2(0.79197514f, 0.19090188f),
    float2(-0.24188840f, 0.99706507f),  float2(-0.81409955f, 0.91437590f),
    float2(0.19984126f, 0.78641367f),   float2(0.14383161f, -0.14100790f),
};

/// <summary>
/// 画面座標から擬似乱数の回転角を作る。
/// 全ピクセルで同じ並びのまま散らすと模様が固定されて見えるので、
/// ピクセルごとにポアソンディスクを回してノイズへ散らす
/// </summary>
float ShadowRandomAngle(float2 screenPosition)
{
    return frac(sin(dot(screenPosition, float2(12.9898f, 78.233f))) * 43758.5453f) * 6.2831853f;
}

/// <summary>
/// シャドウマップを引いて、光が当たっている割合を返す。
/// </summary>
/// <param name="shadowMap">シャドウマップ</param>
/// <param name="shadowSampler">比較サンプラー</param>
/// <param name="shadowUV">シャドウマップ上のUV</param>
/// <param name="shadowDepth">比較する深度（バイアス適用前）</param>
/// <param name="ndotl">面と光の向きの内積。浅い角度ほどバイアスを強める</param>
/// <param name="bias">深度バイアス</param>
/// <param name="normalBias">面の傾きに応じて足すバイアス</param>
/// <param name="softness">ぼかしの広さ（テクセル単位）</param>
/// <param name="sampleCount">サンプル数（4/8/16）</param>
/// <param name="mapSize">シャドウマップの一辺の解像度</param>
/// <param name="screenPosition">画面座標（ノイズの種）</param>
/// <returns>float: 1=完全に当たっている, 0=完全に影</returns>
float SampleShadowSoft(Texture2D<float> shadowMap, SamplerComparisonState shadowSampler,
                       float2 shadowUV, float shadowDepth, float ndotl,
                       float bias, float normalBias, float softness, int sampleCount,
                       float mapSize, float2 screenPosition)
{
    // 光が浅く当たっている面ほど、1テクセルあたりの深度の変化が大きい。
    // そこを一定のバイアスで済ませると縞状の影（シャドウアクネ）が出るので、傾きに応じて足す
    const float slope = saturate(1.0f - ndotl);
    const float totalBias = bias + normalBias * slope;
    const float compareDepth = shadowDepth - totalBias;

    const float texel = 1.0f / max(mapSize, 1.0f);
    const float radius = max(softness, 0.0f) * texel;

    // 回転行列をピクセルごとに作り、サンプル位置を回してノイズへ散らす
    const float angle = ShadowRandomAngle(screenPosition);
    const float sinAngle = sin(angle);
    const float cosAngle = cos(angle);
    const float2x2 rotation = float2x2(cosAngle, -sinAngle, sinAngle, cosAngle);

    const int count = clamp(sampleCount, 1, 16);
    float shadow = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        const float2 offset = mul(kShadowPoissonDisk[i], rotation) * radius;
        shadow += shadowMap.SampleCmpLevelZero(shadowSampler, shadowUV + offset, compareDepth);
    }
    return shadow / (float)count;
}

#endif // HAGINE_SHADOW_SAMPLE_HLSLI
