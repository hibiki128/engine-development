#pragma once
#include "DirectXCommon.h"

/// <summary>
/// モデル共通クラス
/// モデルシステムで共有するDirectXCommonを管理する
/// </summary>
namespace Hagine {
class ModelCommon {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    ModelCommon() = default;
    ~ModelCommon() = default;
    ModelCommon(ModelCommon &) = delete;
    ModelCommon &operator=(ModelCommon &) = delete;

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// Getter
    /// </summary>
    DirectXCommon *GetDxCommon() const { return dxCommon_; }

  private:
    /// ===================================================
    /// private varians
    /// ===================================================

    DirectXCommon *dxCommon_;     // DirectX共通クラス
};
} // namespace Hagine
