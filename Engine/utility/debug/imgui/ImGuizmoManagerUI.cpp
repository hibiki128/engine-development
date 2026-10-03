#define NOMINMAX
#ifdef USE_IMGUI
#include "ImGuizmoManager.h"
#include "ImGuiNotification.h"
#include "Input.h"
#include "Sprite.h"
#include <line/LineRenderer.h>
#include <object/base/BaseObjectManager.h>
#include <transform/WorldTransform.h>
#include <edit/undo/UndoRedoManager.h>
#include "WinApp.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>
#include <map>
#include <vector>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "DebugUIHelper.h"

// =======================================================================
// ImGuizmoManager: エディタUI（一覧・検索・操作モード・フィルタ）
// =======================================================================

namespace Hagine {
namespace {
/// <summary>種類ごとのアイコンと色（シーンのラベル・インスペクタの色分けとそろえる）</summary>
const char *CategoryIcon(GizmoCategory category)
{
    switch (category)
    {
    case GizmoCategory::Sprite:
        return ICON_FA_IMAGE;
    case GizmoCategory::Particle:
        return ICON_FA_STAR;
    case GizmoCategory::Light:
        return ICON_FA_LIGHTBULB;
    default:
        return ICON_FA_CUBE;
    }
}

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

std::string ToLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// <summary>
/// 連番を外した名前（cube_12 → cube / cube (3) → cube / Light2 → Light）。
/// 連番が無ければそのまま返す。外した結果が空になる名前（"12" など）もそのまま返す
/// </summary>
std::string StripNumberSuffix(const std::string &name)
{
    std::string base = name;
    // 「名前 (3)」の形
    if (!base.empty() && base.back() == ')')
    {
        const size_t open = base.rfind('(');
        if (open != std::string::npos && open + 2 < base.size() &&
            std::all_of(base.begin() + open + 1, base.end() - 1, [](unsigned char c) { return std::isdigit(c) != 0; }))
        {
            base.erase(open);
        }
    }
    // 末尾の数字
    size_t end = base.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(base[end - 1])))
    {
        --end;
    }
    base.erase(end);
    // 区切り（_ - . 空白）
    while (!base.empty() && (base.back() == '_' || base.back() == '-' || base.back() == '.' || base.back() == ' '))
    {
        base.pop_back();
    }
    return base.empty() ? name : base;
}

/// <summary>数字を数として比べる並び（cube_2 が cube_10 より前に来る）</summary>
bool NaturalLess(const std::string &a, const std::string &b)
{
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() && j < b.size())
    {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[j]);
        if (std::isdigit(ca) && std::isdigit(cb))
        {
            size_t ei = i;
            size_t ej = j;
            while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei])))
                ++ei;
            while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej])))
                ++ej;
            // 先頭の0を飛ばして桁数→中身の順で比べる（桁あふれさせない）
            size_t si = i;
            size_t sj = j;
            while (si + 1 < ei && a[si] == '0')
                ++si;
            while (sj + 1 < ej && b[sj] == '0')
                ++sj;
            if (ei - si != ej - sj)
                return (ei - si) < (ej - sj);
            const int compared = a.compare(si, ei - si, b, sj, ej - sj);
            if (compared != 0)
                return compared < 0;
            i = ei;
            j = ej;
            continue;
        }
        const int la = std::tolower(ca);
        const int lb = std::tolower(cb);
        if (la != lb)
            return la < lb;
        ++i;
        ++j;
    }
    if ((a.size() - i) != (b.size() - j))
        return (a.size() - i) < (b.size() - j);
    // 大文字小文字・0埋めだけが違う名前も別物として並べる（map のキーで同じ扱いにしない）
    return a < b;
}
} // namespace

// ---- imgui ------------------------------------------------------------

