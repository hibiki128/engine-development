#define NOMINMAX
#ifdef _DEBUG
#include "ImGuizmoManager.h"
#include "ImGuiNotification.h"
#include "Input.h"
#include "Sprite.h"
#include <Line/DrawLine3D.h>
#include <Object/Base/BaseObjectManager.h>
#include <Transform/WorldTransform.h>
#include "WinApp.h"

// =======================================================================
// GizmoTarget メンバ関数実装
// =======================================================================

// 各型に対応したワールド行列を返す
// FreeTransform の場合は translate/rotate/scale ポインタから行列を構築する
namespace Hagine {
Matrix4x4 GizmoTarget::GetWorldMatrix() const {
    switch (type) {
    case Type::BaseObject:
        if (baseObject && baseObject->GetWorldTransform()) {
            return baseObject->GetWorldTransform()->matWorld_;
        }
        break;

    case Type::WorldTransform:
        if (worldTransform) {
            return worldTransform->matWorld_;
        }
        break;

    case Type::FreeTransform:
        if (translate) {
            Vector3 s = scale ? *scale : Vector3{1.0f, 1.0f, 1.0f};
            Vector3 r = rotate ? *rotate : Vector3{0.0f, 0.0f, 0.0f};
            return MakeAffineMatrix(s, r, *translate);
        }
        break;

    case Type::Sprite2D:
        if (position2D) {
            Matrix4x4 m = MakeIdentity4x4();
            m.m[3][0] = position2D->x;
            m.m[3][1] = position2D->y;
            m.m[3][2] = 0.0f;
            return m;
        }
        break;
    }
    return MakeIdentity4x4();
}

// ワールド座標（位置成分）を返す
Vector3 GizmoTarget::GetWorldPosition() const {
    switch (type) {
    case Type::BaseObject:
        if (baseObject)
            return baseObject->GetWorldPosition();
        break;
    case Type::WorldTransform:
        if (worldTransform)
            return worldTransform->GetWorldPosition();
        break;
    case Type::FreeTransform:
        if (translate)
            return *translate;
        break;
    case Type::Sprite2D:
        if (position2D)
            return {position2D->x, position2D->y, 0.0f};
        break;
    }
    return {0.0f, 0.0f, 0.0f};
}

// ギズモ操作によって生じた平行移動デルタを各型に適用する
void GizmoTarget::ApplyTranslationDelta(const Vector3 &delta) {
    switch (type) {
    case Type::BaseObject:
        if (baseObject) {
            baseObject->GetLocalPosition() = baseObject->GetLocalPosition() + delta;
            WorldTransform *wt = baseObject->GetWorldTransform();
            if (wt) {
                wt->translation_ = baseObject->GetLocalPosition();
                wt->UpdateMatrix();
                baseObject->UpdateWorldTransformHierarchy();
            }
        }
        break;

    case Type::WorldTransform:
        if (worldTransform) {
            worldTransform->translation_ = worldTransform->translation_ + delta;
            worldTransform->UpdateMatrix();
        }
        break;

    case Type::FreeTransform:
        if (translate) {
            translate->x += delta.x;
            translate->y += delta.y;
            if (!isScreenSpace) {
                translate->z += delta.z;
            }
        }
        break;

    case Type::Sprite2D:
        if (position2D) {
            position2D->x += delta.x;
            position2D->y += delta.y;
            // Z は無視（スクリーン空間 XY のみ）
        }
        break;
    }
}

// ImGui で変換詳細を表示する
// imguiCallback が設定されている場合はそちらを優先する
void GizmoTarget::ShowImGui() {
    switch (type) {
    case Type::BaseObject:
        if (baseObject) {
            baseObject->ImGui();
        }
        break;

    case Type::WorldTransform:
        if (worldTransform) {
            if (imguiCallback) {
                imguiCallback();
            } else {
                ImGui::DragFloat3("Translation", &worldTransform->translation_.x, 0.1f);
                ImGui::DragFloat3("Scale", &worldTransform->scale_.x, 0.01f);
                Vector3 euler = worldTransform->GetRotationEuler();
                if (ImGui::DragFloat3("Rotation (rad)", &euler.x, 0.01f)) {
                    worldTransform->SetRotationEuler(euler);
                }
                if (ImGui::Button("UpdateMatrix")) {
                    worldTransform->UpdateMatrix();
                }
            }
        }
        break;

    case Type::FreeTransform:
        if (imguiCallback) {
            imguiCallback();
        } else {
            if (translate) {
                if (isScreenSpace) {
                    ImGui::DragFloat2("Position (px)", &translate->x, 1.0f);
                } else {
                    ImGui::DragFloat3("Translation", &translate->x, 0.1f);
                }
            }
            if (rotate)
                ImGui::DragFloat3("Rotation (rad)", &rotate->x, 0.01f);
            if (scale)
                ImGui::DragFloat3("Scale", &scale->x, 0.01f);
        }
        break;

    case Type::Sprite2D:
        if (imguiCallback) {
            imguiCallback();
        } else if (position2D) {
            ImGui::DragFloat2("Position (px)", &position2D->x, 1.0f);
        }
        break;
    }
}

// =======================================================================
// ImGuizmoManager メンバ関数実装
// =======================================================================

void ImGuizmoManager::Finalize() {
    transformMap_.clear();
    selectedNames_.clear();
    copiedObjects_.clear();
}

void ImGuizmoManager::BeginFrame() {
    ImGuizmo::BeginFrame();
}

void ImGuizmoManager::SetViewProjection(ViewProjection *vp) {
    viewProjection_ = vp;
}

// ---- AddTarget オーバーロード群 ----------------------------------------

// BaseObject を登録する
void ImGuizmoManager::AddTarget(const std::string &name, BaseObject *object, bool selectable) {
    GizmoTarget target;
    target.type = GizmoTarget::Type::BaseObject;
    target.name = name;
    target.baseObject = object;
    target.selectable = selectable;
    transformMap_[name] = target;

    UpdateFilteredNames();

    if (selectedNames_.empty()) {
        selectedNames_.insert(name);
    }
}

// WorldTransform のみを持つオブジェクトを登録する
void ImGuizmoManager::AddTarget(const std::string &name, WorldTransform *worldTransform,
                                bool selectable,
                                std::function<void()> imguiCallback) {
    GizmoTarget target;
    target.type = GizmoTarget::Type::WorldTransform;
    target.name = name;
    target.worldTransform = worldTransform;
    target.selectable = selectable;
    target.imguiCallback = imguiCallback;
    transformMap_[name] = target;

    UpdateFilteredNames();

    if (selectedNames_.empty()) {
        selectedNames_.insert(name);
    }
}

// Vector3 ポインタを直接指定して登録する（Sprite・ParticleEmitter など）
void ImGuizmoManager::AddTarget(const std::string &name,
                                Vector3 *translate,
                                Vector3 *rotate,
                                Vector3 *scale,
                                bool selectable,
                                std::function<void()> imguiCallback) {
    GizmoTarget target;
    target.type = GizmoTarget::Type::FreeTransform;
    target.name = name;
    target.translate = translate;
    target.rotate = rotate;
    target.scale = scale;
    target.selectable = selectable;
    target.imguiCallback = imguiCallback;
    transformMap_[name] = target;

    UpdateFilteredNames();

    if (selectedNames_.empty()) {
        selectedNames_.insert(name);
    }
}

// Sprite を登録する（スクリーン空間 XY のみ操作）
void ImGuizmoManager::AddTarget(const std::string &name, Sprite *sprite, bool selectable) {
    GizmoTarget target;
    target.type = GizmoTarget::Type::Sprite2D;
    target.name = name;
    target.position2D = &sprite->GetPositionRef();
    target.selectable = selectable;
    target.isScreenSpace = true;
    target.screenHitRadius = 50.0f;
    transformMap_[name] = target;

    UpdateFilteredNames();

    if (selectedNames_.empty()) {
        selectedNames_.insert(name);
    }
}

// ---- 選択オブジェクト取得（BaseObject 互換用）---------------------------

// 選択中の最初のエントリが BaseObject である場合に返す
BaseObject *ImGuizmoManager::GetSelectedTarget() {
    if (selectedNames_.empty())
        return nullptr;

    auto it = transformMap_.find(*selectedNames_.begin());
    if (it != transformMap_.end() && it->second.type == GizmoTarget::Type::BaseObject) {
        return it->second.baseObject;
    }
    return nullptr;
}

// 選択中のエントリのうち BaseObject のもののみを返す
std::vector<BaseObject *> ImGuizmoManager::GetSelectedTargets() {
    std::vector<BaseObject *> selected;
    for (const std::string &name : selectedNames_) {
        auto it = transformMap_.find(name);
        if (it != transformMap_.end() && it->second.type == GizmoTarget::Type::BaseObject) {
            selected.push_back(it->second.baseObject);
        }
    }
    return selected;
}

// ---- imgui ------------------------------------------------------------

void ImGuizmoManager::imgui() {
    if (!viewProjection_)
        return;

    ImGui::Checkbox("デバッグ表示する", &isDrawDebug_);

    // 操作モード選択
    if (ImGui::RadioButton("移動", currentOperation_ == ImGuizmo::TRANSLATE))
        currentOperation_ = ImGuizmo::TRANSLATE;
    ImGui::SameLine();
    if (ImGui::RadioButton("回転", currentOperation_ == ImGuizmo::ROTATE))
        currentOperation_ = ImGuizmo::ROTATE;
    ImGui::SameLine();
    if (ImGui::RadioButton("スケール", currentOperation_ == ImGuizmo::SCALE))
        currentOperation_ = ImGuizmo::SCALE;

    // 座標系選択
    if (ImGui::RadioButton("ローカル", currentMode_ == ImGuizmo::LOCAL))
        currentMode_ = ImGuizmo::LOCAL;
    ImGui::SameLine();
    if (ImGui::RadioButton("ワールド", currentMode_ == ImGuizmo::WORLD))
        currentMode_ = ImGuizmo::WORLD;

    ImGui::Separator();

    // 検索ボックス
    ImGui::Text("オブジェクト検索:");
    bool searchChanged = ImGui::InputText("##ObjectSearch", searchBuffer_, sizeof(searchBuffer_));
    if (searchChanged)
        UpdateFilteredNames();
    if (filteredNames_.empty())
        UpdateFilteredNames();

    std::string currentDisplayName = selectedNames_.empty() ? "なし"
                                                           : (selectedNames_.size() == 1 ? *selectedNames_.begin()
                                                                                        : "複数選択 (" + std::to_string(selectedNames_.size()) + "個)");

    if (ImGui::BeginCombo("選択オブジェクト", currentDisplayName.c_str())) {
        bool isNoneSelected = selectedNames_.empty();
        if (ImGui::Selectable("なし", isNoneSelected))
            selectedNames_.clear();
        if (isNoneSelected)
            ImGui::SetItemDefaultFocus();

        for (const std::string &name : filteredNames_) {
            auto it = transformMap_.find(name);
            if (it != transformMap_.end()) {
                bool isSelected = (selectedNames_.find(name) != selectedNames_.end());
                if (ImGui::Selectable(name.c_str(), isSelected)) {
                    selectedNames_.clear();
                    selectedNames_.insert(name);
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (strlen(searchBuffer_) > 0) {
        ImGui::Text("検索結果: %zu個", filteredNames_.size());
    }

    ImGui::Spacing();
    ImGui::Text("選択中のオブジェクト数: %zu", selectedNames_.size());
    if (!selectedNames_.empty()) {
        ImGui::Text("選択中:");
        for (const std::string &name : selectedNames_) {
            ImGui::BulletText("%s", name.c_str());
        }
    }

    ImGui::Separator();

    if (ImGui::Button("全選択")) {
        selectedNames_.clear();
        for (const auto &pair : transformMap_)
            selectedNames_.insert(pair.first);
    }
    ImGui::SameLine();
    if (ImGui::Button("選択解除"))
        selectedNames_.clear();

    ImGui::Spacing();

    if (!selectedNames_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.6f, 1.0f));
        ImGui::Text("オブジェクト詳細 (%s)", selectedNames_.begin()->c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();

        ShowSelectedObjectImGui();

        ImGui::Spacing();
        ImGui::Spacing();

        // BaseObject のみコピー・ペーストが可能
        auto it = transformMap_.find(*selectedNames_.begin());
        if (it != transformMap_.end() && it->second.type == GizmoTarget::Type::BaseObject) {
            if (ImGui::Button("コピー", ImVec2(-1, 30)))
                CopySelectedObjects();
            if (!copiedObjects_.empty()) {
                if (ImGui::Button("ペースト", ImVec2(-1, 30)))
                    PasteObjects();
            }
            ImGui::Spacing();
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.3f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("選択オブジェクトを削除", ImVec2(-1, 0)))
            DeleteSelectedObjects();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("選択中の全オブジェクトを削除します");
        ImGui::PopStyleColor(3);
    }

    // 重複オブジェクト候補（Tab でサイクル）
    if (overlapCandidates_.size() > 1) {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.4f, 1.0f));
        ImGui::Text("重複候補: %zu個 (Tab でサイクル選択)", overlapCandidates_.size());
        ImGui::PopStyleColor();
        for (int i = 0; i < static_cast<int>(overlapCandidates_.size()); ++i) {
            bool isCurrent = (i == overlapCycleIndex_);
            if (isCurrent)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            ImGui::Text("  [%d] %s", i, overlapCandidates_[i].first.c_str());
            if (isCurrent)
                ImGui::PopStyleColor();
        }
    }

    ImGui::Separator();
    if (isDrawDebug_)
        DrawDebugRaycast();
}

// ---- Update -----------------------------------------------------------

void ImGuizmoManager::Update(const ImVec2 &scenePosition, const ImVec2 &sceneSize) {
    if (!viewProjection_)
        return;

    ImGuizmo::SetRect(scenePosition.x, scenePosition.y, sceneSize.x, sceneSize.y);
    ImGuizmo::SetDrawlist();

    if (!ImGuizmo::IsUsing()) {
        HandleMouseSelection(scenePosition, sceneSize);

        // Tab キーで重複オブジェクトをサイクル選択
        if (Input::GetInstance()->TriggerKey(DIK_TAB) && overlapCandidates_.size() > 1) {
            CycleOverlapSelection();
        }
    }

    DrawSelectedObjectHighlight();

    if (!selectedNames_.empty()) {
        // スプライト用正射影 VP を使うためシーン情報を渡す
        DisplayGizmo(scenePosition, sceneSize);
    }
}

// ---- ShowSelectedObjectImGui ------------------------------------------

// 選択中エントリの ShowImGui を呼び出す
void ImGuizmoManager::ShowSelectedObjectImGui() {
    if (selectedNames_.empty())
        return;

    std::string firstName = *selectedNames_.begin();
    auto it = transformMap_.find(firstName);
    if (it != transformMap_.end()) {
        it->second.ShowImGui();
    }

    if (selectedNames_.size() > 1) {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 1.0f, 1.0f));
        ImGui::Text("※ %zu個のオブジェクトが選択されています", selectedNames_.size());
        ImGui::Text("表示しているのは '%s' の設定です", firstName.c_str());
        ImGui::PopStyleColor();
    }
}

