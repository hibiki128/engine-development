#include "Animator.h"
#include <Frame.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <cassert>
#include <myMath.h>
#include <set>

namespace Hagine {
std::unordered_map<std::string, Animation> Animator::animationCache_;

void Animator::Initialize(const std::string &directorypath, const std::string &filename) {
    // ディレクトリパスとファイル名を保存
    directorypath_ = directorypath;
    filename_ = filename;

    // 指定されたパスからアニメーションデータを読み込む
    currentAnimation_ = LoadAnimationFile(directorypath_, filename_);

    // 初期状態は補間なし、アニメーション時間は0から開始
    blendState_.isBlending = false;
    animationTime_ = 0.0f;
}

void Animator::Update(bool loop) {
    if (!isAnimation_)
        return;

    if (blendState_.isBlending) {
        UpdateBlend(loop);
    } else {
        UpdateSingle(loop);
    }
}

void Animator::UpdateBlend(bool loop) {
    // 補間の進行度を更新
    blendState_.blendTimer += Frame::DeltaTime();
    blendState_.blendFactor = blendState_.blendTimer / blendState_.blendDuration;

    // 補間が完了した場合
    if (blendState_.blendFactor >= 1.0f) {
        blendState_.blendFactor = 1.0f;
        blendState_.isBlending = false;

        // 補間先のアニメーションを現在のメインアニメーションに設定
        currentAnimation_ = blendState_.toAnimation;
        animationTime_ = blendState_.toAnimationTime;

        // ファイル情報を現在のものに更新
        directorypath_ = blendState_.toDirectoryPath;
        filename_ = blendState_.toFilename;

        // アニメーションの再生状態をリセット
        isAnimation_ = true;
        isFinish_ = false;

        // 補間完了直後に 1フレーム分進める
        animationTime_ += Frame::DeltaTime() * speed_;
        if (loop) {
            // ループ再生時は時間を正規化
            animationTime_ = std::fmod(animationTime_, currentAnimation_.duration);
        } else {
            // ループしない場合は時間をクランプ
            animationTime_ = std::min(animationTime_, currentAnimation_.duration);
            // 終了判定
            if (animationTime_ >= currentAnimation_.duration) {
                isFinish_ = true;
                isAnimation_ = false;
            }
        }

        return;
    }

    // 補間中の再生時間更新
    if (loop) {
        // ループ再生の場合
        blendState_.fromAnimationTime += Frame::DeltaTime() * speed_;
        blendState_.fromAnimationTime = std::fmod(blendState_.fromAnimationTime, blendState_.fromAnimation.duration);

        blendState_.toAnimationTime += Frame::DeltaTime() * speed_;
        blendState_.toAnimationTime = std::fmod(blendState_.toAnimationTime, blendState_.toAnimation.duration);
    } else {
        // ループしない場合
        if (blendState_.fromAnimationTime < blendState_.fromAnimation.duration) {
            blendState_.fromAnimationTime += Frame::DeltaTime() * speed_;
            blendState_.fromAnimationTime = std::min(blendState_.fromAnimationTime, blendState_.fromAnimation.duration);
        }

        if (blendState_.toAnimationTime < blendState_.toAnimation.duration) {
            blendState_.toAnimationTime += Frame::DeltaTime() * speed_;
            blendState_.toAnimationTime = std::min(blendState_.toAnimationTime, blendState_.toAnimation.duration);
        }
    }

    // 現在の表示用アニメーション時間を更新
    animationTime_ = blendState_.toAnimationTime;
}
void Animator::UpdateSingle(bool loop) {
    if (loop) {
        // ループアニメーションの場合
        isAnimation_ = true; // ループ時は常に再生状態を維持
        isFinish_ = false;

        animationTime_ += Frame::DeltaTime() * speed_;
        // 時間を正規化してループさせる
        animationTime_ = std::fmod(animationTime_, currentAnimation_.duration);

        // ボーンを持たないモデルの場合、ルートノードのアニメーションを適用
        if (!modelData_.hasBones) {
            NodeAnimation &rootNodeAnimation = currentAnimation_.nodeAnimations[modelData_.rootNode.name];
            Vector3 translate = CalculateValue(rootNodeAnimation.translate, animationTime_);
            Quaternion rotate = CalculateValue(rootNodeAnimation.rotate, animationTime_);
            Vector3 scale = CalculateValue(rootNodeAnimation.scale, animationTime_);
            localMatrix_ = MakeAffineMatrix(scale, rotate.ToEulerAngles(), translate);
        }
    } else {
        // ループしない場合、アニメーションが終了するまで進行
        if (animationTime_ < currentAnimation_.duration) {
            isFinish_ = false;
            animationTime_ += Frame::DeltaTime() * speed_;

            // ボーンを持たないモデルの場合のルートノード適用
            if (!modelData_.hasBones) {
                NodeAnimation &rootNodeAnimation = currentAnimation_.nodeAnimations[modelData_.rootNode.name];
                Vector3 translate = CalculateValue(rootNodeAnimation.translate, animationTime_);
                Quaternion rotate = CalculateValue(rootNodeAnimation.rotate, animationTime_);
                Vector3 scale = CalculateValue(rootNodeAnimation.scale, animationTime_);
                localMatrix_ = MakeAffineMatrix(scale, rotate.ToEulerAngles(), translate);
            }

            // 再生時間が総時間（duration）を超えたら停止
            if (animationTime_ >= currentAnimation_.duration) {
                animationTime_ = currentAnimation_.duration;
                isAnimation_ = false;
                isFinish_ = true;
            }
        }
    }
}

void Animator::BlendToAnimation(const Animation &newAnimation, float blendDuration) {
    if (&newAnimation == &currentAnimation_) {
        return; // 同じアニメーションなので何もしない
    }

    blendState_.fromAnimation = currentAnimation_;
    blendState_.fromAnimationTime = animationTime_;

    blendState_.toAnimation = newAnimation;
    blendState_.toAnimationTime = 0.0f;
    blendState_.blendDuration = blendDuration;
    blendState_.blendTimer = 0.0f;
    blendState_.blendFactor = 0.0f;
    blendState_.isBlending = true;

    isAnimation_ = true;
    isFinish_ = false;
}

void Animator::BlendToAnimation(const std::string &directoryPath, const std::string &filename, float blendDuration) {
    if (directoryPath == directorypath_ && filename == filename_ && !blendState_.isBlending) {
        return; // 同じファイルで補間中でない場合は何もしない
    }

    Animation newAnimation = LoadAnimationFile(directoryPath, filename);

    // 補間先のファイル情報を保存
    blendState_.toDirectoryPath = directoryPath;
    blendState_.toFilename = filename;

    // 現在補間中の場合は、現在の補間状態から新しいアニメーションへ切り替え
    if (blendState_.isBlending) {
        // 現在の補間結果を元アニメーションとして使用
        blendState_.fromAnimation = GetCurrentAnimation();
        blendState_.fromAnimationTime = animationTime_;
    } else {
        // 通常の切り替え
        blendState_.fromAnimation = currentAnimation_;
        blendState_.fromAnimationTime = animationTime_;
    }

    blendState_.toAnimation = newAnimation;
    blendState_.toAnimationTime = 0.0f;
    blendState_.blendDuration = blendDuration;
    blendState_.blendTimer = 0.0f;
    blendState_.blendFactor = 0.0f;
    blendState_.isBlending = true;

    isAnimation_ = true;
    isFinish_ = false;
}

Animation Animator::GetCurrentAnimation() const {
    if (!blendState_.isBlending) {
        return currentAnimation_;
    }

    // 補間中の場合は動的に生成されたアニメーションを返す
    Animation blendedAnimation;
    blendedAnimation.duration = blendState_.toAnimation.duration;
    blendedAnimation.nodeAnimations = GetBlendedNodeAnimations();
    return blendedAnimation;
}

void Animator::UpdateCurrentFileInfo(const std::string &directoryPath, const std::string &filename) {
    directorypath_ = directoryPath;
    filename_ = filename;
}

std::map<std::string, NodeAnimation> Animator::GetBlendedNodeAnimations() const {
    std::map<std::string, NodeAnimation> blendedAnimations;

    if (!blendState_.isBlending) {
        return currentAnimation_.nodeAnimations;
    }

    // 全てのノード名を収集
    std::set<std::string> allNodeNames;
    for (const auto &pair : blendState_.fromAnimation.nodeAnimations) {
        allNodeNames.insert(pair.first);
    }
    for (const auto &pair : blendState_.toAnimation.nodeAnimations) {
        allNodeNames.insert(pair.first);
    }

    for (const std::string &nodeName : allNodeNames) {
        NodeAnimation blendedNode;

        auto fromIt = blendState_.fromAnimation.nodeAnimations.find(nodeName);
        auto toIt = blendState_.toAnimation.nodeAnimations.find(nodeName);

        if (fromIt != blendState_.fromAnimation.nodeAnimations.end() &&
            toIt != blendState_.toAnimation.nodeAnimations.end()) {

            // 両方のアニメーションにノードが存在する場合
            const NodeAnimation &fromNode = fromIt->second;
            const NodeAnimation &toNode = toIt->second;

            // Translation
            if (!fromNode.translate.empty() && !toNode.translate.empty()) {
                Vector3 blendedTranslate = CalculateBlendedValue(
                    fromNode.translate, toNode.translate,
                    blendState_.fromAnimationTime, blendState_.toAnimationTime,
                    blendState_.blendFactor);

                KeyframeVector3 keyframe = {blendedTranslate, animationTime_};
                blendedNode.translate.push_back(keyframe);
            }

            // Rotation
            if (!fromNode.rotate.empty() && !toNode.rotate.empty()) {
                Quaternion blendedRotate = CalculateBlendedValue(
                    fromNode.rotate, toNode.rotate,
                    blendState_.fromAnimationTime, blendState_.toAnimationTime,
                    blendState_.blendFactor);

                KeyframeQuaternion keyframe = {blendedRotate, animationTime_};
                blendedNode.rotate.push_back(keyframe);
            }

            // Scale
            if (!fromNode.scale.empty() && !toNode.scale.empty()) {
                Vector3 blendedScale = CalculateBlendedValue(
                    fromNode.scale, toNode.scale,
                    blendState_.fromAnimationTime, blendState_.toAnimationTime,
                    blendState_.blendFactor);

                KeyframeVector3 keyframe = {blendedScale, animationTime_};
                blendedNode.scale.push_back(keyframe);
            }

        } else if (fromIt != blendState_.fromAnimation.nodeAnimations.end()) {
            // 補間元のアニメーションにのみ存在する場合
            const NodeAnimation &fromNode = fromIt->second;

            Vector3 defaultTranslate = {0.0f, 0.0f, 0.0f};
            Quaternion defaultRotate = {0.0f, 0.0f, 0.0f, 1.0f};
            Vector3 defaultScale = {1.0f, 1.0f, 1.0f};

            if (!fromNode.translate.empty()) {
                Vector3 fromTranslate = CalculateValue(fromNode.translate, blendState_.fromAnimationTime);
                Vector3 blendedTranslate = Lerp(fromTranslate, defaultTranslate, blendState_.blendFactor);

                KeyframeVector3 keyframe = {blendedTranslate, animationTime_};
                blendedNode.translate.push_back(keyframe);
            }

            if (!fromNode.rotate.empty()) {
                Quaternion fromRotate = CalculateValue(fromNode.rotate, blendState_.fromAnimationTime);
                Quaternion blendedRotate = Quaternion::Slerp(fromRotate, defaultRotate, blendState_.blendFactor);

                KeyframeQuaternion keyframe = {blendedRotate, animationTime_};
                blendedNode.rotate.push_back(keyframe);
            }

            if (!fromNode.scale.empty()) {
                Vector3 fromScale = CalculateValue(fromNode.scale, blendState_.fromAnimationTime);
                Vector3 blendedScale = Lerp(fromScale, defaultScale, blendState_.blendFactor);

                KeyframeVector3 keyframe = {blendedScale, animationTime_};
                blendedNode.scale.push_back(keyframe);
            }

        } else if (toIt != blendState_.toAnimation.nodeAnimations.end()) {
            // 補間先のアニメーションにのみ存在する場合
            const NodeAnimation &toNode = toIt->second;

            Vector3 defaultTranslate = {0.0f, 0.0f, 0.0f};
            Quaternion defaultRotate = {0.0f, 0.0f, 0.0f, 1.0f};
            Vector3 defaultScale = {1.0f, 1.0f, 1.0f};

            if (!toNode.translate.empty()) {
                Vector3 toTranslate = CalculateValue(toNode.translate, blendState_.toAnimationTime);
                Vector3 blendedTranslate = Lerp(defaultTranslate, toTranslate, blendState_.blendFactor);

                KeyframeVector3 keyframe = {blendedTranslate, animationTime_};
                blendedNode.translate.push_back(keyframe);
            }

            if (!toNode.rotate.empty()) {
                Quaternion toRotate = CalculateValue(toNode.rotate, blendState_.toAnimationTime);
                Quaternion blendedRotate = Slerp(defaultRotate, toRotate, blendState_.blendFactor);

                KeyframeQuaternion keyframe = {blendedRotate, animationTime_};
                blendedNode.rotate.push_back(keyframe);
            }

            if (!toNode.scale.empty()) {
                Vector3 toScale = CalculateValue(toNode.scale, blendState_.toAnimationTime);
                Vector3 blendedScale = Lerp(defaultScale, toScale, blendState_.blendFactor);

                KeyframeVector3 keyframe = {blendedScale, animationTime_};
                blendedNode.scale.push_back(keyframe);
            }
        }

        blendedAnimations[nodeName] = blendedNode;
    }

    return blendedAnimations;
}

Animation Animator::LoadAnimationFile(const std::string &directoryPath, const std::string &filename) {
    std::string filePath = directoryPath + "/" + filename;
    Animation animation;

    // キャッシュチェック
    auto it = animationCache_.find(filePath);
    if (it != animationCache_.end()) {
        return it->second;
    }

    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(filePath.c_str(), 0);
    if (!scene || scene->mNumAnimations == 0) {
        return animation;
    }

    aiAnimation *animationAssimp = scene->mAnimations[0];
    animation.duration = float(animationAssimp->mDuration / animationAssimp->mTicksPerSecond);

    // ノードアニメーションの読み込み
    for (uint32_t channelIndex = 0; channelIndex < animationAssimp->mNumChannels; ++channelIndex) {
        aiNodeAnim *nodeAnimationAssimp = animationAssimp->mChannels[channelIndex];
        NodeAnimation &nodeAnimation = animation.nodeAnimations[nodeAnimationAssimp->mNodeName.C_Str()];

        // Position
        for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumPositionKeys; ++keyIndex) {
            aiVectorKey &keyAssimp = nodeAnimationAssimp->mPositionKeys[keyIndex];
            KeyframeVector3 keyframe;
            keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond);
            keyframe.value = {-keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z};
            nodeAnimation.translate.push_back(keyframe);
        }

