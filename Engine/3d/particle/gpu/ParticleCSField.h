#pragma once
#include "../ParticleStruct.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <type/Vector3.h>
#include <type/Vector4.h>

/// =====================================================================
/// GPU パーティクルの「フィールド」＝ 空間に置く範囲（形）と、そこに入った粒子への効果。
///
///   形   : 球 / 箱 / 円柱（位置・回転・大きさ）と、端に向かっての弱まり方
///   効果 : 風・引き寄せ・渦・抵抗・色・大きさ・寿命・トレイル・入った瞬間の変化・この範囲から発生
///          （必要なものだけ足す。どれも「弱まり方」で決まる影響度 0〜1 を掛けて効く）
///   レイヤー : どのエミッターに効くか（エミッター側の「受けるレイヤー」と1つでも重なれば効く）
/// =====================================================================
namespace Hagine {

/// <summary>範囲の形</summary>
enum class ParticleFieldShape : uint32_t
{
    Sphere = 0,   // 球（半径）
    Box = 1,      // 箱（幅・高さ・奥行き）
    Cylinder = 2, // 円柱（半径・高さ。回転させると軸が傾く）
};

/// <summary>中心から端に向かっての弱まり方</summary>
enum class ParticleFieldFalloff : uint32_t
{
    Constant = 0, // 範囲内はどこでも同じ強さ
    Linear = 1,   // 芯の外から端に向かってまっすぐ弱くなる
    Smooth = 2,   // 芯の外から端に向かってなめらかに弱くなる（境目が目立たない）
};

/// <summary>GPU 上で分岐に使う効果のビット（HLSL の FE_* と一致させること）</summary>
namespace FieldEffectBits {
static constexpr uint32_t Wind = 1u << 0;
static constexpr uint32_t Attract = 1u << 1;
static constexpr uint32_t Vortex = 1u << 2;
static constexpr uint32_t Drag = 1u << 3;
static constexpr uint32_t Tint = 1u << 4;
static constexpr uint32_t Size = 1u << 5;
static constexpr uint32_t Life = 1u << 6;
static constexpr uint32_t Kill = 1u << 7;
static constexpr uint32_t Trail = 1u << 8;
static constexpr uint32_t Once = 1u << 9;
// 粒子の Update に関わる効果（これが1つも無いフィールドは Update で読まない）
static constexpr uint32_t UpdateMask = Wind | Attract | Vortex | Drag | Tint | Size | Life | Kill | Trail | Once;
} // namespace FieldEffectBits

/// =============================================
/// GPU に送る1本ぶん（StructuredBuffer 要素・16バイト境界・160バイト）
///
/// 【重要】HLSL 側 `struct ParticleFieldGPU`（Particle.hlsli）とバイト単位で一致させること。
/// 形はワールド→ローカルの軸3本で持つので、シェーダーは回転行列を組み立てずに内積だけで済む。
/// =============================================
struct ParticleFieldGPU
{
    // 形
    Vector3 center = {0.0f, 0.0f, 0.0f};
    uint32_t shape = 0;          // ParticleFieldShape
    Vector3 axisX = {1.0f, 0.0f, 0.0f};
    float boundRadiusSq = 0.0f;  // 形を包む球の半径²（まずこれで大まかに外を弾く）
    Vector3 axisY = {0.0f, 1.0f, 0.0f};
    float falloffStart = 0.0f;   // 芯の大きさ（0〜1。この割合より内側は 100%）
    Vector3 axisZ = {0.0f, 0.0f, 1.0f};
    uint32_t falloffCurve = 0;   // ParticleFieldFalloff
    Vector3 halfExtent = {1.0f, 1.0f, 1.0f}; // 球: x=半径 / 箱: 各軸の半分 / 円柱: x=半径 y=高さの半分
    uint32_t effectFlags = 0;    // FieldEffectBits

    // 力
    Vector3 windVelocity = {0.0f, 0.0f, 0.0f}; // 風: 粒子が近づいていく速度（向き×風速）
    float windResponse = 0.0f;                 // 風: なじむ速さ（1/秒。大きいほどすぐ風と同じ速さになる）
    float vortexSpeed = 0.0f;                  // 渦: 軸（形の Y 軸）まわりに回る速さ（単位/秒。負で逆回り）
    float vortexResponse = 0.0f;               // 渦: なじむ速さ（1/秒）
    float attractStrength = 0.0f;              // 引き寄せ: 中心へ向かう加速度（単位/秒²。負で押し出す）
    float absorbRadius = 0.0f;                 // 引き寄せ: 中心からこの距離まで来たら消す（0 で消さない）
    float dragPerSecond = 0.0f;                // 抵抗: 1秒あたりの減速の強さ
    float lifeSpeed = 1.0f;                    // 寿命: 範囲内での寿命の進み（倍）
    float sizeScale = 1.0f;                    // 大きさ: 範囲内での倍率
    float trailSpawnDistance = 0.0f;           // トレイル: 生成間隔（>0 で上書き）

