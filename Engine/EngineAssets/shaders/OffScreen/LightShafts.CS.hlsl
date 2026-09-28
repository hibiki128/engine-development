// =============================================================
// 光の筋（レイマーチ式のボリュメトリックライト）
//
// カメラからそのピクセルが映している点まで視線を一定間隔で進み、
// 各点が「平行光源から見えているか」をシャドウマップで調べる。
// 見えている点だけ空気中の散乱として光を足していくので、
// 物陰では筋が途切れ、木漏れ日のような本物の光芒になる。
//
// 放射ブラー式（太陽から外へ伸ばすだけ）と違い、
// **太陽が画面の外にあっても筋が出る**し、遮蔽物の形がそのまま筋の形になる。
//
// バンディング対策:
//   サンプル間隔が粗いと縞が出る。開始位置をピクセルごとにずらして（ジッタ）
//   縞を細かいノイズへ散らし、最後に近傍とならして目立たなくする。
// =============================================================

cbuffer LightShaftParams : register(b0)
{
    float4x4 gInverseViewProjection; // NDC → ワールド座標
    float4x4 gLightViewProjection;   // ワールド → ライトのクリップ空間
    float3 gCameraPosition;          // カメラのワールド座標
    float gIntensity;                // 光の筋の強さ
    float3 gLightColor;              // 光の筋の色
    float gDensity;                  // 空気中の散乱の濃さ
    float3 gLightDirection;          // 平行光源が進む向き
    float gMaxDistance;              // レイマーチする最大距離
    float gAnisotropy;               // 散乱の偏り（0で全方位、1に近いほど太陽方向へ集中）
    float gShadowBias;               // シャドウ比較のバイアス
    float gJitterStrength;           // 開始位置をずらす量（0〜1）
    float gPadding;
    int gSampleCount;                // レイマーチの分割数
    int gShadowEnabled;              // シャドウマップが有効か（0なら遮蔽なし）
    int2 gTextureSize;               // 処理対象の解像度
};

Texture2D<float4> gSource : register(t0);
Texture2D<float> gDepth : register(t1);
Texture2D<float> gShadowMap : register(t2);
RWTexture2D<float4> gOutput : register(u0);

// s0 には画像用の線形クランプが入るが、ここでは深度しかサンプルしないので使わない。
// ポストエフェクトのCSは s0=線形クランプ / s1=ポイントクランプ の並びで統一されている
SamplerState gSamplerPoint : register(s1);

/// 深度バッファの値からワールド座標を復元する
float3 ReconstructWorldPosition(int2 pixel, float rawDepth)
{
    float2 uv = (float2(pixel) + 0.5f) / float2(gTextureSize);
    float4 ndc = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, rawDepth, 1.0f);
    float4 world = mul(ndc, gInverseViewProjection);
    return world.xyz / (abs(world.w) < 1e-6f ? 1e-6f : world.w);
}

/// ピクセルごとに散らばる 0〜1 の値。レイの開始位置をずらしてバンディングを潰す。
///
/// インターリーブド・グラディエント・ノイズ。乱数よりも近い画素どうしの値が
/// きれいにばらけるので、少ないサンプル数でも縞が目立ちにくい。
/// 時間で動かしていないのは、TAAが無いとちらつきとして見えてしまうため
float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

/// その点に平行光源の光が届いているかを調べる
/// 前方描画・ディファードと同じ座標の作り方に揃えてある
float SampleLit(float3 worldPosition)
{
    if (gShadowEnabled == 0)
    {
        // シャドウマップが無効なときは中身が古いので、遮蔽なしとして扱う
        return 1.0f;
    }

    float4 shadowCoord = mul(float4(worldPosition, 1.0f), gLightViewProjection);
    float3 projCoord = shadowCoord.xyz / (abs(shadowCoord.w) < 1e-6f ? 1e-6f : shadowCoord.w);
    float2 shadowUV = projCoord.xy * float2(0.5f, -0.5f) + 0.5f;

    // シャドウマップの外は「光が届いている」とみなす。
    // ここを影にすると、マップの範囲の外側で筋が唐突に切れる
    if (shadowUV.x < 0.0f || shadowUV.x > 1.0f ||
        shadowUV.y < 0.0f || shadowUV.y > 1.0f ||
        projCoord.z < 0.0f || projCoord.z > 1.0f)
    {
        return 1.0f;
    }

    float shadowDepth = gShadowMap.SampleLevel(gSamplerPoint, shadowUV, 0);
    return (projCoord.z - gShadowBias <= shadowDepth) ? 1.0f : 0.0f;
}

/// Henyey-Greenstein の位相関数。
/// 視線と光の進む向きの関係で「どれだけこちらへ散乱してくるか」が決まる
float PhaseFunction(float cosTheta)
{
    float g = clamp(gAnisotropy, -0.95f, 0.95f);
    float gg = g * g;
    float denominator = 1.0f + gg - 2.0f * g * cosTheta;
    return (1.0f - gg) / (4.0f * 3.14159265f * max(pow(max(denominator, 1e-4f), 1.5f), 1e-4f));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchThreadID.xy);
    if (pixel.x >= gTextureSize.x || pixel.y >= gTextureSize.y)
    {
        return;
    }

    float4 sourceColor = gSource.Load(int3(pixel, 0));

    int sampleCount = clamp(gSampleCount, 1, 128);
    if (gIntensity <= 0.0f)
    {
        gOutput[pixel] = sourceColor;
        return;
    }

    float rawDepth = gDepth.Load(int3(pixel, 0));

    // 空（深度が書かれていない場所）は、最大距離まで空気があるものとして進む
    bool isSky = (rawDepth >= 1.0f);
    float3 target = ReconstructWorldPosition(pixel, isSky ? 0.9999f : rawDepth);
    float3 toTarget = target - gCameraPosition;

    float travelDistance = isSky ? gMaxDistance : min(length(toTarget), gMaxDistance);
    if (travelDistance <= 0.0f)
    {
        gOutput[pixel] = sourceColor;
        return;
    }

    float3 rayDirection = normalize(toTarget);
    float stepSize = travelDistance / sampleCount;

    // 視線と「太陽から来る光」の角度。太陽を覗き込むほど散乱が強くなる
    float cosTheta = dot(rayDirection, -normalize(gLightDirection));
    float phase = PhaseFunction(cosTheta);

    // 開始位置をピクセルごとにずらす。そろえたままだと等間隔の縞になって見える
    float jitter = InterleavedGradientNoise(float2(pixel)) * saturate(gJitterStrength);

    float accumulated = 0.0f;
    for (int i = 0; i < sampleCount; ++i)
    {
        float t = (i + jitter) * stepSize;
        float3 samplePosition = gCameraPosition + rayDirection * t;
        accumulated += SampleLit(samplePosition);
    }

    // 分割数を変えても明るさが変わらないよう、進んだ距離で正規化する
    float scattering = (accumulated / sampleCount) * travelDistance * gDensity * phase;

    float3 shaft = gLightColor * scattering * gIntensity;

    // 散乱した光は元の絵に足す（遮るのではなく空気が光る）
    gOutput[pixel] = float4(sourceColor.rgb + shaft, sourceColor.a);
}
