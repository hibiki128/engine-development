#ifdef USE_IMGUI
#include "AnimationStateMachineEditor.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include "utility/edit/behavior/BehaviorTreeEditorStyle.h"
#include <algorithm>
#include <animation/BlendSpace.h>
#include <animation/state/AnimationStateMachineRunner.h>
#include <asset/AssetPath.h>
#include <cfloat>
#include <cmath>
#include <edit/undo/UndoRedoManager.h>
#include <filesystem>
#include <icon/IconsFontAwesome5.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace ed = ax::NodeEditor;

// アニメーションのステートマシンのエディタ。
// 左=ノードグラフ（ステートと遷移）、右=インスペクタ（パラメータ・選んだステート/遷移の中身）。

namespace Hagine {

namespace {
using namespace BTEditorStyle;

const char *const kCreatePopup = "ASMCreateMenu";
const char *const kNodePopup = "ASMNodeContext";
const char *const kLinkPopup = "ASMLinkContext";
const Vector4 kNoticeWarn = {0.9f, 0.5f, 0.3f, 1.0f};

// ステートの種類ごとのアクセント
constexpr ImVec4 kAccentClip = {0.56f, 0.70f, 0.88f, 1.0f};
constexpr ImVec4 kAccentBlend = {0.62f, 0.83f, 0.66f, 1.0f};
constexpr ImVec4 kAccentAny = {0.80f, 0.72f, 0.92f, 1.0f};
constexpr ImVec4 kAccentEntry = {0.95f, 0.72f, 0.35f, 1.0f};
constexpr float kFiredGlowTime = 0.6f; // 遷移が起きたときに線を光らせる時間

std::string &PendingOpen()
{
    static std::string file;
    return file;
}

// 窓を開いてほしいか（ファイル名が空なら、窓を開くだけ）
bool &WindowRequested()
{
    static bool requested = false;
    return requested;
}

const char *ParamTypeName(AnimParamType type)
{
    switch (type)
    {
    case AnimParamType::Bool:
        return "オン/オフ";
    case AnimParamType::Trigger:
        return "合図";
    default:
        return "数値";
    }
}

// ファイル名だけ（"animation/Player/Idle.gltf" → "Idle"）
std::string Stem(const std::string &file)
{
    return std::filesystem::path(file).stem().string();
}

// 中身（位置を除いたもの）。変わったときだけ動いているキャラに登録し直してもらう
nlohmann::json ContentOf(const nlohmann::json &state)
{
    nlohmann::json content = state;
    content.erase("anyStateX");
    content.erase("anyStateY");
    if (content.contains("states"))
    {
        for (auto &s : content["states"])
        {
            s.erase("x");
            s.erase("y");
        }
    }
    return content;
}
} // namespace

AnimationStateMachineEditor::AnimationStateMachineEditor()
{
    ed::Config config;
    config.SettingsFile = nullptr;
    pContext_ = ed::CreateEditor(&config);
    alive_ = std::make_shared<AnimationStateMachineEditor *>(this);
}

AnimationStateMachineEditor::~AnimationStateMachineEditor()
{
    *alive_ = nullptr;
    if (pContext_)
        ed::DestroyEditor(pContext_);
}

void AnimationStateMachineEditor::RequestOpen(const std::string &file)
{
    PendingOpen() = file;
    WindowRequested() = true;
}

bool AnimationStateMachineEditor::HasOpenRequest()
{
    return WindowRequested();
}

// ---------------------------------------------------------
// ファイル
// ---------------------------------------------------------
bool AnimationStateMachineEditor::Open(const std::string &file)
{
    std::shared_ptr<AnimationStateMachineAsset> asset = AnimationStateMachineLibrary::Get(file);
    if (!asset)
    {
        ImGuiNotification::Post("ステートマシンが見つかりません: " + file, kNoticeWarn);
        return false;
    }
    asset_ = asset;
    fileName_ = file;
    modified_ = false;
    positionsPending_ = true;
    navigateCountdown_ = 3;
    selectedState_ = -1;
    selectedTransition_ = -1;
    liveRunnerIndex_ = 0;
    ResetUndoBaseline();
    return true;
}

void AnimationStateMachineEditor::Save()
{
    if (!asset_)
        return;
    if (fileName_.empty())
        fileName_ = "NewStateMachine";
    // 新規作成したものは、保存した名前で共有の置き場に入れる（キャラから選べるようになる）
    AnimationStateMachineLibrary::Put(fileName_, asset_);
    asset_->Save(fileName_);
    modified_ = false;
    ImGuiNotification::Post("ステートマシンを保存しました: " + fileName_);
}

void AnimationStateMachineEditor::NewAsset()
{
    asset_ = std::make_shared<AnimationStateMachineAsset>();
    fileName_ = "NewStateMachine";
    AnimStateData idle;
    idle.id = 1;
    idle.name = "Idle";
    asset_->states.push_back(idle);
    asset_->entryState = 1;
    modified_ = false;
    positionsPending_ = true;
    navigateCountdown_ = 3;
    selectedState_ = 1;
    selectedTransition_ = -1;
    ResetUndoBaseline();
}

void AnimationStateMachineEditor::RefreshAnimationFiles()
{
    animationFiles_.clear();
    const std::filesystem::path root = std::filesystem::path(AssetPath::ModelsRoot("animation"));
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(root / "animation", error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
    {
        if (it->is_regular_file() && it->path().extension() == ".gltf")
        {
            animationFiles_.push_back(std::filesystem::relative(it->path(), root).generic_string());
        }
    }
    std::sort(animationFiles_.begin(), animationFiles_.end());
}

// ---------------------------------------------------------
// 編集
// ---------------------------------------------------------
int AnimationStateMachineEditor::AddState(const ImVec2 &position, AnimStateKind kind)
{
    AnimStateData state;
    state.id = asset_->NextStateId();
    state.kind = kind;
    state.name = (kind == AnimStateKind::BlendSpace ? "BlendSpace" : "State") + std::to_string(state.id);
    state.x = position.x;
    state.y = position.y;
    if (asset_->states.empty())
        asset_->entryState = state.id;
    asset_->states.push_back(state);
    ed::SetNodePosition(ed::NodeId(NodeId(state.id)), position);
    selectedState_ = state.id;
    selectedTransition_ = -1;
    undoLabel_ = "ステートを追加";
    return state.id;
}

void AnimationStateMachineEditor::DeleteState(int stateId)
{
    std::erase_if(asset_->states, [stateId](const AnimStateData &s) { return s.id == stateId; });
    std::erase_if(asset_->transitions, [stateId](const AnimTransitionData &t) { return t.from == stateId || t.to == stateId; });
    if (asset_->entryState == stateId)
        asset_->entryState = asset_->states.empty() ? 0 : asset_->states.front().id;
    if (selectedState_ == stateId)
        selectedState_ = -1;
    undoLabel_ = "ステートを削除";
}

void AnimationStateMachineEditor::AddTransition(int from, int to)
{
    AnimTransitionData transition;
    transition.id = asset_->NextTransitionId();
    transition.from = from;
    transition.to = to;
    // 「どこからでも」は条件で抜けるのが普通なので待たない。普通の線は1周待つところから始める
    transition.hasExitTime = (from != AnimationStateMachineAsset::kAnyState);
    asset_->transitions.push_back(transition);
    selectedTransition_ = transition.id;
    selectedState_ = -1;
    undoLabel_ = "遷移を追加";
}

void AnimationStateMachineEditor::HandleCreateAction()
{
    // ピンの番号から「どのステートの出口/入口か」を読む
    auto decode = [](int pin, int &stateId, bool &isOutput) -> bool {
        if (pin == kAnyOutputPin)
        {
            stateId = AnimationStateMachineAsset::kAnyState;
            isOutput = true;
            return true;
        }
        if (pin < 100000)
            return false;
        stateId = (pin - 100000) / 2;
        isOutput = ((pin - 100000) % 2) == 1;
        return true;
    };

    if (ed::BeginCreate(kLinkAccept, 2.0f))
    {
        ed::PinId startId, endId;
        if (ed::QueryNewLink(&startId, &endId))
        {
            int a = 0, b = 0;
            bool aOut = false, bOut = false;
            if (decode(static_cast<int>(startId.Get()), a, aOut) && decode(static_cast<int>(endId.Get()), b, bOut))
            {
                if (aOut == bOut)
                {
                    ed::RejectNewItem(kLinkReject, 2.0f);
                    ed::Suspend();
                    ImGui::SetTooltip("出口（右）から入口（左）へ繋いでください");
                    ed::Resume();
                }
                else if (ed::AcceptNewItem())
                {
                    AddTransition(aOut ? a : b, aOut ? b : a);
                }
            }
        }

        // 出口から何も無い所へ離したら、その場にステートを足して繋ぐ
        ed::PinId pinId;
        if (ed::QueryNewNode(&pinId))
        {
            int stateId = 0;
            bool isOutput = false;
            if (decode(static_cast<int>(pinId.Get()), stateId, isOutput) && isOutput && ed::AcceptNewItem())
            {
                pendingFrom_ = stateId;
                createPos_ = ed::ScreenToCanvas(ImGui::GetMousePos());
                openCreateMenu_ = true;
            }
        }
    }
    ed::EndCreate();
}

void AnimationStateMachineEditor::HandleDeleteAction()
{
    if (ed::BeginDelete())
    {
        ed::NodeId nodeId;
        while (ed::QueryDeletedNode(&nodeId))
        {
            const int id = static_cast<int>(nodeId.Get());
            if (id == kAnyNodeId)
            {
                ed::RejectDeletedItem(); // 「どこからでも」は消せない
                continue;
            }
            if (ed::AcceptDeletedItem())
                DeleteState(id - 1);
        }
        ed::LinkId linkId;
        while (ed::QueryDeletedLink(&linkId))
        {
            if (ed::AcceptDeletedItem())
            {
                const int id = static_cast<int>(linkId.Get()) - 500000;
                std::erase_if(asset_->transitions, [id](const AnimTransitionData &t) { return t.id == id; });
                if (undoLabel_.empty())
                    undoLabel_ = "遷移を削除";
            }
        }
    }
    ed::EndDelete();
}

// ---------------------------------------------------------
// 実行中の様子
// ---------------------------------------------------------
AnimationStateMachineRunner *AnimationStateMachineEditor::GetLiveRunner() const
{
    int index = 0;
    for (AnimationStateMachineRunner *pRunner : AnimationStateMachineRunner::GetInstances())
    {
        if (pRunner->GetAsset() != asset_)
            continue;
        if (index == liveRunnerIndex_)
            return pRunner;
        ++index;
    }
    return nullptr;
}

// ---------------------------------------------------------
// 描画
// ---------------------------------------------------------
void AnimationStateMachineEditor::OnImGuiRender()
{
    const int frame = ImGui::GetFrameCount();
    if (frame == lastDrawFrame_)
        return;
    lastDrawFrame_ = frame;

    if (WindowRequested())
    {
        if (!PendingOpen().empty())
            Open(PendingOpen());
        PendingOpen().clear();
        WindowRequested() = false;
    }
    if (!asset_)
        NewAsset();

    ed::SetCurrentEditor(pContext_);

    if (positionsPending_)
    {
        for (const AnimStateData &s : asset_->states)
            ed::SetNodePosition(ed::NodeId(NodeId(s.id)), ImVec2(s.x, s.y));
        ed::SetNodePosition(ed::NodeId(kAnyNodeId), ImVec2(asset_->anyStateX, asset_->anyStateY));
        positionsPending_ = false;
    }

    DrawToolbar();

    const float availW = ImGui::GetContentRegionAvail().x;
    const float inspW = (std::min)(inspectorWidth_, availW * 0.45f);
    const float canvasW = (std::max)(200.0f, availW - inspW - 8.0f);
    DrawCanvas(canvasW);

    ImGui::SameLine();
    ImGui::BeginChild("##asm_inspector", ImVec2(inspW, 0), ImGuiChildFlags_Borders);
    DrawInspector();
    ImGui::EndChild();

    // ドラッグで動かした位置をデータへ写す
    for (AnimStateData &s : asset_->states)
    {
        const ImVec2 pos = ed::GetNodePosition(ed::NodeId(NodeId(s.id)));
        if (pos.x != FLT_MAX && pos.y != FLT_MAX)
        {
            s.x = pos.x;
            s.y = pos.y;
        }
    }
    const ImVec2 anyPos = ed::GetNodePosition(ed::NodeId(kAnyNodeId));
    if (anyPos.x != FLT_MAX && anyPos.y != FLT_MAX)
    {
        asset_->anyStateX = anyPos.x;
        asset_->anyStateY = anyPos.y;
    }
    CommitUndo();

    ed::SetCurrentEditor(nullptr);
}

void AnimationStateMachineEditor::DrawToolbar()
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(ICON_FA_STREAM);
    ImGui::SameLine();
    const std::string preview = fileName_ + (modified_ ? " *" : "");
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("##asmFile", preview.c_str()))
    {
        if (ImGui::IsWindowAppearing())
            fileList_ = AnimationStateMachineLibrary::ListFiles();
        if (fileList_.empty())
            DimText("保存済みのステートマシンがありません");
        for (const std::string &name : fileList_)
        {
            if (ImGui::Selectable(name.c_str(), name == fileName_))
                Open(name);
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("開くステートマシンを選ぶ（jsons/%s）\n* は保存していない変更あり", AnimationStateMachineAsset::kFolder);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    ImGui::InputTextWithHint("##asmName", "保存する名前", &fileName_);
    ImGui::SameLine();
    if (PrimaryButton(ICON_FA_SAVE " 保存"))
        Save();
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_FILE " 新規"))
        NewAsset();
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_EXPAND " 全体を表示"))
        navigateCountdown_ = 2;

    // 実行中の様子を見るキャラ
    std::vector<AnimationStateMachineRunner *> runners;
    for (AnimationStateMachineRunner *pRunner : AnimationStateMachineRunner::GetInstances())
    {
        if (pRunner->GetAsset() == asset_)
            runners.push_back(pRunner);
    }
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    if (runners.empty())
    {
        DimText(ICON_FA_EYE " このステートマシンで動いているキャラはいません");
    }
    else
    {
        liveRunnerIndex_ = std::clamp(liveRunnerIndex_, 0, static_cast<int>(runners.size()) - 1);
        ImGui::TextColored(kBorderRunning, ICON_FA_EYE);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("##asmLive", runners[liveRunnerIndex_]->GetOwnerName().c_str()))
        {
            for (int i = 0; i < static_cast<int>(runners.size()); ++i)
            {
                if (ImGui::Selectable((runners[i]->GetOwnerName() + "##live" + std::to_string(i)).c_str(), i == liveRunnerIndex_))
                    liveRunnerIndex_ = i;
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("実行中の様子（今のステート・起きた遷移）を見るキャラ");
    }

    ImGui::TextColored(kBorderRunning, "● 再生中のステート");
    ImGui::SameLine();
    ImGui::TextColored(kAccentEntry, "● 最初のステート");
    ImGui::SameLine();
    ImGui::TextDisabled("|  右クリック: ステートを追加  /  出口(右)→入口(左)へ引っ張る: 遷移  /  Delete: 削除  /  Ctrl+Z: 元に戻す");
}

