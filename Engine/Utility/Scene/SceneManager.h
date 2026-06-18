#pragma once
#include "AbstractSceneFactory.h"
#include "SceneTransition.h"
#include <OffScreen.h>
#include <Engine/Render/DrawSystem.h>
#include <memory>
#include <string>

namespace Hagine {
class BaseScene;
class SceneManager {
  private:
    SceneManager() = default;
    ~SceneManager();
    SceneManager(SceneManager &) = delete;
    SceneManager &operator=(SceneManager &) = delete;

  public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static SceneManager *GetInstance() {
        static SceneManager instance;
        return &instance;
    }

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// シーン終了
    /// </summary>
    void SceneFinalize();

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// 更新
    /// </summary>
    void Update();

    /// <summary>
    /// 描画
    /// </summary>
    void Draw();

    /// <summary>
    /// オフスクリーン描画
    /// </summary>
    void DrawForOffScreen();

    void SceneSelection(const std::string &sceneName);

    /// <summary>
    /// 遷移描画
    /// </summary>
    void DrawTransition();

    bool GetTransitionEnd() const { return transitionEnd_; }

  public: // setter / getter
    void SetSceneFactory(std::unique_ptr<AbstractSceneFactory> sceneFactory) { sceneFactory_ = std::move(sceneFactory); }

    /// <summary>
    /// 次シーン予約
    /// </summary>
    void NextSceneReservation(const std::string &sceneName);

    /// <summary>
    /// シーン切り替え
    /// </summary>
    void SceneChange();

    float GetClearTime() const { return clearTime_; }
    float GetHP() const { return hp_; }

    BaseScene *GetBaseScene() const { return scene_.get(); }
    std::string GetCurrentSceneName() const { return currentSceneName_; }

    void SetClearTime(float time) { clearTime_ = time; }
    void SetHP(float hp) { hp_ = hp; }

    void SetOffScreen(OffScreen *offscreen) { offscreen_ = offscreen; }
    void SetDrawSystem(DrawSystem *drawSystem) { drawSystem_ = drawSystem; }

  private:
    OffScreen *offscreen_ = nullptr;
    DrawSystem *drawSystem_ = nullptr;
    // 今のシーン（実行中のシーン）
    std::unique_ptr<BaseScene> scene_;
    // 次のシーン
    std::unique_ptr<BaseScene> nextScene_;
    // シーンファクトリー
    std::unique_ptr<AbstractSceneFactory> sceneFactory_ = nullptr;
    SceneTransition *transition_ = nullptr;

    std::string currentSceneName_;

    bool transitionEnd_ = false;
    bool firstChange_ = false;

    float clearTime_ = 0.0f;
    float hp_ = 0.0f;
};
} // namespace Hagine
