#define NOMINMAX
#include "ParticleEmitter.h"
#include "Frame.h"
#include "line/DrawLine3D.h"
#include <Shadow/ShadowMap.h>

#include "../Utility/Debug/ImGui/ImGuiNotification.h"
#include "../Utility/Debug/ImGui/ImGuizmoManager.h"
#include "../Utility/Debug/ImGui/Debugui_improved.h"
#include "ParticleGroupManager.h"
#include <Particle/ParticleEditor.h>
#include <set>
#include <type/Quaternion.h>

// コンストラクタ
namespace Hagine {
ParticleEmitter::ParticleEmitter() {}

void ParticleEmitter::Initialize(std::string name) {
    transform_.Initialize();
    if (!name.empty()) {
        name_ = name;
        datas_ = std::make_unique<DataHandler>("Particle", name);
        LoadFromJson();
        Manager_ = std::make_unique<ParticleManager>();
        Manager_->Initialize(SrvManager::GetInstance());
        LoadParticleGroup();
        datas_ = std::make_unique<DataHandler>("Particle", name);
    }
    SyncSettingsToTransform();
    lastTranslation_ = transform_.translation_;
    lastRotation_ = transform_.quateRotation_;
    lastScale_ = transform_.scale_;
    ImGuiNotification::Post("パーティクルエミッターを初期化しました: " + name_, {0.2f, 0.8f, 0.8f, 1.0f});
#ifdef _DEBUG
    // WorldTransformのアドレスを渡す
    ImGuizmoManager::GetInstance()->AddTarget(name_, &transform_, isGizmoSelectable_);
#endif
}

void ParticleEmitter::Update() {
    // 経過時間を進める
    elapsedTime_ += Frame::DeltaTime();

    // 発生頻度に基づいてパーティクルを発生させる
    while (elapsedTime_ >= emitFrequency_) {
        Emit();                         // パーティクルを発生させる
        elapsedTime_ -= emitFrequency_; // 過剰に進んだ時間を考慮
    }
}

void ParticleEmitter::UpdateOnce() {
    SyncSettingsToTransform();
    EmitInternal();
}

void ParticleEmitter::Draw(const ViewProjection &vp_) {
    if (ShadowMap::GetInstance()->IsShadowPassActive()) return;
    Manager_->SetEmitterCenter(transform_.translation_);

    transform_.UpdateMatrix();
    if (Manager_) {
        Manager_->Update(vp_);
        Manager_->Draw();
    }
    DrawEmitter();

    size_t activeCount = Manager_->GetActiveParticleCount();
    ParticleEditor::GetInstance()->SetExternalParticleCount(name_, activeCount);
}

void ParticleEmitter::DrawEmitter() {
    if (!isVisible_)
        return;

    std::array<Vector3, 8> localVertices = {
        Vector3{-1.0f, -1.0f, -1.0f},
        Vector3{1.0f, -1.0f, -1.0f},
        Vector3{-1.0f, 1.0f, -1.0f},
        Vector3{1.0f, 1.0f, -1.0f},
        Vector3{-1.0f, -1.0f, 1.0f},
        Vector3{1.0f, -1.0f, 1.0f},
        Vector3{-1.0f, 1.0f, 1.0f},
        Vector3{1.0f, 1.0f, 1.0f}};
    std::array<Vector3, 8> worldVertices;
    Matrix4x4 worldMatrix = MakeAffineMatrix(transform_.scale_, transform_.quateRotation_, transform_.translation_);
    for (size_t i = 0; i < localVertices.size(); i++) {
        worldVertices[i] = Transformation(localVertices[i], worldMatrix);
    }
    constexpr std::array<std::pair<int, int>, 12> edges = {
        std::make_pair(0, 1), std::make_pair(1, 3), std::make_pair(3, 2), std::make_pair(2, 0),
        std::make_pair(4, 5), std::make_pair(5, 7), std::make_pair(7, 6), std::make_pair(6, 4),
        std::make_pair(0, 4), std::make_pair(1, 5), std::make_pair(2, 6), std::make_pair(3, 7)};
    for (const auto &edge : edges) {
        DrawLine3D::GetInstance()->SetPoints(worldVertices[edge.first], worldVertices[edge.second]);
    }
}

// -------------------------------------------------------
// SyncSettingsToTransform
//   transform の値を全グループの ParticleSetting に反映する
//   dirty判定で変化があったときだけ呼ぶ
// -------------------------------------------------------
void ParticleEmitter::SyncSettingsToTransform() {
    if (!Manager_)
        return;
    for (auto &[groupName, setting] : particleSettings_) {
        setting.translate = transform_.translation_;
        setting.rotation = transform_.quateRotation_.ToEulerAngles();
        setting.scale = transform_.scale_;
        Manager_->SetParticleSetting(groupName, setting);
    }
}

// -------------------------------------------------------
// EmitInternal
//   設定同期なしでパーティクルを発射するだけの内部用関数
// -------------------------------------------------------
void ParticleEmitter::EmitInternal() {
    if (Manager_) {
        Manager_->Emit();
    }
}

// -------------------------------------------------------
// Emit（外部から明示的に呼ぶ用。設定更新込み）
// -------------------------------------------------------
void ParticleEmitter::Emit() {
    SyncSettingsToTransform();
    EmitInternal();
}

void ParticleEmitter::SaveToJson() {
    datas_->Save("emitterTranslation", transform_.translation_);
    datas_->Save("emitterRotation", transform_.quateRotation_);
    datas_->Save("emitterScale", transform_.scale_);
    datas_->Save("GroupNames", particleGroupNames_);
    datas_->Save("emitFrequency", emitFrequency_);
    datas_->Save("isVisible", isVisible_);
    datas_->Save("isActive", isActive_);
    datas_->Save("isAuto", isAuto_);
    datas_->Save("isGizmoSelectable", isGizmoSelectable_);
    datas_->Save("drawGroup", drawGroup_);
    for (const auto &[groupName, setting] : particleSettings_) {
        datas_->Save(groupName + "_translate", setting.translate);
        datas_->Save(groupName + "_rotation", setting.rotation);
        datas_->Save(groupName + "_scale", setting.scale);
        datas_->Save(groupName + "_count", setting.count);
        datas_->Save(groupName + "_lifeTimeMin", setting.lifeTimeMin);
        datas_->Save(groupName + "_lifeTimeMax", setting.lifeTimeMax);
        datas_->Save(groupName + "_alphaMin", setting.alphaMin);
        datas_->Save(groupName + "_alphaMax", setting.alphaMax);
        datas_->Save(groupName + "_scaleMin", setting.scaleMin);
        datas_->Save(groupName + "_scaleMax", setting.scaleMax);
        datas_->Save(groupName + "_velocityMin", setting.velocityMin);
        datas_->Save(groupName + "_velocityMax", setting.velocityMax);
        datas_->Save(groupName + "_startScale", setting.particleStartScale);
        datas_->Save(groupName + "_endScale", setting.particleEndScale);
        datas_->Save(groupName + "_startAcce", setting.startAcce);
        datas_->Save(groupName + "_endAcce", setting.endAcce);
        datas_->Save(groupName + "_startRote", setting.startRote);
        datas_->Save(groupName + "_endRote", setting.endRote);
        datas_->Save(groupName + "_rotateStartMax", setting.rotateStartMax);
        datas_->Save(groupName + "_rotateStartMin", setting.rotateStartMin);
        datas_->Save(groupName + "_rotateVelocityMin", setting.rotateVelocityMin);
        datas_->Save(groupName + "_rotateVelocityMax", setting.rotateVelocityMax);
        datas_->Save(groupName + "_allScaleMin", setting.allScaleMin);
        datas_->Save(groupName + "_allScaleMax", setting.allScaleMax);
        datas_->Save(groupName + "_isRandomScale", setting.isRandomSize);
        datas_->Save(groupName + "_isAllRamdomScale", setting.isRandomAllSize);
        datas_->Save(groupName + "_isRandomColor", setting.isRandomColor);
        datas_->Save(groupName + "_isRandomRotate", setting.isRandomRotate);
        datas_->Save(groupName + "_isBillboard", setting.isBillboard);
        datas_->Save(groupName + "_isBillboardX", setting.isBillboardX);
        datas_->Save(groupName + "_isBillboardY", setting.isBillboardY);
        datas_->Save(groupName + "_isBillboardZ", setting.isBillboardZ);
        datas_->Save(groupName + "_isAcceMultiply", setting.isAcceMultiply);
        datas_->Save(groupName + "_isSinMove", setting.isSinMove);
        datas_->Save(groupName + "_isFaceDirection", setting.isFaceDirection);
        datas_->Save(groupName + "_isEndScale", setting.isEndScale);
        datas_->Save(groupName + "_isEmitOnEdge", setting.isEmitOnEdge);
        datas_->Save(groupName + "_isGatherMode", setting.isGatherMode);
        datas_->Save(groupName + "_gatherStartRatio", setting.gatherStartRatio);
        datas_->Save(groupName + "_gatherStrength", setting.gatherStrength);
        datas_->Save(groupName + "_gravity", setting.gravity);
        datas_->Save(groupName + "_isBillboard", setting.isBillboard);
        datas_->Save(groupName + "_enableTrail", setting.enableTrail);
        datas_->Save(groupName + "_trailSpawnInterval", setting.trailSpawnInterval);
        datas_->Save(groupName + "_maxTrailParticles", setting.maxTrailParticles);
        datas_->Save(groupName + "_trailLifeScale", setting.trailLifeScale);
        datas_->Save(groupName + "_trailScaleMultiplier", setting.trailScaleMultiplier);
        datas_->Save(groupName + "_trailColorMultiplier", setting.trailColorMultiplier);
        datas_->Save(groupName + "_trailInheritVelocity", setting.trailInheritVelocity);
        datas_->Save(groupName + "_trailVelocityScale", setting.trailVelocityScale);
        datas_->Save(groupName + "_startColor", setting.startColor);
        datas_->Save(groupName + "_endColor", setting.endColor);
        datas_->Save(groupName + "_blendMode", setting.blendMode);
        Manager_->SetParticleSetting(groupName, setting);
    }
    ImGuiNotification::Post("パーティクルデータを保存しました: " + name_, {0.2f, 0.8f, 0.2f, 1.0f});
}

void ParticleEmitter::LoadFromJson() {
    transform_.translation_ = datas_->Load<Vector3>("emitterTranslation", {0, 0, 0});
    transform_.quateRotation_ = datas_->Load<Quaternion>("emitterRotation", Quaternion::IdentityQuaternion());
    transform_.scale_ = datas_->Load<Vector3>("emitterScale", {1, 1, 1});
    particleGroupNames_ = datas_->Load<std::vector<std::string>>("GroupNames", {});
    emitFrequency_ = datas_->Load<float>("emitFrequency", 0.1f);
    isVisible_ = datas_->Load<bool>("isVisible", true);
    isActive_ = datas_->Load<bool>("isActive", false);
    isAuto_ = datas_->Load<bool>("isAuto", false);
    isGizmoSelectable_ = datas_->Load<bool>("isGizmoSelectable", true);
    drawGroup_ = datas_->Load<std::string>("drawGroup", "3D");
    if (drawGroup_ != "UI") {
        drawGroup_ = "3D"; // 旧データは3D扱いに正規化
    }

    for (const auto &groupName : particleGroupNames_) {
        ParticleSetting setting;
        setting.translate = datas_->Load<Vector3>(groupName + "_translate", {0, 0, 0});
        setting.rotation = datas_->Load<Vector3>(groupName + "_rotation", {0, 0, 0});
        setting.scale = datas_->Load<Vector3>(groupName + "_scale", {1, 1, 1});
        setting.count = datas_->Load<uint32_t>(groupName + "_count", 1);
        setting.lifeTimeMin = datas_->Load<float>(groupName + "_lifeTimeMin", 1.0f);
        setting.lifeTimeMax = datas_->Load<float>(groupName + "_lifeTimeMax", 3.0f);
        setting.alphaMin = datas_->Load<float>(groupName + "_alphaMin", 1.0f);
        setting.alphaMax = datas_->Load<float>(groupName + "_alphaMax", 1.0f);
        setting.scaleMin = datas_->Load<float>(groupName + "_scaleMin", 1.0f);
        setting.scaleMax = datas_->Load<float>(groupName + "_scaleMax", 1.0f);
        setting.velocityMin = datas_->Load<Vector3>(groupName + "_velocityMin", {-1, -1, -1});
        setting.velocityMax = datas_->Load<Vector3>(groupName + "_velocityMax", {1, 1, 1});
        setting.particleStartScale = datas_->Load<Vector3>(groupName + "_startScale", {1, 1, 1});
        setting.particleEndScale = datas_->Load<Vector3>(groupName + "_endScale", {0, 0, 0});
        setting.startAcce = datas_->Load<Vector3>(groupName + "_startAcce", {1, 1, 1});
        setting.endAcce = datas_->Load<Vector3>(groupName + "_endAcce", {1, 1, 1});
        setting.startRote = datas_->Load<Vector3>(groupName + "_startRote", {0, 0, 0});
        setting.endRote = datas_->Load<Vector3>(groupName + "_endRote", {0, 0, 0});
        setting.rotateStartMax = datas_->Load<Vector3>(groupName + "_rotateStartMax", {0, 0, 0});
        setting.rotateStartMin = datas_->Load<Vector3>(groupName + "_rotateStartMin", {0, 0, 0});
        setting.rotateVelocityMin = datas_->Load<Vector3>(groupName + "_rotateVelocityMin", {-0.07f, -0.07f, -0.07f});
        setting.rotateVelocityMax = datas_->Load<Vector3>(groupName + "_rotateVelocityMax", {0.07f, 0.07f, 0.07f});
        setting.allScaleMin = datas_->Load<Vector3>(groupName + "_allScaleMin", {0, 0, 0});
        setting.allScaleMax = datas_->Load<Vector3>(groupName + "_allScaleMax", {1, 1, 1});
        setting.isRandomSize = datas_->Load<bool>(groupName + "_isRandomScale", false);
        setting.isRandomAllSize = datas_->Load<bool>(groupName + "_isAllRamdomScale", false);
        setting.isRandomColor = datas_->Load<bool>(groupName + "_isRandomColor", false);
        setting.isRandomRotate = datas_->Load<bool>(groupName + "_isRandomRotate", false);
        setting.isBillboard = datas_->Load<bool>(groupName + "_isBillboard", false);
        setting.isBillboardX = datas_->Load<bool>(groupName + "_isBillboardX", false);
        setting.isBillboardY = datas_->Load<bool>(groupName + "_isBillboardY", false);
        setting.isBillboardZ = datas_->Load<bool>(groupName + "_isBillboardZ", false);
        setting.isAcceMultiply = datas_->Load<bool>(groupName + "_isAcceMultiply", false);
        setting.isSinMove = datas_->Load<bool>(groupName + "_isSinMove", false);
        setting.isFaceDirection = datas_->Load<bool>(groupName + "_isFaceDirection", false);
        setting.isEndScale = datas_->Load<bool>(groupName + "_isEndScale", false);
        setting.isEmitOnEdge = datas_->Load<bool>(groupName + "_isEmitOnEdge", false);
        setting.isGatherMode = datas_->Load<bool>(groupName + "_isGatherMode", false);
        setting.gatherStartRatio = datas_->Load<float>(groupName + "_gatherStartRatio", 0.0f);
        setting.gatherStrength = datas_->Load<float>(groupName + "_gatherStrength", 0.0f);
        setting.gravity = datas_->Load<float>(groupName + "_gravity", 0.0f);
        setting.isBillboard = datas_->Load<float>(groupName + "_isBillboard", false);
        setting.enableTrail = datas_->Load<bool>(groupName + "_enableTrail", false);
        setting.trailSpawnInterval = datas_->Load<float>(groupName + "_trailSpawnInterval", 0.05f);
        setting.maxTrailParticles = datas_->Load<int>(groupName + "_maxTrailParticles", 20);
        setting.trailLifeScale = datas_->Load<float>(groupName + "_trailLifeScale", 0.5f);
        setting.trailScaleMultiplier = datas_->Load<Vector3>(groupName + "_trailScaleMultiplier", {0.8f, 0.8f, 0.8f});
        setting.trailColorMultiplier = datas_->Load<Vector4>(groupName + "_trailColorMultiplier", {1.0f, 1.0f, 1.0f, 0.7f});
        setting.trailInheritVelocity = datas_->Load<bool>(groupName + "_trailInheritVelocity", true);
        setting.trailVelocityScale = datas_->Load<float>(groupName + "_trailVelocityScale", 0.3f);
        setting.startColor = datas_->Load<Vector4>(groupName + "_startColor", {1.0f, 1.0f, 1.0f, 1.0f});
        setting.endColor = datas_->Load<Vector4>(groupName + "_endColor", {1.0f, 1.0f, 1.0f, 1.0f});
        setting.blendMode = datas_->Load<BlendMode>(groupName + "_blendMode", BlendMode::kAdd);
        particleSettings_[groupName] = setting;
    }
}

void ParticleEmitter::LoadParticleGroup() {
    for (auto &particleGroupname : particleGroupNames_) {
        AddParticleGroup(ParticleGroupManager::GetInstance()->GetParticleGroup(particleGroupname));
    }
    if (!particleGroupNames_.empty()) {
        ImGuiNotification::Post("パーティクルグループを読み込みました: " + name_, {0.2f, 0.8f, 0.8f, 1.0f});
    }
}

ParticleSetting ParticleEmitter::DefaultSetting() {
    ParticleSetting setting;
    setting.translate = {0, 0, 0};
    setting.rotation = {0, 0, 0};
    setting.scale = {1, 1, 1};
    setting.count = 1;
    setting.lifeTimeMin = 1.0f;
    setting.lifeTimeMax = 3.0f;
    setting.alphaMin = 1.0f;
    setting.alphaMax = 1.0f;
    setting.scaleMin = 1.0f;
    setting.scaleMax = 1.0f;
    setting.velocityMin = {-1, -1, -1};
    setting.velocityMax = {1, 1, 1};
    setting.particleStartScale = {1, 1, 1};
    setting.particleEndScale = {0, 0, 0};
    setting.startAcce = {1, 1, 1};
    setting.endAcce = {1, 1, 1};
    setting.startRote = {0, 0, 0};
    setting.endRote = {0, 0, 0};
    setting.rotateStartMax = {0, 0, 0};
    setting.rotateStartMin = {0, 0, 0};
    setting.rotateVelocityMin = {-0.07f, -0.07f, -0.07f};
    setting.rotateVelocityMax = {0.07f, 0.07f, 0.07f};
    setting.allScaleMin = {0, 0, 0};
    setting.allScaleMax = {1, 1, 1};
    setting.isRandomSize = false;
    setting.isRandomAllSize = false;
    setting.isRandomColor = false;
    setting.isRandomRotate = false;
    setting.isBillboard = false;
    setting.isAcceMultiply = false;
    setting.isSinMove = false;
    setting.isFaceDirection = false;
    setting.isEndScale = false;
    setting.isEmitOnEdge = false;
    setting.isGatherMode = false;
    setting.gatherStartRatio = 0.0f;
    setting.gatherStrength = 0.0f;
    setting.gravity = 0.0f;
    setting.enableTrail = false;
    setting.trailSpawnInterval = 0.05f;
    setting.maxTrailParticles = 20;
    setting.trailLifeScale = 0.5f;
    setting.trailScaleMultiplier = {0.8f, 0.8f, 0.8f};
    setting.trailColorMultiplier = {1.0f, 1.0f, 1.0f, 0.7f};
    setting.trailInheritVelocity = true;
    setting.trailVelocityScale = 0.3f;
    setting.startColor = {1.0f, 1.0f, 1.0f, 1.0f};
    setting.endColor = {1.0f, 1.0f, 1.0f, 1.0f};
    return setting;
}

#pragma region ImGui関連

void ParticleEmitter::DebugParticleData() {
#ifdef USE_IMGUI
    if (!Manager_)
        return;

    std::vector<std::string> groupNames = Manager_->GetParticleGroupsName();
    if (selectedGroupIndex_ >= groupNames.size()) {
        selectedGroupIndex_ = std::max(0, (int)groupNames.size() - 1);
    }

    if (!groupNames.empty()) {
        std::vector<const char *> groupNameCStrs;
        for (auto &n : groupNames)
            groupNameCStrs.push_back(n.c_str());

        SectionHeader("[ 編集グループ ]", DebugTheme::kAccentBlue);
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##editGroup", &selectedGroupIndex_, groupNameCStrs.data(), (int)groupNameCStrs.size());

        std::string selectedGroup = groupNames[selectedGroupIndex_];
        ParticleSetting &setting = particleSettings_[selectedGroup];

        ImGui::Spacing();

        if (ImGui::CollapsingHeader("エミッターデータ")) {
            if (ImGui::BeginTable("##EmitterTf", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("位置");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat3("##pos", &transform_.translation_.x, 0.1f, 0.0f, 0.0f, "%.2f");

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("回転");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                float rotationDegrees[3] = {
                    radiansToDegrees(transform_.quateRotation_.x),
                    radiansToDegrees(transform_.quateRotation_.y),
                    radiansToDegrees(transform_.quateRotation_.z)};
                if (ImGui::DragFloat3("##rot", rotationDegrees, 0.1f, -360.0f, 360.0f, "%.1f")) {
                    transform_.quateRotation_.x = degreesToRadians(rotationDegrees[0]);
                    transform_.quateRotation_.y = degreesToRadians(rotationDegrees[1]);
                    transform_.quateRotation_.z = degreesToRadians(rotationDegrees[2]);
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("大きさ");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat3("##scl", &transform_.scale_.x, 0.1f, 0.0f, 0.0f, "%.2f");

                ImGui::EndTable();
            }

            ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
            ImGui::Checkbox("表示", &isVisible_);
            ImGui::PopStyleColor();
#ifdef _DEBUG
            ImGui::SameLine();
            if (ImGui::Checkbox("ギズモ選択", &isGizmoSelectable_)) {
                ImGuizmoManager::GetInstance()->SetSelectable(name_, isGizmoSelectable_);
            }
#endif
        }

        ImGui::Spacing();

        if (ImGui::CollapsingHeader("パーティクルデータ")) {

            if (ImGui::TreeNode("寿命")) {
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("寿命設定");
                ImGui::PopStyleColor();
                ImGui::DragFloat("最大値", &setting.lifeTimeMax, 0.01f, 0.0f);
                ImGui::DragFloat("最小値", &setting.lifeTimeMin, 0.01f, 0.0f);

                setting.lifeTimeMax = std::clamp(setting.lifeTimeMax, setting.lifeTimeMin, 10.0f);
                setting.lifeTimeMin = std::clamp(setting.lifeTimeMin, 0.0f, setting.lifeTimeMax);

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("位置")) {
                ImGui::Checkbox("中心に集める", &setting.isGatherMode);

                if (setting.isGatherMode) {
                    ImGui::Indent();
                    ImGui::DragFloat("強さ", &setting.gatherStrength, 0.1f);
                    ImGui::DragFloat("始まるタイミング", &setting.gatherStartRatio, 0.1f);
                    ImGui::Unindent();
                }

                ImGui::Checkbox("外周", &setting.isEmitOnEdge);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("速度、加速度")) {
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("速度");
                ImGui::PopStyleColor();
                ImGui::DragFloat3("最大値", &setting.velocityMax.x, 0.1f);
                ImGui::DragFloat3("最小値", &setting.velocityMin.x, 0.1f);

                setting.velocityMin.x = std::clamp(setting.velocityMin.x, -FLT_MAX, setting.velocityMax.x);
                setting.velocityMax.x = std::clamp(setting.velocityMax.x, setting.velocityMin.x, FLT_MAX);
                setting.velocityMin.y = std::clamp(setting.velocityMin.y, -FLT_MAX, setting.velocityMax.y);
                setting.velocityMax.y = std::clamp(setting.velocityMax.y, setting.velocityMin.y, FLT_MAX);
                setting.velocityMin.z = std::clamp(setting.velocityMin.z, -FLT_MAX, setting.velocityMax.z);
                setting.velocityMax.z = std::clamp(setting.velocityMax.z, setting.velocityMin.z, FLT_MAX);

                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("加速度");
                ImGui::PopStyleColor();
                ImGui::DragFloat3("最初", &setting.startAcce.x, 0.001f);
                ImGui::DragFloat3("最後", &setting.endAcce.x, 0.001f);

                ImGui::Checkbox("乗算", &setting.isAcceMultiply);

                ImGui::DragFloat("重力", &setting.gravity, 0.01f, -FLT_MAX, FLT_MAX);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("大きさ")) {
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("大きさ");
                ImGui::PopStyleColor();

                if (setting.isRandomAllSize) {
                    ImGui::Checkbox("最初と最後同じ大きさ", &setting.isEndScale);

                    ImGui::DragFloat3("最大値", &setting.allScaleMax.x, 0.1f, 0.0f);
                    ImGui::DragFloat3("最小値", &setting.allScaleMin.x, 0.1f, 0.0f);
                    setting.allScaleMin.x = std::clamp(setting.allScaleMin.x, -FLT_MAX, setting.allScaleMax.x);
                    setting.allScaleMax.x = std::clamp(setting.allScaleMax.x, setting.allScaleMin.x, FLT_MAX);
                    setting.allScaleMin.y = std::clamp(setting.allScaleMin.y, -FLT_MAX, setting.allScaleMax.y);
                    setting.allScaleMax.y = std::clamp(setting.allScaleMax.y, setting.allScaleMin.y, FLT_MAX);
                    setting.allScaleMin.z = std::clamp(setting.allScaleMin.z, -FLT_MAX, setting.allScaleMax.z);
                    setting.allScaleMax.z = std::clamp(setting.allScaleMax.z, setting.allScaleMin.z, FLT_MAX);
                    if (!setting.isEndScale) {
                        ImGui::DragFloat3("最後", &setting.particleEndScale.x, 0.1f);
                    }
                } else if (setting.isRandomSize) {
                    ImGui::DragFloat("最大値", &setting.scaleMax, 0.1f, 0.0f);
                    ImGui::DragFloat("最小値", &setting.scaleMin, 0.1f, 0.0f);
                    setting.scaleMax = std::clamp(setting.scaleMax, setting.scaleMin, FLT_MAX);
                    setting.scaleMin = std::clamp(setting.scaleMin, 0.0f, setting.scaleMax);
                } else if (setting.isSinMove) {
                    ImGui::DragFloat3("最初", &setting.particleStartScale.x, 0.1f, 0.0f);
                } else {
                    ImGui::DragFloat3("最初", &setting.particleStartScale.x, 0.1f, 0.0f);
                    ImGui::DragFloat3("最後", &setting.particleEndScale.x, 0.1f);
                }

                ImGui::Checkbox("均等にランダムな大きさ", &setting.isRandomSize);
                ImGui::Checkbox("ばらばらにランダムな大きさ", &setting.isRandomAllSize);
                ImGui::Checkbox("sin波の動き", &setting.isSinMove);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("回転")) {
                if (!setting.isRandomRotate) {
                    float startRotationDegrees[3] = {
                        radiansToDegrees(setting.startRote.x),
                        radiansToDegrees(setting.startRote.y),
                        radiansToDegrees(setting.startRote.z)};
                    float endRotationDegrees[3] = {
                        radiansToDegrees(setting.endRote.x),
                        radiansToDegrees(setting.endRote.y),
                        radiansToDegrees(setting.endRote.z)};
                    if (ImGui::DragFloat3("最初", startRotationDegrees, 0.1f)) {
                        setting.startRote.x = degreesToRadians(startRotationDegrees[0]);
                        setting.startRote.y = degreesToRadians(startRotationDegrees[1]);
                        setting.startRote.z = degreesToRadians(startRotationDegrees[2]);
                    }
                    if (ImGui::DragFloat3("最後", endRotationDegrees, 0.1f)) {
                        setting.endRote.x = degreesToRadians(endRotationDegrees[0]);
                        setting.endRote.y = degreesToRadians(endRotationDegrees[1]);
                        setting.endRote.z = degreesToRadians(endRotationDegrees[2]);
                    }
                }
                if (setting.isRandomRotate) {
                    float startRotationDegrees[3] = {
                        radiansToDegrees(setting.rotateStartMax.x),
                        radiansToDegrees(setting.rotateStartMax.y),
                        radiansToDegrees(setting.rotateStartMax.z)};
                    float endRotationDegrees[3] = {
                        radiansToDegrees(setting.rotateStartMin.x),
                        radiansToDegrees(setting.rotateStartMin.y),
                        radiansToDegrees(setting.rotateStartMin.z)};

                    if (ImGui::DragFloat3("回転 最大値", startRotationDegrees, 0.1f)) {
                        setting.rotateStartMax.x = degreesToRadians(std::clamp(startRotationDegrees[0], radiansToDegrees(setting.rotateStartMin.x), 180.0f));
                        setting.rotateStartMax.y = degreesToRadians(std::clamp(startRotationDegrees[1], radiansToDegrees(setting.rotateStartMin.y), 180.0f));
                        setting.rotateStartMax.z = degreesToRadians(std::clamp(startRotationDegrees[2], radiansToDegrees(setting.rotateStartMin.z), 180.0f));
                    }

                    if (ImGui::DragFloat3("回転 最小値", endRotationDegrees, 0.1f)) {
                        setting.rotateStartMin.x = degreesToRadians(std::clamp(endRotationDegrees[0], -180.0f, radiansToDegrees(setting.rotateStartMax.x)));
                        setting.rotateStartMin.y = degreesToRadians(std::clamp(endRotationDegrees[1], -180.0f, radiansToDegrees(setting.rotateStartMax.y)));
                        setting.rotateStartMin.z = degreesToRadians(std::clamp(endRotationDegrees[2], -180.0f, radiansToDegrees(setting.rotateStartMax.z)));
                    }

                    ImGui::Checkbox("ランダムな回転速度", &setting.isRotateVelocity);

                    if (setting.isRotateVelocity) {
                        ImGui::DragFloat3("最大値", &setting.rotateVelocityMax.x, 0.01f);
                        ImGui::DragFloat3("最小値", &setting.rotateVelocityMin.x, 0.01f);
                        setting.rotateVelocityMin.x = std::clamp(setting.rotateVelocityMin.x, -FLT_MAX, setting.rotateVelocityMax.x);
                        setting.rotateVelocityMax.x = std::clamp(setting.rotateVelocityMax.x, setting.rotateVelocityMin.x, FLT_MAX);
                        setting.rotateVelocityMin.y = std::clamp(setting.rotateVelocityMin.y, -FLT_MAX, setting.rotateVelocityMax.y);
                        setting.rotateVelocityMax.y = std::clamp(setting.rotateVelocityMax.y, setting.rotateVelocityMin.y, FLT_MAX);
                        setting.rotateVelocityMin.z = std::clamp(setting.rotateVelocityMin.z, -FLT_MAX, setting.rotateVelocityMax.z);
                        setting.rotateVelocityMax.z = std::clamp(setting.rotateVelocityMax.z, setting.rotateVelocityMin.z, FLT_MAX);
                    }
                }

                ImGui::Checkbox("ランダムな回転", &setting.isRandomRotate);
                ImGui::Checkbox("進行方向に向ける", &setting.isFaceDirection);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("トレイル設定")) {
                if (ImGui::Checkbox("トレイルを有効にする", &setting.enableTrail)) {
                    SetTrailEnabled(selectedGroup, setting.enableTrail);
                }

                if (setting.enableTrail) {
                    ImGui::Indent();

                    if (ImGui::DragFloat("トレイル生成間隔", &setting.trailSpawnInterval, 0.001f, 0.001f, 10.0f)) {
                        SetTrailInterval(selectedGroup, setting.trailSpawnInterval);
                    }
                    if (ImGui::DragFloat("トレイル生存時間スケール", &setting.trailLifeScale, 0.01f)) {
                        SetTrailLifeScale(selectedGroup, setting.trailLifeScale);
                    }
                    if (ImGui::DragFloat3("トレイルスケール倍率", &setting.trailScaleMultiplier.x, 0.01f)) {
                        SetTrailScaleMultiplier(selectedGroup, setting.trailScaleMultiplier);
                    }
                    if (ImGui::DragFloat4("トレイル色彩倍率", &setting.trailColorMultiplier.x, 0.01f)) {
                        SetTrailColorMultiplier(selectedGroup, setting.trailColorMultiplier);
                    }

                    if (ImGui::Checkbox("トレイル速度継承", &setting.trailInheritVelocity)) {
                        SetTrailVelocityInheritance(selectedGroup, setting.trailInheritVelocity, setting.trailVelocityScale);
                    }

                    if (setting.trailInheritVelocity) {
                        if (ImGui::DragFloat("トレイル速度スケール", &setting.trailVelocityScale, 0.01f)) {
                            SetTrailVelocityInheritance(selectedGroup, setting.trailInheritVelocity, setting.trailVelocityScale);
                        }
                    }
                    ImGui::Unindent();
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("色彩")) {
                if (ImGui::TreeNode("色")) {
                    ImGui::ColorEdit4("開始色", &setting.startColor.x);
                    ImGui::ColorEdit4("終了色", &setting.endColor.x);
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode("透明度")) {
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::TextUnformatted("透明度の設定");
                    ImGui::PopStyleColor();
                    ImGui::DragFloat("最大値", &setting.alphaMax, 0.01f, 0.0f, 1.0f);
                    ImGui::DragFloat("最小値", &setting.alphaMin, 0.01f, 0.0f, 1.0f);
                    setting.alphaMin = std::clamp(setting.alphaMin, 0.0f, setting.alphaMax);
                    setting.alphaMax = std::clamp(setting.alphaMax, setting.alphaMin, 1.0f);
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
        }

        ImGui::Spacing();

        if (ImGui::CollapsingHeader("パーティクルの数、間隔")) {
            ImGui::DragFloat("間隔", &emitFrequency_, 0.001f, 0.001f, 100.0f);
            ImGui::InputInt("数", reinterpret_cast<int *>(&setting.count), 1, 100);
            setting.count = std::clamp(static_cast<int>(setting.count), 0, 10000);
        }

        ImGui::Spacing();

        if (ImGui::CollapsingHeader("各状態の設定")) {
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("レンダリング設定");
            ImGui::PopStyleColor();

            if (ImGui::TreeNode("ビルボード関連")) {
                ImGui::Checkbox("ビルボード", &setting.isBillboard);
                ImGui::SetItemTooltip("パーティクルを常にカメラに向ける");

                ImGui::Checkbox("Xビルボード", &setting.isBillboardX);
                ImGui::SetItemTooltip("パーティクルのX軸を常にカメラに向ける");

                ImGui::Checkbox("Yビルボード", &setting.isBillboardY);
                ImGui::SetItemTooltip("パーティクルのY軸を常にカメラに向ける");

                ImGui::Checkbox("Zビルボード", &setting.isBillboardZ);
                ImGui::SetItemTooltip("パーティクルのZ軸を常にカメラに向ける");

                ImGui::Checkbox("ランダムカラー", &setting.isRandomColor);
                ImGui::SetItemTooltip("パーティクルごとに異なる色を適用");
                ImGui::TreePop();
            }

            ImGui::Spacing();

            if (ImGui::TreeNode("ブレンドモード")) {
                ShowBlendModeCombo(setting.blendMode);
                ImGui::TreePop();
            }
        }

        ImGui::Spacing();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("グループがありません。");
        ImGui::PopStyleColor();
    }

    if (ImGui::CollapsingHeader("グループ管理")) {

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

        for (const auto &group : allGroups) {
            const std::string &name = group->GetGroupName();
            if (emitterGroupNames.contains(name)) {
                attachedNames.push_back(name);
            } else {
                availableNames.push_back(name);
            }
        }

        availableItems.clear();
        attachedItems.clear();
        for (auto &name : availableNames) {
            availableItems.push_back(name.c_str());
        }
        for (auto &name : attachedNames) {
            attachedItems.push_back(name.c_str());
        }

        while (!leftSelected.empty() && leftSelected.back() >= availableNames.size())
            leftSelected.pop_back();
        while (!rightSelected.empty() && rightSelected.back() >= attachedNames.size())
            rightSelected.pop_back();

        // ドラッグ＆ドロップで利用可能 ⇔ アタッチ済み を移動する。ループ後にまとめて反映
        std::string dndAttachName;   // 利用可能 → アタッチ
        std::string dndDetachName;   // アタッチ → 解除

        float width = ImGui::GetContentRegionAvail().x;
        float halfWidth = width * 0.45f;

        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::Text("利用可能なグループ");
        ImGui::SameLine(width - halfWidth - 50);
        ImGui::Text("アタッチ済みグループ");
        ImGui::PopStyleColor();

        // ── 左: 利用可能（ドラッグ元 / アタッチ済みのドロップ先=解除）──
        ImGui::BeginChild("available_groups", ImVec2(halfWidth, 200), ImGuiChildFlags_Borders);
        if (availableItems.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("利用可能なグループがありません");
            ImGui::PopStyleColor();
        } else {
            for (int i = 0; i < availableItems.size(); i++) {
                bool isSelected = std::find(leftSelected.begin(), leftSelected.end(), i) != leftSelected.end();
                if (ImGui::Selectable(availableItems[i], isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (!ImGui::GetIO().KeyCtrl)
                        leftSelected.clear();

                    auto it = std::find(leftSelected.begin(), leftSelected.end(), i);
                    if (it != leftSelected.end())
                        leftSelected.erase(it);
                    else
                        leftSelected.push_back(i);

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        ParticleGroup *group = ParticleGroupManager::GetInstance()->GetParticleGroup(availableNames[i]);
                        if (group) {
                            AddParticleGroup(group);
                            particleGroupNames_ = Manager_->GetParticleGroupsName();
                        }
                        leftSelected.clear();
                    }
                }
                // 利用可能アイテムをドラッグ元に
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("PG_AVAIL", &i, sizeof(int));
                    ImGui::Text("追加: %s", availableItems[i]);
                    ImGui::EndDragDropSource();
                }
            }
        }
        // アタッチ済みをこの領域にドロップ → 解除
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 8.0f)));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("PG_ATTACHED")) {
                int idx = *static_cast<const int *>(payload->Data);
                if (idx >= 0 && idx < (int)attachedNames.size())
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
        if (ImGui::Button("追加 >>", ImVec2(80, 35))) {
            int moved = 0;
            for (auto it = leftSelected.rbegin(); it != leftSelected.rend(); ++it) {
                int idx = *it;
                ParticleGroup *group = ParticleGroupManager::GetInstance()->GetParticleGroup(availableNames[idx]);
                if (group) {
                    AddParticleGroup(group);
                    particleGroupNames_ = Manager_->GetParticleGroupsName();
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
        if (ImGui::Button("<< 削除", ImVec2(80, 35))) {
            int moved = 0;
            for (auto it = rightSelected.rbegin(); it != rightSelected.rend(); ++it) {
                int idx = *it;
                RemoveParticleGroup(attachedNames[idx]);
                particleGroupNames_ = Manager_->GetParticleGroupsName();
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
        if (attachedItems.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("アタッチされたグループがありません");
            ImGui::PopStyleColor();
        } else {
            for (int i = 0; i < attachedItems.size(); i++) {
                bool isSelected = std::find(rightSelected.begin(), rightSelected.end(), i) != rightSelected.end();
                if (ImGui::Selectable(attachedItems[i], isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (!ImGui::GetIO().KeyCtrl)
                        rightSelected.clear();

                    auto it = std::find(rightSelected.begin(), rightSelected.end(), i);
                    if (it != rightSelected.end())
                        rightSelected.erase(it);
                    else
                        rightSelected.push_back(i);

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        RemoveParticleGroup(attachedNames[i]);
                        particleGroupNames_ = Manager_->GetParticleGroupsName();
                        rightSelected.clear();
                    }
                }
                // アタッチ済みアイテムをドラッグ元に
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("PG_ATTACHED", &i, sizeof(int));
                    ImGui::Text("解除: %s", attachedItems[i]);
                    ImGui::EndDragDropSource();
                }
            }
        }
        // 利用可能をこの領域にドロップ → 追加
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 8.0f)));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("PG_AVAIL")) {
                int idx = *static_cast<const int *>(payload->Data);
                if (idx >= 0 && idx < (int)availableNames.size())
                    dndAttachName = availableNames[idx];
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::EndChild();

        // ドロップ確定をまとめて反映する
        if (!dndAttachName.empty()) {
            ParticleGroup *group = ParticleGroupManager::GetInstance()->GetParticleGroup(dndAttachName);
            if (group) {
                AddParticleGroup(group);
                particleGroupNames_ = Manager_->GetParticleGroupsName();
                ImGuiNotification::Post("グループをアタッチしました: " + dndAttachName, {0.45f, 0.68f, 0.52f, 1.0f});
            }
        }
        if (!dndDetachName.empty()) {
            RemoveParticleGroup(dndDetachName);
            particleGroupNames_ = Manager_->GetParticleGroupsName();
            ImGuiNotification::Post("グループを解除しました: " + dndDetachName, {0.82f, 0.58f, 0.36f, 1.0f});
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("操作: Ctrl + クリックで複数選択 / ダブルクリック または ドラッグ＆ドロップで追加・削除");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("ファイル操作")) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.58f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.52f, 0.70f, 0.95f));
        if (ImGui::Button("設定を保存", ImVec2(140, 32))) {
            SaveToJson();
            datas_->Flush();
            ImGuiNotification::Post("パーティクル設定を保存しました", {0.45f, 0.68f, 0.52f, 1.0f});
        }
        ImGui::PopStyleColor(2);
        ImGui::SetItemTooltip("現在のパーティクル設定をJSONファイルに保存します");
        ImGui::Spacing();
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("エミッター制御")) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
        ImGui::Checkbox("自動生成", &isAuto_);
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("パーティクルを自動的に継続生成します");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgBlue);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.60f, 0.78f, 0.40f));
        if (ImGui::Button("手動生成", ImVec2(110, 0))) {
            UpdateOnce();
        }
        ImGui::PopStyleColor(2);
        ImGui::SetItemTooltip("パーティクルを1回だけ生成します");
        ImGui::Spacing();
    }
