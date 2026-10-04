#pragma once
#include "TransitionPreset.h"
#include <d3d12.h>
#include <string>
#include <wrl.h>

namespace Hagine {
class DirectXCommon;
class SrvManager;

/// <summary>
/// 遷移の幕1枚ぶんの、GPU へ渡す値（Transition.CS.hlsl の Layer と同じ並び）
/// </summary>
struct TransitionGpuLayer
{
    Vector4 color;
    Vector4 color2;
    Vector4 edgeColor;
    Vector4 palette[4];
    Vector2 center;
    float angle;
    float count;
    float progress;
    float softness;
    float edgeWidth;
    float opacity;
    int shape;
    int fill;
    int invert;
    int order;
    float cellSpread;
    float amplitude;
    float seed;
    float gradientAngle;
    float imageScale;
    int paletteCount;
    int hasRule;
    int hasImage;
};
static_assert(sizeof(TransitionGpuLayer) == 192, "Transition.CS.hlsl の Layer と並びを一致させること");

/// <summary>
/// 遷移の定数バッファ（Transition.CS.hlsl の TransitionParams と同じ並び）
/// </summary>
struct TransitionGpuData
{
    int textureSize[2] = {0, 0};
    float time = 0.0f;
    float deltaTime = 0.0f;
    int layerCount = 0;
    int hasSnapshot = 0;
    float padding0[2] = {};
    float mosaic = 0.0f;
    float blur = 0.0f;
    float swirl = 0.0f;
    float zoom = 0.0f;
    float zoomBlur = 0.0f;
    float rotate = 0.0f;
    float chroma = 0.0f;
    float desaturate = 0.0f;
    float brightness = 0.0f;
    float shake = 0.0f;
    float wave = 0.0f;
    float padding1 = 0.0f;
    TransitionGpuLayer layers[TransitionPreset::kMaxLayers] = {};
};
static_assert(sizeof(TransitionGpuData) == 848, "Transition.CS.hlsl の TransitionParams と並びを一致させること");

/// <summary>
/// 遷移の1フレームぶんの状態（どの幕がどこまで進んでいるか）
/// </summary>
struct TransitionFrame
{
    const std::vector<TransitionLayer> *layers = nullptr; // 使う幕
    float layerProgress[TransitionPreset::kMaxLayers] = {}; // 幕ごとの進み具合（イージング済み）
    bool flipInvert = false;                             // 明けるときに「同じ向きへ抜ける」なら true
    TransitionSceneFx sceneFx;                           // 下の画面の崩し方（量は進み具合を掛け済み）
};

/// <summary>
/// シーン遷移の幕を GPU で描く係。
/// 画面（UI まで合成した最終結果）を読み、幕を重ねた結果を同じテクスチャへ書き戻す。
/// 「前の画面」で塗る幕のために、切り替える直前の画面を控えておく役も持つ
/// </summary>
class TransitionRenderer
{
  public:
    /// <summary>初期化（テクスチャ・デスクリプタの確保）</summary>
    void Initialize(DirectXCommon *pDxCommon, SrvManager *pSrvManager);

    /// <summary>終了</summary>
    void Finalize();

    /// <summary>
    /// 今の画面を「前の画面」として控える（幕を重ねる前に呼ぶ）
    /// </summary>
    /// <param name="pTarget">画面（GENERIC_READ 状態）</param>
    void CaptureSnapshot(ID3D12Resource *pTarget);

    /// <summary>控えた前の画面を捨てる（次の遷移でまた控え直す）</summary>
    void ClearSnapshot() { hasSnapshot_ = false; }

    /// <summary>
    /// 幕を重ねて、結果を pTarget へ書き戻す
    /// </summary>
    /// <param name="pTarget">画面（GENERIC_READ 状態。終わったら GENERIC_READ に戻す）</param>
    /// <param name="frame">幕の状態</param>
    /// <param name="time">遷移が始まってからの時間（波や揺れを動かす）</param>
    void Render(ID3D12Resource *pTarget, const TransitionFrame &frame, float time);

  private:
    /// <summary>レイヤーの値を GPU 用へ詰める</summary>
    void FillLayer(TransitionGpuLayer &out, const TransitionLayer &layer, float progress, bool flipInvert, int index);

    /// <summary>images からの相対パスのテクスチャを、テーブルの slot 番目へ差す（無ければ白）</summary>
    bool BindTexture(uint32_t slot, const std::string &relativePath);

    DirectXCommon *pDxCommon_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> outputResource_;   // 幕を重ねた結果（UAV）
    Microsoft::WRL::ComPtr<ID3D12Resource> snapshotResource_; // 切り替える直前の画面
    uint32_t outputUavIndex_ = 0;
    bool hasSnapshot_ = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    TransitionGpuData *pData_ = nullptr;

    // SRV テーブル（t0..t9）。書き換えが前のフレームのディスパッチに混ざらないよう、リングで持つ
    static constexpr uint32_t kTableSize = 10;
    static constexpr uint32_t kTableCount = 4;
    uint32_t tableBaseIndex_ = 0;
    uint32_t tableCursor_ = 0;
    bool tableReady_ = false;
    // 今書いているテーブルの先頭
    uint32_t currentTable_ = 0;
};
} // namespace Hagine
