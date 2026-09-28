#include "../Particle.hlsli"
#include "../../Random/Random.hlsli"
#include "CurlNoise.hlsli"

ConstantBuffer<PerFrame> gPerFrame : register(b0);
ConstantBuffer<ParticleCSSettings> gSettings : register(b1);
ConstantBuffer<FieldCountCB> gFieldCB : register(b2);
// SoA バッファ（u0-u5）。Update は使う機能のバッファだけ load/store して帯域を削る。
RWStructuredBuffer<float>     gLife     : register(u0);
RWStructuredBuffer<PDrawCore> gDrawCore : register(u1);
RWStructuredBuffer<PSimCore>  gSimCore  : register(u2);
RWStructuredBuffer<PTrail>    gTrail    : register(u3);
RWStructuredBuffer<PRotation> gRotation : register(u4);
RWStructuredBuffer<uint2>     gOverride : register(u5);
// フリーリスト（u6-u8）
RWStructuredBuffer<int>  gFreeListIndex     : register(u6);
RWStructuredBuffer<uint> gFreeList          : register(u7);
RWStructuredBuffer<int>  gFreeListTailIndex : register(u8);
// 生存コンパクション用（u9-u10）: 生存slot indexを詰める / append位置のアトミックカウンタ
RWStructuredBuffer<uint> gAliveList    : register(u9);
RWStructuredBuffer<uint> gAliveCounter : register(u10);
// 描画コンパクション（u11）: 描画データ(DrawCore=40B)を詰めた順(instanceId順)に書き出す。
// 描画VSはこれを順次読みして散乱gatherを排除する。
RWStructuredBuffer<PDrawCore> gRenderCompact : register(u11);
// GPU駆動カリング: 描画リストは生存リストと別カウンタ・別順序になる。
RWStructuredBuffer<uint> gVisibleCounter : register(u12); // 描画リスト長(=instanceCount)
RWStructuredBuffer<uint> gRenderSlot     : register(u13); // 描画順 -> 実 slot index
StructuredBuffer<ParticleFieldGPU> gFields : register(t0);
StructuredBuffer<ParticleFieldSettingsOverrideData> gFieldsOverride : register(t1);
// 生存リスト間接ディスパッチ: 前フレームの out リスト = 今フレームの in（処理対象）。
//   Update は全スロット走査をやめ、この in リストの tid 番目だけを sim する → O(生存数)。
StructuredBuffer<uint> gAliveListIn    : register(t2); // in: 処理対象 slot index 列
StructuredBuffer<uint> gAliveCounterIn : register(t3); // in: リスト長

// =============================================
// フィールド適用結果（色・大きさ・トレイルは後段で掛ける）
// =============================================
struct FieldEffectResult
{
    uint forceTrail;         // 1ならトレイル強制生成
    float trailDistOverride; // >0 なら trailSpawnDistance を上書き
    float4 colorMultiplier;  // 色乗算 (白=変化なし)
    float sizeMultiplier;    // 大きさの倍率 (1=変化なし)
};

// =============================================
// 一度きり設定上書き処理
//   フィールド内に入ったパーティクルへ、まだ上書きされていない
//   項目（FieldOverrideBits の8項目）を直接書き換える。
//   書き換えた項目は settingsOverrideFlags.x に記録し、
//   以降の再入時は何もしない（一度きり保証）。
//   ※ 一度きり保証は「項目単位」。複数フィールドが同じ項目を
//     上書きする設定でも、最初に触れたフィールドだけが効く。
//
//   呼び出し元: main() のフィールドループ内
//   引数 p             : 対象パーティクル（ローカル。global往復を排除）
//   引数 fi            : gFieldsOverride の添字（gFields と同一）
//   引数 particleIndex : 乱数シード用のパーティクル配列インデックス
// =============================================
void ApplySettingsOverride(inout Particle p, uint fi, uint particleIndex)
{
    ParticleFieldSettingsOverrideData ov = gFieldsOverride[fi];
    // overrideMask が 0 なら何もしない（高速パス）
    if (ov.overrideMask == 0u)
        return;

    // まだ上書きされていないビットだけ処理する
    uint pending = ov.overrideMask & ~p.settingsOverrideFlags.x;
    if (pending == 0u)
        return; // 全項目上書き済み

    // Min/Max 乱数用（pending がある時だけ生成するので通常パスにコストなし）
    RandomGenerator rng;
    rng.InitSeed(
        uint3(particleIndex, fi * 131u + 7u, particleIndex * 7919u),
        gPerFrame.time
    );

    // --- 寿命: Min/Max 乱数で上書き（縮める用途では自然に早死にする） ---
    if (pending & (1u << OB_LifeTime))
        p.lifeTime = lerp(ov.lifeTimeMin, ov.lifeTimeMax, rng.Generate1d());

    // --- スケール: Min/Max 乱数で上書き（initialScale ごと差し替え） ---
    if (pending & (1u << OB_Scale))
    {
        float s = lerp(ov.scaleMin, ov.scaleMax, rng.Generate1d());
        p.initialScale = float3(s, s, s);
        p.scale = float3(s, s, s);
    }

    // --- 速度: Min/Max 乱数で置換 ---
    if (pending & (1u << OB_Velocity))
    {
        p.velocity = float3(
            lerp(ov.velocityMin.x, ov.velocityMax.x, rng.Generate1d()),
            lerp(ov.velocityMin.y, ov.velocityMax.y, rng.Generate1d()),
            lerp(ov.velocityMin.z, ov.velocityMax.z, rng.Generate1d()));
    }

    // --- 速度倍率: 一度だけ乗算（0で停止、負で反転もできる） ---
    if (pending & (1u << OB_VelocityMul))
        p.velocity *= ov.velocityMultiplier;

    // --- 加速度インパルス: 一度だけ加算 ---
    if (pending & (1u << OB_AccelImpulse))
        p.velocity += ov.accelImpulse;

    // --- 色: RGB を上書き。フラグが立っている間は main() の色再計算から
    //     RGB を保護する（アルファのフェードは通常どおり継続） ---
    if (pending & (1u << OB_Color))
        p.color.rgb = ov.color.rgb;

    // --- トレイル生成間隔の上書き ---
    if (pending & (1u << OB_TrailDistance))
        p.trailSpawnDistance = ov.trailSpawnDistance;

    // --- 向け替え: 速さを保ったままターゲット方向へ ---
    if (pending & (1u << OB_GatherRedirect))
    {
        float3 toTarget = ov.gatherTarget - p.translate;
        float dist = length(toTarget);
        if (dist > 0.01f)
            p.velocity = (toTarget / dist) * length(p.velocity);
    }

    // --- 書き換えたビットを記録 ---
    p.settingsOverrideFlags.x |= pending;
}

