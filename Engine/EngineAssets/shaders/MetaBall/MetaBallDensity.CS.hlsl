#include "MetaBall.hlsli"

// ============================================================
// 密度場の作成
//   格子のサンプル点1つにつき1スレッド。全ボールの寄与を足して密度を書く。
//   CPU 版は「要素が自分の影響球にだけ足し込む」scatter だが、GPU では
//   衝突なく並列化できる gather（サンプル点が全ボールを見る）にしてある。
//   影響半径の外は距離判定だけで抜けるので、離れたボールはほぼ無コスト。
// ============================================================

StructuredBuffer<float4> gBalls : register(t0);
RWStructuredBuffer<float> gDensity : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gGridSamples.x || id.y >= gGridSamples.y || id.z >= gGridSamples.z)
    {
        return;
    }

    const float3 position = gGridOrigin + float3(id) * gCellSize;
    gDensity[MetaBallSampleIndex(id)] = MetaBallDensityAt(gBalls, position);
}
