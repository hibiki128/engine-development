#ifdef USE_IMGUI
#include "BehaviorTreeEditor.h"
#include "BehaviorTreeEditorStyle.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <icon/IconsFontAwesome5.h>
#include <imgui_internal.h>
#include <set>

namespace ed = ax::NodeEditor;

// エディタ本体の描画（ツールバー・ノードグラフ・リンク・追加メニュー・右クリックメニュー）。
// 右ペインのインスペクタは BehaviorTreeEditorInspector.cpp にある。

namespace Hagine {

namespace {
using namespace BTEditorStyle;

const char *const kCreatePopup = "BTNodeCreateMenu";
const char *const kNodePopup = "BTNodeContext";
const char *const kLinkPopup = "BTLinkContext";

// 縦の区切りを挟んで横に並べる
void ToolbarSeparator()
{
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
}

const char *StatusName(NodeStatus s)
{
    switch (s)
    {
    case NodeStatus::Running:
        return "実行中";
    case NodeStatus::Success:
        return "成功";
    case NodeStatus::Failure:
        return "失敗";
    default:
        return "待機";
    }
}

// 1個の数値の設定を、その書式で文字にする
std::string FormatParam(const BTParamDesc &p, float value)
{
    char buf[64] = {};
    switch (p.kind)
    {
    case BTParamKind::Int:
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(value));
        break;
    case BTParamKind::Bool:
        return value >= 1.0f ? "ON" : "OFF";
    default:
        std::snprintf(buf, sizeof(buf), p.format, value);
        break;
    }
    return buf;
}
} // namespace

void BehaviorTreeEditor::OnImGuiRender()
{
    // シーンの設定窓などから同じフレームに2回呼ばれても1回だけ描く
    const int frame = ImGui::GetFrameCount();
    if (frame == lastDrawFrame_)
        return;
    lastDrawFrame_ = frame;

    ed::SetCurrentEditor(pContext_);

    // 読み込み・Undo で変わった位置をノードエディタへ送る
    if (positionsPending_)
    {
        for (const auto &node : asset_.nodes)
            ed::SetNodePosition(ed::NodeId(node.id), ImVec2(node.x, node.y));
        positionsPending_ = false;
    }

    UpdateSingleTest();
    UpdateStatusTimers(ImGui::GetIO().DeltaTime);

    DrawToolbar();

    // 本体: 左=ノードキャンバス / 右=インスペクター
    const float availW = ImGui::GetContentRegionAvail().x;
    // ウィンドウが狭いときはインスペクター幅を縮めて両ペインが収まるようにする
    const float inspW = std::min(inspectorWidth_, availW * 0.45f);
    const float canvasW = std::max(200.0f, availW - inspW - 8.0f);
    DrawCanvas(canvasW);

    ImGui::SameLine();
    ImGui::BeginChild("##bt_inspector", ImVec2(inspW, 0), ImGuiChildFlags_Borders);
    DrawInspector();
    ImGui::EndChild();

    // ドラッグで動かした位置をデータへ写す（保存と Undo に載る）
    for (auto &node : asset_.nodes)
    {
        const ImVec2 pos = ed::GetNodePosition(ed::NodeId(node.id));
        if (pos.x != FLT_MAX && pos.y != FLT_MAX)
        {
            node.x = pos.x;
            node.y = pos.y;
        }
    }
    CommitUndo();

    ed::SetCurrentEditor(nullptr);
}

