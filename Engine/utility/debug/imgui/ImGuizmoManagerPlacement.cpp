#define NOMINMAX
#ifdef USE_IMGUI
#include "ImGuizmoManager.h"
#include "ImGuiNotification.h"
#include <algorithm>
#include <format>
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>
#include <line/LineRenderer.h>
#include <numbers>
#include <object/base/BaseObjectManager.h>
#include <random>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "DebugUIHelper.h"
#include "ImGuizmoManagerInternal.h"

// =======================================================================
// ImGuizmoManager: 配置ツール
//
// 岩や柱のように「同じ物をたくさん置く」作業を、Ctrl+D とドラッグの繰り返しから解放する。
// 選択中のオブジェクトを元に、直線・格子・円・ばらまきの4通りで並べて複製する。
// 窓が開いている間は置く場所をシーンに下描きするので、作る前に配置が確かめられる。
// =======================================================================

namespace Hagine {
using GizmoInternal::RunAsUndoableCommand;

namespace {
constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;
constexpr int kMaxPlacementCount = 400; // 一度に作れる上限（押し間違いで数千個作らないように）
} // namespace

std::vector<ImGuizmoManager::PlacementPoint> ImGuizmoManager::BuildPlacementPoints(const Vector3 &origin) const
{
    std::vector<PlacementPoint> points;
    const PlacementSettings &s = placement_;

    switch (s.mode)
    {
    case 0: // 直線: 元の位置から step ずつずらして count 個
        for (int i = 1; i <= s.lineCount; ++i)
        {
            points.push_back({origin + s.lineStep * static_cast<float>(i)});
        }
        break;
    case 1: // 格子: 元の位置を角にして列×行（元の位置のマスは除く）
        for (int row = 0; row < s.gridRows; ++row)
        {
            for (int column = 0; column < s.gridColumns; ++column)
            {
                if (row == 0 && column == 0)
                {
                    continue;
                }
                points.push_back({origin + Vector3{s.gridSpacingX * static_cast<float>(column), 0.0f, s.gridSpacingZ * static_cast<float>(row)}});
            }
        }
        break;
    case 2: // 円: 元の位置を中心に等間隔
        for (int i = 0; i < s.ringCount; ++i)
        {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>((std::max)(s.ringCount, 1));
            PlacementPoint point;
            point.position = origin + Vector3{std::sin(angle) * s.ringRadius, 0.0f, std::cos(angle) * s.ringRadius};
            // 外向き（中心から離れる向き）に回す
            point.yawDegrees = s.ringFaceOutward ? angle / kDegToRad : 0.0f;
            points.push_back(point);
        }
        break;
    default: // ばらまき: 円の中に、互いに最小距離を空けて置く
    {
        std::mt19937 random(s.seed);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        const int attempts = s.scatterCount * 30;
        for (int attempt = 0; attempt < attempts && static_cast<int>(points.size()) < s.scatterCount; ++attempt)
        {
            // sqrt で面積あたり均一にする（そのままだと中心に寄る）
            const float radius = std::sqrt(unit(random)) * s.scatterRadius;
            const float angle = unit(random) * 2.0f * std::numbers::pi_v<float>;
            const Vector3 candidate = origin + Vector3{std::sin(angle) * radius, 0.0f, std::cos(angle) * radius};
            bool tooClose = (candidate - origin).Length() < s.scatterMinDistance;
            for (const PlacementPoint &placed : points)
            {
                if ((placed.position - candidate).Length() < s.scatterMinDistance)
                {
                    tooClose = true;
                    break;
                }
            }
            if (!tooClose)
            {
                points.push_back({candidate});
            }
        }
        break;
    }
    }

    // ランダムな向き・大きさ（シードが同じなら同じ結果。下描きと作成で一致させるため）
    std::mt19937 random(s.seed * 7919u + 17u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (PlacementPoint &point : points)
    {
        const float yawJitter = (unit(random) * 2.0f - 1.0f) * s.randomYawDegrees;
        point.yawDegrees += yawJitter;
        const float scaleMin = (std::min)(s.scaleMin, s.scaleMax);
        const float scaleMax = (std::max)(s.scaleMin, s.scaleMax);
        point.scale = scaleMin + (scaleMax - scaleMin) * unit(random);
    }

    if (static_cast<int>(points.size()) > kMaxPlacementCount)
    {
        points.resize(kMaxPlacementCount);
    }
    return points;
}

void ImGuizmoManager::ExecutePlacement(BaseObject *pSource)
{
    if (!pSource)
    {
        return;
    }
    const std::vector<PlacementPoint> points = BuildPlacementPoints(pSource->GetWorldPosition());
    if (points.empty())
    {
        return;
    }

    std::vector<std::string> created;
    const std::string sourceName = pSource->GetName();
    RunAsUndoableCommand("並べて複製", [&] {
        BaseObjectManager *manager = BaseObjectManager::GetInstance();
        for (const PlacementPoint &point : points)
        {
            BaseObject *clone = manager->CloneObject(pSource);
            if (!clone)
            {
                continue;
            }
            // 向きと大きさ（元の値に掛け合わせる）
            WorldTransform *transform = clone->GetWorldTransform();
            if (point.yawDegrees != 0.0f)
            {
                const Quaternion yaw = Quaternion::FromAxisAngle({0.0f, 1.0f, 0.0f}, point.yawDegrees * kDegToRad);
                transform->quaternionRotation_ = (yaw * transform->quaternionRotation_).Normalize();
            }
            transform->scale_ = transform->scale_ * point.scale;
            transform->UpdateMatrix();

            // 位置はワールドで指定する（元が親子付けされていても正しく置けるように）
            auto it = transformMap_.find(clone->GetName());
            if (it != transformMap_.end())
            {
                SetTargetWorldPosition(it->second, point.position);
            }
            created.push_back(clone->GetName());
        }
        if (!placement_.keepSource)
        {
            manager->RemoveObject(sourceName);
            transformMap_.erase(sourceName);
        }
    });

    // 作った物を選択状態にする（そのまま動かしたり、接地させたりできる）
    pinnedName_.clear();
    selectedNames_.clear();
    for (const std::string &name : created)
    {
        selectedNames_.insert(name);
    }
    UpdateFilteredNames();
    const std::string message = std::format("「{}」を {} 個並べました", sourceName, created.size());
    if (placement_.snapToGround)
    {
        // 接地は別の Undo になるので、「元に戻す」ボタンは付けない（履歴窓から2つ戻せる）
        SnapSelectedToGround();
        ImGuiNotification::Post(message, {0.45f, 0.68f, 0.52f, 1.0f});
    }
    else
    {
        GizmoInternal::PostUndoToast(message, {0.45f, 0.68f, 0.52f, 1.0f});
    }
}

void ImGuizmoManager::DrawPlacementTool()
{
    BaseObject *source = GetSelectedTarget();
    PlacementSettings &s = placement_;

    // ---- 元にするオブジェクト ----
    if (!source)
    {
        ImGui::Spacing();
        DimText(ICON_FA_MOUSE_POINTER " 並べたいオブジェクトを1つ選んでください");
        DimText("（シーンか階層でクリック）");
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(DebugTheme::kAccentBlue, ICON_FA_CUBE " %s", source->GetName().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("を元に並べます");
    if (selectedNames_.size() > 1)
    {
        ImGui::TextColored(DebugTheme::kAccentOrange, ICON_FA_EXCLAMATION_TRIANGLE " 複数選択中です。元にするのは先頭の1つだけです");
    }
    ImGui::Separator();

    // ---- 並べ方 ----
    static const char *kModeLabels[] = {ICON_FA_ELLIPSIS_H " 直線", ICON_FA_TH " 格子", ICON_FA_CIRCLE_NOTCH " 円", ICON_FA_BRAILLE " ばらまき"};
    for (int i = 0; i < 4; ++i)
    {
        if (i > 0)
            ImGui::SameLine();
        const bool active = (s.mode == i);
        ScopedButtonColors colors(active ? DebugTheme::kButtonPrimary : DebugTheme::kButtonNeutral,
                                  active ? DebugTheme::kButtonPrimaryHover : DebugTheme::kButtonNeutralHover);
        if (ImGui::Button(kModeLabels[i], ImVec2((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * (3 - i)) / static_cast<float>(4 - i), 0.0f)))
        {
            s.mode = i;
        }
    }
    ImGui::Spacing();

    ImGui::PushItemWidth(-ImGui::GetFontSize() * 7.0f);
    switch (s.mode)
    {
    case 0:
        ImGui::SliderInt("個数", &s.lineCount, 1, 64);
        ImGui::DragFloat3("間隔 (XYZ)", &s.lineStep.x, 0.05f);
        FloatNContextMenu("##lineStepCtx", &s.lineStep.x, 3);
        break;
    case 1:
        ImGui::SliderInt("列 (X)", &s.gridColumns, 1, 32);
        ImGui::SliderInt("行 (Z)", &s.gridRows, 1, 32);
        ImGui::DragFloat("X の間隔", &s.gridSpacingX, 0.05f, 0.01f, 1000.0f, "%.2f");
        ImGui::DragFloat("Z の間隔", &s.gridSpacingZ, 0.05f, 0.01f, 1000.0f, "%.2f");
        break;
    case 2:
        ImGui::SliderInt("個数", &s.ringCount, 2, 96);
        ImGui::DragFloat("半径", &s.ringRadius, 0.05f, 0.1f, 1000.0f, "%.2f");
        ImGui::Checkbox("外向きに回す", &s.ringFaceOutward);
        break;
    default:
        ImGui::SliderInt("個数", &s.scatterCount, 1, 200);
        ImGui::DragFloat("範囲の半径", &s.scatterRadius, 0.1f, 0.5f, 1000.0f, "%.1f");
        ImGui::DragFloat("最小の間隔", &s.scatterMinDistance, 0.05f, 0.0f, 100.0f, "%.2f");
        ImGui::SetItemTooltip("これより近くには置きません。大きすぎると指定の個数まで置けないことがあります");
        break;
    }

    ImGui::SeparatorText("ばらつき");
    ImGui::DragFloat("向き ±(度)", &s.randomYawDegrees, 0.5f, 0.0f, 180.0f, "%.0f°");
    float scaleRange[2] = {s.scaleMin, s.scaleMax};
    if (ImGui::DragFloat2("大きさ (最小/最大)", scaleRange, 0.01f, 0.05f, 20.0f, "×%.2f"))
    {
        s.scaleMin = scaleRange[0];
        s.scaleMax = scaleRange[1];
    }
    {
        int seed = static_cast<int>(s.seed);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 7.0f - ImGui::GetFrameHeight() -
                                ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::InputInt("##seed", &seed, 0))
        {
            s.seed = static_cast<uint32_t>((std::max)(seed, 0));
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_DICE "##reseed", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
        {
            s.seed = std::random_device{}() % 100000u;
        }
        ImGui::SetItemTooltip("別の並びを試す");
        ImGui::SameLine();
        ImGui::TextUnformatted("シード");
    }
    ImGui::PopItemWidth();

    ImGui::SeparatorText("仕上げ");
    ImGui::Checkbox("作ったら地面に接地させる", &s.snapToGround);
    ImGui::Checkbox("元のオブジェクトを残す", &s.keepSource);

    // ---- 下描き（置く場所をシーンに描く）----
    const std::vector<PlacementPoint> points = BuildPlacementPoints(source->GetWorldPosition());
    {
        LineRenderer *line = LineRenderer::GetInstance();
        const Vector4 color = {0.45f, 0.85f, 0.60f, 0.9f};
        const Vector3 origin = source->GetWorldPosition();
        for (const PlacementPoint &point : points)
        {
            const float size = 0.35f * point.scale;
            line->AddLine(point.position - Vector3{size, 0.0f, 0.0f}, point.position + Vector3{size, 0.0f, 0.0f}, color);
            line->AddLine(point.position - Vector3{0.0f, 0.0f, size}, point.position + Vector3{0.0f, 0.0f, size}, color);
            line->AddLine(point.position, point.position + Vector3{0.0f, size * 2.0f, 0.0f}, color);
            // 向き（前方向の短い線）
            const float yaw = point.yawDegrees * kDegToRad;
            line->AddLine(point.position, point.position + Vector3{std::sin(yaw), 0.0f, std::cos(yaw)} * (size * 1.8f),
                          {0.95f, 0.75f, 0.35f, 0.9f});
        }
        if (s.mode == 2 || s.mode == 3)
        {
            const float radius = (s.mode == 2) ? s.ringRadius : s.scatterRadius;
            line->AddCircle(origin, {radius, 0.0f, 0.0f}, {0.0f, 0.0f, radius}, {0.45f, 0.60f, 0.78f, 0.6f}, 48);
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%zu 個作ります（緑の十字が置く場所）", points.size());
    ImGui::BeginDisabled(points.empty());
    if (ConfirmButton(std::format(ICON_FA_CLONE " {} 個並べて複製", points.size()).c_str(), ImVec2(-1.0f, 0.0f)))
    {
        ExecutePlacement(source);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Ctrl+Z でまとめて取り消せます");
}

} // namespace Hagine
#endif // USE_IMGUI
