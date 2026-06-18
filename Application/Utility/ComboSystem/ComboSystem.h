#pragma once
#include "Data/DataHandler.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Hagine { class BaseObject; }
class MotionEditor;

/// <summary>
/// コンボシステムを管理するクラス
/// 連続攻撃のシーケンスと時間管理を制御する
/// 攻撃ごとにダメージ・ノックバック・コライダータイミングを設定可能
/// </summary>
class ComboSystem {
  private:
    /// ===================================================
    /// private struct
    /// ===================================================

    /// <summary>
    /// コンボ1段分のデータ
    /// </summary>
    struct ComboData {
        Hagine::BaseObject *target;     // モーションを再生するオブジェクト（見た目用）
        std::string attackData; // 攻撃モーションのファイル名（Jsonキーとしても使用）

        // --- 攻撃パラメータ（ImGuiで調整・セーブ可能）---
        float damage = 10.0f;                 // ダメージ量
        float knockbackPower = 3.0f;          // ノックバックの強さ
        float colliderActiveDuration = 0.25f; // 判定が有効な時間（秒）
        float colliderActivateDelay = 0.08f;  // 攻撃開始からコライダーが有効になる遅延（秒）

        ComboData(Hagine::BaseObject *obj, const std::string &attack,
                  float dmg, float knockback,
                  float duration, float delay)
            : target(obj), attackData(attack),
              damage(dmg), knockbackPower(knockback),
              colliderActiveDuration(duration), colliderActivateDelay(delay) {}
    };

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    void ExecuteComboAttack();
    void ResetCombo();
    void SaveComboStartPositions();

    /// ===================================================
    /// private variables
    /// ===================================================

    std::string name_ = "DefaultCombo"; // DataHandlerのファイル名に使用

    std::vector<ComboData> comboData_;              // コンボデータ配列
    std::vector<Hagine::BaseObject *> comboStartObjects_;   // コンボ開始オブジェクト配列

    int comboIndex_ = 0;             // 現在のコンボインデックス
    float comboCooldown_ = 0.0f;     // コンボクールダウン
    bool comboStarted_ = false;      // コンボ開始フラグ
    bool waitingForReturn_ = false;  // 戻り待ちフラグ
    float returnDelay_ = 0.0f;       // 戻り遅延
    float comboTimeout_ = 0.0f;      // コンボタイムアウトタイマー
    bool inputBuffered_ = false;     // 入力バッファフラグ
    float inputBufferTime_ = 0.0f;   // 入力バッファタイマー

    static const float COMBO_INTERVAL;          // コンボ間隔
    static const float INPUT_BUFFER_DURATION;   // 入力バッファ有効時間
    static const float FINAL_RETURN_DELAY;      // 最終攻撃後の戻り遅延
    static const float COMBO_TIMEOUT_DURATION;  // コンボタイムアウト時間

    // 攻撃発火コールバック
    // 引数: damage, knockbackPower, colliderActiveDuration, colliderActivateDelay
    using AttackFiredCallback = std::function<void(float, float, float, float)>;
    AttackFiredCallback onAttackFired_; // 攻撃発火時のコールバック

    std::unique_ptr<Hagine::DataHandler> dataHandler_; // データハンドラ

  public:
    /// ===================================================
    /// public method
    /// ===================================================

    ComboSystem();
    ~ComboSystem();

    /// <summary>
    /// このコンボシステムの識別名を設定（DataHandlerのファイル名になる）
    /// Add()より前に呼ぶこと
    /// </summary>
    void SetName(const std::string &name) { name_ = name; }

    /// <summary>
    /// チェーン形式でコンボを追加する
    /// </summary>
    /// <param name="target">モーションを再生するオブジェクト（見た目用）</param>
    /// <param name="attackData">モーションファイル名</param>
    /// <param name="damage">ダメージ量（デフォルト10.0）</param>
    /// <param name="knockbackPower">ノックバック強さ（デフォルト3.0）</param>
    /// <param name="colliderActiveDuration">コライダー有効時間（デフォルト0.25秒）</param>
    /// <param name="colliderActivateDelay">コライダー有効化遅延（デフォルト0.08秒）</param>
    ComboSystem &Add(Hagine::BaseObject *target, const std::string &attackData,
                     float damage = 10.0f, float knockbackPower = 3.0f,
                     float colliderActiveDuration = 0.25f, float colliderActivateDelay = 0.08f);

    /// <summary>
    /// コンボをすべてクリア
    /// </summary>
    void Clear();

    /// <summary>
    /// 攻撃入力時に呼ぶ。実行可能ならコンボを進める
    /// </summary>
    /// <returns>true: 攻撃を実行した / false: クールダウン中などで実行できなかった</returns>
    bool TryExecuteCombo();

    /// <summary>
    /// 毎フレーム呼ぶ更新処理
    /// </summary>
    void Update(float deltaTime);

    /// <summary>
    /// 攻撃発火時のコールバックを設定する
    /// PlayerAttackCollider::Activate(...) を呼び出すのに使う
    /// callback(damage, knockbackPower, colliderActiveDuration, colliderActivateDelay)
    /// </summary>
    void SetOnAttackFired(AttackFiredCallback callback) { onAttackFired_ = callback; }

    /// <summary>
    /// 攻撃パラメータをJSONに保存
    /// </summary>
    void SaveAttackParams();

    /// <summary>
    /// 攻撃パラメータをJSONから読み込む
    /// Add() でコンボを登録した後に呼ぶこと
    /// </summary>
    void LoadAttackParams();

    /// <summary>
    /// Getter
    /// </summary>
    bool IsComboActive() const { return comboStarted_; }
    bool IsObjectAttackCompleted(Hagine::BaseObject *target) const;
    bool IsCurrentAttackCompleted() const;
    int GetCurrentComboIndex() const { return comboIndex_; }
    int GetComboLength() const { return static_cast<int>(comboData_.size()); }

#ifdef _DEBUG
    /// <summary>
    /// ImGuiによる攻撃パラメータ調整UI
    /// 各攻撃のダメージ・ノックバック・コライダータイミングを調整してセーブできる
    /// </summary>
    void DrawImGui();
#endif
};