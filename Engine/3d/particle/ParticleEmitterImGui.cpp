#define NOMINMAX
#include "ParticleEmitter.h"
// CPU パーティクルのエミッター設定（エディタ部分）。
//   ・上に「すべて開く／閉じる」
//   ・「基本」にいちばん触る項目（発生間隔・数・寿命・速度・色）を集め、既定で開く
//   ・その下に発生位置・加速度・大きさ・回転・透明度・トレイル・描画、エミッターのトランスフォーム、グループ管理、保存
//   ・数値欄は右クリックでコピー・貼り付け・既定値に戻す
#ifdef USE_IMGUI
#include "../utility/debug/imgui/ImGuiNotification.h"
#include "../utility/debug/imgui/ImGuizmoManager.h"
// DebugUIHelper.h は ImGui:: を使うので imgui.h（ImGuizmoManager.h 経由）の後に include する
#include "../utility/debug/imgui/DebugUIHelper.h"
#include "ParticleGroupManager.h"
#include <MyMath.h>
#include <algorithm>
#include <icon/IconsFontAwesome5.h>
#include <set>

namespace Hagine {
namespace {
// 右クリックの「既定値に戻す」に使う値
constexpr float kZero3[3] = {0.0f, 0.0f, 0.0f};
constexpr float kOne3[3] = {1.0f, 1.0f, 1.0f};
constexpr float kDefaultFrequency = 0.1f;
constexpr float kDefaultLifeTime = 1.0f;
constexpr float kDefaultGravity = 0.0f;

/// <summary>Min ≦ Max になるよう成分ごとにそろえる</summary>
void ClampMinMax(Vector3 &minValue, Vector3 &maxValue)
{
    minValue.x = (std::min)(minValue.x, maxValue.x);
    minValue.y = (std::min)(minValue.y, maxValue.y);
    minValue.z = (std::min)(minValue.z, maxValue.z);
    maxValue.x = (std::max)(maxValue.x, minValue.x);
    maxValue.y = (std::max)(maxValue.y, minValue.y);
    maxValue.z = (std::max)(maxValue.z, minValue.z);
}
} // namespace

void ParticleEmitter::DebugParticleData()
{
    if (!particleManager_)
        return;

    // 「すべて開く／閉じる」は押した次のフレームで各見出しに効かせる
    const int openRequest = sectionOpenRequest_;
    sectionOpenRequest_ = 0;
    auto header = [openRequest](const char *label, const ImVec4 &accent, bool defaultOpen) {
        if (openRequest != 0)
            ImGui::SetNextItemOpen(openRequest > 0);
        return ThemedHeader(label, accent, defaultOpen);
    };
    auto node = [openRequest](const char *label) {
        if (openRequest != 0)
            ImGui::SetNextItemOpen(openRequest > 0);
        return ImGui::TreeNode(label);
    };

    std::vector<std::string> groupNames = particleManager_->GetParticleGroupsName();
    if (selectedGroupIndex_ >= static_cast<int>(groupNames.size()))
    {
        selectedGroupIndex_ = (std::max)(0, static_cast<int>(groupNames.size()) - 1);
    }

    if (!groupNames.empty())
    {
        std::vector<const char *> groupNameCStrs;
        for (auto &n : groupNames)
            groupNameCStrs.push_back(n.c_str());

        SectionHeader("[ 編集グループ ]", DebugTheme::kAccentBlue);
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##editGroup", &selectedGroupIndex_, groupNameCStrs.data(), static_cast<int>(groupNameCStrs.size()));

        std::string selectedGroup = groupNames[selectedGroupIndex_];
        ParticleSetting &setting = particleSettings_[selectedGroup];

        if (ImGui::SmallButton(ICON_FA_EXPAND_ALT " すべて開く"))
            sectionOpenRequest_ = 1;
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_COMPRESS_ALT " すべて閉じる"))
            sectionOpenRequest_ = -1;
        ImGui::Spacing();

