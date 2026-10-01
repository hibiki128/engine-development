#include "PlayModeManager.h"
#ifdef USE_IMGUI
#include "SpriteManager.h"
#include <format>
#include <imgui.h>
#include <light/LightGroup.h>
#include <object/base/BaseObjectManager.h>
#include <scene/SceneManager.h>
#include <utility/debug/imgui/ImGuiExtras.h> // 再生中インジケータ (imspinner)
#include <utility/debug/imgui/DebugUIHelper.h>
#include <utility/debug/imgui/ImGuiNotification.h>

namespace Hagine {
namespace {

/// <summary>
/// スナップショットに無いキーを null（＝削除）として足した JSON を作る。
///
/// RestoreUndoState は差分適用なので、スナップショットに書いていないものには触れない。
/// シーンを作り直すと JSON から「保存済みのオブジェクト」が復活するため、
/// 「再生を押した時点では消していた物」がそのまま残ってしまう。それを消すための補正。
/// </summary>
/// <param name="baseline">再生前のスナップショット</param>
/// <param name="current">作り直した直後の状態</param>
/// <returns>nlohmann::json: 削除指示を足したスナップショット</returns>
nlohmann::json MergeRemovals(const nlohmann::json &baseline, const nlohmann::json &current)
{
    nlohmann::json merged = baseline;
    if (!merged.is_object() || !current.is_object())
    {
        return merged;
    }
    for (auto it = current.begin(); it != current.end(); ++it)
    {
        if (!merged.contains(it.key()))
        {
            merged[it.key()] = nullptr;
        }
    }
    return merged;
}

} // namespace

void PlayModeManager::CaptureBaseline()
{
    objectBaseline_ = BaseObjectManager::GetInstance()->CaptureUndoState();
    spriteBaseline_ = SpriteManager::GetInstance()->CaptureUndoState();
    lightBaseline_ = LightGroup::GetInstance()->CaptureUndoState();
    hasBaseline_ = true;
}

void PlayModeManager::OnSceneChanged()
{
    // 前のシーンのスナップショットを持ち越さない。
    // 持ち越すと、停止したときに前シーンのオブジェクトが今のシーンに生成されてしまう。
    objectBaseline_ = nlohmann::json::object();
    spriteBaseline_ = nlohmann::json::object();
    lightBaseline_ = nlohmann::json::object();
    hasBaseline_ = false;

    // シーンが変わったら再生中に戻す（切り替えた先が止まったままだと動かせないため）
    state_ = State::Playing;
    stepRequested_ = false;
}

void PlayModeManager::RestoreBaseline()
{
    if (!hasBaseline_)
    {
        return;
    }
    BaseObjectManager *objectManager = BaseObjectManager::GetInstance();
    SpriteManager *spriteManager = SpriteManager::GetInstance();
    LightGroup *lightGroup = LightGroup::GetInstance();

    // 今ある物とスナップショットを突き合わせ、余分な物には削除指示を付けてから適用する
    objectManager->RestoreUndoState(MergeRemovals(objectBaseline_, objectManager->CaptureUndoState()));
    spriteManager->RestoreUndoState(MergeRemovals(spriteBaseline_, spriteManager->CaptureUndoState()));
    // 光源は作り直しでシーンファイルの保存値に戻るが、保存していない編集までは戻らない
    // （シーンファイルが無ければ LightGroup はシングルトンなので値を持ち続ける）。ここで明示的に戻す
    lightGroup->RestoreUndoState(MergeRemovals(lightBaseline_, lightGroup->CaptureUndoState()));
}

void PlayModeManager::Play()
{
    if (state_ == State::Playing)
    {
        return;
    }
    // 一時停止からの再開では控え直さない（停止で戻る先が「最初に再生した時点」のままになるように）
    if (state_ == State::Editing)
    {
        CaptureBaseline();
    }
    state_ = State::Playing;
    stepRequested_ = false;
    ImGuiNotification::Post("再生", {0.45f, 0.68f, 0.52f, 1.0f});
}

void PlayModeManager::Pause()
{
    if (state_ != State::Playing)
    {
        return;
    }
    state_ = State::Paused;
    stepRequested_ = false;
    ImGuiNotification::Post("一時停止", {0.80f, 0.72f, 0.42f, 1.0f});
}

void PlayModeManager::StepOnce()
{
    // 再生中に押されたら、まず止めてから1フレームだけ進める
    if (state_ == State::Playing)
    {
        state_ = State::Paused;
    }
    stepRequested_ = true;
}

void PlayModeManager::Stop()
{
    if (state_ == State::Editing)
    {
        return;
    }
    state_ = State::Editing;
    stepRequested_ = false;

    // 起動直後は既定が Playing なので、再生を押さずに停止するとスナップショットが無い。
    // ここで控えておかないと、次のシーン作り直しで未保存の編集ごと消えてしまう。
    if (!hasBaseline_)
    {
        CaptureBaseline();
    }

    // シーンを作り直してから、控えたスナップショットを重ねる。
    // 作り直しでプレイヤー・敵が初期状態に戻り、上から適用することで未保存の編集が残る
    if (!SceneManager::GetInstance()->RequestSceneRebuild([this] { RestoreBaseline(); }))
    {
        // 作り直せないシーンでは、配置オブジェクトとスプライトだけ戻す
        RestoreBaseline();
    }

    ImGuiNotification::Post("停止（再生前の状態へ戻しました）", {0.42f, 0.66f, 0.68f, 1.0f});
}

void PlayModeManager::EndFrame()
{
    // ステップ実行はこのフレームで消費する
    stepRequested_ = false;
}

void PlayModeManager::DrawToolbar()
{
    const bool isPlaying = (state_ == State::Playing);
    const bool isEditing = (state_ == State::Editing);

    // 再生中は緑、それ以外は通常色。押しているモードが一目で分かるようにする。
    ImGui::PushStyleColor(ImGuiCol_Button,
                          isPlaying ? DebugTheme::kBgGreen : ImGui::GetStyle().Colors[ImGuiCol_Button]);
    if (ImGui::Button(isPlaying ? "再生中" : "再生"))
    {
        Play();
    }
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("ゲームの更新を開始します（停止から始めると、この時点の状態を控えます）");

    ImGui::SameLine();
    ImGui::BeginDisabled(!isPlaying);
    if (ImGui::Button("一時停止"))
    {
        Pause();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("ゲームの更新だけ止めます（描画とエディタは動いたままです）");

    ImGui::SameLine();
    ImGui::BeginDisabled(isPlaying);
    if (ImGui::Button("コマ送り"))
    {
        StepOnce();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("1フレームだけ進めます");

    ImGui::SameLine();
    ImGui::BeginDisabled(isEditing);
    if (ImGui::Button("停止"))
    {
        Stop();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("更新を止め、再生を押した時点へ戻します\n"
                          "（シーンを作り直したうえで、保存していない配置の編集も戻します）");

    // 停止のたびにシーンを丸ごと作り直しているので、その所要時間を出しておく。
    // 「停止した瞬間に一拍ある」の原因がここなのかを、感覚ではなく数字で切り分けられる
    SceneManager *sceneManager = SceneManager::GetInstance();
    if (sceneManager->HasRebuildMeasurement())
    {
        const float milliseconds = sceneManager->GetLastRebuildMilliseconds();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, milliseconds > 100.0f ? DebugTheme::kAccentOrange : DebugTheme::kTextDim);
        ImGui::TextUnformatted(std::format("作り直し {:.1f} ms", milliseconds).c_str());
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("直近の停止で「シーンの破棄 → 生成 → Initialize → 未保存編集の復元」に\n"
                              "かかった時間です。100ms を超えると色が変わります");
    }

    ImGui::SameLine();
    const char *label = isPlaying ? "再生中" : (state_ == State::Paused ? "一時停止" : "停止中");
    const ImVec4 stateColor = isPlaying ? DebugTheme::kAccentGreen
                                        : (state_ == State::Paused ? DebugTheme::kAccentYellow
                                                                   : DebugTheme::kTextDim);
    StatusBadge(label, stateColor);

    // 再生中だけ回るスピナー。ゲームループが実際に進んでいるかが一目で分かる
    // （固まったときはここが止まるので、バッジの文字より早く気づける）。
    if (isPlaying)
    {
        ImGui::SameLine();
        // バッジと同じ高さの円に収め、行の中心へ合わせる
        const float frameHeight = ImGui::GetFrameHeight();
        const float radius = frameHeight * 0.30f;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (frameHeight * 0.5f - radius));
        ImSpinner::SpinnerAng("##playspin", radius, radius * 0.45f,
                              ImColor(stateColor), ImColor(0.0f, 0.0f, 0.0f, 0.0f), 5.0f, IM_PI * 1.4f);
    }
}
} // namespace Hagine
#endif // USE_IMGUI
