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
#include <format>
#include <imgui.h>
#include <limits>
#include <vector>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "DebugUIHelper.h"
#include "SceneProjector.h"

// =======================================================================
// ImGuizmoManager: 補助描画（選択ハイライト・ワイヤーフレーム・レイ）
// =======================================================================

namespace Hagine {
namespace {
/// <summary>破線（ImDrawList には破線が無いので、短い線を並べる）</summary>
void AddDashedLine(ImDrawList *pDrawList, const ImVec2 &a, const ImVec2 &b, ImU32 color, float thickness)
{
    constexpr float kDash = 5.0f;
    constexpr float kGap = 4.0f;
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0f)
    {
        return;
    }
    const float ux = dx / length;
    const float uy = dy / length;
    // 画面いっぱいの線で数が膨らまないよう、本数に上限を置く
    const float step = (std::max)(kDash + kGap, length / 200.0f);
    for (float t = 0.0f; t < length; t += step)
    {
        const float end = (std::min)(t + kDash, length);
        pDrawList->AddLine(ImVec2(a.x + ux * t, a.y + uy * t), ImVec2(a.x + ux * end, a.y + uy * end), color, thickness);
    }
}

/// <summary>2D の凸包（時計回り。画面座標は y が下向きなので、見た目で時計回りになる並び）</summary>
std::vector<ImVec2> ConvexHull(std::vector<ImVec2> points)
{
    std::sort(points.begin(), points.end(), [](const ImVec2 &l, const ImVec2 &r) { return l.x < r.x || (l.x == r.x && l.y < r.y); });
    auto cross = [](const ImVec2 &o, const ImVec2 &a, const ImVec2 &b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    std::vector<ImVec2> hull(points.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < points.size(); ++i)
    {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], points[i]) <= 0.0f)
            --k;
        hull[k++] = points[i];
    }
    for (size_t i = points.size() - 1, t = k + 1; i > 0; --i)
    {
        while (k >= t && cross(hull[k - 2], hull[k - 1], points[i - 1]) <= 0.0f)
            --k;
        hull[k++] = points[i - 1];
    }
    hull.resize(k > 1 ? k - 1 : k);
    return hull;
}

ImU32 ToU32(const Vector4 &color, float alphaScale)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w * alphaScale));
}
} // namespace

// ---- DrawSelectionOverlay ----------------------------------------------

// 選択中の物の枠と、マウスを乗せている物の枠をシーン窓の上に描く。
// 以前は D3D の1ピクセルの線（黄色い箱と頭上の逆ピラミッド）で、背景によっては見づらく、
// 物の中に埋まると見えなかった。シーン窓の描画リストへ太さのある滑らかな線で重ね、
// 手前の辺は濃く・奥の辺は薄い破線にして、箱の向きが読み取れるようにした。
void ImGuizmoManager::DrawSelectionOverlay(const ImVec2 &scenePosition, const ImVec2 &sceneSize, bool sceneHovered)
{
    hoveredName_.clear();
    if (!pViewProjection_ || !LineRenderer::GetInstance()->IsCategoryShown(LineCategory::Selection))
    {
        return;
    }

    ImDrawList *pDrawList = ImGui::GetWindowDrawList();
    const SceneProjector projector(*pViewProjection_, scenePosition, sceneSize);
    pDrawList->PushClipRect(scenePosition, ImVec2(scenePosition.x + sceneSize.x, scenePosition.y + sceneSize.y), true);

    // ---- マウスを乗せている物（クリックしたら選ばれる物を先に知らせる）----
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool mouseInScene = mouse.x >= scenePosition.x && mouse.x <= scenePosition.x + sceneSize.x &&
                              mouse.y >= scenePosition.y && mouse.y <= scenePosition.y + sceneSize.y;
    if (showHoverOutline_ && sceneHovered && mouseInScene && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver() && !isBoxSelecting_ &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        hoveredName_ = PickTargetUnderMouse(scenePosition, sceneSize);
    }
    // 一覧（トランスフォームマネージャ）の行に乗せている物も同じ枠で知らせる
    const std::string &outlineName = !hoveredName_.empty() ? hoveredName_ : browserHoveredName_;
    if (showHoverOutline_ && !outlineName.empty() && !IsSelected(outlineName))
    {
        auto it = transformMap_.find(outlineName);
        if (it != transformMap_.end() && !it->second.isScreenSpace)
        {
            DrawTargetOutline(pDrawList, projector, it->second, hoverColor_, false);
        }
    }
    // 一覧側は窓を描くたびに入れ直す（窓を閉じたら残らないよう、使ったら消す）
    browserHoveredName_.clear();

    // ---- 選択中の物 ----
    if (showSelectionOutline_)
    {
        for (const std::string &selectedName : selectedNames_)
        {
            auto it = transformMap_.find(selectedName);
            // スクリーン空間ターゲット（スプライト）はギズモの十字で場所が分かるので枠は描かない
            if (it == transformMap_.end() || it->second.isScreenSpace)
                continue;
            DrawTargetOutline(pDrawList, projector, it->second, selectionColor_, true);
        }
    }
    pDrawList->PopClipRect();
}