// =============================================
// フィールド適用
//   gPerFrame.fieldUpdateMask に立っているフィールドだけを1回のループで処理する
//   （レイヤーの一致と「粒子に効く効果を持つか」は CPU で判定済み）。
//   マスクはディスパッチ全体で同じなので、ループと効果の分岐はウェーブ内で揃う。
//   速度・寿命・入った瞬間の変化はここで p に直接反映し、色・大きさ・トレイルは結果で返して後段で掛ける。
// =============================================
FieldEffectResult ApplyFields(inout Particle p, uint particleIndex, float deltaTime)
{
    FieldEffectResult result;
    result.forceTrail = 0;
    result.trailDistOverride = 0.0f;
    result.colorMultiplier = float4(1, 1, 1, 1);
    result.sizeMultiplier = 1.0f;

    uint mask = gPerFrame.fieldUpdateMask;
    [loop]
    while (mask != 0u)
    {
        const uint fi = firstbitlow(mask);
        mask &= mask - 1u;
        const ParticleFieldGPU f = gFields[fi];

        float w;
        float3 local;
        if (!EvaluateField(f, p.translate, w, local))
            continue;

        const uint fx = f.effectFlags;

        // ---- 消す（入ったらすぐ）----
        if (fx & FE_Kill)
        {
            p.currentTime = p.lifeTime;
            continue;
        }

        // ---- 風: 粒子の速度を風の速度へなじませる（空気に流される動き）----
        if (fx & FE_Wind)
        {
            const float k = 1.0f - exp(-f.windResponse * w * deltaTime);
            p.velocity += (f.windVelocity - p.velocity) * k;
        }

        // ---- 引き寄せ / 押し出し ----
        if (fx & FE_Attract)
        {
            const float3 toCenter = f.center - p.translate;
            const float dist = length(toCenter);
            if (f.absorbRadius > 0.0f && dist < f.absorbRadius)
            {
                // 中心に着いた粒子は消す（吸い込み）
                p.currentTime = p.lifeTime;
                continue;
            }
            if (dist > 1e-4f)
                p.velocity += (toCenter / dist) * (f.attractStrength * w * deltaTime);
        }

        // ---- 渦: 形の Y 軸まわりの接線方向の速さを目標の速さへなじませる ----
        //   接線方向の成分だけを寄せるので、半径方向・軸方向の動きはそのまま残る（外へ飛んでいかない）
        if (fx & FE_Vortex)
        {
            const float3 radial = f.axisX * local.x + f.axisZ * local.z;
            const float radialLength = length(radial);
            if (radialLength > 1e-4f)
            {
                const float3 tangent = cross(f.axisY, radial / radialLength);
                const float current = dot(p.velocity, tangent);
                const float k = 1.0f - exp(-f.vortexResponse * w * deltaTime);
                p.velocity += tangent * ((f.vortexSpeed - current) * k);
            }
        }

        // ---- 抵抗 ----
        if (fx & FE_Drag)
            p.velocity *= exp(-f.dragPerSecond * w * deltaTime);

        // ---- 寿命の進み ----
        if (fx & FE_Life)
            p.currentTime = min(p.currentTime + (f.lifeSpeed - 1.0f) * w * deltaTime, p.lifeTime);

        // ---- 色・大きさ（後段で掛ける。複数重なれば掛け合わせ）----
        if (fx & FE_Tint)
            result.colorMultiplier *= lerp(float4(1, 1, 1, 1), f.tint, w);
        if (fx & FE_Size)
            result.sizeMultiplier *= lerp(1.0f, f.sizeScale, w);

        // ---- トレイル（範囲内にいる間だけ出す。間隔は一番細かいものを使う）----
        if (fx & FE_Trail)
        {
            result.forceTrail = 1;
            if (f.trailSpawnDistance > 0.0f)
                result.trailDistOverride = (result.trailDistOverride <= 0.0f) ? f.trailSpawnDistance
                                                                              : min(result.trailDistOverride, f.trailSpawnDistance);
        }

        // ---- 入った瞬間に1回だけ ----
        if (fx & FE_Once)
            ApplySettingsOverride(p, fi, particleIndex);
    }
    return result;
}

