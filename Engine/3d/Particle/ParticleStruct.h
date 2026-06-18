#pragma once
#include "Graphics/PipeLine/PipeLineManager.h"
#include "type/Matrix4x4.h"
#include "type/Vector3.h"
#include "type/Vector4.h"
#include <Model/ModelStructs.h>
#include <Transform/WorldTransform.h>
#include <cstddef> // offsetof
#include <cstdint>
#include <d3d12.h>
#include <list>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// パーティクルのマテリアル情報（色・UV変換・テクスチャ）
/// </summary>
struct ParticleMaterial {
    Vector4 color;
    Matrix4x4 uvTransform;
    float padding[3];
    std::string textureFilePath;
    uint32_t textureIndex = 0;
};

/// ===== GPUParticle =====

/// <summary>
/// GPUパーティクルの発生源メッシュ情報
/// </summary>
struct EmitterMesh {
    Vector3 translate;
    uint32_t triangleCount;
    Quaternion rotation;
    uint32_t emitFromSurface;
    Vector3 scale;
    float frequency;
    float frequencyTime;
    uint32_t emit;
    uint32_t edgeCount;
    Vector3 anchorPoint;
    float padding;
};

/// <summary>
/// GPU上で管理する1パーティクル分のデータ（HLSL側 Particle と対応）
/// </summary>
struct CSParticle {
    Vector3 translate;
    Vector3 scale;
    float lifeTime;
    Vector3 velocity;
    float currentTime;
    Vector4 color;
    Vector3 initialScale;
    float padding;
    uint32_t isTrailParticle;
    uint32_t parentIndex;
    Vector3 lastTrailPosition;
    float trailSpawnDistance;
    // フィールドによる「一度きり設定上書き」完了フラグ (ビットマスク)
    // HLSL側の Particle::settingsOverrideFlags (uint2) と対応。
    // lo=bit0-31, hi=bit32-63
    uint32_t settingsOverrideFlagsLo = 0;
    uint32_t settingsOverrideFlagsHi = 0;
    // 回転 (XYZ軸回転、ラジアン)
    Vector3 rotation = {0.0f, 0.0f, 0.0f};
    float paddingRot = 0.0f;
    Vector3 angularVelocity = {0.0f, 0.0f, 0.0f};
    float paddingAngVel = 0.0f;
    // 終了スケール
    Vector3 endScale = {0.0f, 0.0f, 0.0f};
    float paddingScale = 0.0f;
};

/// <summary>
/// 描画時にビュー単位で渡す情報（ビュープロジェクション・ビルボード設定など）
/// </summary>
struct PerView {
    Matrix4x4 viewProjection;
    Matrix4x4 billboardMatrix;
    uint32_t enableBillboard = 1;       // 1=ビルボードON(デフォルト), 0=OFF
    uint32_t enableVelocityStretch = 0; // 1=速度方向に引き伸ばす
    float velocityStretchFactor = 0.1f; // 引き伸ばし係数(速さ×係数 = 伸び率)
    // 1=回転あり（VSで回転行列を計算）/ 0=回転なし（VSの sincos×3＋行列積をスキップ）。
    // グループが回転を使わない（enableRandomRotation/enableRandomAngularVelocity が両方OFF）なら
    // 全パーティクルの rotation が常に 0 なので、VS の回転計算を丸ごと省ける。
    uint32_t enableRotation = 0;
};

/// <summary>
/// 発生源メッシュの三角形1枚分の頂点情報
/// </summary>
struct TriangleInfo {
    Vector3 v0;
    float padding0;
    Vector3 v1;
    float padding1;
    Vector3 v2;
    float padding2;
};

/// <summary>
/// 描画時にフレーム単位で渡す情報（時間・グループID など）
/// </summary>
struct PerFrame {
    float time;
    float deltaTime;
    uint32_t groupId;
    int32_t emitterFieldGroupId; // -1=全フィールド対象, 0以上=同IDのフィールドのみ対象
};

/// <summary>
/// エミッターデータ（発生源メッシュを保持）
/// </summary>
struct EmitterData {
    EmitterMesh mesh;
};

/// <summary>
/// メッシュ表面上のサンプル点
/// </summary>
struct SurfacePoint {
    Vector3 position;
    float padding;
};

