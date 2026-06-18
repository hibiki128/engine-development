#include "../Particle.hlsli"
#include "../../Random/Random.hlsli"
#include "CurlNoise.hlsli"

ConstantBuffer<PerFrame> gPerFrame : register(b0);
ConstantBuffer<ParticleCSSettings> gSettings : register(b1);
ConstantBuffer<FieldCountCB> gFieldCB : register(b2);
RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<int> gFreeListIndex : register(u1);
RWStructuredBuffer<uint> gFreeList : register(u2);
RWStructuredBuffer<int> gFreeListTailIndex : register(u3);
// 生存コンパクション用: 生存パーティクルの slot index を詰めて書き出す
RWStructuredBuffer<uint> gAliveList : register(u4);
// 生存コンパクション用: append 位置を返すアトミックカウンタ (各フレーム先頭で 0 にリセット)
RWStructuredBuffer<uint> gAliveCounter : register(u5);
StructuredBuffer<ParticleField> gFields : register(t0);
StructuredBuffer<ParticleFieldSettingsOverrideData> gFieldsOverride : register(t1);

// =============================================
// フィールド適用結果
// =============================================
struct FieldEffectResult
{
    float3 velocity; // 更新後の速度
    float lifeTimeDrain; // 今フレームで削る寿命量の合計
    uint forceTrail; // 1ならトレイル強制生成
    float trailDistOverride; // >0 なら trailSpawnDistance を上書き
    float4 colorMultiplier; // 色乗算 (白=変化なし)
};

// =============================================
// フィールド適用関数
// =============================================
FieldEffectResult ApplyFields(float3 velocity, float3 particlePos, float deltaTime)
{
    FieldEffectResult result;
    result.velocity = velocity;
    result.lifeTimeDrain = 0.0f;
    result.forceTrail = 0;
    result.trailDistOverride = 0.0f;
    result.colorMultiplier = float4(1, 1, 1, 1);

    for (uint fi = 0; fi < gFieldCB.fieldCount; fi++)
    {
        ParticleField f = gFields[fi];

        // groupIdフィルタリング
        // フィールドのgroupId==-1 → 全エミッターに影響
        // エミッターのemitterFieldGroupId==-1 → 全フィールドから影響受ける
        // それ以外は両者が一致するときのみ処理する
        bool groupMatch = (f.groupId == -1) ||
                          (gPerFrame.emitterFieldGroupId == -1) ||
                          (f.groupId == gPerFrame.emitterFieldGroupId);
        if (!groupMatch)
            continue;

        float3 toParticle = particlePos - f.position;
        float distSq = dot(toParticle, toParticle);
        float radiusSq = f.radius * f.radius;

        // 影響範囲外はスキップ（sqrt不要）
        if (distSq >= radiusSq)
            continue;

        float dist = sqrt(distSq); // 範囲内確定後のみsqrt
        // 減衰係数 (端=0, 中心=1)
        float t = 1.0f - saturate(dist / f.radius);
        float influence = (f.falloff == 1.0f) ? t : t * t;

        // =============================================
        // 速度系エフェクト
        // =============================================
        if (f.fieldType == 0) // Wind: 一定方向に押す
        {
            // direction はC++側で正規化済みを想定（毎フレームGPUで割り算しない）
            result.velocity += f.direction * f.strength * influence * deltaTime;
        }
        else if (f.fieldType == 1) // Attract: 中心に引き寄せる
        {
            if (dist > 0.001f)
            {
                float3 dir = -toParticle / dist;
                result.velocity += dir * f.strength * influence * deltaTime;
            }
        }
        else if (f.fieldType == 2) // Repel: 中心から押し出す
        {
            if (dist > 0.001f)
            {
                float3 dir = toParticle / dist;
                result.velocity += dir * f.strength * influence * deltaTime;
            }
        }
        else if (f.fieldType == 3) // Vortex: 渦巻き
        {
            if (dist > 0.001f)
            {
                // axis は C++側で正規化済みを想定
                float3 tangent = cross(toParticle / dist, f.direction);
                result.velocity += tangent * f.strength * influence * deltaTime;
            }
        }

        // =============================================
        // 寿命ドレイン
        //   lifeTimeDrain [秒/秒] を influence で重み付けして加算。
        //   複数フィールドが重なると合算される。
        // =============================================
        if (f.enableLifeDrain != 0)
        {
            result.lifeTimeDrain += f.lifeTimeDrain * influence * deltaTime;
        }

        // =============================================
        // トレイル強制生成
        //   フィールド内にいる間、トレイルを強制的にONにする。
        //   最も強い influence を持つフィールドのオーバーライド距離を優先する。
        // =============================================
        if (f.enableForceTrail != 0)
        {
            result.forceTrail = 1;
            // 上書き距離が指定されていれば、より小さい値（高頻度）を採用
            if (f.trailSpawnDistanceOverride > 0.0f)
            {
                if (result.trailDistOverride <= 0.0f)
                    result.trailDistOverride = f.trailSpawnDistanceOverride;
                else
                    result.trailDistOverride = min(result.trailDistOverride, f.trailSpawnDistanceOverride);
            }
        }

        // =============================================
        // カラー乗算
        //   influence で白（変化なし）とターゲット色をブレンド。
        //   複数フィールドは乗算合成される。
        // =============================================
        if (f.enableColorMultiply != 0)
        {
            float4 blendedColor = lerp(float4(1, 1, 1, 1), f.colorMultiplier, influence);
            result.colorMultiplier *= blendedColor;
        }
    }

    return result;
}