        // Rotation
        for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumRotationKeys; ++keyIndex) {
            aiQuatKey &keyAssimp = nodeAnimationAssimp->mRotationKeys[keyIndex];
            KeyframeQuaternion keyframe;
            keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond);
            keyframe.value = {keyAssimp.mValue.x, -keyAssimp.mValue.y, -keyAssimp.mValue.z, keyAssimp.mValue.w};
            nodeAnimation.rotate.push_back(keyframe);
        }

        // Scale
        for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumScalingKeys; ++keyIndex) {
            aiVectorKey &keyAssimp = nodeAnimationAssimp->mScalingKeys[keyIndex];
            KeyframeVector3 keyframe;
            keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond);
            keyframe.value = {keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z};
            nodeAnimation.scale.push_back(keyframe);
        }
    }

    animationCache_[filePath] = animation;
    return animation;
}

Vector3 Animator::CalculateValue(const std::vector<KeyframeVector3> &keyframes, float time) {
    assert(!keyframes.empty());
    if (keyframes.size() == 1 || time <= keyframes[0].time) {
        return keyframes[0].value;
    }

    for (size_t index = 0; index < keyframes.size() - 1; ++index) {
        size_t nextIndex = index + 1;
        if (keyframes[index].time <= time && time <= keyframes[nextIndex].time) {
            float t = (time - keyframes[index].time) / (keyframes[nextIndex].time - keyframes[index].time);
            return Lerp(keyframes[index].value, keyframes[nextIndex].value, t);
        }
    }
    return (*keyframes.rbegin()).value;
}