/// <summary>
/// 発生源メッシュのエッジ1本分の頂点情報
/// </summary>
struct EdgeInfo {
    Vector3 v0;
    float padding0;
    Vector3 v1;
    float padding1;
};

/// <summary>
/// GPUパーティクルグループの保持データ（マテリアル・パーティクル一覧など）
/// </summary>
struct ParticleCSGroupData {
    // マテリアルデータ
    std::vector<ParticleMaterial> materials;
    // パーティクルのリスト (std::list<Particle> 型)
    std::list<CSParticle> particles;
    // グループ名
    std::string groupName;
    // ブレンドモード
    BlendMode blendMode = BlendMode::kAdd;
};

static const uint32_t kMaxParticleCount = 100000; // 最大パーティクル数
extern uint32_t threadsPerGroup_;                 // 1グループあたりのスレッド数
extern int threadGroupSize_;                      // スレッドグループの数

/// <summary>
/// GPUパーティクルの挙動設定（寿命・速度・色・各種エフェクトの有効化など）
/// HLSL側 struct ParticleCSSettings とレイアウトを一致させること
/// </summary>
struct ParticleCSSettings {
    float lifeTimeMin = 1.0f;
    float lifeTimeMax = 3.0f;
    float scaleMin = 0.5f;
    float scaleMax = 1.5f;
    Vector3 velocityMin = {-0.5f, -0.5f, -0.5f};
    float padding1{};
    Vector3 velocityMax = {0.5f, 0.5f, 0.5f};
    float padding2{};
    Vector4 startColor = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector4 endColor = {1.0f, 1.0f, 1.0f, 0.0f};
    uint32_t enableLifetimeScale = 0;
    uint32_t enableRandomColor = 0;
    uint32_t enableSinScale = 0;
    uint32_t emitCount = 0;
    uint32_t maxParticleCount = 10000;
    float sinScaleFrequency{};
    float sinScaleAmplitude{};
    uint32_t enableGravity = 0;
    Vector3 gravity = {0.0f, -9.8f, 0.0f};
    uint32_t enableTrail = 0;
    float trailSpawnDistance = 0.1f;
    uint32_t maxTrailPerParticle = 5;
    float trailLifeTimeScale = 0.5f;
    float paddingTrail{};
    Vector3 trailScaleMultiplier = {0.8f, 0.8f, 0.8f};
    float padding3{};
    Vector4 trailColorMultiplier = {1.0f, 1.0f, 1.0f, 0.7f};
    float trailVelocityScale = 0.3f;
    uint32_t trailInheritVelocity = 1;
    float trailMinLifeTime = 0.3f;
    float padding4{};
    uint32_t enableGather = 0;
    float gatherStartRatio = 0.5f;
    float gatherStrength = 2.0f;
    float padding5{};
    Vector3 gatherTarget = {0, 0, 0};
    float padding6{};
    Vector3 gatherTargetOffset = {0, 0, 0};
    uint32_t enableGatherForTrail = 0;
    uint32_t enableVortex = 0;
    Vector3 vortexTarget = {0, 0, 0};
    Vector3 vortexTargetOffset = {0, 0, 0};
    float vortexStrength = 5.0f;
    uint32_t enableVortexForTrail = 0;
    Vector3 vortexAxis = {0.0f, 1.0f, 0.0f};
    uint32_t enableAcceleration = 0;
    Vector3 acceleration = {0.0f, 0.0f, 0.0f};
    uint32_t enableVelocityDamping = 0;
    float velocityDampingFactor = 0.0f;
    uint32_t enableLifetimeVelocityDamping = 0;
    float lifetimeVelocityDampingStart = 0.0f;
    uint32_t enableRadialVelocity = 0;
    float radialVelocityStrength = 0.0f;
    float radialVelocityRandomness = 0.0f;
    float padding7{};
    Vector3 radialVelocityCenter = {0.0f, 0.0f, 0.0f};
    float padding8{};
    uint32_t enableCurlNoise;
    float curlNoiseScale;
    float curlNoiseStrength;
    float curlNoiseTimeScale;
    uint32_t curlNoiseOctaves;
    float curlNoiseAttractStrength;
    uint32_t curlNoiseBlendMode;
    float curlNoisePosRandomStrength;
    Vector3 curlNoiseAttractCenter;
    float padding9{};
    // ---- 終了スケール ----
    uint32_t enableEndScale = 0;                // 1=有効: lifeRatio で initialScale→endScale を lerp
    Vector3 endScaleValue = {0.0f, 0.0f, 0.0f}; // 終了時スケール値
    // ---- 回転 ----
    uint32_t enableRandomRotation = 0;        // 1=発生時にランダム初期角度
    Vector3 rotationMin = {0.0f, 0.0f, 0.0f}; // 初期角度 最小 (ラジアン, XYZ)
    Vector3 rotationMax = {0.0f, 0.0f, 0.0f}; // 初期角度 最大 (ラジアン, XYZ)
    float paddingRotMax{};
    uint32_t enableRandomAngularVelocity = 0;        // 1=発生時にランダム角速度
    Vector3 angularVelocityMin = {0.0f, 0.0f, 0.0f}; // 角速度 最小 (ラジアン/秒, XYZ)
    float paddingAngVelMin{};
    Vector3 angularVelocityMax = {0.0f, 0.0f, 0.0f}; // 角速度 最大 (ラジアン/秒, XYZ)
    // ---- 中間カラー (3-stop gradient) ----
    // HLSL packing: enableMidColor(4)+midColorRatio(4)+padMidColor(8) = 16bytes, then float4 midColor = 16bytes
    uint32_t enableMidColor = 0;           // 1=有効: start→mid→end の3段階補間
    float midColorRatio = 0.5f;            // midColor に達するlife比率 [0,1]
    float padMidColor0 = 0.0f;
    float padMidColor1 = 0.0f;
    Vector4 midColor = {1.0f, 1.0f, 1.0f, 1.0f}; // 中間色
    // ---- タービュランス ----
    uint32_t enableTurbulence = 0;         // 1=有効: per-particleランダム振動力
    float turbulenceStrength = 1.0f;       // 振動力の大きさ
    float turbulenceFrequency = 2.0f;      // 振動周波数 (Hz)
    float turbulencePad = 0.0f;
    // ---- 発生形状 ----
    uint32_t emitShape = 0;                // 0=Box, 1=Sphere Surface, 2=Cone
    float emitSphereRadius = 1.0f;         // Sphere/Cone 半径
    float emitConeAngle = 0.5236f;         // Cone 半開角 (ラジアン, デフォルト30°)
    float emitShapePad = 0.0f;
};

