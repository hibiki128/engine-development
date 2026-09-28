// =============================================================
// レイトレーシングによる影（インラインRT / RayQuery）
//
// 深度バッファからワールド座標を復元し、そこから光源へ向けて遮蔽レイを1本飛ばす。
// 何かに当たれば影、当たらなければ日向。結果を 0〜1 のマスクとして書き出す。
//
// **シェーダーテーブルも DXR 用のステートオブジェクトも raygen/hit/miss シェーダーも要らない。**
// 普通のコンピュートシェーダーの中で RayQuery を回すだけ（cs_6_5 以上が必要）。
//
// シャドウマップと違って、
//   ・解像度に起因するギザギザが無い
//   ・接地部分の影が浮かない／めり込まない
//   ・投影範囲（ortho）の外でも影が落ちる
// という利点がある。代わりにシーンの加速構造が要る。
// =============================================================

struct RtShadowConstants
{
    float4x4 inverseViewProjection; // NDC → ワールド
    float3 lightDirection;          // 平行光源が進む向き
    float normalBias;               // 自分自身に当たらないよう、始点を法線方向へ押し出す量
    float maxDistance;              // 遮蔽を探す最大距離
    float softness;                 // 影の柔らかさ（光源の見かけの大きさ。0で硬い影）
    int2 textureSize;               // 出力の解像度
};
ConstantBuffer<RtShadowConstants> gConstants : register(b0);

RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float> gDepth : register(t1);
Texture2D<float4> gNormal : register(t2);
RWTexture2D<float> gShadowMask : register(u0);

/// 深度バッファの値からワールド座標を復元する
float3 ReconstructWorldPosition(float2 uv, float rawDepth)
{
    float4 ndc = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, rawDepth, 1.0f);
    float4 world = mul(ndc, gConstants.inverseViewProjection);
    return world.xyz / (abs(world.w) < 1e-6f ? 1e-6f : world.w);
}

/// 画素ごとに散らばる 0〜1 の値。柔らかい影のためのばらつきに使う
float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    int2 pixel = int2(dispatchThreadID.xy);
    if (pixel.x >= gConstants.textureSize.x || pixel.y >= gConstants.textureSize.y)
    {
        return;
    }

    float rawDepth = gDepth.Load(int3(pixel, 0));

    // 空は影を落とす対象ではない（日向として扱う）
    if (rawDepth >= 1.0f)
    {
        gShadowMask[pixel] = 1.0f;
        return;
    }

    float2 uv = (float2(pixel) + 0.5f) / float2(gConstants.textureSize);
    float3 worldPosition = ReconstructWorldPosition(uv, rawDepth);

    float3 lightVector = -normalize(gConstants.lightDirection);

    // 面自身に当たってしまう（シャドウアクネ）のを防ぐため、始点を法線側へ少し浮かせる。
    // 法線が無い画素（前方描画で描かれた物）は、光の方向へ押し出して代用する
    float4 normalSample = gNormal.Load(int3(pixel, 0));
    float3 offsetDirection = (dot(normalSample.xyz, normalSample.xyz) > 1e-6f)
                                 ? normalize(normalSample.xyz)
                                 : lightVector;

    // 光に背を向けている面へはレイを飛ばさない（始点から面の中へ潜ってしまうため）。
    // ただし「影」ではなく「遮蔽なし」を返すこと。
    // 背を向けている面が暗くなるのはライティング側の N・L が担当していて、
    // ここで 0 を返すと二重に暗くなり、環境光まで落ちてしまう
    if (dot(offsetDirection, lightVector) <= 0.0f)
    {
        gShadowMask[pixel] = 1.0f;
        return;
    }

    float3 rayOrigin = worldPosition + offsetDirection * max(gConstants.normalBias, 1e-4f);

    // 柔らかい影にするため、光の向きを画素ごとに少しだけ散らす。
    // 太陽が点ではなく面積を持っていることの近似
    float3 rayDirection = lightVector;
    if (gConstants.softness > 0.0f)
    {
        float noise = InterleavedGradientNoise(float2(pixel));
        float angle = noise * 6.2831853f;
        // 光の向きに垂直な2軸を作り、その面内でずらす
        float3 up = abs(lightVector.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        float3 tangent = normalize(cross(up, lightVector));
        float3 bitangent = cross(lightVector, tangent);
        float radius = gConstants.softness * frac(noise * 7.0f);
        rayDirection = normalize(lightVector + (tangent * cos(angle) + bitangent * sin(angle)) * radius);
    }

    RayDesc ray;
    ray.Origin = rayOrigin;
    ray.Direction = rayDirection;
    ray.TMin = 0.0f;
    ray.TMax = max(gConstants.maxDistance, 1e-3f);

    // 影は「何かに当たったか」だけ分かればよいので、最初の交差で打ち切ってよい。
    // ACCEPT_FIRST_HIT_AND_END_SEARCH を付けると最短距離を探さなくなるぶん速い
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();

    const bool occluded = (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT);
    gShadowMask[pixel] = occluded ? 0.0f : 1.0f;
}
