#include "SceneTransition.h"
#include "DirectXCommon.h"
#include "Easing.h"
#include "frame/Frame.h"
#include <graphics/srv/SrvManager.h>
#include <algorithm>

namespace Hagine {
namespace {

/// <summary>
/// 読み込みで止まったフレームは経過時間が大きくなる。そのまま進めると演出が飛ぶので、上限を設ける
/// </summary>
constexpr float kMaxStep = 1.0f / 30.0f;

/// <summary>0〜1 の進み具合にイージングを掛ける</summary>
float Ease(EasingType type, float t)
{
    return ApplyEasing<float>(type, 0.0f, 1.0f, std::clamp(t, 0.0f, 1.0f), 1.0f);
}

/// <summary>前半/後半の中での時間（0〜1）から、その幕の進み具合を求める</summary>
float LayerProgress(const TransitionLayer &layer, float phaseT)
{
    const float span = (std::max)(layer.end - layer.start, 1e-4f);
    return Ease(layer.easing, (phaseT - layer.start) / span);
}

/// <summary>下の画面の崩し方に量を掛ける</summary>
TransitionSceneFx ScaleSceneFx(const TransitionSceneFx &fx, float amount)
{
    TransitionSceneFx out = fx;
    const float a = Ease(fx.easing, amount);
    out.mosaic *= a;
    out.blur *= a;
    out.swirl *= a;
    out.zoom *= a;
    out.zoomBlur *= a;
    out.rotate *= a;
    out.chroma *= a;
    out.desaturate *= a;
    out.brightness *= a;
    out.shake *= a;
    out.wave *= a;
    return out;
}

} // namespace

void SceneTransition::Finalize()
{
    renderer_.Finalize();
}

void SceneTransition::Initialize()
{
    library_.Load();
    activePreset_ = library_.Resolve("", "", "") ? *library_.Resolve("", "", "") : TransitionPreset{};
    renderer_.Initialize(DirectXCommon::GetInstance(), SrvManager::GetInstance());

    coverTime_ = holdTime_ = revealTime_ = elapsed_ = 0.0f;
    fadeInFinish_ = false;
    fadeOutFinish_ = false;
    fadeInStart_ = false;
    fadeOutStart_ = false;
    isEnd_ = false;
    useTransition_ = true;
}

void SceneTransition::SetColors(const std::vector<Vector4> &colors)
{
    if (colors.empty())
    {
        return;
    }
    TransitionPreset *preset = library_.Find("六角形（4色）");
    if (!preset)
    {
        return;
    }
    for (TransitionLayer &layer : preset->cover.layers)
    {
        if (layer.fill != TransitionFill::Palette)
        {
            continue;
        }
        layer.paletteCount = static_cast<int>((std::min)(colors.size(), layer.palette.size()));
        for (int i = 0; i < layer.paletteCount; ++i)
        {
            layer.palette[static_cast<size_t>(i)] = colors[static_cast<size_t>(i)];
        }
    }
}

void SceneTransition::Prepare(const std::string &fromScene, const std::string &toScene, const std::string &presetName)
{
    const std::string name = presetName.empty() ? pendingPreset_ : presetName;
    pendingPreset_.clear();
    if (const TransitionPreset *preset = library_.Resolve(name, fromScene, toScene))
    {
        activePreset_ = *preset;
    }
    // 切り替えの演出が始まるので、エディタのプレビューは止める
    previewing_ = false;
    previewPlaying_ = false;
}

bool SceneTransition::PreviewAt(const std::string &presetName, float time)
{
    const TransitionPreset *preset = library_.Find(presetName);
    if (!preset)
    {
        return false;
    }
    for (size_t i = 0; i < library_.presets.size(); ++i)
    {
        if (library_.presets[i].name == presetName)
        {
            selectedPreset_ = static_cast<int>(i);
        }
    }
    activePreset_ = *preset;
    previewing_ = true;
    previewPlaying_ = false;
    previewTime_ = time;
    return true;
}

void SceneTransition::Reset()
{
    coverTime_ = holdTime_ = revealTime_ = elapsed_ = 0.0f;
    fadeInFinish_ = false;
    fadeOutFinish_ = false;
    fadeInStart_ = false;
    fadeOutStart_ = false;
    isEnd_ = false;
    renderer_.ClearSnapshot();
}

void SceneTransition::Update()
{
    // トランジションを使用しない場合は即座に完了状態にする
    if (!useTransition_)
    {
        if (fadeInStart_ && !fadeInFinish_)
        {
            fadeInFinish_ = true;
        }
        if (fadeOutStart_ && !fadeOutFinish_)
        {
            fadeOutFinish_ = true;
        }
        if (fadeInFinish_ && fadeOutFinish_)
        {
            isEnd_ = true;
            fadeInStart_ = false;
            fadeOutStart_ = false;
        }
        return;
    }

    const float dt = std::clamp(Frame::UnscaledDeltaTime(), 0.0f, kMaxStep);
    elapsed_ += dt;

    // 前半: 覆う
    if (fadeInStart_ && !fadeInFinish_)
    {
        coverTime_ += dt;
        if (coverTime_ >= activePreset_.cover.duration)
        {
            coverTime_ = activePreset_.cover.duration;
            fadeInFinish_ = true;
        }
    }
    // 後半: 少し待ってから明ける（シーンの切り替えが済んだ合図＝fadeOutStart_ の後）
    if (fadeOutStart_ && fadeInFinish_ && !fadeOutFinish_)
    {
        if (holdTime_ < activePreset_.hold)
        {
            holdTime_ += dt;
        }
        else
        {
            revealTime_ += dt;
            if (revealTime_ >= activePreset_.reveal.duration)
            {
                revealTime_ = activePreset_.reveal.duration;
                fadeOutFinish_ = true;
            }
        }
    }

    // トランジションが終了したら、終了フラグを立てる
    if (fadeInFinish_ && fadeOutFinish_)
    {
        isEnd_ = true;
        fadeInStart_ = false;
        fadeOutStart_ = false;
    }

    // エディタのプレビュー（実際の切り替え中は出さない）
    if (previewing_ && previewPlaying_)
    {
        previewTime_ += dt;
        const float total = activePreset_.TotalDuration();
        if (previewTime_ > total + 0.4f)
        {
            if (previewLoop_)
            {
                previewTime_ = 0.0f;
            }
            else
            {
                previewTime_ = total;
                previewPlaying_ = false;
            }
        }
    }
}

SceneTransition::Stage SceneTransition::CurrentStage() const
{
    if (isEnd_)
    {
        return Stage::Idle;
    }
    if (fadeOutStart_ && fadeInFinish_)
    {
        return (holdTime_ < activePreset_.hold) ? Stage::Holding : Stage::Revealing;
    }
    if (fadeInFinish_)
    {
        return Stage::Covered;
    }
    if (fadeInStart_)
    {
        return Stage::Covering;
    }
    return Stage::Idle;
}

SceneTransition::Stage SceneTransition::PreviewStage(float time, float &phaseTime) const
{
    const TransitionPreset &preset = activePreset_;
    if (time < preset.cover.duration)
    {
        phaseTime = time;
        return Stage::Covering;
    }
    time -= preset.cover.duration;
    if (time < preset.hold)
    {
        phaseTime = time;
        return Stage::Holding;
    }
    time -= preset.hold;
    phaseTime = (std::min)(time, preset.reveal.duration);
    return Stage::Revealing;
}

bool SceneTransition::BuildFrame(const TransitionPreset &preset, Stage stage, float phaseTime, TransitionFrame &out)
{
    out = TransitionFrame{};
    switch (stage)
    {
    case Stage::Covering:
    case Stage::Covered:
    {
        const float t = (stage == Stage::Covered || preset.cover.duration <= 0.0f)
                            ? 1.0f
                            : phaseTime / preset.cover.duration;
        out.layers = &preset.cover.layers;
        for (size_t i = 0; i < preset.cover.layers.size() && i < TransitionPreset::kMaxLayers; ++i)
        {
            out.layerProgress[i] = LayerProgress(preset.cover.layers[i], t);
        }
        out.sceneFx = ScaleSceneFx(preset.cover.sceneFx, t);
        return true;
    }
    case Stage::Holding:
    case Stage::Revealing:
    {
        // 待っている間は「明ける直前」（後半の頭）の姿を出す
        const float t = (stage == Stage::Holding || preset.reveal.duration <= 0.0f)
                            ? ((stage == Stage::Holding) ? 0.0f : 1.0f)
                            : phaseTime / preset.reveal.duration;
        const std::vector<TransitionLayer> &layers = preset.RevealLayers();
        out.layers = &layers;
        out.flipInvert = (preset.revealMode == TransitionRevealMode::PassThrough);
        // 明けるときは覆ったときの動きを逆にたどる（進み具合 1 → 0）
        for (size_t i = 0; i < layers.size() && i < TransitionPreset::kMaxLayers; ++i)
        {
            out.layerProgress[i] = LayerProgress(layers[i], 1.0f - t);
        }
        out.sceneFx = ScaleSceneFx(preset.RevealSceneFx(), 1.0f - t);
        return true;
    }
    default:
        return false;
    }
}

void SceneTransition::Draw(ID3D12Resource *pTarget)
{
    if (!useTransition_ || !pTarget)
    {
        return;
    }

    Stage stage = CurrentStage();
    float phaseTime = 0.0f;
    float time = elapsed_;
    if (stage != Stage::Idle)
    {
        phaseTime = (stage == Stage::Covering) ? coverTime_ : (stage == Stage::Revealing) ? revealTime_ : holdTime_;
        // 切り替える直前の画面を「前の画面」として控えておく（幕を重ねる前に）
        if (stage == Stage::Covering || stage == Stage::Covered)
        {
            renderer_.CaptureSnapshot(pTarget);
        }
    }
    else if (previewing_)
    {
        stage = PreviewStage(previewTime_, phaseTime);
        time = previewTime_;
        if (stage == Stage::Covering)
        {
            renderer_.CaptureSnapshot(pTarget);
        }
    }
    else
    {
        return;
    }

    TransitionFrame frame;
    if (BuildFrame(activePreset_, stage, phaseTime, frame))
    {
        renderer_.Render(pTarget, frame, time);
    }
}

} // namespace Hagine