    // 見た目
    Vector4 tint = {1.0f, 1.0f, 1.0f, 1.0f};   // 色: 掛ける色（影響度で白から寄せる）

    // 発生（Emit 用）
    float emitLifeMin = 0.25f;
    float emitLifeMax = 0.25f;
    uint32_t emitCount = 0;                    // 今フレームこのフィールドから出す数（CPU が間隔から計算）
    uint32_t pad0 = 0;
};
static_assert(sizeof(ParticleFieldGPU) == 160, "ParticleFieldGPU のサイズが変化。HLSL struct ParticleFieldGPU と一致させること");
static_assert(offsetof(ParticleFieldGPU, windVelocity) == 80, "windVelocity のオフセットずれ。HLSL と要整合");
static_assert(offsetof(ParticleFieldGPU, tint) == 128, "tint のオフセットずれ。HLSL と要整合");
static_assert(offsetof(ParticleFieldGPU, emitLifeMin) == 144, "emitLifeMin のオフセットずれ。HLSL と要整合");

/// =============================================
/// エディタで扱う1本ぶん（名前付き・保存される）
/// =============================================
struct ParticleField
{
    std::string name = "NewField";
    bool enabled = true;

    // ---- 形と範囲 ----
    ParticleFieldShape shape = ParticleFieldShape::Sphere;
    Vector3 position = {0.0f, 0.0f, 0.0f};
    Vector3 rotation = {0.0f, 0.0f, 0.0f};    // オイラー角（ラジアン。ギズモの回転と共用）
    float radius = 5.0f;                       // 球・円柱の半径
    Vector3 boxSize = {10.0f, 10.0f, 10.0f};   // 箱の幅・高さ・奥行き（全長）
    float height = 10.0f;                      // 円柱の高さ（全長）
    ParticleFieldFalloff falloff = ParticleFieldFalloff::Smooth;
    float falloffStart = 0.3f;                 // 芯の大きさ（0〜0.95）

    // ---- どのエミッターに効くか ----
    uint32_t layers = 0xFFFFFFFFu;             // bit i = レイヤー i+1

    // ---- 効果（必要なものだけ有効にする）----
    struct Wind
    {
        bool enabled = false;
        Vector3 direction = {1.0f, 0.0f, 0.0f}; // 風向き
        float speed = 5.0f;                      // 風速（単位/秒）
        float response = 2.0f;                   // なじむ速さ（1/秒）
    } wind;
    struct Attract
    {
        bool enabled = false;
        float strength = 10.0f;    // 引き寄せる加速度（単位/秒²。負で押し出す）
        float absorbRadius = 0.0f; // 中心からこの距離まで来たら消す（0 で消さない）
    } attract;
    struct Vortex
    {
        bool enabled = false;
        float speed = 5.0f;    // 回る速さ（単位/秒。負で逆回り）
        float response = 2.0f; // なじむ速さ（1/秒）
    } vortex;
    struct Drag
    {
        bool enabled = false;
        float perSecond = 2.0f; // 減速の強さ（1秒で速度が e^-値 倍になる）
    } drag;
    struct Tint
    {
        bool enabled = false;
        Vector4 color = {1.0f, 0.5f, 0.2f, 1.0f};
    } tint;
    struct Size
    {
        bool enabled = false;
        float scale = 1.5f;
    } size;
    struct Life
    {
        bool enabled = false;
        float speed = 3.0f;       // 寿命の進み（倍）。3 なら範囲内では3倍の速さで年を取る
        bool killOnEnter = false; // 入ったらすぐ消す
    } life;
    struct Trail
    {
        bool enabled = false;
        float spawnDistance = 0.0f; // 0 ならグループ設定の間隔
    } trail;
    struct Once
    {
        bool enabled = false;
        ParticleFieldSettingsOverride settings; // 入った瞬間に1回だけ書き換える項目
    } once;
    struct Spawn
    {
        bool enabled = false;
        uint32_t count = 1000;   // 1回に出す数（受けるエミッターごと）
        float interval = 0.0f;   // 出す間隔（秒。0 で毎フレーム）
        float lifeMin = 0.25f;
        float lifeMax = 0.25f;
        float timer = 0.0f;      // 実行時の経過（保存しない）
        uint32_t burst = 0;      // 今フレーム出す数（実行時）
    } spawn;

    /// <summary>粒子の Update に関わる効果を1つでも持つか</summary>
    bool AffectsParticles() const
    {
        return wind.enabled || attract.enabled || vortex.enabled || drag.enabled || tint.enabled || size.enabled ||
               life.enabled || trail.enabled || once.enabled;
    }
};

} // namespace Hagine