Quaternion Animator::CalculateValue(const std::vector<KeyframeQuaternion> &keyframes, float time) {
    assert(!keyframes.empty());
    if (keyframes.size() == 1 || time <= keyframes[0].time) {
        return keyframes[0].value;
    }

    for (size_t index = 0; index < keyframes.size() - 1; ++index) {
        size_t nextIndex = index + 1;
        if (keyframes[index].time <= time && time <= keyframes[nextIndex].time) {
            float t = (time - keyframes[index].time) / (keyframes[nextIndex].time - keyframes[index].time);
            return Slerp(keyframes[index].value, keyframes[nextIndex].value, t);
        }
    }
    return (*keyframes.rbegin()).value;
}

Vector3 Animator::CalculateBlendedValue(
    const std::vector<KeyframeVector3> &fromKeyframes,
    const std::vector<KeyframeVector3> &toKeyframes,
    float fromTime, float toTime, float blendFactor) const {

    Vector3 fromValue = CalculateValue(fromKeyframes, fromTime);
    Vector3 toValue = CalculateValue(toKeyframes, toTime);
    return Lerp(fromValue, toValue, blendFactor);
}

Quaternion Animator::CalculateBlendedValue(
    const std::vector<KeyframeQuaternion> &fromKeyframes,
    const std::vector<KeyframeQuaternion> &toKeyframes,
    float fromTime, float toTime, float blendFactor) const {

    Quaternion fromValue = CalculateValue(fromKeyframes, fromTime);
    Quaternion toValue = CalculateValue(toKeyframes, toTime);
    return Slerp(fromValue, toValue, blendFactor);
}
} // namespace Hagine
