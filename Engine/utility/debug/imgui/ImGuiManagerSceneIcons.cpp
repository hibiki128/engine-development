#include "ImGuiManager.h"
#ifdef USE_IMGUI
// シーンビューのアイコン（ライト・カメラ・パーティクルのエミッター・フィールド）。
//   ・画面上で常に同じ大きさ（遠いほど少しだけ小さく・薄く）。押すとその物を選ぶ（Ctrl で追加・解除）
//   ・ダブルクリックでシーンのカメラを寄せる
//   ・選択中の物は影響範囲を線で描く（点光源=球 / スポット=円錐 / カメラ=視錐台 / フィールド=範囲と向き）
// クリックはギズモの Update より先に処理し、使ったクリックは ConsumeSceneClick で奥の物の選択に回さない。
#include "ImGuiManagerSceneOverlayInternal.h"
#include "ImGuiNotification.h"
#include "ImGuizmo.h"
#include <MyMath.h>
#include <algorithm>
#include <camera/CameraManager.h>
#include <cmath>
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>
#include <light/LightGroup.h>
#include <line/LineRenderer.h>
#include <particle/gpu/ParticleCSFieldManager.h>

namespace Hagine {
using namespace SceneOverlayInternal;
namespace {
// これ以上マウスが動いたらクリックではなくドラッグ（矩形選択など）とみなす（ピクセル）
constexpr float kIconClickThreshold = 6.0f;
// 基準のアイコン半径（ピクセル。倍率を掛ける前）
constexpr float kIconBaseRadius = 11.0f;

/// <summary>光の色をアイコンで見える明るさにそろえる（一番明るい成分を 1 に）</summary>
Vector4 NormalizeLightColor(const Vector4 &color)
{
    const float maxComponent = (std::max)({color.x, color.y, color.z, 0.001f});
    const float scale = 1.0f / maxComponent;
    return {(std::min)(color.x * scale, 1.0f), (std::min)(color.y * scale, 1.0f), (std::min)(color.z * scale, 1.0f), 1.0f};
}

ImVec4 ToImVec4(const Vector4 &v) { return ImVec4(v.x, v.y, v.z, v.w); }

/// <summary>向きに直交する2本の軸を作る（円や視錐台の断面を描く用）</summary>
void MakeBasis(const Vector3 &direction, Vector3 &outU, Vector3 &outV)
{
    const Vector3 up = (std::abs(direction.y) > 0.95f) ? Vector3{1.0f, 0.0f, 0.0f} : Vector3{0.0f, 1.0f, 0.0f};
    outU = direction.Cross(up).Normalize();
    outV = outU.Cross(direction).Normalize();
}
} // namespace

// ---- 集める・クリック -------------------------------------------------------

void ImGuiManager::UpdateSceneIcons(const ImVec2 &imageMin, const ImVec2 &imageSize, bool sceneHovered)
{
    sceneIcons_.clear();
    hoveredSceneIcon_ = -1;
    ImGuizmoManager *gizmo = pImGuizmoManager_;
    const ViewProjection *viewProjection = gizmo ? gizmo->GetViewProjection() : nullptr;
    if (!showSceneIcons_ || !viewProjection)
    {
        pressedSceneIcon_ = -1;
        return;
    }

    const Matrix4x4 matrix = viewProjection->matView_ * viewProjection->matProjection_;
    const Matrix4x4 &cameraWorld = viewProjection->matWorld_;
    const Vector3 cameraPosition = {cameraWorld.m[3][0], cameraWorld.m[3][1], cameraWorld.m[3][2]};

    auto addIcon = [&](SceneIcon icon) {
        if ((sceneIconKindMask_ & (1 << static_cast<int>(icon.kind))) == 0)
        {
            return;
        }
        const float distance = (icon.position - cameraPosition).Length();
        // 今そのカメラから見ている（中に入っている）ものは出さない
        if (distance < 0.3f || !ProjectToImage(matrix, icon.position, imageMin, imageSize, icon.screen))
        {
            return;
        }
        const float distanceScale = std::clamp(1.15f - distance / 120.0f, 0.65f, 1.0f);
        icon.radius = kIconBaseRadius * sceneIconScale_ * distanceScale;
        icon.alpha *= std::clamp(1.1f - distance / 150.0f, 0.45f, 1.0f);
        icon.depth = distance;
        sceneIcons_.push_back(std::move(icon));
    };

    // ---- 点光源・スポット ----
    LightGroup *lights = LightGroup::GetInstance();
    const bool lightPickable = gizmo->IsCategoryEnabled(GizmoCategory::Light);
    const std::vector<PointLightGroup::Entry> &points = lights->GetPointLights().GetEntries();
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        const PointLightGroup::Entry &entry = points[i];
        SceneIcon icon;
        icon.kind = SceneIconKind::PointLight;
        icon.position = entry.gpu.position;
        icon.color = NormalizeLightColor(entry.gpu.color);
        icon.glyph = ICON_FA_LIGHTBULB;
        icon.gizmoName = lights->PointGizmoName(i);
        icon.label = entry.name;
        icon.selected = gizmo->IsSelected(icon.gizmoName);
        icon.pickable = lightPickable;
        icon.alpha = entry.gpu.active ? 1.0f : 0.45f;
        icon.index = i;
        addIcon(std::move(icon));
    }
    const std::vector<SpotLightGroup::Entry> &spots = lights->GetSpotLights().GetEntries();
    for (int i = 0; i < static_cast<int>(spots.size()); ++i)
    {
        const SpotLightGroup::Entry &entry = spots[i];
        SceneIcon icon;
        icon.kind = SceneIconKind::SpotLight;
        icon.position = entry.gpu.position;
        icon.direction = entry.gpu.direction;
        icon.color = NormalizeLightColor(entry.gpu.color);
        icon.glyph = ICON_FA_LIGHTBULB;
        icon.gizmoName = lights->SpotGizmoName(i);
        icon.label = entry.name;
        icon.selected = gizmo->IsSelected(icon.gizmoName) || gizmo->IsSelected(lights->SpotAimGizmoName(i));
        icon.pickable = lightPickable;
        icon.alpha = entry.gpu.active ? 1.0f : 0.45f;
        icon.index = i;
        addIcon(std::move(icon));
    }