// ---------------------------------------------------------
// ツールバー
// ---------------------------------------------------------
void BehaviorTreeEditor::DrawToolbar()
{
    // ---- 1段目: ファイル ----
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(ICON_FA_PROJECT_DIAGRAM);
    ImGui::SameLine();
    const std::string preview = fileName_ + (modified_ ? " *" : "");
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("##btFile", preview.c_str()))
    {
        if (ImGui::IsWindowAppearing() || fileListDirty_)
            RefreshFileList();
        if (fileList_.empty())
            DimText("保存済みのツリーがありません");
        for (const auto &name : fileList_)
        {
            if (ImGui::Selectable(name.c_str(), name == fileName_))
                Open(folder_, name);
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("開くツリーを選ぶ（%s フォルダ）\n* は保存していない変更あり", folder_.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputTextWithHint("##btName", "保存する名前", &fileName_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("保存するファイル名（.json なし）");
    ImGui::SameLine();
    if (PrimaryButton(ICON_FA_SAVE " 保存"))
        Save();
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_FILE " 新規"))
        NewTree();
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_EXPAND " 全体を表示"))
        navigateCountdown_ = 2;

    // ---- 2段目: 実行 ----
    if (isRunning_)
    {
        if (ConfirmButton(ICON_FA_SYNC " 変更を反映"))
            BuildAndRun();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("今の内容でツリーを組み立て直して、続けて実行する");
    }
    else if (ConfirmButton(ICON_FA_PLAY " ビルド＆実行"))
    {
        BuildAndRun();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!isRunning_ && !isSingleTesting_);
    if (DangerButton(ICON_FA_STOP " 停止"))
        StopRun();
    ImGui::EndDisabled();

    // 単体テスト
    ToolbarSeparator();
    {
        const std::vector<int> selected = GetSelectedNodeIds();
        const int selNodeId = selected.empty() ? -1 : selected.front();
        const BTNodeData *selNode = asset_.FindNode(selNodeId);
        if (isSingleTesting_)
        {
            if (DangerButton(ICON_FA_STOP " 単体テスト停止"))
                StopSingleTest();
        }
        else
        {
            ImGui::BeginDisabled(!selNode || !pDebugContext_);
            if (NeutralButton(ICON_FA_VIAL " 単体テスト"))
                StartSingleTest(selNodeId);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("選んだノード（とその下）だけを、終わるまで動かす");
        }
        ImGui::SameLine();
        if (singleTestResult_ != NodeStatus::Idle)
        {
            const ImVec4 color = singleTestResult_ == NodeStatus::Running ? kTextRunning : singleTestResult_ == NodeStatus::Success ? kTextSuccess
                                                                                                                                  : kTextFailure;
            ImGui::TextColored(color, "[%s]", StatusName(singleTestResult_));
            ImGui::SameLine();
        }
        ImGui::TextDisabled("選択: %s", selNode ? selNode->title.c_str() : "---");
    }

    // 実行中のアクション（いちばん下で動いている物）を出す
    if (isRunning_)
    {
        ToolbarSeparator();
        std::string runningName = "---";
        for (const auto &node : asset_.nodes)
        {
            if (GetDisplayStatus(node.id) != NodeStatus::Running)
                continue;
            const BTNodeTypeDesc *desc = BehaviorTreeRegistry::Get().Find(node.type);
            runningName = node.title;
            if (desc && desc->kind == BTNodeKind::Action)
                break;
        }
        ImGui::TextColored(kTextRunning, "実行中: %s", runningName.c_str());
    }

    if (!pDebugContext_)
    {
        ToolbarSeparator();
        ImGui::TextColored(DebugTheme::kAccentOrange, "動かす対象がありません");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("SetDebugContext で対象を渡すと、ビルド＆実行・単体テストが使えます");
    }

    // ---- 呼び出し側からの情報（例: 敵の今の作戦）----
    if (infoProvider_)
    {
        const std::string info = infoProvider_();
        if (!info.empty())
            ImGui::TextColored(DebugTheme::kAccentYellow, "%s", info.c_str());
    }

    // ---- 3段目: 凡例と操作 ----
    ImGui::TextColored(kBorderRunning, "● 実行中");
    ImGui::SameLine();
    ImGui::TextColored(kBorderSuccess, "● 成功");
    ImGui::SameLine();
    ImGui::TextColored(kBorderFailure, "● 失敗");
    ImGui::SameLine();
    ImGui::TextDisabled("|  右クリック: ノード追加  /  ピンを空き地へ離す: 繋いで追加  /  Delete: 削除  /  Ctrl+Z: 元に戻す");
}

