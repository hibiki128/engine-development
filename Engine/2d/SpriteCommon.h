#pragma once
#include "DirectXCommon.h"
#include <Graphics/PipeLine/PipeLineManager.h>

/// <summary>
/// スプライト描画の共通処理を管理するシングルトンクラス
/// </summary>
namespace Hagine {
class SpriteCommon {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static SpriteCommon* GetInstance() {
        static SpriteCommon instance;
        return &instance;
    }

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 共通描画設定の適用
    /// </summary>
    void DrawCommonSetting();

    /// <summary>
    /// DirectXCommonの取得
    /// </summary>
    DirectXCommon* GetDxCommon() const { return dxCommon_; }

    /// <summary>
    /// ブレンドモードの設定
    /// </summary>
    void SetBlendMode(BlendMode blendMode);

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    SpriteCommon() = default;
    ~SpriteCommon() = default;
    SpriteCommon(SpriteCommon&) = delete;
    SpriteCommon& operator=(SpriteCommon&) = delete;

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    DirectXCommon* dxCommon_ = nullptr;        // DirectX共通処理
    PipeLineManager* psoManager_ = nullptr;    // パイプライン管理
};
} // namespace Hagine