        // ---- 基本（よく触る項目）----
        if (header("基本（よく触る項目）", DebugTheme::kAccentGreen, true))
        {
            ImGui::PushItemWidth(-110.0f);
            ImGui::DragFloat("発生間隔", &emitFrequency_, 0.001f, 0.001f, 100.0f, "%.3f 秒");
            FloatNContextMenu("##freqMenu", &emitFrequency_, 1, &kDefaultFrequency);
            ImGui::SetItemTooltip("自動発生のとき、この秒数ごとに1回出します");
            ImGui::InputInt("1回に出す数", reinterpret_cast<int *>(&setting.count), 1, 100);
            setting.count = std::clamp(static_cast<int>(setting.count), 0, 10000);

            ImGui::DragFloat("寿命 最小", &setting.lifeTimeMin, 0.01f, 0.0f, 10.0f, "%.2f 秒");
            FloatNContextMenu("##lifeMinMenu", &setting.lifeTimeMin, 1, &kDefaultLifeTime);
            ImGui::DragFloat("寿命 最大", &setting.lifeTimeMax, 0.01f, 0.0f, 10.0f, "%.2f 秒");
            FloatNContextMenu("##lifeMaxMenu", &setting.lifeTimeMax, 1, &kDefaultLifeTime);
            setting.lifeTimeMax = std::clamp(setting.lifeTimeMax, setting.lifeTimeMin, 10.0f);
            setting.lifeTimeMin = std::clamp(setting.lifeTimeMin, 0.0f, setting.lifeTimeMax);

            ImGui::DragFloat3("速度 最小", &setting.velocityMin.x, 0.1f);
            FloatNContextMenu("##velMinMenu", &setting.velocityMin.x, 3, kZero3);
            ImGui::DragFloat3("速度 最大", &setting.velocityMax.x, 0.1f);
            FloatNContextMenu("##velMaxMenu", &setting.velocityMax.x, 3, kZero3);
            ClampMinMax(setting.velocityMin, setting.velocityMax);

            ImGui::ColorEdit4("開始色", &setting.startColor.x);
            ImGui::ColorEdit4("終了色", &setting.endColor.x);
            ImGui::PopItemWidth();
        }

        // ---- 発生位置 ----
        if (header("発生位置", DebugTheme::kAccentBlue, false))
        {
            ImGui::Checkbox("中心に集める", &setting.isGatherMode);
            if (setting.isGatherMode)
            {
                ImGui::Indent();
                ImGui::DragFloat("強さ", &setting.gatherStrength, 0.1f);
                ImGui::DragFloat("始まるタイミング", &setting.gatherStartRatio, 0.1f);
                ImGui::Unindent();
            }
            ImGui::Checkbox("外周から出す", &setting.isEmitOnEdge);
        }

        // ---- 加速度・重力 ----
        if (header("加速度・重力", DebugTheme::kAccentCyan, false))
        {
            ImGui::DragFloat3("加速度 最初", &setting.startAcce.x, 0.001f);
            FloatNContextMenu("##acceStartMenu", &setting.startAcce.x, 3, kZero3);
            ImGui::DragFloat3("加速度 最後", &setting.endAcce.x, 0.001f);
            FloatNContextMenu("##acceEndMenu", &setting.endAcce.x, 3, kZero3);
            ImGui::Checkbox("乗算", &setting.isAcceMultiply);
            ImGui::SetItemTooltip("加速度を足すのではなく速度に掛けます");
            ImGui::DragFloat("重力", &setting.gravity, 0.01f, -FLT_MAX, FLT_MAX);
            FloatNContextMenu("##gravityMenu", &setting.gravity, 1, &kDefaultGravity);
        }