// ---------------------------------------------------------
// キャンバス
// ---------------------------------------------------------
void BehaviorTreeEditor::DrawCanvas(float width)
{
    // キャンバスの大きさが変わるとノードエディタは前の見え方を保とうとするので、
    // 全体表示は大きさが落ち着いてから行う
    const ImVec2 canvasSize(width, ImGui::GetContentRegionAvail().y);
    if (navigateCountdown_ > 0 && (canvasSize.x != lastCanvasSize_.x || canvasSize.y != lastCanvasSize_.y))
        navigateCountdown_ = 3;
    lastCanvasSize_ = canvasSize;

    ed::Begin("ビヘイビアツリーエディタ", ImVec2(width, 0));

    const float now = static_cast<float>(ImGui::GetTime());
    const float pulse = sinf(now * 5.0f) * 0.5f + 0.5f; // 0→1 sin波

    // 根（入力の無い最初のノード）と、根まで繋がっていないノード
    std::set<int> hasParent;
    for (const auto &link : asset_.links)
        hasParent.insert(BTPin::NodeOfInput(link.end));
    const int rootId = asset_.FindRootNodeId();

    for (auto &node : asset_.nodes)
    {
        const bool isRoot = node.id == rootId;
        const bool isOrphan = !isRoot && !hasParent.count(node.id);
        DrawNode(node, isRoot, isOrphan, pulse);
    }
    DrawLinks(pulse);

    HandleCreateAction();
    HandleDeleteAction();
    DrawCreateMenu();
    DrawContextMenus();

    if (navigateCountdown_ > 0 && --navigateCountdown_ == 0)
        ed::NavigateToContent(0.0f);

    ed::End();
}