// ---- HandleMouseSelection ---------------------------------------------

// マウスクリック時のレイキャストによる選択判定
// BaseObject/WorldTransform/FreeTransform すべての型に対応するため
// 行列版の RayIntersectAABBByMatrix を使用する
void ImGuizmoManager::HandleMouseSelection(const ImVec2 &scenePosition, const ImVec2 &sceneSize) {
    ImVec2 mousePos = ImGui::GetMousePos();
    bool isInScene = (mousePos.x >= scenePosition.x && mousePos.x <= scenePosition.x + sceneSize.x &&
                      mousePos.y >= scenePosition.y && mousePos.y <= scenePosition.y + sceneSize.y);

    if (ImGuizmo::IsUsing() || !isInScene || !Input::IsTriggerMouse(0) || !viewProjection_)
        return;

    bool isCtrlPressed = Input::GetInstance()->PushKey(DIK_LCONTROL);
    std::string pickedName;
    bool foundHit = false;

    // マウス位置をシーンウィンドウ相対座標に変換し、スプライト座標系にスケール
    Vector2 mouseScreenPos = Input::GetMousePos();
    float relX = mouseScreenPos.x - scenePosition.x;
    float relY = mouseScreenPos.y - scenePosition.y;
    float spriteSpaceX = (relX / sceneSize.x) * static_cast<float>(WinApp::kClientWidth);
    float spriteSpaceY = (relY / sceneSize.y) * static_cast<float>(WinApp::kClientHeight);

    // ---- パス1: スクリーン空間ターゲット優先 2D ヒットテスト ----
    float minDist2D = std::numeric_limits<float>::max();
    for (const auto &pair : transformMap_) {
        const GizmoTarget &target = pair.second;
        if (!target.selectable || !target.isScreenSpace)
            continue;
        if (isMultiSelecting_ && selectedNames_.find(pair.first) != selectedNames_.end())
            continue;

        float posX = 0.0f, posY = 0.0f;
        if (target.type == GizmoTarget::Type::Sprite2D) {
            if (!target.position2D) continue;
            posX = target.position2D->x;
            posY = target.position2D->y;
        } else {
            if (!target.translate) continue;
            posX = target.translate->x;
            posY = target.translate->y;
        }

        float dx = spriteSpaceX - posX;
        float dy = spriteSpaceY - posY;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist <= target.screenHitRadius && dist < minDist2D) {
            minDist2D = dist;
            pickedName = pair.first;
            foundHit = true;
        }
    }

    if (foundHit) {
        // 2D ヒット時は重複候補をリセット
        overlapCandidates_.clear();
        overlapCycleIndex_ = 0;
    } else {
        // ---- パス2: 3D レイキャスト（全ヒット候補収集）----
        // スクリーン上でクリック位置に中心が近いオブジェクトを優先するため
        // スクリーン距離でソートし、大きなオブジェクト内の小さいオブジェクトを選択しやすくする
        Ray currentRay = Input::GetInstance()->GetCurrentRay();

        struct HitCandidate {
            std::string name;
            float rayDist;    // レイ上の距離（カメラからの奥行き）
            float screenDist; // マウスクリックから中心のスクリーン距離
        };
        std::vector<HitCandidate> candidates;

        AABB defaultAABB;
        defaultAABB.min = {-1.3f, -1.3f, -1.3f};
        defaultAABB.max = {1.3f, 1.3f, 1.3f};
        Sphere defaultSphere;
        defaultSphere.center = {0.0f, 0.0f, 0.0f};
        defaultSphere.radius = 1.3f;

        for (const auto &pair : transformMap_) {
            const GizmoTarget &target = pair.second;
            if (!target.selectable || target.isScreenSpace)
                continue;
            if (target.type == GizmoTarget::Type::BaseObject) {
                if (!target.baseObject || !target.baseObject->IsGizmoSelectable())
                    continue;
            }
            if (isMultiSelecting_ && selectedNames_.find(pair.first) != selectedNames_.end())
                continue;

            Matrix4x4 worldMatrix = target.GetWorldMatrix();
            RayHitInfo currentHit;
            bool hit = Input::RayIntersectAABBByMatrix(currentRay, worldMatrix, currentHit, defaultAABB);
            if (!hit) {
                hit = Input::RayIntersectSphereByMatrix(currentRay, worldMatrix, currentHit, defaultSphere);
            }

            if (hit) {
                // オブジェクト中心のスクリーン投影位置を求め、クリック位置との距離を計算
                float screenDist = std::numeric_limits<float>::max();
                Vector3 screenCenter;
                if (WorldToScreen(target.GetWorldPosition(), screenCenter, scenePosition, sceneSize)) {
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
        for (const auto &c : candidates) {
            overlapCandidates_.push_back({c.name, c.rayDist});
        }
        overlapCycleIndex_ = 0;

        if (!candidates.empty()) {
            pickedName = candidates[0].name;
            foundHit = true;
        }
    }

    // 選択状態を更新
    if (foundHit && !pickedName.empty()) {
        if (isCtrlPressed) {
            if (selectedNames_.find(pickedName) != selectedNames_.end()) {
                selectedNames_.erase(pickedName);
            } else {
                selectedNames_.insert(pickedName);
            }
            isMultiSelecting_ = true;
        } else {
            selectedNames_.clear();
            selectedNames_.insert(pickedName);
            isMultiSelecting_ = false;
        }
    } else {
        if (!isCtrlPressed) {
            selectedNames_.clear();
            overlapCandidates_.clear();
            overlapCycleIndex_ = 0;
            isMultiSelecting_ = false;
        }
    }

    if (!isCtrlPressed && isMultiSelecting_) {
        isMultiSelecting_ = false;
    }
}

// Tab キーで重複候補をサイクルして次のオブジェクトを選択する
void ImGuizmoManager::CycleOverlapSelection() {
    if (overlapCandidates_.size() <= 1)
        return;
    overlapCycleIndex_ = (overlapCycleIndex_ + 1) % static_cast<int>(overlapCandidates_.size());
    selectedNames_.clear();
    selectedNames_.insert(overlapCandidates_[overlapCycleIndex_].first);
}

// ---- DisplayGizmo -----------------------------------------------------

// 選択中の全エントリの重心位置にギズモを表示し、操作量を各エントリに反映する
void ImGuizmoManager::DisplayGizmo(const ImVec2 &scenePosition, const ImVec2 &sceneSize) {
    if (!viewProjection_ || selectedNames_.empty())
        return;

    std::vector<GizmoTarget *> selectedTargets;
    for (const std::string &name : selectedNames_) {
        auto it = transformMap_.find(name);
        if (it != transformMap_.end()) {
            selectedTargets.push_back(&it->second);
        }
    }
    if (selectedTargets.empty())
        return;

    // 選択中にスクリーン空間（Sprite）が含まれるか確認
    // ※ 3Dオブジェクトとスプライトを同時選択した場合は動作が未定義
    bool anyScreenSpace = std::any_of(selectedTargets.begin(), selectedTargets.end(),
                                      [](const GizmoTarget *t) { return t->isScreenSpace; });

    // 選択中ターゲットの重心位置を求め、ギズモ表示用仮想行列を生成
    Vector3 centerPos = {0.0f, 0.0f, 0.0f};
    for (GizmoTarget *target : selectedTargets) {
        centerPos = centerPos + target->GetWorldPosition();
    }
    centerPos = centerPos / static_cast<float>(selectedTargets.size());

    Matrix4x4 centerMatrix = MakeIdentity4x4();
    centerMatrix.m[3][0] = centerPos.x;
    centerMatrix.m[3][1] = centerPos.y;
    centerMatrix.m[3][2] = centerPos.z;

    float matrixArray[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            matrixArray[i * 4 + j] = centerMatrix.m[i][j];

    float viewArray[16], projArray[16];

    if (anyScreenSpace) {
        // スプライト用：単位ビュー行列 + スプライトと同じ正射影行列
        // これにより ImGuizmo がピクセル座標系でギズモを正しい位置に描画する
        Matrix4x4 identView = MakeIdentity4x4();
        Matrix4x4 orthoProj = MakeOrthographicMatrix(
            0.0f, 0.0f,
            static_cast<float>(WinApp::kClientWidth),
            static_cast<float>(WinApp::kClientHeight),
            0.0f, 100.0f);

        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                viewArray[i * 4 + j] = identView.m[i][j];
                projArray[i * 4 + j] = orthoProj.m[i][j];
            }
        }
    } else {
        // 3Dオブジェクト用：カメラの View/Projection をそのまま使用
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                viewArray[i * 4 + j] = viewProjection_->matView_.m[i][j];
                projArray[i * 4 + j] = viewProjection_->matProjection_.m[i][j];
            }
        }
    }

    // スクリーン空間（Sprite 等）は XY 移動のみ許可
    ImGuizmo::OPERATION effectiveOp = currentOperation_;
    if (anyScreenSpace) {
        effectiveOp = ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Y;
    }

    if (ImGuizmo::Manipulate(viewArray, projArray, effectiveOp, currentMode_, matrixArray)) {
        Matrix4x4 newMatrix;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                newMatrix.m[i][j] = matrixArray[i * 4 + j];

        // 操作によって生じたデルタを全選択ターゲットに適用する
        // スクリーン空間の場合、deltaPos はピクセル単位になるので直接加算できる
        Vector3 deltaPos = {
            newMatrix.m[3][0] - centerMatrix.m[3][0],
            newMatrix.m[3][1] - centerMatrix.m[3][1],
            newMatrix.m[3][2] - centerMatrix.m[3][2]};

        for (GizmoTarget *target : selectedTargets) {
            target->ApplyTranslationDelta(deltaPos);
        }
    }
}

