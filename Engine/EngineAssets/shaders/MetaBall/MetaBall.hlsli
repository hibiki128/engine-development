// ============================================================
// メタボール（GPU生成）の共通定義
//   要素配列     : float4 × 3 / 1個
//   頂点出力     : uint 配列（VertexData を 9 ワードで格納）
//   密度・カウンタ: float / uint の配列
// ============================================================

#ifndef HAGINE_METABALL_HLSLI
#define HAGINE_METABALL_HLSLI

// VertexData(float4 position / float2 texcoord / float3 normal) は 36 バイト = 9 ワード
#define METABALL_VERTEX_WORDS 9

// 要素1個の float4 の数（MetaBallGpuField.cpp の kBallStride と同じ）
#define METABALL_BALL_STRIDE 3

cbuffer MetaBallConstants : register(b0)
{
    float3 gGridOrigin;      // 格子の原点（サンプル点 (0,0,0) の位置）
    float gCellSize;         // セル1辺の長さ
    uint3 gGridSamples;      // 軸ごとのサンプル点数（セル数 = これ - 1）
    uint gBallCount;         // 要素の数
    float gThreshold;        // 等値面のしきい値
    float gUvScale;          // 平面投影UVのスケール
    uint gMaxVertexCount;    // 出力できる頂点数の上限
    float gTime;             // 経過時間（脈動用）
    float gWobbleAmplitude;  // 脈動の振幅（0で静止）
    float gWobbleSpeed;      // 脈動の速さ
    float gWobbleFrequency;  // 位置による位相のばらけ具合
    float gPadding;
};

/// <summary>
/// Wyvill の falloff（t2 = (距離 / 影響半径)^2、t=0 で 1・t=1 で 0）
/// </summary>
float MetaBallFalloff(float t2)
{
    const float a = 22.0f / 9.0f;
    const float b = 17.0f / 9.0f;
    const float c = 4.0f / 9.0f;
    return 1.0f + t2 * (-a + t2 * (b - c * t2));
}

/// <summary>
/// 脈動による半径の倍率
/// </summary>
float MetaBallWobbleScale(float phase)
{
    if (gWobbleAmplitude <= 0.0f)
    {
        return 1.0f;
    }
    return 1.0f + gWobbleAmplitude * sin(gTime * gWobbleSpeed + phase * gWobbleFrequency);
}

/// <summary>
/// 要素 i の中心・脈動を反映した影響半径・密度の高さ・箱の半分の大きさを取得
/// </summary>
void MetaBallFetch(StructuredBuffer<float4> balls, uint index, out float3 center, out float radius, out float stiffness,
                   out float3 halfExtents)
{
    const uint base = index * METABALL_BALL_STRIDE;
    const float4 positionRadius = balls[base + 0];
    const float4 extra = balls[base + 1];
    center = positionRadius.xyz;
    stiffness = extra.x;
    radius = max(positionRadius.w * MetaBallWobbleScale(extra.y), 1e-6f);
    halfExtents = balls[base + 2].xyz;
}

/// <summary>
/// 指定位置の密度（全要素の寄与の合計）
/// </summary>
float MetaBallDensityAt(StructuredBuffer<float4> balls, float3 position)
{
    float sum = 0.0f;
    for (uint i = 0; i < gBallCount; ++i)
    {
        float3 center;
        float radius;
        float stiffness;
        float3 halfExtents;
        MetaBallFetch(balls, i, center, radius, stiffness, halfExtents);

        // 箱の表面からの距離（halfExtents が 0 なら中心からの距離）
        const float3 diff = max(abs(position - center) - halfExtents, 0.0f);
        const float t2 = dot(diff, diff) / (radius * radius);
        if (t2 < 1.0f)
        {
            sum += stiffness * MetaBallFalloff(t2);
        }
    }
    return sum;
}

/// <summary>
/// サンプル点 (x,y,z) の格子内での通し番号
/// </summary>
uint MetaBallSampleIndex(uint3 coordinate)
{
    return (coordinate.z * gGridSamples.y + coordinate.y) * gGridSamples.x + coordinate.x;
}

#endif // HAGINE_METABALL_HLSLI
