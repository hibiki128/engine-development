#define NOMINMAX
#ifdef USE_IMGUI
#include "ImGuizmoManager.h"
#include "ImGuiNotification.h"
#include <Mymath.h>
#include <algorithm>
#include <edit/undo/UndoRedoManager.h>
#include <format>
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <metaball/MetaBallObject.h>
#include <numbers>
#include <object/base/BaseObjectManager.h>
#include <transform/WorldTransform.h>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "DebugUIHelper.h"

// =======================================================================
// ImGuizmoManager: インスペクタ窓（選択中の物の詳細・複数選択のまとめて編集）
//
// ギズモ窓の下に詳細を積んでいた頃は、操作モードやフィルタの設定と
// オブジェクトの中身が1つの窓に混ざって縦に長くなっていた。
// 中身だけを独立した窓に出し、見出しに「何を見ているか」を常に置く。
// =======================================================================

namespace Hagine {
namespace {
/// <summary>ImGui の色を通知用の色へ変換する</summary>
Vector4 ToColor(const ImVec4 &color)
{
    return {color.x, color.y, color.z, color.w};
}

/// <summary>対象の種類を表すアイコン（階層ビューと同じ割り当て）</summary>
const char *TargetIcon(const GizmoTarget &target)
{
    switch (target.category)
    {
    case GizmoCategory::Sprite:
        return ICON_FA_IMAGE;
    case GizmoCategory::Particle:
        return ICON_FA_STAR;
    case GizmoCategory::Light:
        return ICON_FA_LIGHTBULB;
    default:
        break;
    }
    if (target.type == GizmoTarget::Type::BaseObject && target.baseObject)
    {
        BaseObject *obj = target.baseObject;
        if (dynamic_cast<MetaBallObject *>(obj))
        {
            return ICON_FA_CIRCLE;
        }
        if (obj->IsPrimitive())
        {
            return ICON_FA_CUBE;
        }
        if (obj->GetObject3d() && obj->GetObject3d()->GetModel() && obj->GetObject3d()->GetHaveAnimation())
        {
            return ICON_FA_RUNNING;
        }
        return ICON_FA_SHAPES;
    }
    return ICON_FA_ARROWS_ALT;
}

/// <summary>種類の表示名</summary>
const char *CategoryLabel(const GizmoTarget &target)
{
    switch (target.category)
    {
    case GizmoCategory::Sprite:
        return "スプライト";
    case GizmoCategory::Particle:
        return "パーティクル";
    case GizmoCategory::Light:
        return "ライト";
    default:
        break;
    }
    if (target.type == GizmoTarget::Type::BaseObject && target.baseObject)
    {
        BaseObject *obj = target.baseObject;
        if (dynamic_cast<MetaBallObject *>(obj))
        {
            return "メタボール";
        }
        if (obj->IsPrimitive())
        {
            return "プリミティブ";
        }
        return "モデル";
    }
    return "トランスフォーム";
}

/// <summary>種類ごとのアクセント色（見出しの色帯・アイコン）</summary>
ImVec4 CategoryAccent(GizmoCategory category)
{
    switch (category)
    {
    case GizmoCategory::Sprite:
        return DebugTheme::kAccentPurple;
    case GizmoCategory::Particle:
        return DebugTheme::kAccentOrange;
    case GizmoCategory::Light:
        return DebugTheme::kAccentYellow;
    default:
        return DebugTheme::kAccentBlue;
    }
}

/// <summary>地を持たないアイコンボタン（見出しの右端に並べる）</summary>
bool HeaderIconButton(const char *id, const char *icon, bool active, const char *tooltip)
{
    const ImVec4 textColor = active ? ImGui::GetStyleColorVec4(ImGuiCol_CheckMark)
                                    : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    ScopedButtonColors colors(active ? DebugTheme::kButtonNeutral : DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    ImGui::PushID(id);
    const bool pressed = ImGui::Button(icon, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
    ImGui::PopID();
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

/// <summary>何も選んでいないときの案内</summary>
void DrawEmptyState()
{
    const float availY = ImGui::GetContentRegionAvail().y;
    ImGui::Dummy(ImVec2(0.0f, (std::max)(availY * 0.28f, 12.0f)));

    auto centered = [](const char *text, const ImVec4 &color) {
        const float width = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX((std::max)((ImGui::GetContentRegionAvail().x - width) * 0.5f, 0.0f) + ImGui::GetCursorPosX());
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    };
    centered(ICON_FA_MOUSE_POINTER, DebugTheme::kTextDim);
    ImGui::Spacing();
    centered("何も選択されていません", DebugTheme::kTextReadOnly);
    ImGui::Spacing();
    centered("シーンをクリック・階層の行をクリック", DebugTheme::kTextDim);
    centered("Ctrl+K で名前から探すこともできます", DebugTheme::kTextDim);
}

/// <summary>
/// 数値1つを「全員同じならその値、違えば混在」と見せつつ、ドラッグした量を全員に足す欄。
/// 戻り値はこのフレームで動いた量（動いていなければ 0）
/// </summary>
float MixedAxisDrag(const char *id, float average, bool mixed, float speed, const ImVec4 &frameBg)
{
    float value = average;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, frameBg);
    ImGui::SetNextItemWidth(-1);
    const bool changed = ImGui::DragFloat(id, &value, speed, 0.0f, 0.0f, mixed ? "混在 (%.2f)" : "%.2f");
    ImGui::PopStyleColor();
    if (mixed)
    {
        ImGui::SetItemTooltip("選択中の値がそろっていません。ドラッグした量を全員へ足します");
    }
    return changed ? (value - average) : 0.0f;
}
} // namespace

// ---- 見出し -------------------------------------------------------------

void ImGuizmoManager::DrawInspectorHeader(GizmoTarget &target)
{
    const ImVec4 accent = CategoryAccent(target.category);
    BaseObject *obj = (target.type == GizmoTarget::Type::BaseObject) ? target.baseObject : nullptr;
    const bool pinned = !pinnedName_.empty();

    // 見出しカード（アクセント色の薄い地）
    ImVec4 cardBg = accent;
    cardBg.w = 0.10f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::BeginChild("##inspectorHeader", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);

    // 左端の色帯
    {
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddRectFilled(windowPos, ImVec2(windowPos.x + 3.0f, windowPos.y + windowSize.y),
                                                  ImGui::ColorConvertFloat4ToU32(accent), 6.0f, ImDrawFlags_RoundCornersLeft);
    }

    // ---- 1行目: アイコン・名前 ／ 右端に操作ボタン ----
    const float buttonSize = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const int buttonCount = obj ? 4 : 3; // 表示・フォーカス・ピン・メニュー（表示は BaseObject のみ）
    const float buttonsWidth = buttonSize * buttonCount + spacing * (buttonCount - 1);

    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::TextUnformatted(TargetIcon(target));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    {
        // 名前が長いときは右のボタンに食い込まないよう切り詰める
        const float nameWidth = ImGui::GetContentRegionAvail().x - buttonsWidth - spacing;
        const ImVec2 textPos = ImGui::GetCursorScreenPos();
        const float textOffsetY = (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f;
        ImGui::Dummy(ImVec2((std::max)(nameWidth, 10.0f), ImGui::GetFrameHeight()));
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(textPos.x, textPos.y + textOffsetY),
                                  ImVec2(textPos.x + (std::max)(nameWidth, 10.0f), textPos.y + ImGui::GetFrameHeight()),
                                  textPos.x + (std::max)(nameWidth, 10.0f), target.name.c_str(), nullptr, nullptr);
        ImGui::SetItemTooltip("%s", target.name.c_str());
    }

    ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonsWidth);
    if (obj)
    {
        const bool visible = obj->GetIsModelDraw();
        if (HeaderIconButton("vis", visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH, !visible,
                             visible ? "表示中（クリックで隠す）" : "非表示（クリックで表示）"))
        {
            obj->SetIsModelDraw(!visible);
        }
        ImGui::SameLine();
    }
    if (HeaderIconButton("focus", ICON_FA_CROSSHAIRS, false, "カメラを寄せる (F)"))
    {
        // ピン留め中でも見ている物へ寄れるよう、選択をそろえてから寄せる
        SelectOnly(target.name);
        RequestFocusOnSelection();
    }
    ImGui::SameLine();
    if (HeaderIconButton("pin", ICON_FA_THUMBTACK, pinned,
                         pinned ? "ピン留め中（クリックで選択に追従へ戻す）" : "ピン留め: 他を選んでもこの表示のままにする"))
    {
        pinnedName_ = pinned ? std::string() : target.name;
    }
    ImGui::SameLine();
    if (HeaderIconButton("menu", ICON_FA_ELLIPSIS_H, false, "その他の操作"))
    {
        ImGui::OpenPopup("##inspectorMenu");
    }
    if (ImGui::BeginPopup("##inspectorMenu"))
    {
        ImGui::TextDisabled("%s", target.name.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_COPY " トランスフォームをコピー"))
        {
            CopyTransformFrom(target);
        }
        if (ImGui::BeginMenu(ICON_FA_PASTE " 選択中へ貼り付け", transformClipboard_.valid))
        {
            if (ImGui::MenuItem("位置・回転・拡縮すべて"))
                PasteTransformToSelection(true, true, true);
            if (ImGui::MenuItem("位置だけ"))
                PasteTransformToSelection(true, false, false);
            if (ImGui::MenuItem("回転だけ"))
                PasteTransformToSelection(false, true, false);
            if (ImGui::MenuItem("拡縮だけ"))
                PasteTransformToSelection(false, false, true);
            ImGui::EndMenu();
        }
        if (obj)
        {
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_CLONE " 複製", "Ctrl+D"))
            {
                SelectOnly(target.name);
                DuplicateSelectedObjects();
            }
            ImGui::Separator();
            DrawPrefabLinkMenuItems(obj);
        }
        if (ImGui::MenuItem(ICON_FA_COPY " 名前をコピー"))
        {
            ImGui::SetClipboardText(target.name.c_str());
        }
        if (obj)
        {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
            if (ImGui::MenuItem(ICON_FA_TRASH_ALT " 削除", "Delete"))
            {
                SelectOnly(target.name);
                pinnedName_.clear();
                DeleteSelectedObjects();
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }

    // ---- 2行目: 戻る・進む ／ 種類バッジ ／ 親のパンくず ----
    {
        const bool canBack = inspectorHistoryIndex_ > 0;
        const bool canForward = inspectorHistoryIndex_ + 1 < static_cast<int>(inspectorHistory_.size());
        ScopedButtonColors colors(DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
        ImGui::BeginDisabled(!canBack);
        if (ImGui::Button(ICON_FA_CHEVRON_LEFT "##inspBack"))
            NavigateInspectorHistory(-1);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("前に見ていた物へ戻る（マウスの戻るボタン）");
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::BeginDisabled(!canForward);
        if (ImGui::Button(ICON_FA_CHEVRON_RIGHT "##inspForward"))
            NavigateInspectorHistory(1);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("進む（マウスの進むボタン）");
        ImGui::SameLine();
    }
    StatusBadge(CategoryLabel(target), accent);
    // プレハブから置いた物は、プレハブ名の札（押すとプレハブの操作）
    if (obj && !obj->GetPrefabSource().empty())
    {
        ImGui::SameLine();
        ImVec4 prefabColor = DebugTheme::kAccentGreen;
        ScopedButtonColors colors(ImVec4(prefabColor.x, prefabColor.y, prefabColor.z, 0.18f),
                                  ImVec4(prefabColor.x, prefabColor.y, prefabColor.z, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_Text, prefabColor);
        if (ImGui::Button((std::string(ICON_FA_BOX " ") + obj->GetPrefabSource() + "##prefabBadge").c_str()))
        {
            ImGui::OpenPopup("##prefabLinkMenu");
        }
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("プレハブから置いた物です。押すと「反映」「置き直す」など");
        if (ImGui::BeginPopup("##prefabLinkMenu"))
        {
            DrawPrefabLinkMenuItems(obj);
            ImGui::EndPopup();
        }
    }
    if (pinned)
    {
        ImGui::SameLine();
        StatusBadge(ICON_FA_THUMBTACK " ピン留め", DebugTheme::kAccentYellow);
    }
    if (obj && obj->GetParent())
    {
        // 親をたどって「祖先 > 親」と並べる。押すとその親を選ぶ
        std::vector<BaseObject *> ancestors;
        for (BaseObject *p = obj->GetParent(); p; p = p->GetParent())
        {
            ancestors.push_back(p);
        }
        std::reverse(ancestors.begin(), ancestors.end());

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(ICON_FA_LEVEL_UP_ALT);
        ImGui::SetItemTooltip("親");
        for (size_t i = 0; i < ancestors.size(); ++i)
        {
            ImGui::SameLine(0.0f, 2.0f);
            ScopedButtonColors colors(DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Button(ancestors[i]->GetName().c_str()))
            {
                pinnedName_.clear();
                SelectOnly(ancestors[i]->GetName());
            }
            ImGui::PopID();
            if (i + 1 < ancestors.size())
            {
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::TextDisabled(ICON_FA_CHEVRON_RIGHT);
            }
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ---- 項目検索 -----------------------------------------------------------

void ImGuizmoManager::DrawInspectorSearchBar()
{
    // インスペクタにフォーカスがあるときの Ctrl+F で検索欄へ入る
    const bool focusRequested = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                                ImGui::IsKeyPressed(ImGuiKey_F, false) && ImGui::GetIO().KeyCtrl;
    if (focusRequested)
    {
        ImGui::SetKeyboardFocusHere();
    }

    const bool active = !inspectorFilter_.empty();
    const float clearWidth = active ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clearWidth);
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentYellow));
    }
    ImGui::InputTextWithHint("##inspectorSearch", ICON_FA_SEARCH " 見出し・タブの名前で絞る (Ctrl+F)", &inspectorFilter_,
                             ImGuiInputTextFlags_EscapeClearsAll);
    if (active)
    {
        ImGui::PopStyleColor();
    }
    ImGui::SetItemTooltip("例: 「マテリアル」「コライダー」「物理」\n"
                          "一致した見出しを開いた状態で、タブをまたいで並べます（Esc で消す）");
    if (active)
    {
        ImGui::SameLine();
        ScopedButtonColors colors(DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
        if (ImGui::Button(ICON_FA_TIMES "##inspectorSearchClear", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
        {
            inspectorFilter_.clear();
        }
        ImGui::SetItemTooltip("検索を消す");
    }
}

// ---- 本体 ---------------------------------------------------------------

void ImGuizmoManager::DrawInspector()
{
    // ピン留めしていた物が消えたら、選択への追従に戻す
    if (!pinnedName_.empty() && transformMap_.find(pinnedName_) == transformMap_.end())
    {
        pinnedName_.clear();
    }

    if (pinnedName_.empty() && selectedNames_.size() > 1)
    {
        DrawMultiSelectionInspector();
        return;
    }

    // マウスのサイドボタンで戻る・進む（インスペクタの上にマウスがあるときだけ）
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle + 1))
            NavigateInspectorHistory(-1);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle + 2))
            NavigateInspectorHistory(1);
    }