// ---- DecomposeMatrix --------------------------------------------------

// 行列からスケール・回転（クォータニオン）・位置を分解して返す
void ImGuizmoManager::DecomposeMatrix(const Matrix4x4 &matrix, Vector3 &position, Quaternion &rotation, Vector3 &scale) {
    position = {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};

    Vector3 col0 = {matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]};
    Vector3 col1 = {matrix.m[1][0], matrix.m[1][1], matrix.m[1][2]};
    Vector3 col2 = {matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]};

    scale.x = col0.Length();
    scale.y = col1.Length();
    scale.z = col2.Length();

    Matrix4x4 rotMatrix = matrix;
    if (scale.x != 0.0f) {
        rotMatrix.m[0][0] /= scale.x;
        rotMatrix.m[0][1] /= scale.x;
        rotMatrix.m[0][2] /= scale.x;
    }
    if (scale.y != 0.0f) {
        rotMatrix.m[1][0] /= scale.y;
        rotMatrix.m[1][1] /= scale.y;
        rotMatrix.m[1][2] /= scale.y;
    }
    if (scale.z != 0.0f) {
        rotMatrix.m[2][0] /= scale.z;
        rotMatrix.m[2][1] /= scale.z;
        rotMatrix.m[2][2] /= scale.z;
    }

    rotation = Quaternion::FromMatrix(rotMatrix);
}