void AnimationStateMachineEditor::DrawCanvas(float width)
{
    ed::Begin("アニメーションステートマシン", ImVec2(width, 0));

    const float pulse = sinf(static_cast<float>(ImGui::GetTime()) * 5.0f) * 0.5f + 0.5f;
    const AnimationStateMachineRunner *pLive = GetLiveRunner();
    const int currentState = pLive ? pLive->GetCurrentStateId() : -1;

    DrawAnyStateNode();
    for (AnimStateData &state : asset_->states)
    {
        DrawStateNode(state, state.id == asset_->entryState, state.id == currentState, pulse);
    }
    DrawLinks(pulse);

    HandleCreateAction();
    HandleDeleteAction();
    DrawMenus();

    // 選択（ノード・線）をインスペクタへ
    if (ed::GetSelectedObjectCount() > 0)
    {
        ed::NodeId nodes[1];
        ed::LinkId links[1];
        if (ed::GetSelectedNodes(nodes, 1) > 0)
        {
            const int id = static_cast<int>(nodes[0].Get());
            if (id != kAnyNodeId)
            {
                selectedState_ = id - 1;
                selectedTransition_ = -1;
            }
        }
        else if (ed::GetSelectedLinks(links, 1) > 0)
        {
            selectedTransition_ = static_cast<int>(links[0].Get()) - 500000;
            selectedState_ = -1;
        }
    }

    if (navigateCountdown_ > 0 && --navigateCountdown_ == 0)
        ed::NavigateToContent(0.0f);

    ed::End();
}