#endif // USE_IMGUI
}

void ParticleEmitter::Debug() {
#ifdef _DEBUG
    if (!name_.empty() && Manager_) {
        DebugParticleData();
    }
#endif
}
#pragma endregion

bool ParticleEmitter::IsAllParticlesComplete() {
    return Manager_->IsAllParticlesComplete();
}

void ParticleEmitter::AddParticleGroup(ParticleGroup *particleGroup) {
    if (!particleGroup)
        return;

    const std::string &groupName = particleGroup->GetGroupName();

    ParticleGroup *independentGroup = ParticleGroupManager::GetInstance()->GetIndependentParticleGroup(groupName);
    if (!independentGroup) {
        return;
    }

    auto it = particleSettings_.find(groupName);
    if (it == particleSettings_.end()) {
        particleSettings_[groupName] = DefaultSetting();
    }

    Manager_->AddParticleGroup(independentGroup);
}

std::unique_ptr<ParticleEmitter> ParticleEmitter::Clone() const {
    auto newEmitter = std::make_unique<ParticleEmitter>();

    newEmitter->SetName(this->name_);
    newEmitter->SetFrequency(this->emitFrequency_);
    newEmitter->SetActive(this->isActive_);
    newEmitter->isAuto_ = this->isAuto_;
    newEmitter->isVisible_ = this->isVisible_;
    newEmitter->isGizmoSelectable_ = this->isGizmoSelectable_;
    newEmitter->transform_ = this->transform_;
    newEmitter->particleSettings_ = this->particleSettings_;
    newEmitter->particleGroupNames_ = this->particleGroupNames_;
    newEmitter->lastTranslation_ = this->lastTranslation_;
    newEmitter->lastRotation_ = this->lastRotation_;
    newEmitter->lastScale_ = this->lastScale_;

    newEmitter->Manager_ = std::make_unique<ParticleManager>();
    newEmitter->Manager_->Initialize(SrvManager::GetInstance());

    for (const auto &groupName : particleGroupNames_) {
        ParticleGroup *group = ParticleGroupManager::GetInstance()->GetParticleGroup(groupName);
        if (group) {
            newEmitter->AddParticleGroup(group);
        }
    }
    return newEmitter;
}

