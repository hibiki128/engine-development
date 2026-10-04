#include "SceneTransition.h"
#ifdef USE_IMGUI
#include "SceneManager.h"
#include "SceneRegistry.h"
#include "imgui.h"
#include "utility/debug/imgui/AssetDragDrop.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include <asset/AssetPath.h>
#include <graphics/texture/TextureManager.h>
#include <icon/IconsFontAwesome5.h>
#include <algorithm>
#include <format>
#endif // USE_IMGUI

// 「シーン遷移」窓。演出の一覧・編集・プレビュー・使い分けの決まりを扱う
namespace Hagine {

#ifdef USE_IMGUI
namespace {

/// <summary>EasingType の並びに対応する表示名</summary>
const char *kEasingNames[] = {
    "Linear",
    "InSine", "OutSine", "InOutSine",
    "InQuad", "OutQuad", "InOutQuad",
    "InCubic", "OutCubic", "InOutCubic",
    "InQuart", "OutQuart", "InOutQuart",
    "InQuint", "OutQuint", "InOutQuint",
    "InCirc", "OutCirc", "InOutCirc",
    "InExpo", "OutExpo", "InOutExpo",
    "InBack", "OutBack", "InOutBack",
    "InElastic", "OutElastic", "InOutElastic",
    "InBounce", "OutBounce", "InOutBounce"};
constexpr int kEasingCount = static_cast<int>(sizeof(kEasingNames) / sizeof(kEasingNames[0]));

/// <summary>形の表示名と説明（TransitionShape の並び）</summary>
struct ShapeInfo
{
    const char *name;
    const char *description;
};
constexpr ShapeInfo kShapes[] = {
    {"フェード", "画面全体が一度に覆われる"},
    {"ワイプ", "一方向から流れ込む"},
    {"円（アイリス）", "円が広がる／「反転」で円が絞られて閉じる"},
    {"四角", "四角が広がる"},
    {"ひし形", "ひし形が広がる"},
    {"時計", "時計の針のように回って覆う"},
    {"ブラインド", "細い帯が一斉に閉じる"},
    {"滑り込む帯", "帯が順番をずらして滑り込む"},
    {"扉（両側から）", "両側から閉じる／「反転」で中央から開く"},
    {"四角いマス", "四角いマスが順に埋まる"},
    {"六角形のマス", "六角形のマスが順に埋まる"},
    {"三角形のマス", "三角形のマスが順に埋まる"},
    {"丸いマス（水玉）", "丸い点が大きくなって埋まる"},
    {"ノイズで溶ける", "まだらに溶けるように覆う（ディゾルブ）"},
    {"うずまき", "中心から渦を描くように覆う"},
    {"星形", "星形が広がる／「反転」で星形に絞られる"},
    {"ハート", "ハートが広がる／「反転」でハートに絞られる"},
    {"波打つワイプ", "境目が波打ちながら流れる"},
    {"扇", "扇が開くように覆う"},
    {"ルール画像", "白黒の画像の暗い所から順に覆う（ノベルゲームの「ルール画像」）。画像は自由に描ける"},
};
static_assert(sizeof(kShapes) / sizeof(kShapes[0]) == static_cast<size_t>(TransitionShape::Count), "kShapes は TransitionShape と同数にすること");

const char *kFillNames = "単色\0グラデーション\0マスごとに色を塗り分け\0画像\0前の画面\0";
const char *kOrderNames = "ばらばら\0向きに沿って順に\0中心から\0交互（市松・1本おき）\0";

bool UsesAngle(TransitionShape s)
{
    switch (s)
    {
    case TransitionShape::Fade:
    case TransitionShape::Dissolve:
    case TransitionShape::RuleImage:
    case TransitionShape::Tiles:
    case TransitionShape::Hexagons:
    case TransitionShape::Triangles:
    case TransitionShape::Dots:
        return false;
    default:
        return true;
    }
}

bool UsesCenter(TransitionShape s)
{
    switch (s)
    {
    case TransitionShape::Iris:
    case TransitionShape::Box:
    case TransitionShape::Diamond:
    case TransitionShape::Clock:
    case TransitionShape::Spiral:
    case TransitionShape::Star:
    case TransitionShape::Heart:
    case TransitionShape::Fan:
        return true;
    default:
        return false;
    }
}

bool IsCellShape(TransitionShape s)
{
    return s == TransitionShape::Tiles || s == TransitionShape::Hexagons || s == TransitionShape::Triangles ||
           s == TransitionShape::Dots || s == TransitionShape::Bars;
}

/// <summary>「数」の欄の名前（形ごとに意味が違う）。使わない形は nullptr</summary>
const char *CountLabel(TransitionShape s)
{
    switch (s)
    {
    case TransitionShape::Clock: return "羽根の数";
    case TransitionShape::Blinds: return "帯の本数";
    case TransitionShape::Bars: return "帯の本数";
    case TransitionShape::Tiles:
    case TransitionShape::Hexagons:
    case TransitionShape::Triangles:
    case TransitionShape::Dots: return "縦のマスの数";
    case TransitionShape::Dissolve: return "模様の細かさ";
    case TransitionShape::Spiral: return "巻き数";
    case TransitionShape::Star: return "とげの数";
    case TransitionShape::Wave: return "波の数";
    case TransitionShape::Fan: return "扇の数";
    default: return nullptr;
    }
}

/// <summary>images からの相対パスの画像を選ぶ欄（アセットブラウザからドロップ）</summary>
bool ImageSlot(const char *label, std::string &relativePath)
{
    bool changed = false;
    ImGui::PushID(label);
    TextureManager *pTextureManager = TextureManager::GetInstance();
    D3D12_GPU_DESCRIPTOR_HANDLE handle{};
    if (!relativePath.empty())
    {
        pTextureManager->LoadTexture(relativePath);
        handle = pTextureManager->GetSrvHandleGPU(AssetPath::Image(relativePath));
    }
    if (handle.ptr != 0)
    {
        ImGui::Image(static_cast<ImTextureID>(handle.ptr), ImVec2(48.0f, 48.0f));
    }
    else
    {
        ImGui::Button("ここへ\nドロップ", ImVec2(48.0f, 48.0f));
    }
    std::string dropped;
    if (AssetDragDrop::TextureTarget(dropped))
    {
        relativePath = dropped;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(label);
    ImGui::TextDisabled("%s", relativePath.empty() ? "(未設定) アセットブラウザからドロップ" : relativePath.c_str());
    if (!relativePath.empty() && ImGui::SmallButton(ICON_FA_TIMES " 外す"))
    {
        relativePath.clear();
        changed = true;
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

/// <summary>シーン名を選ぶ（"*" はどれでも）</summary>
bool SceneCombo(const char *id, std::string &value, bool allowAny)
{
    bool changed = false;
    const std::string preview = (value.empty() || value == "*") ? std::string("（どれでも）") : value;
    if (ImGui::BeginCombo(id, preview.c_str()))
    {
        if (allowAny && ImGui::Selectable("（どれでも）", value == "*" || value.empty()))
        {
            value = "*";
            changed = true;
        }
        for (const std::string &name : SceneRegistry::GetInstance()->GetSceneNames())
        {
            if (ImGui::Selectable(name.c_str(), value == name))
            {
                value = name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/// <summary>演出の名前を選ぶ</summary>
bool PresetCombo(const char *id, std::string &value, const TransitionLibrary &library)
{
    bool changed = false;
    if (ImGui::BeginCombo(id, value.empty() ? "（未設定）" : value.c_str()))
    {
        for (const TransitionPreset &preset : library.presets)
        {
            if (ImGui::Selectable(preset.name.c_str(), preset.name == value))
            {
                value = preset.name;
                changed = true;
            }
            if (!preset.description.empty())
            {
                ImGui::SetItemTooltip("%s", preset.description.c_str());
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace
#endif // USE_IMGUI

void SceneTransition::DrawEditor()
{
#ifdef USE_IMGUI
    TransitionLibrary &library = library_;
    if (library.presets.empty())
    {
        library.presets = TransitionLibrary::BuiltInPresets();
    }
    selectedPreset_ = std::clamp(selectedPreset_, 0, static_cast<int>(library.presets.size()) - 1);

    // 実際の切り替え中でなければ、プレビューは編集中の演出をそのまま映す
    const bool switching = !isEnd_ && (fadeInStart_ || fadeOutStart_);
    if (!switching)
    {
        activePreset_ = library.presets[static_cast<size_t>(selectedPreset_)];
    }

    DrawPreviewControls();
    ImGui::Separator();

    const float listWidth = 230.0f;
    if (ImGui::BeginChild("##transitionList", ImVec2(listWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX))
    {
        DrawPresetList();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##transitionDetail", ImVec2(0.0f, 0.0f)))
    {
        if (ImGui::BeginTabBar("##transitionTabs"))
        {
            TransitionPreset &preset = library.presets[static_cast<size_t>(selectedPreset_)];
            if (ImGui::BeginTabItem(ICON_FA_SLIDERS_H " 基本"))
            {
                DrawPresetDetails(preset);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_FA_ARROW_RIGHT " 前半（覆う）"))
            {
                DrawPhaseEditor(preset.cover, false, preset.revealMode);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_FA_ARROW_LEFT " 後半（明ける）"))
            {
                if (preset.revealMode == TransitionRevealMode::Custom)
                {
                    DrawPhaseEditor(preset.reveal, true, preset.revealMode);
                }
                else
                {
                    DimText(preset.revealMode == TransitionRevealMode::Reverse
                                ? "いまは「覆ったときの逆再生」で明けます。前半の幕がそのまま逆に動きます。"
                                : "いまは「同じ向きへ抜ける」で明けます。前半の幕が、覆ったのと同じ順番で消えていきます。");
                    DimText("明け方を別に作るときは「基本」タブの「明け方」を「別に作る」にしてください。");
                    ImGui::Spacing();
                    if (PrimaryButton(ICON_FA_COPY " 前半の幕を写して、明け方を別に作る"))
                    {
                        preset.reveal.layers = preset.cover.layers;
                        preset.reveal.sceneFx = preset.cover.sceneFx;
                        if (preset.revealMode == TransitionRevealMode::PassThrough)
                        {
                            for (TransitionLayer &layer : preset.reveal.layers)
                            {
                                layer.invert = !layer.invert;
                            }
                        }
                        preset.revealMode = TransitionRevealMode::Custom;
                        MarkDirty();
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_FA_ROUTE " 使い分け"))
            {
                DrawRuleEditor();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();
#endif // USE_IMGUI
}

#ifdef USE_IMGUI
void SceneTransition::DrawPresetList()
{
    TransitionLibrary &library = library_;

    // 作る・保存
    if (PrimaryButton(ICON_FA_PLUS " 新しく作る"))
    {
        ImGui::OpenPopup("##newTransition");
    }
    ImGui::SameLine();
    if (NeutralButton(dirty_ ? ICON_FA_SAVE " 保存 ●" : ICON_FA_SAVE " 保存"))
    {
        if (library.Save())
        {
            dirty_ = false;
            ImGuiNotification::Post("シーン遷移を保存しました: " + TransitionLibrary::FilePath());
        }
        else
        {
            ImGuiNotification::Post("シーン遷移を保存できませんでした", {0.85f, 0.42f, 0.42f, 1.0f});
        }
    }
    ImGui::SetItemTooltip("演出の一覧と使い分けを %s に保存します", TransitionLibrary::FilePath().c_str());

    if (ImGui::BeginPopup("##newTransition"))
    {
        if (ImGui::MenuItem(ICON_FA_FILE " 空から（黒のフェード1枚）"))
        {
            TransitionPreset preset;
            preset.name = library.MakeUniqueName("新しい遷移");
            TransitionLayer layer;
            layer.name = "黒";
            preset.cover.layers.push_back(layer);
            library.presets.push_back(preset);
            selectedPreset_ = static_cast<int>(library.presets.size()) - 1;
            MarkDirty();
        }
        ImGui::SeparatorText("ひな形から");
        for (const TransitionPreset &builtIn : TransitionLibrary::BuiltInPresets())
        {
            if (ImGui::MenuItem(builtIn.name.c_str()))
            {
                TransitionPreset preset = builtIn;
                preset.name = library.MakeUniqueName(builtIn.name);
                library.presets.push_back(preset);
                selectedPreset_ = static_cast<int>(library.presets.size()) - 1;
                MarkDirty();
            }
            ImGui::SetItemTooltip("%s", builtIn.description.c_str());
        }
        ImGui::EndPopup();
    }

    static std::string search;
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##transitionSearch", ICON_FA_SEARCH " 名前で探す", &search);
    ImGui::Spacing();

    int pendingDelete = -1;
    int pendingDuplicate = -1;
    for (int i = 0; i < static_cast<int>(library.presets.size()); ++i)
    {
        TransitionPreset &preset = library.presets[static_cast<size_t>(i)];
        if (!search.empty() && preset.name.find(search) == std::string::npos)
        {
            continue;
        }
        ImGui::PushID(i);
        const bool isDefault = (preset.name == library.defaultPreset);
        const std::string label = std::format("{}{}", isDefault ? ICON_FA_STAR " " : "", preset.name);
        if (ImGui::Selectable(label.c_str(), selectedPreset_ == i))
        {
            selectedPreset_ = i;
            previewTime_ = 0.0f;
        }
        if (!preset.description.empty())
        {
            ImGui::SetItemTooltip("%s", preset.description.c_str());
        }
        // ダブルクリックでその場で再生
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            previewing_ = true;
            previewPlaying_ = true;
            previewTime_ = 0.0f;
        }
        if (ImGui::BeginPopupContextItem("##presetMenu"))
        {
            if (ImGui::MenuItem(ICON_FA_STAR " 既定にする", nullptr, false, !isDefault))
            {
                library.defaultPreset = preset.name;
                MarkDirty();
            }
            if (ImGui::MenuItem(ICON_FA_COPY " 複製"))
            {
                pendingDuplicate = i;
            }
            if (ImGui::MenuItem(ICON_FA_TRASH " 削除", nullptr, false, library.presets.size() > 1))
            {
                pendingDelete = i;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (pendingDuplicate >= 0)
    {
        TransitionPreset copy = library.presets[static_cast<size_t>(pendingDuplicate)];
        copy.name = library.MakeUniqueName(copy.name);
        library.presets.insert(library.presets.begin() + pendingDuplicate + 1, copy);
        selectedPreset_ = pendingDuplicate + 1;
        MarkDirty();
    }
    if (pendingDelete >= 0)
    {
        const std::string removed = library.presets[static_cast<size_t>(pendingDelete)].name;
        library.presets.erase(library.presets.begin() + pendingDelete);
        if (library.defaultPreset == removed)
        {
            library.defaultPreset = library.presets.front().name;
        }
        selectedPreset_ = std::clamp(selectedPreset_, 0, static_cast<int>(library.presets.size()) - 1);
        MarkDirty();
        ImGuiNotification::Post("シーン遷移の演出を削除しました: " + removed, {0.82f, 0.58f, 0.36f, 1.0f});
    }

    ImGui::Spacing();
    DimText(ICON_FA_STAR " = 既定（決まりに当てはまらないときに使う）");
    DimText("右クリックで既定にする・複製・削除。ダブルクリックで再生");
}

void SceneTransition::DrawPreviewControls()
{
    const TransitionPreset &preset = activePreset_;
    const float total = (std::max)(preset.TotalDuration(), 0.001f);
    const bool switching = !isEnd_ && (fadeInStart_ || fadeOutStart_);

    ImGui::BeginDisabled(switching);
    if (PrimaryButton(previewPlaying_ ? ICON_FA_PAUSE " 一時停止" : ICON_FA_PLAY " 再生"))
    {
        if (!previewing_ || previewTime_ >= total)
        {
            previewTime_ = 0.0f;
        }
        previewing_ = true;
        previewPlaying_ = !previewPlaying_;
    }
    ImGui::SetItemTooltip("シーンを切り替えずに、今の画面へこの演出を掛けて見ます（覆う → 待つ → 明ける）");
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_STOP " 止める"))
    {
        previewing_ = false;
        previewPlaying_ = false;
        previewTime_ = 0.0f;
    }
    ImGui::SameLine();
    ImGui::Checkbox("繰り返す", &previewLoop_);
    ImGui::EndDisabled();

    // 本番と同じ流れ（実際にシーンを切り替える）で試す
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    if (previewScene_.empty())
    {
        previewScene_ = SceneManager::GetInstance()->GetCurrentSceneName();
    }
    SceneCombo("##previewScene", previewScene_, false);
    ImGui::SameLine();
    ImGui::BeginDisabled(switching || previewScene_.empty());
    if (NeutralButton(ICON_FA_DOOR_OPEN " このシーンへ切り替えて試す"))
    {
        previewing_ = false;
        previewPlaying_ = false;
        SceneManager::GetInstance()->SceneSelection(previewScene_, preset.name);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("選んだシーンへ、この演出で実際に切り替えます（保存していない変更も使われます）");

    // 時間の帯: 前半・待ち・後半を色分けし、押す・ドラッグでその時刻の見た目にする
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 26.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##transitionTimeline", size);
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const float coverEnd = preset.cover.duration / total;
    const float holdEnd = (preset.cover.duration + preset.hold) / total;
    auto segment = [&](float from, float to, ImVec4 color, const char *text) {
        const ImVec2 a(origin.x + size.x * from, origin.y);
        const ImVec2 b(origin.x + size.x * to, origin.y + size.y);
        color.w = 0.45f;
        drawList->AddRectFilled(a, b, ImGui::GetColorU32(color), 3.0f);
        if (b.x - a.x > ImGui::CalcTextSize(text).x + 6.0f)
        {
            drawList->AddText(ImVec2(a.x + 4.0f, a.y + 5.0f), ImGui::GetColorU32(ImGuiCol_Text), text);
        }
    };
    segment(0.0f, coverEnd, DebugTheme::kAccentOrange, "覆う");
    segment(coverEnd, holdEnd, DebugTheme::kAccentPurple, "待つ（ここで切り替わる）");
    segment(holdEnd, 1.0f, DebugTheme::kAccentBlue, "明ける");
    if (switching)
    {
        DimText("シーンを切り替え中です");
    }
    else
    {
        if (ImGui::IsItemActive())
        {
            const float t = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f);
            previewTime_ = t * total;
            previewing_ = true;
            previewPlaying_ = false;
        }
        const float cursorX = origin.x + size.x * std::clamp(previewTime_ / total, 0.0f, 1.0f);
        if (previewing_)
        {
            drawList->AddLine(ImVec2(cursorX, origin.y - 2.0f), ImVec2(cursorX, origin.y + size.y + 2.0f),
                              ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), 2.0f);
        }
        ImGui::SetItemTooltip("押す・ドラッグでその時刻の見た目で止めます（%.2f 秒 / 全体 %.2f 秒）", previewTime_, total);
    }
}

void SceneTransition::DrawPresetDetails(TransitionPreset &preset)
{
    TransitionLibrary &library = library_;

    // 名前を変えたら、既定・決まりの参照も付け替える
    std::string name = preset.name;
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::InputText("名前", &name, ImGuiInputTextFlags_EnterReturnsTrue) && !name.empty() && name != preset.name)
    {
        const std::string unique = library.MakeUniqueName(name);
        for (TransitionRule &rule : library.rules)
        {
            if (rule.preset == preset.name)
            {
                rule.preset = unique;
            }
        }
        if (library.defaultPreset == preset.name)
        {
            library.defaultPreset = unique;
        }
        preset.name = unique;
        MarkDirty();
    }
    ImGui::SetItemTooltip("Enter で決定。ゲームからは NextSceneReservation(\"シーン名\", \"この名前\") で指定できます");
    ImGui::SameLine();
    const bool isDefault = (library.defaultPreset == preset.name);
    ImGui::BeginDisabled(isDefault);
    if (NeutralButton(isDefault ? ICON_FA_STAR " 既定" : ICON_FA_STAR " 既定にする"))
    {
        library.defaultPreset = preset.name;
        MarkDirty();
    }
    ImGui::EndDisabled();

    if (ImGui::InputTextMultiline("説明", &preset.description, ImVec2(-1.0f, 44.0f)))
    {
        MarkDirty();
    }

    SectionHeader("[ 時間 ]", DebugTheme::kAccentOrange);
    bool changed = false;
    changed |= ImGui::DragFloat("覆う時間（秒）", &preset.cover.duration, 0.01f, 0.0f, 10.0f, "%.2f");
    ImGui::SetItemTooltip("0 にすると一瞬で覆います（クロスフェードのように前の画面から直接切り替える演出で使います）");
    changed |= ImGui::DragFloat("待つ時間（秒）", &preset.hold, 0.01f, 0.0f, 10.0f, "%.2f");
    ImGui::SetItemTooltip("覆いきってから明け始めるまで。この間にシーンが切り替わります（読み込みが重いときは自動で延びます）");
    changed |= ImGui::DragFloat("明ける時間（秒）", &preset.reveal.duration, 0.01f, 0.0f, 10.0f, "%.2f");
    preset.cover.duration = (std::max)(preset.cover.duration, 0.0f);
    preset.hold = (std::max)(preset.hold, 0.0f);
    preset.reveal.duration = (std::max)(preset.reveal.duration, 0.0f);
    DimText(std::format("合計 {:.2f} 秒", preset.TotalDuration()).c_str());

    SectionHeader("[ 明け方 ]", DebugTheme::kAccentBlue);
    int mode = static_cast<int>(preset.revealMode);
    changed |= ImGui::RadioButton("覆ったときの逆再生", &mode, static_cast<int>(TransitionRevealMode::Reverse));
    ImGui::SetItemTooltip("円が閉じたなら、明けるときは円が開く");
    ImGui::SameLine();
    changed |= ImGui::RadioButton("同じ向きへ抜ける", &mode, static_cast<int>(TransitionRevealMode::PassThrough));
    ImGui::SetItemTooltip("左から覆ったなら、明けるときも左から消えていく（幕が通り抜ける）");
    ImGui::SameLine();
    changed |= ImGui::RadioButton("別に作る", &mode, static_cast<int>(TransitionRevealMode::Custom));
    ImGui::SetItemTooltip("「後半（明ける）」タブで、明けるときの幕を自由に組みます");
    if (mode != static_cast<int>(preset.revealMode))
    {
        // 別に作るへ切り替えたとき、空なら前半の幕を写して始められるようにする
        if (mode == static_cast<int>(TransitionRevealMode::Custom) && preset.reveal.layers.empty())
        {
            preset.reveal.layers = preset.cover.layers;
            preset.reveal.sceneFx = preset.cover.sceneFx;
        }
        preset.revealMode = static_cast<TransitionRevealMode>(mode);
    }
    if (changed)
    {
        MarkDirty();
    }

    SectionHeader("[ 使い方 ]", DebugTheme::kAccentGreen);
    DimText("・ゲームから: SceneManager::GetInstance()->NextSceneReservation(\"GAME\", \"演出の名前\")");
    DimText("・名前を渡さなければ「使い分け」タブの決まり → 既定（" ICON_FA_STAR "）の順に選ばれます");
    DimText("・幕は最大4枚まで重ねられます。前半/後半の中で動く時間をずらすと、2段・3段の演出になります");
}

void SceneTransition::DrawPhaseEditor(TransitionPhase &phase, bool isReveal, TransitionRevealMode revealMode)
{
    SectionHeader(isReveal ? "[ 明ける幕 ]" : "[ 覆う幕 ]", DebugTheme::kAccentOrange);
    if (isReveal)
    {
        DimText("明けるときは、ここの幕が「覆う動き」を逆にたどって消えていきます。向きを変えるときは各幕の「反転」");
    }
    else
    {
        DimText("下から順に重なります（下の幕ほど手前）。各幕は「前半の中のいつ動くか」を持てます");
    }

    ImGui::BeginDisabled(static_cast<int>(phase.layers.size()) >= TransitionPreset::kMaxLayers);
    if (PrimaryButton(ICON_FA_PLUS " 幕を足す"))
    {
        TransitionLayer layer;
        layer.name = std::format("幕 {}", phase.layers.size() + 1);
        phase.layers.push_back(layer);
        MarkDirty();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    StatusBadge(std::format("{} / {} 枚", phase.layers.size(), TransitionPreset::kMaxLayers).c_str(), DebugTheme::kAccentBlue);

    int pendingRemove = -1;
    int pendingDuplicate = -1;
    int moveUp = -1;
    int moveDown = -1;
    for (int i = 0; i < static_cast<int>(phase.layers.size()); ++i)
    {
        TransitionLayer &layer = phase.layers[static_cast<size_t>(i)];
        ImGui::PushID(i);
        if (ImGui::Checkbox("##enabled", &layer.enabled))
        {
            MarkDirty();
        }
        ImGui::SetItemTooltip("この幕を使う / 使わない");
        ImGui::SameLine();
        const std::string header = std::format("{}. {}  （{}）###layer{}", i + 1, layer.name,
                                               kShapes[static_cast<int>(layer.shape)].name, i);
        const bool open = ThemedHeader(header.c_str(), DebugTheme::kAccentOrange, i == 0);
        if (ImGui::BeginPopupContextItem("##layerMenu"))
        {
            if (ImGui::MenuItem(ICON_FA_ARROW_UP " 上へ", nullptr, false, i > 0))
                moveUp = i;
            if (ImGui::MenuItem(ICON_FA_ARROW_DOWN " 下へ", nullptr, false, i + 1 < static_cast<int>(phase.layers.size())))
                moveDown = i;
            if (ImGui::MenuItem(ICON_FA_COPY " 複製", nullptr, false, static_cast<int>(phase.layers.size()) < TransitionPreset::kMaxLayers))
                pendingDuplicate = i;
            if (ImGui::MenuItem(ICON_FA_TRASH " 削除"))
                pendingRemove = i;
            ImGui::EndPopup();
        }
        ImGui::SetItemTooltip("右クリックで並べ替え・複製・削除");
        if (open)
        {
            ImGui::Indent();
            if (DrawLayerEditor(layer, isReveal, revealMode))
            {
                MarkDirty();
            }
            ImGui::Unindent();
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
    if (moveUp > 0)
    {
        std::swap(phase.layers[static_cast<size_t>(moveUp)], phase.layers[static_cast<size_t>(moveUp - 1)]);
        MarkDirty();
    }
    if (moveDown >= 0)
    {
        std::swap(phase.layers[static_cast<size_t>(moveDown)], phase.layers[static_cast<size_t>(moveDown + 1)]);
        MarkDirty();
    }
    if (pendingDuplicate >= 0)
    {
        TransitionLayer copy = phase.layers[static_cast<size_t>(pendingDuplicate)];
        copy.name += " のコピー";
        phase.layers.insert(phase.layers.begin() + pendingDuplicate + 1, copy);
        MarkDirty();
    }
    if (pendingRemove >= 0)
    {
        phase.layers.erase(phase.layers.begin() + pendingRemove);
        MarkDirty();
    }
    if (phase.layers.empty())
    {
        DimText(isReveal ? "幕がありません（明ける瞬間に新しい画面がそのまま出ます）"
                         : "幕がありません（下の画面の崩し方だけで切り替わります）");
    }

    ImGui::Spacing();
    SectionHeader("[ 下の画面の崩し方 ]", DebugTheme::kAccentPurple);
    DimText(isReveal ? "明けるにつれて弱まります" : "覆うにつれて強くなります（幕と組み合わせて「渦に吸い込まれて暗転」など）");
    DrawSceneFxEditor(phase.sceneFx);
}

bool SceneTransition::DrawLayerEditor(TransitionLayer &layer, bool isReveal, TransitionRevealMode revealMode)
{
    (void)revealMode;
    bool changed = false;
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);

    changed |= ImGui::InputText("名前##layerName", &layer.name);

    // ---- いつ動くか ----
    ImGui::SeparatorText("いつ動くか");
    changed |= ImGui::DragFloatRange2("動く区間", &layer.start, &layer.end, 0.005f, 0.0f, 1.0f, "始め %.2f", "終わり %.2f");
    ImGui::SetItemTooltip(isReveal ? "後半の時間を 0〜1 とした区間（覆う向きで見た区間。明けるときは後ろからたどる）"
                                   : "前半の時間を 0〜1 とした区間。幕ごとにずらすと順に動く演出になります");
    int easing = static_cast<int>(layer.easing);
    if (ImGui::Combo("動き方", &easing, kEasingNames, kEasingCount))
    {
        layer.easing = static_cast<EasingType>(easing);
        changed = true;
    }

    // ---- 形 ----
    ImGui::SeparatorText("形");
    int shape = static_cast<int>(layer.shape);
    if (ImGui::BeginCombo("形", kShapes[shape].name))
    {
        for (int i = 0; i < static_cast<int>(TransitionShape::Count); ++i)
        {
            if (ImGui::Selectable(kShapes[i].name, i == shape))
            {
                layer.shape = static_cast<TransitionShape>(i);
                changed = true;
            }
            ImGui::SetItemTooltip("%s", kShapes[i].description);
        }
        ImGui::EndCombo();
    }
    DimText(kShapes[static_cast<int>(layer.shape)].description);

    const TransitionShape s = layer.shape;
    if (s != TransitionShape::Fade)
    {
        changed |= ImGui::Checkbox(isReveal ? "反転（向きを逆に）" : "反転（覆う順番を逆に）", &layer.invert);
        ImGui::SetItemTooltip("円なら「広がる」と「絞られる」が入れ替わります");
        changed |= ImGui::DragFloat("境目のぼかし", &layer.softness, 0.002f, 0.0f, 1.0f, "%.3f");
    }
    if (UsesAngle(s))
    {
        changed |= ImGui::DragFloat("向き（度）", &layer.angle, 1.0f, -360.0f, 360.0f, "%.0f");
        ImGui::SetItemTooltip("0 で右向き、90 で下向き");
    }
    if (UsesCenter(s))
    {
        changed |= ImGui::DragFloat2("中心（UV）", &layer.center.x, 0.005f, -0.5f, 1.5f);
        ImGui::SetItemTooltip("(0.5, 0.5) が画面の真ん中。キャラの位置に合わせると、そこへ向かって閉じる");
    }
    if (const char *countLabel = CountLabel(s))
    {
        changed |= ImGui::DragFloat(countLabel, &layer.count, 0.05f, 1.0f, 64.0f, "%.1f");
    }
    if (IsCellShape(s))
    {
        int order = static_cast<int>(layer.order);
        if (ImGui::Combo("埋まる順番", &order, kOrderNames))
        {
            layer.order = static_cast<TransitionOrder>(order);
            changed = true;
        }
        changed |= ImGui::SliderFloat("順番のばらつき", &layer.cellSpread, 0.0f, 0.98f);
        ImGui::SetItemTooltip("0 で全部のマスが一斉に、大きいほど順番に時間差が付きます");
        if (layer.order == TransitionOrder::Sweep && !UsesAngle(s))
        {
            changed |= ImGui::DragFloat("順に埋まる向き（度）", &layer.angle, 1.0f, -360.0f, 360.0f, "%.0f");
        }
        if (layer.order == TransitionOrder::Center && !UsesCenter(s))
        {
            changed |= ImGui::DragFloat2("広がり始める中心（UV）", &layer.center.x, 0.005f, -0.5f, 1.5f);
        }
    }
    if (s == TransitionShape::Wave)
    {
        changed |= ImGui::DragFloat("波の大きさ", &layer.amplitude, 0.002f, 0.0f, 0.5f);
    }
    if (IsCellShape(s) || s == TransitionShape::Dissolve)
    {
        changed |= ImGui::DragFloat("乱数の種", &layer.seed, 0.1f, 0.0f, 1000.0f, "%.1f");
        ImGui::SetItemTooltip("変えると、ばらばらの埋まり方・色の割り振りが別の模様になります");
    }
    if (s == TransitionShape::RuleImage)
    {
        changed |= ImageSlot("ルール画像（暗い所から覆う）", layer.ruleImage);
        DimText("白黒のグラデーション画像を描いて置けば、どんな形の切り替えでも作れます");
    }

    // ---- 塗り ----
    ImGui::SeparatorText("塗り");
    int fill = static_cast<int>(layer.fill);
    if (ImGui::Combo("塗り方", &fill, kFillNames))
    {
        layer.fill = static_cast<TransitionFill>(fill);
        changed = true;
    }
    switch (layer.fill)
    {
    case TransitionFill::Color:
        changed |= ImGui::ColorEdit3("色", &layer.color.x);
        break;
    case TransitionFill::Gradient:
        changed |= ImGui::ColorEdit3("始まりの色", &layer.color.x);
        changed |= ImGui::ColorEdit3("終わりの色", &layer.color2.x);
        changed |= ImGui::DragFloat("グラデーションの向き（度）", &layer.gradientAngle, 1.0f, -360.0f, 360.0f, "%.0f");
        break;
    case TransitionFill::Palette:
        changed |= ImGui::SliderInt("色の数", &layer.paletteCount, 1, 4);
        for (int i = 0; i < layer.paletteCount; ++i)
        {
            ImGui::PushID(i);
            changed |= ImGui::ColorEdit3(std::format("色 {}", i + 1).c_str(), &layer.palette[static_cast<size_t>(i)].x);
            ImGui::PopID();
        }
        DimText("マスの形（六角形など）ならマスごと、それ以外は画面を細かく区切った区画ごとに色が決まります");
        break;
    case TransitionFill::Image:
        changed |= ImageSlot("塗りの画像", layer.image);
        changed |= ImGui::ColorEdit3("画像に掛ける色", &layer.color.x);
        changed |= ImGui::DragFloat("繰り返し", &layer.imageScale, 0.05f, 0.0f, 50.0f, "%.2f");
        ImGui::SetItemTooltip("0 で画面いっぱいに引き伸ばす。1 以上で縦にその回数だけ並べる（模様のタイル）");
        break;
    case TransitionFill::PreviousScreen:
        DimText("切り替える直前の画面で塗ります。後半（明ける）で使うと、前の画面から新しい画面へ直接切り替わります");
        DimText("（プレビューではシーンが変わらないので、今の画面とほぼ同じに見えます。「切り替えて試す」で確認してください）");
        break;
    default:
        break;
    }
    changed |= ImGui::SliderFloat("濃さ", &layer.opacity, 0.0f, 1.0f);

    // ---- ふちの光 ----
    if (s != TransitionShape::Fade)
    {
        ImGui::SeparatorText("ふちの光");
        changed |= ImGui::DragFloat("光の幅", &layer.edgeWidth, 0.001f, 0.0f, 0.3f, "%.3f");
        ImGui::SetItemTooltip("0 で光らない。覆っていく境目が光ります（溶けるふちの炎・ワイプの光る縁など）");
        if (layer.edgeWidth > 0.0f)
        {
            changed |= ImGui::ColorEdit3("光の色", &layer.edgeColor.x);
            changed |= ImGui::DragFloat("光の強さ", &layer.edgeIntensity, 0.02f, 0.0f, 10.0f);
        }
    }

    ImGui::PopItemWidth();
    return changed;
}

void SceneTransition::DrawSceneFxEditor(TransitionSceneFx &fx)
{
    bool changed = false;
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    changed |= ImGui::DragFloat("モザイク（ピクセル）", &fx.mosaic, 0.5f, 0.0f, 256.0f, "%.0f");
    changed |= ImGui::DragFloat("ぼかし（ピクセル）", &fx.blur, 0.2f, 0.0f, 64.0f, "%.1f");
    changed |= ImGui::DragFloat("渦（度）", &fx.swirl, 2.0f, -2000.0f, 2000.0f, "%.0f");
    changed |= ImGui::DragFloat("ズーム", &fx.zoom, 0.01f, -0.9f, 10.0f, "%.2f");
    ImGui::SetItemTooltip("+ で寄る / - で引く");
    changed |= ImGui::DragFloat("中心へ流れるぼかし", &fx.zoomBlur, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::DragFloat("回転（度）", &fx.rotate, 1.0f, -1440.0f, 1440.0f, "%.0f");
    changed |= ImGui::DragFloat("色ずれ（ピクセル）", &fx.chroma, 0.2f, 0.0f, 80.0f, "%.1f");
    changed |= ImGui::SliderFloat("色を抜く", &fx.desaturate, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("明るさ（+白 / -黒）", &fx.brightness, -1.0f, 1.0f);
    changed |= ImGui::DragFloat("揺れ（ピクセル）", &fx.shake, 0.2f, 0.0f, 80.0f, "%.1f");
    changed |= ImGui::DragFloat("波打ち（ピクセル）", &fx.wave, 0.2f, 0.0f, 120.0f, "%.1f");
    int easing = static_cast<int>(fx.easing);
    if (ImGui::Combo("強まり方", &easing, kEasingNames, kEasingCount))
    {
        fx.easing = static_cast<EasingType>(easing);
        changed = true;
    }
    ImGui::PopItemWidth();
    if (NeutralButton(ICON_FA_UNDO " 崩さない（全部 0 に）"))
    {
        const EasingType keep = fx.easing;
        fx = TransitionSceneFx{};
        fx.easing = keep;
        changed = true;
    }
    if (changed)
    {
        MarkDirty();
    }
}

void SceneTransition::DrawRuleEditor()
{
    TransitionLibrary &library = library_;
    SectionHeader("[ 既定 ]", DebugTheme::kAccentYellow);
    ImGui::SetNextItemWidth(260.0f);
    if (PresetCombo("既定の演出", library.defaultPreset, library))
    {
        MarkDirty();
    }
    ImGui::SetItemTooltip("ゲームから名前を指定されず、下の決まりにも当てはまらないときに使います");

    SectionHeader("[ 決まり（上から順に、最初に当てはまった物を使う） ]", DebugTheme::kAccentGreen);
    DimText("例: 「TITLE → GAME はアイリス」「どれでも → RESULT は白フェード」");
    int pendingRemove = -1;
    int moveUp = -1;
    if (ImGui::BeginTable("##transitionRules", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("切り替える前", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 18.0f);
        ImGui::TableSetupColumn("切り替えた後", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("使う演出", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(library.rules.size()); ++i)
        {
            TransitionRule &rule = library.rules[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (SceneCombo("##from", rule.from, true))
                MarkDirty();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ICON_FA_ARROW_RIGHT);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (SceneCombo("##to", rule.to, true))
                MarkDirty();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (PresetCombo("##preset", rule.preset, library))
                MarkDirty();
            if (!library.Find(rule.preset))
            {
                ImGui::SameLine();
                ImGui::TextColored(DebugTheme::kAccentRed, ICON_FA_EXCLAMATION_TRIANGLE);
                ImGui::SetItemTooltip("この名前の演出がありません（この決まりは飛ばされます）");
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(ICON_FA_ARROW_UP) && i > 0)
                moveUp = i;
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_TIMES))
                pendingRemove = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (moveUp > 0)
    {
        std::swap(library.rules[static_cast<size_t>(moveUp)], library.rules[static_cast<size_t>(moveUp - 1)]);
        MarkDirty();
    }
    if (pendingRemove >= 0)
    {
        library.rules.erase(library.rules.begin() + pendingRemove);
        MarkDirty();
    }
    if (PrimaryButton(ICON_FA_PLUS " 決まりを足す"))
    {
        TransitionRule rule;
        rule.from = "*";
        rule.to = "*";
        rule.preset = library.presets[static_cast<size_t>(selectedPreset_)].name;
        library.rules.push_back(rule);
        MarkDirty();
    }
    ImGui::SetItemTooltip("いま選んでいる演出を使う決まりを足します");
}
#endif // USE_IMGUI

} // namespace Hagine
