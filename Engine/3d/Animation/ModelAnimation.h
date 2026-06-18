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
class ModelAnimation {
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
    /// <param name="roop">ループ再生フラグ</param>
    void Update(bool roop);

    /// <summary>
    /// アニメーション再生
    /// </summary>
    void PlayAnimation();

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
    /// private variants
    /// ===================================================

    std::unique_ptr<Animator> animator_; // アニメーター
    std::unique_ptr<Bone> bone_;         // ボーン
    std::unique_ptr<Skin> skin_;         // スキン
    std::string directorypath_;          // ディレクトリパス
    std::string filename_;               // ファイル名
    ModelData modelData_;                // モデルデータ
};
} // namespace Hagine
