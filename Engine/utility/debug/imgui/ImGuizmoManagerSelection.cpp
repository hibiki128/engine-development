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
// ImGuizmoManager: 選択（クリック・矩形・ホットキー・フォーカス）
// =======================================================================

namespace Hagine {
// ---- PruneSelectionByFilter -------------------------------------------

// 操作対象フィルタで無効化された分類の名前を選択セットから取り除く。
// これによりフィルタOFFにした種類のギズモが表示され続けるのを防ぐ。
void ImGuizmoManager::PruneSelectionByFilter()
{
    for (auto it = selectedNames_.begin(); it != selectedNames_.end();)
    {
        auto found = transformMap_.find(*it);
        if (found != transformMap_.end() && !IsCategoryEnabled(found->second.category))
        {
            it = selectedNames_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

// ---- HandleMouseSelection ---------------------------------------------

// マウスクリック時のレイキャストによる選択判定
// BaseObject/WorldTransform/FreeTransform すべての型に対応するため
// 行列版の RayIntersectOBBByMatrix を使用する（回転した対象でも形どおりに当たる）
void ImGuizmoManager::HandleMouseSelection(const ImVec2 &scenePosition, const ImVec2 &sceneSize, bool sceneHovered)
{
    ImVec2 mousePos = ImGui::GetMousePos();
    bool isInScene = (mousePos.x >= scenePosition.x && mousePos.x <= scenePosition.x + sceneSize.x &&
                      mousePos.y >= scenePosition.y && mousePos.y <= scenePosition.y + sceneSize.y);

    // 他の ImGui ウィンドウがシーンに重なっている上でのクリックは無視（誤選択防止）。
    // 選択の確定は「押した瞬間」ではなく「ドラッグせずに離した瞬間」（HandleBoxSelection が判定）。
    if (!sceneHovered || ImGuizmo::IsUsing() || !isInScene || !clickSelectRequested_ || !pViewProjection_)
        return;
    clickSelectRequested_ = false;

    bool isCtrlPressed = Input::GetInstance()->PushKey(DIK_LCONTROL);
    std::string pickedName;
    bool foundHit = false;

    // マウス位置をシーンウィンドウ相対座標に変換し、スプライト座標系にスケール。
    // scenePosition は ImGui 座標系なので、ImGui のマウス座標(mousePos)を使って整合させる
    // （Input::GetMousePos() はクライアント座標系なのでマルチビューポート時にずれる）。
    float relX = mousePos.x - scenePosition.x;
    float relY = mousePos.y - scenePosition.y;
    float spriteSpaceX = (relX / sceneSize.x) * static_cast<float>(WinApp::GetVirtualWidth());
    float spriteSpaceY = (relY / sceneSize.y) * static_cast<float>(WinApp::GetVirtualHeight());

    // ---- パス1: スクリーン空間ターゲット優先 2D ヒットテスト ----
    float minDist2D = std::numeric_limits<float>::max();
    for (const auto &pair : transformMap_)
    {
        const GizmoTarget &target = pair.second;
        if (!target.selectable || !target.isScreenSpace)
            continue;
        if (!IsCategoryEnabled(target.category))
            continue;
        if (isMultiSelecting_ && selectedNames_.find(pair.first) != selectedNames_.end())
            continue;

        float posX = 0.0f, posY = 0.0f;
        if (target.type == GizmoTarget::Type::Sprite2D)
        {
            if (!target.position2D)
                continue;
            posX = target.position2D->x;
            posY = target.position2D->y;
        }
        else
        {
            if (!target.translate)
                continue;
            posX = target.translate->x;
            posY = target.translate->y;
        }

        float dx = spriteSpaceX - posX;
        float dy = spriteSpaceY - posY;
        float dist = std::sqrt(dx * dx + dy * dy);

        // カスタム判定があれば実際の形状で、無ければ従来どおり原点まわりの円で判定する
        const bool isHit = target.screenHitTest
                               ? target.screenHitTest(Vector2{spriteSpaceX, spriteSpaceY})
                               : (dist <= target.screenHitRadius);

        // 距離は候補が重なった場合の優先度にのみ使う（近い原点のものを優先）
        if (isHit && dist < minDist2D)
        {
            minDist2D = dist;
            pickedName = pair.first;
            foundHit = true;
        }
    }

    if (foundHit)
    {
        // 2D ヒット時は重複候補をリセット
        overlapCandidates_.clear();
        overlapCycleIndex_ = 0;
    }
    else
    {
        // ---- パス2: 3D レイキャスト（全ヒット候補収集）----
        // スクリーン上でクリック位置に中心が近いオブジェクトを優先するため
        // スクリーン距離でソートし、大きなオブジェクト内の小さいオブジェクトを選択しやすくする
        Ray currentRay = Input::GetInstance()->GetCurrentRay();

        struct HitCandidate
        {
            std::string name;
            float rayDist;    // レイ上の距離（カメラからの奥行き）
            float screenDist; // マウスクリックから中心のスクリーン距離
        };
        std::vector<HitCandidate> candidates;

        for (const auto &pair : transformMap_)
        {
            const GizmoTarget &target = pair.second;
            if (!target.selectable || target.isScreenSpace)
                continue;
            if (!IsCategoryEnabled(target.category))
                continue;
            if (target.type == GizmoTarget::Type::BaseObject)
            {
                if (!target.baseObject || !target.baseObject->IsGizmoSelectable())
                    continue;
            }
            if (isMultiSelecting_ && selectedNames_.find(pair.first) != selectedNames_.end())
                continue;

            // モデルの実形状（ローカルAABB）で判定する。
            // 固定サイズの箱で判定していた頃は、地面のような平たいモデルや
            // 細長いモデルで「見た目と掴める場所がずれる」ことになっていた。
            const AABB localBounds = target.GetLocalBounds();
            Matrix4x4 worldMatrix = target.GetWorldMatrix();
            RayHitInfo currentHit;
            bool hit = Input::RayIntersectOBBByMatrix(currentRay, worldMatrix, currentHit, localBounds);

            if (hit)
            {
                // オブジェクト中心のスクリーン投影位置を求め、クリック位置との距離を計算
                float screenDist = std::numeric_limits<float>::max();
                Vector3 screenCenter;
                if (WorldToScreen(target.GetWorldPosition(), screenCenter, scenePosition, sceneSize))
                {
                    float sdx = mousePos.x - screenCenter.x;
                    float sdy = mousePos.y - screenCenter.y;
                    screenDist = std::sqrt(sdx * sdx + sdy * sdy);
                }
                candidates.push_back({pair.first, currentHit.distance, screenDist});
            }
        }

        // スクリーン距離を主キー、レイ距離を副キーでソート
        // → 大スケール emitter に囲まれていても、画面上でクリックに近い小オブジェクトが優先される
        std::sort(candidates.begin(), candidates.end(), [](const HitCandidate &a, const HitCandidate &b) {
            constexpr float kScreenDistThreshold = 20.0f;
            if (std::abs(a.screenDist - b.screenDist) > kScreenDistThreshold)
                return a.screenDist < b.screenDist;
            return a.rayDist < b.rayDist;
        });

        // 重複候補を保存（Tab キーでサイクル可能）
        overlapCandidates_.clear();
        for (const auto &c : candidates)
        {
            overlapCandidates_.push_back({c.name, c.rayDist});
        }
        overlapCycleIndex_ = 0;

        if (!candidates.empty())
        {
            pickedName = candidates[0].name;
            foundHit = true;
        }
    }

    // 選択状態を更新
    if (foundHit && !pickedName.empty())
    {
        if (isCtrlPressed)
        {
            if (selectedNames_.find(pickedName) != selectedNames_.end())
            {
                selectedNames_.erase(pickedName);
            }
            else
            {
                selectedNames_.insert(pickedName);
            }
            isMultiSelecting_ = true;
        }
        else
        {
            selectedNames_.clear();
            selectedNames_.insert(pickedName);
            isMultiSelecting_ = false;
        }
    }
    else
    {
        if (!isCtrlPressed)
        {
            selectedNames_.clear();
            overlapCandidates_.clear();
            overlapCycleIndex_ = 0;
            isMultiSelecting_ = false;
        }
    }

    if (!isCtrlPressed && isMultiSelecting_)
    {
        isMultiSelecting_ = false;
    }
}

// シーンウィンドウ上でだけ効くギズモ操作のホットキー。
// デバッグカメラが WASD を使うため、移動キーと衝突しないキーだけを割り当てている。
void ImGuizmoManager::HandleHotkeys(bool sceneHovered)
{
    // 文字入力中やシーン以外のウィンドウを触っている間は誤爆させない
    if (!sceneHovered || ImGui::GetIO().WantTextInput)
    {
        return;
    }

    Input *input = Input::GetInstance();

    // Ctrl 併用のショートカット（コピー等）と食い合わないよう、単独押しのときだけ反応させる
    const bool ctrlHeld = input->PushKey(DIK_LCONTROL) || input->PushKey(DIK_RCONTROL);

    if (input->TriggerKey(DIK_TAB) && overlapCandidates_.size() > 1)
    {
        CycleOverlapSelection();
    }

    if (ctrlHeld)
    {
        return;
    }

    if (input->TriggerKey(DIK_1))
    {
        currentOperation_ = ImGuizmo::TRANSLATE;
    }
    if (input->TriggerKey(DIK_2))
    {
        currentOperation_ = ImGuizmo::ROTATE;
    }
    if (input->TriggerKey(DIK_3))
    {
        currentOperation_ = ImGuizmo::SCALE;
    }
    if (input->TriggerKey(DIK_4))
    {
        currentMode_ = (currentMode_ == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        ImGuiNotification::Post(currentMode_ == ImGuizmo::LOCAL ? "座標系: ローカル" : "座標系: ワールド",
                                {0.42f, 0.66f, 0.68f, 1.0f});
    }
    if (input->TriggerKey(DIK_5))
    {
        useSnap_ = !useSnap_;
        ImGuiNotification::Post(useSnap_ ? "スナップ: ON" : "スナップ: OFF", {0.45f, 0.68f, 0.52f, 1.0f});
    }
    if (input->TriggerKey(DIK_F))
    {
        RequestFocusOnSelection();
    }
}

// 選択中ターゲットの重心と大きさからフォーカス要求を立てる
void ImGuizmoManager::RequestFocusOnSelection()
{
    Vector3 center = {0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    int count = 0;

    for (const std::string &name : selectedNames_)
    {
        auto it = transformMap_.find(name);
        if (it == transformMap_.end() || it->second.isScreenSpace)
        {
            continue;
        }
        const GizmoTarget &target = it->second;
        center = center + target.GetWorldPosition();
        ++count;

        // ローカルAABBにワールドスケールを掛けて、画面に収まる距離の目安にする
        const AABB bounds = target.GetLocalBounds();
        const Matrix4x4 world = target.GetWorldMatrix();
        const Vector3 worldScale = {
            Vector3{world.m[0][0], world.m[0][1], world.m[0][2]}.Length(),
            Vector3{world.m[1][0], world.m[1][1], world.m[1][2]}.Length(),
            Vector3{world.m[2][0], world.m[2][1], world.m[2][2]}.Length()};
        const Vector3 halfExtent = {
            (bounds.max.x - bounds.min.x) * 0.5f * worldScale.x,
            (bounds.max.y - bounds.min.y) * 0.5f * worldScale.y,
            (bounds.max.z - bounds.min.z) * 0.5f * worldScale.z};
        radius = (std::max)(radius, halfExtent.Length());
    }

    if (count == 0)
    {
        ImGuiNotification::Post("フォーカスする対象が選択されていません", {0.82f, 0.58f, 0.36f, 1.0f});
        return;
    }

    focusTarget_ = center / static_cast<float>(count);
    focusRadius_ = (std::max)(radius, 0.5f);
    focusRequested_ = true;
}

// フォーカス要求を取り出す（DebugCamera から毎フレーム呼ばれる）
bool ImGuizmoManager::ConsumeFocusRequest(Vector3 &outTarget, float &outRadius)
{
    if (!focusRequested_)
    {
        return false;
    }
    outTarget = focusTarget_;
    outRadius = focusRadius_;
    focusRequested_ = false;
    return true;
}

// 新規オブジェクトの既定配置位置（カメラ前方）を返す
Vector3 ImGuizmoManager::GetSpawnPosition(float distance) const
{
    if (!pViewProjection_)
    {
        return {0.0f, 0.0f, 0.0f};
    }

    // カメラのワールド行列は行ベクトル規約なので、3行目が位置・2行目がZ軸（視線方向）
    const Matrix4x4 &cameraWorld = pViewProjection_->matWorld_;
    const Vector3 cameraPosition = {cameraWorld.m[3][0], cameraWorld.m[3][1], cameraWorld.m[3][2]};
    const Vector3 forward = Vector3{cameraWorld.m[2][0], cameraWorld.m[2][1], cameraWorld.m[2][2]}.Normalize();

    Vector3 spawn = cameraPosition + forward * distance;

    // スナップ中はグリッドに乗せて出す（並べる作業の初手がずれない）
    if (useSnap_ && snapTranslate_ > 0.0f)
    {
        spawn.x = std::round(spawn.x / snapTranslate_) * snapTranslate_;
        spawn.y = std::round(spawn.y / snapTranslate_) * snapTranslate_;
        spawn.z = std::round(spawn.z / snapTranslate_) * snapTranslate_;
    }
    return spawn;
}

// ---- 矩形（ラバーバンド）選択 ------------------------------------------

void ImGuizmoManager::HandleBoxSelection(const ImVec2 &scenePosition, const ImVec2 &sceneSize, bool sceneHovered)
{
    ImGuiIO &io = ImGui::GetIO();
    const ImVec2 mousePos = io.MousePos;
    const bool isInScene = (mousePos.x >= scenePosition.x && mousePos.x <= scenePosition.x + sceneSize.x &&
                            mousePos.y >= scenePosition.y && mousePos.y <= scenePosition.y + sceneSize.y);

    clickSelectRequested_ = false;

    // ギズモの上で押した場合はギズモ操作を優先する
    if (!isBoxSelecting_)
    {
        if (sceneHovered && isInScene && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver())
        {
            isBoxSelecting_ = true;
            boxSelectStart_ = mousePos;
        }
        return;
    }

    // ドラッグ中は枠を描く。シーンウィンドウの前面に出したいので前景の描画リストを使う。
    const ImVec2 rectMin = {(std::min)(boxSelectStart_.x, mousePos.x), (std::min)(boxSelectStart_.y, mousePos.y)};
    const ImVec2 rectMax = {(std::max)(boxSelectStart_.x, mousePos.x), (std::max)(boxSelectStart_.y, mousePos.y)};
    const float dragWidth = rectMax.x - rectMin.x;
    const float dragHeight = rectMax.y - rectMin.y;
    const bool isDragEnough = (dragWidth >= kBoxSelectThreshold || dragHeight >= kBoxSelectThreshold);

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (isDragEnough)
        {
            ImDrawList *drawList = ImGui::GetForegroundDrawList();
            drawList->AddRectFilled(rectMin, rectMax, IM_COL32(90, 150, 220, 40));
            drawList->AddRect(rectMin, rectMax, IM_COL32(120, 190, 255, 220));
        }
        return;
    }

    // ボタンを離した：しきい値を超えていれば矩形選択として確定する。
    // 超えていない場合は「クリックだった」ことにして単体選択へ渡す。
    if (isDragEnough)
    {
        SelectInsideScreenRect(rectMin, rectMax, scenePosition, sceneSize, io.KeyCtrl);
    }
    else
    {
        clickSelectRequested_ = true;
    }
    isBoxSelecting_ = false;
}

void ImGuizmoManager::SelectInsideScreenRect(const ImVec2 &rectMin, const ImVec2 &rectMax,
                                             const ImVec2 &scenePosition, const ImVec2 &sceneSize, bool additive)
{
    if (!additive)
    {
        selectedNames_.clear();
    }

    size_t hitCount = 0;
    for (const auto &[name, target] : transformMap_)
    {
        if (!target.selectable || !IsCategoryEnabled(target.category))
        {
            continue;
        }
        if (target.type == GizmoTarget::Type::BaseObject &&
            (!target.baseObject || !target.baseObject->IsGizmoSelectable()))
        {
            continue;
        }

        // スクリーン空間（スプライト）は仮想解像度座標なので、シーン矩形へ写してから判定する
        ImVec2 screenPoint;
        if (target.isScreenSpace)
        {
            const Vector3 position = target.GetWorldPosition();
            screenPoint.x = scenePosition.x + (position.x / static_cast<float>(WinApp::GetVirtualWidth())) * sceneSize.x;
            screenPoint.y = scenePosition.y + (position.y / static_cast<float>(WinApp::GetVirtualHeight())) * sceneSize.y;
        }
        else
        {
            Vector3 projected;
            if (!WorldToScreen(target.GetWorldPosition(), projected, scenePosition, sceneSize))
            {
                continue; // カメラの後ろにある
            }
            screenPoint = {projected.x, projected.y};
        }

        if (screenPoint.x >= rectMin.x && screenPoint.x <= rectMax.x &&
            screenPoint.y >= rectMin.y && screenPoint.y <= rectMax.y)
        {
            selectedNames_.insert(name);
            ++hitCount;
        }
    }

    // 重なり候補は矩形選択では意味を持たないので畳んでおく
    overlapCandidates_.clear();
    overlapCycleIndex_ = 0;
    isMultiSelecting_ = selectedNames_.size() > 1;

    ImGuiNotification::Post("矩形選択: " + std::to_string(hitCount) + "個", {0.4f, 0.8f, 1.0f, 1.0f});
}

// Tab キーで重複候補をサイクルして次のオブジェクトを選択する
void ImGuizmoManager::CycleOverlapSelection()
{
    if (overlapCandidates_.size() <= 1)
        return;
    overlapCycleIndex_ = (overlapCycleIndex_ + 1) % static_cast<int>(overlapCandidates_.size());
    selectedNames_.clear();
    selectedNames_.insert(overlapCandidates_[overlapCycleIndex_].first);
}

} // namespace Hagine
#endif // USE_IMGUI