    // ---- カメラ（ギズモ未登録。選択はこちらで覚える）----
    CameraManager *cameras = CameraManager::GetInstance();
    const Camera *activeCamera = cameras->GetActive();
    for (const std::string &name : cameras->GetCameraNames())
    {
        Camera *camera = cameras->Find(name);
        if (!camera)
        {
            continue;
        }
        const bool active = (camera == activeCamera);
        SceneIcon icon;
        icon.kind = SceneIconKind::Camera;
        icon.position = camera->GetPosition();
        icon.direction = camera->GetForward();
        icon.color = active ? Vector4{0.95f, 0.85f, 0.45f, 1.0f} : Vector4{0.72f, 0.80f, 0.92f, 1.0f};
        icon.glyph = ICON_FA_VIDEO;
        icon.cameraName = name;
        icon.label = active ? name + "（使用中）" : name;
        icon.selected = (selectedCameraIcon_ == name);
        addIcon(std::move(icon));
    }

    // ---- パーティクルのエミッター ----
    const bool particlePickable = gizmo->IsCategoryEnabled(GizmoCategory::Particle);
    for (const ImGuizmoManager::LabelTarget &target : gizmo->CollectIconTargets(GizmoCategory::Particle))
    {
        SceneIcon icon;
        icon.kind = SceneIconKind::Emitter;
        icon.position = target.position;
        icon.color = {DebugTheme::kAccentOrange.x, DebugTheme::kAccentOrange.y, DebugTheme::kAccentOrange.z, 1.0f};
        icon.glyph = ICON_FA_STAR;
        icon.gizmoName = target.name;
        icon.label = target.name;
        icon.selected = target.selected;
        icon.pickable = particlePickable;
        addIcon(std::move(icon));
    }

    // ---- フィールド（代表の効果で絵と色を変える）----
    ParticleCSFieldManager *fields = ParticleCSFieldManager::GetInstance();
    std::vector<ParticleField> &fieldList = fields->GetFields();
    for (int i = 0; i < static_cast<int>(fieldList.size()); ++i)
    {
        const ParticleField &field = fieldList[i];
        SceneIcon icon;
        icon.kind = SceneIconKind::Field;
        icon.position = field.position;
        icon.color = ParticleCSFieldManager::FieldColor(field);
        icon.glyph = ParticleCSFieldManager::FieldIcon(field);
        icon.gizmoName = ParticleCSFieldManager::GizmoName(field.name);
        icon.label = field.name;
        icon.selected = gizmo->IsSelected(icon.gizmoName);
        icon.pickable = particlePickable;
        icon.alpha = field.enabled ? 1.0f : 0.45f;
        icon.index = i;
        addIcon(std::move(icon));
    }

    // 奥から描く（手前のアイコンが上に来る）
    std::sort(sceneIcons_.begin(), sceneIcons_.end(), [](const SceneIcon &a, const SceneIcon &b) { return a.depth > b.depth; });