    const std::string name = !pinnedName_.empty() ? pinnedName_
                                                   : (selectedNames_.empty() ? std::string() : *selectedNames_.begin());
    auto it = name.empty() ? transformMap_.end() : transformMap_.find(name);
    if (it == transformMap_.end())
    {
        DrawEmptyState();
        return;
    }

    if (pinnedName_.empty())
    {
        RecordInspectorHistory(name);
    }
    DrawInspectorHeader(it->second);
    ImGui::Spacing();
    DrawInspectorSearchBar();

    // 詳細はスクロール領域に入れ、見出しを常に見えるところへ残す
    ImGui::BeginChild("##inspectorBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    ImGui::PushID(name.c_str());
    {
        InspectorSearch::Scope searchScope(inspectorFilter_);
        it->second.ShowImGui();
        if (InspectorSearch::IsActive() && InspectorSearch::MatchCount() == 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextWrapped(ICON_FA_SEARCH " 「%s」に一致する見出し・タブがありません", inspectorFilter_.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::PopID();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextWrapped(ICON_FA_INFO_CIRCLE " 数値欄は右クリックでコピー・貼り付け・既定値に戻す");
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

// ---- 履歴（戻る・進む） ---------------------------------------------------

void ImGuizmoManager::RecordInspectorHistory(const std::string &name)
{
    if (inspectorHistoryIndex_ >= 0 && inspectorHistoryIndex_ < static_cast<int>(inspectorHistory_.size()) &&
        inspectorHistory_[inspectorHistoryIndex_] == name)
    {
        return; // 同じ物を見続けている
    }
    // 戻った先から別の物を選んだら、その先の履歴は捨てる（ブラウザと同じ）
    inspectorHistory_.resize(static_cast<size_t>(inspectorHistoryIndex_ + 1));
    inspectorHistory_.push_back(name);
    if (inspectorHistory_.size() > kInspectorHistoryMax)
    {
        inspectorHistory_.erase(inspectorHistory_.begin());
    }
    inspectorHistoryIndex_ = static_cast<int>(inspectorHistory_.size()) - 1;
}

void ImGuizmoManager::NavigateInspectorHistory(int step)
{
    // 消えた物は飛ばして、次に見られる物まで進む
    for (int index = inspectorHistoryIndex_ + step; index >= 0 && index < static_cast<int>(inspectorHistory_.size()); index += step)
    {
        if (transformMap_.find(inspectorHistory_[index]) != transformMap_.end())
        {
            inspectorHistoryIndex_ = index;
            pinnedName_.clear();
            SelectOnly(inspectorHistory_[index]);
            return;
        }
    }
}

// ---- 複数選択 -----------------------------------------------------------

void ImGuizmoManager::DrawMultiSelectionInspector()
{
    // 見出し: 何個・どの種類を選んでいるか
    std::vector<GizmoTarget *> targets;
    int countByCategory[kGizmoCategoryCount] = {};
    for (const std::string &name : selectedNames_)
    {
        auto it = transformMap_.find(name);
        if (it == transformMap_.end())
        {
            continue;
        }
        targets.push_back(&it->second);
        ++countByCategory[static_cast<int>(it->second.category)];
    }
    std::sort(targets.begin(), targets.end(),
              [](const GizmoTarget *lhs, const GizmoTarget *rhs) { return lhs->name < rhs->name; });

    ImVec4 cardBg = DebugTheme::kAccentCyan;
    cardBg.w = 0.10f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::BeginChild("##multiHeader", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentCyan);
    ImGui::Text(ICON_FA_LAYER_GROUP "  %d 個を選択中", static_cast<int>(targets.size()));
    ImGui::PopStyleColor();
    static const char *kCategoryNames[kGizmoCategoryCount] = {"オブジェクト", "スプライト", "パーティクル", "ライト"};
    bool first = true;
    for (int i = 0; i < kGizmoCategoryCount; ++i)
    {
        if (countByCategory[i] == 0)
        {
            continue;
        }
        if (!first)
        {
            ImGui::SameLine();
        }
        first = false;
        StatusBadge(std::format("{} {}", kCategoryNames[i], countByCategory[i]).c_str(),
                    CategoryAccent(static_cast<GizmoCategory>(i)));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // 3D の対象（スプライトはピクセル座標なので別扱い）
    std::vector<GizmoTarget *> spatial;
    std::vector<BaseObject *> objects;
    for (GizmoTarget *target : targets)
    {
        if (!target->isScreenSpace)
        {
            spatial.push_back(target);
        }
        if (target->type == GizmoTarget::Type::BaseObject && target->baseObject)
        {
            objects.push_back(target->baseObject);
        }
    }

    ImGui::BeginChild("##multiBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);

    // ---- まとめて動かす ----
    if (!spatial.empty() && ThemedHeader(ICON_FA_ARROWS_ALT "  トランスフォーム（まとめて）##multiTf", DebugTheme::kAccentBlue, true))
    {
        ImGui::Indent(6.0f);

        // 位置（ワールド）。そろっていない軸は「混在」と出し、動かした量を全員へ足す
        Vector3 minPos = spatial[0]->GetWorldPosition();
        Vector3 maxPos = minPos;
        Vector3 sumPos = {0.0f, 0.0f, 0.0f};
        for (GizmoTarget *target : spatial)
        {
            const Vector3 p = target->GetWorldPosition();
            minPos = {(std::min)(minPos.x, p.x), (std::min)(minPos.y, p.y), (std::min)(minPos.z, p.z)};
            maxPos = {(std::max)(maxPos.x, p.x), (std::max)(maxPos.y, p.y), (std::max)(maxPos.z, p.z)};
            sumPos = sumPos + p;
        }
        const Vector3 average = sumPos / static_cast<float>(spatial.size());
        constexpr float kMixedEpsilon = 1e-4f;

        if (ImGui::BeginTable("##multiPos", 4, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX))
        {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.5f);
            ImGui::TableSetupColumn("x");
            ImGui::TableSetupColumn("y");
            ImGui::TableSetupColumn("z");

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(DebugTheme::kAccentBlue, "位置");
            ImGui::SetItemTooltip("ワールド座標。ドラッグした量を全員に足します");

            const float averages[3] = {average.x, average.y, average.z};
            const float mins[3] = {minPos.x, minPos.y, minPos.z};
            const float maxs[3] = {maxPos.x, maxPos.y, maxPos.z};
            static const ImVec4 kAxisBg[3] = {DebugTheme::FrameBg(DebugTheme::kAccentRed),
                                              DebugTheme::FrameBg(DebugTheme::kAccentGreen),
                                              DebugTheme::FrameBg(DebugTheme::kAccentBlue)};
            static const char *kAxisIds[3] = {"##mpx", "##mpy", "##mpz"};
            for (int axis = 0; axis < 3; ++axis)
            {
                ImGui::TableNextColumn();
                const bool mixed = (maxs[axis] - mins[axis]) > kMixedEpsilon;
                const float delta = MixedAxisDrag(kAxisIds[axis], averages[axis], mixed, 0.1f, kAxisBg[axis]);
                if (delta != 0.0f)
                {
                    for (GizmoTarget *target : spatial)
                    {
                        Vector3 p = target->GetWorldPosition();
                        (axis == 0 ? p.x : (axis == 1 ? p.y : p.z)) += delta;
                        SetTargetWorldPosition(*target, p);
                    }
                }
            }
            ImGui::EndTable();
        }

        // 回転（ワールド軸まわりに、それぞれの中心で回す）。動かした量を足して欄は0へ戻す
        {
            static Vector3 deltaDegrees{};
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(DebugTheme::kAccentCyan, "回転を加える");
            ImGui::SameLine(ImGui::GetFontSize() * 4.5f + ImGui::GetStyle().CellPadding.x);
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentCyan));
            Vector3 before = deltaDegrees;
            if (ImGui::DragFloat3("##multiRot", &deltaDegrees.x, 0.2f, 0.0f, 0.0f, "%.1f°"))
            {
                const float toRad = std::numbers::pi_v<float> / 180.0f;
                const Vector3 step = (deltaDegrees - before) * toRad;
                const Matrix4x4 rotation = MakeRotateXMatrix(step.x) * MakeRotateYMatrix(step.y) * MakeRotateZMatrix(step.z);
                for (GizmoTarget *target : spatial)
                {
                    Matrix4x4 world = target->GetWorldMatrix();
                    const Vector3 position = {world.m[3][0], world.m[3][1], world.m[3][2]};
                    world.m[3][0] = world.m[3][1] = world.m[3][2] = 0.0f;
                    world = world * rotation;
                    world.m[3][0] = position.x;
                    world.m[3][1] = position.y;
                    world.m[3][2] = position.z;
                    target->ApplyWorldMatrix(world);
                }
            }
            ImGui::PopStyleColor();
            if (!ImGui::IsItemActive())
            {
                deltaDegrees = {};
            }
        }

        // 拡縮（全員に同じ倍率を掛ける）
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(DebugTheme::kAccentGreen, "拡縮を掛ける");
            ImGui::SameLine(ImGui::GetFontSize() * 4.5f + ImGui::GetStyle().CellPadding.x);
            const float presetWidth = ImGui::CalcTextSize("×0.5").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (presetWidth + ImGui::GetStyle().ItemSpacing.x) * 2.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentGreen));
            const float previous = multiScaleFactor_;
            float applyRatio = 1.0f;
            if (ImGui::DragFloat("##multiScale", &multiScaleFactor_, 0.005f, 0.05f, 20.0f, "×%.3f"))
            {
                applyRatio = multiScaleFactor_ / (std::max)(previous, 1e-4f);
            }
            ImGui::PopStyleColor();
            if (!ImGui::IsItemActive())
            {
                multiScaleFactor_ = 1.0f;
            }
            ImGui::SameLine();
            if (NeutralButton("×0.5"))
                applyRatio = 0.5f;
            ImGui::SameLine();
            if (NeutralButton("×2"))
                applyRatio = 2.0f;

            if (applyRatio != 1.0f)
            {
                for (GizmoTarget *target : spatial)
                {
                    Matrix4x4 world = target->GetWorldMatrix();
                    for (int row = 0; row < 3; ++row)
                    {
                        for (int column = 0; column < 3; ++column)
                        {
                            world.m[row][column] *= applyRatio;
                        }
                    }
                    target->ApplyWorldMatrix(world);
                }
            }
        }

        ImGui::Spacing();
        if (PrimaryButton(ICON_FA_ARROW_DOWN " 地面に接地", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 2.0f, 0.0f)))
        {
            SnapSelectedToGround();
        }
        ImGui::SameLine();
        if (NeutralButton(ICON_FA_CROSSHAIRS " カメラを寄せる", ImVec2(-1.0f, 0.0f)))
        {
            RequestFocusOnSelection();
        }
        if (transformClipboard_.valid)
        {
            if (NeutralButton(ICON_FA_PASTE " コピーしたトランスフォームを全員へ貼る", ImVec2(-1.0f, 0.0f)))
            {
                PasteTransformToSelection(true, true, true);
            }
        }
        ImGui::Unindent(6.0f);
        ImGui::Spacing();
    }