// 対象のローカルAABBをワールドへ写した箱を描く（選択中は手前の辺を太く、奥の辺を破線、うっすら塗る）
void ImGuizmoManager::DrawTargetOutline(ImDrawList *pDrawList, const SceneProjector &projector, const GizmoTarget &target,
                                        const Vector4 &color, bool selected)
{
    const Matrix4x4 world = target.GetWorldMatrix();
    const AABB bounds = target.GetLocalBounds();

    // 角はビット順（x:1, y:2, z:4）
    Vector3 corners[8];
    for (int i = 0; i < 8; ++i)
    {
        const Vector3 local = {(i & 1) ? bounds.max.x : bounds.min.x, (i & 2) ? bounds.max.y : bounds.min.y,
                               (i & 4) ? bounds.max.z : bounds.min.z};
        corners[i] = Transformation(local, world);
    }
    Vector3 center = {0.0f, 0.0f, 0.0f};
    for (const Vector3 &corner : corners)
    {
        center = center + corner;
    }
    center = center / 8.0f;

    // 面（4隅）。カメラの方を向いている面を「手前」とし、手前の面に触れていない辺が奥の辺
    static constexpr int kFaces[6][4] = {
        {0, 2, 6, 4}, // -X
        {1, 3, 7, 5}, // +X
        {0, 1, 5, 4}, // -Y
        {2, 3, 7, 6}, // +Y
        {0, 1, 3, 2}, // -Z
        {4, 5, 7, 6}, // +Z
    };
    const Matrix4x4 &cameraWorld = pViewProjection_->matWorld_;
    const Vector3 cameraPosition = {cameraWorld.m[3][0], cameraWorld.m[3][1], cameraWorld.m[3][2]};
    bool frontFacing[6] = {};
    for (int f = 0; f < 6; ++f)
    {
        const Vector3 &a = corners[kFaces[f][0]];
        const Vector3 faceCenter = (corners[kFaces[f][0]] + corners[kFaces[f][1]] + corners[kFaces[f][2]] + corners[kFaces[f][3]]) / 4.0f;
        Vector3 normal = (corners[kFaces[f][1]] - a).Cross(corners[kFaces[f][3]] - a);
        if (normal.LengthSq() < 1e-12f)
        {
            // 厚みの無い箱（板など）は向きが決まらないので、全部手前として描く
            frontFacing[f] = true;
            continue;
        }
        if (normal.Dot(faceCenter - center) < 0.0f)
        {
            normal = normal * -1.0f; // 外向きにそろえる
        }
        frontFacing[f] = normal.Dot(cameraPosition - faceCenter) > 0.0f;
    }

    // 辺と、その辺に接する2面
    struct Edge
    {
        int a, b, faceA, faceB;
    };
    static constexpr Edge kEdges[12] = {
        {0, 1, 2, 4}, {2, 3, 3, 4}, {4, 5, 2, 5}, {6, 7, 3, 5}, // X 方向
        {0, 2, 0, 4}, {1, 3, 1, 4}, {4, 6, 0, 5}, {5, 7, 1, 5}, // Y 方向
        {0, 4, 0, 2}, {1, 5, 1, 2}, {2, 6, 0, 3}, {3, 7, 1, 3}, // Z 方向
    };

    // うっすら塗る（全部の角がカメラの前にあるときだけ。後ろへはみ出すと凸包が壊れる）
    if (selected && showSelectionFill_)
    {
        std::vector<ImVec2> points;
        points.reserve(8);
        for (const Vector3 &corner : corners)
        {
            ImVec2 screen;
            if (!projector.Point(corner, screen))
            {
                points.clear();
                break;
            }
            points.push_back(screen);
        }
        if (points.size() == 8)
        {
            const std::vector<ImVec2> hull = ConvexHull(points);
            if (hull.size() >= 3)
            {
                pDrawList->AddConvexPolyFilled(hull.data(), static_cast<int>(hull.size()), ToU32(color, 0.07f));
            }
        }
    }

    const ImU32 shadow = IM_COL32(0, 0, 0, selected ? 150 : 90);
    const ImU32 front = ToU32(color, 1.0f);
    const ImU32 back = ToU32(color, 0.40f);
    const float thickness = selected ? 2.0f : 1.4f;
    uint32_t drawn = 0;
    // 奥の辺 → 手前の辺の順に描いて、手前が上に来るようにする
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool frontPass = (pass == 1);
        for (const Edge &edge : kEdges)
        {
            const bool isFront = frontFacing[edge.faceA] || frontFacing[edge.faceB];
            if (isFront != frontPass)
                continue;
            if (!isFront && (!selected || !showSelectionHiddenEdges_))
                continue;
            ImVec2 a, b;
            if (!projector.Segment(corners[edge.a], corners[edge.b], a, b))
                continue;
            if (isFront)
            {
                // 下に暗い縁を敷いて、明るい背景でも暗い背景でも線が沈まないようにする
                pDrawList->AddLine(a, b, shadow, thickness + 2.0f);
                pDrawList->AddLine(a, b, front, thickness);
            }
            else
            {
                AddDashedLine(pDrawList, a, b, back, 1.2f);
            }
            ++drawn;
        }
    }

    // 原点（ギズモの中心）に小さな点
    if (selected)
    {
        ImVec2 pivot;
        if (projector.Point(target.GetWorldPosition(), pivot))
        {
            pDrawList->AddCircleFilled(pivot, 4.0f, IM_COL32(0, 0, 0, 160), 12);
            pDrawList->AddCircleFilled(pivot, 2.6f, front, 12);
        }
    }
    LineRenderer::GetInstance()->AddExternalLineCount(LineCategory::Selection, drawn);
}

