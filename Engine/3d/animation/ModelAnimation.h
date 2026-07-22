#pragma once
#include "Animator.h"
#include "Bone.h"
#include "Skin.h"
#include <memory>

/// <summary>
/// モデルアニメーション管理クラス
/// Animator、Bone、Skinを統合してモデルのアニメーションを制御する
/// </summary>
namespace Hagine {
class ModelAnimation
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="directorypath">ディレクトリパス</param>
    /// <param name="filename">ファイル名</param>
    void Initialize(const std::string &directorypath, const std::string &filename);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="loop">ループ再生フラグ</param>
    void Update(bool loop);

    /// <summary>
    /// アニメーション再生
    /// </summary>
    void PlayAnimation();

    /// <summary>
    /// レイヤーアニメーション（体の一部だけ差し替えるモーション）を再生する
    /// 指定ジョイント以下だけがこのアニメーションで上書きされ、残りは通常の
    /// アニメーションのまま再生される（走りながら撃つ等の上半身差し替え用）。
    /// 同じファイルを再指定した場合は先頭から再生し直す
    /// </summary>
    /// <param name="directorypath">ディレクトリパス</param>
    /// <param name="filename">アニメーションファイル名</param>
    /// <param name="maskRootJoint">上書き範囲の根になるジョイント名（この子孫が対象）</param>
    /// <param name="loop">ループ再生するか（false なら再生終了で自動的に解除する）</param>
    /// <param name="fadeDuration">レイヤーの出入りにかける時間（秒）</param>
    void PlayLayerAnimation(const std::string &directorypath, const std::string &filename,
                            const std::string &maskRootJoint, bool loop, float fadeDuration);

    /// <summary>
    /// レイヤーアニメーションを解除する（フェードアウトしてから停止）
    /// </summary>
    /// <param name="fadeDuration">フェードアウトにかける時間（秒）</param>
    void StopLayerAnimation(float fadeDuration);

    /// <summary>
    /// レイヤーアニメーションが再生中か（フェードアウト中は false）
    /// </summary>
    /// <returns>bool: 再生中なら true</returns>
    bool IsLayerPlaying() const { return layerAnimator_ != nullptr && layerTargetWeight_ > 0.0f; }

    /// <summary>
    /// レイヤー用アニメーターを取得（未再生なら nullptr）
    /// </summary>
    /// <returns>Animator*: レイヤー用アニメーター</returns>
    Animator *GetLayerAnimator() { return layerAnimator_.get(); }

    /// <summary>
    /// Getter
    /// </summary>
    Skeleton GetSkeletonData() { return bone_->GetSkeleton(); }
    Animator *GetAnimator() { return animator_.get(); }
    Bone *GetBone() { return bone_.get(); }
    Skin *GetSkin() { return skin_.get(); }
    bool IsFinish() { return animator_->IsFinish(); }

    /// <summary>
    /// Setter
    /// </summary>
    void SetModelData(ModelData modelData) { modelData_ = modelData; }
    void SetIsAnimation(bool anime) { animator_->SetIsAnimation(anime); }
    void SetSpeed(float speed) { animator_->SetSpeed(speed); }
    void SetBlendDuration(float duration) { animator_->SetBlendDuration(duration); }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// レイヤーの重みをフェード目標へ進め、不要になったレイヤーを解放する
    /// </summary>
    void UpdateLayerWeight();

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    std::unique_ptr<Animator> animator_; // アニメーター
    std::unique_ptr<Bone> bone_;         // ボーン
    std::unique_ptr<Skin> skin_;         // スキン
    std::string directorypath_;          // ディレクトリパス
    std::string filename_;               // ファイル名
    ModelData modelData_;                // モデルデータ

    // ─── レイヤーアニメーション（上半身だけ差し替える等の部分再生）───
    std::unique_ptr<Animator> layerAnimator_; // レイヤー用アニメーター（未使用時はnullptr）
    std::vector<uint8_t> layerMask_;          // 上書き対象のジョイントマスク
    std::string layerFilename_;               // レイヤーで再生中のファイル名
    std::string layerMaskRootJoint_;          // マスクの根ジョイント名
    float layerWeight_ = 0.0f;                // 現在のレイヤー適用度（0〜1）
    float layerTargetWeight_ = 0.0f;          // 目標のレイヤー適用度（解除時は0）
    float layerFadeDuration_ = 0.1f;          // フェードにかける時間（秒）
    bool layerLoop_ = false;                  // レイヤーをループ再生するか
};
} // namespace Hagine