// ---- WorldToScreen ----------------------------------------------------

// ワールド座標をシーンウィンドウのスクリーン座標に変換する
bool ImGuizmoManager::WorldToScreen(const Vector3 &worldPos, Vector3 &screenPos, const ImVec2 &scenePosition, const ImVec2 &sceneSize) {
    Vector4 clipPos;
    {
        Vector3 v = worldPos;
        float x = v.x * viewProjection_->matView_.m[0][0] + v.y * viewProjection_->matView_.m[1][0] + v.z * viewProjection_->matView_.m[2][0] + viewProjection_->matView_.m[3][0];
        float y = v.x * viewProjection_->matView_.m[0][1] + v.y * viewProjection_->matView_.m[1][1] + v.z * viewProjection_->matView_.m[2][1] + viewProjection_->matView_.m[3][1];
        float z = v.x * viewProjection_->matView_.m[0][2] + v.y * viewProjection_->matView_.m[1][2] + v.z * viewProjection_->matView_.m[2][2] + viewProjection_->matView_.m[3][2];
        float w = v.x * viewProjection_->matView_.m[0][3] + v.y * viewProjection_->matView_.m[1][3] + v.z * viewProjection_->matView_.m[2][3] + viewProjection_->matView_.m[3][3];

        clipPos.x = x * viewProjection_->matProjection_.m[0][0] + y * viewProjection_->matProjection_.m[1][0] + z * viewProjection_->matProjection_.m[2][0] + w * viewProjection_->matProjection_.m[3][0];
        clipPos.y = x * viewProjection_->matProjection_.m[0][1] + y * viewProjection_->matProjection_.m[1][1] + z * viewProjection_->matProjection_.m[2][1] + w * viewProjection_->matProjection_.m[3][1];
        clipPos.z = x * viewProjection_->matProjection_.m[0][2] + y * viewProjection_->matProjection_.m[1][2] + z * viewProjection_->matProjection_.m[2][2] + w * viewProjection_->matProjection_.m[3][2];
        clipPos.w = x * viewProjection_->matProjection_.m[0][3] + y * viewProjection_->matProjection_.m[1][3] + z * viewProjection_->matProjection_.m[2][3] + w * viewProjection_->matProjection_.m[3][3];
    }

    if (clipPos.w <= 0.0f)
        return false;

    float ndcX = clipPos.x / clipPos.w;
    float ndcY = clipPos.y / clipPos.w;

    screenPos.x = scenePosition.x + (ndcX * 0.5f + 0.5f) * sceneSize.x;
    screenPos.y = scenePosition.y + (0.5f - ndcY * 0.5f) * sceneSize.y;
    screenPos.z = clipPos.z / clipPos.w;

    return true;
}