// マウスの下にある物の名前（シーンのクリック選択と同じ決め方。無ければ空）
std::string ImGuizmoManager::PickTargetUnderMouse(const ImVec2 &scenePosition, const ImVec2 &sceneSize)
{
    const ImVec2 mouse = ImGui::GetMousePos();
    const Ray ray = Input::GetInstance()->GetCurrentRay();
    std::string bestName;
    float bestScreenDistance = std::numeric_limits<float>::max();
    float bestRayDistance = std::numeric_limits<float>::max();
    for (const auto &[name, target] : transformMap_)
    {
        if (!target.selectable || target.isScreenSpace || !IsCategoryEnabled(target.category))
            continue;
        if (target.type == GizmoTarget::Type::BaseObject && (!target.baseObject || !target.baseObject->IsGizmoSelectable()))
            continue;
        RayHitInfo hit;
        if (!Input::RayIntersectOBBByMatrix(ray, target.GetWorldMatrix(), hit, target.GetLocalBounds()))
            continue;
        float screenDistance = std::numeric_limits<float>::max();
        Vector3 screenCenter;
        if (WorldToScreen(target.GetWorldPosition(), screenCenter, scenePosition, sceneSize))
        {
            screenDistance = std::sqrt((mouse.x - screenCenter.x) * (mouse.x - screenCenter.x) + (mouse.y - screenCenter.y) * (mouse.y - screenCenter.y));
        }
        // クリック選択と同じく、画面上で中心が近い物を優先（差が小さければ手前の物）
        constexpr float kScreenDistThreshold = 20.0f;
        const bool better = std::abs(screenDistance - bestScreenDistance) > kScreenDistThreshold ? screenDistance < bestScreenDistance
                                                                                                  : hit.distance < bestRayDistance;
        if (bestName.empty() || better)
        {
            bestName = name;
            bestScreenDistance = screenDistance;
            bestRayDistance = hit.distance;
        }
    }
    return bestName;
}

