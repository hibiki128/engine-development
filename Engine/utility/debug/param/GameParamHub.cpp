#include "GameParamHub.h"
#include <utility/data/DataHandler.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#include <algorithm>
#include <type_traits>
#ifdef USE_IMGUI
#include <imgui.h>
#endif // USE_IMGUI

namespace Hagine {

namespace {
/// ドラッグ＆ドロップで使うペイロード識別子
constexpr const char *kDndPayloadType = "GAMEPARAM_KEY";
/// レイアウト保存先
constexpr const char *kLayoutFolder = "Debug";
constexpr const char *kLayoutFile = "GameParamHubLayout";
/// パラメータ値の保存先（全ラベル共通の1ファイルに owner/name キーで保存）
constexpr const char *kValueFolder = "Debug";
constexpr const char *kValueFile = "GameParamValues";
} // namespace

GameParamHub::~GameParamHub() = default;

void GameParamHub::EnsureValueStore()
{
    if (!valueStore_)
    {
        valueStore_ = std::make_unique<DataHandler>(kValueFolder, kValueFile);
    }
}

void GameParamHub::SaveEntryValue(const Entry &e)
{
    const std::string key = MakeKey(e.owner, e.name);
    std::visit([&](auto *p) {
        using Ptr = decltype(p);
        if constexpr (std::is_same_v<Ptr, float *>)
            valueStore_->Save<float>(key, *p);
        else if constexpr (std::is_same_v<Ptr, int *>)
            valueStore_->Save<int>(key, *p);
        else if constexpr (std::is_same_v<Ptr, bool *>)
            valueStore_->Save<bool>(key, *p);
        else if constexpr (std::is_same_v<Ptr, Vector2 *>)
            valueStore_->Save<Vector2>(key, *p);
        else if constexpr (std::is_same_v<Ptr, Vector3 *>)
            valueStore_->Save<Vector3>(key, *p);
        else if constexpr (std::is_same_v<Ptr, Vector4 *>)
            valueStore_->Save<Vector4>(key, *p);
        // const 参照（読み取り専用の表示値）は保存しない
    },
               e.ptr);
}

void GameParamHub::SaveOwnerValues(const std::string &owner)
{
    EnsureValueStore();
    for (auto &e : entries_)
    {
        if (e.owner == owner)
        {
            SaveEntryValue(e);
        }
    }
    valueStore_->Flush();
}

int GameParamHub::SaveAssignedValues(const std::string &window, const std::string &tab, const std::string &section)
{
    EnsureValueStore();
    int count = 0;
    for (auto &e : entries_)
    {
        const std::string key = MakeKey(e.owner, e.name);
        auto it = assignments_.find(key);
        if (it == assignments_.end())
        {
            continue; // 未分類はスキップ
        }
        const Assignment &a = it->second;
        // window は必須一致。tab/section は指定されていれば一致を要求する
        // （section="" ならタブ配下すべて、tab="" ならウィンドウ配下すべてが対象）
        if (a.window != window)
        {
            continue;
        }
        if (!tab.empty() && a.tab != tab)
        {
            continue;
        }
        if (!section.empty() && a.section != section)
        {
            continue;
        }
        SaveEntryValue(e);
        ++count;
    }
    valueStore_->Flush();
    return count;
}

void GameParamHub::LoadOwnerValues(const std::string &owner)
{
    for (auto &e : entries_)
    {
        if (e.owner == owner)
        {
            ApplySavedValue(e);
        }
    }
}

void GameParamHub::ApplySavedValue(Entry &e)
{
    EnsureValueStore();
    const std::string key = MakeKey(e.owner, e.name);
    std::visit([&](auto *p) {
        using Ptr = decltype(p);
        if constexpr (std::is_same_v<Ptr, float *>)
            *p = valueStore_->Load<float>(key, *p);
        else if constexpr (std::is_same_v<Ptr, int *>)
            *p = valueStore_->Load<int>(key, *p);
        else if constexpr (std::is_same_v<Ptr, bool *>)
            *p = valueStore_->Load<bool>(key, *p);
        else if constexpr (std::is_same_v<Ptr, Vector2 *>)
            *p = valueStore_->Load<Vector2>(key, *p);
        else if constexpr (std::is_same_v<Ptr, Vector3 *>)
            *p = valueStore_->Load<Vector3>(key, *p);
        else if constexpr (std::is_same_v<Ptr, Vector4 *>)
            *p = valueStore_->Load<Vector4>(key, *p);
        // const 参照は書き戻せないので何もしない
    },
               e.ptr);
}

void GameParamHub::CaptureOriginal(Entry &e)
{
    // 登録時点の現在値（＝コード既定値。ApplySavedValueより前に呼ぶこと）をスナップショットする
    std::visit([&](auto *p) {
        using Ptr = decltype(p);
        if constexpr (std::is_same_v<Ptr, float *>)
            e.original = *p;
        else if constexpr (std::is_same_v<Ptr, int *>)
            e.original = *p;
        else if constexpr (std::is_same_v<Ptr, bool *>)
            e.original = *p;
        else if constexpr (std::is_same_v<Ptr, Vector2 *>)
            e.original = *p;
        else if constexpr (std::is_same_v<Ptr, Vector3 *>)
            e.original = *p;
        else if constexpr (std::is_same_v<Ptr, Vector4 *>)
            e.original = *p;
        // const 参照はリセット対象外
    },
               e.ptr);
    e.hasOriginal = true;
}

void GameParamHub::ResetEntryToOriginal(Entry &e)
{
    if (!e.hasOriginal)
    {
        return;
    }
    // 捕捉しておいたコード既定値を書き戻す
    std::visit([&](auto *p) {
        using Ptr = decltype(p);
        using V = std::remove_cv_t<std::remove_pointer_t<Ptr>>;
        if constexpr (std::is_same_v<Ptr, float *> || std::is_same_v<Ptr, int *> ||
                      std::is_same_v<Ptr, bool *> || std::is_same_v<Ptr, Vector2 *> ||
                      std::is_same_v<Ptr, Vector3 *> || std::is_same_v<Ptr, Vector4 *>)
        {
            if (const V *v = std::get_if<V>(&e.original))
            {
                *p = *v;
            }
        }
        // const 参照は書き戻せない
    },
               e.ptr);

    // 保存済みの値も消しておく（次回起動時もコード既定値に戻す）
    EnsureValueStore();
    valueStore_->Remove(MakeKey(e.owner, e.name));
    valueStore_->Flush();
}

void GameParamHub::ResetOwnerToOriginal(const std::string &owner)
{
    for (auto &e : entries_)
    {
        if (e.owner == owner)
        {
            ResetEntryToOriginal(e);
        }
    }
}

int GameParamHub::ResetAssignedToOriginal(const std::string &window, const std::string &tab, const std::string &section)
{
    int count = 0;
    for (auto &e : entries_)
    {
        const std::string key = MakeKey(e.owner, e.name);
        auto it = assignments_.find(key);
        if (it == assignments_.end())
        {
            continue;
        }
        const Assignment &a = it->second;
        if (a.window != window)
        {
            continue;
        }
        if (!tab.empty() && a.tab != tab)
        {
            continue;
        }
        if (!section.empty() && a.section != section)
        {
            continue;
        }
        ResetEntryToOriginal(e);
        ++count;
    }
    return count;
}

GameParamHub *GameParamHub::GetInstance()
{
    static GameParamHub instance;
    return &instance;
}

std::string GameParamHub::MakeKey(const std::string &owner, const std::string &name)
{
    return owner + "/" + name;
}

void GameParamHub::Register(const std::string &owner, const std::string &name, ParamPtr ptr, const Options &opts)
{
    const std::string key = MakeKey(owner, name);
    // 同一キーは上書き（シーン再初期化での再登録を想定）
    for (auto &e : entries_)
    {
        if (MakeKey(e.owner, e.name) == key)
        {
            e.ptr = ptr;
            e.opts = opts;
            if (!e.hasOriginal)
            {
                CaptureOriginal(e); // 元の値（コード既定値）を初回だけ捕捉
            }
            ApplySavedValue(e); // 保存済みの値があれば復元する
            return;
        }
    }
    entries_.push_back({owner, name, ptr, opts});
    Entry &added = entries_.back();
    CaptureOriginal(added);   // 元の値（コード既定値）を捕捉（ApplySavedValueより前）
    ApplySavedValue(added);   // 保存済みの値があれば起動時に復元する（未保存なら現在値のまま）
}

void GameParamHub::Unregister(const std::string &owner)
{
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [&](const Entry &e) { return e.owner == owner; }),
        entries_.end());
}

