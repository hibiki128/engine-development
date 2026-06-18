#pragma once
#include "BaseScene.h"

/// <summary>
/// セレクトシーンのクラス
/// ステージ選択や設定などを行うシーンを管理する
/// </summary>
class TutorialScene : public Hagine::BaseScene {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize() override;

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize() override;

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update() override;

    /// <summary>
    /// 描画処理
    /// </summary>
    void Draw() override;

    /// <summary>
    /// オフスクリーン描画処理
    /// </summary>
    void DrawForOffScreen() override;

    /// <summary>
    /// シーン設定を追加
    /// </summary>
    void AddSceneSetting() override;

    /// <summary>
    /// オブジェクト設定を追加
    /// </summary>
    void AddObjectSetting() override;

    /// <summary>
    /// パーティクル設定を追加
    /// </summary>
    void AddParticleSetting() override;

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// カメラを更新
    /// </summary>
    void CameraUpdate();

    /// <summary>
    /// シーン遷移を実行
    /// </summary>
    void ChangeScene();

  private:
    /// ===================================================
    /// private variants
    /// ===================================================
};