void ParticleEmitter::SetTrailEnabled(const std::string &groupName, bool enabled) {
    if (particleSettings_.find(groupName) != particleSettings_.end()) {
        particleSettings_[groupName].enableTrail = enabled;
        if (Manager_) {
            Manager_->SetTrailEnabled(groupName, enabled);
        }
    }
}

void ParticleEmitter::SetTrailInterval(const std::string &groupName, float interval) {
    if (particleSettings_.find(groupName) != particleSettings_.end()) {
        particleSettings_[groupName].trailSpawnInterval = interval;
    }
}

void ParticleEmitter::SetMaxTrailParticles(const std::string &groupName, int maxTrails) {
    if (particleSettings_.find(groupName) != particleSettings_.end()) {
        particleSettings_[groupName].maxTrailParticles = maxTrails;
        if (Manager_) {
            Manager_->SetTrailSettings(groupName,
                                       particleSettings_[groupName].trailSpawnInterval, maxTrails);
        }
    }
}

void ParticleEmitter::SetTrailLifeScale(const std::string &groupName, float scale) {
    if (particleSettings_.find(groupName) != particleSettings_.end()) {
        particleSettings_[groupName].trailLifeScale = scale;
    }
}

void ParticleEmitter::SetTrailScaleMultiplier(const std::string &groupName, const Vector3 &multiplier) {
    if (particleSettings_.find(groupName) != particleSettings_.end()) {
        particleSettings_[groupName].trailScaleMultiplier = multiplier;
    }
}

void ParticleEmitter::SetTrailColorMultiplier(const std::string &groupName, const Vector4 &multiplier) {
    if (particleSettings_.find(groupName) != particleSettings_.end()) {
        particleSettings_[groupName].trailColorMultiplier = multiplier;
    }
}

void ParticleEmitter::SetTrailVelocityInheritance(const std::string &groupName, bool inherit, float scale) {
    if (particleSettings_.find(groupName) != particleSettings_.end()) {
        particleSettings_[groupName].trailInheritVelocity = inherit;
        particleSettings_[groupName].trailVelocityScale = scale;
    }
}

void ParticleEmitter::ShowBlendModeCombo(BlendMode &currentMode) {
#ifdef USE_IMGUI
    static const char *blendModeItems[] = {
        "なし",
        "通常",
        "加算",
        "減算",
        "乗算",
        "スクリーン"};

    int currentIndex = static_cast<int>(currentMode);

    if (ImGui::Combo("ブレンドモード", &currentIndex, blendModeItems, IM_ARRAYSIZE(blendModeItems))) {
        currentMode = static_cast<BlendMode>(currentIndex);
    }
#endif // USE_IMGUI
}
} // namespace Hagine
