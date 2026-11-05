// =============================================================
// ボリュメトリックフォグ: 注入（本体）
//
// 視錐台を分けた格子（フロクセル）のセルごとに、霧の濃さと
// そこへ届く光（平行光源・点光源・スポットライト・環境光）を求め、
// 散乱（rgb）と減衰係数（a）として書く。
// セル内の位置はフレームごとにずらし、前フレームの結果と混ぜてノイズを消す。
//
// VOLUMETRIC_FOG_RAYTRACED_SHADOW が 1 のときは、点光源・スポットライトへ
// 影のレイ（RayQuery）を飛ばして遮られた光を落とす（cs_6_5 以上が必要）。
// =============================================================

#ifndef VOLUMETRIC_FOG_RAYTRACED_SHADOW
#define VOLUMETRIC_FOG_RAYTRACED_SHADOW 0
#endif

cbuffer InjectParams : register(b0)
{
    float4x4 gInverseView;            // ビュー → ワールド
    float4x4 gPreviousView;           // ワールド → 前フレームのビュー
    float4x4 gPreviousViewProjection; // ワールド → 前フレームのクリップ
    float4x4 gLightViewProjection;    // ワールド → 平行光源のクリップ
    float3 gCameraPosition;
    float gNear;                      // 一番手前のスライスの奥行き
    float3 gAlbedo;                   // 霧の色（光を散らす割合）
    float gFar;                       // 一番奥のスライスの奥行き
    float3 gDirectionalColor;         // 平行光源の色 × 強さ
    float gDensity;                   // 基準の高さでの霧の濃さ
    float3 gDirectionalDirection;     // 平行光源が進む向き
    float gHeightFalloff;             // 高さ方向に薄くなる速さ
    float3 gAmbientColor;             // 環境光
    float gBaseHeight;                // ここより下は濃さが一定
    float3 gJitter;                   // セル内の位置のずらし（xy: -0.5〜0.5 / z: 0〜1）
    float gHistoryBlend;              // 前フレームを混ぜる割合
    float gProjectionScaleX;          // 射影行列の _11
    float gProjectionScaleY;          // 射影行列の _22
    float gAnisotropy;                // 散乱の偏り
    float gShadowBias;                // シャドウ比較のバイアス
    uint gLightCount;                 // 点光源・スポットライトの数
    uint gShadowEnabled;              // シャドウマップが有効か
    uint gHistoryValid;               // 前フレームの結果が使えるか
    float gLocalLightScale;           // 点光源・スポットライトの散乱の強さ
    int3 gGridSize;                   // 格子の分割数
    float gPadding0;
    uint gLocalLightShadow;           // 点光源・スポットライトの影を調べるか
    float gLightShadowOffset;         // 影のレイを光源の手前で止める距離
    float2 gPadding1;
};

struct VolumeLight
{
    float3 position;  // ワールド座標
    float range;      // 届く距離
    float3 color;     // 色 × 強さ
    float decay;      // 減衰の鋭さ
    float3 direction; // スポットライトの向き
    float cosAngle;   // スポットライトの開き角の余弦
    uint type;        // 0: 点光源 / 1: スポットライト
    float3 padding;
};

Texture3D<float4> gHistory : register(t0);
Texture2D<float> gShadowMap : register(t1);
StructuredBuffer<VolumeLight> gLights : register(t2);
#if VOLUMETRIC_FOG_RAYTRACED_SHADOW
RaytracingAccelerationStructure gScene : register(t3);
#endif
RWTexture3D<float4> gOutput : register(u0);

SamplerState gSamplerLinear : register(s0);
SamplerState gSamplerPoint : register(s1);

static const float kPi = 3.14159265f;
// これより弱く届く光は影を調べない
static const float kShadowAttenuationThreshold = 1e-3f;

/// 格子の奥行き方向の位置（0〜1）からビュー空間の奥行きへ（手前ほど細かい指数分布）
float SliceToViewDepth(float slice)
{
    return gNear * pow(gFar / gNear, slice);
}

/// ビュー空間の奥行きから格子の奥行き方向の位置（0〜1）へ
float ViewDepthToSlice(float viewDepth)
{
    return log(max(viewDepth, 1e-4f) / gNear) / log(gFar / gNear);
}

/// 格子座標（セル単位の連続値）からワールド座標へ
float3 GridToWorld(float3 gridPosition)
{
    float2 uv = gridPosition.xy / float2(gGridSize.xy);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float viewDepth = SliceToViewDepth(gridPosition.z / float(gGridSize.z));
    float3 viewPosition = float3(ndc.x / gProjectionScaleX * viewDepth, ndc.y / gProjectionScaleY * viewDepth, viewDepth);
    return mul(float4(viewPosition, 1.0f), gInverseView).xyz;
}

/// 霧の濃さ（減衰係数）
float FogDensity(float3 worldPosition)
{
    float height = max(worldPosition.y - gBaseHeight, 0.0f);
    return gDensity * exp(-gHeightFalloff * height);
}

/// Henyey-Greenstein の位相関数。cosTheta は光の進む向きと「カメラへ向かう向き」の余弦
float PhaseFunction(float cosTheta)
{
    float g = clamp(gAnisotropy, -0.95f, 0.95f);
    float gg = g * g;
    float denominator = max(1.0f + gg - 2.0f * g * cosTheta, 1e-4f);
    return (1.0f - gg) / (4.0f * kPi * pow(denominator, 1.5f));
}

