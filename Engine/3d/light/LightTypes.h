#pragma once
#include <cstdint>
#include <string>
#include <type/Vector3.h>
#include <type/Vector4.h>

// ---- 前方描画（定数バッファ経由）のライト枠 ----
// ディファードON時はポイントライトを StructuredBuffer（kMaxBufferedPointLights）から読むため、
// この枠は「ディファードOFFのときのフォールバック」でしか使われない。
// ※ EngineAssets/shaders/Object/Object3d.hlsli の同名マクロと必ず一致させること。
#define MAX_POINT_LIGHTS 16
// スポットライトは前方・ディファードともに定数バッファ経由（タイルカリング対象外）なので上限あり。
// ※ Object3d.hlsli の MAX_SPOT_LIGHTS / Deferred.hlsli の MAX_SPOT_LIGHTS_DEFERRED と一致させること。
#define MAX_SPOT_LIGHTS 32

namespace Hagine {

/// <summary>
/// ライトタイプ種別
/// </summary>
enum class LightType
{
    Directional, // 平行光源
    Point,       // ポイントライト
    Spot         // スポットライト
};

// ===================================================
//  GPUへ流す実体（HLSL側の構造体と並びを一致させること）
// ===================================================

/// <summary>
/// 平行光源データ。Object3d.hlsli の DirectionalLight と一致させること
/// </summary>
struct DirectionalLightData
{
    Vector4 color;       // 色 (RGBA)
    Vector3 direction;   // 方向
    float intensity;     // 輝度
    int32_t active;      // 有効フラグ
    int32_t HalfLambert; // ハーフランバート適用
    int32_t BlinnPhong;  // Blinn-Phong適用
};

/// <summary>
/// ポイントライトデータ。Object3d.hlsli の PointLight と一致させること
/// </summary>
struct PointLightData
{
    Vector4 color;       // 色 (RGBA)
    Vector3 position;    // 位置
    float intensity;     // 輝度
    int32_t active;      // 有効フラグ
    float radius;        // 影響半径
    float decay;         // 減衰率
    int32_t HalfLambert; // ハーフランバート適用
    int32_t BlinnPhong;  // Blinn-Phong適用
    float padding[3];
};

/// <summary>
/// ポイントライト群（前方描画用の定数バッファ）
/// </summary>
struct PointLightsCB
{
    alignas(16) PointLightData lights[MAX_POINT_LIGHTS];
    int32_t count; // 有効数
    float padding[3];
};

/// <summary>
/// スポットライトデータ。Object3d.hlsli の SpotLight と一致させること
/// </summary>
struct SpotLightData
{
    Vector4 color;       // 色 (RGBA)
    Vector3 position;    // 位置
    float intensity;     // 輝度
    Vector3 direction;   // 方向
    float distance;      // 照射距離
    float decay;         // 減衰率
    float cosAngle;      // コーン角度の余弦
    int32_t active;      // 有効フラグ
    int32_t HalfLambert; // ハーフランバート適用
    int32_t BlinnPhong;  // Blinn-Phong適用
    float padding[3];
};

/// <summary>
/// スポットライト群（定数バッファ）
/// </summary>
struct SpotLightsCB
{
    SpotLightData lights[MAX_SPOT_LIGHTS];
    int32_t count; // 有効数
    float padding[3];
};

/// <summary>
/// GPU転送用カメラデータ
/// </summary>
struct CameraForGPU
{
    Vector3 worldPosition; // ワールド座標
};

/// <summary>
/// ディファード用ポイントライト（StructuredBuffer要素・48バイト）。
/// HLSL側（Deferred.hlsli）の PointLightGPU と必ず一致させること
/// </summary>
struct PointLightGPU
{
    Vector3 position{};     // ワールド座標
    float radius = 0.0f;    // 影響半径
    Vector3 color{};        // 色（RGB）
    float intensity = 0.0f; // 輝度
    float decay = 1.0f;     // 減衰率
    uint32_t flags = 0;     // bit0: HalfLambert / bit1: BlinnPhong
    float padding[2]{};     // 16バイト境界合わせ
};
static_assert(sizeof(PointLightGPU) == 48, "PointLightGPUはHLSL側と一致させること");

// ===================================================
//  UI のやりとり用
// ===================================================

/// <summary>
/// 実行時に登録する動的ポイントライトのパラメータ。
/// GPUパーティクルの発光など、毎フレーム位置・強さが変わる光源に使う。
/// </summary>
struct DynamicPointLightDesc
{
    Vector3 position{};                   // ワールド座標
    Vector4 color = {1.f, 1.f, 1.f, 1.f}; // 色
    float intensity = 1.0f;               // 輝度
    float radius = 5.0f;                  // 影響半径
    float decay = 1.0f;                   // 減衰率
};

/// <summary>
/// 光源のプロパティUIから「一覧の持ち主にやってほしいこと」を返す小さな伝票。
///
/// 名前の一意化はポイントとスポットをまたいで行う必要があり、複製・削除は
/// 一覧の選択状態やギズモ登録にも影響する。それらは LightGroup の仕事なので、
/// 各ライトのクラスは「何を要求されたか」だけを返して自分では実行しない。
/// </summary>
struct LightEditRequest
{
    enum class Kind
    {
        None,      // 何もなし
        Rename,    // 名前の変更
        Duplicate, // 複製
        Remove,    // 削除
    };

    Kind kind = Kind::None;
    std::string newName; // Kind::Rename のときの希望名
};

/// <summary>
/// 光源一覧の行を描いた結果。走査中に配列を増減させられないので、
/// 「どの行が押されたか」だけを返してループ後にまとめて処理する。
/// </summary>
struct LightListResult
{
    int clickedIndex = -1;   // 選択された行（-1で無し）
    int duplicateIndex = -1; // 複製を要求された行
    int removeIndex = -1;    // 削除を要求された行
};
} // namespace Hagine
