#include "ModelAnimation.h"
#include <algorithm>
#include <Frame.h>

namespace Hagine {
void ModelAnimation::Initialize(const std::string &directorypath, const std::string &filename)
{
    directorypath_ = directorypath;
    filename_ = filename;

    // 各構成要素を生成
    animator_ = std::make_unique<Animator>();
    bone_ = std::make_unique<Bone>();
    skin_ = std::make_unique<Skin>();

    // アニメーターの初期化（アニメーションファイルの読み込み等）
    animator_->Initialize(directorypath_, filename_);

    // モデルがボーン情報を持っている場合のみ初期化処理を実行
    if (modelData_.hasBones)
    {
        bone_->Initialize(modelData_);
        skin_->Initialize(bone_->GetSkeleton(), modelData_);
    }
}

void ModelAnimation::Update(bool loop)
{
    // アニメーションデータがある場合は、現在のアニメーション時間を更新
    if (modelData_.hasAnimations)
    {
        // ループ設定に基づいてアニメーションの時間を進める
        animator_->Update(loop);
    }
    // ボーン情報がある場合は、ボーン階層とスキン（頂点ウェイト）を最新状態に更新
    if (modelData_.hasBones)
    {
        // レイヤー（上半身だけ差し替える等）の時間と重みを進める
        if (layerAnimator_)
        {
            layerAnimator_->Update(layerLoop_);
        }
        UpdateLayerWeight();

        // 現在のアニメーションデータと再生時間をボーンに適用し、階層行列を再計算。
        // 補間中のみ合成ポーズの一時オブジェクトを生成し、非補間時は参照渡しにして
        // 毎フレームのアニメーション全体（全ノード・全キーフレーム）のコピーを避ける。
        Animation blended;
        const bool isBlending = animator_->IsBlending();
        if (isBlending)
        {
            blended = animator_->GetCurrentAnimation();
        }
        const Animation &baseAnimation = isBlending ? blended : animator_->GetCurrentAnimationRef();

        if (layerAnimator_ && layerWeight_ > 0.0f)
        {
            // レイヤー側も補間中は合成ポーズを作る（コンボの段の繋ぎ）
            Animation layerBlended;
            const bool isLayerBlending = layerAnimator_->IsBlending();
            if (isLayerBlending)
            {
                layerBlended = layerAnimator_->GetCurrentAnimation();
            }
            const Animation &layerAnimation = isLayerBlending ? layerBlended : layerAnimator_->GetCurrentAnimationRef();

            bone_->UpdateLayered(baseAnimation, animator_->GetAnimationTime(),
                                 layerAnimation, layerAnimator_->GetAnimationTime(),
                                 layerMask_, layerWeight_);
        }
        else
        {
            bone_->Update(baseAnimation, animator_->GetAnimationTime());
        }

        // 計算されたボーン行列を元に、シェーダーに送るパレット行列を更新
        skin_->Update(bone_->GetSkeleton());
    }
}

void ModelAnimation::PlayLayerAnimation(const std::string &directorypath, const std::string &filename,
                                        const std::string &maskRootJoint, bool loop, float fadeDuration)
{
    // ボーンが無いモデル・マスク指定なしではレイヤーを使えない
    if (!modelData_.hasBones || maskRootJoint.empty())
    {
        return;
    }

    layerLoop_ = loop;
    layerFadeDuration_ = (fadeDuration > 0.0f) ? fadeDuration : 0.0f;
    layerTargetWeight_ = 1.0f;

    // マスクは根ジョイントが変わったときだけ作り直す
    if (layerMaskRootJoint_ != maskRootJoint || layerMask_.empty())
    {
        layerMaskRootJoint_ = maskRootJoint;
        layerMask_ = bone_->MakeSubtreeMask(maskRootJoint);
    }

    if (layerAnimator_)
    {
        if (layerFilename_ == filename)
        {
            // 同じモーションの再指定は先頭へ巻き戻して再生し直す（連射での撃ち直し）
            layerAnimator_->SetAnimationTime(0.0f);
            layerAnimator_->SetIsAnimation(true);
        }
        else
        {
            // 別モーションへはアニメーター内で補間する（コンボの段が繋がって見える）
            layerAnimator_->BlendToAnimation(directorypath, filename, layerFadeDuration_);
            layerFilename_ = filename;
        }
        return;
    }

    layerAnimator_ = std::make_unique<Animator>();
    layerAnimator_->SetModelData(modelData_);
    layerAnimator_->Initialize(directorypath, filename);
    layerAnimator_->SetSpeed(animator_->GetSpeed());
    layerFilename_ = filename;
}

void ModelAnimation::StopLayerAnimation(float fadeDuration)
{
    if (!layerAnimator_)
    {
        return;
    }
    layerFadeDuration_ = (fadeDuration > 0.0f) ? fadeDuration : 0.0f;
    layerTargetWeight_ = 0.0f;
}

void ModelAnimation::UpdateLayerWeight()
{
    if (!layerAnimator_)
    {
        return;
    }

    // ループしないレイヤーは再生し終わったら自動で解除する（撃ち終わりで通常モーションへ戻す）
    if (!layerLoop_ && layerAnimator_->IsFinish())
    {
        layerTargetWeight_ = 0.0f;
    }

    // フェード時間ゼロなら即座に目標値へ
    if (layerFadeDuration_ <= 0.0f)
    {
        layerWeight_ = layerTargetWeight_;
    }
    else
    {
        const float step = Frame::DeltaTime() / layerFadeDuration_;
        if (layerWeight_ < layerTargetWeight_)
        {
            layerWeight_ = (std::min)(layerWeight_ + step, layerTargetWeight_);
        }
        else if (layerWeight_ > layerTargetWeight_)
        {
            layerWeight_ = (std::max)(layerWeight_ - step, layerTargetWeight_);
        }
    }

    // 完全にフェードアウトしたらアニメーターを解放する
    if (layerTargetWeight_ <= 0.0f && layerWeight_ <= 0.0f)
    {
        layerAnimator_.reset();
        layerFilename_.clear();
    }
}

void ModelAnimation::PlayAnimation()
{
    // アニメーションを有効化し、時間を先頭（0.0f）にリセット
    animator_->SetIsAnimation(true);
    animator_->SetAnimationTime(0.0f);
}
} // namespace Hagine