void AnimationStateMachineEditor::DrawAnyStateNode()
{
    ed::PushStyleColor(ed::StyleColor_NodeBg, kNodeBgIdle);
    ed::PushStyleColor(ed::StyleColor_NodeBorder, WithAlpha(kAccentAny, 0.6f));
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
    ed::BeginNode(ed::NodeId(kAnyNodeId));
    ImGui::TextColored(kAccentAny, ICON_FA_RANDOM " どこからでも");
    ImGui::TextDisabled("条件を満たせば今のステートに関係なく遷移");
    ed::BeginPin(ed::PinId(kAnyOutputPin), ed::PinKind::Output);
    ImGui::TextColored(kPinOutput, "            出口 ->");
    ed::EndPin();
    ed::EndNode();
    ed::PopStyleVar(1);
    ed::PopStyleColor(2);
}

void AnimationStateMachineEditor::DrawStateNode(AnimStateData &state, bool isEntry, bool isCurrent, float pulse)
{
    const ImVec4 accent = (state.kind == AnimStateKind::BlendSpace) ? kAccentBlend : kAccentClip;
    ImVec4 bg = kNodeBgIdle;
    ImVec4 border = WithAlpha(accent, 0.45f);
    float borderWidth = 1.5f;
    if (isCurrent)
    {
        bg = kNodeBgRunning;
        border = WithAlpha(kBorderRunning, 0.85f + pulse * 0.15f);
        borderWidth = 2.5f + pulse * 1.5f;
    }
    else if (isEntry)
    {
        border = WithAlpha(kAccentEntry, 0.8f);
        borderWidth = 2.0f;
    }

    ed::PushStyleColor(ed::StyleColor_NodeBg, bg);
    ed::PushStyleColor(ed::StyleColor_NodeBorder, border);
    ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, borderWidth);
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
    ed::BeginNode(ed::NodeId(NodeId(state.id)));

    if (isEntry)
    {
        ImGui::TextColored(kAccentEntry, ICON_FA_FLAG);
        ImGui::SameLine();
    }
    ImGui::TextColored(isCurrent ? kTextRunning : accent, "%s%s", state.name.c_str(), isCurrent ? " [>>]" : "");

    // 何を再生するか（短く）
    if (state.kind == AnimStateKind::Clip)
    {
        ImGui::TextDisabled(ICON_FA_FILM " %s%s", state.file.empty() ? "(ファイル未設定)" : Stem(state.file).c_str(), state.loop ? "" : "  1回");
    }
    else
    {
        ImGui::TextDisabled(ICON_FA_LAYER_GROUP " %d個を混ぜる  X:%s Y:%s", static_cast<int>(state.points.size()),
                            state.paramX.empty() ? "-" : state.paramX.c_str(), state.paramY.empty() ? "-" : state.paramY.c_str());
    }

    ed::BeginPin(ed::PinId(InputPin(state.id)), ed::PinKind::Input);
    ImGui::TextColored(kPinInput, "-> 入口");
    ed::EndPin();
    ImGui::SameLine(0.0f, 60.0f);
    ed::BeginPin(ed::PinId(OutputPin(state.id)), ed::PinKind::Output);
    ImGui::TextColored(kPinOutput, "出口 ->");
    ed::EndPin();

    ed::EndNode();
    ed::PopStyleVar(2);
    ed::PopStyleColor(2);
}