// ---- GenerateUniqueName -----------------------------------------------

// 同名エントリが存在しないユニークな名前を生成する
std::string ImGuizmoManager::GenerateUniqueName(const std::string &baseName) {
    std::string newName;
    int counter = 1;

    std::string cleanBaseName = baseName;
    size_t underscorePos = baseName.find_last_of('_');
    if (underscorePos != std::string::npos) {
        std::string suffix = baseName.substr(underscorePos + 1);
        bool isNumber = true;
        for (char c : suffix) {
            if (!std::isdigit(c)) {
                isNumber = false;
                break;
            }
        }
        if (isNumber)
            cleanBaseName = baseName.substr(0, underscorePos);
    }

    do {
        newName = cleanBaseName + "_" + std::to_string(counter++);
    } while (transformMap_.find(newName) != transformMap_.end());

    return newName;
}

// ---- CopySelectedObjects / PasteObjects / DeleteSelectedObjects --------

// 選択中の BaseObject をコピーバッファに保存する（非 BaseObject はスキップ）
void ImGuizmoManager::CopySelectedObjects() {
    copiedObjects_.clear();
    for (const std::string &name : selectedNames_) {
        auto it = transformMap_.find(name);
        if (it != transformMap_.end() && it->second.type == GizmoTarget::Type::BaseObject) {
            copiedObjects_.push_back(it->second.baseObject);
        }
    }
}