void BehaviorTreeEditor::DrawNode(BTNodeData &node, bool isRoot, bool isOrphan, float pulse)
{
    const BTNodeTypeDesc *desc = BehaviorTreeRegistry::Get().Find(node.type);
    const NodeStatus status = GetDisplayStatus(node.id);
    const float timer = GetStatusTimer(node.id);
    const ImVec4 accent = AccentOf(desc);

    // 落ち着いた配色: 待機中は共通の暗い地＋種類のアクセントの枠。実行状態だけ色を付ける
    ImVec4 bgColor = kNodeBgIdle;
    ImVec4 borderColor;
    float borderWidth = 1.5f;
    ImVec4 titleColor = accent;
    const char *statusBadge = "";
    if (status == NodeStatus::Running)
    {
        bgColor = kNodeBgRunning;
        borderColor = WithAlpha(kBorderRunning, 0.85f + pulse * 0.15f);
        borderWidth = 2.5f + pulse * 1.5f;
        titleColor = kTextRunning;
        statusBadge = " [>>]";
    }
    else if (status == NodeStatus::Success || timer > 0.0f)
    {
        const float i = (status == NodeStatus::Success) ? 1.0f : timer / kStatusGlowTime;
        bgColor = kNodeBgSuccess;
        borderColor = WithAlpha(kBorderSuccess, 0.35f + i * 0.55f);
        borderWidth = 2.0f;
        titleColor = kTextSuccess;
        statusBadge = " [OK]";
    }
    else if (status == NodeStatus::Failure || timer < 0.0f)
    {
        const float i = (status == NodeStatus::Failure) ? 1.0f : (-timer) / kStatusGlowTime;
        bgColor = kNodeBgFailure;
        borderColor = WithAlpha(kBorderFailure, 0.35f + i * 0.55f);
        borderWidth = 2.0f;
        titleColor = kTextFailure;
        statusBadge = " [NG]";
    }
    else
    {
        borderColor = WithAlpha(accent, 0.45f);
    }

    ed::PushStyleColor(ed::StyleColor_NodeBg, bgColor);
    ed::PushStyleColor(ed::StyleColor_NodeBorder, borderColor);
    ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, borderWidth);
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);

    ed::BeginNode(ed::NodeId(node.id));

    // ---- タイトル行 ----
    if (isRoot)
    {
        ImGui::TextColored(kBadgeRoot, "根");
        ImGui::SameLine();
    }
    else if (isOrphan)
    {
        ImGui::TextColored(kBadgeOrphan, "未接続");
        ImGui::SameLine();
    }
    ImGui::TextColored(titleColor, "%s%s%s", node.title.c_str(), desc ? "" : " (未登録)", statusBadge);

    // ツールチップ（説明・設定・状態）
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::TextColored(titleColor, "%s", node.title.c_str());
        ImGui::Separator();
        if (desc)
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextUnformatted(desc->description.c_str());
            ImGui::PopTextWrapPos();
            if (!desc->params.empty() || !desc->textLabel.empty())
            {
                ImGui::Separator();
                for (const auto &p : desc->params)
                    ImGui::Text("%s: %s", p.label.c_str(), FormatParam(p, node.Param(p.index)).c_str());
                if (!desc->textLabel.empty())
                    ImGui::Text("%s: %s", desc->textLabel.c_str(), node.text.c_str());
            }
        }
        else
        {
            ImGui::Text("種類 %d は登録されていません", node.type);
        }
        if (isOrphan)
        {
            ImGui::Separator();
            ImGui::TextColored(kBadgeOrphan, "根から繋がっていないので実行されません");
        }
        ImGui::Separator();
        ImGui::Text("状態: %s", StatusName(status));
        ImGui::EndTooltip();
    }

    // ノード内の短い要約（編集は右のインスペクターで行う。
    // ノード内でコンボを開くとキャンバスの変換で正しく展開されないため）
    if (desc)
    {
        const std::string summary = MakeSummary(node, *desc);
        if (!summary.empty())
            ImGui::TextDisabled("%s", summary.c_str());
    }

    // ---- 入力ピン ----
    ed::BeginPin(ed::PinId(BTPin::Input(node.id)), ed::PinKind::Input);
    ImGui::TextColored(kPinInput, "-> IN");
    ed::EndPin();

    // ---- 出力ピン ----
    const BTNodeKind kind = desc ? desc->kind : BTNodeKind::Action;
    if (kind == BTNodeKind::Condition)
    {
        ed::BeginPin(ed::PinId(BTPin::Success(node.id)), ed::PinKind::Output);
        ImGui::TextColored(kPinSuccess, "OK ->");
        ed::EndPin();
        ed::BeginPin(ed::PinId(BTPin::Failure(node.id)), ed::PinKind::Output);
        ImGui::TextColored(kPinFailure, "NG ->");
        ed::EndPin();
    }
    else if (kind == BTNodeKind::WeightedRandom)
    {
        // 重みは合計に対する割合で見せる（選ばれる確率）
        float total = 0.0f;
        for (const auto &wo : node.weightedOutputs)
            total += std::max(0.0f, wo.weight);
        for (const auto &wo : node.weightedOutputs)
        {
            ed::BeginPin(ed::PinId(wo.pinId), ed::PinKind::Output);
            ImGui::Text("%.0f%% ->", total > 0.0f ? std::max(0.0f, wo.weight) / total * 100.0f : 0.0f);
            ed::EndPin();
        }
    }
    else if (kind == BTNodeKind::Composite)
    {
        ed::BeginPin(ed::PinId(BTPin::Output(node.id)), ed::PinKind::Output);
        ImGui::TextColored(kPinOutput, "OUT ->");
        ed::EndPin();
    }

    ed::EndNode();

    ed::PopStyleVar(2);
    ed::PopStyleColor(2);
}

