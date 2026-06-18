#include "ModelAnimation.h"

namespace Hagine {
void ModelAnimation::Initialize(const std::string &directorypath, const std::string &filename) {
    directorypath_ = directorypath;
    filename_ = filename;
    
    // 各構成要素を生成
    animator_ = std::make_unique<Animator>();
    bone_ = std::make_unique<Bone>();
    skin_ = std::make_unique<Skin>();
    
    // アニメーターの初期化（アニメーションファイルの読み込み等）
    animator_->Initialize(directorypath_, filename_);

    // モデルがボーン情報を持っている場合のみ初期化処理を実行
    if (modelData_.hasBones) {
        bone_->Initialize(modelData_);
        skin_->Initialize(bone_->GetSkeleton(), modelData_);
    }
}

void ModelAnimation::Update(bool roop) {
    // アニメーションデータがある場合は、現在のアニメーション時間を更新
    if (modelData_.hasAnimations) {
        // ループ設定に基づいてアニメーションの時間を進める
        animator_->Update(roop);
    }
    // ボーン情報がある場合は、ボーン階層とスキン（頂点ウェイト）を最新状態に更新
    if (modelData_.hasBones) {
        // 現在のアニメーションデータと再生時間をボーンに適用し、階層行列を再計算
        bone_->Update(animator_->GetCurrentAnimation(), animator_->GetAnimationTime());
        // 計算されたボーン行列を元に、シェーダーに送るパレット行列を更新
        skin_->Update(bone_->GetSkeleton());
    }
}

void ModelAnimation::PlayAnimation() {
    // アニメーションを有効化し、時間を先頭（0.0f）にリセット
    animator_->SetIsAnimation(true);
    animator_->SetAnimationTime(0.0f);
}
} // namespace Hagine