        // ---- 大きさ ----
        if (header("大きさ", DebugTheme::kAccentOrange, false))
        {
            if (setting.isRandomAllSize)
            {
                ImGui::Checkbox("最初と最後同じ大きさ", &setting.isEndScale);

                ImGui::DragFloat3("最大値", &setting.allScaleMax.x, 0.1f, 0.0f);
                FloatNContextMenu("##allScaleMaxMenu", &setting.allScaleMax.x, 3, kOne3);
                ImGui::DragFloat3("最小値", &setting.allScaleMin.x, 0.1f, 0.0f);
                FloatNContextMenu("##allScaleMinMenu", &setting.allScaleMin.x, 3, kOne3);
                ClampMinMax(setting.allScaleMin, setting.allScaleMax);
                if (!setting.isEndScale)
                {
                    ImGui::DragFloat3("最後", &setting.particleEndScale.x, 0.1f);
                    FloatNContextMenu("##endScaleMenu", &setting.particleEndScale.x, 3, kZero3);
                }
            }
            else if (setting.isRandomSize)
            {
                ImGui::DragFloat("最大値", &setting.scaleMax, 0.1f, 0.0f);
                ImGui::DragFloat("最小値", &setting.scaleMin, 0.1f, 0.0f);
                setting.scaleMax = std::clamp(setting.scaleMax, setting.scaleMin, FLT_MAX);
                setting.scaleMin = std::clamp(setting.scaleMin, 0.0f, setting.scaleMax);
            }
            else if (setting.isSinMove)
            {
                ImGui::DragFloat3("最初", &setting.particleStartScale.x, 0.1f, 0.0f);
                FloatNContextMenu("##startScaleMenu", &setting.particleStartScale.x, 3, kOne3);
            }
            else
            {
                ImGui::DragFloat3("最初", &setting.particleStartScale.x, 0.1f, 0.0f);
                FloatNContextMenu("##startScaleMenu", &setting.particleStartScale.x, 3, kOne3);
                ImGui::DragFloat3("最後", &setting.particleEndScale.x, 0.1f);
                FloatNContextMenu("##endScaleMenu", &setting.particleEndScale.x, 3, kZero3);
            }

            ImGui::Checkbox("均等にランダムな大きさ", &setting.isRandomSize);
            ImGui::Checkbox("ばらばらにランダムな大きさ", &setting.isRandomAllSize);
            ImGui::Checkbox("sin波の動き", &setting.isSinMove);
        }

        // ---- 回転 ----
        if (header("回転", DebugTheme::kAccentPurple, false))
        {
            if (!setting.isRandomRotate)
            {
                float startRotationDegrees[3] = {
                    radiansToDegrees(setting.startRotate.x),
                    radiansToDegrees(setting.startRotate.y),
                    radiansToDegrees(setting.startRotate.z)};
                float endRotationDegrees[3] = {
                    radiansToDegrees(setting.endRotate.x),
                    radiansToDegrees(setting.endRotate.y),
                    radiansToDegrees(setting.endRotate.z)};
                bool startChanged = ImGui::DragFloat3("最初 (度)", startRotationDegrees, 0.1f);
                startChanged |= FloatNContextMenu("##startRotMenu", startRotationDegrees, 3, kZero3);
                if (startChanged)
                {
                    setting.startRotate.x = degreesToRadians(startRotationDegrees[0]);
                    setting.startRotate.y = degreesToRadians(startRotationDegrees[1]);
                    setting.startRotate.z = degreesToRadians(startRotationDegrees[2]);
                }
                bool endChanged = ImGui::DragFloat3("最後 (度)", endRotationDegrees, 0.1f);
                endChanged |= FloatNContextMenu("##endRotMenu", endRotationDegrees, 3, kZero3);
                if (endChanged)
                {
                    setting.endRotate.x = degreesToRadians(endRotationDegrees[0]);
                    setting.endRotate.y = degreesToRadians(endRotationDegrees[1]);
                    setting.endRotate.z = degreesToRadians(endRotationDegrees[2]);
                }
            }
            if (setting.isRandomRotate)
            {
                float startRotationDegrees[3] = {
                    radiansToDegrees(setting.rotateStartMax.x),
                    radiansToDegrees(setting.rotateStartMax.y),
                    radiansToDegrees(setting.rotateStartMax.z)};
                float endRotationDegrees[3] = {
                    radiansToDegrees(setting.rotateStartMin.x),
                    radiansToDegrees(setting.rotateStartMin.y),
                    radiansToDegrees(setting.rotateStartMin.z)};

                if (ImGui::DragFloat3("回転 最大値 (度)", startRotationDegrees, 0.1f))
                {
                    setting.rotateStartMax.x = degreesToRadians(std::clamp(startRotationDegrees[0], radiansToDegrees(setting.rotateStartMin.x), 180.0f));
                    setting.rotateStartMax.y = degreesToRadians(std::clamp(startRotationDegrees[1], radiansToDegrees(setting.rotateStartMin.y), 180.0f));
                    setting.rotateStartMax.z = degreesToRadians(std::clamp(startRotationDegrees[2], radiansToDegrees(setting.rotateStartMin.z), 180.0f));
                }

                if (ImGui::DragFloat3("回転 最小値 (度)", endRotationDegrees, 0.1f))
                {
                    setting.rotateStartMin.x = degreesToRadians(std::clamp(endRotationDegrees[0], -180.0f, radiansToDegrees(setting.rotateStartMax.x)));
                    setting.rotateStartMin.y = degreesToRadians(std::clamp(endRotationDegrees[1], -180.0f, radiansToDegrees(setting.rotateStartMax.y)));
                    setting.rotateStartMin.z = degreesToRadians(std::clamp(endRotationDegrees[2], -180.0f, radiansToDegrees(setting.rotateStartMax.z)));
                }

                ImGui::Checkbox("ランダムな回転速度", &setting.isRotateVelocity);

                if (setting.isRotateVelocity)
                {
                    ImGui::DragFloat3("最大値", &setting.rotateVelocityMax.x, 0.01f);
                    FloatNContextMenu("##rotVelMaxMenu", &setting.rotateVelocityMax.x, 3, kZero3);
                    ImGui::DragFloat3("最小値", &setting.rotateVelocityMin.x, 0.01f);
                    FloatNContextMenu("##rotVelMinMenu", &setting.rotateVelocityMin.x, 3, kZero3);
                    ClampMinMax(setting.rotateVelocityMin, setting.rotateVelocityMax);
                }
            }

            ImGui::Checkbox("ランダムな回転", &setting.isRandomRotate);
            ImGui::Checkbox("進行方向に向ける", &setting.isFaceDirection);
        }

