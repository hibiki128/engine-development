#include "ImGuiManager.h"
#ifdef USE_IMGUI
// シーンビューの上に重ねる操作部品。
//   ・左上: ツールバー（移動/回転/拡縮・座標系・スナップと刻み・グリッド・デバッグカメラと速さ）
//   ・右上: 軸の向き表示（押すとその軸の方向から見る）
//   ・枠  : 一時停止中・停止中は色付きの枠と札を出す（再生中は何も出さない＝従来どおり）
// どれも子ウィンドウにしているので、上にマウスがある間はシーンのクリック選択が発火しない
// （シーン窓の IsWindowHovered が子ウィンドウの上では false になる）。
#include "DebugUIHelper.h"
#include "ImGuiNotification.h"
#include "ImGuizmoManager.h"
#include "ImGuiManagerSceneOverlayInternal.h"
#include <Input.h>
#include <fstream>
#include <Mymath.h>
#include <algorithm>
#include <camera/CameraManager.h>
#include <debug/capture/CaptureManager.h>
#include <edit/play/PlayModeManager.h>
#include <format>
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <line/LineRenderer.h>
#include <numbers>

namespace Hagine {
using namespace SceneOverlayInternal;
namespace {
/// <summary>ツールバーのアイコンボタン。選ばれている間はアクセント色の地にする</summary>
bool ToolButton(const char *id, const char *icon, bool active, const char *tooltip)
{
    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    const ImVec4 base = active ? ImVec4(accent.x * 0.55f, accent.y * 0.55f, accent.z * 0.55f, 0.90f)
                               : ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    const ImVec4 hover = active ? ImVec4(accent.x * 0.70f, accent.y * 0.70f, accent.z * 0.70f, 0.95f)
                                : DebugTheme::kButtonGhostHover;
    ScopedButtonColors colors(base, hover);
    ImGui::PushStyleColor(ImGuiCol_Text, active ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::PushID(id);
    const float size = ImGui::GetFrameHeight();
    const bool pressed = ImGui::Button(icon, ImVec2(size, size));
    ImGui::PopID();
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

/// <summary>
/// シーンのクリック対象（種類）の切り替えボタン。ON は種類の色、OFF は薄い色に斜線。
/// 左クリックで反転、右クリックか Alt+クリックで「これだけ」
/// </summary>
/// <param name="outSolo">「これだけ」の操作だったか</param>
/// <returns>bool: 押されたか（左右どちらでも）</returns>
bool CategoryButton(const char *id, const char *icon, const ImVec4 &accent, bool enabled, const char *tooltip, bool &outSolo)
{
    ScopedButtonColors colors(ImVec4(0.0f, 0.0f, 0.0f, 0.0f), DebugTheme::kButtonGhostHover);
    ImVec4 text = accent;
    if (!enabled)
    {
        text = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        text.w *= 0.55f;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::PushID(id);
    const float size = ImGui::GetFrameHeight();
    const bool pressed = ImGui::Button(icon, ImVec2(size, size));
    const bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    ImGui::PopID();
    ImGui::PopStyleColor();
    if (!enabled)
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x + 5.0f, max.y - 5.0f), ImVec2(max.x - 5.0f, min.y + 5.0f),
                                            ImGui::GetColorU32(ImGuiCol_TextDisabled), 1.5f);
    }
    ImGui::SetItemTooltip("%s", tooltip);
    outSolo = rightClicked || (pressed && ImGui::GetIO().KeyAlt);
    return pressed || rightClicked;
}

/// <summary>ツールバーの区切り（縦線）</summary>
void ToolSeparator()
{
    ImGui::SameLine();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + 3.0f), ImVec2(pos.x, pos.y + height - 3.0f),
                                        ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
    ImGui::Dummy(ImVec2(1.0f, height));
    ImGui::SameLine();
}

/// <summary>視線の向き（単位ベクトル）から DebugCamera のピッチ・ヨーを求める</summary>
void DirectionToPitchYaw(const Vector3 &direction, float &outPitch, float &outYaw)
{
    // DebugCamera の視線 = (cos p sin y, -sin p, cos p cos y)
    outPitch = -std::asin(std::clamp(direction.y, -1.0f, 1.0f));
    const float horizontal = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    outYaw = (horizontal > 1e-4f) ? std::atan2(direction.x, direction.z) : 0.0f;
}
} // namespace