    // ---- マウスが乗っているアイコン（重なっていたら手前を優先）----
    const bool canInteract = sceneHovered && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
    if (canInteract)
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        for (int i = static_cast<int>(sceneIcons_.size()) - 1; i >= 0; --i)
        {
            const SceneIcon &icon = sceneIcons_[i];
            if (!icon.pickable)
            {
                continue;
            }
            const float dx = mouse.x - icon.screen.x;
            const float dy = mouse.y - icon.screen.y;
            const float hitRadius = icon.radius + 2.0f;
            if (dx * dx + dy * dy <= hitRadius * hitRadius)
            {
                hoveredSceneIcon_ = i;
                break;
            }
        }
    }

    // ---- クリック（押して、動かさずに離したら選ぶ）----
    // 押したアイコンは名前で覚える（並びは毎フレーム作り直すので番号では追えない）
    static std::string pressedKey;
    auto keyOf = [](const SceneIcon &icon) { return icon.gizmoName.empty() ? "camera:" + icon.cameraName : icon.gizmoName; };
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && sceneHovered)
    {
        pressedSceneIcon_ = hoveredSceneIcon_;
        pressedKey = (hoveredSceneIcon_ >= 0) ? keyOf(sceneIcons_[hoveredSceneIcon_]) : std::string();

        // ダブルクリック: 選んでからシーンのカメラを寄せる
        if (hoveredSceneIcon_ >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            const SceneIcon &icon = sceneIcons_[hoveredSceneIcon_];
            float radius = 1.5f;
            if (icon.kind == SceneIconKind::PointLight && icon.index >= 0 && icon.index < static_cast<int>(points.size()))
            {
                radius = (std::max)(points[icon.index].gpu.radius * 0.5f, 1.5f);
            }
            else if (icon.kind == SceneIconKind::Field && icon.index >= 0 && icon.index < static_cast<int>(fieldList.size()))
            {
                radius = (std::max)(fieldList[icon.index].radius * 0.5f, 1.5f);
            }
            if (pCurrentScene_ && !pCurrentScene_->IsDebugCameraActive())
            {
                pCurrentScene_->ToggleDebugCamera();
            }
            gizmo->RequestFocus(icon.position, radius);
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        const float dragSq = ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Left];
        const bool isClick = dragSq < kIconClickThreshold * kIconClickThreshold;
        if (isClick && pressedSceneIcon_ >= 0 && hoveredSceneIcon_ >= 0 && keyOf(sceneIcons_[hoveredSceneIcon_]) == pressedKey)
        {
            const SceneIcon &icon = sceneIcons_[hoveredSceneIcon_];
            const bool additive = ImGui::GetIO().KeyCtrl;
            if (icon.kind == SceneIconKind::Camera)
            {
                selectedCameraIcon_ = (additive && selectedCameraIcon_ == icon.cameraName) ? std::string() : icon.cameraName;
                cameras->SetSelectedName(icon.cameraName);
                if (!additive)
                {
                    gizmo->SelectOnly(std::string()); // ギズモの選択は外す
                }
            }
            else
            {
                if (!additive)
                {
                    selectedCameraIcon_.clear();
                    gizmo->SelectOnly(icon.gizmoName);
                }
                else
                {
                    gizmo->ToggleSelect(icon.gizmoName);
                }
            }
            gizmo->ConsumeSceneClick();
        }
        else if (isClick && sceneHovered && pressedSceneIcon_ < 0 && !ImGui::GetIO().KeyCtrl)
        {
            // 何もない所をクリックしたらカメラの選択も外す（ギズモ側の選択はギズモが処理する）
            selectedCameraIcon_.clear();
        }
        pressedSceneIcon_ = -1;
        pressedKey.clear();
    }

    DrawSelectedIconRanges();
}

// ---- 範囲の線 ---------------------------------------------------------------