// ---- DrawSnapGrid -----------------------------------------------------

// 移動スナップ中に、選択物の足元へ刻み幅のマス目を描く。
// 「どこに吸い付くのか」が見えないと刻み幅の設定を間違えていても気付けないので、
// 掴んでいる間だけ、対象の高さの水平面へ描く。中心から離れるほど薄くする。
void ImGuizmoManager::DrawSnapGrid()
{
    if (!showSnapGrid_ || !ImGuizmo::IsUsing() || currentOperation_ != ImGuizmo::TRANSLATE)
    {
        return;
    }
    const bool snapActive = (useSnap_ != ImGui::GetIO().KeyShift);
    if (!snapActive || snapTranslate_ <= 0.0f)
    {
        return;
    }

    // 選択物（3D）の重心を中心にする
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
    if (count == 0)
    {
        return;
    }
    center = center / static_cast<float>(count);

    // 刻みに乗った点を中心に、半径 kHalfCells マスぶん描く
    constexpr int kHalfCells = 8;
    const float step = snapTranslate_;
    const float originX = std::round(center.x / step) * step;
    const float originZ = std::round(center.z / step) * step;
    const float extent = step * static_cast<float>(kHalfCells);
    const float y = center.y;

    LineRenderer *pLine = LineRenderer::GetInstance();
    for (int i = -kHalfCells; i <= kHalfCells; ++i)
    {
        // 中心の線ほど濃く、外側ほど薄く
        const float fade = 1.0f - static_cast<float>(std::abs(i)) / static_cast<float>(kHalfCells + 1);
        const bool isAxis = (i == 0);
        const float alpha = (isAxis ? 0.9f : 0.45f) * fade;
        const float offset = step * static_cast<float>(i);

        // X 方向に伸びる線（Z が一定）と Z 方向に伸びる線（X が一定）
        const Vector4 colorAlongX = isAxis ? Vector4{0.95f, 0.45f, 0.45f, alpha} : Vector4{0.85f, 0.78f, 0.45f, alpha};
        const Vector4 colorAlongZ = isAxis ? Vector4{0.45f, 0.60f, 0.95f, alpha} : Vector4{0.85f, 0.78f, 0.45f, alpha};
        pLine->AddLine({originX - extent, y, originZ + offset}, {originX + extent, y, originZ + offset}, colorAlongX);
        pLine->AddLine({originX + offset, y, originZ - extent}, {originX + offset, y, originZ + extent}, colorAlongZ);
    }
}

// ---- DrawDebugRaycast / DrawAABBWireframe / DrawSphereWireframe -------

// 全エントリのAABB・スフィアワイヤーフレームとレイを描画する
void ImGuizmoManager::DrawDebugRaycast()
{
    if (!showDebugRaycast_)
        return;

    Ray currentRay = Input::GetInstance()->GetCurrentRay();
    if (showDebugHitPoints_)
    {
        Vector3 rayEnd = currentRay.origin + (currentRay.direction * currentRay.length);
        LineRenderer::GetInstance()->AddLine(currentRay.origin, rayEnd, {1.0f, 0.0f, 0.0f, 1.0f});
    }

    for (const auto &pair : transformMap_)
    {
        const GizmoTarget &target = pair.second;

        // スクリーン空間ターゲットは3Dデバッグ描画対象外
        if (target.isScreenSpace)
            continue;
        bool isSelected = selectedNames_.find(pair.first) != selectedNames_.end();
        // クリック対象から外した種類は、選択中のもの以外は描かない（画面の見やすさのため）
        if (!isSelected && !IsCategoryEnabled(target.category))
            continue;
        // 全オブジェクトぶんの枠を出すと配置作業中の画面が線だらけになるので、
        // 既定では選択中のものだけ描く
        if (debugSelectedOnly_ && !isSelected)
            continue;

        Matrix4x4 worldMatrix = target.GetWorldMatrix();
        const AABB localBounds = target.GetLocalBounds();
        Vector4 aabbColor = isSelected ? Vector4{1.0f, 1.0f, 0.0f, 1.0f} : Vector4{0.0f, 0.0f, 1.0f, 1.0f};
        Vector4 sphereColor = isSelected ? Vector4{1.0f, 0.5f, 0.0f, 1.0f} : Vector4{1.0f, 0.0f, 1.0f, 1.0f};

        if (showDebugAABB_)
            DrawAABBWireframe(worldMatrix, localBounds, aabbColor);
        if (showDebugSphere_)
            DrawSphereWireframe(worldMatrix, localBounds, sphereColor);
        if (showDebugHitPoints_)
            TestAndDrawRayHit(currentRay, target);
    }
}

