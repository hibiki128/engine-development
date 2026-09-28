// =============================================================
// 距離フォグ ＋ 高さフォグ（太陽への散乱つき）
//
// 深度からワールド座標を復元し、カメラからその点までの視線に沿って
// 「高さ方向に指数で薄くなる霧」の濃さを積分する。
//
//   密度(y) = density * exp(-falloff * (y - baseHeight))
//
// を視線 P(t) = camera + dir * t について t で積分したものが光学的厚み。
// 解析的に積分できるのでレイマーチは要らない。
//
//   ∫ density * exp(-falloff * (y0 + t * dir.y)) dt
//     = density * exp(-falloff * y0) * (1 - exp(-falloff * dir.y * d)) / (falloff * dir.y)
//
// falloff を 0 にすると高さに依存しない素直な距離フォグになる。
// =============================================================

cbuffer FogParams : register(b0)
{
    float4x4 gInverseViewProjection; // NDC → ワールド座標
    float3 gCameraPosition;          // カメラのワールド座標
    float gDensity;                  // 基準高さでの霧の濃さ
    float3 gFogColor;                // 霧の色（リニア）
    float gHeightFalloff;            // 高さ方向の減衰。0で高さ無視の距離フォグ
    float3 gSunDirection;            // 平行光源が進む向き
    float gSunIntensity;             // 太陽側の色へ寄せる強さ。0で散乱なし
    float3 gSunColor;                // 太陽を覗き込んだときの霧の色
    float gSunExponent;              // 散乱の鋭さ。大きいほど太陽の周りだけ光る
    float gStartDistance;            // ここまではフォグを掛けない距離
    float gMaxDistance;              // フォグを積分する最大距離
    float gBaseHeight;               // 霧が最も濃くなる高さ
    float gMaxOpacity;               // フォグの上限。1未満にすると景色が完全には消えない
    int2 gTextureSize;               // 処理対象の解像度
    float gSkyStrength;              // 空（深度が書かれていない場所）へ掛ける割合
    float gPadding;
};

Texture2D<float4> gSource : register(t0);
Texture2D<float> gDepth : register(t1);
RWTexture2D<float4> gOutput : register(u0);

/// 深度バッファの値からワールド座標を復元する
float3 ReconstructWorldPosition(int2 pixel, float rawDepth)
{
    float2 uv = (float2(pixel) + 0.5f) / float2(gTextureSize);

    // DirectXのNDCは左上が(-1, +1)。UVのYとは向きが逆になる
    float4 ndc = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, rawDepth, 1.0f);

    float4 world = mul(ndc, gInverseViewProjection);
    return world.xyz / (abs(world.w) < 1e-6f ? 1e-6f : world.w);
}

/// 視線に沿った霧の濃さ（0〜1）を求める
float ComputeFogFactor(float3 rayDirection, float travelDistance)
{
    // 手前側はフォグを掛けない。カメラに張り付いた物まで白くならないようにするため
    float distance = min(max(travelDistance - gStartDistance, 0.0f), gMaxDistance);
    if (distance <= 0.0f)
    {
        return 0.0f;
    }

    float opticalDepth;
    if (gHeightFalloff < 1e-4f)
    {
        // 高さを無視した一様な霧
        opticalDepth = gDensity * distance;
    }
    else
    {
        // 積分の開始点は「フォグが効き始める位置」なので、そこの高さを使う
        float3 startPosition = gCameraPosition + rayDirection * min(travelDistance, gStartDistance);
        float densityAtStart = gDensity * exp(-gHeightFalloff * (startPosition.y - gBaseHeight));

        float directionY = rayDirection.y;
        if (abs(directionY) < 1e-4f)
        {
            // ほぼ水平な視線は高さが変わらないので、そのまま距離を掛ければよい
            opticalDepth = densityAtStart * distance;
        }
        else
        {
            float exponent = gHeightFalloff * directionY;
            opticalDepth = densityAtStart * (1.0f - exp(-exponent * distance)) / exponent;
        }
    }

    return 1.0f - exp(-max(opticalDepth, 0.0f));
}

/// 太陽の方を向いているほど霧を太陽の色へ寄せる
float3 ComputeFogColor(float3 rayDirection)
{
    if (gSunIntensity <= 0.0f)
    {
        return gFogColor;
    }

    // gSunDirection は光が進む向きなので、その逆が「太陽のある方角」。
    // ライト側の向きは正規化されているとは限らないのでここで揃える
    float sunLength = length(gSunDirection);
    if (sunLength < 1e-6f)
    {
        return gFogColor;
    }
    float sunAmount = saturate(dot(rayDirection, -gSunDirection / sunLength));
    float blend = saturate(pow(sunAmount, max(gSunExponent, 1e-2f)) * gSunIntensity);
    return lerp(gFogColor, gSunColor, blend);
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
    float rawDepth = gDepth.Load(int3(pixel, 0));

    // 深度が書かれていない場所＝空。復元すると遠平面の彼方になるので、
    // 最大距離までの霧として扱い、さらに別係数で強さを調節できるようにする
    bool isSky = (rawDepth >= 1.0f);

    float3 toPixel;
    float travelDistance;
    if (isSky)
    {
        // 遠平面上の点として方向だけ取り出す
        toPixel = ReconstructWorldPosition(pixel, 0.9999f) - gCameraPosition;
        travelDistance = gStartDistance + gMaxDistance;
    }
    else
    {
        toPixel = ReconstructWorldPosition(pixel, rawDepth) - gCameraPosition;
        travelDistance = length(toPixel);
    }

    float3 rayDirection = normalize(toPixel);

    float fogFactor = ComputeFogFactor(rayDirection, travelDistance);
    if (isSky)
    {
        fogFactor *= saturate(gSkyStrength);
    }
    fogFactor = min(fogFactor, saturate(gMaxOpacity));

    float3 fogColor = ComputeFogColor(rayDirection);

    gOutput[pixel] = float4(lerp(sourceColor.rgb, fogColor, fogFactor), sourceColor.a);
}
