#pragma once
#ifdef _DEBUG
#include "imgui.h"
#endif // _DEBUG
#include "json.hpp"
#include <fstream>

/// <summary>
/// ヒットストップ機能を管理するクラス
/// 攻撃ヒット時に時間を止める演出を制御
/// </summary>
class HitStop {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    /// <summary>
    /// ヒットストップを開始
    /// </summary>
    void Start();

    /// <summary>
    /// ImGui表示
    /// </summary>
    void imgui();

    /// <summary>
    /// Getter
    /// </summary>
    bool IsActive() const { return isActive_; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// 設定を読み込み
    /// </summary>
    void LoadSettings();

    /// <summary>
    /// 設定を保存
    /// </summary>
    void SaveSettings();

  private:
    /// ===================================================
    /// private variants
    /// ===================================================

    float stopDuration_ = 0.1f; // 停止時間（秒）
    float elapsedTime_ = 0.0f;  // 経過時間
    bool isActive_ = false;     // アクティブフラグ
};