void ImGuiManager::DrawSelectedIconRanges()
{
    LineRenderer *line = LineRenderer::GetInstance();
    LightGroup *lights = LightGroup::GetInstance();
    for (const SceneIcon &icon : sceneIcons_)
    {
        if (!icon.selected)
        {
            continue;
        }
        Vector4 color = icon.color;
        color.w = 0.85f;
        switch (icon.kind)
        {
        case SceneIconKind::PointLight:
        {
            const auto &points = lights->GetPointLights().GetEntries();
            if (icon.index >= 0 && icon.index < static_cast<int>(points.size()))
            {
                line->AddSphere(icon.position, points[icon.index].gpu.radius, color, 24);
            }
            break;
        }
        case SceneIconKind::SpotLight:
        {
            const auto &spots = lights->GetSpotLights().GetEntries();
            if (icon.index < 0 || icon.index >= static_cast<int>(spots.size()))
            {
                break;
            }
            const SpotLightData &spot = spots[icon.index].gpu;
            const Vector3 direction = spot.direction.Length() > 1e-4f ? spot.direction.Normalize() : Vector3{0.0f, -1.0f, 0.0f};
            const float cosAngle = std::clamp(spot.cosAngle, -0.999f, 0.999f);
            const float angle = std::acos(cosAngle);
            const float length = (std::max)(spot.distance, 0.1f);
            // 円錐の底（照射距離の位置の円）と、頂点から底への4本の線
            const float baseRadius = length * std::tan((std::min)(angle, 1.45f));
            Vector3 u;
            Vector3 v;
            MakeBasis(direction, u, v);
            const Vector3 baseCenter = icon.position + direction * length;
            line->AddCircle(baseCenter, u * baseRadius, v * baseRadius, color, 32);
            for (const Vector3 &side : {u, u * -1.0f, v, v * -1.0f})
            {
                line->AddLine(icon.position, baseCenter + side * baseRadius, color);
            }
            line->AddLine(icon.position, baseCenter, Vector4{color.x, color.y, color.z, 0.4f});
            break;
        }
        case SceneIconKind::Camera:
        {
            const Camera *camera = CameraManager::GetInstance()->Find(icon.cameraName);
            if (!camera)
            {
                break;
            }
            const ViewProjection &projection = camera->GetViewProjection();
            const Vector3 forward = camera->GetForward().Normalize();
            const Vector3 right = camera->GetRight().Normalize();
            const Vector3 up = camera->GetUp().Normalize();
            // far は 1000 などと大きいことが多いので、線は見やすい距離で打ち切る
            const float nearZ = (std::max)(projection.nearZ_, 0.01f);
            const float farZ = (std::min)(projection.farZ_, 25.0f);
            const float tanY = std::tan(projection.fovAngleY_ * 0.5f);
            const float tanX = tanY * projection.aspectRatio;
            auto corners = [&](float depth, Vector3 out[4]) {
                const Vector3 center = icon.position + forward * depth;
                const Vector3 x = right * (tanX * depth);
                const Vector3 y = up * (tanY * depth);
                out[0] = center - x + y;
                out[1] = center + x + y;
                out[2] = center + x - y;
                out[3] = center - x - y;
            };
            Vector3 nearCorners[4];
            Vector3 farCorners[4];
            corners(nearZ, nearCorners);
            corners(farZ, farCorners);
            line->AddPolyline(nearCorners, 4, color, true);
            line->AddPolyline(farCorners, 4, color, true);
            for (int c = 0; c < 4; ++c)
            {
                line->AddLine(icon.position, farCorners[c], Vector4{color.x, color.y, color.z, 0.5f});
            }
            // 上の向き（画面の上がどちらか分かるように、遠い面の上辺に三角を付ける）
            const Vector3 topMiddle = (farCorners[0] + farCorners[1]) * 0.5f;
            const Vector3 peak = topMiddle + up * (tanY * farZ * 0.25f);
            line->AddLine(farCorners[0] + (farCorners[1] - farCorners[0]) * 0.35f, peak, color);
            line->AddLine(farCorners[0] + (farCorners[1] - farCorners[0]) * 0.65f, peak, color);
            break;
        }
        case SceneIconKind::Field:
            ParticleCSFieldManager::GetInstance()->DrawFieldGizmo(icon.index);
            break;
        default:
            break;
        }
    }
}

// ---- 描く -------------------------------------------------------------------