// ローカル空間のAABBをワールド変換してワイヤーフレームを描画する
void ImGuizmoManager::DrawAABBWireframe(const Matrix4x4 &worldMatrix, const AABB &aabb, const Vector4 &color)
{
    Vector3 vertices[8] = {
        {aabb.min.x, aabb.min.y, aabb.min.z},
        {aabb.max.x, aabb.min.y, aabb.min.z},
        {aabb.max.x, aabb.min.y, aabb.max.z},
        {aabb.min.x, aabb.min.y, aabb.max.z},
        {aabb.min.x, aabb.max.y, aabb.min.z},
        {aabb.max.x, aabb.max.y, aabb.min.z},
        {aabb.max.x, aabb.max.y, aabb.max.z},
        {aabb.min.x, aabb.max.y, aabb.max.z},
    };

    for (int i = 0; i < 8; i++)
    {
        vertices[i] = Transformation(vertices[i], worldMatrix);
    }

    // vertices は 0-3 が下面、4-7 が対応する上面。AddBoxCorners の並びと一致する
    LineRenderer::GetInstance()->AddBoxCorners(vertices, color);
}

// ローカルAABBに外接するスフィアのワイヤーフレームを描画する
void ImGuizmoManager::DrawSphereWireframe(const Matrix4x4 &worldMatrix, const AABB &localBounds, const Vector4 &color)
{
    const Vector3 localCenter = {
        (localBounds.max.x + localBounds.min.x) * 0.5f,
        (localBounds.max.y + localBounds.min.y) * 0.5f,
        (localBounds.max.z + localBounds.min.z) * 0.5f};
    const Vector3 localHalfExtent = {
        (localBounds.max.x - localBounds.min.x) * 0.5f,
        (localBounds.max.y - localBounds.min.y) * 0.5f,
        (localBounds.max.z - localBounds.min.z) * 0.5f};

    Vector3 worldCenter = Transformation(localCenter, worldMatrix);

    // 行ベクトル規約なので各行が基底ベクトル。その長さがワールドスケールになる
    Vector3 scale = {
        Vector3{worldMatrix.m[0][0], worldMatrix.m[0][1], worldMatrix.m[0][2]}.Length(),
        Vector3{worldMatrix.m[1][0], worldMatrix.m[1][1], worldMatrix.m[1][2]}.Length(),
        Vector3{worldMatrix.m[2][0], worldMatrix.m[2][1], worldMatrix.m[2][2]}.Length()};
    float worldRadius = Vector3{localHalfExtent.x * scale.x,
                                localHalfExtent.y * scale.y,
                                localHalfExtent.z * scale.z}
                            .Length();

    LineRenderer::GetInstance()->AddSphere(worldCenter, worldRadius, color, 16);
}

// GizmoTarget のワールド行列を使ってAABBのレイヒット点を描画する
void ImGuizmoManager::TestAndDrawRayHit(const Ray &ray, const GizmoTarget &target)
{
    Matrix4x4 worldMatrix = target.GetWorldMatrix();
    const AABB aabb = target.GetLocalBounds();

    RayHitInfo aabbHit;
    if (!Input::RayIntersectOBBByMatrix(ray, worldMatrix, aabbHit, aabb))
    {
        return;
    }

    LineRenderer *pLine = LineRenderer::GetInstance();
    pLine->AddSphere(aabbHit.hitPoint, 0.05f, {0.0f, 1.0f, 0.0f, 1.0f}, 8);
    Vector3 normalEnd = aabbHit.hitPoint + (aabbHit.hitNormal * 0.3f);
    pLine->AddLine(aabbHit.hitPoint, normalEnd, {0.0f, 1.0f, 0.0f, 1.0f});
}

} // namespace Hagine
#endif // USE_IMGUI