void BehaviorTreeEditor::DrawLinks(float pulse)
{
    // 実行中は流れる粒の色も付ける
    ed::PushStyleColor(ed::StyleColor_Flow, kLinkFlow);
    ed::PushStyleColor(ed::StyleColor_FlowMarker, kLinkFlowMarker);

    const bool live = isRunning_ || isSingleTesting_;
    for (const auto &link : asset_.links)
    {
        ImVec4 linkColor = kLinkIdle;
        float thickness = 1.5f;
        bool doFlow = false;

        int srcId = -1;
        if (live && GetPinRole(link.start, &srcId) != PinRole::None)
        {
            // 繋がりの色は、出している側が実行中なら「繋いだ先」の状態で決める
            // （実行中のコンポジットから出る線が全部光ると、どの枝を通っているか分からないため）。
            // 出している側が実行中でなければ、出している側の状態で決める
            const int dstId = BTPin::NodeOfInput(link.end);
            const int colorId = GetDisplayStatus(srcId) == NodeStatus::Running ? dstId : srcId;
            const NodeStatus s = GetDisplayStatus(colorId);
            const float t = GetStatusTimer(colorId);
            if (s == NodeStatus::Running)
            {
                linkColor = ImVec4(kLinkFlow.x, 0.78f + pulse * 0.22f, 0.0f, 1.0f);
                thickness = 3.0f;
                doFlow = true;
            }
            else if (s == NodeStatus::Success || t > 0.0f)
            {
                const float i = (s == NodeStatus::Success) ? 1.0f : t / kStatusGlowTime;
                linkColor = WithAlpha(kBorderSuccess, 0.45f + i * 0.55f);
                thickness = 2.0f;
            }
            else if (s == NodeStatus::Failure || t < 0.0f)
            {
                const float i = (s == NodeStatus::Failure) ? 1.0f : (-t) / kStatusGlowTime;
                linkColor = WithAlpha(kBorderFailure, 0.45f + i * 0.55f);
                thickness = 2.0f;
            }
        }

        ed::Link(ed::LinkId(link.id), ed::PinId(link.start), ed::PinId(link.end), linkColor, thickness);
        if (doFlow)
            ed::Flow(ed::LinkId(link.id));
    }

    ed::PopStyleColor(2);
}

