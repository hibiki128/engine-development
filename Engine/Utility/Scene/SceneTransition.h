#pragma once
#include "Sprite.h"
#include "memory"
#include "vector"
namespace Hagine {
class SceneTransition {
  private:

    SceneTransition() = default;
    ~SceneTransition() = default;
    SceneTransition(SceneTransition &) = delete;
    SceneTransition &operator=(SceneTransition &) = delete;

  public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns></returns>
      static SceneTransition* GetInstance() {
        static SceneTransition instance;
          return &instance;
    }

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 更新
    /// </summary>
    void Update();

    /// <summary>
    /// 描画
    /// </summary>
    void Draw();

    void Debug();

    /// <summary>
    /// セット
    /// </summary>
    /// <param name="start"></param>
    void SetFadeInStart(bool start) { fadeInStart_ = start; }
    void SetFadeOutStart(bool start) { fadeOutStart_ = start; }
    void SetFadeInFinish(bool finish) { fadeInFinish_ = finish; }
    void SetUseTransition(bool use) { useTransition_ = use; }

    /// <summary>
    /// getter
    /// </summary>
    /// <returns></returns>
    bool IsEnd() { return isEnd_; }
    bool FadeInFinish() { return fadeInFinish_; }
    bool FadeInStart() { return fadeInStart_; }
    bool GetUseTransition() const { return useTransition_; }

    /// <summary>
    /// リセット
    /// </summary>
    void Reset();

  private:
    /// <summary>
    /// フェードアップデート
    /// </summary>
    void FadeUpdate();

    /// <summary>
    /// フェードイン
    /// </summary>
    void FadeIn();

    /// <summary>
    /// フェードアウト
    /// </summary>
    void FadeOut();

    /// <summary>
    /// デフォルトフェードイン
    /// </summary>
    void DefaultFadeIn();

    /// <summary>
    /// デフォルトフェードアウト
    /// </summary>
    void DefaultFadeOut();

    void ReverseFadeIn();

    void ReverseFadeOut();

    /// <summary>
    /// インスタンシング用の変換行列更新
    /// </summary>
    void UpdateTransitionInstances();

  private:
    // フェードの持続時間
    float duration_ = 0.0f;
    // 経過時間カウンター
    float counter_ = 0.0f;

    std::unique_ptr<Sprite> sprite_ = nullptr;

    // インスタンシング用Sprite
    std::unique_ptr<Sprite> transitionSprite_ = nullptr;
    int totalInstances_;
    int rows_;
    int cols_;

    // 各インスタンスのサイズを保存する配列
    std::vector<std::vector<float>> instanceSizes_;

    Vector2 spPos_ = {0.0f, 0.0f};

    bool fadeInStart_ = false;
    bool fadeOutStart_ = false;
    bool fadeInFinish_ = false;
    bool fadeOutFinish_ = false;
    bool isEnd_ = false;
    bool useTransition_ = true; // デフォルトはトランジションを使用
};
} // namespace Hagine