/// =====================================================================
/// フィールドがパーティクルに適用する「一度きりの設定上書き」データ
/// overrideMask のビットが立っている項目だけ上書きされる。
/// パーティクル側の settingsOverrideFlags に同じビットが既に立っていたら
/// 上書きをスキップし、一度きり保証を実現する。
/// =====================================================================
struct ParticleFieldSettingsOverride {
    /// 上書きするかどうかのビットマスク（0=上書きしない）
    /// ParticleSettingsOverrideBits の組み合わせ
    uint64_t overrideMask = 0;

    // ---------- 上書き値 ----------
    // overrideMask の対応ビットが立っているときのみ使用される

    float lifeTimeMin = 1.0f;
    float lifeTimeMax = 3.0f;
    float scaleMin = 0.5f;
    float scaleMax = 1.5f;
    Vector3 velocityMin = {-0.5f, -0.5f, -0.5f};
    Vector3 velocityMax = {0.5f, 0.5f, 0.5f};
    Vector4 startColor = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector4 endColor = {1.0f, 1.0f, 1.0f, 0.0f};
    uint32_t enableLifetimeScale = 0;
    uint32_t enableRandomColor = 0;
    uint32_t enableSinScale = 0;
    float sinScaleFrequency = 1.0f;
    float sinScaleAmplitude = 0.5f;
    uint32_t enableGravity = 0;
    Vector3 gravity = {0.0f, -9.8f, 0.0f};
    uint32_t enableTrail = 0;
    float trailSpawnDistance = 0.1f;
    uint32_t maxTrailPerParticle = 5;
    float trailLifeTimeScale = 0.5f;
    Vector3 trailScaleMultiplier = {0.8f, 0.8f, 0.8f};
    Vector4 trailColorMultiplier = {1.0f, 1.0f, 1.0f, 0.7f};
    float trailVelocityScale = 0.3f;
    uint32_t trailInheritVelocity = 1;
    float trailMinLifeTime = 0.3f;
    uint32_t enableGather = 0;
    float gatherStartRatio = 0.5f;
    float gatherStrength = 2.0f;
    Vector3 gatherTarget = {0.0f, 0.0f, 0.0f};
    uint32_t enableVortex = 0;
    float vortexStrength = 5.0f;
    Vector3 vortexAxis = {0.0f, 1.0f, 0.0f};
    uint32_t enableAcceleration = 0;
    Vector3 acceleration = {0.0f, 0.0f, 0.0f};
    uint32_t enableVelocityDamping = 0;
    float velocityDampingFactor = 0.95f;
    uint32_t enableLifetimeVelDamping = 0;
    float lifetimeVelDampingStart = 0.5f;
    uint32_t enableCurlNoise = 0;
    float curlNoiseScale = 1.0f;
    float curlNoiseStrength = 1.0f;
    float curlNoiseTimeScale = 1.0f;
    uint32_t curlNoiseOctaves = 3;
    float curlNoiseAttractStrength = 0.0f;
    uint32_t curlNoiseBlendMode = 0;
    float curlNoisePosRandom = 0.0f;
};