// コピー済み BaseObject を複製して BaseObjectManager に追加する
void ImGuizmoManager::PasteObjects() {
    if (copiedObjects_.empty())
        return;

    selectedNames_.clear();

    for (BaseObject *copiedObj : copiedObjects_) {
        std::unique_ptr<BaseObject> newObject = std::make_unique<BaseObject>();
        newObject->SetPrimitive(copiedObj->IsPrimitive());
        newObject->Init(copiedObj->GetName());

        if (!copiedObj->GetModelPath().empty()) {
            newObject->CreateModel(copiedObj->GetModelPath());
        } else if (copiedObj->GetPrimitiveType() != PrimitiveType::kCount) {
            newObject->CreatePrimitiveModel(copiedObj->GetPrimitiveType());
        }

        if (!copiedObj->GetTexturePath().empty()) {
            newObject->SetTexture(copiedObj->GetTexturePath());
        }

        newObject->GetLocalPosition() = copiedObj->GetLocalPosition();
        newObject->GetLocalRotation() = copiedObj->GetLocalRotation();
        newObject->GetLocalScale() = copiedObj->GetLocalScale();
        newObject->GetLocalPosition().x += 1.0f;
        newObject->GetLighting() = copiedObj->GetLighting();
        newObject->SetColor(copiedObj->GetColor());

        std::string uniqueName = GenerateUniqueName(copiedObj->GetName());
        newObject->GetName() = uniqueName;

        BaseObjectManager::GetInstance()->AddObject(std::move(newObject));

        BaseObject *addedObject = BaseObjectManager::GetInstance()->GetObjectByName(uniqueName);
        if (addedObject) {
            AddTarget(uniqueName, addedObject);
        }

        selectedNames_.insert(uniqueName);
    }

    copiedObjects_.clear();
    ImGuiNotification::Post("オブジェクトを貼り付けました", {0.4f, 0.8f, 1.0f, 1.0f});
}

