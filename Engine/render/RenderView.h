#pragma once
#include <cstdint>

namespace Hagine {

/// <summary>
/// 今どのカメラの絵を描いているか（0 = メインのシーン描画、1〜 = カメラビュー窓）。
///
/// オブジェクトの変換行列・ライトのカメラ位置・スカイボックスの行列はアップロード用の
/// 定数バッファ1つに毎回書いてから描いている。同じフレームに別のカメラでもう一度描くと、
/// GPU が実行する時点では後から書いた値しか残らず、どちらの絵も同じになってしまう。
/// そこで描く側はこの番号ごとに別の定数バッファを使う。
/// </summary>
class RenderView
{
  public:
    static constexpr int kMaxViews = 5; ///< メイン1＋カメラビュー窓4

    /// <summary>今描いているビューの番号</summary>
    static int Current() { return current_; }

    /// <summary>カメラビュー窓の描画中か（メインではない）</summary>
    static bool IsExtra() { return current_ != 0; }

    /// <summary>描くビューを切り替える（描き終わったら 0 に戻すこと）</summary>
    static void SetCurrent(int index) { current_ = (index >= 0 && index < kMaxViews) ? index : 0; }

    /// <summary>
    /// カメラビュー窓が1つでも開いているか。GPU パーティクルはメインのカメラの視界で
    /// 描く粒を間引いているので、開いている間は間引きを止める（別のカメラから見えなくなるため）
    /// </summary>
    static bool IsExtraViewsActive() { return extraViewsActive_; }
    static void SetExtraViewsActive(bool active) { extraViewsActive_ = active; }

    /// <summary>
    /// カメラビュー窓で使う「全面が一番奥」の深度（SRV番号）。ソフトパーティクルの深度の代わりに渡し、
    /// メインの画面の深度で粒が削られないようにする（0 なら未作成）
    /// </summary>
    static uint32_t GetFarDepthSrvIndex() { return farDepthSrvIndex_; }
    static void SetFarDepthSrvIndex(uint32_t index) { farDepthSrvIndex_ = index; }

  private:
    static inline int current_ = 0;
    static inline bool extraViewsActive_ = false;
    static inline uint32_t farDepthSrvIndex_ = 0;
};

} // namespace Hagine