void GameParamHub::SaveLayout()
{
    DataHandler data(kLayoutFolder, kLayoutFile);

    json windowsJson = json::array();
    for (const auto &w : windows_)
    {
        json tabsJson = json::array();
        for (const auto &t : w.tabs)
        {
            json sectionsJson = json::array();
            for (const auto &s : t.sections)
            {
                sectionsJson.push_back(s.name);
            }
            tabsJson.push_back({{"name", t.name}, {"sections", sectionsJson}});
        }
        windowsJson.push_back({{"name", w.name}, {"tabs", tabsJson}});
    }

    json assignJson = json::object();
    for (const auto &[key, a] : assignments_)
    {
        assignJson[key] = {{"window", a.window}, {"tab", a.tab}, {"section", a.section}};
    }

    data.Save("windows", windowsJson);
    data.Save("assignments", assignJson);
    data.Flush();
    layoutDirty_ = false;
#ifdef USE_IMGUI
    ImGuiNotification::Post("パラメータ仕分けを保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
#endif
}

void GameParamHub::LoadLayout()
{
    DataHandler data(kLayoutFolder, kLayoutFile);
    windows_.clear();
    assignments_.clear();

    json windowsJson = data.Load<json>("windows", json::array());
    for (const auto &wj : windowsJson)
    {
        WindowDef w;
        w.name = wj.value("name", "");
        if (w.name.empty())
        {
            continue;
        }
        for (const auto &tj : wj.value("tabs", json::array()))
        {
            TabDef t;
            t.name = tj.value("name", "");
            if (t.name.empty())
            {
                continue;
            }
            for (const auto &sj : tj.value("sections", json::array()))
            {
                t.sections.push_back({sj.get<std::string>()});
            }
            w.tabs.push_back(std::move(t));
        }
        windows_.push_back(std::move(w));
    }

    json assignJson = data.Load<json>("assignments", json::object());
    for (auto it = assignJson.begin(); it != assignJson.end(); ++it)
    {
        Assignment a;
        a.window = it.value().value("window", "");
        a.tab = it.value().value("tab", "");
        a.section = it.value().value("section", "");
        assignments_[it.key()] = a;
    }
    layoutDirty_ = false;
}

GameParamHub::WindowDef *GameParamHub::FindWindowDef(const std::string &name)
{
    for (auto &w : windows_)
    {
        if (w.name == name)
        {
            return &w;
        }
    }
    return nullptr;
}

std::vector<std::string> GameParamHub::CollectAssigned(const std::string &window, const std::string &tab, const std::string &section)
{
    std::vector<std::string> result;
    for (const auto &e : entries_)
    {
        const std::string key = MakeKey(e.owner, e.name);
        auto it = assignments_.find(key);
        if (it == assignments_.end())
        {
            continue;
        }
        if (it->second.window == window && it->second.tab == tab && it->second.section == section)
        {
            result.push_back(key);
        }
    }
    return result;
}

void GameParamHub::DrawImGui(bool *open)
{
#ifdef USE_IMGUI
    // 初回のみレイアウトを復元する
    if (!layoutLoaded_)
    {
        LoadLayout();
        layoutLoaded_ = true;
    }

    DrawHubWindow(open);
    DrawUserWindows();
    DrawConfirmPopup();
#else
    (void)open;
#endif
}

#ifdef USE_IMGUI

void GameParamHub::RequestConfirm(const std::string &message, std::function<void()> action)
{
    confirmMessage_ = message;
    confirmAction_ = std::move(action);
    wantOpenConfirm_ = true;
}

void GameParamHub::DrawConfirmPopup()
{
    // ボタン側で開いた ID と、ここで描くモーダルの ID を一致させるためトップレベルで開閉する
    if (wantOpenConfirm_)
    {
        ImGui::OpenPopup("確認##gameparamhub");
        wantOpenConfirm_ = false;
    }
    // 画面中央に出す
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("確認##gameparamhub", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(confirmMessage_.c_str());
        ImGui::Separator();
        if (ImGui::Button("実行", ImVec2(120.0f, 0.0f)))
        {
            if (confirmAction_)
            {
                confirmAction_();
            }
            confirmAction_ = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::SetItemDefaultFocus();
        if (ImGui::Button("キャンセル", ImVec2(120.0f, 0.0f)))
        {
            confirmAction_ = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void GameParamHub::ApplyMultiSelectRequests(void *multiSelectIo, const std::vector<std::string> &keys)
{
    ImGuiMultiSelectIO *io = static_cast<ImGuiMultiSelectIO *>(multiSelectIo);
    for (const ImGuiSelectionRequest &req : io->Requests)
    {
        if (req.Type == ImGuiSelectionRequestType_SetAll)
        {
            if (req.Selected)
            {
                // このブロック内の全項目を選択
                for (const auto &k : keys)
                {
                    selectedKeys_.insert(k);
                }
            }
            else
            {
                // 選択全体をクリア（通常のクリックや Esc・空白クリックで飛んでくる）
                selectedKeys_.clear();
            }
        }
        else if (req.Type == ImGuiSelectionRequestType_SetRange)
        {
            int first = static_cast<int>(req.RangeFirstItem);
            int last = static_cast<int>(req.RangeLastItem);
            if (first > last)
            {
                std::swap(first, last);
            }
            for (int i = first; i <= last; ++i)
            {
                if (i >= 0 && i < static_cast<int>(keys.size()))
                {
                    if (req.Selected)
                        selectedKeys_.insert(keys[i]);
                    else
                        selectedKeys_.erase(keys[i]);
                }
            }
        }
    }
}

void GameParamHub::DrawParamWidget(Entry &entry)
{
    const std::string label = entry.name + "##" + entry.owner;
    bool changed = false;

    std::visit([&](auto *p) {
        using T = std::remove_pointer_t<std::remove_cv_t<std::remove_reference_t<decltype(p)>>>;
        using Ptr = std::remove_reference_t<decltype(p)>;
        const Options &o = entry.opts;

        if constexpr (std::is_same_v<Ptr, float *>)
        {
            changed = ImGui::DragFloat(label.c_str(), p, o.speed, o.min, o.max);
        }
        else if constexpr (std::is_same_v<Ptr, int *>)
        {
            changed = ImGui::DragInt(label.c_str(), p, o.speed, static_cast<int>(o.min), static_cast<int>(o.max));
        }
        else if constexpr (std::is_same_v<Ptr, bool *>)
        {
            changed = ImGui::Checkbox(label.c_str(), p);
        }
        else if constexpr (std::is_same_v<Ptr, Vector2 *>)
        {
            changed = ImGui::DragFloat2(label.c_str(), &p->x, o.speed, o.min, o.max);
        }
        else if constexpr (std::is_same_v<Ptr, Vector3 *>)
        {
            changed = ImGui::DragFloat3(label.c_str(), &p->x, o.speed, o.min, o.max);
        }
        else if constexpr (std::is_same_v<Ptr, Vector4 *>)
        {
            if (o.isColor)
            {
                changed = ImGui::ColorEdit4(label.c_str(), &p->x);
            }
            else
            {
                changed = ImGui::DragFloat4(label.c_str(), &p->x, o.speed, o.min, o.max);
            }
        }
        else if constexpr (std::is_same_v<Ptr, const float *>)
        {
            ImGui::Text("%s: %.3f", entry.name.c_str(), *p);
        }
        else if constexpr (std::is_same_v<Ptr, const int *>)
        {
            ImGui::Text("%s: %d", entry.name.c_str(), *p);
        }
        else if constexpr (std::is_same_v<Ptr, const bool *>)
        {
            ImGui::Text("%s: %s", entry.name.c_str(), *p ? "True" : "False");
        }
        else
        {
            (void)sizeof(T); // 未対応型（コンパイル時に列挙済みのため到達しない）
        }
    },
               entry.ptr);

    if (changed && entry.opts.onChange)
    {
        entry.opts.onChange();
    }
}

void GameParamHub::MoveKeys(const std::string &droppedKey, const Assignment &target)
{
    // 掴んだ項目が複数選択に含まれていれば選択全体を、そうでなければその1件だけを移動する
    std::vector<std::string> targets;
    const bool moveSelection = selectedKeys_.count(droppedKey) > 0;
    if (moveSelection)
    {
        targets.assign(selectedKeys_.begin(), selectedKeys_.end());
    }
    else
    {
        targets.push_back(droppedKey);
    }

    for (const auto &k : targets)
    {
        if (target.window.empty())
        {
            assignments_.erase(k); // 未分類へ戻す
        }
        else
        {
            assignments_[k] = target;
        }
    }

    if (moveSelection)
    {
        selectedKeys_.clear(); // 移動し終えたら選択を解除
    }
    layoutDirty_ = true;
}

void GameParamHub::HandleDropTarget(const std::string &window, const std::string &tab, const std::string &section)
{
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kDndPayloadType))
        {
            const std::string key = static_cast<const char *>(payload->Data);
            MoveKeys(key, {window, tab, section});
        }
        ImGui::EndDragDropTarget();
    }
}

void GameParamHub::DrawDropZone(const char *label, const std::string &window, const std::string &tab, const std::string &section)
{
    // フル幅の控えめなボタンをドロップ受け皿にする（ここへ落とすと移動）
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.30f, 0.38f, 0.55f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.46f, 0.60f, 0.85f));
    ImGui::Button(label, ImVec2(-1.0f, 0.0f));
    ImGui::PopStyleColor(2);
    HandleDropTarget(window, tab, section);
}