// ---------------------------------------------------------
// メニュー
// ---------------------------------------------------------
void BehaviorTreeEditor::DrawCreateMenu()
{
    ed::Suspend();
    if (ed::ShowBackgroundContextMenu())
    {
        // 右クリックしたキャンバス位置に新規ノードを配置する
        createPos_ = ed::ScreenToCanvas(ImGui::GetMousePos());
        pendingLinkPin_ = 0;
        openCreateMenu_ = true;
    }
    if (openCreateMenu_)
    {
        ImGui::OpenPopup(kCreatePopup);
        createSearch_.clear();
        openCreateMenu_ = false;
    }

    if (ImGui::BeginPopup(kCreatePopup))
    {
        const auto &registry = BehaviorTreeRegistry::Get();
        // 入力ピンから引っ張ったときは、出力を持つ種類だけ出す
        const bool needOutput = pendingLinkPin_ != 0 && GetPinRole(pendingLinkPin_) == PinRole::Input;
        auto usable = [needOutput](const BTNodeTypeDesc &t) { return t.create && (!needOutput || t.kind != BTNodeKind::Action); };

        int createdType = -1;
        DimText(pendingLinkPin_ ? "繋いだ状態でノードを追加" : "ノードを追加");
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##btSearch", ICON_FA_SEARCH " 名前・説明で探す", &createSearch_);
        ImGui::Separator();

        if (!createSearch_.empty())
        {
            // 探しているときは分類をまたいで一覧にする（右に分類）
            int hits = 0;
            for (const auto &t : registry.All())
            {
                if (!usable(t))
                    continue;
                if (t.name.find(createSearch_) == std::string::npos && t.description.find(createSearch_) == std::string::npos &&
                    t.category.find(createSearch_) == std::string::npos)
                    continue;
                ++hits;
                if (ImGui::MenuItem(t.name.c_str(), t.category.c_str()))
                    createdType = t.typeId;
                if (ImGui::IsItemHovered() && !t.description.empty())
                    ImGui::SetTooltip("%s", t.description.c_str());
            }
            if (hits == 0)
                DimText("見つかりません");
        }
        else
        {
            for (const auto &category : registry.Categories())
            {
                if (!ImGui::BeginMenu(category.c_str()))
                    continue;
                for (const auto &t : registry.All())
                {
                    if (t.category != category || !usable(t))
                        continue;
                    if (ImGui::MenuItem(t.name.c_str()))
                        createdType = t.typeId;
                    if (ImGui::IsItemHovered() && !t.description.empty())
                        ImGui::SetTooltip("%s", t.description.c_str());
                }
                ImGui::EndMenu();
            }
        }

        if (createdType >= 0)
        {
            const int id = CreateNode(createdType, createPos_);
            ConnectToPendingPin(id);
            if (const BTNodeData *node = asset_.FindNode(id))
                ImGuiNotification::Post("ノードを作成しました: " + node->title);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    else
    {
        pendingLinkPin_ = 0;
    }
    ed::Resume();
}

void BehaviorTreeEditor::DrawContextMenus()
{
    ed::Suspend();
    ed::NodeId nodeId;
    if (ed::ShowNodeContextMenu(&nodeId))
    {
        contextNodeId_ = static_cast<int>(nodeId.Get());
        ImGui::OpenPopup(kNodePopup);
    }
    ed::LinkId linkId;
    if (ed::ShowLinkContextMenu(&linkId))
    {
        contextLinkId_ = static_cast<int>(linkId.Get());
        ImGui::OpenPopup(kLinkPopup);
    }

    if (ImGui::BeginPopup(kNodePopup))
    {
        const BTNodeData *node = asset_.FindNode(contextNodeId_);
        if (!node)
        {
            ImGui::CloseCurrentPopup();
        }
        else
        {
            DimText(node->title.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_VIAL " 単体テスト", nullptr, false, pDebugContext_ != nullptr))
                StartSingleTest(contextNodeId_);
            if (ImGui::MenuItem(ICON_FA_COPY " 複製"))
            {
                // 選択に含まれていれば選択ごと、そうでなければこのノードだけ
                std::vector<int> ids = GetSelectedNodeIds();
                if (std::find(ids.begin(), ids.end(), contextNodeId_) == ids.end())
                    ids = {contextNodeId_};
                DuplicateNodes(ids);
            }
            if (ImGui::MenuItem("つながりを全部外す"))
            {
                const int id = contextNodeId_;
                std::set<int> pins = {BTPin::Input(id), BTPin::Output(id), BTPin::Success(id), BTPin::Failure(id)};
                for (const auto &w : node->weightedOutputs)
                    pins.insert(w.pinId);
                std::erase_if(asset_.links, [&pins](const BTLinkData &l) { return pins.count(l.start) || pins.count(l.end); });
                undoLabel_ = "つながりを外す";
                modified_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_TRASH " 削除"))
                DeleteNode(contextNodeId_);
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup(kLinkPopup))
    {
        if (ImGui::MenuItem(ICON_FA_TRASH " 接続を削除"))
        {
            const int id = contextLinkId_;
            std::erase_if(asset_.links, [id](const BTLinkData &l) { return l.id == id; });
            undoLabel_ = "接続を削除";
            modified_ = true;
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}

std::string BehaviorTreeEditor::MakeSummary(const BTNodeData &node, const BTNodeTypeDesc &desc) const
{
    if (desc.summary)
        return desc.summary(node);
    // 数値の設定を最大3つ「/」で並べる
    std::string result;
    int count = 0;
    for (const auto &p : desc.params)
    {
        if (count >= 3)
            break;
        if (!result.empty())
            result += " / ";
        result += FormatParam(p, node.Param(p.index));
        ++count;
    }
    return result;
}

} // namespace Hagine

#endif // USE_IMGUI
