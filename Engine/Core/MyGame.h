#pragma once
#include "Framework.h"
#include <memory>

// MotionEditor はアプリ側 Utility（Hagine 名前空間外）
class MotionEditor;

namespace Hagine {
class MyGame : public Framework {
  public: // メンバ関数
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize() override;

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize() override;

    /// <summary>
    /// 更新
    /// </summary>
    void Update() override;

    /// <summary>
    /// 描画
    /// </summary>
    void Draw() override;

  private:
    // ゲーム固有のモーション編集機能（エンジンには持たせない）
    MotionEditor *motionEditor_ = nullptr;
};
} // namespace Hagine
