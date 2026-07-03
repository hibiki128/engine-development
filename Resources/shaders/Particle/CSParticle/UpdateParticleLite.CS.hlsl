#include "../Particle.hlsli"

// =============================================
// UpdateParticle 軽量バリアント（§6 D: 演出なし専用）
//
//   フル版 UpdateParticle.CS.hlsl は単一巨大カーネルで全演出コード
//   (curl/vortex/gather/turbulence/field/trail/rotation/override) を抱え、
//   レジスタ過多 → 低オキュパンシ → DRAM レイテンシを隠せない疑いがある。
//
//   このバリアントは「重い演出が全 OFF かつフィールドの影響を受けない」
//   グループ専用に、それらをコードごと削った軽量カーネル。
//   レジスタ激減 → オキュパンシ上昇 → レイテンシ隠蔽を狙う。
//
//   ★保持する処理（いずれも追加バッファ load を伴わない安価な算術）:
//     加速度 / 重力 / 速度減衰 / 寿命速度減衰 / 移動 / 色補間 / スケール /
//     死亡判定+フリーリスト返却 / SoA store / 生存・描画コンパクション
//   ★削った処理（フル版のみ）:
//     curlNoise / vortex / gather / turbulence / フィールド全般 /
//     トレイル生成 / 回転 / 設定上書き
//
//   触る SoA バッファは Life(u0)/DrawCore(u1)/SimCore(u2) のみ
//   （Trail/Rotation/Override は load も store もしない）。
//   ルートシグネチャはフル版 UpdateEmitter と共有（本シェーダは登録レジスタの
//   部分集合なので合法）。グループの設定で PSO を振り分ける。
//
//   ※色/スケール/移動の数式はフル版と**完全一致**させること。
//     （lite 適格グループはフル版でも全演出ブランチが OFF になり同一結果になる。
//      数式がずれると lite⇔full 切替時に見た目が飛ぶ）
// =============================================

ConstantBuffer<PerFrame> gPerFrame : register(b0);
ConstantBuffer<ParticleCSSettings> gSettings : register(b1);
// 触る SoA バッファは DrawCore/SimCore のみ（Trail/Rotation/Override は不要）。
RWStructuredBuffer<float>     gLife     : register(u0);
RWStructuredBuffer<PDrawCore> gDrawCore : register(u1);
RWStructuredBuffer<PSimCore>  gSimCore  : register(u2);
// フリーリスト（死亡返却に tail/本体のみ使用。head(u6) はトレイル生成専用なので不要）
RWStructuredBuffer<uint> gFreeList          : register(u7);
RWStructuredBuffer<int>  gFreeListTailIndex : register(u8);
// 生存コンパクション（u9-u10）
RWStructuredBuffer<uint> gAliveList    : register(u9);
RWStructuredBuffer<uint> gAliveCounter : register(u10);
// 描画コンパクション（u11）
RWStructuredBuffer<PDrawCore> gRenderCompact : register(u11);
// 生存リスト間接ディスパッチ（§8）: 前フレームの out リスト = 今フレームの in（処理対象）。
//   軽量版はトレイルを持たないため append は survivor のみ（下のグループ集約コンパクション）。
StructuredBuffer<uint> gAliveListIn    : register(t2); // in: 処理対象 slot index 列
StructuredBuffer<uint> gAliveCounterIn : register(t3); // in: リスト長

// グループ単位コンパクション用の groupshared（アトミック競合削減）。
//   生存粒子の追記先 gAliveCounter[0] への atomic を「ワープに1回」から「グループに1回」へ集約し、
//   単一アドレスへの atomic 殺到（35M で約110万回/フレーム）を 1/(グループ内ワープ数) に削減する。
groupshared uint sGroupAliveCount; // グループ内の生存数（LDS で集計）
groupshared uint sGroupBase;       // gAliveCounter から取得したグループのグローバル基点