/// 平行光源の光が届いているか（1: 届く / 0: 影）
float SampleDirectionalShadow(float3 worldPosition)
{
    if (gShadowEnabled == 0)
    {
        return 1.0f;
    }
    float4 shadowCoord = mul(float4(worldPosition, 1.0f), gLightViewProjection);
    float3 projCoord = shadowCoord.xyz / (abs(shadowCoord.w) < 1e-6f ? 1e-6f : shadowCoord.w);
    float2 shadowUV = projCoord.xy * float2(0.5f, -0.5f) + 0.5f;

    // シャドウマップの外は光が届いているとみなす
    if (any(shadowUV < 0.0f) || any(shadowUV > 1.0f) || projCoord.z < 0.0f || projCoord.z > 1.0f)
    {
        return 1.0f;
    }
    float shadowDepth = gShadowMap.SampleLevel(gSamplerPoint, shadowUV, 0);
    return (projCoord.z - gShadowBias <= shadowDepth) ? 1.0f : 0.0f;
}

/// 点光源・スポットライトの光が届いているか（1: 届く / 0: 遮られる）
float LocalLightVisibility(float3 worldPosition, float3 toLight, float distance)
{
#if VOLUMETRIC_FOG_RAYTRACED_SHADOW
    if (gLocalLightShadow == 0)
    {
        return 1.0f;
    }
    // 光源を包むランプなどに当たらないよう、光源の手前でレイを止める
    float maxDistance = distance - gLightShadowOffset;
    if (maxDistance <= 1e-3f)
    {
        return 1.0f;
    }
    RayDesc ray;
    ray.Origin = worldPosition;
    ray.Direction = toLight;
    ray.TMin = 1e-3f;
    ray.TMax = maxDistance;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();
    return (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0f : 1.0f;
#else
    return 1.0f;
#endif
}

/// その点へ届いてカメラ方向へ散る光の量
float3 InScatteredLight(float3 worldPosition, float3 viewDirection)
{
    float3 light = gAmbientColor;

    // 平行光源
    float directionalLength = length(gDirectionalDirection);
    if (directionalLength > 1e-6f && any(gDirectionalColor > 0.0f))
    {
        float3 travel = gDirectionalDirection / directionalLength;
        light += gDirectionalColor * PhaseFunction(dot(travel, -viewDirection)) * SampleDirectionalShadow(worldPosition);
    }

    // 点光源・スポットライト（減衰はエンジンのライティングと同じ式）
    [loop]
    for (uint i = 0; i < gLightCount; ++i)
    {
        VolumeLight source = gLights[i];
        float3 toPoint = worldPosition - source.position;
        float distance = length(toPoint);
        if (distance >= source.range)
        {
            continue;
        }
        float3 travel = toPoint / max(distance, 1e-4f);
        float attenuation = pow(saturate(1.0f - distance / source.range), source.decay);
        if (source.type == 1)
        {
            float cosAngle = dot(travel, source.direction);
            attenuation *= saturate((cosAngle - source.cosAngle) / max(1.0f - source.cosAngle, 1e-4f));
        }
        if (attenuation < kShadowAttenuationThreshold)
        {
            continue;
        }
        attenuation *= LocalLightVisibility(worldPosition, -travel, distance);
        light += source.color * attenuation * PhaseFunction(dot(travel, -viewDirection)) * gLocalLightScale;
    }
    return light;
}

/// 前フレームの同じ場所の結果を引く（取れなければ false）
bool SampleHistory(float3 worldPosition, out float4 history)
{
    history = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (gHistoryValid == 0)
    {
        return false;
    }
    float4 previousClip = mul(float4(worldPosition, 1.0f), gPreviousViewProjection);
    float previousDepth = mul(float4(worldPosition, 1.0f), gPreviousView).z;
    if (previousClip.w <= 1e-4f || previousDepth <= 0.0f)
    {
        return false;
    }
    float2 ndc = previousClip.xy / previousClip.w;
    float3 uvw = float3(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f, ViewDepthToSlice(previousDepth));
    if (any(uvw < 0.0f) || any(uvw > 1.0f))
    {
        return false;
    }
    history = gHistory.SampleLevel(gSamplerLinear, uvw, 0);
    return true;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int3 cell = int3(dispatchThreadID);
    if (any(cell >= gGridSize))
    {
        return;
    }

    // セル内の位置をフレームごとにずらして計算する
    float3 samplePosition = GridToWorld(float3(cell) + float3(0.5f + gJitter.x, 0.5f + gJitter.y, gJitter.z));
    float3 viewDirection = normalize(samplePosition - gCameraPosition);

    // 霧が無いセルは光を調べない
    float density = FogDensity(samplePosition);
    float4 result = float4(0.0f, 0.0f, 0.0f, density);
    if (density > 1e-7f)
    {
        result.rgb = density * gAlbedo * InScatteredLight(samplePosition, viewDirection);
    }

    // 前フレームの結果はセルの中心で再投影して混ぜる
    float4 history;
    if (SampleHistory(GridToWorld(float3(cell) + 0.5f), history))
    {
        result = lerp(result, history, gHistoryBlend);
    }

    gOutput[cell] = result;
}
