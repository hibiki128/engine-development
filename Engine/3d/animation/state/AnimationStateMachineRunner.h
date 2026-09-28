#pragma once
#include "AnimationStateMachine.h"
#include <animation/AnimationController.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Hagine {
class Object3d;

/// <summary>
/// アニメーションのステートマシンを動かすもの（キャラ1体に1つ）。
/// データ（AnimationStateMachineAsset）を見て、毎フレーム遷移の条件を調べ、
/// 中に持った AnimationController で再生を切り替える。
///
/// 使い方:
///   runner.Initialize(pObject3d, "TitleCharacter", "cube_1");
///   runner.SetFloat("speed", 3.0f);   // ゲーム側は値を渡すだけ
///   runner.SetTrigger("shot");
///   runner.Update(dt);                // アニメーションの更新より前に呼ぶ
/// エディタでデータを直すと、次の Update で登録し直して反映する
/// </summary>
class AnimationStateMachineRunner
{
  public:
    AnimationStateMachineRunner();
    ~AnimationStateMachineRunner();
    AnimationStateMachineRunner(const AnimationStateMachineRunner &) = delete;
    AnimationStateMachineRunner &operator=(const AnimationStateMachineRunner &) = delete;

    /// <summary>
    /// 初期化する
    /// </summary>
    /// <param name="pObject">動かす Object3d（スキンの入った gltf）</param>
    /// <param name="assetName">ステートマシンのファイル名（拡張子なし）</param>
    /// <param name="ownerName">エディタに出す持ち主の名前</param>
    /// <returns>bool: ファイルが読めたら true</returns>
    bool Initialize(Object3d *pObject, const std::string &assetName, const std::string &ownerName);

    /// <summary>
    /// 遷移を調べて再生を進める。アニメーションの更新（Object3d::AnimationUpdate）より前に呼ぶ
    /// </summary>
    /// <param name="deltaTime">経過時間（秒）</param>
    void Update(float deltaTime);

    /// ---- パラメータ（ゲーム側から渡す）----
    void SetFloat(const std::string &name, float value);
    void SetBool(const std::string &name, bool value);
    void SetTrigger(const std::string &name);
    float GetValue(const std::string &name) const;

    /// <summary>
    /// ステートへ直接切り替える（エディタの「ここへ」ボタン用）
    /// </summary>
    void ForceState(int stateId);

    /// ===================================================
    /// Getter
    /// ===================================================
    int GetCurrentStateId() const { return currentState_; }
    float GetStateTime() const { return stateTime_; }
    float GetNormalizedTime() const;
    int GetLastTransitionId() const { return lastTransition_; }
    float GetLastTransitionAge() const { return lastTransitionAge_; }
    const std::string &GetAssetName() const { return assetName_; }
    const std::string &GetOwnerName() const { return ownerName_; }
    const std::shared_ptr<AnimationStateMachineAsset> &GetAsset() const { return asset_; }
    AnimationController &GetController() { return controller_; }

    /// <summary>
    /// 今動いているステートマシンの一覧（エディタが実行中の様子を見るのに使う）
    /// </summary>
    static const std::vector<AnimationStateMachineRunner *> &GetInstances();

  private:
    void Rebuild();
    void Enter(int stateId, float duration, bool immediate);
    bool CheckConditions(const AnimTransitionData &transition) const;
    void ConsumeTriggers(const AnimTransitionData &transition);
    float GetClipDuration(const std::string &file);
    static std::string BlendSpaceName(int stateId);

    Object3d *pObject_ = nullptr;
    std::shared_ptr<AnimationStateMachineAsset> asset_;
    std::string assetName_;
    std::string ownerName_;
    AnimationController controller_;
    int builtRevision_ = -1;
    int currentState_ = -1;
    float stateTime_ = 0.0f;
    std::map<std::string, float> values_;    ///< パラメータの今の値
    std::map<std::string, float> durations_; ///< ファイル → 1周の長さ（秒）
    int lastTransition_ = -1;
    float lastTransitionAge_ = 0.0f;
};

} // namespace Hagine
