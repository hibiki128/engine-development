#include "MetaBall.hlsli"

// ============================================================
// Marching Cubes（セル1つにつき1スレッド）
//
//   密度場のしきい値をまたぐ辺に頂点を作り、三角形を出力バッファへ詰める。
//   CPU 版（MetaBall.cpp）と同じテーブル・同じ補間・同じ法線の作り方なので、
//   出てくる形は CPU 版と一致する。
//
//   違いは頂点を共有しないこと。CPU 版は辺ごとにキャッシュして
//   インデックスを張り直すが、GPU では三角形ごとに3頂点を書き出す
//   （法線は位置から作るので、共有しなくても滑らかさは変わらない）。
// ============================================================

// 三角形テーブル: 256 パターン × 16（辺番号3つで三角形1枚 / -1 が終端）
StructuredBuffer<int> gTriTable : register(t0);
RWStructuredBuffer<float> gDensity : register(u0);
RWStructuredBuffer<uint> gVertices : register(u1);
RWStructuredBuffer<uint> gCounter : register(u2);

/// 隅 i の格子オフセット（CPU 版 kMarchingCubesCornerOffset と同じ並び）
static const int3 kCornerOffset[8] = {
    int3(0, 0, 0), int3(1, 0, 0), int3(1, 0, 1), int3(0, 0, 1),
    int3(0, 1, 0), int3(1, 1, 0), int3(1, 1, 1), int3(0, 1, 1),
};

/// 辺 i が結ぶ隅の番号（CPU 版 kMarchingCubesEdgeCorner と同じ並び）
static const int2 kEdgeCorner[12] = {
    int2(0, 1), int2(1, 2), int2(2, 3), int2(3, 0),
    int2(4, 5), int2(5, 6), int2(6, 7), int2(7, 4),
    int2(0, 4), int2(1, 5), int2(2, 6), int2(3, 7),
};

/// <summary>格子の外へはみ出さないように読む密度</summary>
float DensityAt(int3 coordinate)
{
    const int3 clamped = clamp(coordinate, int3(0, 0, 0), int3(gGridSamples) - 1);
    return gDensity[MetaBallSampleIndex(uint3(clamped))];
}

/// <summary>密度場の勾配。密度は内側ほど高いので、外向き法線は勾配の逆になる</summary>
float3 GradientAt(int3 coordinate)
{
    return float3(DensityAt(coordinate + int3(1, 0, 0)) - DensityAt(coordinate - int3(1, 0, 0)),
                  DensityAt(coordinate + int3(0, 1, 0)) - DensityAt(coordinate - int3(0, 1, 0)),
                  DensityAt(coordinate + int3(0, 0, 1)) - DensityAt(coordinate - int3(0, 0, 1))) *
           (0.5f / gCellSize);
}

/// <summary>サンプル点の位置</summary>
float3 SamplePosition(int3 coordinate)
{
    return gGridOrigin + float3(coordinate) * gCellSize;
}

/// <summary>頂点1個を出力バッファへ詰める（VertexData と同じ 9 ワード）</summary>
void StoreVertex(uint vertexIndex, float3 position, float2 texcoord, float3 normal)
{
    const uint base = vertexIndex * METABALL_VERTEX_WORDS;
    gVertices[base + 0] = asuint(position.x);
    gVertices[base + 1] = asuint(position.y);
    gVertices[base + 2] = asuint(position.z);
    gVertices[base + 3] = asuint(1.0f);
    gVertices[base + 4] = asuint(texcoord.x);
    gVertices[base + 5] = asuint(texcoord.y);
    gVertices[base + 6] = asuint(normal.x);
    gVertices[base + 7] = asuint(normal.y);
    gVertices[base + 8] = asuint(normal.z);
}

/// <summary>辺の上に頂点を作って書き込む</summary>
void EmitEdgeVertex(uint vertexIndex, int3 cell, int edge, float corner[8])
{
    const int c0 = kEdgeCorner[edge].x;
    const int c1 = kEdgeCorner[edge].y;
    const int3 coordinate0 = cell + kCornerOffset[c0];
    const int3 coordinate1 = cell + kCornerOffset[c1];

    // しきい値をまたぐ位置を線形補間で求める
    const float d0 = corner[c0];
    const float d1 = corner[c1];
    const float diff = d1 - d0;
    const float t = saturate(abs(diff) > 1e-8f ? (gThreshold - d0) / diff : 0.5f);

    const float3 position = lerp(SamplePosition(coordinate0), SamplePosition(coordinate1), t);

    float3 normal = -lerp(GradientAt(coordinate0), GradientAt(coordinate1), t); // 外向き
    const float lengthSq = dot(normal, normal);
    normal = (lengthSq > 1e-12f) ? normal * rsqrt(lengthSq) : float3(0.0f, 1.0f, 0.0f);

    // Marching Cubes は UV を作らないので、法線の主軸へ平面投影しておく
    const float3 axis = abs(normal);
    float2 texcoord;
    if (axis.x >= axis.y && axis.x >= axis.z)
    {
        texcoord = float2(position.z, position.y) * gUvScale;
    }
    else if (axis.y >= axis.z)
    {
        texcoord = float2(position.x, position.z) * gUvScale;
    }
    else
    {
        texcoord = float2(position.x, position.y) * gUvScale;
    }

    StoreVertex(vertexIndex, position, texcoord, normal);
}

[numthreads(4, 4, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
    // セル数はサンプル点数より1つ少ない
    if (id.x + 1 >= gGridSamples.x || id.y + 1 >= gGridSamples.y || id.z + 1 >= gGridSamples.z)
    {
        return;
    }

    const int3 cell = int3(id);

    // セルの8隅の密度を取り、しきい値以上の隅のビットを立てる
    float corner[8];
    uint cubeIndex = 0;
    [unroll] for (int i = 0; i < 8; ++i)
    {
        corner[i] = DensityAt(cell + kCornerOffset[i]);
        if (corner[i] >= gThreshold)
        {
            cubeIndex |= (1u << uint(i));
        }
    }

    // 全部内側／全部外側なら表面は通らない
    if (cubeIndex == 0 || cubeIndex == 255)
    {
        return;
    }

    // このセルが出す三角形の数を先に数える（確保は1回のアトミックで済ませる）
    const int tableBase = int(cubeIndex) * 16;
    int triangleCount = 0;
    for (int t = 0; t < 15; t += 3)
    {
        if (gTriTable[tableBase + t] < 0)
        {
            break;
        }
        ++triangleCount;
    }
    if (triangleCount == 0)
    {
        return;
    }

    const uint vertexCount = uint(triangleCount) * 3;
    uint base = 0;
    InterlockedAdd(gCounter[0], vertexCount, base);
    // あふれたぶんは捨てる（上限は CPU 側が十分大きく取るが、保険として必ず見る）
    if (base + vertexCount > gMaxVertexCount)
    {
        return;
    }

    for (int triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex)
    {
        const int slot = triangleIndex * 3;
        EmitEdgeVertex(base + uint(slot) + 0, cell, gTriTable[tableBase + slot + 0], corner);
        EmitEdgeVertex(base + uint(slot) + 1, cell, gTriTable[tableBase + slot + 1], corner);
        EmitEdgeVertex(base + uint(slot) + 2, cell, gTriTable[tableBase + slot + 2], corner);
    }
}