void ImGuizmoManager::DrawImGui()
{
    if (!pViewProjection_)
        return;

    // ---- オブジェクト選択（一番よく使うので一番上）----
    SectionHeader("[ オブジェクト選択 ]", DebugTheme::kAccentPurple);
    DrawObjectBrowser();

    // ---- 選択中の物の操作 ----
    if (!selectedNames_.empty())
    {
        auto it = transformMap_.find(*selectedNames_.begin());
        const bool isObject = it != transformMap_.end() && it->second.type == GizmoTarget::Type::BaseObject;
        const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
        ImGui::BeginDisabled(!isObject);
        if (NeutralButton(ICON_FA_CLONE " 複製", ImVec2(width, 0.0f)))
            DuplicateSelectedObjects();
        ImGui::SetItemTooltip("Ctrl+D: その場で複製して、複製したほうを選びます（オブジェクトのみ）");
        ImGui::SameLine();
        if (NeutralButton(ICON_FA_COPY " コピー", ImVec2(width, 0.0f)))
            CopySelectedObjects();
        ImGui::SameLine();
        ImGui::BeginDisabled(copiedNames_.empty());
        if (NeutralButton(ICON_FA_PASTE " 貼り付け", ImVec2(width, 0.0f)))
            PasteObjects();
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (DangerButton(ICON_FA_TRASH_ALT " 削除", ImVec2(width, 0.0f)))
            DeleteSelectedObjects();
        ImGui::SetItemTooltip("選択中の全オブジェクトを削除します");

        // 重なっていた候補（Tab で順に選べる）
        if (overlapCandidates_.size() > 1)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentYellow);
            ImGui::Text(ICON_FA_LAYER_GROUP " 重なり %zu件（Tab で順に選択）", overlapCandidates_.size());
            ImGui::PopStyleColor();
            for (int i = 0; i < static_cast<int>(overlapCandidates_.size()); ++i)
            {
                const bool isCurrent = (i == overlapCycleIndex_);
                ImGui::PushStyleColor(ImGuiCol_Text, isCurrent ? DebugTheme::kAccentGreen : DebugTheme::kTextDim);
                ImGui::Text("  [%d] %s", i, overlapCandidates_[i].first.c_str());
                ImGui::PopStyleColor();
            }
        }

        if (inspectorWindowOpen_)
        {
            // 詳細はインスペクタ窓に出ているので、ここに同じものを重ねない
            DimText("詳細はインスペクタ窓に表示しています");
        }
        else if (ThemedHeader(std::format("詳細 ({})###gizmoDetail", *selectedNames_.begin()).c_str(), DebugTheme::kAccentYellow, true))
        {
            ShowSelectedObjectImGui();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ---- 選択の表示 ----
    if (ThemedHeader("選択の表示##gizmoSelectionLook", DebugTheme::kAccentOrange))
    {
        AccentCheckbox("選んだ物の枠", &showSelectionOutline_, DebugTheme::kAccentOrange);
        ImGui::SameLine();
        ImGui::ColorEdit4("##selectionColor", &selectionColor_.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
        ImGui::SetItemTooltip("選んだ物の枠の色");
        ImGui::BeginDisabled(!showSelectionOutline_);
        ImGui::Indent();
        ImGui::Checkbox("奥の辺を破線で出す", &showSelectionHiddenEdges_);
        ImGui::SameLine();
        ImGui::Checkbox("中をうっすら塗る", &showSelectionFill_);
        ImGui::Unindent();
        ImGui::EndDisabled();
        AccentCheckbox("マウスを乗せた物の枠", &showHoverOutline_, DebugTheme::kAccentCyan);
        ImGui::SetItemTooltip("クリックしたら選ばれる物を、先に細い枠で知らせます（一覧の行に乗せたときも出ます）");
        ImGui::SameLine();
        ImGui::ColorEdit4("##hoverColor", &hoverColor_.x, ImGuiColorEditFlags_NoInputs);

        ImGui::Spacing();
        AccentCheckbox("補助表示（AABB・外接球・レイ）", &isDrawDebug_, DebugTheme::kAccentGreen);
        ImGui::SetItemTooltip("当たり判定の確認用に、対象の AABB / 外接球 / マウスのレイを線で表示します");
        if (isDrawDebug_)
        {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
            ImGui::Checkbox("選択中のみ", &debugSelectedOnly_);
            ImGui::SameLine();
            ImGui::Checkbox("AABB", &showDebugAABB_);
            ImGui::SameLine();
            ImGui::Checkbox("スフィア", &showDebugSphere_);
            ImGui::SameLine();
            ImGui::Checkbox("レイ", &showDebugHitPoints_);
            ImGui::PopStyleColor();
            ImGui::Unindent();
        }
        DimText("線の種類ごとの表示は 表示 → デバッグ線 でまとめて切り替えられます");
    }

    // ---- 操作説明 ----
    if (ThemedHeader("ショートカット##gizmoShortcuts", DebugTheme::kAccentBlue))
    {
        ImGui::BulletText("1 / 2 / 3 : 移動 / 回転 / スケール");
        ImGui::BulletText("4 : ローカル ⇔ ワールド 切替");
        ImGui::BulletText("5 : スナップ ON/OFF （Shift 押下中は一時反転）");
        ImGui::BulletText("F : 選択オブジェクトへ視点を寄せる");
        ImGui::BulletText("Tab : 重なったオブジェクトを順に選択");
        ImGui::BulletText("Ctrl+D : 複製 / Ctrl+C・Ctrl+V : コピー・貼り付け");
        ImGui::BulletText("空ドラッグ : 矩形選択（Ctrl 併用で選択に追加）");
        ImGui::BulletText("右クリック : その場所に置く・選択の操作・視点のメニュー");
        ImGui::BulletText("Shift+数字 / Ctrl+Shift+数字 : カメラのブックマークへ移動 / 保存");
        ImGui::BulletText("Alt+1〜4 / Alt+0 : クリック対象をその種類だけ / すべてに");
        ImGui::TextDisabled("※ シーンウィンドウにマウスがある時だけ効きます");
    }

    // ---- シーンのクリック対象 ----
    // 4種類（オブジェクト/スプライト/パーティクル/ライト）が同時にあると掴みたい物を選びづらいので、
    // チェックした種類だけをシーン上のクリック・矩形選択の対象にする。
    // シーンのツールバーと Alt+数字 からも切り替えられる。
    ImGui::Spacing();
    SectionHeader("[ シーンのクリック対象 ]", DebugTheme::kAccentGreen);
    ImGui::TextDisabled("チェックした種類だけシーンのクリックで選べます（一覧からはいつでも選べます）");

    // 分類の表示名。追加時はここと GizmoCategory を対応させる
    static const char *kCategoryLabels[kGizmoCategoryCount] = {
        "オブジェクト", "スプライト", "パーティクル", "ライト"};

    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
    for (int i = 0; i < kGizmoCategoryCount; ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        bool enabled = categoryEnabled_[i];
        if (ImGui::Checkbox(kCategoryLabels[i], &enabled))
        {
            SetCategoryEnabled(static_cast<GizmoCategory>(i), enabled);
        }
    }
    ImGui::PopStyleColor();

    // 「この種類だけ」を素早く選べるショートカット
    if (ImGui::SmallButton("全部##catAll"))
    {
        EnableAllCategories();
    }
    ImGui::SetItemTooltip("Alt+0");
    for (int i = 0; i < kGizmoCategoryCount; ++i)
    {
        ImGui::SameLine();
        ImGui::PushID(i);
        if (ImGui::SmallButton(std::format("{}のみ", kCategoryLabels[i]).c_str()))
        {
            SoloCategory(static_cast<GizmoCategory>(i));
        }
        ImGui::SetItemTooltip("Alt+%d", i + 1);
        ImGui::PopID();
    }

    ImGui::Spacing();
    SectionHeader("[ 操作モード ]", DebugTheme::kAccentBlue);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentBlue);
    if (ImGui::RadioButton("移動", currentOperation_ == ImGuizmo::TRANSLATE))
        currentOperation_ = ImGuizmo::TRANSLATE;
    ImGui::SameLine();
    if (ImGui::RadioButton("回転", currentOperation_ == ImGuizmo::ROTATE))
        currentOperation_ = ImGuizmo::ROTATE;
    ImGui::SameLine();
    if (ImGui::RadioButton("スケール", currentOperation_ == ImGuizmo::SCALE))
        currentOperation_ = ImGuizmo::SCALE;
    ImGui::PopStyleColor();

    ImGui::Spacing();
    SectionHeader("[ 座標系 ]", DebugTheme::kAccentCyan);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);
    if (ImGui::RadioButton("ローカル", currentMode_ == ImGuizmo::LOCAL))
        currentMode_ = ImGuizmo::LOCAL;
    ImGui::SameLine();
    if (ImGui::RadioButton("ワールド", currentMode_ == ImGuizmo::WORLD))
        currentMode_ = ImGuizmo::WORLD;
    ImGui::PopStyleColor();

    ImGui::Spacing();
    SectionHeader("[ スナップ（グリッド吸着）]", DebugTheme::kAccentYellow);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentYellow);
    ImGui::Checkbox("スナップを使う", &useSnap_);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("操作量を刻み幅に丸めます。Shift 押下中はこの設定が一時的に反転します");

    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("移動の刻み", &snapTranslate_, 0.05f, 0.01f, 100.0f, "%.2f");
    ImGui::SameLine();
    // 等間隔に並べるときによく使う刻みをワンタッチで
    if (ImGui::SmallButton("0.5##snapT"))
        snapTranslate_ = 0.5f;
    ImGui::SameLine();
    if (ImGui::SmallButton("1##snapT"))
        snapTranslate_ = 1.0f;
    ImGui::SameLine();
    if (ImGui::SmallButton("5##snapT"))
        snapTranslate_ = 5.0f;

    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("回転の刻み(度)", &snapRotateDegree_, 1.0f, 1.0f, 180.0f, "%.0f");
    ImGui::SameLine();
    if (ImGui::SmallButton("15##snapR"))
        snapRotateDegree_ = 15.0f;
    ImGui::SameLine();
    if (ImGui::SmallButton("45##snapR"))
        snapRotateDegree_ = 45.0f;
    ImGui::SameLine();
    if (ImGui::SmallButton("90##snapR"))
        snapRotateDegree_ = 90.0f;

    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("拡縮の刻み", &snapScale_, 0.01f, 0.01f, 10.0f, "%.2f");
    AccentCheckbox("移動中に刻みのグリッドを描く", &showSnapGrid_, DebugTheme::kAccentYellow);
    ImGui::SetItemTooltip("スナップONで移動ギズモを掴んでいる間、対象のまわりに刻み幅のマス目を描きます");

    ImGui::Spacing();
    SectionHeader("[ 整列・配置 ]", DebugTheme::kAccentPurple);
    ImGui::TextDisabled("選択中のオブジェクトをまとめて並べます");

    static const char *kAxisLabels[3] = {"X", "Y", "Z"};
    // 軸ごとに「最小 / 中央 / 最大 に揃える」を並べる
    for (int axis = 0; axis < 3; ++axis)
    {
        ImGui::PushID(axis);
        ImGui::TextUnformatted(kAxisLabels[axis]);
        ImGui::SameLine();
        if (ImGui::SmallButton("最小##align"))
            AlignSelected(axis, AlignMode::Min);
        ImGui::SameLine();
        if (ImGui::SmallButton("中央##align"))
            AlignSelected(axis, AlignMode::Center);
        ImGui::SameLine();
        if (ImGui::SmallButton("最大##align"))
            AlignSelected(axis, AlignMode::Max);
        ImGui::SameLine();
        if (ImGui::SmallButton("等間隔##dist"))
            DistributeSelected(axis);
        ImGui::PopID();
    }
    ImGui::SetItemTooltip("等間隔は両端をそのままに、間のオブジェクトを均等な位置へ動かします（3つ以上必要）");

    if (ImGui::Button("地面に接地##snapGround", ImVec2(-1, 0)))
    {
        SnapSelectedToGround();
    }
    ImGui::SetItemTooltip("選択中のオブジェクトを、真下にある他のオブジェクトの上面へ落とします\n"
                          "（下に何も無ければ Y=0 へ）");
}

