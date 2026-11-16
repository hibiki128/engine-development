#include "BubbleManager.h"
#ifdef USE_IMGUI
#include "../SpriteManager.h"
#include "imgui.h"
#include "utility/debug/imgui/AssetDragDrop.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include <asset/AssetPath.h>
#include <graphics/texture/TextureManager.h>
#include <imgui_stdlib.h>
#include <object/base/BaseObjectManager.h>
#include <algorithm>
#include <cmath>
#include <optional>
#endif // USE_IMGUI

// 「吹き出し」窓。出し方とスタイルの一覧・編集・プレビュー
namespace Hagine {

#ifdef USE_IMGUI
namespace {

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

const char *kTargetNames = "画面\0オブジェクト\0スプライト\0";
const char *kAppearNames = "なし\0薄い所から\0広がる\0下から浮く\0上から落ちる\0左から\0右から\0横に伸びる\0縦に伸びる\0回って広がる\0大きい所から縮む\0";
const char *kCharAppearNames = "そのまま\0薄い所から\0弾む\0上から落ちる\0下から浮く\0右から滑る\0大きい所から縮む\0回る\0震えて止まる\0文字化けから戻る\0";
const char *kEdgeNames = "伸ばす\0繰り返す\0";
const char *kAlignNames = "左\0中央\0右\0";
const char *kWrapNames = "幅で折り返す\0改行だけ\0";

constexpr const char *kPreviewTextKey = "__bubble_preview__"; // プレビューの文字の画像の登録名
constexpr const char *kWhiteTexture = "debug/white1x1.png";   // 本体の画像が無いときの白
constexpr float kListWidth = 200.0f;                          // 一覧の幅
constexpr float kSettingsWidth = 380.0f;                      // 設定欄の幅

ImVec2 ToImVec2(const Vector2 &v) { return {v.x, v.y}; }

/// <summary>
/// 列挙をコンボで選択
/// </summary>
template <typename Enum>
bool EnumCombo(const char *label, Enum &value, const char *items)
{
    int index = static_cast<int>(value);
    if (ImGui::Combo(label, &index, items))
    {
        value = static_cast<Enum>(index);
        return true;
    }
    return false;
}

/// <summary>
/// 画像の欄（アセットブラウザからドロップ）
/// </summary>
bool ImageSlot(const char *id, const char *label, std::string &relativePath)
{
    bool changed = false;
    ImGui::PushID(id);
    D3D12_GPU_DESCRIPTOR_HANDLE handle{};
    if (!relativePath.empty())
    {
        TextureManager::GetInstance()->LoadTexture(relativePath);
        handle = TextureManager::GetInstance()->GetSrvHandleGPU(AssetPath::Image(relativePath));
    }
    if (handle.ptr != 0)
    {
        ImGui::Image(static_cast<ImTextureID>(handle.ptr), ImVec2(40.0f, 40.0f));
    }
    else
    {
        ImGui::Button("##empty", ImVec2(40.0f, 40.0f));
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
    ImGui::TextDisabled("%s", relativePath.empty() ? "未設定" : relativePath.c_str());
    if (!relativePath.empty() && ImGui::SmallButton("外す"))
    {
        relativePath.clear();
        changed = true;
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

/// <summary>
/// 名前の一覧から1つを選ぶコンボ
/// </summary>
bool NameCombo(const char *label, std::string &value, const std::vector<std::string> &names)
{
    bool changed = false;
    if (ImGui::BeginCombo(label, value.empty() ? "未設定" : value.c_str()))
    {
        for (const std::string &name : names)
        {
            if (ImGui::Selectable(name.c_str(), name == value))
            {
                value = name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/// <summary>
/// 名前を確定時に変更（重なれば番号を付ける）
/// </summary>
template <typename MakeUnique>
bool RenameField(std::string &name, MakeUnique makeUnique)
{
    std::string edited = name;
    if (ImGui::InputText("名前", &edited, ImGuiInputTextFlags_EnterReturnsTrue) && !edited.empty() && edited != name)
    {
        name = makeUnique(edited);
        return true;
    }
    return false;
}

/// <summary>
/// 追加・複製・削除のボタンと名前の一覧
/// </summary>
/// <returns>int: 押したボタン（0 なし / 1 追加 / 2 複製 / 3 削除）</returns>
template <typename Item>
int DrawNameList(const char *id, std::vector<Item> &items, int &selected)
{
    int action = 0;
    if (ImGui::Button("追加"))
    {
        action = 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("複製"))
    {
        action = 2;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(items.size() <= 1);
    if (ImGui::Button("削除"))
    {
        action = 3;
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::PushID(id);
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
    {
        ImGui::PushID(i);
        if (ImGui::Selectable(items[static_cast<size_t>(i)].name.c_str(), selected == i))
        {
            selected = i;
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    return action;
}

/// <summary>
/// 四角形をドローリストへ描画
/// </summary>
void DrawQuads(ImDrawList *drawList, const std::vector<BubbleQuad> &quads, D3D12_GPU_DESCRIPTOR_HANDLE texture,
               const ImVec2 &center, float scale)
{
    if (texture.ptr == 0)
    {
        return;
    }
    const ImTextureID id = static_cast<ImTextureID>(texture.ptr);
    auto toScreen = [&](const Vector2 &p) { return ImVec2(center.x + p.x * scale, center.y + p.y * scale); };
    for (const BubbleQuad &quad : quads)
    {
        const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(quad.color.x, quad.color.y, quad.color.z, quad.color.w));
        drawList->AddImageQuad(id, toScreen(quad.position[0]), toScreen(quad.position[1]), toScreen(quad.position[2]),
                               toScreen(quad.position[3]), ToImVec2(quad.uv[0]), ToImVec2(quad.uv[1]),
                               ToImVec2(quad.uv[2]), ToImVec2(quad.uv[3]), color);
    }
}

} // namespace
#endif // USE_IMGUI

void BubbleManager::DrawEditor()
{
#ifdef USE_IMGUI
    BubbleLibrary &library = GetLibrary();
    previewTime_ += ImGui::GetIO().DeltaTime;

    // 保存・読み直し
    if (ImGui::Button(dirty_ ? "保存 *" : "保存"))
    {
        if (library.Save())
        {
            dirty_ = false;
            ImGuiNotification::Post("吹き出しを保存しました: " + BubbleLibrary::FilePath());
        }
        else
        {
            ImGuiNotification::Post("吹き出しを保存できませんでした", {0.85f, 0.42f, 0.42f, 1.0f});
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("読み直す"))
    {
        library.Load();
        dirty_ = false;
        previewSignature_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("表示中を全部消す"))
    {
        Clear();
    }

    if (ImGui::BeginTabBar("##bubbleTabs"))
    {
        if (ImGui::BeginTabItem("出し方"))
        {
            DrawInstanceTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("スタイル"))
        {
            DrawStyleTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
#endif // USE_IMGUI
}

#ifdef USE_IMGUI
void BubbleManager::DrawInstanceTab()
{
    BubbleLibrary &library = GetLibrary();
    selectedInstance_ = std::clamp(selectedInstance_, 0, static_cast<int>(library.instances.size()) - 1);

    if (ImGui::BeginChild("##instanceList", ImVec2(kListWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX))
    {
        const int action = DrawNameList("instances", library.instances, selectedInstance_);
        if (action == 1)
        {
            ImGui::OpenPopup("##newInstance");
        }
        else if (action == 2)
        {
            BubbleInstance copy = library.instances[static_cast<size_t>(selectedInstance_)];
            copy.name = library.MakeUniqueInstanceName(copy.name);
            library.instances.insert(library.instances.begin() + selectedInstance_ + 1, copy);
            ++selectedInstance_;
            MarkDirty();
        }
        else if (action == 3 && library.instances.size() > 1)
        {
            library.instances.erase(library.instances.begin() + selectedInstance_);
            selectedInstance_ = (std::min)(selectedInstance_, static_cast<int>(library.instances.size()) - 1);
            MarkDirty();
        }

        // 空から・ひな形から追加
        if (ImGui::BeginPopup("##newInstance"))
        {
            std::optional<BubbleInstance> added;
            if (ImGui::MenuItem("空から"))
            {
                BubbleInstance instance;
                instance.name = "NewBubble";
                instance.style = library.styles.empty() ? std::string() : library.styles.front().name;
                added = instance;
            }
            ImGui::SeparatorText("ひな形から");
            for (const BubbleInstance &builtIn : BubbleLibrary::BuiltInInstances())
            {
                if (ImGui::MenuItem(builtIn.name.c_str()))
                {
                    added = builtIn;
                }
            }
            if (added)
            {
                // 使うスタイルが無ければひな形から追加
                if (!library.FindStyle(added->style))
                {
                    for (const BubbleStyle &style : BubbleLibrary::BuiltInStyles())
                    {
                        if (style.name == added->style)
                        {
                            library.styles.push_back(style);
                        }
                    }
                }
                added->name = library.MakeUniqueInstanceName(added->name);
                library.instances.push_back(*added);
                selectedInstance_ = static_cast<int>(library.instances.size()) - 1;
                previewTime_ = 0.0f;
                MarkDirty();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##instanceDetail", ImVec2(0.0f, 0.0f)))
    {
        DrawInstanceDetails(library.instances[static_cast<size_t>(selectedInstance_)]);
    }
    ImGui::EndChild();
}

void BubbleManager::DrawStyleTab()
{
    BubbleLibrary &library = GetLibrary();
    selectedStyle_ = std::clamp(selectedStyle_, 0, static_cast<int>(library.styles.size()) - 1);

    if (ImGui::BeginChild("##styleList", ImVec2(kListWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX))
    {
        const int action = DrawNameList("styles", library.styles, selectedStyle_);
        if (action == 1)
        {
            ImGui::OpenPopup("##newStyle");
        }
        else if (action == 2)
        {
            BubbleStyle copy = library.styles[static_cast<size_t>(selectedStyle_)];
            copy.name = library.MakeUniqueStyleName(copy.name);
            library.styles.insert(library.styles.begin() + selectedStyle_ + 1, copy);
            ++selectedStyle_;
            MarkDirty();
        }
        else if (action == 3 && library.styles.size() > 1)
        {
            library.styles.erase(library.styles.begin() + selectedStyle_);
            selectedStyle_ = (std::min)(selectedStyle_, static_cast<int>(library.styles.size()) - 1);
            MarkDirty();
        }

        // 空から・ひな形から追加
        if (ImGui::BeginPopup("##newStyle"))
        {
            std::optional<BubbleStyle> added;
            if (ImGui::MenuItem("空から"))
            {
                BubbleStyle style;
                style.name = "NewStyle";
                added = style;
            }
            ImGui::SeparatorText("ひな形から");
            for (const BubbleStyle &builtIn : BubbleLibrary::BuiltInStyles())
            {
                if (ImGui::MenuItem(builtIn.name.c_str()))
                {
                    added = builtIn;
                }
            }
            if (added)
            {
                added->name = library.MakeUniqueStyleName(added->name);
                library.styles.push_back(*added);
                selectedStyle_ = static_cast<int>(library.styles.size()) - 1;
                MarkDirty();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##styleDetail", ImVec2(0.0f, 0.0f)))
    {
        DrawStyleDetails(library.styles[static_cast<size_t>(selectedStyle_)]);
    }
    ImGui::EndChild();
}

void BubbleManager::DrawInstanceDetails(BubbleInstance &instance)
{
    BubbleLibrary &library = GetLibrary();
    bool changed = false;

    // 設定
    if (ImGui::BeginChild("##instanceSettings", ImVec2(kSettingsWidth, 0.0f), ImGuiChildFlags_ResizeX))
    {
        ImGui::PushItemWidth(-110.0f);
        changed |= RenameField(instance.name, [&](const std::string &name) { return library.MakeUniqueInstanceName(name); });
        std::vector<std::string> styleNames;
        for (const BubbleStyle &style : library.styles)
        {
            styleNames.push_back(style.name);
        }
        changed |= NameCombo("スタイル", instance.style, styleNames);

        // 文字
        ImGui::SeparatorText("文字");
        ImGui::PushItemWidth(-1.0f);
        changed |= ImGui::InputTextMultiline("##text", &instance.text, ImVec2(-1.0f, ImGui::GetTextLineHeight() * 7.0f));
        ImGui::PopItemWidth();

        // 位置
        ImGui::SeparatorText("位置");
        changed |= EnumCombo("追従先", instance.targetKind, kTargetNames);
        switch (instance.targetKind)
        {
        case BubbleTargetKind::Object:
            changed |= NameCombo("オブジェクト", instance.target, BaseObjectManager::GetInstance()->GetSortedObjectNames());
            changed |= ImGui::DragFloat3("ずれ（ワールド）", &instance.worldOffset.x, 0.05f);
            break;
        case BubbleTargetKind::Sprite:
        {
            std::vector<std::string> spriteNames;
            for (SpriteData *sprite : SpriteManager::GetInstance()->GetAllSprites())
            {
                spriteNames.push_back(sprite->name);
            }
            changed |= NameCombo("スプライト", instance.target, spriteNames);
            break;
        }
        default:
            changed |= ImGui::DragFloat2("画面の位置", &instance.screenPosition.x, 1.0f, 0.0f, 0.0f, "%.0f");
            break;
        }
        changed |= ImGui::DragFloat2("画面上のずれ", &instance.offset.x, 1.0f, 0.0f, 0.0f, "%.0f");
        changed |= ImGui::SliderFloat2("基準点", &instance.pivot.x, 0.0f, 1.0f, "%.2f");

        // しっぽ
        ImGui::SeparatorText("しっぽ");
        changed |= ImGui::Checkbox("出す", &instance.showTail);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("左右反転", &instance.tailFlip);
        changed |= ImGui::SliderFloat("位置", &instance.tailPosition, 0.0f, 1.0f, "%.3f");

        // 出方
        ImGui::SeparatorText("出方");
        changed |= EnumCombo("出方", instance.appear, kAppearNames);
        changed |= ImGui::DragFloat("出る時間", &instance.appearDuration, 0.01f, 0.0f, 5.0f, "%.2f 秒");
        int easing = static_cast<int>(instance.appearEasing);
        if (ImGui::Combo("イージング", &easing, kEasingNames, kEasingCount))
        {
            instance.appearEasing = static_cast<EasingType>(easing);
            changed = true;
        }
        changed |= ImGui::DragFloat("消える時間", &instance.disappearDuration, 0.01f, 0.0f, 5.0f, "%.2f 秒");

        // 文字送り
        ImGui::SeparatorText("文字送り");
        changed |= ImGui::DragFloat("1文字の間隔", &instance.charInterval, 0.002f, 0.0f, 1.0f, "%.3f 秒");
        changed |= ImGui::DragFloat("句読点の待ち", &instance.punctuationPause, 0.01f, 0.0f, 3.0f, "%.2f 秒");
        changed |= EnumCombo("1文字の出方", instance.charAppear, kCharAppearNames);
        ImGui::BeginDisabled(instance.charAppear == BubbleCharAppear::Instant);
        changed |= ImGui::DragFloat("1文字の時間", &instance.charDuration, 0.01f, 0.0f, 2.0f, "%.2f 秒");
        ImGui::EndDisabled();

        // 表示中の動き
        ImGui::SeparatorText("表示中の動き");
        BubbleMotion &motion = instance.motion;
        changed |= ImGui::SliderFloat("文字の震え", &motion.textShake, 0.0f, 10.0f, "%.1f px");
        changed |= ImGui::SliderFloat("文字の波", &motion.textWave, 0.0f, 20.0f, "%.1f px");
        changed |= ImGui::SliderFloat("文字の揺れ", &motion.textSway, 0.0f, 45.0f, "%.1f 度");
        changed |= ImGui::SliderFloat("文字化け", &motion.textGlitch, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("全体の震え", &motion.bodyShake, 0.0f, 20.0f, "%.1f px");
        changed |= ImGui::SliderFloat("全体の揺れ", &motion.bodySway, 0.0f, 20.0f, "%.1f 度");
        changed |= ImGui::SliderFloat("速さ", &motion.speed, 0.0f, 4.0f, "%.2f");

        // 消えるまで
        ImGui::SeparatorText("消えるまで");
        bool autoClose = instance.lifeTime >= 0.0f;
        if (ImGui::Checkbox("自動で消す", &autoClose))
        {
            instance.lifeTime = autoClose ? 2.0f : -1.0f;
            changed = true;
        }
        if (autoClose)
        {
            changed |= ImGui::DragFloat("待ち時間", &instance.lifeTime, 0.05f, 0.0f, 60.0f, "%.2f 秒");
        }

        // ゲームで試す
        ImGui::SeparatorText("ゲームで試す");
        if (ImGui::Button("表示"))
        {
            lastShownId_ = Show(instance);
        }
        ImGui::SameLine();
        if (ImGui::Button("送る"))
        {
            Advance(lastShownId_);
        }
        ImGui::SameLine();
        if (ImGui::Button("閉じる"))
        {
            Close(lastShownId_);
        }
        ImGui::PopItemWidth();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // プレビュー
    if (ImGui::BeginChild("##instancePreview", ImVec2(0.0f, 0.0f)))
    {
        if (ImGui::Button("再生"))
        {
            previewTime_ = 0.0f;
        }
        const BubbleStyle *style = library.FindStyle(instance.style);
        const BubbleStyle &previewStyle = style ? *style : library.styles.front();
        changed |= DrawPreview(previewStyle, instance, instance.tailPosition, false);
    }
    ImGui::EndChild();

    if (changed)
    {
        MarkDirty();
    }
}

void BubbleManager::DrawStyleDetails(BubbleStyle &style)
{
    BubbleLibrary &library = GetLibrary();
    bool changed = false;

    // 設定
    if (ImGui::BeginChild("##styleSettings", ImVec2(kSettingsWidth, 0.0f), ImGuiChildFlags_ResizeX))
    {
        ImGui::PushItemWidth(-110.0f);
        const std::string oldName = style.name;
        if (RenameField(style.name, [&](const std::string &name) { return library.MakeUniqueStyleName(name); }))
        {
            for (BubbleInstance &instance : library.instances)
            {
                if (instance.style == oldName)
                {
                    instance.style = style.name;
                }
            }
            changed = true;
        }

        // 本体
        ImGui::SeparatorText("本体");
        changed |= ImageSlot("body", "画像", style.bodyTexture);
        changed |= ImGui::ColorEdit4("色##body", &style.bodyColor.x);
        changed |= ImGui::DragFloat4("9スライス", &style.sliceBorder.x, 0.5f, 0.0f, 1024.0f, "%.0f");
        changed |= EnumCombo("辺", style.edgeMode, kEdgeNames);
        changed |= DrawSliceEditor(style);

        // しっぽ
        ImGui::SeparatorText("しっぽ");
        changed |= ImageSlot("tail", "画像", style.tailTexture);
        changed |= DrawTailPivotEditor(style);
        changed |= ImGui::DragFloat2("付け根", &style.tailPivot.x, 0.5f, 0.0f, 0.0f, "%.0f");
        changed |= ImGui::DragFloat("重ねる量", &style.tailOverlap, 0.1f, -64.0f, 64.0f, "%.1f px");
        changed |= ImGui::DragFloat("角の丸み", &style.cornerRadius, 0.5f, 0.0f, 512.0f, "%.1f px");

        // 文字
        ImGui::SeparatorText("文字");
        if (!fontListLoaded_)
        {
            fontFiles_ = BubbleText::FontFiles();
            fontListLoaded_ = true;
        }
        changed |= NameCombo("フォント", style.font, fontFiles_);
        changed |= ImGui::DragFloat("大きさ", &style.fontSize, 0.5f, 4.0f, 256.0f, "%.1f px");
        changed |= ImGui::ColorEdit4("色##text", &style.textColor.x);
        changed |= ImGui::DragFloat("行の間隔", &style.lineSpacing, 0.01f, 0.5f, 4.0f, "%.2f");
        changed |= ImGui::DragFloat("字間", &style.letterSpacing, 0.1f, -32.0f, 64.0f, "%.1f px");
        changed |= EnumCombo("揃え", style.align, kAlignNames);
        changed |= ImGui::DragFloat4("余白", &style.textPadding.x, 0.5f, 0.0f, 512.0f, "%.0f");

        // 大きさ
        ImGui::SeparatorText("大きさ");
        changed |= ImGui::DragFloat("最小幅", &style.minWidth, 1.0f, 0.0f, 4096.0f, "%.0f px");
        changed |= ImGui::DragFloat("最大幅", &style.maxWidth, 1.0f, 0.0f, 4096.0f, "%.0f px");

        // 折り返し
        ImGui::SeparatorText("折り返し");
        changed |= EnumCombo("折り返し", style.wrapMode, kWrapNames);
        ImGui::BeginDisabled(style.wrapMode != BubbleWrapMode::Width);
        changed |= ImGui::DragFloat("幅", &style.wrapWidth, 1.0f, 0.0f, 4096.0f, style.wrapWidth > 0.0f ? "%.0f px" : "自動");
        ImGui::EndDisabled();
        changed |= ImGui::Checkbox("禁則処理", &style.kinsoku);
        ImGui::PopItemWidth();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // プレビュー
    if (ImGui::BeginChild("##stylePreview", ImVec2(0.0f, 0.0f)))
    {
        ImGui::InputTextMultiline("##sample", &sampleText_, ImVec2(-1.0f, ImGui::GetTextLineHeight() * 4.0f));
        BubbleInstance sample;
        sample.text = sampleText_;
        sample.appear = BubbleAppear::None;
        sample.charInterval = 0.0f;
        sample.tailPosition = sampleTailPosition_;
        DrawPreview(style, sample, sampleTailPosition_, true);
    }
    ImGui::EndChild();

    if (changed)
    {
        MarkDirty();
    }
}

bool BubbleManager::DrawPreview(const BubbleStyle &style, const BubbleInstance &instance, float &tailPosition, bool showGuides)
{
    // 文字の画像（入力が変わったときだけ作り直し）
    const bool withGarble = BubbleText::UsesGarble(instance);
    const std::string signature = instance.text + '\x1f' + nlohmann::json(style).dump() + (withGarble ? "g" : "");
    if (signature != previewSignature_)
    {
        previewSignature_ = signature;
        previewText_ = BubbleText::Build(instance.text, style, withGarble);
        previewTextHandle_ = TextureManager::GetInstance()->UpdateDynamicTexture(
            kPreviewTextKey, previewText_.pixels.data(), previewText_.imageWidth, previewText_.imageHeight);
    }

    // 本体としっぽの画像
    D3D12_GPU_DESCRIPTOR_HANDLE bodyHandle{};
    Vector2 bodySize;
    ResolveTexture(style.bodyTexture.empty() ? kWhiteTexture : style.bodyTexture, bodyHandle, bodySize);
    D3D12_GPU_DESCRIPTOR_HANDLE tailHandle{};
    Vector2 tailSize = {0.0f, 0.0f};
    if (!ResolveTexture(style.tailTexture, tailHandle, tailSize))
    {
        tailSize = {0.0f, 0.0f};
    }

    // 再生位置から出方と文字送りを算出
    float appearProgress = 1.0f;
    float appearTime = 0.0f;
    if (instance.appear != BubbleAppear::None && instance.appearDuration > 0.0f)
    {
        appearTime = instance.appearDuration;
        const float t = std::clamp(previewTime_ / instance.appearDuration, 0.0f, 1.0f);
        appearProgress = ApplyEasing(instance.appearEasing, 0.0f, 1.0f, t, 1.0f);
    }
    std::vector<float> revealTimes;
    float typingEnd = 0.0f;
    BuildRevealTimes(instance, previewText_, revealTimes, typingEnd);
    std::vector<float> charProgress;
    UpdateCharProgress(instance, revealTimes, (std::max)(previewTime_ - appearTime, 0.0f), charProgress);

    // 本体が中央に来るように配置
    const Vector2 size = BubbleGeometry::BodySize(style, previewText_);
    BubbleShapeParams params;
    params.pivot = instance.pivot;
    params.anchor = {(instance.pivot.x - 0.5f) * size.x, (instance.pivot.y - 0.5f) * size.y};
    params.bodyTextureSize = bodySize;
    params.tailTextureSize = tailSize;
    params.showTail = instance.showTail;
    params.tailPosition = tailPosition;
    params.tailFlip = instance.tailFlip;
    params.appear = instance.appear;
    params.appearProgress = appearProgress;
    params.charAppear = instance.charAppear;
    params.charProgress = &charProgress;
    params.motion = &instance.motion;
    params.time = previewTime_;
    const BubbleMesh mesh = BubbleGeometry::Build(style, previewText_, params);

    // 表示する場所と倍率
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 region = ImGui::GetContentRegionAvail();
    region.x = (std::max)(region.x, 64.0f);
    region.y = (std::max)(region.y, 160.0f);
    const float margin = 16.0f + (std::max)(tailSize.x, tailSize.y);
    const float scale = (std::min)({1.0f, (region.x - 2.0f * margin) / (std::max)(size.x, 1.0f),
                                    (region.y - 2.0f * margin) / (std::max)(size.y, 1.0f)});
    const float drawScale = (std::max)(scale, 0.05f);
    const ImVec2 center = {origin.x + region.x * 0.5f, origin.y + region.y * 0.5f};

    ImGui::InvisibleButton("##previewArea", region);
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(origin, {origin.x + region.x, origin.y + region.y}, true);
    drawList->AddRectFilled(origin, {origin.x + region.x, origin.y + region.y}, IM_COL32(58, 60, 66, 255));

    // 本体・しっぽ・文字
    DrawQuads(drawList, mesh.body, bodyHandle, center, drawScale);
    DrawQuads(drawList, mesh.tail, tailHandle, center, drawScale);
    DrawQuads(drawList, mesh.text, previewTextHandle_, center, drawScale);

    // 文字の領域と本体の枠
    auto toScreen = [&](const Vector2 &p) { return ImVec2(center.x + p.x * drawScale, center.y + p.y * drawScale); };
    if (showGuides)
    {
        drawList->AddRect(toScreen(mesh.bodyMin), toScreen(mesh.bodyMax), IM_COL32(255, 255, 255, 70));
        drawList->AddRect(toScreen(mesh.textMin), toScreen(mesh.textMax), IM_COL32(120, 200, 255, 160));
    }

    // しっぽの位置をドラッグで移動
    bool changed = false;
    if (ImGui::IsItemActivated())
    {
        draggingTail_ = true;
    }
    if (!ImGui::IsItemActive())
    {
        draggingTail_ = false;
    }
    if (draggingTail_)
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const Vector2 local = {(mouse.x - center.x) / drawScale, (mouse.y - center.y) / drawScale};
        const float radius = BubbleGeometry::CornerRadius(style, mesh.bodyMin, mesh.bodyMax);
        const float t = BubbleGeometry::NearestPerimeter(mesh.bodyMin, mesh.bodyMax, radius, local);
        if (std::abs(t - tailPosition) > 1e-4f)
        {
            tailPosition = t;
            changed = true;
        }
    }
    if (instance.showTail)
    {
        drawList->AddCircle(toScreen(mesh.tailPoint), 5.0f, draggingTail_ ? IM_COL32(255, 210, 90, 255) : IM_COL32(255, 210, 90, 140), 0, 2.0f);
    }
    drawList->PopClipRect();
    return changed;
}

bool BubbleManager::DrawSliceEditor(BubbleStyle &style)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle{};
    Vector2 size;
    if (!ResolveTexture(style.bodyTexture, handle, size) || size.x <= 0.0f || size.y <= 0.0f)
    {
        return false;
    }

    // 画像を枠に収まる倍率で表示
    const float maxWidth = (std::max)(ImGui::GetContentRegionAvail().x, 64.0f);
    const float scale = (std::min)({maxWidth / size.x, 200.0f / size.y, 4.0f});
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 extent = {size.x * scale, size.y * scale};
    ImGui::InvisibleButton("##sliceArea", extent);
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + extent.x, origin.y + extent.y}, IM_COL32(40, 40, 44, 255));
    drawList->AddImage(static_cast<ImTextureID>(handle.ptr), origin, {origin.x + extent.x, origin.y + extent.y});

    // 4本の境界線（左・上・右・下）
    const float lines[4] = {
        origin.x + style.sliceBorder.x * scale,
        origin.y + style.sliceBorder.y * scale,
        origin.x + (size.x - style.sliceBorder.z) * scale,
        origin.y + (size.y - style.sliceBorder.w) * scale};
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (ImGui::IsItemActivated())
    {
        float best = 8.0f;
        sliceDragLine_ = -1;
        for (int i = 0; i < 4; ++i)
        {
            const float distance = std::abs((i % 2 == 0 ? mouse.x : mouse.y) - lines[i]);
            if (distance < best)
            {
                best = distance;
                sliceDragLine_ = i;
            }
        }
    }
    if (!ImGui::IsItemActive())
    {
        sliceDragLine_ = -1;
    }

    bool changed = false;
    if (sliceDragLine_ >= 0)
    {
        const float x = std::clamp((mouse.x - origin.x) / scale, 0.0f, size.x);
        const float y = std::clamp((mouse.y - origin.y) / scale, 0.0f, size.y);
        float *border = &style.sliceBorder.x;
        const float value = sliceDragLine_ == 0 ? x : sliceDragLine_ == 1 ? y : sliceDragLine_ == 2 ? size.x - x : size.y - y;
        const float rounded = std::round(value);
        if (rounded != border[sliceDragLine_])
        {
            border[sliceDragLine_] = rounded;
            changed = true;
        }
    }
    for (int i = 0; i < 4; ++i)
    {
        const ImU32 color = sliceDragLine_ == i ? IM_COL32(255, 210, 90, 255) : IM_COL32(90, 200, 255, 200);
        if (i % 2 == 0)
        {
            drawList->AddLine({lines[i], origin.y}, {lines[i], origin.y + extent.y}, color, 1.5f);
        }
        else
        {
            drawList->AddLine({origin.x, lines[i]}, {origin.x + extent.x, lines[i]}, color, 1.5f);
        }
    }
    return changed;
}

bool BubbleManager::DrawTailPivotEditor(BubbleStyle &style)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle{};
    Vector2 size;
    if (!ResolveTexture(style.tailTexture, handle, size) || size.x <= 0.0f || size.y <= 0.0f)
    {
        return false;
    }

    // 画像を枠に収まる倍率で表示
    const float maxWidth = (std::max)(ImGui::GetContentRegionAvail().x, 64.0f);
    const float scale = (std::min)({maxWidth / size.x, 160.0f / size.y, 4.0f});
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 extent = {size.x * scale, size.y * scale};
    ImGui::InvisibleButton("##pivotArea", extent);
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + extent.x, origin.y + extent.y}, IM_COL32(40, 40, 44, 255));
    drawList->AddImage(static_cast<ImTextureID>(handle.ptr), origin, {origin.x + extent.x, origin.y + extent.y});

    // クリックした所を付け根に
    bool changed = false;
    if (ImGui::IsItemActive())
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const Vector2 pivot = {std::round(std::clamp((mouse.x - origin.x) / scale, 0.0f, size.x)),
                               std::round(std::clamp((mouse.y - origin.y) / scale, 0.0f, size.y))};
        if (pivot.x != style.tailPivot.x || pivot.y != style.tailPivot.y)
        {
            style.tailPivot = pivot;
            changed = true;
        }
    }
    const ImVec2 marker = {origin.x + style.tailPivot.x * scale, origin.y + style.tailPivot.y * scale};
    drawList->AddLine({marker.x - 6.0f, marker.y}, {marker.x + 6.0f, marker.y}, IM_COL32(255, 210, 90, 255), 2.0f);
    drawList->AddLine({marker.x, marker.y - 6.0f}, {marker.x, marker.y + 6.0f}, IM_COL32(255, 210, 90, 255), 2.0f);
    return changed;
}
#endif // USE_IMGUI

} // namespace Hagine
