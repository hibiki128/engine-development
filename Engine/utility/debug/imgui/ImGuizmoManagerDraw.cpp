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
#include <format>
#include <imgui.h>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "DebugUIHelper.h"

// =======================================================================
// ImGuizmoManager: 補助描画（選択ハイライト・ワイヤーフレーム・レイ）
// =======================================================================

namespace Hagine {
// ---- DrawSelectedObjectHighlight / DrawSelectionMarker ----------------

// 選択中の全エントリにハイライトマーカーを描画する
void ImGuizmoManager::DrawSelectedObjectHighlight()
{
    if (selectedNames_.empty() || !pViewProjection_)
        return;

    for (const std::string &selectedName : selectedNames_)
    {
        auto it = transformMap_.find(selectedName);
        if (it == transformMap_.end())
            continue;

        // スクリーン空間ターゲットはピクセル座標を3D世界座標として扱えないためスキップ
        if (it->second.isScreenSpace)
            continue;

        DrawSelectionMarker(it->second.GetWorldPosition());
    }
}

// オブジェクトの上方に逆ピラミッド型の選択マーカーを描画する
void ImGuizmoManager::DrawSelectionMarker(const Vector3 &worldPosition)
{
    Vector3 markerPos = worldPosition + Vector3(0.0f, 2.0f, 0.0f);
    Vector4 markerColor = {1.0f, 1.0f, 0.0f, 1.0f};
    float markerSize = 0.5f;

    Vector3 apex = markerPos - Vector3(0.0f, markerSize, 0.0f);
    Vector3 topLeft = markerPos + Vector3(-markerSize, markerSize, -markerSize);
    Vector3 topRight = markerPos + Vector3(markerSize, markerSize, -markerSize);
    Vector3 topFront = markerPos + Vector3(-markerSize, markerSize, markerSize);
    Vector3 topBack = markerPos + Vector3(markerSize, markerSize, markerSize);

    LineRenderer *pLine = LineRenderer::GetInstance();
    pLine->AddLine(apex, topLeft, markerColor);
    pLine->AddLine(apex, topRight, markerColor);
    pLine->AddLine(apex, topFront, markerColor);
    pLine->AddLine(apex, topBack, markerColor);
    pLine->AddLine(topLeft, topRight, markerColor);
    pLine->AddLine(topRight, topBack, markerColor);
    pLine->AddLine(topBack, topFront, markerColor);
    pLine->AddLine(topFront, topLeft, markerColor);
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
        // 操作対象フィルタで無効化された種類は描画しない（画面の見やすさのため）
        if (!IsCategoryEnabled(target.category))
            continue;

        bool isSelected = selectedNames_.find(pair.first) != selectedNames_.end();
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