void AnimationStateMachineEditor::DrawLinks(float pulse)
{
    ed::PushStyleColor(ed::StyleColor_Flow, kLinkFlow);
    ed::PushStyleColor(ed::StyleColor_FlowMarker, kLinkFlowMarker);
    const AnimationStateMachineRunner *pLive = GetLiveRunner();
    for (const AnimTransitionData &t : asset_->transitions)
    {
        const int start = (t.from == AnimationStateMachineAsset::kAnyState) ? kAnyOutputPin : OutputPin(t.from);
        ImVec4 color = (t.from == AnimationStateMachineAsset::kAnyState) ? WithAlpha(kAccentAny, 0.7f) : kLinkIdle;
        float thickness = 1.5f;
        bool flow = false;
        if (t.id == selectedTransition_)
        {
            color = DebugTheme::kAccentBlue;
            thickness = 2.5f;
        }
        if (pLive && pLive->GetLastTransitionId() == t.id && pLive->GetLastTransitionAge() < kFiredGlowTime)
        {
            // たった今この遷移が起きた
            color = ImVec4(kLinkFlow.x, 0.78f + pulse * 0.22f, 0.0f, 1.0f);
            thickness = 3.0f;
            flow = true;
        }
        ed::Link(ed::LinkId(LinkId(t.id)), ed::PinId(start), ed::PinId(InputPin(t.to)), color, thickness);
        if (flow)
            ed::Flow(ed::LinkId(LinkId(t.id)));
    }
    ed::PopStyleColor(2);
}