/// =============================================
/// GPUに送るフィールドデータ（StructuredBuffer 要素 / 16バイト境界）
///
/// 【重要】このレイアウトは HLSL 側 `struct ParticleField`
///   （Resources/shaders/Particle/Particle.hlsli）と**バイト単位で一致**させること。
///   メンバの追加/削除/並べ替えは両方を同時に直し、下の static_assert を更新する。
///
/// 【責務（1構造体に混載。Phase 6 で論理グループとして整理）】
///   1. Force        : position / radius / direction / strength / fieldType / falloff
///                     （ApplyFields の速度系エフェクト。Wind/Attract/Repel/Vortex）
///   2. LifeDrain    : lifeTimeDrain / enableLifeDrain（範囲内で寿命を削る）
///   3. ForceTrail   : enableForceTrail / trailSpawnDistanceOverride（フィールドでトレイル強制）
///   4. ColorMultiply: enableColorMultiply / colorMultiplier（範囲内で色を乗算）
///   5. SettingsOverride : enableSettingsOverride（一度きりの設定上書きの有効化。
///                         実データは gFieldsOverride[t1]/ParticleFieldSettingsOverride 側）
///   6. EmitSpawn    : enableEmitSpawn / emitSpawnLifeTimeMin/Max / emitSpawnCount
///                     （Emit 時、このフィールド範囲内にのみ発生させる）
///   7. GroupFilter  : groupId（どのエミッターに影響するか）
///
/// 【グループフィルタ仕様（GPUの ApplyFields と一致）】
///   - field.groupId == -1                      → 全エミッターに影響
///   - emitter.fieldGroupId == -1               → そのエミッターは全フィールドの影響を受ける
///   - 上記以外は field.groupId == emitter.fieldGroupId のときのみ影響
/// =============================================
struct ParticleFieldData {
    // --- 1. Force（速度系エフェクト） ---
    Vector3 position = {0, 0, 0};  // フィールドの中心座標
    float radius = 5.0f;           // 影響範囲（球）
    Vector3 direction = {1, 0, 0}; // Wind/Vortex軸方向（C++側で正規化して転送）
    float strength = 1.0f;         // 力の強さ
    uint32_t fieldType = 0;        // ParticleFieldType (0:Wind 1:Attract 2:Repel 3:Vortex)
    float falloff = 1.0f;          // 減衰指数（1=線形, 2=二乗）

    // --- 2. LifeDrain（寿命ドレイン） ---
    float lifeTimeDrain = 0.0f;   // 毎秒削る寿命量（秒/秒）
    uint32_t enableLifeDrain = 0; // 0=無効 1=有効

    // --- 3. ForceTrail（トレイル強制生成） ---
    uint32_t enableForceTrail = 0;           // 0=無効 1=有効
    float trailSpawnDistanceOverride = 0.0f; // >0 のときトレイル生成間隔を上書き

