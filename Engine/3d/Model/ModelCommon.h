#pragma once
#include "DirectXCommon.h"

/// <summary>
/// モデル共通クラス
/// モデルシステムで共有するDirectXCommonを管理する
/// </summary>
namespace Hagine {
class ModelCommon
{
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
    DirectXCommon *GetDxCommon() const { return pDxCommon_; }

  private:
    /// ===================================================
    /// private varians
    /// ===================================================

    DirectXCommon *pDxCommon_; // DirectX共通クラス
};
} // namespace Hagine