// =============================================
// 一度きり設定上書き処理
//   フィールド内に入ったパーティクルへ、まだ上書きされていない
//   ParticleCSSettings 項目を直接書き換える。
//   書き換えた項目は settingsOverrideFlags に記録し、
//   以降の同一フィールドへの再入時は何もしない（一度きり保証）。
//
//   呼び出し元: main() のフィールドループ内
//   引数 particleIndex : 対象パーティクルの配列インデックス
//   引数 fi            : gFieldsOverride の添字（gFields と同一）
// =============================================
// 対象パーティクルはローカル変数 p で読み書きする（global往復を排除）。
// 全て自スロットへの操作なので安全かつメモリ往復が無くなる。
void ApplySettingsOverride(inout Particle p, uint fi)
{
    ParticleFieldSettingsOverrideData ov = gFieldsOverride[fi];
    // overrideMask が 0 なら何もしない（高速パス）
    if (ov.overrideMask.x == 0u && ov.overrideMask.y == 0u)
        return;

    // まだ上書きされていないビットだけ処理する
    // = overrideMask & ~settingsOverrideFlags
    uint2 pFlags = p.settingsOverrideFlags;
    uint2 pending;
    pending.x = ov.overrideMask.x & ~pFlags.x;
    pending.y = ov.overrideMask.y & ~pFlags.y;

    if (pending.x == 0u && pending.y == 0u)
        return; // 全項目上書き済み

    // --- 各項目を pending ビットで個別チェック ---
    // bit0-31 (pending.x)
    if (pending.x & (1u << OB_LifeTimeMin))
        p.lifeTime = ov.lifeTimeMin; // lifeTime を直接上書き

    if (pending.x & (1u << OB_LifeTimeMax))
    {
        // lifeTimeMax はパーティクルの寿命上限をリセット
        // currentTime もリセットして新しい寿命で動かしたい場合は 0 にする
        // ここでは lifeTime を lifeTimeMax で上書きするだけにして
        // currentTime はリセットしない（残り寿命が縮まる使い方を自然に実現）
        p.lifeTime = ov.lifeTimeMax;
    }

    if (pending.x & (1u << OB_ScaleMin))
    {
        // 現在のスケールを scaleMin 値で再設定
        float3 newScale = float3(ov.scaleMin, ov.scaleMin, ov.scaleMin);
        p.initialScale = newScale;
        p.scale = newScale;
    }

    if (pending.x & (1u << OB_ScaleMax))
    {
        float3 newScale = float3(ov.scaleMax, ov.scaleMax, ov.scaleMax);
        p.initialScale = newScale;
        p.scale = newScale;
    }

    if (pending.x & (1u << OB_VelocityMin))
        p.velocity = max(p.velocity, ov.velocityMin);

    if (pending.x & (1u << OB_VelocityMax))
        p.velocity = min(p.velocity, ov.velocityMax);

    if (pending.x & (1u << OB_StartColor))
        p.color = ov.startColor;

    if (pending.x & (1u << OB_EndColor))
        p.color = ov.endColor;

    // Note: enableXxx 系のフラグはパーティクル自体には持たせず
    //       gSettings (ConstantBuffer) 側の話なので、パーティクル単体での
    //       enable 制御は「このフレームからトレイル起動」等の
    //       trailSpawnDistance を直接書き換える形で代替する。

    if (pending.x & (1u << OB_TrailSpawnDistance))
    {
        p.trailSpawnDistance = ov.trailSpawnDistance;
    }

    if (pending.x & (1u << OB_GatherTarget))
    {
        // gatherTarget はグループ設定なので、パーティクル側では
        // 速度を直接 gatherTarget 方向に向け替えることで擬似実現する
        float3 toTarget = ov.gatherTarget - p.translate;
        float dist = length(toTarget);
        if (dist > 0.01f)
            p.velocity = normalize(toTarget) * length(p.velocity);
    }

    // bit32-63 (pending.y) — 加速度・ダンピング・CurlNoise はグループ全体設定なので
    // パーティクル単体では velocity を直接書き換えることで近似する

    if (pending.y & (1u << (OB_Acceleration - 32u)))
    {
        // 加速度を現在 velocity に即時加算（一回だけ）
        p.velocity += ov.acceleration;
    }

    if (pending.y & (1u << (OB_VelocityDampingFactor - 32u)))
    {
        // 一回だけ速度にダンピングを適用
        p.velocity *= ov.velocityDampingFactor;
    }

    // --- 書き換えたビットを記録 ---
    p.settingsOverrideFlags.x |= pending.x;
    p.settingsOverrideFlags.y |= pending.y;
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
    // 滑らかに減衰させる（Phase 5: ロールバックの全件破棄を撤廃）。
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

        gParticles[trailIndex].translate = spawnPosition;
        gParticles[trailIndex].initialScale = parentCurrentScale * gSettings.trailScaleMultiplier;
        gParticles[trailIndex].scale = gParticles[trailIndex].initialScale;

        if (gSettings.trailInheritVelocity != 0)
        {
            gParticles[trailIndex].velocity = parentVelocity * gSettings.trailVelocityScale;
        }
        else
        {
            gParticles[trailIndex].velocity = float3(0.0f, 0.0f, 0.0f);
        }

        gParticles[trailIndex].color = parentColor * gSettings.trailColorMultiplier;
        gParticles[trailIndex].lifeTime = trailLifeTime;
        gParticles[trailIndex].currentTime = 0.0f;
        gParticles[trailIndex].isTrailParticle = 1;
        gParticles[trailIndex].parentIndex = particleIndex;
        gParticles[trailIndex].lastTrailPosition = spawnPosition;
        gParticles[trailIndex].trailSpawnDistance = gSettings.trailSpawnDistance;
    }

    // capped のときは移動区間を全消費して lastTrailPosition を currentPosition まで進め、
    // 追従の遅れを次フレームへ持ち越さないようにする。
    float consumedDistance = capped ? totalDistance : (float(requiredCount) * targetDistance);
    p.lastTrailPosition = lastPos + direction * consumedDistance;
}

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int particleIndex = DTid.x;
    if (particleIndex >= (int) gSettings.maxParticleCount)
        return;

    // =============================================
    // グローバルメモリからローカルにコピー（1回のロードで済ます）
    // 以降はローカル変数を読み書きし、最後に1回だけ書き戻す。
    // p.xxx への散発的アクセスを排除して
    // メモリ帯域を大幅に節約する。
    // =============================================
    Particle p = gParticles[particleIndex];

    // lifeTime <= 0 のスロットは未使用（Init時の初期値）なのでスキップする。
    // color.a による判定から変更: startColor.a=0（完全透明スタート）を
    // 設定した場合でも正常に更新が走るようにするため。
    if (p.lifeTime <= 0.0f)
        return;
        
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
    if (gFieldCB.fieldCount > 0)
    {
        FieldEffectResult fieldResult = ApplyFields(
                p.velocity,
                p.translate,
                gPerFrame.deltaTime);

        p.velocity = fieldResult.velocity;

            // --- 寿命ドレイン ---
            // currentTime を進めることで残り寿命を縮める。
        if (fieldResult.lifeTimeDrain > 0.0f)
        {
            p.currentTime =
                    min(p.currentTime + fieldResult.lifeTimeDrain,
                        p.lifeTime);
        }

            // --- カラー乗算・トレイルフラグは後段で適用 ---
        fieldColorMultiplier = fieldResult.colorMultiplier;
        fieldForceTrail = fieldResult.forceTrail;
        fieldTrailDistOverride = fieldResult.trailDistOverride;

            // =============================================
            // 7.6. 一度きり設定上書き
            //   各フィールドについて、パーティクルが影響範囲内かどうかを
            //   再チェックし、enableSettingsOverride が立っているものだけ処理する。
            //   ApplyFields のループとは独立させているのは、
            //   上書き処理が gParticles を直接書き換えるため
            //   速度処理と混在させると副作用が出る可能性があるため。
            // =============================================
        for (uint fi = 0; fi < gFieldCB.fieldCount; fi++)
        {
            if (gFields[fi].enableSettingsOverride == 0u)
                continue; // 設定上書き機能OFF → 高速スキップ

            float3 toP = p.translate - gFields[fi].position;
            if (dot(toP, toP) >= gFields[fi].radius * gFields[fi].radius)
                continue; // 範囲外

                // ローカル p で完結（往復書き戻し不要）
            ApplySettingsOverride(p, fi);
        }
    }
        
        // 9. 移動更新
    p.translate += p.velocity * gPerFrame.deltaTime;
    p.currentTime += gPerFrame.deltaTime;
    float3 currentPosition = p.translate;

        // 10. 各種パラメータ更新
    float lifeRatio = p.currentTime / p.lifeTime;
        
    if (!gSettings.enableRandomColor)
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
            // initialScale→endScaleValue を lifeRatio で補間（既存の lifetimeMul/sinMul とは独立）
            p.scale = lerp(p.initialScale, p.endScale, lifeRatio) * sinMul;
        }
        else if (gSettings.enableLifetimeScale || gSettings.enableSinScale)
        {
            p.scale = p.initialScale * lifetimeMul * sinMul;
        }
    }
    
        // 回転更新
    p.rotation += p.angularVelocity * gPerFrame.deltaTime;
        
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

    gParticles[particleIndex] = p;

    // =============================================
    // 生存コンパクション
    //   このフレームの更新後も生存しているパーティクルだけを
    //   aliveList の先頭から詰めて書き出す。
    //   描画側は aliveCounter 個だけ instance を発行すればよく、
    //   死亡スロットへの無駄な VS 起動（オーバードロー要因）を排除する。
    //   ※ このフレームに新規生成されたトレイル子は、自身のスレッドが
    //     既に早期 return しているため次フレームから描画対象になる（1F遅延・許容）。
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
}