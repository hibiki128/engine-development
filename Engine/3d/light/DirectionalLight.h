#pragma once
#include "LightTypes.h"
#include "d3d12.h"
#include "wrl.h"
#include <string>

namespace Hagine {
class DirectXCommon;
class DataHandler;
class LineRenderer;

/// <summary>
/// 平行光源（太陽光）1つぶんを持つクラス。
///
/// シーンに1つしか無いので配列は持たず、定数バッファと編集UI・保存/読み込みだけを担当する。
/// 生成と破棄、描画時のバインドは LightGroup が面倒を見る。
/// </summary>
class DirectionalLight
{
  public:
    /// <summary>
    /// 定数バッファを作り、既定値を書き込む
    /// </summary>
    /// <param name="pDxCommon">DirectX共通処理</param>
    void Initialize(DirectXCommon *pDxCommon);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 全体有効フラグをGPUデータへ反映する（毎フレーム呼ぶ）
    /// </summary>
    void Update();

    /// <summary>
    /// 定数バッファのGPUアドレス
    /// </summary>
    /// <returns>D3D12_GPU_VIRTUAL_ADDRESS: 未生成なら 0</returns>
    D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress() const;

    /// <summary>
    /// 光の進む方向（正規化済み）
    /// </summary>
    Vector3 GetDirection() const;

    /// <summary>
    /// 光の色（一覧の色見本などに使う）
    /// </summary>
    Vector4 GetColor() const;

    /// <summary>
    /// 有効かどうか
    /// </summary>
    bool IsEnabled() const { return enabled_; }

    /// <summary>
    /// 有効／無効を切り替える
    /// </summary>
    /// <param name="enabled">有効にするなら true</param>
    void SetEnabled(bool enabled) { enabled_ = enabled; }

    /// <summary>
    /// 有効フラグへの参照（ImGuiのチェックボックスへ直接渡す用）
    /// </summary>
    bool *GetEnabledPtr() { return &enabled_; }

    /// <summary>
    /// プロパティ編集UI（右ペインに出す詳細設定）
    /// </summary>
    void DrawProperties();

    /// <summary>
    /// デバッグ用の可視化（光の向きを平行線で描く）
    /// </summary>
    /// <param name="drawLine">線の描画先</param>
    void DrawVisualization(LineRenderer *drawLine) const;

    /// <summary>
    /// JSONへ保存する
    /// </summary>
    /// <param name="handler">保存先</param>
    void Save(DataHandler *handler) const;

    /// <summary>
    /// JSONから読み込む
    /// </summary>
    /// <param name="handler">読み込み元</param>
    void Load(DataHandler *handler);

  private:
    DirectXCommon *pDxCommon_ = nullptr;                  // DirectX基盤
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;     // 定数バッファ
    DirectionalLightData *pData_ = nullptr;               // 書き込み先
    bool enabled_ = true;                                 // 全体有効フラグ
};
} // namespace Hagine