        // ---- 透明度 ----
        if (header("透明度", DebugTheme::kAccentYellow, false))
        {
            ImGui::DragFloat("最大値##alpha", &setting.alphaMax, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("最小値##alpha", &setting.alphaMin, 0.01f, 0.0f, 1.0f);
            setting.alphaMin = std::clamp(setting.alphaMin, 0.0f, setting.alphaMax);
            setting.alphaMax = std::clamp(setting.alphaMax, setting.alphaMin, 1.0f);
        }

        // ---- トレイル ----
        if (header(setting.enableTrail ? "トレイル  ●###trail" : "トレイル###trail", DebugTheme::kAccentCyan, false))
        {
            if (ImGui::Checkbox("トレイルを有効にする", &setting.enableTrail))
            {
                SetTrailEnabled(selectedGroup, setting.enableTrail);
            }

            if (setting.enableTrail)
            {
                ImGui::Indent();

                if (ImGui::DragFloat("トレイル生成間隔", &setting.trailSpawnInterval, 0.001f, 0.001f, 10.0f))
                {
                    SetTrailInterval(selectedGroup, setting.trailSpawnInterval);
                }
                if (ImGui::DragFloat("トレイル生存時間スケール", &setting.trailLifeScale, 0.01f))
                {
                    SetTrailLifeScale(selectedGroup, setting.trailLifeScale);
                }
                if (ImGui::DragFloat3("トレイルスケール倍率", &setting.trailScaleMultiplier.x, 0.01f))
                {
                    SetTrailScaleMultiplier(selectedGroup, setting.trailScaleMultiplier);
                }
                if (ImGui::DragFloat4("トレイル色彩倍率", &setting.trailColorMultiplier.x, 0.01f))
                {
                    SetTrailColorMultiplier(selectedGroup, setting.trailColorMultiplier);
                }

                if (ImGui::Checkbox("トレイル速度継承", &setting.trailInheritVelocity))
                {
                    SetTrailVelocityInheritance(selectedGroup, setting.trailInheritVelocity, setting.trailVelocityScale);
                }

                if (setting.trailInheritVelocity)
                {
                    if (ImGui::DragFloat("トレイル速度スケール", &setting.trailVelocityScale, 0.01f))
                    {
                        SetTrailVelocityInheritance(selectedGroup, setting.trailInheritVelocity, setting.trailVelocityScale);
                    }
                }
                ImGui::Unindent();
            }
        }

        // ---- 描画（ビルボード・色・ブレンド）----
        if (header("描画", DebugTheme::kAccentBlue, false))
        {
            if (node("ビルボード関連"))
            {
                ImGui::Checkbox("ビルボード", &setting.isBillboard);
                ImGui::SetItemTooltip("パーティクルを常にカメラに向ける");

                ImGui::Checkbox("Xビルボード", &setting.isBillboardX);
                ImGui::SetItemTooltip("パーティクルのX軸を常にカメラに向ける");

                ImGui::Checkbox("Yビルボード", &setting.isBillboardY);
                ImGui::SetItemTooltip("パーティクルのY軸を常にカメラに向ける");

                ImGui::Checkbox("Zビルボード", &setting.isBillboardZ);
                ImGui::SetItemTooltip("パーティクルのZ軸を常にカメラに向ける");
                ImGui::TreePop();
            }
            ImGui::Checkbox("ランダムカラー", &setting.isRandomColor);
            ImGui::SetItemTooltip("パーティクルごとに異なる色を適用");
            if (node("ブレンドモード"))
            {
                ShowBlendModeCombo(setting.blendMode);
                ImGui::TreePop();
            }
        }

        // ---- エミッターのトランスフォーム ----
        if (header("エミッターの位置・大きさ", DebugTheme::kAccentGreen, false))
        {
            if (ImGui::BeginTable("##EmitterTf", 2, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("位置");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat3("##pos", &transform_.translation_.x, 0.1f, 0.0f, 0.0f, "%.2f");
                FloatNContextMenu("##posMenu", &transform_.translation_.x, 3, kZero3);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("回転");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                float rotationDegrees[3] = {
                    radiansToDegrees(transform_.quaternionRotation_.x),
                    radiansToDegrees(transform_.quaternionRotation_.y),
                    radiansToDegrees(transform_.quaternionRotation_.z)};
                if (ImGui::DragFloat3("##rot", rotationDegrees, 0.1f, -360.0f, 360.0f, "%.1f"))
                {
                    transform_.quaternionRotation_.x = degreesToRadians(rotationDegrees[0]);
                    transform_.quaternionRotation_.y = degreesToRadians(rotationDegrees[1]);
                    transform_.quaternionRotation_.z = degreesToRadians(rotationDegrees[2]);
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("大きさ");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat3("##scl", &transform_.scale_.x, 0.1f, 0.0f, 0.0f, "%.2f");
                FloatNContextMenu("##sclMenu", &transform_.scale_.x, 3, kOne3);

                ImGui::EndTable();
            }

            AccentCheckbox("発生範囲の枠を表示", &isVisible_, DebugTheme::kAccentGreen);
            ImGui::SameLine();
            if (ImGui::Checkbox("ギズモで選べる", &isGizmoSelectable_))
            {
                ImGuizmoManager::GetInstance()->SetSelectable(name_, isGizmoSelectable_);
            }
        }
    }
    else
    {
        DimText("グループがありません。下の「グループ管理」で付けてください。");
    }

    if (header("グループ管理", DebugTheme::kAccentPurple, false))
    {

        ImGui::Spacing();

        std::set<std::string> emitterGroupNames(
            particleGroupNames_.begin(),
            particleGroupNames_.end());

        std::vector<ParticleGroup *> allGroups = ParticleGroupManager::GetInstance()->GetParticleGroups();

        static std::vector<int> leftSelected;
        static std::vector<int> rightSelected;

        std::vector<std::string> availableNames;
        std::vector<const char *> availableItems;
        std::vector<std::string> attachedNames;
        std::vector<const char *> attachedItems;

        for (const auto &pGroup : allGroups)
        {
            const std::string &name = pGroup->GetGroupName();
            if (emitterGroupNames.contains(name))
            {
                attachedNames.push_back(name);
            }
            else
            {
                availableNames.push_back(name);
            }
        }

        availableItems.clear();
        attachedItems.clear();
        for (auto &name : availableNames)
        {
            availableItems.push_back(name.c_str());
        }
        for (auto &name : attachedNames)
        {
            attachedItems.push_back(name.c_str());
        }

        while (!leftSelected.empty() && leftSelected.back() >= availableNames.size())
            leftSelected.pop_back();
        while (!rightSelected.empty() && rightSelected.back() >= attachedNames.size())
            rightSelected.pop_back();

        // ドラッグ＆ドロップで利用可能 ⇔ アタッチ済み を移動する。ループ後にまとめて反映
        std::string dndAttachName; // 利用可能 → アタッチ
        std::string dndDetachName; // アタッチ → 解除

        float width = ImGui::GetContentRegionAvail().x;
        float halfWidth = width * 0.45f;

        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::Text("利用可能なグループ");
        ImGui::SameLine(width - halfWidth - 50);
        ImGui::Text("アタッチ済みグループ");
        ImGui::PopStyleColor();

        // ── 左: 利用可能（ドラッグ元 / アタッチ済みのドロップ先=解除）──
        ImGui::BeginChild("available_groups", ImVec2(halfWidth, 200), ImGuiChildFlags_Borders);
        if (availableItems.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("利用可能なグループがありません");
            ImGui::PopStyleColor();
        }
        else
        {
            for (int i = 0; i < availableItems.size(); i++)
            {
                bool isSelected = std::find(leftSelected.begin(), leftSelected.end(), i) != leftSelected.end();
                if (ImGui::Selectable(availableItems[i], isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (!ImGui::GetIO().KeyCtrl)
                        leftSelected.clear();

                    auto it = std::find(leftSelected.begin(), leftSelected.end(), i);
                    if (it != leftSelected.end())
                        leftSelected.erase(it);
                    else
                        leftSelected.push_back(i);

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        ParticleGroup *pGroup = ParticleGroupManager::GetInstance()->GetParticleGroup(availableNames[i]);
                        if (pGroup)
                        {
                            AddParticleGroup(pGroup);
                            particleGroupNames_ = particleManager_->GetParticleGroupsName();
                        }
                        leftSelected.clear();
                    }
                }
                // 利用可能アイテムをドラッグ元に
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    ImGui::SetDragDropPayload("PG_AVAIL", &i, sizeof(int));
                    ImGui::Text("追加: %s", availableItems[i]);
                    ImGui::EndDragDropSource();
                }
            }
        }
        // アタッチ済みをこの領域にドロップ → 解除
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 8.0f)));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("PG_ATTACHED"))
            {
                int idx = *static_cast<const int *>(payload->Data);
                if (idx >= 0 && idx < static_cast<int>(attachedNames.size()))
                    dndDetachName = attachedNames[idx];
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 12));

        ImGui::PushID("move_right");
        bool canMoveRight = !leftSelected.empty();
        ImGui::BeginDisabled(!canMoveRight);
        if (ConfirmButton("追加 >>", ImVec2(80, 35)))
        {
            int moved = 0;
            for (auto it = leftSelected.rbegin(); it != leftSelected.rend(); ++it)
            {
                int idx = *it;
                ParticleGroup *pGroup = ParticleGroupManager::GetInstance()->GetParticleGroup(availableNames[idx]);
                if (pGroup)
                {
                    AddParticleGroup(pGroup);
                    particleGroupNames_ = particleManager_->GetParticleGroupsName();
                    ++moved;
                }
            }
            leftSelected.clear();
            if (moved > 0)
                ImGuiNotification::Post(std::to_string(moved) + " 個のグループをアタッチしました", {0.45f, 0.68f, 0.52f, 1.0f});
        }
        ImGui::EndDisabled();
        ImGui::PopID();

        ImGui::PushID("move_left");
        bool canMoveLeft = !rightSelected.empty();
        ImGui::BeginDisabled(!canMoveLeft);
        if (DangerButton("<< 削除", ImVec2(80, 35)))
        {
            int moved = 0;
            for (auto it = rightSelected.rbegin(); it != rightSelected.rend(); ++it)
            {
                int idx = *it;
                RemoveParticleGroup(attachedNames[idx]);
                particleGroupNames_ = particleManager_->GetParticleGroupsName();
                ++moved;
            }
            rightSelected.clear();
            if (moved > 0)
                ImGuiNotification::Post(std::to_string(moved) + " 個のグループを解除しました", {0.82f, 0.58f, 0.36f, 1.0f});
        }
        ImGui::EndDisabled();
        ImGui::PopID();

        ImGui::PopStyleVar();
        ImGui::EndGroup();

        ImGui::SameLine();

        // ── 右: アタッチ済み（ドラッグ元 / 利用可能のドロップ先=追加）──
        ImGui::BeginChild("attached_groups", ImVec2(halfWidth, 200), ImGuiChildFlags_Borders);
        if (attachedItems.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("アタッチされたグループがありません");
            ImGui::PopStyleColor();
        }
        else
        {
            for (int i = 0; i < attachedItems.size(); i++)
            {
                bool isSelected = std::find(rightSelected.begin(), rightSelected.end(), i) != rightSelected.end();
                if (ImGui::Selectable(attachedItems[i], isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    if (!ImGui::GetIO().KeyCtrl)
                        rightSelected.clear();

                    auto it = std::find(rightSelected.begin(), rightSelected.end(), i);
                    if (it != rightSelected.end())
                        rightSelected.erase(it);
                    else
                        rightSelected.push_back(i);

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        RemoveParticleGroup(attachedNames[i]);
                        particleGroupNames_ = particleManager_->GetParticleGroupsName();
                        rightSelected.clear();
                    }
                }
                // アタッチ済みアイテムをドラッグ元に
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                {
                    ImGui::SetDragDropPayload("PG_ATTACHED", &i, sizeof(int));
                    ImGui::Text("解除: %s", attachedItems[i]);
                    ImGui::EndDragDropSource();
                }
            }
        }
        // 利用可能をこの領域にドロップ → 追加
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 8.0f)));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("PG_AVAIL"))
            {
                int idx = *static_cast<const int *>(payload->Data);
                if (idx >= 0 && idx < static_cast<int>(availableNames.size()))
                    dndAttachName = availableNames[idx];
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::EndChild();

        // ドロップ確定をまとめて反映する
        if (!dndAttachName.empty())
        {
            ParticleGroup *pGroup = ParticleGroupManager::GetInstance()->GetParticleGroup(dndAttachName);
            if (pGroup)
            {
                AddParticleGroup(pGroup);
                particleGroupNames_ = particleManager_->GetParticleGroupsName();
                ImGuiNotification::Post("グループをアタッチしました: " + dndAttachName, {0.45f, 0.68f, 0.52f, 1.0f});
            }
        }
        if (!dndDetachName.empty())
        {
            RemoveParticleGroup(dndDetachName);
            particleGroupNames_ = particleManager_->GetParticleGroupsName();
            ImGuiNotification::Post("グループを解除しました: " + dndDetachName, {0.82f, 0.58f, 0.36f, 1.0f});
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("操作: Ctrl + クリックで複数選択 / ダブルクリック または ドラッグ＆ドロップで追加・削除");
        ImGui::PopStyleColor();
    }

    // ---- 保存 ----
    if (header("ファイル操作", DebugTheme::kTextDim, false))
    {
        if (PrimaryButton(ICON_FA_SAVE " 設定を保存", ImVec2(160, 0)))
        {
            SaveToJson();
            datas_->Flush();
            ImGuiNotification::Post("パーティクル設定を保存しました", {0.45f, 0.68f, 0.52f, 1.0f});
        }
        ImGui::SetItemTooltip("現在のパーティクル設定をJSONファイルに保存します");
    }
}
} // namespace Hagine
#else
namespace Hagine {
void ParticleEmitter::DebugParticleData() {}
} // namespace Hagine
#endif // USE_IMGUI