// 親パーティクルはローカル変数 p で読み書きする（global往復を排除）。
// 子トレイルスロットへの書き込みのみ gParticles[trailIndex] を直接触る（互いに素なので安全）。
void SpawnTrailParticles(inout Particle p, int particleIndex, float3 currentPosition)
{
    float parentLifeTime = p.lifeTime;
    float parentCurrentTime = p.currentTime;
    float parentRemainingLife = max(0.0f, parentLifeTime - parentCurrentTime);

    if (parentRemainingLife < gSettings.trailMinLifeTime * 0.2f)
        return;

    float3 lastPos = p.lastTrailPosition;
    float targetDistance = p.trailSpawnDistance;

    if (targetDistance <= 0.001f)
        return;

    float3 moveVector = currentPosition - lastPos;
    float totalDistance = length(moveVector);
    
    if (totalDistance < targetDistance)
        return;

    int desiredTrails = int(totalDistance / targetDistance);
    int trailsToSpawn = min(desiredTrails, gSettings.maxTrailPerParticle);

    if (trailsToSpawn <= 0)
        return;

    // フリーリストから必要本数を予約し、空きが足りなければ「入る分だけ」に切り詰める。
    // 旧実装は1本でも足りないと全件ロールバックして0本生成だった（フリーリスト枯渇＝高密度時に
    // トレイルが丸ごと消える）。ここでは入る分だけ生成し余剰予約のみ返すことで、高負荷時も
    // 滑らかに減衰させる。
    // フリーリストは [head, tail) が利用可能。head を進めて予約し、余りを返す。
    uint capacity = gSettings.maxParticleCount;
    uint originalHead;
    InterlockedAdd(gFreeListIndex[0], trailsToSpawn, originalHead);
    uint tail = gFreeListTailIndex[0];
    int available = (int) (tail - originalHead); // モジュラ差分（over-reserve 時は負）

    if (available <= 0)
    {
        // 1スロットも確保できなかった → 予約を全部返して終了
        int dummyRollback;
        InterlockedAdd(gFreeListIndex[0], -trailsToSpawn, dummyRollback);
        return;
    }
    if (available < trailsToSpawn)
    {
        // 入る分だけに切り詰め、余剰予約を返す
        int giveBack;
        InterlockedAdd(gFreeListIndex[0], -(trailsToSpawn - available), giveBack);
        trailsToSpawn = available;
    }

    // 低FPS(大きいdeltaTime)や本数上限/フリーリスト枯渇で desiredTrails を全部置けない場合は、
    // 移動区間全体へ均等配置して追従の遅れを防ぐ（capped）。全部置けるときは targetDistance 間隔。
    bool capped = (desiredTrails > trailsToSpawn);
    float spacing = capped ? (totalDistance / float(trailsToSpawn)) : targetDistance;

    float3 direction = normalize(moveVector);
    int requiredCount = trailsToSpawn;

    float3 parentCurrentScale = p.scale;
    float3 parentVelocity = p.velocity;
    float4 parentColor = p.color;

    float desiredTrailLife = parentRemainingLife * gSettings.trailLifeTimeScale;
    float trailLifeTime = max(desiredTrailLife, gSettings.trailMinLifeTime);
    trailLifeTime = clamp(trailLifeTime, gSettings.trailMinLifeTime, 10.0f);

    for (int i = 0; i < requiredCount; ++i)
    {
        int slot = (originalHead + i) % capacity;
        int trailIndex = gFreeList[slot];

        if (trailIndex < 0 || trailIndex >= gSettings.maxParticleCount)
            continue;

        float spawnDistance = spacing * (float(i) + 1.0f);
        float3 spawnPosition = lastPos + direction * spawnDistance;

        RandomGenerator generator;
        generator.InitSeed(
            uint3(particleIndex, gPerFrame.groupId + i, uint(gPerFrame.time * 1000.0f)),
            gPerFrame.time + float(particleIndex) + float(i) * 0.1f
        );

        // 子トレイルスロットへ SoA バッファで書き込む（互いに素なスロットなので安全）。
        float3 childInitialScale = parentCurrentScale * gSettings.trailScaleMultiplier;

        PDrawCore cdc;
        cdc.translate = spawnPosition;
        cdc.scaleXY = PackScaleXY(childInitialScale);
        cdc.scaleZ = PackScaleZ(childInitialScale);
        cdc.velocity = (gSettings.trailInheritVelocity != 0)
                           ? (parentVelocity * gSettings.trailVelocityScale)
                           : float3(0.0f, 0.0f, 0.0f);
        cdc.color = PackColorRGBA8(parentColor * gSettings.trailColorMultiplier);
        gDrawCore[trailIndex] = cdc;

        PSimCore csc;
        csc.currentTime = 0.0f;
        csc.initialScaleXY = PackScaleXY(childInitialScale);
        csc.initialScaleZ_isTrail = PackScaleZTrail(childInitialScale, 1u); // トレイル isTrail=1
        gSimCore[trailIndex] = csc;

        PTrail ctr;
        ctr.parentIndex = particleIndex;
        ctr.lastTrailPosition = spawnPosition;
        ctr.trailSpawnDistance = gSettings.trailSpawnDistance;
        gTrail[trailIndex] = ctr;

        // Life は最後に書いてスロットを「生存」にする（次フレームから更新対象）
        gLife[trailIndex] = trailLifeTime;

        // 生存リスト間接ディスパッチ: 生成したトレイル子も out リストへ append する。
        //   in リスト経由でしか sim しない設計のため、append しないと子が処理も描画もされない。
        uint trailDst;
        InterlockedAdd(gAliveCounter[0], 1, trailDst);
        gAliveList[trailDst] = (uint) trailIndex;

        // GPU駆動カリング: 子も視錐台に入っていれば描画リストへ詰めて今フレームから描く。
        bool trailVisible = (gSettings.enableFrustumCull == 0) ||
                            IsSphereInFrustum(gSettings.frustumPlanes, spawnPosition,
                                              ParticleCullRadius(childInitialScale, cdc.velocity,
                                                                 gSettings.frustumRadiusScale,
                                                                 gSettings.frustumStretchFactor));
        if (trailVisible)
        {
            uint trailVisDst;
            InterlockedAdd(gVisibleCounter[0], 1, trailVisDst);
            gRenderCompact[trailVisDst] = cdc;
            if (gSettings.enableRandomRotation != 0 || gSettings.enableRandomAngularVelocity != 0)
                gRenderSlot[trailVisDst] = (uint) trailIndex;
        }
    }

    // capped のときは移動区間を全消費して lastTrailPosition を currentPosition まで進め、
    // 追従の遅れを次フレームへ持ち越さないようにする。
    float consumedDistance = capped ? totalDistance : (float(requiredCount) * targetDistance);
    p.lastTrailPosition = lastPos + direction * consumedDistance;
}