// 選択中の全エントリを削除する
// BaseObject の場合は BaseObjectManager からも削除する
void ImGuizmoManager::DeleteSelectedObjects() {
    if (selectedNames_.empty())
        return;

    size_t count = selectedNames_.size();
    for (const std::string &name : selectedNames_) {
        auto it = transformMap_.find(name);
        if (it != transformMap_.end() && it->second.type == GizmoTarget::Type::BaseObject) {
            BaseObjectManager::GetInstance()->RemoveObject(name);
        }
        transformMap_.erase(name);
    }

    UpdateFilteredNames();
    selectedNames_.clear();

    if (!transformMap_.empty()) {
        selectedNames_.insert(transformMap_.begin()->first);
    }
    ImGuiNotification::Post("選択オブジェクトを削除しました: " + std::to_string(count) + "個", {0.9f, 0.7f, 0.2f, 1.0f});
}

// ---- DrawSelectedObjectHighlight / DrawSelectionMarker ----------------

// 選択中の全エントリにハイライトマーカーを描画する
void ImGuizmoManager::DrawSelectedObjectHighlight() {
    if (selectedNames_.empty() || !viewProjection_)
        return;

    for (const std::string &selectedName : selectedNames_) {
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
void ImGuizmoManager::DrawSelectionMarker(const Vector3 &worldPosition) {
    Vector3 markerPos = worldPosition + Vector3(0.0f, 2.0f, 0.0f);
    Vector4 markerColor = {1.0f, 1.0f, 0.0f, 1.0f};
    float markerSize = 0.5f;

    Vector3 apex = markerPos - Vector3(0.0f, markerSize, 0.0f);
    Vector3 topLeft = markerPos + Vector3(-markerSize, markerSize, -markerSize);
    Vector3 topRight = markerPos + Vector3(markerSize, markerSize, -markerSize);
    Vector3 topFront = markerPos + Vector3(-markerSize, markerSize, markerSize);
    Vector3 topBack = markerPos + Vector3(markerSize, markerSize, markerSize);

    DrawLine3D::GetInstance()->SetPoints(apex, topLeft, markerColor);
    DrawLine3D::GetInstance()->SetPoints(apex, topRight, markerColor);
    DrawLine3D::GetInstance()->SetPoints(apex, topFront, markerColor);
    DrawLine3D::GetInstance()->SetPoints(apex, topBack, markerColor);
    DrawLine3D::GetInstance()->SetPoints(topLeft, topRight, markerColor);
    DrawLine3D::GetInstance()->SetPoints(topRight, topBack, markerColor);
    DrawLine3D::GetInstance()->SetPoints(topBack, topFront, markerColor);
    DrawLine3D::GetInstance()->SetPoints(topFront, topLeft, markerColor);
}

// ---- UpdateFilteredNames ----------------------------------------------

// 検索バッファに基づいてフィルタ済みの名前リストを更新する
void ImGuizmoManager::UpdateFilteredNames() {
    filteredNames_.clear();

    std::vector<std::string> allNames;
    for (const auto &pair : transformMap_)
        allNames.push_back(pair.first);
    std::sort(allNames.begin(), allNames.end());

    std::string searchStr = searchBuffer_;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

    for (const std::string &name : allNames) {
        if (strlen(searchBuffer_) == 0) {
            filteredNames_.push_back(name);
        } else {
            std::string lowerName = name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            if (lowerName.find(searchStr) != std::string::npos) {
                filteredNames_.push_back(name);
            }
        }
    }
}

// ---- DrawDebugRaycast / DrawAABBWireframe / DrawSphereWireframe -------

// 全エントリのAABB・スフィアワイヤーフレームとレイを描画する
void ImGuizmoManager::DrawDebugRaycast() {
    if (!showDebugRaycast_)
        return;

    Ray currentRay = Input::GetInstance()->GetCurrentRay();
    Vector3 rayEnd = currentRay.origin + (currentRay.direction * currentRay.length);
    DrawLine3D::GetInstance()->SetPoints(currentRay.origin, rayEnd, {1.0f, 0.0f, 0.0f, 1.0f});

    for (const auto &pair : transformMap_) {
        const GizmoTarget &target = pair.second;

        // スクリーン空間ターゲットは3Dデバッグ描画対象外
        if (target.isScreenSpace)
            continue;

        Matrix4x4 worldMatrix = target.GetWorldMatrix();
        bool isSelected = selectedNames_.find(pair.first) != selectedNames_.end();
        Vector4 aabbColor = isSelected ? Vector4{1.0f, 1.0f, 0.0f, 1.0f} : Vector4{0.0f, 0.0f, 1.0f, 1.0f};
        Vector4 sphereColor = isSelected ? Vector4{1.0f, 0.5f, 0.0f, 1.0f} : Vector4{1.0f, 0.0f, 1.0f, 1.0f};

        DrawAABBWireframe(worldMatrix, aabbColor);
        DrawSphereWireframe(worldMatrix, sphereColor);
        TestAndDrawRayHit(currentRay, target);
    }
}

// ローカル空間のAABBをワールド変換してワイヤーフレームを描画する
void ImGuizmoManager::DrawAABBWireframe(const Matrix4x4 &worldMatrix, const Vector4 &color) {
    AABB aabb;
    aabb.min = {-1.3f, -1.3f, -1.3f};
    aabb.max = {1.3f, 1.3f, 1.3f};

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

    for (int i = 0; i < 8; i++) {
        vertices[i] = Transformation(vertices[i], worldMatrix);
    }

    // 下面
    DrawLine3D::GetInstance()->SetPoints(vertices[0], vertices[1], color);
    DrawLine3D::GetInstance()->SetPoints(vertices[1], vertices[2], color);
    DrawLine3D::GetInstance()->SetPoints(vertices[2], vertices[3], color);
    DrawLine3D::GetInstance()->SetPoints(vertices[3], vertices[0], color);
    // 上面
    DrawLine3D::GetInstance()->SetPoints(vertices[4], vertices[5], color);
    DrawLine3D::GetInstance()->SetPoints(vertices[5], vertices[6], color);
    DrawLine3D::GetInstance()->SetPoints(vertices[6], vertices[7], color);
    DrawLine3D::GetInstance()->SetPoints(vertices[7], vertices[4], color);
    // 縦
    DrawLine3D::GetInstance()->SetPoints(vertices[0], vertices[4], color);
    DrawLine3D::GetInstance()->SetPoints(vertices[1], vertices[5], color);
    DrawLine3D::GetInstance()->SetPoints(vertices[2], vertices[6], color);
    DrawLine3D::GetInstance()->SetPoints(vertices[3], vertices[7], color);
}

// スフィアワイヤーフレームを描画する
void ImGuizmoManager::DrawSphereWireframe(const Matrix4x4 &worldMatrix, const Vector4 &color) {
    Sphere sphere{};

    Vector3 worldCenter = Transformation(sphere.center, worldMatrix);

    Vector3 scale = {
        sqrt(worldMatrix.m[0][0] * worldMatrix.m[0][0] + worldMatrix.m[1][0] * worldMatrix.m[1][0] + worldMatrix.m[2][0] * worldMatrix.m[2][0]),
        sqrt(worldMatrix.m[0][1] * worldMatrix.m[0][1] + worldMatrix.m[1][1] * worldMatrix.m[1][1] + worldMatrix.m[2][1] * worldMatrix.m[2][1]),
        sqrt(worldMatrix.m[0][2] * worldMatrix.m[0][2] + worldMatrix.m[1][2] * worldMatrix.m[1][2] + worldMatrix.m[2][2] * worldMatrix.m[2][2])};
    float worldRadius = sphere.radius * std::max({scale.x, scale.y, scale.z});

    DrawLine3D::GetInstance()->DrawSphere(worldCenter, color, worldRadius, 16);
}

// GizmoTarget のワールド行列を使ってAABB・スフィアのレイヒット点を描画する
void ImGuizmoManager::TestAndDrawRayHit(const Ray &ray, const GizmoTarget &target) {
    Matrix4x4 worldMatrix = target.GetWorldMatrix();

    AABB aabb;
    aabb.min = {-1.3f, -1.3f, -1.3f};
    aabb.max = {1.3f, 1.3f, 1.3f};
    Sphere sphere;
    sphere.center = {0.0f, 0.0f, 0.0f};
    sphere.radius = 1.3f;

    RayHitInfo aabbHit, sphereHit;
    bool aabbResult = Input::RayIntersectAABBByMatrix(ray, worldMatrix, aabbHit, aabb);
    bool sphereResult = Input::RayIntersectSphereByMatrix(ray, worldMatrix, sphereHit, sphere);

    if (aabbResult) {
        DrawLine3D::GetInstance()->DrawSphere(aabbHit.hitPoint, {0.0f, 1.0f, 0.0f, 1.0f}, 0.05f, 8);
        Vector3 normalEnd = aabbHit.hitPoint + (aabbHit.hitNormal * 0.3f);
        DrawLine3D::GetInstance()->SetPoints(aabbHit.hitPoint, normalEnd, {0.0f, 1.0f, 0.0f, 1.0f});
    }

    if (sphereResult) {
        DrawLine3D::GetInstance()->DrawSphere(sphereHit.hitPoint, {1.0f, 0.0f, 1.0f, 1.0f}, 0.05f, 8);
        Vector3 normalEnd = sphereHit.hitPoint + (sphereHit.hitNormal * 0.3f);
        DrawLine3D::GetInstance()->SetPoints(sphereHit.hitPoint, normalEnd, {1.0f, 0.0f, 1.0f, 1.0f});
    }
}

} // namespace Hagine
#endif // _DEBUG
