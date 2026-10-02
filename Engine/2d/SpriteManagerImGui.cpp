#define NOMINMAX
#include "SpriteManager.h"
#include "WinApp.h"
#include <asset/AssetPath.h>
#include <filesystem>
#include <format>
#include <graphics/texture/TextureManager.h>
#include <icon/IconsFontAwesome5.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#ifdef USE_IMGUI
#include "utility/debug/imgui/ImGuizmoManager.h"
// DebugUIHelper.h は ImGui:: を使うので imgui.h（ImGuizmoManager.h 経由）の後に include する
#include "utility/debug/imgui/AssetDragDrop.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include <browser/ShowFolder.h>
#include <edit/undo/ImGuiUndoTracker.h>
#endif // USE_IMGUI

// スプライトのエディタUI（生成ダイアログ・スプライトマネージャ窓）。
// 本体（登録・描画・保存）は SpriteManager.cpp にある。

#ifdef USE_IMGUI
namespace {
// UI の編集ジェスチャを Undo 履歴へ積むトラッカー。シングルトンなので1つでよい。
// ヘッダーのメンバーにすると Undo 関連のヘッダーが 100 本以上の .cpp へ広がるので、ここに置く
Hagine::ImGuiUndoTracker g_undoTracker;
} // namespace

namespace Hagine {
namespace {

constexpr float kListThumbSize = 18.0f;    // 一覧の行に出すサムネイル
constexpr float kDetailThumbSize = 72.0f;  // 詳細の見出しに出すサムネイル
constexpr float kPreviewThumbSize = 168.0f; // 生成ダイアログのプレビュー

/// <summary>画像のサムネイル用の ID（読めなければ 0）</summary>
ImTextureID TextureThumb(const std::string &relPath)
{
    if (relPath.empty())
    {
        return 0;
    }
    TextureManager *textureManager = TextureManager::GetInstance();
    textureManager->LoadTexture(relPath); // 読み込み済みならすぐ戻る
    return static_cast<ImTextureID>(textureManager->GetSrvHandleGPU(AssetPath::Image(relPath)).ptr);
}

/// <summary>画像のピクセルサイズ（読めなければ 0）</summary>
Vector2 TexturePixelSize(const std::string &relPath)
{
    if (relPath.empty())
    {
        return {0.0f, 0.0f};
    }
    TextureManager::GetInstance()->LoadTexture(relPath);
    const DirectX::TexMetadata &metadata = TextureManager::GetInstance()->GetMetaData(relPath);
    return {static_cast<float>(metadata.width), static_cast<float>(metadata.height)};
}

/// <summary>
/// 縦横比を保ったまま box 四方の枠へ画像を描く。枠そのものが1つのアイテムになるので、
/// 直後に ドロップ先・ツールチップ を付けられる
/// </summary>
/// <returns>bool: 枠がクリックされたら true</returns>
bool FittedImageBox(const char *id, ImTextureID texture, Vector2 pixelSize, float box)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(box, box));
    const ImVec2 max(min.x + box, min.y + box);
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    // 透明部分が分かるよう、暗い市松模様を敷く
    const float cell = box / 8.0f;
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const ImU32 color = ((x + y) % 2 == 0) ? IM_COL32(48, 48, 54, 255) : IM_COL32(36, 36, 40, 255);
            drawList->AddRectFilled(ImVec2(min.x + cell * x, min.y + cell * y),
                                    ImVec2(min.x + cell * (x + 1), min.y + cell * (y + 1)), color);
        }
    }

    if (texture != 0 && pixelSize.x > 0.0f && pixelSize.y > 0.0f)
    {
        const float scale = (std::min)(box / pixelSize.x, box / pixelSize.y);
        const ImVec2 size(pixelSize.x * scale, pixelSize.y * scale);
        const ImVec2 imageMin(min.x + (box - size.x) * 0.5f, min.y + (box - size.y) * 0.5f);
        drawList->AddImage(texture, imageMin, ImVec2(imageMin.x + size.x, imageMin.y + size.y));
    }
    else
    {
        const char *text = "画像なし";
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        drawList->AddText(ImVec2(min.x + (box - textSize.x) * 0.5f, min.y + (box - textSize.y) * 0.5f),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    }

    const bool hovered = ImGui::IsItemHovered();
    drawList->AddRect(min, max, ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Border), 3.0f);
    return clicked;
}

/// <summary>左にラベル、右に入力欄を並べる行の頭（入力欄の幅は残り全部）</summary>
void RowLabel(const char *label)
{
    const float labelX = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(labelX + LabelColumnWidth());
    ImGui::SetNextItemWidth(-FLT_MIN);
}

