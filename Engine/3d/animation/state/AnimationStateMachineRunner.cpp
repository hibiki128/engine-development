#include "AnimationStateMachineRunner.h"
#include <animation/Animator.h>
#include <animation/BlendSpace.h>
#include <asset/AssetPath.h>
#include <algorithm>

namespace Hagine {

namespace {
std::vector<AnimationStateMachineRunner *> &Instances()
{
    static std::vector<AnimationStateMachineRunner *> instances;
    return instances;
}
constexpr float kDefaultFade = 0.25f; // 登録時の切り替え時間（遷移ごとに上書きする）
} // namespace

const std::vector<AnimationStateMachineRunner *> &AnimationStateMachineRunner::GetInstances()
{
    return Instances();
}

AnimationStateMachineRunner::AnimationStateMachineRunner()
{
    Instances().push_back(this);
}

AnimationStateMachineRunner::~AnimationStateMachineRunner()
{
    std::erase(Instances(), this);
}

std::string AnimationStateMachineRunner::BlendSpaceName(int stateId)
{
    return "sm:" + std::to_string(stateId);
}

bool AnimationStateMachineRunner::Initialize(Object3d *pObject, const std::string &assetName, const std::string &ownerName)
{
    pObject_ = pObject;
    assetName_ = assetName;
    ownerName_ = ownerName;
    asset_ = AnimationStateMachineLibrary::Get(assetName);
    builtRevision_ = -1;
    currentState_ = -1;
    values_.clear();
    if (!asset_ || !pObject_)
    {
        return false;
    }
    Rebuild();
    return true;
}

float AnimationStateMachineRunner::GetClipDuration(const std::string &file)
{
    auto it = durations_.find(file);
    if (it != durations_.end())
    {
        return it->second;
    }
    // Animator は読み込み結果をキャッシュしているので、再生用と二重には読まない
    Animator loader;
    loader.Initialize(AssetPath::ModelsRoot(file), file);
    const float duration = loader.GetAnimation().duration;
    durations_[file] = duration;
    return duration;
}

void AnimationStateMachineRunner::Rebuild()
{
    controller_.Initialize(pObject_);
    for (const AnimStateData &state : asset_->states)
    {
        if (state.kind == AnimStateKind::Clip)
        {
            if (!state.file.empty())
            {
                controller_.RegisterClip(state.file, state.file, state.loop, state.speed, kDefaultFade);
                GetClipDuration(state.file);
            }
            continue;
        }
        std::vector<BlendSpacePoint> points;
        for (const AnimBlendPoint &p : state.points)
        {
            if (p.file.empty())
            {
                continue;
            }
            controller_.RegisterClip(p.file, p.file, true, 1.0f, kDefaultFade);
            GetClipDuration(p.file);
            BlendSpacePoint point;
            point.clipName = p.file;
            point.position = {p.x, p.y};
            point.speed = p.speed;
            points.push_back(point);
        }
        controller_.RegisterBlendSpace(BlendSpaceName(state.id), static_cast<BlendSpaceMode>(state.blendMode), points, kDefaultFade);
        if (AnimationBlendSpace *pSpace = controller_.FindBlendSpace(BlendSpaceName(state.id)))
        {
            pSpace->SetPlaybackSpeed(state.speed);
        }
    }

    // パラメータ: 増えたものは初期値で、消えたものは捨てる（今の値は残す）
    std::map<std::string, float> values;
    for (const AnimParamDef &def : asset_->params)
    {
        auto it = values_.find(def.name);
        values[def.name] = (it != values_.end()) ? it->second : def.defaultValue;
    }
    values_ = std::move(values);

    builtRevision_ = asset_->GetRevision();

    // 登録し直したので今のステートを再生し直す（消えていたら最初のステートへ）
    const int state = asset_->FindState(currentState_) ? currentState_ : asset_->entryState;
    currentState_ = -1;
    Enter(state, 0.0f, true);
}

void AnimationStateMachineRunner::Enter(int stateId, float duration, bool immediate)
{
    const AnimStateData *pState = asset_->FindState(stateId);
    if (!pState)
    {
        return;
    }
    if (pState->kind == AnimStateKind::Clip)
    {
        if (!pState->file.empty())
        {
            // 遷移ごとの切り替え時間は、クリップの補間時間として登録し直して渡す
            controller_.RegisterClip(pState->file, pState->file, pState->loop, pState->speed, duration);
            if (controller_.GetCurrentClipName() == pState->file)
            {
                // 同じファイルへの遷移（一回きりのモーションのやり直しなど）は頭から
                controller_.SetTime(0.0f);
                controller_.SetPaused(false);
            }
            else if (immediate)
            {
                controller_.PlayImmediate(pState->file);
            }
            else
            {
                controller_.Play(pState->file);
            }
        }
    }
    else
    {
        const std::string name = BlendSpaceName(pState->id);
        if (AnimationBlendSpace *pSpace = controller_.FindBlendSpace(name))
        {
            pSpace->SetFadeDuration(immediate ? 0.0f : duration);
        }
        controller_.PlayBlendSpace(name, {GetValue(pState->paramX), GetValue(pState->paramY)});
    }
    currentState_ = stateId;
    stateTime_ = 0.0f;
}

void AnimationStateMachineRunner::ForceState(int stateId)
{
    if (!asset_)
    {
        return;
    }
    Enter(stateId, kDefaultFade, false);
}

float AnimationStateMachineRunner::GetNormalizedTime() const
{
    if (!asset_)
    {
        return 0.0f;
    }
    const AnimStateData *pState = asset_->FindState(currentState_);
    if (!pState)
    {
        return 0.0f;
    }
    const std::string &file = (pState->kind == AnimStateKind::Clip) ? pState->file
                              : (pState->points.empty() ? std::string() : pState->points.front().file);
    auto it = durations_.find(file);
    if (it == durations_.end() || it->second <= 0.0f)
    {
        return 0.0f;
    }
    return stateTime_ * (std::max)(pState->speed, 0.0f) / it->second;
}

float AnimationStateMachineRunner::GetValue(const std::string &name) const
{
    auto it = values_.find(name);
    return (it != values_.end()) ? it->second : 0.0f;
}

void AnimationStateMachineRunner::SetFloat(const std::string &name, float value)
{
    values_[name] = value;
}

void AnimationStateMachineRunner::SetBool(const std::string &name, bool value)
{
    values_[name] = value ? 1.0f : 0.0f;
}

void AnimationStateMachineRunner::SetTrigger(const std::string &name)
{
    values_[name] = 1.0f;
}

bool AnimationStateMachineRunner::CheckConditions(const AnimTransitionData &transition) const
{
    for (const AnimCondition &c : transition.conditions)
    {
        const float v = GetValue(c.param);
        switch (c.op)
        {
        case AnimConditionOp::Greater:
            if (!(v > c.value))
                return false;
            break;
        case AnimConditionOp::Less:
            if (!(v < c.value))
                return false;
            break;
        case AnimConditionOp::IsTrue:
        case AnimConditionOp::Triggered:
            if (v < 0.5f)
                return false;
            break;
        case AnimConditionOp::IsFalse:
            if (v >= 0.5f)
                return false;
            break;
        }
    }
    if (transition.hasExitTime && GetNormalizedTime() < transition.exitTime)
    {
        return false;
    }
    // 条件も待ち時間も無い遷移は、毎フレーム即座に抜けてしまうので1周だけは待つ
    if (transition.conditions.empty() && !transition.hasExitTime && GetNormalizedTime() < 1.0f)
    {
        return false;
    }
    return true;
}

void AnimationStateMachineRunner::ConsumeTriggers(const AnimTransitionData &transition)
{
    for (const AnimCondition &c : transition.conditions)
    {
        if (c.op == AnimConditionOp::Triggered)
        {
            values_[c.param] = 0.0f;
        }
    }
}

void AnimationStateMachineRunner::Update(float deltaTime)
{
    if (!pObject_ || !asset_)
    {
        return;
    }
    if (builtRevision_ != asset_->GetRevision())
    {
        Rebuild();
    }
    if (!asset_->FindState(currentState_))
    {
        Enter(asset_->entryState, 0.0f, true);
        if (currentState_ < 0)
        {
            return; // ステートが1つも無い
        }
    }

    stateTime_ += deltaTime;
    lastTransitionAge_ += deltaTime;

    const AnimStateData *pState = asset_->FindState(currentState_);
    if (pState && pState->kind == AnimStateKind::BlendSpace)
    {
        controller_.PlayBlendSpace(BlendSpaceName(pState->id), {GetValue(pState->paramX), GetValue(pState->paramY)});
    }

    // 遷移: 「どこからでも」を先に、次に今のステートから出る線を、並び順に調べて最初の1本
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const AnimTransitionData &t : asset_->transitions)
        {
            const bool fromAny = (t.from == AnimationStateMachineAsset::kAnyState);
            if ((pass == 0) != fromAny)
            {
                continue;
            }
            if (!fromAny && t.from != currentState_)
            {
                continue;
            }
            if (fromAny && t.to == currentState_)
            {
                continue; // 今いるステートへの「どこからでも」は繰り返し入り直してしまうので見ない
            }
            if (!CheckConditions(t))
            {
                continue;
            }
            ConsumeTriggers(t);
            lastTransition_ = t.id;
            lastTransitionAge_ = 0.0f;
            Enter(t.to, t.duration, false);
            return;
        }
    }
}

} // namespace Hagine