    // ---- 共通の設定（BaseObject のみ）----
    if (!objects.empty() && ThemedHeader(ICON_FA_SLIDERS_H "  共通の設定##multiCommon", DebugTheme::kAccentPurple, true))
    {
        ImGui::Indent(6.0f);
        // チェックが全員そろっていなければ「－」表示にする（押すと全員 ON）
        auto mixedCheckbox = [&](const char *label, auto getter, auto setter) {
            int onCount = 0;
            for (BaseObject *obj : objects)
            {
                onCount += getter(obj) ? 1 : 0;
            }
            const bool mixed = onCount > 0 && onCount < static_cast<int>(objects.size());
            bool value = onCount == static_cast<int>(objects.size());
            if (mixed)
            {
                ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            }
            const bool clicked = AccentCheckbox(label, &value, DebugTheme::kAccentPurple);
            if (mixed)
            {
                ImGui::PopItemFlag();
            }
            if (clicked)
            {
                const bool next = mixed ? true : value;
                for (BaseObject *obj : objects)
                {
                    setter(obj, next);
                }
            }
        };
        InlineColumns columns(3);
        mixedCheckbox("表示", [](BaseObject *o) { return o->GetIsModelDraw(); },
                      [](BaseObject *o, bool v) { o->SetIsModelDraw(v); });
        columns.Next(1);
        mixedCheckbox("ライティング", [](BaseObject *o) { return o->GetLighting(); },
                      [](BaseObject *o, bool v) { o->GetLighting() = v; });
        columns.Next(2);
        mixedCheckbox("ギズモで選べる", [](BaseObject *o) { return o->IsGizmoSelectable(); },
                      [](BaseObject *o, bool v) { o->SetGizmoSelectable(v); });

        ImGui::Spacing();
        if (ConfirmButton(ICON_FA_CLONE " まとめて複製", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 2.0f, 0.0f)))
        {
            DuplicateSelectedObjects();
        }
        ImGui::SameLine();
        if (DangerButton(ICON_FA_TRASH_ALT " まとめて削除", ImVec2(-1.0f, 0.0f)))
        {
            DeleteSelectedObjects();
        }
        ImGui::Unindent(6.0f);
        ImGui::Spacing();
    }

    // ---- 選択中の一覧（押すとその1つだけを選ぶ・Ctrl+クリックで外す）----
    if (ThemedHeader(ICON_FA_LIST "  選択中の一覧##multiList", DebugTheme::kAccentCyan, false))
    {
        std::string selectOnly;
        std::string deselect;
        for (GizmoTarget *target : targets)
        {
            ImGui::PushID(target->name.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, CategoryAccent(target->category));
            ImGui::TextUnformatted(TargetIcon(*target));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Selectable(target->name.c_str(), false))
            {
                if (ImGui::GetIO().KeyCtrl)
                    deselect = target->name;
                else
                    selectOnly = target->name;
            }
            ImGui::SetItemTooltip("クリック: これだけを選ぶ / Ctrl+クリック: 選択から外す");
            ImGui::PopID();
        }
        // 一覧を回し終えてから選択を変える（走査中に集合を書き換えない）
        if (!selectOnly.empty())
            SelectOnly(selectOnly);
        if (!deselect.empty())
            selectedNames_.erase(deselect);
    }

    ImGui::EndChild();
}