// スレッドグループ256（C++ kFullUpdateThreadsPerGroup と一致必須）。
// 演出多用でレジスタが多くオキュパンシが低いため、常駐ブロック数を増やす狙いで1024→256に縮小。
// Wave 単位集約（下記 WaveActiveSum/WavePrefixSum 等）はブロックサイズ非依存なので正しさ不変。
[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // 生存リスト間接ディスパッチ: 全スロット走査ではなく in リストの tid 番目だけ処理する。
    //   tid >= リスト長 のスレッドは何もしない（早期 return。フル版は Wave 集約なのでバリア制約なし）。
    uint tid = DTid.x;
    if (tid >= gAliveCounterIn[0])
        return;
    int particleIndex = (int) gAliveListIn[tid];
    if (particleIndex < 0 || particleIndex >= (int) gSettings.maxParticleCount)
        return;

    // =============================================
    // SoA ロード:
    //   まず Life(4B) だけ読み、死亡/未使用スロット(lifeTime<=0)は
    //   ここで離脱して残りのバッファを一切触らない（帯域の最大の削減点）。
    //   生存スロットは DrawCore/SimCore を常時、Trail/Rotation/Override を
    //   機能フラグに応じてのみ load する。以降は従来どおりローカル p を読み書きする。
    // =============================================
    float lifeTime = gLife[particleIndex];
    if (lifeTime <= 0.0f)
        return;

    // 機能ゲート: 使う機能のバッファだけ load/store する
    const bool useRotation = (gSettings.enableRandomRotation != 0 || gSettings.enableRandomAngularVelocity != 0);
    // フィールドは「受ける設定（fieldCount>0）」かつ「効くものがある（マスク≠0）」ときだけ
    const bool fieldsOn = (gFieldCB.fieldCount > 0) && (gPerFrame.fieldUpdateMask != 0u);
    const bool useOverride = fieldsOn;
    const bool useTrail = (gSettings.enableTrail != 0 || fieldsOn);

    Particle p = (Particle) 0;
    p.lifeTime = lifeTime;

    PDrawCore dc = gDrawCore[particleIndex];
    p.translate = dc.translate;
    p.scale = UnpackScale3(dc.scaleXY, dc.scaleZ);
    p.velocity = dc.velocity;
    p.color = UnpackColorRGBA8(dc.color);

    PSimCore sc = gSimCore[particleIndex];
    p.currentTime = sc.currentTime;
    p.initialScale = UnpackScale3(sc.initialScaleXY, sc.initialScaleZ_isTrail);
    p.isTrailParticle = sc.initialScaleZ_isTrail >> 16; // 上位16bit = isTrailParticle

    if (useTrail)
    {
        PTrail tr = gTrail[particleIndex];
        p.parentIndex = tr.parentIndex;
        p.lastTrailPosition = tr.lastTrailPosition;
        p.trailSpawnDistance = tr.trailSpawnDistance;
    }

    if (useRotation)
    {
        PRotation rot = gRotation[particleIndex];
        p.rotation = rot.rotation;
        p.angularVelocity = rot.angularVelocity;
    }

    if (useOverride)
        p.settingsOverrideFlags = gOverride[particleIndex];
        
        // 1. 加速度処理
    if (gSettings.enableAcceleration)
    {
        p.velocity += gSettings.acceleration * gPerFrame.deltaTime;
    }
        
        // 2. 重力処理
    if (gSettings.enableGravity)
    {
        p.velocity += gSettings.gravity * gPerFrame.deltaTime;
    }
        
        // 3. 速度減衰処理
    if (gSettings.enableVelocityDamping)
    {
        p.velocity *= pow(gSettings.velocityDampingFactor, gPerFrame.deltaTime * 60.0f);
    }
        
        // 4. ライフタイムに応じた速度減衰
    if (gSettings.enableLifetimeVelocityDamping)
    {
        float lifeRatio = p.currentTime / p.lifeTime;
            
        if (lifeRatio >= gSettings.lifetimeVelocityDampingStart)
        {
            float dampingProgress = (lifeRatio - gSettings.lifetimeVelocityDampingStart) /
                                       (1.0f - gSettings.lifetimeVelocityDampingStart);
            float dampingMultiplier = 1.0f - (dampingProgress * dampingProgress);
            p.velocity *= dampingMultiplier;
        }
    }
        
        // 5. ギャザー処理
    if (gSettings.enableGather)
    {
        bool isTrail = (p.isTrailParticle != 0);
            
        if (!isTrail || gSettings.enableGatherForTrail)
        {
            float lifeRatio = p.currentTime / p.lifeTime;
        
            if (lifeRatio >= gSettings.gatherStartRatio)
            {
                float3 targetPosition = gSettings.gatherTarget;
                float3 toTarget = targetPosition - p.translate;
                float distance = length(toTarget);

                if (distance > 0.01f)
                {
                    float3 dirToTarget = normalize(toTarget);
                    float t = (lifeRatio - gSettings.gatherStartRatio) / (1.0f - gSettings.gatherStartRatio);
                    t = t * t;
                        
                    float3 desiredVelocity = dirToTarget * gSettings.gatherStrength;
                    float lerpFactor = 5.0f * gPerFrame.deltaTime;
                
                    p.velocity = lerp(p.velocity, desiredVelocity, lerpFactor * t * 10.0f);
                }
            }
        }
    }

        // 6. 渦巻き（Vortex）処理
    if (gSettings.enableVortex)
    {
        bool isTrail = (p.isTrailParticle != 0);
            
        if (!isTrail || gSettings.enableVortexForTrail)
        {
            float3 center = gSettings.vortexTarget;
            float3 toParticle = p.translate - center;
            float dist = length(toParticle);
                
            if (dist > 0.05f)
            {
                float3 axis = gSettings.vortexAxis;
                if (length(axis) < 0.001f)
                    axis = float3(0, 1, 0);
                else
                    axis = normalize(axis);

                float3 tangent = cross(normalize(toParticle), axis);
                p.velocity += tangent * gSettings.vortexStrength * gPerFrame.deltaTime;
            }
        }
    }
        
        // 7. タービュランス（per-particle ランダム振動力）
    if (gSettings.enableTurbulence)
    {
        float fi = float(particleIndex);
        float px = frac(sin(fi * 127.1f) * 43758.5f) * 6.28318f;
        float py = frac(sin(fi * 311.7f) * 43758.5f) * 6.28318f;
        float pz = frac(sin(fi * 74.7f) * 43758.5f) * 6.28318f;
        float t = gPerFrame.time * gSettings.turbulenceFrequency;
        float3 turbForce = float3(
            sin(t + px),
            cos(t + py),
            sin(t + pz + 1.0471f) // 位相60°ずらして3軸をデコリレート
        ) * gSettings.turbulenceStrength;
        p.velocity += turbForce * gPerFrame.deltaTime;
    }

        // 7.1 音声振動（音の“立ち上がり”でバンっと揺らす）
        //   各粒子は「自分固有のランダム方向」へ、高周波 sin 振動で震える。
        //   揺れ幅 = 立ち上がりエンベロープ(audioAmplitude) × 感度 × 振動の大きさ。
        //   ★audioAmplitude は CPU が onset(音量の増加分)で跳ね上げ時間で減衰させた値。
        //     → 波形が大きくなった瞬間にバンっと強く震え、その後スッと落ち着く（＝振動っぽい）。
        //   ★pow(・, audioAttackSharpness) で「大きい音だけドンと・小さい音は無視」を作る。
        //   ★sin は反転するので velocity は発散しない（飛んでいかない）。
        //   ★方向・位相・周波数を粒子ごとに散らすので「全体が同じ方向」にならずバラバラに動く。
    if (gSettings.enableAudioVibration)
    {
        float fi = float(particleIndex);
        // per-particle 固定ランダム方向（決定論的・毎フレーム不変）
        float3 dir = float3(
            frac(sin(fi * 12.9898f) * 43758.5453f) * 2.0f - 1.0f,
            frac(sin(fi * 78.2330f) * 43758.5453f) * 2.0f - 1.0f,
            frac(sin(fi * 37.7190f) * 43758.5453f) * 2.0f - 1.0f);
        float dlen = length(dir);
        if (dlen > 0.0001f)
        {
            dir /= dlen;
            // per-particle 位相・周波数（organic に散らす）。基準周波数を粒子ごとに ±20% ジッタ。
            float phase = frac(sin(fi * 45.13f) * 43758.5453f) * 6.28318f;
            float freqJitter = 0.8f + frac(sin(fi * 98.71f) * 43758.5453f) * 0.4f;
            float freq = max(gSettings.audioVibrationFrequency, 0.0f) * freqJitter;
            float osc = sin(gPerFrame.time * freq + phase); // [-1,1] 高周波＝細かく震える
            // 立ち上がりエンベロープに感度を掛け、反応カーブ(指数)で「大きい音だけドンと」に整形
            float env = saturate(gSettings.audioAmplitude * gSettings.audioVibrationSensitivity);
            env = pow(env, max(gSettings.audioAttackSharpness, 0.0001f));
            float vib = osc * env * gSettings.audioVibrationStrength;
            p.velocity += dir * vib * gPerFrame.deltaTime;
        }
    }

        // 8. Curl Noise による速度場
    if (gSettings.enableCurlNoise)
    {
        bool isTrail = (p.isTrailParticle != 0);
        if (!isTrail)
        {
            float3 pos = p.translate;
                
                // --------------------------------------------------
                // [問題2対策] パーティクル固有のランダムオフセットを
                // CurlNoise サンプリング座標に加算する。
                // エミッタが小さく全員がほぼ同じ位置から生まれる場合でも、
                // 各パーティクルが異なるノイズフィールドをサンプリングするため
                // バラバラの方向に動くようになる。
                // curlNoisePosRandomStrength = 0 のとき従来と同じ動作。
                // --------------------------------------------------
            if (gSettings.curlNoisePosRandomStrength > 0.0f)
            {
                    // particleIndex を元にした決定論的オフセット
                    // 毎フレーム変わらず、パーティクルごとに固有な値になる
                float fi = float(particleIndex);
                float3 idOffset = float3(
                        frac(sin(fi * 127.1f) * 43758.5f),
                        frac(sin(fi * 311.7f) * 43758.5f),
                        frac(sin(fi * 74.7f) * 43758.5f)
                    ) * 2.0f - 1.0f; // [-1, 1] に正規化
                    
                pos += idOffset * gSettings.curlNoisePosRandomStrength;
            }
                
                // Curl Noise 速度場を計算
            float3 curlVel = ComputeCurlNoise(
                    pos,
                    gSettings.curlNoiseScale,
                    gPerFrame.time * gSettings.curlNoiseTimeScale,
                    (int) gSettings.curlNoiseOctaves
                ) * gSettings.curlNoiseStrength;
                
                // 引き戻し力（Attract）
                // curlNoiseAttractCenter はエミッター座標 + オフセットを
                // C++側で毎フレーム合算して渡す。
                // これによりエミッターが動いても自動追従する。
            if (gSettings.curlNoiseAttractStrength > 0.0f)
            {
                float3 attractTarget = gSettings.curlNoiseAttractCenter;
                float3 toCenter = attractTarget - p.translate;
                float dist = length(toCenter);
                if (dist > 0.001f)
                {
                    float3 attractVel = normalize(toCenter) * dist * gSettings.curlNoiseAttractStrength;
                    curlVel += attractVel;
                }
            }
                
                // --------------------------------------------------
                // [問題1対策] curlNoiseBlendMode によって挙動を切り替え
                //
                //   0 (Replace) : velocity を完全置き換え（従来通り）
                //                 流体的挙動。Gather/Vortex は無効化される。
                //
                //   1 (Add)     : velocity に加算
                //                 Gather/Vortex 等の速度を保持したまま
                //                 Curl Noise の流れが上乗せされる。
                //                 ※加算なので加速しすぎる場合は
                //                   velocityDamping か curlNoiseStrength を
                //                   小さめに設定することを推奨。
                // --------------------------------------------------
            if (gSettings.curlNoiseBlendMode == 0)
            {
                    // 完全置き換え（従来動作）
                p.velocity = curlVel;
            }
            else
            {
                    // 加算（Gather/Vortex等と共存）
                p.velocity += curlVel * gPerFrame.deltaTime;
            }
        }
    }

        // =============================================
        // 7.5. フィールド処理
        // gFieldCB.fieldCount が 0 のとき（フィールドなし or
        // Emitter側で receiveFields_=false のとき）はスキップされる
        // =============================================
    uint fieldForceTrail = 0;
    float fieldTrailDistOverride = 0.0f;
    float4 fieldColorMultiplier = float4(1, 1, 1, 1);
    float fieldSizeMultiplier = 1.0f;
    if (fieldsOn)
    {
        FieldEffectResult fieldResult = ApplyFields(p, (uint) particleIndex, gPerFrame.deltaTime);
        fieldColorMultiplier = fieldResult.colorMultiplier;
        fieldSizeMultiplier = fieldResult.sizeMultiplier;
        fieldForceTrail = fieldResult.forceTrail;
        fieldTrailDistOverride = fieldResult.trailDistOverride;
    }

        // 9. 移動更新
    p.translate += p.velocity * gPerFrame.deltaTime;
    p.currentTime += gPerFrame.deltaTime;
    float3 currentPosition = p.translate;

        // 10. 各種パラメータ更新
    float lifeRatio = p.currentTime / p.lifeTime;

    // フィールドの色上書き(OB_Color)を受けた粒子は RGB を固定し、アルファのフェードだけ続ける。
    // （旧実装は色上書き直後にここで毎フレーム再計算されて即座に消えていた）
    const bool colorLocked = fieldsOn &&
                             ((p.settingsOverrideFlags.x & (1u << OB_Color)) != 0u);

    if (gSettings.enableColorGradient)
    {
        // N段カラーグラデーション: CPUがベイクした256段LUTをlifeRatioでサンプル（start/mid/end/random を上書き）
        uint gi = (uint) (saturate(lifeRatio) * 255.0f + 0.5f);
        float4 gradColor = UnpackColorRGBA8(gSettings.colorLUT[gi >> 2][gi & 3]);
        if (colorLocked)
            p.color.a = gradColor.a;
        else
            p.color = gradColor;
    }
    else if (!gSettings.enableRandomColor)
    {
        float4 lerpedColor;
        if (gSettings.enableMidColor)
        {
            // 3-stop gradient: start → mid → end
            float r = saturate(gSettings.midColorRatio);
            if (lifeRatio < r)
            {
                float t = (r > 0.001f) ? (lifeRatio / r) : 0.0f;
                lerpedColor = lerp(gSettings.startColor, gSettings.midColor, t);
            }
            else
            {
                float span = 1.0f - r;
                float t = (span > 0.001f) ? ((lifeRatio - r) / span) : 1.0f;
                lerpedColor = lerp(gSettings.midColor, gSettings.endColor, t);
            }
        }
        else
        {
            lerpedColor = lerp(gSettings.startColor, gSettings.endColor, lifeRatio);
        }
        if (colorLocked)
            p.color.a = saturate(lerpedColor.a);
        else
            p.color = float4(lerpedColor.rgb, saturate(lerpedColor.a));
    }
    else
    {
        // ランダムカラーモード: RGB はランダム（Emit時に設定済み）。
        // アルファのみ startColor.a→endColor.a で補間する。
        p.color.a = saturate(lerp(gSettings.startColor.a, gSettings.endColor.a, lifeRatio));
    }
        
        // スケール更新: 3つのifを排除して1回の乗算にまとめる
        // enableLifetimeScale=0 かつ enableSinScale=0 なら両方1.0fになるので無変化
        {
        float lifetimeMul = gSettings.enableLifetimeScale ? (1.0f - lifeRatio) : 1.0f;
        float sinMul = 1.0f;
        if (gSettings.enableSinScale)
        {
            float sinWave = sin(p.currentTime * gSettings.sinScaleFrequency) * 0.5f + 0.5f;
            sinMul = 1.0f + (sinWave * 2.0f - 1.0f) * gSettings.sinScaleAmplitude;
        }
        if (gSettings.enableEndScale)
        {
            // initialScale→endScaleValue を lifeRatio で補間（per-particle endScale は廃止し設定値を直読み）
            p.scale = lerp(p.initialScale, gSettings.endScaleValue, lifeRatio) * sinMul;
        }
        else if (gSettings.enableLifetimeScale || gSettings.enableSinScale)
        {
            p.scale = p.initialScale * lifetimeMul * sinMul;
        }
    }

        // 寿命カーブ: サイズ/アルファに倍率カーブを乗算（256段LUTをlifeRatioでサンプル）
    if (gSettings.enableSizeCurve)
    {
        uint si = (uint) (saturate(lifeRatio) * 255.0f + 0.5f);
        p.scale *= gSettings.sizeCurveLUT[si >> 2][si & 3];
    }
    if (gSettings.enableAlphaCurve)
    {
        uint ai = (uint) (saturate(lifeRatio) * 255.0f + 0.5f);
        p.color.a *= gSettings.alphaCurveLUT[ai >> 2][ai & 3];
    }

        // 回転更新
    p.rotation += p.angularVelocity * gPerFrame.deltaTime;
        
        // --- フィールドによる大きさの倍率 (大きさ更新の後に適用) ---
    //   毎フレーム大きさを計算し直す設定なら掛けるだけでよい。
    //   計算し直さない設定（大きさ固定）では積み重ならないよう、元の大きさから掛け直す。
    if (fieldsOn)
    {
        const bool scaleRecomputed = gSettings.enableEndScale || gSettings.enableLifetimeScale || gSettings.enableSinScale;
        if (scaleRecomputed)
            p.scale *= fieldSizeMultiplier;
        else if (!gSettings.enableSizeCurve)
            p.scale = p.initialScale * fieldSizeMultiplier;
    }

        // --- フィールドによるカラー乗算 (色更新の後に適用) ---
    if (fieldColorMultiplier.r != 1.0f ||
            fieldColorMultiplier.g != 1.0f ||
            fieldColorMultiplier.b != 1.0f ||
            fieldColorMultiplier.a != 1.0f)
    {
        float savedAlpha = p.color.a;
        p.color *= fieldColorMultiplier;
            // アルファは startColor.a→endColor.a 補間由来の値を保持しつつフィールド乗算を適用する
        p.color.a = savedAlpha * fieldColorMultiplier.a;
    }
        
        // 11. トレイル生成
        //   通常トレイル（設定ON） または フィールド強制トレイル のどちらかが有効ならば生成する。
        //   フィールド強制トレイルは isTrailParticle==0 のパーティクルにのみ適用する。
    bool doTrail = (gSettings.enableTrail != 0) || (fieldForceTrail != 0);
    if (doTrail &&
            p.isTrailParticle == 0 &&
            p.color.a > 0.05f)
    {
            // フィールドによる距離オーバーライドを一時適用
        float savedDist = p.trailSpawnDistance;
        if (fieldForceTrail != 0 && fieldTrailDistOverride > 0.0f)
        {
            p.trailSpawnDistance = fieldTrailDistOverride;
        }
        else if (fieldForceTrail != 0 && p.trailSpawnDistance <= 0.0f)
        {
                // gSettings.trailSpawnDistance を借りる（0 なら固定値）
            p.trailSpawnDistance =
                    (gSettings.trailSpawnDistance > 0.0f) ? gSettings.trailSpawnDistance : 0.3f;
        }

            // 親はローカル p で完結（往復書き戻し不要）。子スロットのみ内部で直接書き込む。
        SpawnTrailParticles(p, particleIndex, currentPosition);

            // 元の距離設定を戻す（通常トレイルが有効なときは書き換えない）
        if (fieldForceTrail != 0 && gSettings.enableTrail == 0)
        {
            p.trailSpawnDistance = savedDist;
        }
    }
        
        // 死亡判定
        // color.a による判定から変更: startColor.a=0（完全透明スタート）を
        // 設定した場合でも即死しないよう、寿命切れ（currentTime >= lifeTime）で判定する。
    if (p.currentTime >= p.lifeTime)
    {
        p.scale = float3(0.0f, 0.0f, 0.0f);
        p.lastTrailPosition = float3(0.0f, 0.0f, 0.0f);
        // フリーリストに戻す前に lifeTime を 0 にリセットし、
        // 早期リターン条件（lifeTime <= 0）で未使用スロットと判別できるようにする
        p.lifeTime = 0.0f;

        // Wave 集約: 同 Wave 内の死亡レーンをまとめて freeList tail を 1 回だけ進める。
        // 高密度では毎フレームの死亡数も多く、単一 tail カウンタへの atomic 競合を削減できる。
        // freeList は順不同の空きプールなので、Wave 内での割り当て順が変わっても問題ない。
        uint dyingInWave = WaveActiveCountBits(true);
        uint dyingOffset = WavePrefixCountBits(true);
        int tailBase = 0;
        if (WaveIsFirstLane())
        {
            InterlockedAdd(gFreeListTailIndex[0], (int) dyingInWave, tailBase);
        }
        tailBase = WaveReadLaneFirst(tailBase);

        int slot = (tailBase + (int) dyingOffset) % gSettings.maxParticleCount;
        gFreeList[slot] = particleIndex;
    }

    // =============================================
    // SoA 書き戻し: 使った機能のバッファだけ store する。
    // =============================================
    gLife[particleIndex] = p.lifeTime;

    PDrawCore odc;
    odc.translate = p.translate;
    odc.scaleXY = PackScaleXY(p.scale);
    // フリップブックのコマ番号を空いている上位16bitへ同梱する（無効なら0のまま）
    odc.scaleZ = PackScaleZFrame(
                p.scale, ComputeParticleWord(gSettings.enableFlipbook, gSettings.flipbookCols,
                                             gSettings.flipbookRows, gSettings.flipbookMode,
                                             gSettings.flipbookFps, gSettings.flipbookRandomStart,
                                             lifeRatio, gPerFrame.time, particleIndex));
    odc.velocity = p.velocity;
    odc.color = PackColorRGBA8(p.color);
    gDrawCore[particleIndex] = odc;

    PSimCore osc;
    osc.currentTime = p.currentTime;
    osc.initialScaleXY = PackScaleXY(p.initialScale);
    osc.initialScaleZ_isTrail = PackScaleZTrail(p.initialScale, p.isTrailParticle);
    gSimCore[particleIndex] = osc;

    if (useTrail)
    {
        PTrail otr;
        otr.parentIndex = p.parentIndex;
        otr.lastTrailPosition = p.lastTrailPosition;
        otr.trailSpawnDistance = p.trailSpawnDistance;
        gTrail[particleIndex] = otr;
    }

    if (useRotation)
    {
        PRotation orot;
        orot.rotation = p.rotation;
        orot.angularVelocity = p.angularVelocity;
        gRotation[particleIndex] = orot;
    }

    if (useOverride)
        gOverride[particleIndex] = p.settingsOverrideFlags;

    // =============================================
    // 生存コンパクション
    //   このフレームの更新後も生存しているパーティクルだけを
    //   aliveList の先頭から詰めて書き出す。
    //   描画側は aliveCounter 個だけ instance を発行すればよく、
    //   死亡スロットへの無駄な VS 起動（オーバードロー要因）を排除する。
    //   ※ このフレームに新規生成されたトレイル子は SpawnTrailParticles 内で
    //     out リスト/renderCompact へ即 append 済み（今フレームから描画、次フレームから sim）。
    // =============================================
    //   高速化: 生存スレッドが各自 InterlockedAdd すると高密度時に単一カウンタへ
    //   数万回の atomic が殺到し直列化する。Wave 内で生存数をまとめ、先頭レーンが
    //   1 回だけ加算 → WavePrefixCountBits で各レーンのオフセットを得ることで
    //   atomic 回数を 1/WaveSize に削減する（SM6.0 / cs_6_0）。
    bool alive = IsAliveParticle(p);
    uint aliveInWave = WaveActiveCountBits(alive);
    if (aliveInWave > 0)
    {
        uint laneOffset = WavePrefixCountBits(alive);
        uint waveBase = 0;
        if (WaveIsFirstLane())
        {
            InterlockedAdd(gAliveCounter[0], aliveInWave, waveBase);
        }
        waveBase = WaveReadLaneFirst(waveBase);
        if (alive)
        {
            gAliveList[waveBase + laneOffset] = (uint) particleIndex;
        }
    }

    // =============================================
    // 描画コンパクション（GPU駆動の視錐台カリング）
    //   生存かつ視錐台に入っている粒子だけを描画リストへ詰める。詰めた数が
    //   ExecuteIndirect の instanceCount になるので、画面外の粒子は VS すら起動しない。
    //   生存リストとは別カウンタ＝別順序になるため、回転グループ用に実 slot も並べて書く。
    // =============================================
    bool visible = alive &&
                   ((gSettings.enableFrustumCull == 0) ||
                    IsSphereInFrustum(gSettings.frustumPlanes, p.translate,
                                      ParticleCullRadius(p.scale, p.velocity,
                                                         gSettings.frustumRadiusScale,
                                                         gSettings.frustumStretchFactor)));
    uint visibleInWave = WaveActiveCountBits(visible);
    if (visibleInWave > 0)
    {
        uint visLaneOffset = WavePrefixCountBits(visible);
        uint visWaveBase = 0;
        if (WaveIsFirstLane())
        {
            InterlockedAdd(gVisibleCounter[0], visibleInWave, visWaveBase);
        }
        visWaveBase = WaveReadLaneFirst(visWaveBase);
        if (visible)
        {
            uint vdst = visWaveBase + visLaneOffset;
            // 描画データを詰めた順に書き出す（odc は上の DrawCore 書き戻しで構築済み）。
            // 描画VSは gRenderCompact[instanceId] を順次読みできる。
            gRenderCompact[vdst] = odc;
            // 実 slot は回転グループだけが必要（VS の enableRotation と同条件・定数分岐）。
            if (useRotation)
                gRenderSlot[vdst] = (uint) particleIndex;
        }
    }
}