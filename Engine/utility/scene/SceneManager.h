#pragma once
#include "SceneRegistry.h"
#include "SceneTransition.h"
#include <OffScreen.h>
#include <render/DrawSystem.h>
#include <functional>
#include <memory>
#include <string>

namespace Hagine {
class BaseScene;
class SceneManager
{
  private:
    SceneManager() = default;
    ~SceneManager();
    SceneManager(SceneManager &) = delete;
    SceneManager &operator=(SceneManager &) = delete;

  public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static SceneManager *GetInstance()
    {
        static SceneManager instance;
        return &instance;
    }

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="transition">シーン遷移演出（Framework が所有・注入する）</param>
    void Initialize(SceneTransition *transition);

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
    /// <summary>
    /// 次シーン予約
    /// </summary>
    void NextSceneReservation(const std::string &sceneName);

    /// <summary>
    /// シーン切り替え
    /// </summary>
    void SceneChange();

#ifdef USE_IMGUI
    /// <summary>
    /// 今のシーンを同じ名前で作り直すよう予約する（Play モードの停止で使う）。
    ///
    /// 作り直しは「シーンを破棄 → 生成 → Initialize」なので、シーン自身の Update や
    /// ImGui の描画中に実行すると、実行中の関数の this ごと消えることになる。
    /// そのため実際の作り直しは次フレームの Update 先頭まで遅らせる。
    /// </summary>
    /// <param name="onRebuilt">作り直した直後に呼ぶ処理（未保存の編集を戻す用）</param>
    /// <returns>bool: 予約できたら true。シーンが無い・未登録名で作り直せない場合は false</returns>
    bool RequestSceneRebuild(std::function<void()> onRebuilt = {});
#endif // USE_IMGUI

    float GetClearTime() const { return clearTime_; }
    float GetHP() const { return hp_; }
    bool GetIsGameOver() const { return isGameOver_; }

    BaseScene *GetBaseScene() const { return scene_.get(); }
    std::string GetCurrentSceneName() const { return currentSceneName_; }

    void SetClearTime(float time) { clearTime_ = time; }
    void SetHP(float hp) { hp_ = hp; }
    void SetIsGameOver(bool flag) { isGameOver_ = flag; }

    void SetOffScreen(OffScreen *pOffScreen) { pOffscreen_ = pOffScreen; }

    /// <summary>
    /// メインのオフスクリーン（ステージ0）を取得する。
    /// ゲーム側からポストエフェクトを一時的に制御する演出（必殺技の白黒フラッシュ等）に使う
    /// </summary>
    OffScreen *GetOffScreen() const { return pOffscreen_; }

    void SetDrawSystem(DrawSystem *drawSystem) { pDrawSystem_ = drawSystem; }
    void SetWinApp(WinApp *winApp) { pWinApp_ = winApp; }

    /// <summary>
    /// シーン遷移演出を取得（App 側からのフェード制御用）
    /// </summary>
    SceneTransition *GetSceneTransition() const { return pTransition_; }

  private:
#ifdef USE_IMGUI
    /// <summary>
    /// 予約された作り直しを実行する（今のシーンと同名のシーンを生成し直す）
    /// </summary>
    void RebuildCurrentScene();
#endif // USE_IMGUI

    OffScreen *pOffscreen_ = nullptr;
    DrawSystem *pDrawSystem_ = nullptr;
    WinApp *pWinApp_ = nullptr;
    // 今のシーン（実行中のシーン）
    std::unique_ptr<BaseScene> scene_;
    // 次のシーン
    std::unique_ptr<BaseScene> nextScene_;
    SceneTransition *pTransition_ = nullptr;

    std::string currentSceneName_;

    bool transitionEnd_ = false;
    bool firstChange_ = false;

#ifdef USE_IMGUI
    bool rebuildRequested_ = false;         // 次フレーム先頭でシーンを作り直すか
    std::function<void()> onSceneRebuilt_;  // 作り直した直後に呼ぶ処理
#endif // USE_IMGUI

    float clearTime_ = 0.0f;
    float hp_ = 0.0f;
    bool isGameOver_ = false; // 直前のゲームがゲームオーバーだったか（リザルト表示用）
};
} // namespace Hagine