// ---- トランスフォームのコピー・貼り付け ---------------------------------

void ImGuizmoManager::CopyTransformFrom(const GizmoTarget &target)
{
    Vector3 translation{};
    Quaternion rotation{};
    Vector3 scale{};
    DecomposeMatrix(target.GetWorldMatrix(), translation, rotation, scale);
    transformClipboard_.valid = true;
    transformClipboard_.translation = translation;
    transformClipboard_.rotation = rotation;
    transformClipboard_.scale = scale;

    // 位置だけは数値の貼り付け先（右クリックメニュー）でも使えるよう OS のクリップボードにも入れる
    const float values[3] = {translation.x, translation.y, translation.z};
    ImGui::SetClipboardText(ValueClipboard::Format(values, 3).c_str());
    ImGuiNotification::Post("トランスフォームをコピーしました: " + target.name, ToColor(DebugTheme::kAccentBlue));
}

void ImGuizmoManager::PasteTransformToSelection(bool translation, bool rotation, bool scale)
{
    if (!transformClipboard_.valid)
    {
        return;
    }
    int pastedCount = 0;
    for (const std::string &name : selectedNames_)
    {
        auto it = transformMap_.find(name);
        if (it == transformMap_.end() || it->second.isScreenSpace)
        {
            continue;
        }
        GizmoTarget &target = it->second;

        // 今のワールド成分を分解し、指定された成分だけ差し替えて組み直す
        Vector3 currentTranslation{};
        Quaternion currentRotation{};
        Vector3 currentScale{};
        DecomposeMatrix(target.GetWorldMatrix(), currentTranslation, currentRotation, currentScale);
        const Vector3 t = translation ? transformClipboard_.translation : currentTranslation;
        const Quaternion r = rotation ? transformClipboard_.rotation : currentRotation;
        const Vector3 s = scale ? transformClipboard_.scale : currentScale;
        target.ApplyWorldMatrix(MakeAffineMatrix(s, r, t));
        ++pastedCount;
    }
    if (pastedCount > 0)
    {
        ImGuiNotification::Post(std::format("トランスフォームを {} 個に貼り付けました", pastedCount), ToColor(DebugTheme::kAccentBlue));
    }
}

