#pragma once
#include "Sprite.h"
#include "type/Vector4.h"
#include "memory"
#include "random"
#include "vector"
namespace Hagine {

/// <summary>
/// シーンの切り替えを覆い隠す幕。
///
/// 画面を正六角形で敷き詰めておき、1枚ずつ大きくして埋めていく。
/// 埋まる順番と色はマスごとにランダムなので、切り替えのたびに違う模様になる。
///
/// 六角形は1枚のマスク画像（debug/hexagon.png）を敷き詰めて出している。
/// 色はスプライトのマテリアル色なので、色ごとにスプライトを1枚ずつ持ち、
/// 同じ色のマスをまとめてインスタンシングで描く（色数ぶんの描画で済む）。
/// 使う色はゲーム側から SetColors() で差し替えられる。
/// </summary>
class SceneTransition
{
  public:
    SceneTransition() = default;
    ~SceneTransition() = default;
    SceneTransition(SceneTransition &) = delete;
    SceneTransition &operator=(SceneTransition &) = delete;

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
    /// 幕に使う色を差し替える（ゲーム側の色マスタに合わせるとき用）。
    /// マスへの割り振りは次の Reset() から新しい色で行われる
    /// </summary>
    /// <param name="colors">使う色。空なら既定色のまま</param>
    void SetColors(const std::vector<Vector4> &colors);

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

    /// <summary>
    /// 画面を覆う六角形のマスを敷き直す（大きさを変えたときにも呼ぶ）
    /// </summary>
    void BuildCells();

    /// <summary>
    /// マスごとの色と、埋まり始めるまでの遅れを引き直す。
    /// 切り替えのたびに呼ぶので、毎回ちがう模様になる
    /// </summary>
    void ShuffleCells();

    /// <summary>
    /// 経過時間からマスの大きさを決める
    /// </summary>
    /// <param name="localTime">そのマスが伸び始めてからの時間（秒）</param>
    /// <returns>float: 一辺（高さ）の長さ</returns>
    float CalcCellSize(float localTime) const;

  private:
    /// <summary>敷き詰めた六角形1マスぶん</summary>
    struct Cell
    {
        Vector2 center{};      // 画面上の中心
        float delay = 0.0f;    // 埋まり始めるまでの遅れ（秒）
        int colorIndex = 0;    // colors_ の何番目の色か
        float size = 0.0f;     // いまの大きさ（高さ）
    };

    // フェードの持続時間
    float duration_ = 0.0f;
    // 経過時間カウンター
    float counter_ = 0.0f;

    std::unique_ptr<Sprite> sprite_ = nullptr;

    // 六角形のマス。敷き詰めた順に並んでいる
    std::vector<Cell> cells_;
    // 色ごとのスプライト（同じ色のマスをまとめて1回で描く）
    std::vector<std::unique_ptr<Sprite>> colorSprites_;
    // 色ごとに、その色を使っているマスの番号
    std::vector<std::vector<int>> cellsByColor_;

    // 幕に使う色（既定はゲームの4色。SetColors で差し替えられる）
    std::vector<Vector4> colors_ = {
        {0.90f, 0.25f, 0.25f, 1.0f}, // 赤
        {0.25f, 0.50f, 0.95f, 1.0f}, // 青
        {0.30f, 0.80f, 0.40f, 1.0f}, // 緑
        {0.95f, 0.85f, 0.30f, 1.0f}  // 黄
    };

    // 埋まる順番と色をばらすための乱数
    std::mt19937 random_{std::random_device{}()};

    // 六角形1マスの高さ。幅はこの sqrt(3)/2 倍になる（正六角形なので）
    float cellSize_ = 96.0f;
    // 最後のマスが埋まり始めるまでの遅れ（秒）。大きいほどばらけて見える
    float maxDelay_ = 0.45f;

    Vector2 spPos_ = {0.0f, 0.0f};

    bool fadeInStart_ = false;
    bool fadeOutStart_ = false;
    bool fadeInFinish_ = false;
    bool fadeOutFinish_ = false;
    bool isEnd_ = false;
    bool useTransition_ = true; // デフォルトはトランジションを使用
};
} // namespace Hagine
