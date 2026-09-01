#include "Deferred.hlsli"

// ============================================================
// タイルベースのライトカリング
//   画面を DEFERRED_TILE_SIZE 四方のタイルに分割し、タイルごとに
//   「そのタイルの錐台と交差するポイントライト」のインデックス配列を作る。
//   ライティングパスは自分のタイルのリストだけを走査すれば済むため、
//   光源が数千個あっても1画素あたりの処理は実際に届く光の数で頭打ちになる。
// ============================================================

ConstantBuffer<DeferredConstants> gConstants : register(b0);
Texture2D<float> gDepth : register(t0);
StructuredBuffer<PointLightGPU> gPointLights : register(t1);
// ライト総数（先頭1要素）。CPU分で初期化され、粒子光源CSが追記した分だけ増えている。
// CPUでは総数を確定できないため、走査数はこのGPU側カウンタから読む。
StructuredBuffer<uint> gLightCounter : register(t2);
RWStructuredBuffer<uint> gTileLightIndices : register(u0);

// タイル内の深度レンジ（float を asuint して整数アトミックで求める。深度は非負なので順序が保たれる）
groupshared uint gsMinDepth;
groupshared uint gsMaxDepth;
// タイルに残ったライト
groupshared uint gsLightCount;
groupshared uint gsLightIndices[DEFERRED_MAX_LIGHTS_PER_TILE];

/// <summary>デバイス深度(0〜1)をビュー空間のZへ戻す</summary>
float LinearizeDepth(float deviceDepth)
{
    // 行ベクトル規約の射影行列: depth = (viewZ * P22 + P32) / viewZ
    const float p22 = gConstants.projection[2][2];
    const float p32 = gConstants.projection[3][2];
    const float denom = deviceDepth - p22;
    // 分母0（無限遠）は far で頭打ちにする
    return abs(denom) < 1e-8f ? gConstants.farZ : (p32 / denom);
}

[numthreads(DEFERRED_TILE_SIZE, DEFERRED_TILE_SIZE, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID,
          uint3 groupId : SV_GroupID,
          uint groupIndex : SV_GroupIndex)
{
    if (groupIndex == 0)
    {
        gsMinDepth = 0x7F7FFFFFu; // float の最大値相当
        gsMaxDepth = 0u;
        gsLightCount = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    // ── タイル内の深度レンジを求める ──
    const uint2 pixel = dispatchThreadId.xy;
    if (pixel.x < gConstants.screenSize.x && pixel.y < gConstants.screenSize.y)
    {
        const float deviceDepth = gDepth.Load(int3(pixel, 0));
        // 深度1.0は「何も描かれていない」＝空。レンジを無駄に広げないよう除外する
        if (deviceDepth < 1.0f)
        {
            const uint depthBits = asuint(deviceDepth);
            InterlockedMin(gsMinDepth, depthBits);
            InterlockedMax(gsMaxDepth, depthBits);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    const uint tileIndex = groupId.y * gConstants.tileCount.x + groupId.x;
    const uint outBase = tileIndex * DEFERRED_TILE_STRIDE;

    // タイル全体が空なら照らす対象が無いので即終了
    if (gsMinDepth > gsMaxDepth)
    {
        if (groupIndex == 0)
        {
            gTileLightIndices[outBase] = 0u;
        }
        return;
    }

    const float tileMinViewZ = LinearizeDepth(asfloat(gsMinDepth));
    const float tileMaxViewZ = LinearizeDepth(asfloat(gsMaxDepth));

    // ── タイル錐台の側面4枚をビュー空間で構築する ──
    // ndc.x = viewX * P00 / viewZ, ndc.y = viewY * P11 / viewZ なので、
    // 境界 ndc=L の平面は原点を通り、法線 (P00, 0, -L) で表せる。
    const float p00 = gConstants.projection[0][0];
    const float p11 = gConstants.projection[1][1];

    // タイルの NDC 範囲（画面Yは下向き、NDC Yは上向きなので上下が入れ替わる）
    const float ndcLeft = -1.0f + (2.0f * float(groupId.x)) / float(gConstants.tileCount.x);
    const float ndcRight = -1.0f + (2.0f * float(groupId.x + 1)) / float(gConstants.tileCount.x);
    const float ndcTop = 1.0f - (2.0f * float(groupId.y)) / float(gConstants.tileCount.y);
    const float ndcBottom = 1.0f - (2.0f * float(groupId.y + 1)) / float(gConstants.tileCount.y);

    // 内側が正になるように法線を向ける
    float4 planes[4];
    planes[0] = float4(normalize(float3(p00, 0.0f, -ndcLeft)), 0.0f);   // 左
    planes[1] = float4(normalize(float3(-p00, 0.0f, ndcRight)), 0.0f);  // 右
    planes[2] = float4(normalize(float3(0.0f, p11, -ndcBottom)), 0.0f); // 下
    planes[3] = float4(normalize(float3(0.0f, -p11, ndcTop)), 0.0f);    // 上

    // ── ライトを分担して判定する ──
    // 粒子光源はGPUが InterlockedAdd で積むため、溢れた分までカウンタが進んでいる。
    // 実際に書き込まれたのは容量までなので、そこで頭打ちにする。
    const uint lightCount = min(gLightCounter[0], gConstants.pointLightCapacity);
    for (uint i = groupIndex; i < lightCount; i += DEFERRED_TILE_SIZE * DEFERRED_TILE_SIZE)
    {
        const PointLightGPU light = gPointLights[i];
        if (light.radius <= 0.0f || light.intensity <= 0.0f)
        {
            continue;
        }

        // ライト中心をビュー空間へ
        const float3 posView = mul(float4(light.position, 1.0f), gConstants.view).xyz;

        // 奥行き方向
        if (posView.z + light.radius < tileMinViewZ || posView.z - light.radius > tileMaxViewZ)
        {
            continue;
        }

        // 側面4枚
        bool inside = true;
        [unroll]
        for (int p = 0; p < 4; ++p)
        {
            if (dot(planes[p].xyz, posView) + light.radius < 0.0f)
            {
                inside = false;
            }
        }
        if (!inside)
        {
            continue;
        }

        uint slot;
        InterlockedAdd(gsLightCount, 1u, slot);
        if (slot < DEFERRED_MAX_LIGHTS_PER_TILE)
        {
            gsLightIndices[slot] = i;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // ── 共有メモリの結果を出力バッファへ書き出す ──
    const uint writeCount = min(gsLightCount, DEFERRED_MAX_LIGHTS_PER_TILE);
    if (groupIndex == 0)
    {
        gTileLightIndices[outBase] = writeCount;
    }
    for (uint w = groupIndex; w < writeCount; w += DEFERRED_TILE_SIZE * DEFERRED_TILE_SIZE)
    {
        gTileLightIndices[outBase + 1u + w] = gsLightIndices[w];
    }
}