    // --- 4. ColorMultiply（カラー乗算） ---
    uint32_t enableColorMultiply = 0;                   // 0=無効 1=有効
    Vector4 colorMultiplier = {1.0f, 1.0f, 1.0f, 1.0f}; // 乗算色（白=変化なし）

    // --- 5. SettingsOverride（一度きり設定上書きの有効化フラグ） ---
    uint32_t enableSettingsOverride = 0; // 0=無効 1=有効（実データは gFieldsOverride 側）

    // --- 6. EmitSpawn（Emit時スポーン判定） ---
    uint32_t enableEmitSpawn = 0;       // 1=このフィールド範囲内にのみEmit
    float emitSpawnLifeTimeMin = 0.25f; // enableEmitSpawn=1 時の寿命Min
    float emitSpawnLifeTimeMax = 0.25f; // enableEmitSpawn=1 時の寿命Max
    uint32_t emitSpawnCount = 0;        // enableEmitSpawn=1 時の発生数（0=エミッター依存）

    // --- 7. GroupFilter（グループID） ---
    int32_t groupId = -1;                      // -1=全エミッター対象 / 0以上=同IDのエミッターのみ
    float groupIdPadding[3] = {0.f, 0.f, 0.f}; // 16バイト境界揃え
};

// GPUレイアウト契約の固定（HLSL `struct ParticleField` と一致させること）。
// 値が変わった＝レイアウトが動いた合図。HLSL 側も合わせて更新する。
static_assert(sizeof(ParticleFieldData) == 112, "ParticleFieldData のサイズが変化。HLSL struct ParticleField と一致させること");
static_assert(offsetof(ParticleFieldData, colorMultiplier) == 60, "colorMultiplier のオフセットずれ。HLSL と要整合");
static_assert(offsetof(ParticleFieldData, enableSettingsOverride) == 76, "enableSettingsOverride のオフセットずれ。HLSL と要整合");
static_assert(offsetof(ParticleFieldData, groupId) == 96, "groupId のオフセットずれ。HLSL と要整合");

/// =============================================
/// エディタ用フィールド（名前付き）
/// =============================================
struct ParticleField {
    std::string name = "NewField";
    bool enabled = true;
    ParticleFieldData data = {};
    ParticleFieldSettingsOverride override_ = {}; // 一度きり設定上書きデータ
};

/// =======================

/// ====== CPUParticle ======

/// <summary>
/// CPUパーティクルの挙動設定（寿命・速度・色・軌跡・各種オプションなど）
/// </summary>
struct ParticleSetting {
    int maxTrailParticles; // 最大軌跡パーティクル数
    float gatherStartRatio = 0.5f;
    float gatherStrength = 2.0f;
    float trailSpawnInterval; // 軌跡パーティクル生成間隔
    float trailLifeScale{};   // 軌跡パーティクルの寿命スケール
    float lifeTimeMin{};
    float lifeTimeMax{};
    float gravity{};
    float alphaMin{};
    float alphaMax{};
    float scaleMin{};
    float scaleMax{};
    float trailVelocityScale{}; // 軌跡の速度スケール
    Vector3 translate{};
    Vector3 rotation{};
    Vector3 scale{};
    Vector3 velocityMin{};
    Vector3 velocityMax{};
    Vector3 particleStartScale{};
    Vector3 particleEndScale{};
    Vector3 startAcce{};
    Vector3 endAcce{};
    Vector3 startRote{};
    Vector3 endRote{};
    Vector3 rotateVelocityMin{};
    Vector3 rotateVelocityMax{};
    Vector3 allScaleMax{};
    Vector3 allScaleMin{};
    Vector3 rotateStartMax{};
    Vector3 rotateStartMin{};
    Vector3 trailScaleMultiplier{}; // 軌跡パーティクルのサイズ倍率
    Vector4 startColor = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector4 endColor = {1.0f, 1.0f, 1.0f, 1.0f};
    Vector4 trailColorMultiplier{}; // 軌跡パーティクルの色倍率
    uint32_t count{};
    bool enableTrail{};          // 軌跡機能を有効にするか
    bool trailInheritVelocity{}; // 軌跡が親の速度を継承するか
    bool isRandomColor{};
    bool isBillboard = false;
    bool isBillboardX = false;
    bool isBillboardY = false;
    bool isBillboardZ = false;
    bool isRandomRotate = false;
    bool isRotateVelocity = false;
    bool isAcceMultiply = false;
    bool isRandomSize = false;
    bool isRandomAllSize = false;
    bool isSinMove = false;
    bool isFaceDirection = false;
    bool isEndScale = false;
    bool isEmitOnEdge = false;
    bool isGatherMode = false;