// ---- DrawObjectBrowser ------------------------------------------------

// シーンの物を探して選ぶ一覧。cube_1, cube_2 … のような連番は「cube」に1行でまとめ、▼で開くと中身が出る。
// 以前はコンボボックス1つで、物が増えると長いリストを上下に探すしかなかった。
void ImGuizmoManager::DrawObjectBrowser()
{
    // ---- 絞り込み ----
    const float comboWidth = 120.0f;
    ImGui::SetNextItemWidth((std::max)(ImGui::GetContentRegionAvail().x - comboWidth - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x * 2.0f, 80.0f));
    if (ImGui::InputTextWithHint("##ObjectSearch", ICON_FA_SEARCH " 名前で絞り込み...", searchBuffer_, sizeof(searchBuffer_)))
    {
        UpdateFilteredNames();
    }
    ImGui::SameLine();
    {
        static const char *kFilterLabels[] = {"すべての種類", "オブジェクト", "スプライト", "パーティクル", "ライト"};
        int filterIndex = browserCategoryFilter_ + 1;
        ImGui::SetNextItemWidth(comboWidth);
        if (ImGui::Combo("##browserCategory", &filterIndex, kFilterLabels, IM_ARRAYSIZE(kFilterLabels)))
            browserCategoryFilter_ = filterIndex - 1;
    }
    ImGui::SameLine();
    {
        ScopedButtonColors colors(browserGroupNumbered_ ? DebugTheme::kButtonPrimary : DebugTheme::kButtonGhost,
                                  browserGroupNumbered_ ? DebugTheme::kButtonPrimaryHover : DebugTheme::kButtonGhostHover);
        if (ImGui::Button(ICON_FA_LAYER_GROUP "##browserGroup", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
            browserGroupNumbered_ = !browserGroupNumbered_;
    }
    ImGui::SetItemTooltip(browserGroupNumbered_ ? "連番をまとめて表示中（cube_1, cube_2 → cube ▼）。押すと1つずつ並べます"
                                                : "1つずつ並べています。押すと連番をまとめます（cube_1, cube_2 → cube ▼）");

    // ---- 並べる物を集める ----
    const std::string query = ToLower(searchBuffer_);
    struct Row
    {
        const std::string *name;
        GizmoCategory category;
    };
    std::map<std::string, std::vector<Row>, bool (*)(const std::string &, const std::string &)> groups(NaturalLess);
    int shownCount = 0;
    for (const auto &[name, target] : transformMap_)
    {
        if (browserCategoryFilter_ >= 0 && static_cast<int>(target.category) != browserCategoryFilter_)
            continue;
        if (!query.empty() && ToLower(name).find(query) == std::string::npos)
            continue;
        const std::string key = browserGroupNumbered_ ? StripNumberSuffix(name) : name;
        groups[key].push_back({&name, target.category});
        ++shownCount;
    }
    for (auto &[key, rows] : groups)
    {
        std::sort(rows.begin(), rows.end(), [](const Row &l, const Row &r) { return NaturalLess(*l.name, *r.name); });
    }

    // 選択が外から（シーンのクリックなどで）変わったら、その行が見えるよう開いて送る
    const std::string currentSelection = selectedNames_.size() == 1 ? *selectedNames_.begin() : std::string();
    if (currentSelection != browserLastSelection_)
    {
        browserLastSelection_ = currentSelection;
        browserScrollToSelection_ = !currentSelection.empty();
    }

    // 範囲選択（Shift+クリック）用に、一覧の並び（閉じたまとまりの中身も含む）を控える
    std::vector<const std::string *> visibleOrder;
    visibleOrder.reserve(shownCount);
    for (const auto &[key, rows] : groups)
        for (const Row &row : rows)
            visibleOrder.push_back(row.name);

    auto selectRow = [&](const std::string &name) {
        const ImGuiIO &io = ImGui::GetIO();
        if (io.KeyShift && !browserRangeAnchor_.empty())
        {
            // 起点から今の行までを選ぶ（見えている並びで）
            auto anchorIt = std::find_if(visibleOrder.begin(), visibleOrder.end(), [&](const std::string *n) { return *n == browserRangeAnchor_; });
            auto currentIt = std::find_if(visibleOrder.begin(), visibleOrder.end(), [&](const std::string *n) { return *n == name; });
            if (anchorIt != visibleOrder.end() && currentIt != visibleOrder.end())
            {
                if (!io.KeyCtrl)
                    selectedNames_.clear();
                if (anchorIt > currentIt)
                    std::swap(anchorIt, currentIt);
                for (auto it = anchorIt; it <= currentIt; ++it)
                    selectedNames_.insert(**it);
                return;
            }
        }
        if (io.KeyCtrl)
            ToggleSelect(name);
        else
            SelectOnly(name);
        browserRangeAnchor_ = name;
        browserScrollToSelection_ = false; // 一覧で選んだ物は見えているので送らない
        browserLastSelection_ = selectedNames_.size() == 1 ? *selectedNames_.begin() : std::string();
    };

    auto drawItem = [&](const Row &row, const char *label) {
        const std::string &name = *row.name;
        const bool selected = IsSelected(name);
        ImGui::PushID(name.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, CategoryAccent(row.category));
        ImGui::TextUnformatted(CategoryIcon(row.category));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_AllowDoubleClick))
        {
            selectRow(name);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                FocusOnSelection();
        }
        if (ImGui::IsItemHovered())
        {
            // シーンでも同じ物を枠で知らせる（次のフレームのシーン描画で使う）
            browserHoveredName_ = name;
            ImGui::SetTooltip("%s\nダブルクリック: カメラを寄せる / Ctrl: 追加・解除 / Shift: 範囲", name.c_str());
        }
        if (ImGui::BeginPopupContextItem("##browserItemContext"))
        {
            if (!selected)
                SelectOnly(name);
            if (ImGui::MenuItem(ICON_FA_CROSSHAIRS " カメラを寄せる", "F"))
                FocusOnSelection();
            if (ImGui::MenuItem(ICON_FA_CLONE " 複製", "Ctrl+D"))
                DuplicateSelectedObjects();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_TRASH_ALT " 削除"))
                DeleteSelectedObjects();
            ImGui::EndPopup();
        }
        if (selected && browserScrollToSelection_ && name == currentSelection)
        {
            ImGui::SetScrollHereY(0.4f);
            browserScrollToSelection_ = false;
        }
        ImGui::PopID();
    };

    // ---- 一覧 ----
    browserHoveredName_.clear();
    ImGui::BeginChild("##objectBrowser", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
    const bool filtering = !query.empty();
    for (const auto &[key, rows] : groups)
    {
        if (rows.size() == 1)
        {
            drawItem(rows.front(), rows.front().name->c_str());
            continue;
        }

        // まとめた行。見出しを押すと中身を全部選ぶ（▼で開閉）
        int selectedInGroup = 0;
        bool containsTarget = false;
        for (const Row &row : rows)
        {
            selectedInGroup += IsSelected(*row.name) ? 1 : 0;
            containsTarget |= (*row.name == currentSelection);
        }
        if (filtering || (browserScrollToSelection_ && containsTarget))
            ImGui::SetNextItemOpen(true);

        ImGui::PushID(key.c_str());
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (selectedInGroup == static_cast<int>(rows.size()))
            flags |= ImGuiTreeNodeFlags_Selected;
        const std::string header = selectedInGroup > 0 && selectedInGroup < static_cast<int>(rows.size())
                                       ? std::format("{}  ({})  {}つ選択中###group", key, rows.size(), selectedInGroup)
                                       : std::format("{}  ({})###group", key, rows.size());
        ImGui::PushStyleColor(ImGuiCol_Text, CategoryAccent(rows.front().category));
        const bool open = ImGui::TreeNodeEx(header.c_str(), flags);
        ImGui::PopStyleColor();
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            // 見出しを押したら中身を全部選ぶ（Ctrl なら今の選択に足す）
            if (!ImGui::GetIO().KeyCtrl)
                selectedNames_.clear();
            for (const Row &row : rows)
                selectedNames_.insert(*row.name);
            browserScrollToSelection_ = false;
        }
        ImGui::SetItemTooltip("押す: %s を全部選ぶ（Ctrl で追加）\n▼ / ダブルクリック: 開く", key.c_str());
        if (open)
        {
            for (const Row &row : rows)
            {
                drawItem(row, row.name->c_str());
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (groups.empty())
    {
        DimText(transformMap_.empty() ? "シーンに物がありません" : "一致する物がありません");
    }
    ImGui::EndChild();

    // ---- 件数と全体の操作 ----
    ImGui::TextDisabled("%d 件 / 選択 %zu", shownCount, selectedNames_.size());
    ImGui::SameLine();
    const float buttonsWidth = ImGui::CalcTextSize("全部選ぶ選択解除").x + ImGui::GetStyle().FramePadding.x * 4.0f + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - buttonsWidth));
    if (ImGui::SmallButton("全部選ぶ"))
    {
        // 絞り込み中なら、見えている物だけ
        selectedNames_.clear();
        for (const auto &[key, rows] : groups)
            for (const Row &row : rows)
                selectedNames_.insert(*row.name);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("選択解除"))
        selectedNames_.clear();
}