void ImGuiManager::DrawSceneOverlay(const ImVec2 &imageMin, const ImVec2 &imageSize)
{
    if (!showSceneOverlay_ || imageSize.x < 200.0f || imageSize.y < 120.0f)
    {
        return;
    }
    ImGuizmoManager *gizmo = pImGuizmoManager_;
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 imageMax = ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y);

    // 名前ラベル・距離（ツールバーより下に描く）
    DrawSceneLabels(imageMin, imageSize);

    // ---- 構図ガイド（カメラワークや UI の配置を決めるとき用）----
    if (showThirdsGuide_)
    {
        const ImU32 color = IM_COL32(255, 255, 255, 70);
        for (int i = 1; i <= 2; ++i)
        {
            const float x = imageMin.x + imageSize.x * static_cast<float>(i) / 3.0f;
            const float y = imageMin.y + imageSize.y * static_cast<float>(i) / 3.0f;
            drawList->AddLine(ImVec2(x, imageMin.y), ImVec2(x, imageMax.y), color, 1.0f);
            drawList->AddLine(ImVec2(imageMin.x, y), ImVec2(imageMax.x, y), color, 1.0f);
        }
    }
    if (showSafeAreaGuide_)
    {
        // 外周 5%（アクション）と 10%（タイトル）の2重枠
        for (float margin : {0.05f, 0.10f})
        {
            const ImVec2 min = ImVec2(imageMin.x + imageSize.x * margin, imageMin.y + imageSize.y * margin);
            const ImVec2 max = ImVec2(imageMax.x - imageSize.x * margin, imageMax.y - imageSize.y * margin);
            drawList->AddRect(min, max, margin < 0.07f ? IM_COL32(120, 200, 255, 90) : IM_COL32(255, 210, 120, 110), 0.0f, 0, 1.0f);
        }
    }

    // ---- 再生状態の枠と札（一時停止・停止のときだけ）----
    {
        const PlayModeManager::State state = PlayModeManager::GetInstance()->GetState();
        if (state != PlayModeManager::State::Playing)
        {
            const bool paused = (state == PlayModeManager::State::Paused);
            const ImVec4 color = paused ? DebugTheme::kAccentYellow : DebugTheme::kAccentBlue;
            drawList->AddRect(imageMin, imageMax, ImGui::ColorConvertFloat4ToU32(color), 0.0f, 0, 2.0f);
            const char *label = paused ? ICON_FA_PAUSE "  一時停止中" : ICON_FA_STOP "  停止中（編集）";
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            const ImVec2 badgeMin = ImVec2(imageMin.x + (imageSize.x - textSize.x) * 0.5f - 10.0f, imageMax.y - textSize.y - 16.0f);
            const ImVec2 badgeMax = ImVec2(badgeMin.x + textSize.x + 20.0f, badgeMin.y + textSize.y + 8.0f);
            ImVec4 bg = color;
            bg.w = 0.85f;
            drawList->AddRectFilled(badgeMin, badgeMax, ImGui::ColorConvertFloat4ToU32(ImVec4(0.06f, 0.06f, 0.08f, 0.80f)), 6.0f);
            drawList->AddRect(badgeMin, badgeMax, ImGui::ColorConvertFloat4ToU32(bg), 6.0f, 0, 1.0f);
            drawList->AddText(ImVec2(badgeMin.x + 10.0f, badgeMin.y + 4.0f), ImGui::ColorConvertFloat4ToU32(color), label);
        }
    }

    // ---- 左上: ツールバー ----
    ImGui::SetCursorScreenPos(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.09f, 0.78f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    ImGui::BeginChild("##sceneToolbar", ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY |
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        const ImGuizmo::OPERATION operation = gizmo->GetOperation();
        if (ToolButton("move", ICON_FA_ARROWS_ALT, operation == ImGuizmo::TRANSLATE, "移動 (1)"))
            gizmo->SetOperation(ImGuizmo::TRANSLATE);
        ImGui::SameLine();
        if (ToolButton("rotate", ICON_FA_SYNC_ALT, operation == ImGuizmo::ROTATE, "回転 (2)"))
            gizmo->SetOperation(ImGuizmo::ROTATE);
        ImGui::SameLine();
        if (ToolButton("scale", ICON_FA_EXPAND_ARROWS_ALT, operation == ImGuizmo::SCALE, "拡縮 (3)"))
            gizmo->SetOperation(ImGuizmo::SCALE);

        ToolSeparator();
        const bool world = (gizmo->GetMode() == ImGuizmo::WORLD);
        if (ToolButton("space", world ? ICON_FA_GLOBE : ICON_FA_CUBE, false,
                       world ? "ワールド座標で操作中（クリックでローカルへ / 4）" : "ローカル座標で操作中（クリックでワールドへ / 4）"))
        {
            gizmo->SetMode(world ? ImGuizmo::LOCAL : ImGuizmo::WORLD);
        }

        ToolSeparator();
        bool &useSnap = gizmo->UseSnap();
        if (ToolButton("snap", ICON_FA_MAGNET, useSnap, "スナップ（5 / Shift を押している間は反転）"))
            useSnap = !useSnap;
        ImGui::SameLine();
        {
            // 刻み幅（今の操作モードのぶん）。押すと候補が出る
            const char *valueText = nullptr;
            std::string text;
            if (operation == ImGuizmo::ROTATE)
                text = std::format("{:.0f}°", gizmo->SnapRotateDegree());
            else if (operation == ImGuizmo::SCALE)
                text = std::format("{:.2f}", gizmo->SnapScale());
            else
                text = std::format("{:.2g}", gizmo->SnapTranslate());
            valueText = text.c_str();
            ScopedButtonColors colors(DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
            ImGui::PushStyleColor(ImGuiCol_Text, useSnap ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
                                                         : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Button((std::string(valueText) + " " ICON_FA_CARET_DOWN "##snapStep").c_str()))
                ImGui::OpenPopup("##snapStepPopup");
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("スナップの刻み幅");
            if (ImGui::BeginPopup("##snapStepPopup"))
            {
                ImGui::SeparatorText("移動");
                static const float kTranslatePresets[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
                for (float preset : kTranslatePresets)
                {
                    if (preset != kTranslatePresets[0])
                        ImGui::SameLine();
                    if (ImGui::Selectable(std::format("{:g}##t", preset).c_str(), gizmo->SnapTranslate() == preset, 0,
                                          ImVec2(ImGui::CalcTextSize("0.25").x, 0.0f)))
                        gizmo->SnapTranslate() = preset;
                }
                ImGui::SetNextItemWidth(160.0f);
                ImGui::DragFloat("##snapT", &gizmo->SnapTranslate(), 0.05f, 0.01f, 100.0f, "%.2f");
                ImGui::SeparatorText("回転（度）");
                static const float kRotatePresets[] = {5.0f, 15.0f, 30.0f, 45.0f, 90.0f};
                for (float preset : kRotatePresets)
                {
                    if (preset != kRotatePresets[0])
                        ImGui::SameLine();
                    if (ImGui::Selectable(std::format("{:g}##r", preset).c_str(), gizmo->SnapRotateDegree() == preset, 0,
                                          ImVec2(ImGui::CalcTextSize("90").x, 0.0f)))
                        gizmo->SnapRotateDegree() = preset;
                }
                ImGui::SeparatorText("拡縮");
                ImGui::SetNextItemWidth(160.0f);
                ImGui::DragFloat("##snapS", &gizmo->SnapScale(), 0.01f, 0.01f, 10.0f, "%.2f");
                ImGui::Separator();
                ImGui::Checkbox("移動中に刻みのグリッドを描く", &gizmo->ShowSnapGrid());
                ImGui::EndPopup();
            }
        }

        ToolSeparator();
        if (ToolButton("grid", ICON_FA_BORDER_ALL, showGrid_, "床のグリッド表示"))
            showGrid_ = !showGrid_;

        // デバッグ線（種類ごとの表示）。グリッド・コライダー・ライト・選択の枠などをここでまとめて切り替える
        ImGui::SameLine();
        LineRenderer *lines = LineRenderer::GetInstance();
        if (ToolButton("debugLines", ICON_FA_PENCIL_RULER, lines->IsAllLinesEnabled(),
                       "デバッグ線（種類ごとに出す/隠す）\n右クリック: 全部の線を出す/隠す"))
            ImGui::OpenPopup("##debugLinePopup");
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            lines->SetAllLinesEnabled(!lines->IsAllLinesEnabled());
        ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 0.0f), ImVec2(420.0f, FLT_MAX));
        if (ImGui::BeginPopup("##debugLinePopup"))
        {
            lines->DrawCategoryImGui(true);
            ImGui::Separator();
            if (ImGui::Selectable(ICON_FA_EXTERNAL_LINK_ALT " 窓で開く"))
                showDebugLineView_ = true;
            ImGui::EndPopup();
        }

        ToolSeparator();
        DebugCamera *debugCamera = pCurrentScene_ ? pCurrentScene_->GetDebugCamera() : nullptr;
        const bool debugCameraActive = debugCamera && debugCamera->GetActive();
        if (ToolButton("dbgcam", ICON_FA_VIDEO, debugCameraActive,
                       debugCameraActive ? "デバッグカメラ使用中（F3 / クリックで元のカメラへ）" : "デバッグカメラに切り替える (F3)"))
        {
            if (pCurrentScene_)
                pCurrentScene_->ToggleDebugCamera();
        }
        if (debugCamera && debugCameraActive)
        {
            ImGui::SameLine();
            ScopedButtonColors colors(DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
            if (ImGui::Button(ICON_FA_TACHOMETER_ALT " " ICON_FA_CARET_DOWN "##camSpeed"))
                ImGui::OpenPopup("##camSpeedPopup");
            ImGui::SetItemTooltip("カメラの移動速度（Ctrl で5倍）");
            if (ImGui::BeginPopup("##camSpeedPopup"))
            {
                float speed = debugCamera->GetMoveSpeed();
                ImGui::SetNextItemWidth(200.0f);
                if (ImGui::SliderFloat("移動速度", &speed, 0.001f, 0.1f, "%.3f", ImGuiSliderFlags_Logarithmic))
                    debugCamera->SetMoveSpeed(speed);
                static const float kSpeedPresets[] = {0.002f, 0.005f, 0.015f, 0.04f};
                static const char *kSpeedLabels[] = {"ゆっくり", "ふつう", "速い", "とても速い"};
                for (int i = 0; i < 4; ++i)
                {
                    if (i > 0)
                        ImGui::SameLine();
                    if (NeutralButton(kSpeedLabels[i]))
                        debugCamera->SetMoveSpeed(kSpeedPresets[i]);
                }
                ImGui::EndPopup();
            }
        }

        // カメラのブックマーク
        ImGui::SameLine();
        if (ToolButton("bookmark", ICON_FA_BOOKMARK, false, "カメラのブックマーク（Shift+数字で呼び出し / Ctrl+Shift+数字で保存）"))
            ImGui::OpenPopup("##cameraBookmarks");
        DrawCameraBookmarkPopup();

        // スクリーンショット（F9 と同じ）
        ImGui::SameLine();
        if (ToolButton("screenshot", ICON_FA_CAMERA, false, "スクリーンショットを撮る (F9)"))
            CaptureManager::GetInstance()->RequestScreenshot();

        // 名前ラベルと計測
        ToolSeparator();
        if (ToolButton("labels", ICON_FA_TAG, sceneLabelMode_ != 0, "名前ラベルと距離の計測"))
            ImGui::OpenPopup("##labelPopup");
        if (ImGui::BeginPopup("##labelPopup"))
        {
            ImGui::SeparatorText("名前ラベル");
            ImGui::RadioButton("出さない", &sceneLabelMode_, 0);
            ImGui::SameLine();
            ImGui::RadioButton("選択中だけ", &sceneLabelMode_, 1);
            ImGui::SameLine();
            ImGui::RadioButton("すべて", &sceneLabelMode_, 2);
            ImGui::SeparatorText("計測");
            ImGui::Checkbox("2つ以上選ぶと間の距離を描く", &showSceneMeasure_);
            ImGui::TextDisabled("名前順に隣どうしを結んで、距離と各軸の差を出します");
            ImGui::SeparatorText("構図ガイド");
            ImGui::Checkbox("三分割線", &showThirdsGuide_);
            ImGui::SameLine();
            ImGui::Checkbox("セーフエリア", &showSafeAreaGuide_);
            ImGui::SetItemTooltip("青: 外周5%（動きが切れない範囲） / 橙: 外周10%（文字を置く範囲）");
            ImGui::EndPopup();
        }

        // ライト・カメラ・エミッター・フィールドのアイコン
        ImGui::SameLine();
        if (ToolButton("icons", ICON_FA_EYE, showSceneIcons_, "ライト・カメラ・エミッター・フィールドのアイコン（押すと選べる）"))
            ImGui::OpenPopup("##iconPopup");
        if (ImGui::BeginPopup("##iconPopup"))
        {
            DrawSceneIconSettings();
            ImGui::EndPopup();
        }

        // シーンのクリック対象（種類ごと）。階層・インスペクタからの選択には効かない
        ToolSeparator();
        struct CategoryItem
        {
            GizmoCategory category;
            const char *icon;
            const char *tooltip;
        };
        static const CategoryItem kCategoryItems[kGizmoCategoryCount] = {
            {GizmoCategory::Object, ICON_FA_CUBE, "オブジェクトをクリックで選ぶ (Alt+1 でこれだけ)\n右クリック / Alt+クリック: これだけ（もう一度で全部）"},
            {GizmoCategory::Sprite, ICON_FA_IMAGE, "スプライトをクリックで選ぶ (Alt+2 でこれだけ)\n右クリック / Alt+クリック: これだけ（もう一度で全部）"},
            {GizmoCategory::Particle, ICON_FA_STAR, "パーティクルをクリックで選ぶ (Alt+3 でこれだけ)\n右クリック / Alt+クリック: これだけ（もう一度で全部）"},
            {GizmoCategory::Light, ICON_FA_LIGHTBULB, "ライトをクリックで選ぶ (Alt+4 でこれだけ)\n右クリック / Alt+クリック: これだけ（もう一度で全部）"},
        };
        for (int i = 0; i < kGizmoCategoryCount; ++i)
        {
            const CategoryItem &item = kCategoryItems[i];
            if (i > 0)
                ImGui::SameLine();
            bool solo = false;
            if (CategoryButton(item.icon, item.icon, LabelAccent(item.category), gizmo->IsCategoryEnabled(item.category), item.tooltip, solo))
            {
                if (solo)
                    gizmo->SoloCategory(item.category);
                else
                    gizmo->ToggleCategory(item.category);
            }
        }

        // 直前のクリックで重なっていた数（Tab で順に選べることを知らせる）
        const int overlapCount = gizmo->GetOverlapCount();
        if (overlapCount > 1)
        {
            ImGui::SameLine();
            const std::string label = std::format(ICON_FA_LAYER_GROUP " {}件重なり {}/{}  Tab##overlap", overlapCount,
                                                  gizmo->GetOverlapIndex() + 1, overlapCount);
            {
                ScopedButtonColors colors(ImVec4(0.0f, 0.0f, 0.0f, 0.0f), DebugTheme::kButtonGhostHover);
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentYellow);
                if (ImGui::Button(label.c_str(), ImVec2(0.0f, ImGui::GetFrameHeight())))
                    gizmo->CycleOverlap();
                ImGui::PopStyleColor();
            }
            ImGui::SetItemTooltip("クリックした場所に重なっている物を順に選びます（Tab と同じ）");
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);

    // ---- 右上: 軸の向き表示 ----
    const ViewProjection *viewProjection = gizmo->GetViewProjection();
    if (showViewAxis_ && viewProjection)
    {
        constexpr float kWidgetSize = 92.0f;
        constexpr float kRadius = 30.0f;
        const ImVec2 widgetMin = ImVec2(imageMax.x - kWidgetSize - 8.0f, imageMin.y + 8.0f);
        ImGui::SetCursorScreenPos(widgetMin);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("##sceneAxis", ImVec2(kWidgetSize, kWidgetSize), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        ImDrawList *axisDraw = ImGui::GetWindowDrawList();
        const ImVec2 center = ImVec2(widgetMin.x + kWidgetSize * 0.5f, widgetMin.y + kWidgetSize * 0.5f);
        const bool widgetHovered = ImGui::IsWindowHovered();
        axisDraw->AddCircleFilled(center, kRadius + 14.0f,
                                  ImGui::ColorConvertFloat4ToU32(ImVec4(0.07f, 0.07f, 0.09f, widgetHovered ? 0.70f : 0.40f)), 32);

        struct AxisBall
        {
            Vector3 world;   // ワールドの軸方向
            ImVec2 screen;   // 画面上の位置
            float depth;     // 奥行き（大きいほど奥）
            ImU32 color;
            const char *label;
            bool positive;
        };
        const ImU32 colors[3] = {IM_COL32(222, 96, 96, 255), IM_COL32(120, 196, 110, 255), IM_COL32(96, 146, 232, 255)};
        const char *labels[3] = {"X", "Y", "Z"};
        std::vector<AxisBall> balls;
        balls.reserve(6);
        const Matrix4x4 &view = viewProjection->matView_;
        for (int axis = 0; axis < 3; ++axis)
        {
            for (int sign = 0; sign < 2; ++sign)
            {
                Vector3 world = {0.0f, 0.0f, 0.0f};
                (axis == 0 ? world.x : (axis == 1 ? world.y : world.z)) = sign == 0 ? 1.0f : -1.0f;
                const Vector3 v = TransformNormal(world, view);
                AxisBall ball;
                ball.world = world;
                ball.screen = ImVec2(center.x + v.x * kRadius, center.y - v.y * kRadius);
                ball.depth = v.z;
                ball.color = colors[axis];
                ball.label = labels[axis];
                ball.positive = (sign == 0);
                balls.push_back(ball);
            }
        }
        // 奥の玉から描いて、手前の玉が上に来るようにする
        std::sort(balls.begin(), balls.end(), [](const AxisBall &a, const AxisBall &b) { return a.depth > b.depth; });

        int clickedBall = -1;
        for (int i = 0; i < static_cast<int>(balls.size()); ++i)
        {
            const AxisBall &ball = balls[i];
            const float ballRadius = ball.positive ? 8.0f : 6.0f;
            if (ball.positive)
            {
                axisDraw->AddLine(center, ball.screen, ball.color, 2.0f);
            }
            ImGui::SetCursorScreenPos(ImVec2(ball.screen.x - ballRadius, ball.screen.y - ballRadius));
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##axisBall", ImVec2(ballRadius * 2.0f, ballRadius * 2.0f)))
            {
                clickedBall = i;
            }
            const bool hovered = ImGui::IsItemHovered();
            if (hovered)
            {
                ImGui::SetTooltip("%s%s 側から見る", ball.positive ? "+" : "-", ball.label);
            }
            ImGui::PopID();

            if (ball.positive)
            {
                axisDraw->AddCircleFilled(ball.screen, ballRadius + (hovered ? 1.5f : 0.0f), ball.color, 16);
                const ImVec2 textSize = ImGui::CalcTextSize(ball.label);
                axisDraw->AddText(ImVec2(ball.screen.x - textSize.x * 0.5f, ball.screen.y - textSize.y * 0.5f),
                                  IM_COL32(18, 18, 22, 255), ball.label);
            }
            else
            {
                const ImU32 faded = (ball.color & 0x00FFFFFF) | (hovered ? 0xE0000000 : 0x70000000);
                axisDraw->AddCircleFilled(ball.screen, ballRadius, faded, 16);
                axisDraw->AddCircle(ball.screen, ballRadius, ball.color, 16, 1.2f);
            }
        }

        if (clickedBall >= 0)
        {
            // 玉の側から中心を見る＝視線はその軸の逆向き
            const Vector3 direction = balls[clickedBall].world * -1.0f;
            float pitch = 0.0f;
            float yaw = 0.0f;
            DirectionToPitchYaw(direction, pitch, yaw);
            // 真上・真下から見るときは今のヨーを保ち、画面の向きが急に回らないようにする
            if (std::abs(direction.y) > 0.99f)
            {
                const Matrix4x4 &cameraWorld = viewProjection->matWorld_;
                const Vector3 forward = {cameraWorld.m[2][0], cameraWorld.m[2][1], cameraWorld.m[2][2]};
                float unusedPitch = 0.0f;
                DirectionToPitchYaw(forward.Normalize(), unusedPitch, yaw);
            }
            // デバッグカメラでなければ切り替えてから向ける（ゲームのカメラは動かさない）
            if (pCurrentScene_ && !pCurrentScene_->IsDebugCameraActive())
            {
                pCurrentScene_->ToggleDebugCamera();
            }
            gizmo->RequestViewAlign(pitch, yaw);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
}

// ---- 右クリックメニュー -----------------------------------------------------

void ImGuiManager::DrawSceneContextMenu(bool sceneHovered)
{
    const ImGuiIO &io = ImGui::GetIO();
    // 右ドラッグは視点の回転なので、ほとんど動かさずに離したときだけ開く
    constexpr float kClickThresholdSqr = 5.0f * 5.0f;
    if (sceneHovered && !ImGuizmo::IsUsing() && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < kClickThresholdSqr)
    {
        sceneContextPosition_ = pImGuizmoManager_->GetSpawnPositionUnderCursor();
        ImGui::OpenPopup("##sceneContext");
    }
    if (!ImGui::BeginPopup("##sceneContext"))
    {
        return;
    }
    ImGuizmoManager *gizmo = pImGuizmoManager_;
    const Vector3 &point = sceneContextPosition_;
    ImGui::TextDisabled(ICON_FA_MAP_MARKER_ALT " (%.1f, %.1f, %.1f)", point.x, point.y, point.z);
    ImGui::Separator();

    // ---- 選択中の物への操作 ----
    const size_t selectedCount = gizmo->GetSelectedNames().size();
    if (selectedCount > 0)
    {
        ImGui::TextDisabled("%s", selectedCount == 1 ? gizmo->GetSelectedNames().begin()->c_str()
                                                     : std::format("{} 個を選択中", selectedCount).c_str());
        if (ImGui::MenuItem(ICON_FA_CROSSHAIRS " カメラを寄せる", "F"))
            gizmo->FocusOnSelection();
        if (ImGui::MenuItem(ICON_FA_MAP_MARKER_ALT " ここへ移動"))
            gizmo->MoveSelectionTo(point);
        if (ImGui::MenuItem(ICON_FA_ARROW_DOWN " 地面に接地"))
            gizmo->SnapSelectedToGround();
        if (ImGui::MenuItem(ICON_FA_CLONE " 複製", "Ctrl+D"))
            gizmo->DuplicateSelectedObjects();
        if (BaseObject *selected = gizmo->GetSelectedTarget(); selected && selectedCount == 1)
        {
            if (selected->GetPrefabSource().empty())
            {
                gizmo->DrawPrefabLinkMenuItems(selected);
            }
            else if (ImGui::BeginMenu((std::string(ICON_FA_BOX " プレハブ: ") + selected->GetPrefabSource()).c_str()))
            {
                gizmo->DrawPrefabLinkMenuItems(selected);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem(ICON_FA_TH " 並べて複製（配置ツール）"))
                showPlacementToolView_ = true;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
        if (ImGui::MenuItem(ICON_FA_TRASH_ALT " 削除", "Delete"))
            gizmo->DeleteSelectedObjects();
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    // ---- ここに置く ----
    if (ImGui::BeginMenu(ICON_FA_PLUS " ここに置く"))
    {
        struct PrimitiveItem
        {
            const char *label;
            PrimitiveType type;
            const char *baseName;
        };
        static const PrimitiveItem kPrimitives[] = {
            {"キューブ", PrimitiveType::Cube, "cube"},       {"球体", PrimitiveType::Sphere, "sphere"},
            {"平面", PrimitiveType::Plane, "plane"},         {"シリンダー", PrimitiveType::Cylinder, "cylinder"},
            {"円錐", PrimitiveType::Cone, "cone"},           {"ピラミッド", PrimitiveType::Pyramid, "pyramid"},
            {"リング", PrimitiveType::Ring, "ring"},         {"岩", PrimitiveType::Rock, "rock"},
        };
        ImGui::SeparatorText("プリミティブ");
        for (const PrimitiveItem &item : kPrimitives)
        {
            if (ImGui::MenuItem((std::string(ICON_FA_CUBE " ") + item.label).c_str()))
                gizmo->PlacePrimitive(item.type, item.baseName, point);
        }
        const std::vector<std::string> prefabs = pBaseObjectManager_->ListPrefabNames();
        ImGui::SeparatorText("プレハブ");
        if (prefabs.empty())
        {
            ImGui::TextDisabled("（まだありません）");
        }
        for (const std::string &prefab : prefabs)
        {
            if (ImGui::MenuItem((std::string(ICON_FA_BOX " ") + prefab).c_str()))
                gizmo->PlacePrefab(prefab, point);
        }
        ImGui::EndMenu();
    }

    // ---- 視点 ----
    if (ImGui::BeginMenu(ICON_FA_VIDEO " 視点"))
    {
        struct ViewItem
        {
            const char *label;
            Vector3 direction; // 視線の向き
        };
        static const ViewItem kViews[] = {
            {"正面から (+Z を見る)", {0.0f, 0.0f, 1.0f}}, {"後ろから (-Z を見る)", {0.0f, 0.0f, -1.0f}},
            {"右から (-X を見る)", {-1.0f, 0.0f, 0.0f}},  {"左から (+X を見る)", {1.0f, 0.0f, 0.0f}},
            {"真上から", {0.0f, -1.0f, 0.0f}},
        };
        for (const ViewItem &view : kViews)
        {
            if (ImGui::MenuItem(view.label))
            {
                float pitch = 0.0f;
                float yaw = 0.0f;
                DirectionToPitchYaw(view.direction, pitch, yaw);
                if (pCurrentScene_ && !pCurrentScene_->IsDebugCameraActive())
                    pCurrentScene_->ToggleDebugCamera();
                gizmo->RequestViewAlign(pitch, yaw);
            }
        }
        if (!cameraBookmarksLoaded_)
            LoadCameraBookmarks();
        auto it = cameraBookmarks_.find(overlaySceneName_);
        ImGui::SeparatorText("ブックマーク");
        bool any = false;
        if (it != cameraBookmarks_.end())
        {
            for (int i = 0; i < kCameraBookmarkCount; ++i)
            {
                if (!it->second[i].valid)
                    continue;
                any = true;
                if (ImGui::MenuItem(std::format("ブックマーク {}", i + 1).c_str(), std::format("Shift+{}", i + 1).c_str()))
                    RecallCameraBookmark(i);
            }
        }
        if (!any)
            ImGui::TextDisabled("（Ctrl+Shift+数字で保存）");
        ImGui::EndMenu();
    }

    // ---- 表示 ----
    if (ImGui::BeginMenu(ICON_FA_EYE " 表示"))
    {
        ImGui::MenuItem("床のグリッド", nullptr, &showGrid_);
        ImGui::MenuItem("ツールバー", nullptr, &showSceneOverlay_);
        ImGui::MenuItem("軸の向き表示", nullptr, &showViewAxis_);
        ImGui::MenuItem("アイコン（ライト・カメラ等）", nullptr, &showSceneIcons_);
        ImGui::MenuItem("距離の計測", nullptr, &showSceneMeasure_);
        ImGui::MenuItem("構図ガイド: 三分割線", nullptr, &showThirdsGuide_);
        ImGui::MenuItem("構図ガイド: セーフエリア", nullptr, &showSafeAreaGuide_);
        ImGui::SeparatorText("名前ラベル");
        if (ImGui::MenuItem("出さない", nullptr, sceneLabelMode_ == 0))
            sceneLabelMode_ = 0;
        if (ImGui::MenuItem("選択中だけ", nullptr, sceneLabelMode_ == 1))
            sceneLabelMode_ = 1;
        if (ImGui::MenuItem("すべて", nullptr, sceneLabelMode_ == 2))
            sceneLabelMode_ = 2;
        ImGui::EndMenu();
    }
    ImGui::EndPopup();
}

// ---- 名前ラベル・距離の計測 -----------------------------------------------

void ImGuiManager::DrawSceneLabels(const ImVec2 &imageMin, const ImVec2 &imageSize)
{
    const ViewProjection *viewProjection = pImGuizmoManager_->GetViewProjection();
    if (!viewProjection || (sceneLabelMode_ == 0 && !showSceneMeasure_))
    {
        return;
    }
    const Matrix4x4 matrix = viewProjection->matView_ * viewProjection->matProjection_;
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), true);

    // ---- 距離（選択中の物を名前順に結ぶ）----
    const std::vector<ImGuizmoManager::LabelTarget> selected = pImGuizmoManager_->CollectLabelTargets(true);
    if (showSceneMeasure_ && selected.size() >= 2)
    {
        constexpr size_t kMaxSegments = 8; // 大量に選んだときに線だらけにしない
        const ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(DebugTheme::kAccentYellow);
        for (size_t i = 0; i + 1 < selected.size() && i < kMaxSegments; ++i)
        {
            const Vector3 &a = selected[i].position;
            const Vector3 &b = selected[i + 1].position;
            ImVec2 screenA;
            ImVec2 screenB;
            const bool visibleA = ProjectToImage(matrix, a, imageMin, imageSize, screenA);
            const bool visibleB = ProjectToImage(matrix, b, imageMin, imageSize, screenB);
            if (!visibleA || !visibleB)
            {
                continue;
            }
            drawList->AddLine(screenA, screenB, lineColor, 1.5f);
            drawList->AddCircleFilled(screenA, 3.0f, lineColor, 8);
            drawList->AddCircleFilled(screenB, 3.0f, lineColor, 8);

            const Vector3 delta = b - a;
            const std::string text = std::format("{:.2f}  (Δx {:.1f} / Δy {:.1f} / Δz {:.1f})", delta.Length(), delta.x, delta.y, delta.z);
            const ImVec2 middle = ImVec2((screenA.x + screenB.x) * 0.5f, (screenA.y + screenB.y) * 0.5f);
            const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
            const ImVec2 boxMin = ImVec2(middle.x - textSize.x * 0.5f - 5.0f, middle.y - textSize.y * 0.5f - 2.0f);
            const ImVec2 boxMax = ImVec2(middle.x + textSize.x * 0.5f + 5.0f, middle.y + textSize.y * 0.5f + 2.0f);
            drawList->AddRectFilled(boxMin, boxMax, IM_COL32(18, 18, 22, 220), 4.0f);
            drawList->AddRect(boxMin, boxMax, lineColor, 4.0f);
            drawList->AddText(ImVec2(boxMin.x + 5.0f, boxMin.y + 2.0f), lineColor, text.c_str());
        }
    }

    // ---- 名前ラベル ----
    if (sceneLabelMode_ != 0)
    {
        const std::vector<ImGuizmoManager::LabelTarget> targets =
            (sceneLabelMode_ == 1) ? selected : pImGuizmoManager_->CollectLabelTargets(false);
        constexpr size_t kMaxLabels = 256;
        size_t drawn = 0;
        // 選択していない物を先に、選択中を後から（上に）描く
        for (int pass = 0; pass < 2; ++pass)
        {
            for (const ImGuizmoManager::LabelTarget &target : targets)
            {
                if (target.selected != (pass == 1) || drawn >= kMaxLabels)
                {
                    continue;
                }
                ImVec2 screen;
                if (!ProjectToImage(matrix, target.position, imageMin, imageSize, screen))
                {
                    continue;
                }
                DrawPill(drawList, ImVec2(screen.x, screen.y - 10.0f), target.name.c_str(), LabelAccent(target.category), target.selected);
                ++drawn;
            }
        }
    }
    drawList->PopClipRect();
}

// ---- カメラのブックマーク ---------------------------------------------------

namespace {
std::string CameraBookmarkPath()
{
    return AssetPath::Json("ImGuiSetting/CameraBookmarks.json");
}
} // namespace

void ImGuiManager::LoadCameraBookmarks()
{
    cameraBookmarksLoaded_ = true;
    std::ifstream file(CameraBookmarkPath());
    if (!file)
    {
        return;
    }
    const nlohmann::json data = nlohmann::json::parse(file, nullptr, false);
    if (data.is_discarded() || !data.is_object())
    {
        return;
    }
    for (auto it = data.begin(); it != data.end(); ++it)
    {
        if (!it.value().is_array())
        {
            continue;
        }
        auto &slots = cameraBookmarks_[it.key()];
        for (const nlohmann::json &entry : it.value())
        {
            const int slot = entry.value("slot", -1);
            if (slot < 0 || slot >= kCameraBookmarkCount)
            {
                continue;
            }
            slots[slot].valid = true;
            slots[slot].position = entry.value("position", Vector3{});
            slots[slot].rotation = entry.value("rotation", Vector3{});
        }
    }
}

void ImGuiManager::SaveCameraBookmarks() const
{
    nlohmann::json data = nlohmann::json::object();
    for (const auto &[sceneName, slots] : cameraBookmarks_)
    {
        nlohmann::json list = nlohmann::json::array();
        for (int i = 0; i < kCameraBookmarkCount; ++i)
        {
            if (!slots[i].valid)
            {
                continue;
            }
            nlohmann::json entry;
            entry["slot"] = i;
            entry["position"] = slots[i].position;
            entry["rotation"] = slots[i].rotation;
            list.push_back(entry);
        }
        data[sceneName] = list;
    }
    std::ofstream file(CameraBookmarkPath());
    if (file)
    {
        file << data.dump(4);
    }
}

void ImGuiManager::StoreCameraBookmark(int slot)
{
    DebugCamera *debugCamera = pCurrentScene_ ? pCurrentScene_->GetDebugCamera() : nullptr;
    const ViewProjection *viewProjection = pImGuizmoManager_->GetViewProjection();
    if (slot < 0 || slot >= kCameraBookmarkCount || !debugCamera)
    {
        return;
    }
    if (!cameraBookmarksLoaded_)
    {
        LoadCameraBookmarks();
    }
    CameraBookmark &bookmark = cameraBookmarks_[overlaySceneName_][slot];
    if (debugCamera->GetActive())
    {
        bookmark.position = debugCamera->GetViewPosition();
        bookmark.rotation = debugCamera->GetViewRotation();
    }
    else if (viewProjection)
    {
        // ゲームのカメラで見ているときは、その構図を覚える（呼び出すとデバッグカメラで再現する）
        bookmark.position = viewProjection->translation_;
        bookmark.rotation = viewProjection->eulerRotation_;
    }
    bookmark.valid = true;
    SaveCameraBookmarks();
    ImGuiNotification::Post(std::format("カメラの位置をブックマーク {} に保存しました", slot + 1), {0.45f, 0.60f, 0.78f, 1.0f});
}

void ImGuiManager::RecallCameraBookmark(int slot)
{
    if (!cameraBookmarksLoaded_)
    {
        LoadCameraBookmarks();
    }
    auto it = cameraBookmarks_.find(overlaySceneName_);
    if (slot < 0 || slot >= kCameraBookmarkCount || it == cameraBookmarks_.end() || !it->second[slot].valid || !pCurrentScene_)
    {
        ImGuiNotification::Post(std::format("ブックマーク {} は空です（Ctrl+Shift+{} で保存）", slot + 1, slot + 1),
                                {0.82f, 0.58f, 0.36f, 1.0f});
        return;
    }
    DebugCamera *debugCamera = pCurrentScene_->GetDebugCamera();
    if (!debugCamera)
    {
        return;
    }
    if (!debugCamera->GetActive())
    {
        pCurrentScene_->ToggleDebugCamera();
    }
    // 実際に置くのは DebugCamera::Update（有効化の処理の後）
    debugCamera->SetView(it->second[slot].position, it->second[slot].rotation);
}

void ImGuiManager::DrawCameraBookmarkPopup()
{
    if (!ImGui::BeginPopup("##cameraBookmarks"))
    {
        return;
    }
    if (!cameraBookmarksLoaded_)
    {
        LoadCameraBookmarks();
    }
    ImGui::SeparatorText(std::format("カメラのブックマーク（{}）", overlaySceneName_).c_str());
    auto &slots = cameraBookmarks_[overlaySceneName_];
    bool changed = false;
    for (int i = 0; i < kCameraBookmarkCount; ++i)
    {
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(DebugTheme::kAccentBlue, "%d", i + 1);
        ImGui::SameLine();
        if (slots[i].valid)
        {
            if (PrimaryButton(ICON_FA_VIDEO " 移動"))
            {
                RecallCameraBookmark(i);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%.1f, %.1f, %.1f)", slots[i].position.x, slots[i].position.y, slots[i].position.z);
        }
        else
        {
            ImGui::TextDisabled("（空き）");
        }
        ImGui::SameLine(ImGui::GetFontSize() * 17.0f);
        if (NeutralButton(ICON_FA_SAVE " ここを保存"))
        {
            StoreCameraBookmark(i);
        }
        if (slots[i].valid)
        {
            ImGui::SameLine();
            ScopedButtonColors colors(DebugTheme::kButtonGhost, DebugTheme::kButtonDangerHover);
            if (ImGui::Button(ICON_FA_TIMES))
            {
                slots[i].valid = false;
                changed = true;
            }
            ImGui::SetItemTooltip("このブックマークを消す");
        }
        ImGui::PopID();
    }
    if (changed)
    {
        SaveCameraBookmarks();
    }
    ImGui::Separator();
    ImGui::TextDisabled("シーンにマウスがあるとき: Shift+数字で移動 / Ctrl+Shift+数字で保存");
    ImGui::EndPopup();
}

void ImGuiManager::HandleCameraBookmarkKeys(bool sceneHovered)
{
    if (!sceneHovered || ImGui::GetIO().WantTextInput)
    {
        return;
    }
    Input *input = Input::GetInstance();
    const bool shiftHeld = input->PushKey(DIK_LSHIFT) || input->PushKey(DIK_RSHIFT);
    const bool ctrlHeld = input->PushKey(DIK_LCONTROL) || input->PushKey(DIK_RCONTROL);
    if (!shiftHeld)
    {
        return;
    }
    // DIK_1〜DIK_9 は連番
    for (int i = 0; i < kCameraBookmarkCount; ++i)
    {
        if (input->TriggerKey(static_cast<BYTE>(DIK_1 + i)))
        {
            if (ctrlHeld)
                StoreCameraBookmark(i);
            else
                RecallCameraBookmark(i);
        }
    }
}

} // namespace Hagine
#endif // USE_IMGUI