/// <summary>
/// 3×3 のマスで「左上・中央・右下…」を選ぶ部品（基準点・画面寄せで使う）
/// </summary>
/// <param name="id">ID</param>
/// <param name="current">今の値（一致するマスを光らせる。不要なら nullptr）</param>
/// <param name="outValue">押されたマスの値（各成分 0 / 0.5 / 1）</param>
/// <param name="tooltipVerb">ツールチップの言い回し（例: "に揃える"）</param>
/// <returns>bool: どれかが押されたら true</returns>
bool NineGrid(const char *id, const Vector2 *current, Vector2 &outValue, const char *tooltipVerb)
{
    static const char *const kNames[3][3] = {{"左上", "上", "右上"}, {"左", "中央", "右"}, {"左下", "下", "右下"}};
    const float cell = ImGui::GetFrameHeight() * 0.8f;
    bool pressed = false;

    ImGui::PushID(id);
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    // 暗い地に暗いマスだと見えないので、枠線で区切る
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            const Vector2 value = {column * 0.5f, row * 0.5f};
            const bool selected = current && std::abs(current->x - value.x) < 0.001f && std::abs(current->y - value.y) < 0.001f;
            if (column > 0)
            {
                ImGui::SameLine();
            }
            ImGui::PushID(row * 3 + column);
            ImGui::PushStyleColor(ImGuiCol_Button, selected ? DebugTheme::kAccentBlue : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            if (ImGui::Button("##cell", ImVec2(cell, cell)))
            {
                outValue = value;
                pressed = true;
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s%s", kNames[row][column], tooltipVerb);
            ImGui::PopID();
        }
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImGui::EndGroup();
    ImGui::PopID();
    return pressed;
}

/// <summary>
/// 見た目の矩形を画面の端・中央へ寄せたときの位置（回転は考えない）。
/// 位置は基準点の場所なので、基準点と大きさから逆算する
/// </summary>
Vector2 AlignedPosition(const Vector2 &align, const Vector2 &anchor, const Vector2 &visibleSize)
{
    const float screenW = static_cast<float>(WinApp::GetVirtualWidth());
    const float screenH = static_cast<float>(WinApp::GetVirtualHeight());
    return {align.x * screenW + (anchor.x - align.x) * visibleSize.x,
            align.y * screenH + (anchor.y - align.y) * visibleSize.y};
}

/// <summary>ファイル名に使えない文字が入っているか（スプライト名はそのまま保存ファイル名になる）</summary>
bool HasForbiddenFileChar(const std::string &name)
{
    return name.find_first_of("\\/:*?\"<>|") != std::string::npos;
}

/// <summary>画像のパスから名前の元（拡張子なしのファイル名）を取り出す</summary>
std::string StemOf(const std::string &relPath)
{
    const std::u8string stem = std::filesystem::path(std::u8string(relPath.begin(), relPath.end())).stem().u8string();
    return std::string(stem.begin(), stem.end());
}

const char *const kBlendModeNames[] = {"なし", "通常", "加算", "減算", "乗算", "スクリーン"};
} // namespace

// ============================================================
//  生成
// ============================================================

void SpriteManager::RegisterSpriteFromEditor(const std::string &name, const std::string &texturePath,
                                             const SpriteTransform &transform)
{
    // 生成を1つの操作として Undo 履歴へ積む（前後の差分）
    const nlohmann::json before = CaptureUndoState();
    RegisterSprite(name, texturePath, transform);
    const nlohmann::json after = CaptureUndoState();
    auto [diffBefore, diffAfter] = MakeTopLevelJsonDiff(before, after);
    UndoRedoManager::GetInstance()->Push(std::make_unique<JsonStateCommand>(
        "スプライト作成: " + name, std::move(diffBefore), std::move(diffAfter),
        [](const nlohmann::json &s) { SpriteManager::GetInstance()->RestoreUndoState(s); }));
    // マネージャ窓のトラッカーが同じ差分をもう一度積まないようにする
    g_undoTracker.SkipCurrentGesture();

    // 作った物をそのまま触れるよう、一覧とシーンの両方で選んでおく
    selectedName_ = name;
    lastGizmoPick_ = name;
    ImGuizmoManager::GetInstance()->SelectOnly(name);
}

void SpriteManager::PlaceSpriteFromEditor(const std::string &texturePath, const Vector2 &position)
{
    if (texturePath.empty())
    {
        return;
    }
    SpriteTransform transform;
    transform.position = position;
    // 置いた点が画像の中心になるようにする（左上だと、落とした所から右下へずれて見える）
    transform.anchorPoint = {0.5f, 0.5f};
    RegisterSpriteFromEditor(MakeUniqueSpriteName(StemOf(texturePath)), texturePath, transform);
}

