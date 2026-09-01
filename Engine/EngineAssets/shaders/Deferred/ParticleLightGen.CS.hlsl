#include "../Particle/Particle.hlsli"
#include "Deferred.hlsli"

// ============================================================
// 粒子1個1個の光源化
//   GPUパーティクルの生存粒子バッファ（描画コンパクション済み＝詰めた順）を読み、
//   間引きながらポイントライトのStructuredBufferへ追記する。
//   位置がGPU上にしか無いためCPUを経由せず、ここで直接ライト配列を作る。
//
//   追記先は LightGroup のポイントライトバッファ。先頭にはCPU側（手置き＋動的）の
//   ライトが詰められており、カウンタもその個数で初期化済み。このCSは
//   InterlockedAdd でその後ろを取り合う。
//
//   ライトは数が増えるほどタイルあたりの負荷になるので、
//   「何粒ごとに1つ光源にするか（間引き）」と「1エミッターあたりの上限」で必ず絞る。
// ============================================================

/// <summary>
/// 粒子光源の生成パラメータ（C++ ParticleLightGenConstants と一致させること）
/// </summary>
struct ParticleLightGenConstants
{
    uint particleStride;   // 間引き: この個数ごとに1粒を光源にする（1で全粒子）
    uint maxLights;        // このディスパッチが作れる光源の上限
    uint bufferCapacity;   // ライトバッファ全体の容量（超えた分は捨てる）
    uint useParticleColor; // 1=粒子の色をそのまま光の色にする / 0=固定色を使う

    float3 lightColor; // 固定色（useParticleColor=0 のとき）
    float intensity;   // 光の強さ（粒子のアルファを掛ける＝寿命フェードに追従）

    float radius;        // 光の届く半径
    float decay;         // 減衰の強さ
    float cullDistance;  // カメラからこの距離を超える粒子は光源にしない（0=無効）
    float alphaCutoff;   // このアルファ未満の粒子は光源にしない

    float3 cameraPosition; // 距離カリング用のカメラワールド座標
    float padding;
};

ConstantBuffer<ParticleLightGenConstants> gConstants : register(b0);
// 描画コンパクション済みの生存粒子（instanceId 順に詰められている）
StructuredBuffer<PDrawCore> gRenderCompact : register(t0);
// 当該フレームの生存数（先頭1要素）
StructuredBuffer<uint> gAliveCount : register(t1);
// ライト配列とその総数カウンタ
RWStructuredBuffer<PointLightGPU> gLights : register(u0);
RWStructuredBuffer<uint> gLightCounter : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint lightSlot = dispatchThreadId.x;
    if (lightSlot >= gConstants.maxLights)
    {
        return;
    }

    // ディスパッチ数はCPU側の生存数（1〜2フレーム遅延）から決めているため、
    // 実際の生存数はGPU側のカウンタで判定する。
    const uint particleIndex = lightSlot * gConstants.particleStride;
    if (particleIndex >= gAliveCount[0])
    {
        return;
    }

    const PDrawCore particle = gRenderCompact[particleIndex];
    const float4 particleColor = UnpackColorRGBA8(particle.color);

    // 消えかけの粒子まで光源にすると数だけ増えて絵に効かないので落とす
    if (particleColor.a <= gConstants.alphaCutoff)
    {
        return;
    }

    // 遠くの粒子光源は画面上ほとんど効かないわりにタイルを圧迫するので捨てる
    if (gConstants.cullDistance > 0.0f &&
        distance(particle.translate, gConstants.cameraPosition) > gConstants.cullDistance)
    {
        return;
    }

    // アルファに追従させると、粒子の消え際で光もフェードアウトする
    const float intensity = gConstants.intensity * particleColor.a;
    if (intensity <= 0.0001f)
    {
        return;
    }

    uint index;
    InterlockedAdd(gLightCounter[0], 1u, index);
    if (index >= gConstants.bufferCapacity)
    {
        return; // 溢れた分は捨てる（カウンタは進むのでUI側で検出できる）
    }

    PointLightGPU light;
    light.position = particle.translate;
    light.radius = gConstants.radius;
    light.color = gConstants.useParticleColor != 0 ? particleColor.rgb : gConstants.lightColor;
    light.intensity = intensity;
    light.decay = gConstants.decay;
    // 粒子まわりの陰影は柔らかいほうが自然なのでハーフランバート固定
    light.flags = POINT_LIGHT_FLAG_HALF_LAMBERT;
    light.padding = float2(0.0f, 0.0f);
    gLights[index] = light;
}