void GameParamHub::DrawMoveContextMenu(const std::string &key)
{
    if (ImGui::BeginPopupContextItem(("move_" + key).c_str()))
    {
        // この項目が複数選択に含まれていれば選択全体をまとめて移動する
        const int moveCount = selectedKeys_.count(key) ? static_cast<int>(selectedKeys_.size()) : 1;
        if (moveCount > 1)
        {
            ImGui::TextDisabled("選択中の %d 個をまとめて移動", moveCount);
            ImGui::Separator();
        }
        if (ImGui::MenuItem("未分類へ戻す"))
        {
            MoveKeys(key, {});
        }
        // この項目（複数選択なら選択全体）を元の値へ戻す
        if (ImGui::MenuItem("元の値に戻す"))
        {
            const bool many = selectedKeys_.count(key) && selectedKeys_.size() > 1;
            std::string msg = many
                                  ? "選択中の " + std::to_string(selectedKeys_.size()) + " 個を元の値（コード既定値）に戻します。よろしいですか？"
                                  : "この項目を元の値（コード既定値）に戻します。よろしいですか？";
            RequestConfirm(msg, [this, key] {
                std::vector<std::string> targets;
                if (selectedKeys_.count(key))
                    targets.assign(selectedKeys_.begin(), selectedKeys_.end());
                else
                    targets.push_back(key);
                for (const auto &tk : targets)
                {
                    for (auto &e : entries_)
                    {
                        if (MakeKey(e.owner, e.name) == tk)
                        {
                            ResetEntryToOriginal(e);
                            break;
                        }
                    }
                }
            });
        }
        ImGui::Separator();
        for (const auto &w : windows_)
        {
            if (ImGui::BeginMenu(w.name.c_str()))
            {
                if (ImGui::MenuItem("(ウィンドウ直下)"))
                {
                    MoveKeys(key, {w.name, "", ""});
                }
                for (const auto &t : w.tabs)
                {
                    if (ImGui::BeginMenu(t.name.c_str()))
                    {
                        if (ImGui::MenuItem("(タブ直下)"))
                        {
                            MoveKeys(key, {w.name, t.name, ""});
                        }
                        for (const auto &s : t.sections)
                        {
                            if (ImGui::MenuItem(s.name.c_str()))
                            {
                                MoveKeys(key, {w.name, t.name, s.name});
                            }
                        }
                        ImGui::EndMenu();
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }
}

void GameParamHub::DrawParamRows(const std::vector<std::string> &keys)
{
    if (keys.empty())
    {
        return;
    }

    // ImGuiのマルチセレクト: クリック=単一選択 / Ctrl+クリック=追加解除 / Shift+クリック=範囲 / Esc=解除。
    // 選択ハンドル(≡)がそのままドラッグ元にもなり、ドラッグ⇄クリックの区別はImGui側が調停する。
    ImGuiMultiSelectFlags msFlags = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_ScopeRect;
    ImGuiMultiSelectIO *io = ImGui::BeginMultiSelect(msFlags, -1, static_cast<int>(keys.size()));
    ApplyMultiSelectRequests(io, keys);

    for (int idx = 0; idx < static_cast<int>(keys.size()); ++idx)
    {
        const std::string &key = keys[idx];
        Entry *entry = nullptr;
        for (auto &e : entries_)
        {
            if (MakeKey(e.owner, e.name) == key)
            {
                entry = &e;
                break;
            }
        }
        if (!entry)
        {
            continue;
        }

        ImGui::PushID(key.c_str());

        // 選択ハンドル（クリックで選択・ドラッグで移動）。Selectableがマルチセレクト対象。
        const bool selected = selectedKeys_.count(key) > 0;
        ImGui::SetNextItemSelectionUserData(idx);
        ImGui::Selectable(("≡##sel_" + key).c_str(), selected, ImGuiSelectableFlags_AllowOverlap, ImVec2(22.0f, 0.0f));

        // ドラッグでウィンドウ/タブ/セクションへ移動（選択に含まれていれば選択全体を移動）
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            ImGui::SetDragDropPayload(kDndPayloadType, key.c_str(), key.size() + 1);
            if (selectedKeys_.count(key) && selectedKeys_.size() > 1)
                ImGui::Text("%d個の項目をまとめて移動", static_cast<int>(selectedKeys_.size()));
            else
                ImGui::Text("%s", key.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("クリックで選択 / Ctrl+クリックで複数 / Shift+クリックで範囲\nドラッグでウィンドウへ移動 / 右クリックで移動メニュー");
        }
        DrawMoveContextMenu(key);

        ImGui::SameLine();
        DrawParamWidget(*entry);

        ImGui::PopID();
    }

    io = ImGui::EndMultiSelect();
    ApplyMultiSelectRequests(io, keys);
}

void GameParamHub::DrawLayoutTree()
{
    // ── 新規ウィンドウ作成 ──
    static char newWindowName[64] = {};
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##newWindow", "新しいウィンドウ名", newWindowName, sizeof(newWindowName));
    ImGui::SameLine();
    if (ImGui::Button("ウィンドウ作成") && newWindowName[0] != '\0')
    {
        if (!FindWindowDef(newWindowName))
        {
            windows_.push_back({newWindowName, {}});
            layoutDirty_ = true;
        }
        newWindowName[0] = '\0';
    }

    ImGui::Separator();

    // ── ウィンドウ → タブ → セクションのツリー（各ノードがドロップ先） ──
    int windowIndexToDelete = -1;
    for (int wi = 0; wi < static_cast<int>(windows_.size()); ++wi)
    {
        WindowDef &w = windows_[wi];
        ImGui::PushID(wi);

        bool windowOpen = ImGui::TreeNodeEx(w.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        HandleDropTarget(w.name, "", "");

        // ウィンドウ操作メニュー
        if (ImGui::BeginPopupContextItem("windowMenu"))
        {
            if (ImGui::MenuItem("ウィンドウを削除（中身は未分類へ）"))
            {
                windowIndexToDelete = wi;
            }
            ImGui::EndPopup();
        }

        if (windowOpen)
        {
            // タブ追加
            static char newTabName[64] = {};
            ImGui::SetNextItemWidth(140.0f);
            ImGui::InputTextWithHint("##newTab", "新しいタブ名", newTabName, sizeof(newTabName));
            ImGui::SameLine();
            if (ImGui::Button("タブ追加") && newTabName[0] != '\0')
            {
                bool exists = false;
                for (const auto &t : w.tabs)
                {
                    exists |= (t.name == newTabName);
                }
                if (!exists)
                {
                    w.tabs.push_back({newTabName, {}});
                    layoutDirty_ = true;
                }
                newTabName[0] = '\0';
            }

            // ウィンドウ直下のパラメータ
            DrawParamRows(CollectAssigned(w.name, "", ""));

            int tabIndexToDelete = -1;
            for (int ti = 0; ti < static_cast<int>(w.tabs.size()); ++ti)
            {
                TabDef &t = w.tabs[ti];
                ImGui::PushID(1000 + ti);

                bool tabOpen = ImGui::TreeNodeEx(("[タブ] " + t.name).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                HandleDropTarget(w.name, t.name, "");

                if (ImGui::BeginPopupContextItem("tabMenu"))
                {
                    if (ImGui::MenuItem("タブを削除（中身は未分類へ）"))
                    {
                        tabIndexToDelete = ti;
                    }
                    ImGui::EndPopup();
                }

                if (tabOpen)
                {
                    // セクション追加
                    static char newSectionName[64] = {};
                    ImGui::SetNextItemWidth(140.0f);
                    ImGui::InputTextWithHint("##newSection", "新しいセクション名", newSectionName, sizeof(newSectionName));
                    ImGui::SameLine();
                    if (ImGui::Button("セクション追加") && newSectionName[0] != '\0')
                    {
                        bool exists = false;
                        for (const auto &s : t.sections)
                        {
                            exists |= (s.name == newSectionName);
                        }
                        if (!exists)
                        {
                            t.sections.push_back({newSectionName});
                            layoutDirty_ = true;
                        }
                        newSectionName[0] = '\0';
                    }

                    // タブ直下のパラメータ
                    DrawParamRows(CollectAssigned(w.name, t.name, ""));

                    int sectionIndexToDelete = -1;
                    for (int si = 0; si < static_cast<int>(t.sections.size()); ++si)
                    {
                        SectionDef &s = t.sections[si];
                        ImGui::PushID(2000 + si);

                        bool sectionOpen = ImGui::TreeNodeEx(("[区切] " + s.name).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                        HandleDropTarget(w.name, t.name, s.name);

                        if (ImGui::BeginPopupContextItem("sectionMenu"))
                        {
                            if (ImGui::MenuItem("セクションを削除（中身は未分類へ）"))
                            {
                                sectionIndexToDelete = si;
                            }
                            ImGui::EndPopup();
                        }

                        if (sectionOpen)
                        {
                            DrawParamRows(CollectAssigned(w.name, t.name, s.name));
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }

                    if (sectionIndexToDelete >= 0)
                    {
                        // 割り当てを未分類へ戻してからセクション定義を消す
                        for (auto &[key, a] : assignments_)
                        {
                            (void)key;
                            if (a.window == w.name && a.tab == t.name && a.section == t.sections[sectionIndexToDelete].name)
                            {
                                a = {};
                            }
                        }
                        std::erase_if(assignments_, [](const auto &pair) { return pair.second.window.empty(); });
                        t.sections.erase(t.sections.begin() + sectionIndexToDelete);
                        layoutDirty_ = true;
                    }

                    ImGui::TreePop();
                }
                ImGui::PopID();
            }

            if (tabIndexToDelete >= 0)
            {
                for (auto &[key, a] : assignments_)
                {
                    (void)key;
                    if (a.window == w.name && a.tab == w.tabs[tabIndexToDelete].name)
                    {
                        a = {};
                    }
                }
                std::erase_if(assignments_, [](const auto &pair) { return pair.second.window.empty(); });
                w.tabs.erase(w.tabs.begin() + tabIndexToDelete);
                layoutDirty_ = true;
            }

            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    if (windowIndexToDelete >= 0)
    {
        for (auto &[key, a] : assignments_)
        {
            (void)key;
            if (a.window == windows_[windowIndexToDelete].name)
            {
                a = {};
            }
        }
        std::erase_if(assignments_, [](const auto &pair) { return pair.second.window.empty(); });
        windows_.erase(windows_.begin() + windowIndexToDelete);
        layoutDirty_ = true;
    }
}

void GameParamHub::DrawHubWindow(bool *open)
{
    if (open && !*open)
    {
        return;
    }

    ImGui::Begin("ゲームパラメータHub", open, ImGuiWindowFlags_NoFocusOnAppearing);

    // ── ツールバー ──
    if (ImGui::Button("仕分けを保存"))
    {
        SaveLayout();
    }
    ImGui::SameLine();
    if (ImGui::Button("再読込"))
    {
        LoadLayout();
    }
    if (layoutDirty_)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "※未保存の変更あり");
    }
    // 複数選択中はまとめて選択解除できるようにする
    if (!selectedKeys_.empty())
    {
        ImGui::SameLine();
        if (ImGui::Button(("選択解除 (" + std::to_string(selectedKeys_.size()) + ")").c_str()))
        {
            selectedKeys_.clear();
        }
    }
    ImGui::TextDisabled("「≡」クリックで選択(Ctrl=複数/Shift=範囲)・ドラッグでウィンドウへ移動 / 右クリックで移動・元に戻すメニュー");

    if (ImGui::BeginTabBar("hubTabs"))
    {
        // ── ラベル別一覧（各ラベルの値の保存 ＋ 未分類項目の仕分け元） ──
        if (ImGui::BeginTabItem("ラベル / 未分類"))
        {
            ImGui::TextDisabled("各ラベルの[保存]で現在値をJSONに保存（次回起動時に自動復元）。[初期化]で元の値に戻す。「≡」クリックで選択(Ctrl/Shift対応)→ドラッグでウィンドウへ移動。");
            static ImGuiTextFilter filter;
            filter.Draw("検索", 180.0f);

            // owner の登場順を保ちながらグループ化する
            std::vector<std::string> owners;
            for (const auto &e : entries_)
            {
                if (std::find(owners.begin(), owners.end(), e.owner) == owners.end())
                {
                    owners.push_back(e.owner);
                }
            }

            for (const auto &owner : owners)
            {
                // このラベルの未分類キー（フィルタ適用）を集める
                std::vector<std::string> keys;
                for (const auto &e : entries_)
                {
                    if (e.owner != owner)
                    {
                        continue;
                    }
                    const std::string key = MakeKey(e.owner, e.name);
                    if (assignments_.count(key))
                    {
                        continue;
                    }
                    if (!filter.PassFilter(key.c_str()))
                    {
                        continue;
                    }
                    keys.push_back(key);
                }

                ImGui::PushID(owner.c_str());
                bool nodeOpen = ImGui::TreeNodeEx(owner.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

                // ラベルごとの [保存] / [復元] / [初期化] ボタン（ヘッダ右）
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.40f, 0.85f));
                if (ImGui::SmallButton("保存"))
                {
                    RequestConfirm("「" + owner + "」の全項目を現在の値で保存します。よろしいですか？",
                                   [this, owner] {
                                       SaveOwnerValues(owner);
                                       ImGuiNotification::Post("「" + owner + "」の値を保存しました", {0.45f, 0.68f, 0.52f, 1.0f});
                                   });
                }
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("このラベルの全項目の現在値をJSONに保存（次回起動時に自動復元）");
                ImGui::SameLine();
                if (ImGui::SmallButton("復元"))
                {
                    LoadOwnerValues(owner);
                    ImGuiNotification::Post("「" + owner + "」の保存値を復元しました", {0.42f, 0.66f, 0.68f, 1.0f});
                }
                ImGui::SetItemTooltip("保存済みの値に戻す（確認なし）");
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.35f, 0.20f, 0.85f));
                if (ImGui::SmallButton("初期化"))
                {
                    RequestConfirm("「" + owner + "」の全項目を元の値（コード既定値）に戻し、保存済みの値も削除します。よろしいですか？",
                                   [this, owner] {
                                       ResetOwnerToOriginal(owner);
                                       ImGuiNotification::Post("「" + owner + "」を元の値に戻しました", {0.82f, 0.58f, 0.36f, 1.0f});
                                   });
                }
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("元の値（コード既定値）に戻す＋保存済みの値を削除");

                if (nodeOpen)
                {
                    if (keys.empty())
                    {
                        ImGui::TextDisabled("（別ウィンドウへ仕分け済み／表示できる項目なし）");
                    }
                    else
                    {
                        DrawParamRows(keys);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }

        // ── レイアウト編集（ウィンドウ/タブ/セクション作成＋仕分け先ツリー） ──
        if (ImGui::BeginTabItem("レイアウト"))
        {
            DrawLayoutTree();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void GameParamHub::DrawUserWindows()
{
    for (const auto &w : windows_)
    {
        ImGui::Begin(w.name.c_str(), nullptr, ImGuiWindowFlags_NoFocusOnAppearing);

        // このウィンドウ内（全タブ・全区切り）の値をまとめて保存 / 初期化
        float halfW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.40f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.60f, 0.50f, 0.95f));
        if (ImGui::Button("このウィンドウの値を保存", ImVec2(halfW, 0.0f)))
        {
            std::string wn = w.name;
            RequestConfirm("「" + wn + "」内の全項目を現在の値で保存します。よろしいですか？",
                           [this, wn] {
                               int n = SaveAssignedValues(wn, "", "");
                               ImGuiNotification::Post("「" + wn + "」の " + std::to_string(n) + " 項目を保存しました", {0.45f, 0.68f, 0.52f, 1.0f});
                           });
        }
        ImGui::PopStyleColor(2);
        ImGui::SetItemTooltip("このウィンドウ内（全タブ・全区切り）の全項目の現在値を保存します");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.35f, 0.20f, 0.85f));
        if (ImGui::Button("このウィンドウを初期化", ImVec2(halfW, 0.0f)))
        {
            std::string wn = w.name;
            RequestConfirm("「" + wn + "」内の全項目を元の値（コード既定値）に戻し、保存済みの値も削除します。よろしいですか？",
                           [this, wn] {
                               int n = ResetAssignedToOriginal(wn, "", "");
                               ImGuiNotification::Post("「" + wn + "」の " + std::to_string(n) + " 項目を元の値に戻しました", {0.82f, 0.58f, 0.36f, 1.0f});
                           });
        }
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("このウィンドウ内の全項目を元の値（コード既定値）に戻します");

        // このウィンドウ自体をドロップ先にする（Hubから項目をここへ直接落として移動できる）
        DrawDropZone("＋ ここにドロップしてこのウィンドウへ移動", w.name, "", "");

        // ウィンドウ直下
        DrawParamRows(CollectAssigned(w.name, "", ""));

        if (!w.tabs.empty())
        {
            if (ImGui::BeginTabBar(("tabs_" + w.name).c_str()))
            {
                for (const auto &t : w.tabs)
                {
                    if (ImGui::BeginTabItem(t.name.c_str()))
                    {
                        // このタブ配下をまとめて保存 / 初期化
                        if (ImGui::SmallButton(("このタブを保存##" + w.name + t.name).c_str()))
                        {
                            std::string wn = w.name, tn = t.name;
                            RequestConfirm("「" + tn + "」タブの全項目を現在の値で保存します。よろしいですか？",
                                           [this, wn, tn] {
                                               int n = SaveAssignedValues(wn, tn, "");
                                               ImGuiNotification::Post("「" + tn + "」タブの " + std::to_string(n) + " 項目を保存しました", {0.45f, 0.68f, 0.52f, 1.0f});
                                           });
                        }
                        ImGui::SetItemTooltip("このタブ配下（区切り含む）の全項目を保存します");
                        ImGui::SameLine();
                        if (ImGui::SmallButton(("初期化##tab" + w.name + t.name).c_str()))
                        {
                            std::string wn = w.name, tn = t.name;
                            RequestConfirm("「" + tn + "」タブの全項目を元の値（コード既定値）に戻します。よろしいですか？",
                                           [this, wn, tn] {
                                               int n = ResetAssignedToOriginal(wn, tn, "");
                                               ImGuiNotification::Post("「" + tn + "」タブの " + std::to_string(n) + " 項目を元の値に戻しました", {0.82f, 0.58f, 0.36f, 1.0f});
                                           });
                        }
                        ImGui::SetItemTooltip("このタブ配下の全項目を元の値に戻します");

                        // このタブをドロップ先にする
                        DrawDropZone("＋ このタブへドロップ", w.name, t.name, "");

                        // タブ直下
                        DrawParamRows(CollectAssigned(w.name, t.name, ""));

                        for (const auto &s : t.sections)
                        {
                            if (ImGui::CollapsingHeader(s.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                // この区切り配下をまとめて保存 / 初期化
                                if (ImGui::SmallButton(("この区切りを保存##" + w.name + t.name + s.name).c_str()))
                                {
                                    std::string wn = w.name, tn = t.name, sn = s.name;
                                    RequestConfirm("「" + sn + "」区切りの全項目を現在の値で保存します。よろしいですか？",
                                                   [this, wn, tn, sn] {
                                                       int n = SaveAssignedValues(wn, tn, sn);
                                                       ImGuiNotification::Post("「" + sn + "」区切りの " + std::to_string(n) + " 項目を保存しました", {0.45f, 0.68f, 0.52f, 1.0f});
                                                   });
                                }
                                ImGui::SetItemTooltip("この区切り内の全項目を保存します");
                                ImGui::SameLine();
                                if (ImGui::SmallButton(("初期化##sec" + w.name + t.name + s.name).c_str()))
                                {
                                    std::string wn = w.name, tn = t.name, sn = s.name;
                                    RequestConfirm("「" + sn + "」区切りの全項目を元の値（コード既定値）に戻します。よろしいですか？",
                                                   [this, wn, tn, sn] {
                                                       int n = ResetAssignedToOriginal(wn, tn, sn);
                                                       ImGuiNotification::Post("「" + sn + "」区切りの " + std::to_string(n) + " 項目を元の値に戻しました", {0.82f, 0.58f, 0.36f, 1.0f});
                                                   });
                                }
                                ImGui::SetItemTooltip("この区切り内の全項目を元の値に戻します");

                                // このセクションをドロップ先にする
                                DrawDropZone("＋ この区切りへドロップ", w.name, t.name, s.name);
                                DrawParamRows(CollectAssigned(w.name, t.name, s.name));
                            }
                        }
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        }

        ImGui::End();
    }
}

#endif // USE_IMGUI

} // namespace Hagine