void ImGuiManager::DrawSceneIcons(const ImVec2 &imageMin, const ImVec2 &imageSize)
{
    if (!showSceneIcons_ || sceneIcons_.empty())
    {
        return;
    }
    const ViewProjection *viewProjection = pImGuizmoManager_->GetViewProjection();
    if (!viewProjection)
    {
        return;
    }
    const Matrix4x4 matrix = viewProjection->matView_ * viewProjection->matProjection_;
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), true);
    ImFont *font = ImGui::GetFont();

    for (int i = 0; i < static_cast<int>(sceneIcons_.size()); ++i)
    {
        const SceneIcon &icon = sceneIcons_[i];
        const bool hovered = (i == hoveredSceneIcon_);
        const float alpha = icon.alpha * (icon.pickable ? 1.0f : 0.5f);
        const float radius = icon.radius * (hovered ? 1.12f : 1.0f);
        ImVec4 accent = ToImVec4(icon.color);
        accent.w = alpha;

        // 丸い地と縁（選択中は白い太い縁）
        drawList->AddCircleFilled(icon.screen, radius, ImGui::ColorConvertFloat4ToU32(ImVec4(0.07f, 0.07f, 0.09f, 0.78f * alpha)), 20);
        if (icon.selected)
        {
            drawList->AddCircle(icon.screen, radius + 2.0f, IM_COL32(255, 255, 255, static_cast<int>(230 * alpha)), 20, 2.0f);
        }
        drawList->AddCircle(icon.screen, radius, ImGui::ColorConvertFloat4ToU32(accent), 20, hovered ? 2.0f : 1.2f);

        // 向きの印（スポット・カメラ）: 縁の外に小さな三角
        if (icon.direction.Length() > 1e-4f)
        {
            ImVec2 tip;
            if (ProjectToImage(matrix, icon.position + icon.direction.Normalize() * 1.0f, imageMin, imageSize, tip))
            {
                float dx = tip.x - icon.screen.x;
                float dy = tip.y - icon.screen.y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length > 1.0f)
                {
                    dx /= length;
                    dy /= length;
                    const ImVec2 point = ImVec2(icon.screen.x + dx * (radius + 7.0f), icon.screen.y + dy * (radius + 7.0f));
                    const ImVec2 baseCenter = ImVec2(icon.screen.x + dx * (radius + 1.0f), icon.screen.y + dy * (radius + 1.0f));
                    const ImVec2 side = ImVec2(-dy * 4.5f, dx * 4.5f);
                    drawList->AddTriangleFilled(point, ImVec2(baseCenter.x + side.x, baseCenter.y + side.y),
                                                ImVec2(baseCenter.x - side.x, baseCenter.y - side.y), ImGui::ColorConvertFloat4ToU32(accent));
                }
            }
        }

        // 絵（Font Awesome の文字）
        const float fontSize = radius * 1.05f;
        const ImVec2 glyphSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, icon.glyph);
        drawList->AddText(font, fontSize, ImVec2(icon.screen.x - glyphSize.x * 0.5f, icon.screen.y - glyphSize.y * 0.5f),
                          ImGui::ColorConvertFloat4ToU32(accent), icon.glyph);

        // 名前（選択中・ホバー中だけ。名前ラベルが「すべて」のときは重なるので選択中の札に任せる）
        if ((icon.selected || hovered) && !icon.label.empty())
        {
            ImVec4 pillAccent = ToImVec4(icon.color);
            DrawPill(drawList, ImVec2(icon.screen.x, icon.screen.y - radius - 5.0f), icon.label.c_str(), pillAccent, icon.selected);
        }
    }
    drawList->PopClipRect();

    if (hoveredSceneIcon_ >= 0 && hoveredSceneIcon_ < static_cast<int>(sceneIcons_.size()))
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
}

// ---- 設定（ツールバーのポップアップ）-----------------------------------------

void ImGuiManager::DrawSceneIconSettings()
{
    ImGui::SeparatorText("シーンのアイコン");
    ImGui::Checkbox("アイコンを表示", &showSceneIcons_);
    ImGui::BeginDisabled(!showSceneIcons_);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("大きさ", &sceneIconScale_, 0.6f, 2.0f, "%.1f 倍");
    struct KindItem
    {
        SceneIconKind kind;
        const char *label;
    };
    static const KindItem kKinds[kSceneIconKindCount] = {
        {SceneIconKind::PointLight, ICON_FA_LIGHTBULB " 点光源"}, {SceneIconKind::SpotLight, ICON_FA_LIGHTBULB " スポット"},
        {SceneIconKind::Camera, ICON_FA_VIDEO " カメラ"},         {SceneIconKind::Emitter, ICON_FA_STAR " エミッター"},
        {SceneIconKind::Field, ICON_FA_WIND " フィールド"},
    };
    for (int i = 0; i < kSceneIconKindCount; ++i)
    {
        const int bit = 1 << static_cast<int>(kKinds[i].kind);
        bool shown = (sceneIconKindMask_ & bit) != 0;
        if (i % 3 != 0)
            ImGui::SameLine();
        if (ImGui::Checkbox(kKinds[i].label, &shown))
        {
            sceneIconKindMask_ = shown ? (sceneIconKindMask_ | bit) : (sceneIconKindMask_ & ~bit);
        }
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("押すと選択・Ctrl で追加 / ダブルクリックでカメラを寄せる");
    ImGui::TextDisabled("選択中は範囲（球・円錐・視錐台・フィールド）を線で描きます");
}
} // namespace Hagine
#endif // USE_IMGUI