// スレッドグループ=256（フル版1024と異なる）。
//   Ampere(GA106/RTX3060)は1SM常駐1536スレッド上限のため、1024ブロックだと
//   2ブロック目(2048)が乗らず占有率67%で頭打ち。軽量版はレジスタが少ないので
//   256に割れば最大6ブロック=1536常駐でき占有率を上げられる（レイテンシ隠蔽強化）。
//   ★C++側 ParticleCSGroup::kLiteUpdateThreadsPerGroup と必ず一致させること。
//
//   ※ 生存コンパクションで groupshared バリアを使うため**早期 return できない**
//     （全スレッドが GroupMemoryBarrierWithGroupSync に到達する必要がある）。
//     死亡/OOB スロットは valid/alive フラグで分岐して処理を飛ばし、合流して
//     コンパクションのバリアへ到達させる。
[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    // 生存リスト間接ディスパッチ（§8）: 全スロット走査ではなく in リストの tid 番目を処理する。
    //   早期 return 不可（グループ集約バリアのため全スレッドが到達する必要がある）。
    //   tid>=リスト長 / dead / OOB は alive=false のまま下のコンパクションへ合流する。
    uint tid = DTid.x;
    int particleIndex = -1;

    bool alive = false;
    PDrawCore odc = (PDrawCore) 0;

    if (tid < gAliveCounterIn[0])
    {
        particleIndex = (int) gAliveListIn[tid];
        // Life(4B) だけ先読みし、死亡/未使用スロット(lifeTime<=0)は処理を飛ばす（帯域削減点）。
        float lifeTime = (particleIndex >= 0 && particleIndex < (int) gSettings.maxParticleCount)
                             ? gLife[particleIndex]
                             : 0.0f;
        if (lifeTime > 0.0f)
        {
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
            p.isTrailParticle = sc.initialScaleZ_isTrail >> 16; // store でそのまま保存する

            // 1. 加速度
            if (gSettings.enableAcceleration)
                p.velocity += gSettings.acceleration * gPerFrame.deltaTime;

            // 2. 重力
            if (gSettings.enableGravity)
                p.velocity += gSettings.gravity * gPerFrame.deltaTime;

            // 3. 速度減衰
            if (gSettings.enableVelocityDamping)
                p.velocity *= pow(gSettings.velocityDampingFactor, gPerFrame.deltaTime * 60.0f);

            // 4. 寿命による速度減衰
            if (gSettings.enableLifetimeVelocityDamping)
            {
                float lr = p.currentTime / p.lifeTime;
                if (lr >= gSettings.lifetimeVelocityDampingStart)
                {
                    float dampingProgress = (lr - gSettings.lifetimeVelocityDampingStart) /
                                            (1.0f - gSettings.lifetimeVelocityDampingStart);
                    float dampingMultiplier = 1.0f - (dampingProgress * dampingProgress);
                    p.velocity *= dampingMultiplier;
                }
            }

            // 5. 移動更新
            p.translate += p.velocity * gPerFrame.deltaTime;
            p.currentTime += gPerFrame.deltaTime;

            // 6. 色更新（フル版と同一: グラデーションLUT / 3-stop gradient / randomColor アルファ補間）
            float lifeRatio = p.currentTime / p.lifeTime;
            if (gSettings.enableColorGradient)
            {
                uint gi = (uint) (saturate(lifeRatio) * 255.0f + 0.5f);
                p.color = UnpackColorRGBA8(gSettings.colorLUT[gi >> 2][gi & 3]);
            }
            else if (!gSettings.enableRandomColor)
            {
                float4 lerpedColor;
                if (gSettings.enableMidColor)
                {
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
                p.color.a = saturate(lerp(gSettings.startColor.a, gSettings.endColor.a, lifeRatio));
            }

            // 7. スケール更新（フル版と同一）
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
                    p.scale = lerp(p.initialScale, gSettings.endScaleValue, lifeRatio) * sinMul;
                }
                else if (gSettings.enableLifetimeScale || gSettings.enableSinScale)
                {
                    p.scale = p.initialScale * lifetimeMul * sinMul;
                }
            }

            // 寿命カーブ: サイズ/アルファに倍率カーブを乗算（フル版と同一）
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

            // 8. 死亡判定（寿命切れで freeList へ返却。死亡は低頻度なので従来どおり Wave 集約のまま）
            if (p.currentTime >= p.lifeTime)
            {
                p.scale = float3(0.0f, 0.0f, 0.0f);
                p.lifeTime = 0.0f;

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

            // SoA 書き戻し（Life/DrawCore/SimCore のみ。Trail/Rotation/Override は触らない）
            gLife[particleIndex] = p.lifeTime;

            odc.translate = p.translate;
            odc.scaleXY = PackScaleXY(p.scale);
            odc.scaleZ = PackScaleZ(p.scale);
            odc.velocity = p.velocity;
            odc.color = PackColorRGBA8(p.color);
            gDrawCore[particleIndex] = odc;

            PSimCore osc;
            osc.currentTime = p.currentTime;
            osc.initialScaleXY = PackScaleXY(p.initialScale);
            osc.initialScaleZ_isTrail = PackScaleZTrail(p.initialScale, p.isTrailParticle);
            gSimCore[particleIndex] = osc;

            alive = IsAliveParticle(p);
        }
    }

    // ---- 生存コンパクション + 描画コンパクション（グループ単位集約）----
    //   gAliveCounter[0] への atomic を「グループに1回」へ集約して単一アドレス競合を削減する。
    //     1) 各ワープが自ワープの生存数を LDS カウンタ sGroupAliveCount へ1回だけ加算（ワープ集約）
    //     2) グループ先頭スレッドが1回だけ gAliveCounter[0] へ加算し基点を取得
    //     3) 各生存レーンは base + ワープ内基点 + レーン内オフセット へ詰めて書き出す
    if (groupIndex == 0)
        sGroupAliveCount = 0;
    GroupMemoryBarrierWithGroupSync();

    uint aliveInWave = WaveActiveCountBits(alive);
    uint laneOffset = WavePrefixCountBits(alive);
    uint warpBase = 0;
    if (WaveIsFirstLane() && aliveInWave > 0)
        InterlockedAdd(sGroupAliveCount, aliveInWave, warpBase); // LDS atomic（ワープに1回）
    warpBase = WaveReadLaneFirst(warpBase);
    GroupMemoryBarrierWithGroupSync();

    if (groupIndex == 0)
    {
        uint base = 0;
        if (sGroupAliveCount > 0)
            InterlockedAdd(gAliveCounter[0], sGroupAliveCount, base); // グローバルは「グループに1回」
        sGroupBase = base;
    }
    GroupMemoryBarrierWithGroupSync();

    if (alive)
    {
        uint dst = sGroupBase + warpBase + laneOffset;
        gAliveList[dst] = (uint) particleIndex;
        gRenderCompact[dst] = odc;
    }
}