void SpriteManager::DrawSpriteCreationModal()
{
    constexpr const char *kPopupId = "スプライトを作る##spriteCreate";
    if (showSpriteCreationModal_)
    {
        showSpriteCreationModal_ = false;
        if (creationNeedsReset_)
        {
            // 置き場所の既定は画面の真ん中・基準点は中央（左上・原点だと画面の隅に出て見落とす）
            creation_ = CreationForm{};
            creation_.transform.position = {WinApp::GetVirtualWidth() * 0.5f, WinApp::GetVirtualHeight() * 0.5f};
            creation_.transform.anchorPoint = {0.5f, 0.5f};
            texturePath_.clear();
            creationNeedsReset_ = false;
        }
        ImGui::OpenPopup(kPopupId);
    }

    ImGui::SetNextWindowSize(ImVec2(880.0f, 0.0f), ImGuiCond_Appearing);
    // 後ろの窓の文字が透けると入力欄と重なって読めないので、背景は塗りつぶす
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    CreationForm &form = creation_;
    SpriteTransform &tf = form.transform;

    // ---- 左: 画像を選ぶ ----
    ImGui::BeginGroup();
    SectionHeader("1. 画像を選ぶ", DebugTheme::kAccentOrange);
    ImGui::BeginChild("##spriteTexturePicker", ImVec2(500.0f, 420.0f), ImGuiChildFlags_Borders);
    ShowTextureFile(texturePath_, "spriteCreate");
    ImGui::EndChild();
    ImGui::EndGroup();

    // 画像を選び直したら名前を付け直す（手で書き換えた名前はそのまま残す）
    if (texturePath_ != form.lastTexture)
    {
        form.lastTexture = texturePath_;
        if (!texturePath_.empty() && (form.name.empty() || form.name == form.autoName))
        {
            form.autoName = MakeUniqueSpriteName(StemOf(texturePath_));
            form.name = form.autoName;
        }
    }

    ImGui::SameLine();

    // ---- 右: プレビューと設定 ----
    ImGui::BeginGroup();
    ImGui::PushItemWidth(330.0f);
    SectionHeader("2. 置き方を決める", DebugTheme::kAccentGreen);

    const Vector2 pixelSize = TexturePixelSize(texturePath_);
    FittedImageBox("##preview", TextureThumb(texturePath_), pixelSize, kPreviewThumbSize);
    std::string dropped;
    if (AssetDragDrop::TextureTarget(dropped))
    {
        texturePath_ = dropped;
    }
    ImGui::SetItemTooltip("アセットブラウザの画像をここへドロップしても選べます");
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (texturePath_.empty())
    {
        ImGui::TextDisabled("左から画像を選んでください");
    }
    else
    {
        ImGui::TextUnformatted(StemOf(texturePath_).c_str());
        ImGui::TextDisabled("%.0f × %.0f px", pixelSize.x, pixelSize.y);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 150.0f);
        ImGui::TextDisabled("%s", texturePath_.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndGroup();
    ImGui::Spacing();

    const float rowWidth = 330.0f;
    ImGui::BeginChild("##spriteCreateSettings", ImVec2(rowWidth, 0.0f), ImGuiChildFlags_AutoResizeY);
    RowLabel("名前");
    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputText("##name", &form.name);

    RowLabel("位置");
    ImGui::DragFloat2("##pos", &tf.position.x, 1.0f, 0.0f, 0.0f, "%.0f");

    // 画面の端・中央へ寄せる（画像の大きさと基準点から位置を逆算する）
    RowLabel("画面に寄せる");
    Vector2 align{};
    if (NineGrid("##align", nullptr, align, "に寄せる"))
    {
        tf.position = AlignedPosition(align, tf.anchorPoint, pixelSize);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("画面 %d × %d", WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight());

    RowLabel("基準点");
    Vector2 anchor = tf.anchorPoint;
    if (NineGrid("##anchor", &tf.anchorPoint, anchor, "を基準にする"))
    {
        tf.anchorPoint = anchor;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("位置・回転・拡大の中心");

    RowLabel("色");
    ImGui::ColorEdit4("##color", &tf.color.x, ImGuiColorEditFlags_AlphaBar);

    RowLabel("反転");
    ImGui::Checkbox("左右##flipX", &tf.isFlipX);
    ImGui::SameLine();
    ImGui::Checkbox("上下##flipY", &tf.isFlipY);

    RowLabel("並べる数");
    int count = static_cast<int>(tf.instanceCount);
    if (ImGui::InputInt("##count", &count))
    {
        tf.instanceCount = static_cast<uint32_t>(std::clamp(count, 1, 1000));
    }
    ImGui::SetItemTooltip("同じ画像を何枚まとめて描くか（インスタンス数）。ふつうは 1");
    ImGui::EndChild();
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    // ---- 確認 ----
    ImGui::Separator();
    const char *problem = nullptr;
    if (texturePath_.empty())
        problem = "画像を選んでください";
    else if (form.name.empty())
        problem = "名前を入れてください";
    else if (HasForbiddenFileChar(form.name))
        problem = "名前に \\ / : * ? \" < > | は使えません（保存ファイル名になるため）";
    else if (GetSprite(form.name))
        problem = "同じ名前のスプライトがあります";

    const bool canCreate = (problem == nullptr);
    if (problem)
    {
        ImGui::TextColored(DebugTheme::kAccentOrange, ICON_FA_EXCLAMATION_TRIANGLE " %s", problem);
    }
    else
    {
        ImGui::TextDisabled("Enter で作成 / Esc でキャンセル");
    }

    // 開いた最初のフレームの Enter は無視する（メニューやパレットで Enter を押して開いたときに即作られないように）
    const bool enterPressed = !ImGui::IsWindowAppearing() &&
                              (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
    const bool createPressed = canCreate ? ConfirmButton(ICON_FA_PLUS " 作成", ImVec2(160.0f, 0.0f))
                                         : NeutralButton(ICON_FA_PLUS " 作成", ImVec2(160.0f, 0.0f));
    if (canCreate && (createPressed || enterPressed))
    {
        RegisterSpriteFromEditor(form.name, texturePath_, tf);
        creationNeedsReset_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (NeutralButton("キャンセル", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        // 入力途中で閉じた内容は、次に開いたとき残しておく（うっかり閉じても打ち直さずに済む）
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ============================================================
//  スプライトマネージャ窓
// ============================================================

void SpriteManager::DrawSpriteManager()
{
    // この窓での編集ジェスチャを Undo 履歴として追跡する
    g_undoTracker.Begin([this] { return CaptureUndoState(); });

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    // ---- ツールバー ----
    if (ConfirmButton(ICON_FA_PLUS " 新規作成"))
    {
        ShowSpriteCreationModal();
    }
    ImGui::SetItemTooltip("画像を選んでスプライトを作ります。\nアセットブラウザから画像をシーンへドラッグしても置けます");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu 個", sprites_.size());

    // シーン上でスプライトをクリックして掴んだら、一覧でもそれを選ぶ
    {
        std::string pickedSprite;
        for (const std::string &name : ImGuizmoManager::GetInstance()->GetSelectedNames())
        {
            if (FindSpriteByName(name))
            {
                pickedSprite = name;
                break;
            }
        }
        if (!pickedSprite.empty() && pickedSprite != lastGizmoPick_)
        {
            selectedName_ = pickedSprite;
        }
        lastGizmoPick_ = pickedSprite;
    }

    if (sprites_.empty())
    {
        ImGui::Spacing();
        DimText("スプライトがありません。「新規作成」か、アセットブラウザから画像をシーンへドラッグしてください");
    }
    else
    {
        // 選んでいた物が消えていたら一番手前（一覧の先頭）を選び直す
        if (!FindSpriteByName(selectedName_))
        {
            selectedName_ = sprites_.back()->name;
        }
        ImGui::Spacing();
        DrawSpriteList();
        if (SpriteData *selected = FindSpriteByName(selectedName_))
        {
            ImGui::Spacing();
            DrawSpriteDetails(selected);
        }
    }

    ImGui::Spacing();
    DrawSpriteFileSection();

    ImGui::PopStyleVar(3);

    // 編集ジェスチャが確定していたら差分をUndo履歴へ積む
    g_undoTracker.End(
        "スプライト編集",
        [this] { return CaptureUndoState(); },
        [](const nlohmann::json &s) { SpriteManager::GetInstance()->RestoreUndoState(s); });
}

void SpriteManager::DrawSpriteList()
{
    SectionHeader("一覧（上ほど手前に描かれる）", DebugTheme::kAccentBlue);

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##spriteFilter", ICON_FA_SEARCH " 名前・画像で絞り込み", &listFilter_);

    auto lower = [](std::string text) {
        for (char &c : text)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        return text;
    };
    const std::string query = lower(listFilter_);

    // 描画は sprites_ の先頭から順なので、後ろほど手前に重なる。
    // 一覧はレイヤーと同じく「上が手前」に見せたいので、後ろから並べる
    std::vector<int> rows;
    for (int i = static_cast<int>(sprites_.size()) - 1; i >= 0; --i)
    {
        const SpriteData &sprite = *sprites_[i];
        if (!query.empty() && lower(sprite.name).find(query) == std::string::npos &&
            lower(sprite.textureFilePath).find(query) == std::string::npos)
        {
            continue;
        }
        rows.push_back(i);
    }
    const bool canReorder = query.empty();

    // 行の中で直接いじると一覧が崩れるので、操作は控えておいて描き終えてから行う
    enum class Action
    {
        None,
        MoveFront,   // 1つ手前へ
        MoveBack,    // 1つ奥へ
        ToFrontMost, // 最前面へ
        ToBackMost,  // 最背面へ
        Duplicate,
        Delete,
    };
    Action action = Action::None;
    std::string actionTarget;

    const float rowHeight = std::max(kListThumbSize, ImGui::GetFrameHeight()) + 4.0f;
    // 行の高さにはセルの上下の余白も乗るので、それも足して見積もる（足りないと最後の行が切れる）
    const float rowPitch = rowHeight + ImGui::GetStyle().CellPadding.y * 2.0f;
    const float tableHeight = std::min(static_cast<float>(rows.size() + 1) * rowPitch + 8.0f, 260.0f);
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##spriteList", 5, flags, ImVec2(-FLT_MIN, tableHeight)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("順", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, kListThumbSize);
        ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableSetupColumn("数", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableHeadersRow();

        for (int index : rows)
        {
            SpriteData &sprite = *sprites_[index];
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
            ImGui::PushID(sprite.name.c_str());
            const bool isSelected = (sprite.name == selectedName_);
            if (isSelected)
            {
                // 選んでいる行は行ごと色を付ける（名前の欄だけだと、どれを選んでいるか見落とす）
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(0.45f, 0.60f, 0.78f, 0.28f)));
            }

            // 手前・奥へ
            ImGui::TableNextColumn();
            if (canReorder)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 1));
                ImGui::BeginDisabled(index == static_cast<int>(sprites_.size()) - 1);
                if (ImGui::ArrowButton("##front", ImGuiDir_Up))
                {
                    action = Action::MoveFront;
                    actionTarget = sprite.name;
                }
                ImGui::SetItemTooltip("1つ手前へ");
                ImGui::EndDisabled();
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::BeginDisabled(index == 0);
                if (ImGui::ArrowButton("##back", ImGuiDir_Down))
                {
                    action = Action::MoveBack;
                    actionTarget = sprite.name;
                }
                ImGui::SetItemTooltip("1つ奥へ");
                ImGui::EndDisabled();
                ImGui::PopStyleVar();
            }
            else
            {
                ImGui::TextDisabled("%d", static_cast<int>(sprites_.size()) - index);
            }

            // サムネイル
            ImGui::TableNextColumn();
            if (ImTextureID thumb = TextureThumb(sprite.textureFilePath))
            {
                ImGui::Image(thumb, ImVec2(kListThumbSize, kListThumbSize));
            }

            // 名前（クリックで選択・右クリックでメニュー）
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (!sprite.isVisible)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }
            if (ImGui::Selectable(sprite.name.c_str(), isSelected, ImGuiSelectableFlags_AllowOverlap))
            {
                selectedName_ = sprite.name;
                // シーンのギズモでもこのスプライトを掴んだ状態にする
                ImGuizmoManager::GetInstance()->SelectOnly(sprite.name);
                lastGizmoPick_ = sprite.name;
            }
            if (!sprite.isVisible)
            {
                ImGui::PopStyleColor();
            }
            if (ImGui::BeginItemTooltip())
            {
                const Vector2 pixelSize = TexturePixelSize(sprite.textureFilePath);
                FittedImageBox("##tip", TextureThumb(sprite.textureFilePath), pixelSize, 96.0f);
                ImGui::TextUnformatted(sprite.textureFilePath.c_str());
                ImGui::TextDisabled("%.0f × %.0f px / 右クリックでメニュー", pixelSize.x, pixelSize.y);
                ImGui::EndTooltip();
            }
            if (ImGui::BeginPopupContextItem("##rowMenu"))
            {
                ImGui::TextDisabled("%s", sprite.name.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_CLONE " 複製"))
                {
                    action = Action::Duplicate;
                    actionTarget = sprite.name;
                }
                if (ImGui::MenuItem(ICON_FA_ANGLE_DOUBLE_UP " 最前面へ", nullptr, false, canReorder))
                {
                    action = Action::ToFrontMost;
                    actionTarget = sprite.name;
                }
                if (ImGui::MenuItem(ICON_FA_ANGLE_DOUBLE_DOWN " 最背面へ", nullptr, false, canReorder))
                {
                    action = Action::ToBackMost;
                    actionTarget = sprite.name;
                }
                if (ImGui::MenuItem(ICON_FA_COPY " 名前をコピー"))
                {
                    ImGui::SetClipboardText(sprite.name.c_str());
                }
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
                if (ImGui::MenuItem(ICON_FA_TRASH_ALT " 削除"))
                {
                    action = Action::Delete;
                    actionTarget = sprite.name;
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }

            // 表示の切り替え（目のアイコン）
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, sprite.isVisible ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                                                                  : DebugTheme::kAccentOrange);
            if (ImGui::Button(sprite.isVisible ? ICON_FA_EYE : ICON_FA_EYE_SLASH, ImVec2(ImGui::GetFrameHeight(), 0.0f)))
            {
                sprite.isVisible = !sprite.isVisible;
            }
            ImGui::PopStyleColor(2);
            ImGui::SetItemTooltip(sprite.isVisible ? "表示中（クリックで隠す）" : "非表示（クリックで表示）");

            // インスタンス数（複数並べている物だけ目立たせる）
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (sprite.instanceData.size() > 1)
                ImGui::Text("%zu", sprite.instanceData.size());
            else
                ImGui::TextDisabled("1");

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!canReorder)
    {
        DimText("絞り込み中は並べ替えできません");
    }

    // ---- 控えておいた操作を行う ----
    if (action == Action::None)
    {
        return;
    }
    const int index = FindSpriteIndex(actionTarget);
    switch (action)
    {
    case Action::MoveFront:
        MoveDrawOrder(actionTarget, index + 1);
        break;
    case Action::MoveBack:
        MoveDrawOrder(actionTarget, index - 1);
        break;
    case Action::ToFrontMost:
        MoveDrawOrder(actionTarget, static_cast<int>(sprites_.size()) - 1);
        break;
    case Action::ToBackMost:
        MoveDrawOrder(actionTarget, 0);
        break;
    case Action::Duplicate:
        if (SpriteData *copy = DuplicateSprite(actionTarget))
        {
            selectedName_ = copy->name;
            lastGizmoPick_ = copy->name;
            ImGuizmoManager::GetInstance()->SelectOnly(copy->name);
        }
        break;
    case Action::Delete:
        UnregisterSprite(actionTarget);
        break;
    default:
        break;
    }
}

void SpriteManager::DrawSpriteDetails(SpriteData *sprite)
{
    ImGui::PushID(sprite->name.c_str());

    // ---- 見出し: サムネイル（ドロップで画像を差し替え）・名前・画像の情報 ----
    const Vector2 pixelSize = TexturePixelSize(sprite->textureFilePath);
    if (FittedImageBox("##thumb", TextureThumb(sprite->textureFilePath), pixelSize, kDetailThumbSize))
    {
        ImGui::OpenPopup("##replaceTexture");
    }
    std::string dropped;
    const bool droppedTexture = AssetDragDrop::TextureTarget(dropped);
    ImGui::SetItemTooltip("クリック: 画像を選び直す / 画像をドロップ: 差し替え");

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(sprite->name.c_str());
    ImGui::TextDisabled("%s", sprite->textureFilePath.c_str());
    ImGui::TextDisabled("画像 %.0f × %.0f px", pixelSize.x, pixelSize.y);
    if (NeutralButton(ICON_FA_CLONE " 複製"))
    {
        if (SpriteData *copy = DuplicateSprite(sprite->name))
        {
            selectedName_ = copy->name;
            lastGizmoPick_ = copy->name;
            ImGuizmoManager::GetInstance()->SelectOnly(copy->name);
        }
    }
    ImGui::SameLine();
    const bool deletePressed = DangerButton(ICON_FA_TRASH_ALT " 削除");
    ImGui::SetItemTooltip("Ctrl+Z で戻せます");
    ImGui::EndGroup();

    // 画像の選び直し（サムネイルを押すと開く）
    std::string replaced = droppedTexture ? dropped : std::string();
    ImGui::SetNextWindowSize(ImVec2(520.0f, 420.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##replaceTexture"))
    {
        ImGui::TextDisabled("差し替える画像を選んでください");
        std::string picked = sprite->textureFilePath;
        ShowTextureFile(picked, "spriteReplace");
        if (picked != sprite->textureFilePath)
        {
            replaced = picked;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!replaced.empty() && replaced != sprite->textureFilePath)
    {
        // 大きさは今のまま。画像の縦横比が違うときは「見た目」タブの「原寸」で合わせ直せる
        sprite->textureFilePath = replaced;
        sprite->sprite->SetTexturePath(replaced);
    }

    ImGui::Spacing();
    if (ImGui::BeginTabBar("##spriteDetailTabs"))
    {
        if (ImGui::BeginTabItem("配置"))
        {
            DrawSpritePlacement(sprite);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("見た目"))
        {
            DrawSpriteAppearance(sprite);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("UV"))
        {
            DrawSpriteUV(sprite);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopID();

    // 削除は最後に行う（ここまで sprite を触っているため）
    if (deletePressed)
    {
        UnregisterSprite(sprite->name);
    }
}

void SpriteManager::DrawSpritePlacement(SpriteData *sprite)
{
    if (sprite->instanceData.empty())
    {
        DimText("インスタンスがありません");
        return;
    }

    int &selected = selectedInstance_[sprite->name];
    auto clampSelection = [&] {
        selected = std::clamp(selected, 0, static_cast<int>(sprite->instanceData.size()) - 1);
    };
    // ギズモの掴み先を、編集中のインスタンスへ合わせる。
    // instanceData の再確保で登録済みのポインタが無効になるので、アドレスを比べて張り直す
    auto syncGizmo = [&] {
        clampSelection();
        if (gizmoBound_[sprite->name] != &sprite->instanceData[selected].translation)
        {
            SyncGizmoTarget(sprite, selected);
        }
    };
    syncGizmo();

    const int count = static_cast<int>(sprite->instanceData.size());

    // ---- 複数並べているときだけ、どれを触るかを選ぶ行を出す ----
    if (count > 1)
    {
        RowLabel("インスタンス");
        const float buttonWidth = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        if (ImGui::ArrowButton("##prev", ImGuiDir_Left))
            selected = (selected + count - 1) % count;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (buttonWidth + spacing) * 3.0f);
        const std::string preview = std::format("{} / {}{}", selected + 1, count,
                                                sprite->instanceData[selected].isActive ? "" : "（非表示）");
        if (ImGui::BeginCombo("##instance", preview.c_str()))
        {
            for (int i = 0; i < count; ++i)
            {
                const std::string label = std::format("{}{}", i + 1, sprite->instanceData[i].isActive ? "" : "（非表示）");
                if (ImGui::Selectable(label.c_str(), i == selected))
                    selected = i;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##next", ImGuiDir_Right))
            selected = (selected + 1) % count;
        ImGui::SameLine();
        ImGui::BeginDisabled(count >= 1000); // Sprite 側の行列バッファの上限
        if (ImGui::Button(ICON_FA_PLUS "##addInstance", ImVec2(buttonWidth, 0.0f)))
        {
            InstanceSRT copy = sprite->instanceData[selected];
            copy.translation.x += 20.0f;
            copy.translation.y += 20.0f;
            sprite->instanceData.insert(sprite->instanceData.begin() + selected + 1, copy);
            ++selected;
            UpdateSpriteInstances(sprite);
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("今のインスタンスを複製して増やす");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_MINUS "##removeInstance", ImVec2(buttonWidth, 0.0f)))
        {
            sprite->instanceData.erase(sprite->instanceData.begin() + selected);
            UpdateSpriteInstances(sprite);
        }
        ImGui::SetItemTooltip("今のインスタンスを消す");
        syncGizmo(); // 追加・削除で再確保されていたら張り直す
    }

    InstanceSRT &instance = sprite->instanceData[selected];
    const Vector2 size = sprite->sprite->GetSize();
    const float resetWidth = ImGui::CalcTextSize("戻す").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float resetSpace = resetWidth + ImGui::GetStyle().ItemSpacing.x;

    RowLabel("位置");
    ImGui::DragFloat2("##position", &instance.translation.x, 1.0f, 0.0f, 0.0f, "%.0f");

    RowLabel("画面に寄せる");
    Vector2 align{};
    if (NineGrid("##align", nullptr, align, "に寄せる"))
    {
        const Vector2 visible = {size.x * instance.scale.x, size.y * instance.scale.y};
        const Vector2 position = AlignedPosition(align, sprite->sprite->GetAnchorPoint(), visible);
        instance.translation.x = position.x;
        instance.translation.y = position.y;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("画面 %d × %d", WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight());

    RowLabel("拡大");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - resetSpace);
    ImGui::DragFloat2("##scale", &instance.scale.x, 0.01f, 0.0f, 10.0f, "%.2f");
    ImGui::SameLine();
    if (SmallResetButton("戻す##scale"))
    {
        instance.scale = {1.0f, 1.0f, 1.0f};
    }

    RowLabel("回転");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - resetSpace);
    ImGui::SliderAngle("##rotation", &instance.rotation.z, -180.0f, 180.0f, "%.0f 度");
    ImGui::SameLine();
    if (SmallResetButton("戻す##rotation"))
    {
        instance.rotation.z = 0.0f;
    }

    if (count > 1)
    {
        RowLabel("表示");
        ImGui::Checkbox("このインスタンスを表示##active", &instance.isActive);

        RowLabel("まとめて");
        if (ImGui::SmallButton("全部表示"))
        {
            for (InstanceSRT &other : sprite->instanceData)
                other.isActive = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("全部隠す"))
        {
            for (InstanceSRT &other : sprite->instanceData)
                other.isActive = false;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("拡大を全部戻す"))
        {
            for (InstanceSRT &other : sprite->instanceData)
                other.scale = {1.0f, 1.0f, 1.0f};
        }
    }
    else
    {
        ImGui::Spacing();
        if (ImGui::SmallButton(ICON_FA_PLUS " 同じ画像をもう1枚並べる"))
        {
            InstanceSRT copy = instance;
            copy.translation.x += 20.0f;
            copy.translation.y += 20.0f;
            sprite->instanceData.push_back(copy);
            selected = static_cast<int>(sprite->instanceData.size()) - 1;
            UpdateSpriteInstances(sprite);
            syncGizmo();
        }
        ImGui::SetItemTooltip("インスタンスを増やして、1つのスプライトで同じ画像を何枚も描く（ゲージの目盛りなど）");
    }
    DimText("シーンのギズモでも動かせます");
}

void SpriteManager::DrawSpriteAppearance(SpriteData *sprite)
{
    Sprite *body = sprite->sprite.get();

    // ---- サイズ（比率の固定・原寸へ戻す）----
    {
        const Vector2 current = body->GetSize();
        const float buttonWidth = ImGui::GetFrameHeight();
        const float originalWidth = ImGui::CalcTextSize("原寸").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;

        RowLabel("サイズ");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - originalWidth - spacing * 2.0f);
        float values[2] = {current.x, current.y};
        if (ImGui::DragFloat2("##size", values, 1.0f, 0.0f, 4096.0f, "%.0f"))
        {
            if (sprite->lockAspectRatio && current.x > 0.0f && current.y > 0.0f)
            {
                // 動かした方に合わせて、もう片方を比率どおりに追従させる
                const float aspect = current.y / current.x;
                if (values[0] != current.x)
                    values[1] = values[0] * aspect;
                else
                    values[0] = values[1] / aspect;
            }
            body->SetSize({std::max(0.0f, values[0]), std::max(0.0f, values[1])});
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, sprite->lockAspectRatio ? DebugTheme::kAccentBlue
                                                                       : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        if (ImGui::Button(sprite->lockAspectRatio ? ICON_FA_LINK "##lock" : ICON_FA_UNLINK "##lock", ImVec2(buttonWidth, 0.0f)))
        {
            sprite->lockAspectRatio = !sprite->lockAspectRatio;
        }
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip(sprite->lockAspectRatio ? "縦横比を固定中（クリックで解除）" : "縦横を別々に変える（クリックで比率を固定）");
        ImGui::SameLine();
        if (ImGui::Button("原寸"))
        {
            body->ResetSizeToTexture();
        }
        ImGui::SetItemTooltip("画像のピクセルサイズに戻す");
    }

    RowLabel("色");
    Vector4 color = body->GetColor();
    if (ImGui::ColorEdit4("##color", &color.x, ImGuiColorEditFlags_AlphaBar))
    {
        body->SetColor({color.x, color.y, color.z});
        body->SetAlpha(color.w);
    }

    // ---- 基準点 ----
    // 変えても見た目の場所は動かさない（基準点だけ移る）。動いてしまうと置き直しになるため
    RowLabel("基準点");
    const Vector2 oldAnchor = body->GetAnchorPoint();
    Vector2 newAnchor = oldAnchor;
    bool anchorChanged = NineGrid("##anchor", &oldAnchor, newAnchor, "を基準にする");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    float anchorValues[2] = {newAnchor.x, newAnchor.y};
    if (ImGui::DragFloat2("##anchorValue", anchorValues, 0.01f, 0.0f, 1.0f, "%.2f"))
    {
        newAnchor = {anchorValues[0], anchorValues[1]};
        anchorChanged = true;
    }
    ImGui::SetItemTooltip("位置・回転・拡大の中心。変えても見た目の場所はそのまま");
    if (anchorChanged)
    {
        const Vector2 size = body->GetSize();
        const float baseRotation = body->GetRotation();
        for (InstanceSRT &instance : sprite->instanceData)
        {
            // 左上の角が動かないよう、基準点がずれたぶんだけ位置を足す（回転も考える）
            const float dx = (newAnchor.x - oldAnchor.x) * size.x * instance.scale.x;
            const float dy = (newAnchor.y - oldAnchor.y) * size.y * instance.scale.y;
            const float angle = instance.rotation.z + baseRotation;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            instance.translation.x += dx * c - dy * s;
            instance.translation.y += dx * s + dy * c;
        }
        body->SetAnchorPoint(newAnchor);
    }

    RowLabel("反転");
    bool flipX = body->GetFlipX();
    bool flipY = body->GetFlipY();
    if (ImGui::Checkbox("左右##flipX", &flipX))
        body->SetFlipX(flipX);
    ImGui::SameLine();
    if (ImGui::Checkbox("上下##flipY", &flipY))
        body->SetFlipY(flipY);

    RowLabel("合成");
    int blend = static_cast<int>(sprite->blendMode);
    if (ImGui::Combo("##blend", &blend, kBlendModeNames, IM_ARRAYSIZE(kBlendModeNames)))
    {
        sprite->blendMode = static_cast<BlendMode>(blend);
    }

    // 全インスタンス共通の回転。1枚だけのときは「配置」の回転で足りるので、使っているときだけ出す
    float baseRotation = body->GetRotation();
    if (sprite->instanceData.size() > 1 || baseRotation != 0.0f)
    {
        RowLabel("全体の回転");
        if (ImGui::SliderAngle("##baseRotation", &baseRotation, -180.0f, 180.0f, "%.0f 度"))
        {
            body->SetRotation(baseRotation);
        }
        ImGui::SetItemTooltip("全インスタンスの回転にまとめて足される");
    }

    RowLabel("表示");
    ImGui::Checkbox("表示する##visible", &sprite->isVisible);
    ImGui::SameLine();
    ImGui::Checkbox("最背面##backMost", &sprite->isBackMost);
    ImGui::SetItemTooltip("3D の物より奥に描く（背景画像など）");
}

void SpriteManager::DrawSpriteUV(SpriteData *sprite)
{
    Sprite *body = sprite->sprite.get();
    Vector2 uvSize = body->GetUVSize();
    Vector2 uvPosition = body->GetUVPosition();
    float uvRotate = body->GetUVRotate();
    bool changed = false;

    RowLabel("繰り返し");
    changed |= ImGui::DragFloat2("##uvSize", &uvSize.x, 0.01f, 0.01f, 64.0f, "%.2f");
    ImGui::SetItemTooltip("1 で1枚。2 にすると画像が2回ならぶ（タイリング）");

    RowLabel("ずらし");
    changed |= ImGui::DragFloat2("##uvOffset", &uvPosition.x, 0.01f, -4.0f, 4.0f, "%.2f");

    RowLabel("回転");
    changed |= ImGui::SliderAngle("##uvRotate", &uvRotate, -180.0f, 180.0f, "%.0f 度");

    if (changed)
    {
        body->SetUVSize(uvSize);
        body->SetUVPosition(uvPosition);
        body->SetUVRotate(uvRotate);
    }
    ImGui::Spacing();
    if (ImGui::SmallButton("UVを戻す"))
    {
        body->SetUVSize({1.0f, 1.0f});
        body->SetUVPosition({0.0f, 0.0f});
        body->SetUVRotate(0.0f);
    }
}

void SpriteManager::DrawSpriteFileSection()
{
    if (!ImGui::CollapsingHeader(ICON_FA_FOLDER " 保存 / 読み込み（全シーン共通）"))
    {
        return;
    }
    ImGui::Indent(6.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextWrapped("シーンの保存（Ctrl+S）でも一緒に保存されます。ここは保存先のフォルダを変えたいとき、"
                       "保存した状態へ読み直したいときに使います。");
    ImGui::PopStyleColor();

    RowLabel("フォルダ");
    ImGui::InputText("##folder", &saveFolder_);
    const bool folderOk = !saveFolder_.empty() && !HasForbiddenFileChar(saveFolder_);
    if (folderOk)
    {
        ImGui::TextDisabled("%s/Sprites/%s/", AssetPath::JsonRoot().c_str(), saveFolder_.c_str());
    }
    else
    {
        ImGui::TextColored(DebugTheme::kAccentOrange, "フォルダ名を入れてください（\\ / : * ? \" < > | は使えません）");
    }

    const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    ImGui::BeginDisabled(!folderOk);
    if (ConfirmButton(ICON_FA_SAVE " 保存", ImVec2(width, 0.0f)))
    {
        SaveAllSprites();
    }
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_UPLOAD " 読み直す", ImVec2(width, 0.0f)))
    {
        LoadAllSprites();
    }
    ImGui::SetItemTooltip("今あるスプライトを、保存した内容で置き換える（Ctrl+Z で戻せます）");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (DangerButton(ICON_FA_TRASH_ALT " 全部消す", ImVec2(width, 0.0f)))
    {
        ImGui::OpenPopup("全部消す##spriteClearAll");
    }

    if (ImGui::BeginPopupModal("全部消す##spriteClearAll", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("スプライト %zu 個をすべて消しますか？", sprites_.size());
        ImGui::TextDisabled("Ctrl+Z で戻せます。ゲーム側がコードで作ったスプライトは消えません");
        ImGui::Separator();
        if (DangerButton("消す", ImVec2(120.0f, 0.0f)))
        {
            RemoveOwnedSprites();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (NeutralButton("キャンセル", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::Unindent(6.0f);
}
} // namespace Hagine

#else // USE_IMGUI

namespace Hagine {
// エディタを持たない構成（Release）では UI は何もしない
void SpriteManager::DrawSpriteCreationModal() {}
void SpriteManager::DrawSpriteManager() {}
} // namespace Hagine

#endif // USE_IMGUI