// ---- ShowSelectedObjectImGui ------------------------------------------

// 選択中エントリの ShowImGui を呼び出す
void ImGuizmoManager::ShowSelectedObjectImGui()
{
    if (selectedNames_.empty())
        return;

    std::string firstName = *selectedNames_.begin();
    auto it = transformMap_.find(firstName);
    if (it != transformMap_.end())
    {
        it->second.ShowImGui();
    }

    if (selectedNames_.size() > 1)
    {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 1.0f, 1.0f));
        ImGui::Text("※ %zu個のオブジェクトが選択されています", selectedNames_.size());
        ImGui::Text("表示しているのは '%s' の設定です", firstName.c_str());
        ImGui::PopStyleColor();
    }
}

// ---- UpdateFilteredNames ----------------------------------------------

// 検索バッファに基づいてフィルタ済みの名前リストを更新する
void ImGuizmoManager::UpdateFilteredNames()
{
    filteredNames_.clear();

    std::vector<std::string> allNames;
    // 一覧からはクリック対象フィルタに関係なく選べるよう、全種類を出す
    for (const auto &pair : transformMap_)
    {
        allNames.push_back(pair.first);
    }
    std::sort(allNames.begin(), allNames.end());

    std::string searchStr = searchBuffer_;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

    for (const std::string &name : allNames)
    {
        if (strlen(searchBuffer_) == 0)
        {
            filteredNames_.push_back(name);
        }
        else
        {
            std::string lowerName = name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            if (lowerName.find(searchStr) != std::string::npos)
            {
                filteredNames_.push_back(name);
            }
        }
    }
}

} // namespace Hagine
#endif // USE_IMGUI