// ---- プレハブ保存ダイアログ ---------------------------------------------

void ImGuizmoManager::OpenPrefabSaveDialog(const std::string &rootName)
{
    prefabDialogRequested_ = true;
    prefabDialogRoot_ = rootName;
    prefabDialogName_ = rootName;
}

void ImGuizmoManager::DrawEditorModals()
{
    const char *kPopupName = ICON_FA_BOX " プレハブとして保存";
    if (prefabDialogRequested_)
    {
        ImGui::OpenPopup(kPopupName);
        prefabDialogRequested_ = false;
    }

    // マルチビューポートで別のOSウィンドウへ飛ばないよう、メイン画面の中央に固定する
    const ImGuiViewport *mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(mainViewport->ID);
    ImGui::SetNextWindowPos(mainViewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        BaseObjectManager *manager = BaseObjectManager::GetInstance();
        BaseObject *root = manager->GetObjectByName(prefabDialogRoot_);
        if (!root)
        {
            ImGui::TextDisabled("対象のオブジェクトが見つかりません");
            if (NeutralButton("閉じる", ImVec2(120.0f, 0.0f)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        // 子孫の数（何個ぶんのプレハブになるか）
        int subtreeCount = 0;
        std::vector<BaseObject *> stack = {root};
        while (!stack.empty())
        {
            BaseObject *node = stack.back();
            stack.pop_back();
            ++subtreeCount;
            for (BaseObject *child : *node->GetChildren())
                stack.push_back(child);
        }

        ImGui::Text("「%s」を子 %d 個ごと保存します", prefabDialogRoot_.c_str(), subtreeCount - 1);
        ImGui::TextDisabled("根の位置は原点に直して保存され、置いた場所が根の位置になります");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(320.0f);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("プレハブ名", &prefabDialogName_, ImGuiInputTextFlags_EnterReturnsTrue);

        const std::vector<std::string> existing = manager->ListPrefabNames();
        const bool overwrite = std::find(existing.begin(), existing.end(), prefabDialogName_) != existing.end();
        if (overwrite)
        {
            ImGui::TextColored(DebugTheme::kAccentOrange, ICON_FA_EXCLAMATION_TRIANGLE " 同じ名前のプレハブを上書きします");
        }

        ImGui::Spacing();
        const bool canSave = !prefabDialogName_.empty();
        ImGui::BeginDisabled(!canSave);
        if (ConfirmButton("保存", ImVec2(120.0f, 0.0f)) || (enter && canSave))
        {
            if (manager->SavePrefab(prefabDialogRoot_, prefabDialogName_))
            {
                // 保存した元の物もそのプレハブとつながる（そのまま「反映」「置き直す」ができる）
                root->SetPrefabSource(prefabDialogName_);
                ImGuiNotification::Post("プレハブを保存しました: " + prefabDialogName_, ToColor(DebugTheme::kAccentGreen));
            }
            else
            {
                ImGuiNotification::Post("プレハブを保存できませんでした", ToColor(DebugTheme::kAccentRed));
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (NeutralButton("キャンセル", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ---- シーンビューの名前ラベル -------------------------------------------

std::vector<ImGuizmoManager::LabelTarget> ImGuizmoManager::CollectLabelTargets(bool selectedOnly) const
{
    std::vector<LabelTarget> targets;
    for (const auto &[name, target] : transformMap_)
    {
        const bool selected = selectedNames_.find(name) != selectedNames_.end();
        // クリック対象から外した種類でも、選択中なら名前を出す
        if (target.isScreenSpace || (!selected && !IsCategoryEnabled(target.category)))
        {
            continue;
        }
        if (selectedOnly && !selected)
        {
            continue;
        }
        targets.push_back({name, target.GetWorldPosition(), target.category, selected});
    }
    // 並びを毎フレーム同じにする（ラベルの重なり順がちらつかないように）
    std::sort(targets.begin(), targets.end(), [](const LabelTarget &a, const LabelTarget &b) { return a.name < b.name; });
    return targets;
}

std::vector<ImGuizmoManager::LabelTarget> ImGuizmoManager::CollectIconTargets(GizmoCategory category) const
{
    std::vector<LabelTarget> targets;
    for (const auto &[name, target] : transformMap_)
    {
        if (target.category != category || target.isScreenSpace || !target.sceneIcon || !target.selectable)
        {
            continue;
        }
        const bool selected = selectedNames_.find(name) != selectedNames_.end();
        targets.push_back({name, target.GetWorldPosition(), target.category, selected});
    }
    std::sort(targets.begin(), targets.end(), [](const LabelTarget &a, const LabelTarget &b) { return a.name < b.name; });
    return targets;
}

// ---- 視点そろえ ---------------------------------------------------------

bool ImGuizmoManager::ConsumeViewAlignRequest(float &outPitch, float &outYaw, bool &outHasPivot, Vector3 &outPivot)
{
    if (!viewAlignRequested_)
    {
        return false;
    }
    viewAlignRequested_ = false;
    outPitch = viewAlignPitch_;
    outYaw = viewAlignYaw_;

    // 選択物があればその重心を中心に回り込む（無ければカメラ側で前方の点を使う）
    Vector3 center = {0.0f, 0.0f, 0.0f};
    int count = 0;
    for (const std::string &name : selectedNames_)
    {
        auto it = transformMap_.find(name);
        if (it == transformMap_.end() || it->second.isScreenSpace)
        {
            continue;
        }
        center = center + it->second.GetWorldPosition();
        ++count;
    }
    outHasPivot = count > 0;
    outPivot = count > 0 ? center / static_cast<float>(count) : Vector3{0.0f, 0.0f, 0.0f};
    return true;
}

} // namespace Hagine
#endif // USE_IMGUI