    BlendMode blendMode = BlendMode::kAdd;

    ParticleSetting() : enableTrail(false), trailSpawnInterval(0.05f),
                        maxTrailParticles(1), trailLifeScale(0.5f),
                        trailScaleMultiplier({0.8f, 0.8f, 0.8f}),
                        trailColorMultiplier({1.0f, 1.0f, 1.0f, 0.7f}),
                        trailInheritVelocity(true), trailVelocityScale(0.3f) {}
};

/// <summary>
/// パーティクルの統計情報（数・インスタンス数）
/// </summary>
struct ParticleStats {
    size_t count = 0;
    size_t instanceCount = 0; // 同じ名前のエミッター数
};

/// <summary>
/// GPUへ送る描画用パーティクルデータ（WVP・World・色）
/// </summary>
struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4 color;
};

/// <summary>
/// CPUで管理する1パーティクル分のデータ
/// </summary>
struct Particle {
    WorldTransform transform{}; // 位置
    Vector3 emitterPosition{};
    Vector3 velocity{}; // 速度
    Vector3 Acce{};
    Vector3 startScale{};
    Vector3 endScale{};
    Vector3 startAcce{};
    Vector3 endAcce{};
    Vector3 startRote{};
    Vector3 endRote{};
    Vector3 rotateVelocity{};
    Vector3 fixedDirection{};
    Vector4 color{};     // 色
    float lifeTime{};    // ライフタイム
    float currentTime{}; // 現在の時間
    float initialAlpha{};
    Vector3 relativePosition{}; // 親からの相対位置
    Vector3 parentOffset{};     // 親に対するオフセット
    bool isChild{};             // 子パーティクルかどうか
    bool createTrail{};         // 軌跡を作成するか
    float trailSpawnTimer{};    // 軌跡生成のタイマー
    float trailSpawnInterval{}; // 軌跡生成間隔
    int maxChildren{};          // 最大子供数
    float childLifeScale{};     // 子の寿命スケール（親より短く）

    BlendMode blendMode = BlendMode::kAdd;

    Particle() : isChild(false), createTrail(false), trailSpawnTimer(0.0f),
                 trailSpawnInterval(0.1f), maxChildren(10), childLifeScale(0.8f) {}
};

/// <summary>
/// CPUパーティクルグループの保持データ（マテリアル・パーティクル一覧・インスタンシング情報）
/// </summary>
struct ParticleGroupData {
    // マテリアルデータ
    std::vector<ParticleMaterial> materials;
    // パーティクルのリスト (std::list<Particle> 型)
    std::list<Particle> particles;
    // インスタンシングデータ用SRVインデックス
    uint32_t instancingSRVIndex = 0;
    // インスタンシングリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource = nullptr;
    // インスタンス数
    uint32_t instanceCount = 0;
    // インスタンシングデータを書き込むためのポインタ
    ParticleForGPU *instancingData = nullptr;
    // グループ名
    std::string groupName;
    // ブレンドモード
    BlendMode blendMode = BlendMode::kAdd;
};

/// =========================

/// <summary>
/// MaterialData を ParticleMaterial へ変換
/// </summary>
/// <param name="material">変換元のマテリアルデータ</param>
/// <returns>ParticleMaterial: 変換後のパーティクルマテリアル</returns>
ParticleMaterial ForParticleMaterial(MaterialData material);

/// <summary>
/// MaterialData の配列を ParticleMaterial の配列へ変換
/// </summary>
/// <param name="materials">変換元のマテリアルデータ配列</param>
/// <returns>std::vector&lt;ParticleMaterial&gt;: 変換後の配列</returns>
std::vector<ParticleMaterial> ForParticleMaterials(std::vector<MaterialData> materials);
} // namespace Hagine