void AnimationStateMachineEditor::DrawMenus()
{
    ed::Suspend();
    if (ed::ShowBackgroundContextMenu())
    {
        createPos_ = ed::ScreenToCanvas(ImGui::GetMousePos());
        pendingFrom_ = kNoPending;
        openCreateMenu_ = true;
    }
    if (openCreateMenu_)
    {
        ImGui::OpenPopup(kCreatePopup);
        openCreateMenu_ = false;
    }
    if (ImGui::BeginPopup(kCreatePopup))
    {
        DimText(pendingFrom_ != kNoPending ? "繋いだ状態でステートを追加" : "ステートを追加");
        ImGui::Separator();
        int created = -1;
        if (ImGui::MenuItem(ICON_FA_FILM " クリップのステート"))
            created = AddState(createPos_, AnimStateKind::Clip);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("アニメーションファイル1つを再生する");
        if (ImGui::MenuItem(ICON_FA_LAYER_GROUP " ブレンドスペースのステート"))
            created = AddState(createPos_, AnimStateKind::BlendSpace);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("複数のファイルを、パラメータ（移動の速さや向きなど）で混ぜる");
        if (created >= 0)
        {
            if (pendingFrom_ != kNoPending)
                AddTransition(pendingFrom_, created);
            pendingFrom_ = kNoPending;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ed::NodeId nodeId;
    if (ed::ShowNodeContextMenu(&nodeId))
    {
        contextStateId_ = static_cast<int>(nodeId.Get()) - 1;
        ImGui::OpenPopup(kNodePopup);
    }
    ed::LinkId linkId;
    if (ed::ShowLinkContextMenu(&linkId))
    {
        contextTransitionId_ = static_cast<int>(linkId.Get()) - 500000;
        ImGui::OpenPopup(kLinkPopup);
    }
    if (ImGui::BeginPopup(kNodePopup))
    {
        const AnimStateData *pState = asset_->FindState(contextStateId_);
        if (!pState)
        {
            ImGui::CloseCurrentPopup();
        }
        else
        {
            DimText(pState->name.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_FLAG " 最初のステートにする", nullptr, false, asset_->entryState != pState->id))
            {
                asset_->entryState = pState->id;
                undoLabel_ = "最初のステートを変更";
            }
            AnimationStateMachineRunner *pLive = GetLiveRunner();
            if (ImGui::MenuItem(ICON_FA_PLAY " 今すぐこのステートへ", nullptr, false, pLive != nullptr))
                pLive->ForceState(pState->id);
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_TRASH " 削除"))
                DeleteState(contextStateId_);
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup(kLinkPopup))
    {
        if (ImGui::MenuItem(ICON_FA_TRASH " 遷移を削除"))
        {
            const int id = contextTransitionId_;
            std::erase_if(asset_->transitions, [id](const AnimTransitionData &t) { return t.id == id; });
            undoLabel_ = "遷移を削除";
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}

// ---------------------------------------------------------
// インスペクタ
// ---------------------------------------------------------
void AnimationStateMachineEditor::DrawInspector()
{
    // ---- 実行中の様子 ----
    if (AnimationStateMachineRunner *pLive = GetLiveRunner())
    {
        SectionHeader("[ 実行中 ]", DebugTheme::kAccentYellow);
        const AnimStateData *pState = asset_->FindState(pLive->GetCurrentStateId());
        ImGui::Text("%s  のステート: ", pLive->GetOwnerName().c_str());
        ImGui::SameLine();
        StatusBadge(pState ? pState->name.c_str() : "(なし)", DebugTheme::kAccentYellow);
        ImGui::TextDisabled("入ってから %.2f 秒 / 再生 %.2f 周", pLive->GetStateTime(), pLive->GetNormalizedTime());
        ImGui::Spacing();
    }

    DrawParamsSection();
    ImGui::Spacing();

    if (AnimStateData *pState = asset_->FindState(selectedState_))
    {
        DrawStateSection(*pState);
        return;
    }
    for (AnimTransitionData &t : asset_->transitions)
    {
        if (t.id == selectedTransition_)
        {
            DrawTransitionSection(t);
            return;
        }
    }
    SectionHeader("[ 選択 ]", DebugTheme::kAccentBlue);
    ImGui::PushTextWrapPos(0.0f);
    DimText("ステート（箱）か遷移（線）をクリックすると、ここで中身を編集できます。");
    ImGui::PopTextWrapPos();
}

void AnimationStateMachineEditor::DrawParamsSection()
{
    SectionHeader("[ パラメータ ]", DebugTheme::kAccentCyan);
    ImGui::PushTextWrapPos(0.0f);
    DimText("ゲーム側が SetFloat / SetBool / SetTrigger で値を渡し、遷移の条件で使います。");
    ImGui::PopTextWrapPos();

    AnimationStateMachineRunner *pLive = GetLiveRunner();
    int removeIndex = -1;
    for (size_t i = 0; i < asset_->params.size(); ++i)
    {
        AnimParamDef &p = asset_->params[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.38f);
        ImGui::InputText("##name", &p.name);
        if (ImGui::IsItemDeactivatedAfterEdit())
            undoLabel_ = "パラメータの名前を変更";
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
        int type = static_cast<int>(p.type);
        if (ImGui::Combo("##type", &type, "数値\0オン/オフ\0合図\0"))
            p.type = static_cast<AnimParamType>(type);
        ImGui::SameLine();
        // 実行中なら今の値をその場で変えて試せる。止まっていれば初期値
        if (p.type == AnimParamType::Float)
        {
            float v = pLive ? pLive->GetValue(p.name) : p.defaultValue;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
            if (ImGui::DragFloat("##value", &v, 0.02f, 0.0f, 0.0f, "%.2f"))
            {
                if (pLive)
                    pLive->SetFloat(p.name, v);
                else
                    p.defaultValue = v;
            }
        }
        else if (p.type == AnimParamType::Bool)
        {
            bool v = (pLive ? pLive->GetValue(p.name) : p.defaultValue) >= 0.5f;
            if (ImGui::Checkbox("##value", &v))
            {
                if (pLive)
                    pLive->SetBool(p.name, v);
                else
                    p.defaultValue = v ? 1.0f : 0.0f;
            }
        }
        else
        {
            ImGui::BeginDisabled(!pLive);
            if (NeutralButton(ICON_FA_BOLT "##fire", ImVec2(ImGui::GetContentRegionAvail().x - 28.0f, 0.0f)))
                pLive->SetTrigger(p.name);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("実行中のキャラへ合図を送る");
        }
        ImGui::SameLine();
        if (DangerButton("x", ImVec2(22.0f, 0.0f)))
            removeIndex = static_cast<int>(i);
        ImGui::PopID();
    }
    if (removeIndex >= 0)
    {
        asset_->params.erase(asset_->params.begin() + removeIndex);
        undoLabel_ = "パラメータを削除";
    }
    if (NeutralButton(ICON_FA_PLUS " パラメータを追加", ImVec2(-1, 0)))
    {
        AnimParamDef def;
        def.name = "param" + std::to_string(asset_->params.size() + 1);
        asset_->params.push_back(def);
        undoLabel_ = "パラメータを追加";
    }
}

bool AnimationStateMachineEditor::DrawFileCombo(const char *id, std::string &file)
{
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(id, file.empty() ? "(ファイルを選ぶ)" : file.c_str(), ImGuiComboFlags_HeightLarge))
    {
        if (ImGui::IsWindowAppearing() || animationFiles_.empty())
            RefreshAnimationFiles();
        for (const std::string &candidate : animationFiles_)
        {
            if (ImGui::Selectable(candidate.c_str(), candidate == file))
            {
                file = candidate;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void AnimationStateMachineEditor::DrawStateSection(AnimStateData &state)
{
    SectionHeader("[ ステート ]", state.kind == AnimStateKind::BlendSpace ? DebugTheme::kAccentGreen : DebugTheme::kAccentBlue);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##stateName", &state.name);

    int kind = static_cast<int>(state.kind);
    bool kindChanged = ImGui::RadioButton("クリップ", &kind, 0);
    ImGui::SameLine();
    kindChanged |= ImGui::RadioButton("ブレンドスペース", &kind, 1);
    if (kindChanged)
    {
        state.kind = static_cast<AnimStateKind>(kind);
        undoLabel_ = "ステートの種類を変更";
    }

    if (state.kind == AnimStateKind::Clip)
    {
        DimText("再生するファイル");
        if (DrawFileCombo("##clipFile", state.file))
            undoLabel_ = "ファイルを変更";
        ImGui::Checkbox("ループ", &state.loop);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("速度", &state.speed, 0.01f, 0.0f, 8.0f, "%.2f");
    }
    else
    {
        static const char *kModes[] = {"向き＋長さ（中心と周り）", "自由配置", "一直線（X だけ）"};
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("##blendMode", &state.blendMode, kModes, IM_ARRAYSIZE(kModes));
        // X/Y に使うパラメータ（数値のものだけ）
        auto paramCombo = [&](const char *label, std::string &target) {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
            if (ImGui::BeginCombo(label, target.empty() ? "(なし)" : target.c_str()))
            {
                if (ImGui::Selectable("(なし)", target.empty()))
                    target.clear();
                for (const AnimParamDef &p : asset_->params)
                {
                    if (p.type == AnimParamType::Float && ImGui::Selectable(p.name.c_str(), p.name == target))
                        target = p.name;
                }
                ImGui::EndCombo();
            }
        };
        paramCombo("X のパラメータ", state.paramX);
        paramCombo("Y のパラメータ", state.paramY);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("速度", &state.speed, 0.01f, 0.0f, 8.0f, "%.2f");

        DimText("混ぜるファイルと位置");
        int removeIndex = -1;
        for (size_t i = 0; i < state.points.size(); ++i)
        {
            AnimBlendPoint &p = state.points[i];
            ImGui::PushID(static_cast<int>(i));
            DrawFileCombo("##pointFile", p.file);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
            ImGui::DragFloat2("##pointPos", &p.x, 0.01f, -10.0f, 10.0f, "%.2f");
            ImGui::SameLine();
            if (DangerButton("x", ImVec2(22.0f, 0.0f)))
                removeIndex = static_cast<int>(i);
            ImGui::PopID();
        }
        if (removeIndex >= 0)
            state.points.erase(state.points.begin() + removeIndex);
        if (NeutralButton(ICON_FA_PLUS " 点を追加", ImVec2(-1, 0)))
            state.points.push_back({});
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(asset_->entryState == state.id);
    if (NeutralButton(ICON_FA_FLAG " 最初のステートにする", ImVec2(-1, 0)))
    {
        asset_->entryState = state.id;
        undoLabel_ = "最初のステートを変更";
    }
    ImGui::EndDisabled();
    AnimationStateMachineRunner *pLive = GetLiveRunner();
    ImGui::BeginDisabled(!pLive);
    if (ConfirmButton(ICON_FA_PLAY " 今すぐこのステートへ（実行中のキャラ）", ImVec2(-1, 0)))
        pLive->ForceState(state.id);
    ImGui::EndDisabled();

    // このステートから出る遷移の一覧（線が重なって選びにくいとき用）
    ImGui::Spacing();
    DimText("ここから出る遷移（上から順に調べる）");
    for (const AnimTransitionData &t : asset_->transitions)
    {
        if (t.from != state.id)
            continue;
        const AnimStateData *pTo = asset_->FindState(t.to);
        const std::string label = std::string(ICON_FA_ARROW_RIGHT " ") + (pTo ? pTo->name : "?") + "##tr" + std::to_string(t.id);
        if (ImGui::Selectable(label.c_str(), false))
        {
            selectedTransition_ = t.id;
            selectedState_ = -1;
        }
    }
}

void AnimationStateMachineEditor::DrawTransitionSection(AnimTransitionData &transition)
{
    SectionHeader("[ 遷移 ]", DebugTheme::kAccentOrange);
    const AnimStateData *pFrom = asset_->FindState(transition.from);
    const AnimStateData *pTo = asset_->FindState(transition.to);
    ImGui::Text("%s  " ICON_FA_ARROW_RIGHT "  %s",
                transition.from == AnimationStateMachineAsset::kAnyState ? "どこからでも" : (pFrom ? pFrom->name.c_str() : "?"),
                pTo ? pTo->name.c_str() : "?");

    ImGui::Checkbox("再生が進むのを待つ", &transition.hasExitTime);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("今のステートの再生がここまで進んでから遷移する（1 で1周、2 で2周）");
    ImGui::BeginDisabled(!transition.hasExitTime);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::DragFloat("##exitTime", &transition.exitTime, 0.01f, 0.0f, 20.0f, "待つ量 %.2f 周");
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::DragFloat("##duration", &transition.duration, 0.01f, 0.0f, 3.0f, "切り替え %.2f 秒");

    ImGui::Spacing();
    DimText("条件（全部満たしたら遷移）");
    if (transition.conditions.empty() && !transition.hasExitTime)
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(DebugTheme::kAccentOrange, "条件も待ちも無いので、1周したら遷移します");
        ImGui::PopTextWrapPos();
    }
    int removeIndex = -1;
    for (size_t i = 0; i < transition.conditions.size(); ++i)
    {
        AnimCondition &c = transition.conditions[i];
        ImGui::PushID(static_cast<int>(i));
        const AnimParamDef *pDef = asset_->FindParam(c.param);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
        if (ImGui::BeginCombo("##param", c.param.empty() ? "(選ぶ)" : c.param.c_str()))
        {
            for (const AnimParamDef &p : asset_->params)
            {
                if (ImGui::Selectable(p.name.c_str(), p.name == c.param))
                {
                    c.param = p.name;
                    // 種類に合う比べ方にそろえる
                    c.op = (p.type == AnimParamType::Float) ? AnimConditionOp::Greater
                           : (p.type == AnimParamType::Bool) ? AnimConditionOp::IsTrue
                                                             : AnimConditionOp::Triggered;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        const AnimParamType type = pDef ? pDef->type : AnimParamType::Float;
        if (type == AnimParamType::Float)
        {
            int op = (c.op == AnimConditionOp::Less) ? 1 : 0;
            ImGui::SetNextItemWidth(56.0f);
            if (ImGui::Combo("##op", &op, ">\0<\0"))
                c.op = op == 1 ? AnimConditionOp::Less : AnimConditionOp::Greater;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
            ImGui::DragFloat("##value", &c.value, 0.02f, 0.0f, 0.0f, "%.2f");
        }
        else if (type == AnimParamType::Bool)
        {
            int op = (c.op == AnimConditionOp::IsFalse) ? 1 : 0;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
            if (ImGui::Combo("##op", &op, "オン\0オフ\0"))
                c.op = op == 1 ? AnimConditionOp::IsFalse : AnimConditionOp::IsTrue;
        }
        else
        {
            c.op = AnimConditionOp::Triggered;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
            DimText("合図が来たら");
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 22.0f);
        if (DangerButton("x", ImVec2(22.0f, 0.0f)))
            removeIndex = static_cast<int>(i);
        ImGui::PopID();
    }
    if (removeIndex >= 0)
        transition.conditions.erase(transition.conditions.begin() + removeIndex);
    ImGui::BeginDisabled(asset_->params.empty());
    if (NeutralButton(ICON_FA_PLUS " 条件を追加", ImVec2(-1, 0)))
    {
        AnimCondition c;
        const AnimParamDef &p = asset_->params.front();
        c.param = p.name;
        c.op = (p.type == AnimParamType::Float) ? AnimConditionOp::Greater
               : (p.type == AnimParamType::Bool) ? AnimConditionOp::IsTrue
                                                 : AnimConditionOp::Triggered;
        transition.conditions.push_back(c);
    }
    ImGui::EndDisabled();
    if (asset_->params.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("先に上の「パラメータ」を作ってください");

    ImGui::Spacing();
    if (DangerButton(ICON_FA_TRASH " この遷移を削除", ImVec2(-1, 0)))
    {
        const int id = transition.id;
        std::erase_if(asset_->transitions, [id](const AnimTransitionData &t) { return t.id == id; });
        selectedTransition_ = -1;
        undoLabel_ = "遷移を削除";
    }
}

// ---------------------------------------------------------
// Undo
// ---------------------------------------------------------
void AnimationStateMachineEditor::CommitUndo()
{
    if (ImGui::IsAnyItemActive())
        return;
    nlohmann::json now = asset_->ToJson();
    if (now == undoBaseline_)
    {
        undoLabel_.clear();
        return;
    }
    // 中身が変わったときだけ、動いているキャラに登録し直してもらう（位置だけなら不要）
    if (ContentOf(now) != ContentOf(undoBaseline_))
        asset_->Touch();

    const std::string label = undoLabel_.empty() ? "ステートマシンを編集" : undoLabel_;
    std::weak_ptr<AnimationStateMachineEditor *> alive = alive_;
    UndoRedoManager::GetInstance()->Push(std::make_unique<JsonStateCommand>(
        "ステートマシン: " + label, undoBaseline_, now, [alive](const nlohmann::json &s) {
            if (auto self = alive.lock(); self && *self)
                (*self)->RestoreUndoState(s);
        }));
    undoBaseline_ = std::move(now);
    undoLabel_.clear();
    modified_ = true;
}

void AnimationStateMachineEditor::ResetUndoBaseline()
{
    undoBaseline_ = asset_ ? asset_->ToJson() : nlohmann::json();
    undoLabel_.clear();
}

void AnimationStateMachineEditor::RestoreUndoState(const nlohmann::json &state)
{
    if (!asset_)
        return;
    asset_->FromJson(state); // 中で Touch するので動いているキャラにも戻る
    positionsPending_ = true;
    modified_ = true;
    ResetUndoBaseline();
}

} // namespace Hagine

#endif // USE_IMGUI
