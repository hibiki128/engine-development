#define NOMINMAX
#include "ParticleEmitter.h"
#include <attachment/AttachmentManager.h>
#include "Frame.h"
#include "line/LineRenderer.h"
#include <render/deferred/DeferredRenderer.h>
#include <shadow/ShadowMap.h>

#include "../utility/debug/imgui/ImGuiNotification.h"
#include "../utility/debug/imgui/ImGuizmoManager.h"
#include "../utility/debug/imgui/DebugUIHelper.h"
#include "ParticleGroupManager.h"
#include <particle/ParticleEditor.h>
#include <set>
#include <type/Quaternion.h>

// コンストラクタ
namespace Hagine {
namespace {
// 生きているエミッター（カメラビュー窓で描くため）
std::vector<ParticleEmitter *> &Registry()
{
    static std::vector<ParticleEmitter *> registry;
    return registry;
}
} // namespace

ParticleEmitter::ParticleEmitter()
{
    Registry().push_back(this);
}

void ParticleEmitter::DrawAllForView(const ViewProjection &viewProjection)
{
    for (ParticleEmitter *pEmitter : Registry())
    {
        if (pEmitter->drawnInMain_ && pEmitter->particleManager_)
        {
            pEmitter->particleManager_->DrawForView(viewProjection);
        }
    }
}

void ParticleEmitter::ClearDrawnMarks()
{
    for (ParticleEmitter *pEmitter : Registry())
    {
        pEmitter->drawnInMain_ = false;
    }
}

ParticleEmitter::~ParticleEmitter()
{
    std::erase(Registry(), this);

#ifdef USE_IMGUI
    // ギズモには transform_（メンバ）のアドレスを渡しているので、
    // 解除し忘れると破棄後のメモリを掴んだままになる。
    // 同名で登録し直されている場合は相手の登録を消さない。
    if (gizmoRegistered_)
    {
        ImGuizmoManager::GetInstance()->RemoveTargetIfOwnedBy(name_, &transform_);
        gizmoRegistered_ = false;
    }
#endif // USE_IMGUI

    // 親子付けの登録も外す（ギズモと違い Release でも登録している）
    if (attachRegistered_)
    {
        AttachmentManager::GetInstance()->Unregister(AttachName(name_));
        attachRegistered_ = false;
    }

    // 保有していた独立グループを破棄する。
    // 返さないとシーンを行き来するたびに積み上がり、SRVもバッファも戻らない
    for (ParticleGroup *group : ownedIndependentGroups_)
    {
        ParticleGroupManager::GetInstance()->ReleaseIndependentGroup(group);
    }
    ownedIndependentGroups_.clear();
}

void ParticleEmitter::Initialize(std::string name)
{
    transform_.Initialize();
    if (!name.empty())
    {
        name_ = name;
        datas_ = std::make_unique<DataHandler>("Particle", name);
        LoadFromJson();
        particleManager_ = std::make_unique<ParticleManager>();
        particleManager_->Initialize(SrvManager::GetInstance());
        LoadParticleGroup();
        datas_ = std::make_unique<DataHandler>("Particle", name);
    }
    SyncSettingsToTransform();
    lastTranslation_ = transform_.translation_;
    lastRotation_ = transform_.quaternionRotation_;
    lastScale_ = transform_.scale_;
    ImGuiNotification::Post("パーティクルエミッターを初期化しました: " + name_, {0.2f, 0.8f, 0.8f, 1.0f});
#ifdef USE_IMGUI
    // WorldTransformのアドレスを渡す
    ImGuizmoManager::GetInstance()->AddTarget(name_, &transform_, isGizmoSelectable_);
    // WorldTransform単体の既定はObjectなので、パーティクルとして明示的に分類し直す
    ImGuizmoManager::GetInstance()->SetCategory(name_, GizmoCategory::Particle);
    gizmoRegistered_ = true;
#endif

    // 種類をまたいだ親子付けの対象として登録する（オブジェクトに付けられるようにする）
    {
        AttachTarget target;
        target.kind = AttachKind::Particle;
        target.name = AttachName(name_);
        target.worldTransform = &transform_;
        AttachmentManager::GetInstance()->Register(target);
        attachRegistered_ = true;
    }
}

void ParticleEmitter::Update()
{
    // 経過時間を進める
    elapsedTime_ += Frame::UnscaledDeltaTime();

    // 発生頻度に基づいてパーティクルを発生させる
    while (elapsedTime_ >= emitFrequency_)
    {
        Emit();                         // パーティクルを発生させる
        elapsedTime_ -= emitFrequency_; // 過剰に進んだ時間を考慮
    }
}

void ParticleEmitter::UpdateOnce()
{
    SyncSettingsToTransform();
    EmitInternal();
}

void ParticleEmitter::Draw(const ViewProjection &vp_)
{
    // パーティクルは半透明なのでG-Bufferには載せず、前方描画フェーズで描く
    if (ShadowMap::GetInstance()->IsShadowPassActive() ||
        DeferredRenderer::GetInstance()->IsGBufferPassActive())
        return;
    drawnInMain_ = true; // カメラビュー窓にも出す印
    particleManager_->SetEmitterCenter(transform_.translation_);

    transform_.UpdateMatrix();
    if (particleManager_)
    {
        particleManager_->Update(vp_);
        particleManager_->Draw();
    }
    DrawEmitter();

    size_t activeCount = particleManager_->GetActiveParticleCount();
    ParticleEditor::GetInstance()->SetExternalParticleCount(name_, activeCount);
}

void ParticleEmitter::DrawEmitter()
{
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
    Matrix4x4 worldMatrix = MakeAffineMatrix(transform_.scale_, transform_.quaternionRotation_, transform_.translation_);
    for (size_t i = 0; i < localVertices.size(); i++)
    {
        worldVertices[i] = Transformation(localVertices[i], worldMatrix);
    }
    constexpr std::array<std::pair<int, int>, 12> edges = {
        std::make_pair(0, 1), std::make_pair(1, 3), std::make_pair(3, 2), std::make_pair(2, 0),
        std::make_pair(4, 5), std::make_pair(5, 7), std::make_pair(7, 6), std::make_pair(6, 4),
        std::make_pair(0, 4), std::make_pair(1, 5), std::make_pair(2, 6), std::make_pair(3, 7)};
    for (const auto &edge : edges)
    {
        LineRenderer::GetInstance()->AddLine(worldVertices[edge.first], worldVertices[edge.second], {1.0f, 1.0f, 1.0f, 1.0f});
    }
}

void ParticleEmitter::SyncSettingsToTransform()
{
    if (!particleManager_)
        return;
    for (auto &[groupName, setting] : particleSettings_)
    {
        setting.translate = transform_.translation_;
        setting.rotation = transform_.quaternionRotation_.ToEulerAngles();
        setting.scale = transform_.scale_;
        particleManager_->SetParticleSetting(groupName, setting);
    }
}

void ParticleEmitter::EmitInternal()
{
    if (particleManager_)
    {
        particleManager_->Emit();
    }
}

void ParticleEmitter::Emit()
{
    SyncSettingsToTransform();
    EmitInternal();
}

void ParticleEmitter::SaveToJsonAs(const std::string &name)
{
    // 保存先だけを差し替えて書き、元へ戻す
    std::unique_ptr<DataHandler> original = std::move(datas_);
    datas_ = std::make_unique<DataHandler>("Particle", name);
    SaveToJson();
    datas_ = std::move(original);
}

void ParticleEmitter::SaveToJson()
{
    datas_->Save("emitterTranslation", transform_.translation_);
    datas_->Save("emitterRotation", transform_.quaternionRotation_);
    datas_->Save("emitterScale", transform_.scale_);
    datas_->Save("GroupNames", particleGroupNames_);
    datas_->Save("emitFrequency", emitFrequency_);
    datas_->Save("isVisible", isVisible_);
    datas_->Save("isActive", isActive_);
    datas_->Save("isAuto", isAuto_);
    datas_->Save("isGizmoSelectable", isGizmoSelectable_);
    datas_->Save("drawGroup", drawGroup_);
    for (const auto &[groupName, setting] : particleSettings_)
    {
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
        datas_->Save(groupName + "_startRotate", setting.startRotate);
        datas_->Save(groupName + "_endRotate", setting.endRotate);
        datas_->Save(groupName + "_rotateStartMax", setting.rotateStartMax);
        datas_->Save(groupName + "_rotateStartMin", setting.rotateStartMin);
        datas_->Save(groupName + "_rotateVelocityMin", setting.rotateVelocityMin);
        datas_->Save(groupName + "_rotateVelocityMax", setting.rotateVelocityMax);
        datas_->Save(groupName + "_allScaleMin", setting.allScaleMin);
        datas_->Save(groupName + "_allScaleMax", setting.allScaleMax);
        datas_->Save(groupName + "_isRandomScale", setting.isRandomSize);
        datas_->Save(groupName + "_isAllRandomScale", setting.isRandomAllSize);
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
        particleManager_->SetParticleSetting(groupName, setting);
    }
    ImGuiNotification::Post("パーティクルデータを保存しました: " + name_, {0.2f, 0.8f, 0.2f, 1.0f});
}

void ParticleEmitter::LoadFromJson()
{
    transform_.translation_ = datas_->Load<Vector3>("emitterTranslation", {0, 0, 0});
    transform_.quaternionRotation_ = datas_->Load<Quaternion>("emitterRotation", Quaternion::IdentityQuaternion());
    transform_.scale_ = datas_->Load<Vector3>("emitterScale", {1, 1, 1});
    particleGroupNames_ = datas_->Load<std::vector<std::string>>("GroupNames", {});
    emitFrequency_ = datas_->Load<float>("emitFrequency", 0.1f);
    isVisible_ = datas_->Load<bool>("isVisible", true);
    isActive_ = datas_->Load<bool>("isActive", false);
    isAuto_ = datas_->Load<bool>("isAuto", false);
    isGizmoSelectable_ = datas_->Load<bool>("isGizmoSelectable", true);
    drawGroup_ = datas_->Load<std::string>("drawGroup", "3D");
    if (drawGroup_ != "UI")
    {
        drawGroup_ = "3D"; // 旧データは3D扱いに正規化
    }

    for (const auto &groupName : particleGroupNames_)
    {
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
        setting.startRotate = datas_->Load<Vector3>(groupName + "_startRotate", {0, 0, 0});
        setting.endRotate = datas_->Load<Vector3>(groupName + "_endRotate", {0, 0, 0});
        setting.rotateStartMax = datas_->Load<Vector3>(groupName + "_rotateStartMax", {0, 0, 0});
        setting.rotateStartMin = datas_->Load<Vector3>(groupName + "_rotateStartMin", {0, 0, 0});
        setting.rotateVelocityMin = datas_->Load<Vector3>(groupName + "_rotateVelocityMin", {-0.07f, -0.07f, -0.07f});
        setting.rotateVelocityMax = datas_->Load<Vector3>(groupName + "_rotateVelocityMax", {0.07f, 0.07f, 0.07f});
        setting.allScaleMin = datas_->Load<Vector3>(groupName + "_allScaleMin", {0, 0, 0});
        setting.allScaleMax = datas_->Load<Vector3>(groupName + "_allScaleMax", {1, 1, 1});
        setting.isRandomSize = datas_->Load<bool>(groupName + "_isRandomScale", false);
        setting.isRandomAllSize = datas_->Load<bool>(groupName + "_isAllRandomScale", false);
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
        setting.blendMode = datas_->Load<BlendMode>(groupName + "_blendMode", BlendMode::Add);
        particleSettings_[groupName] = setting;
    }
}

void ParticleEmitter::LoadParticleGroup()
{
    for (auto &particleGroupname : particleGroupNames_)
    {
        AddParticleGroup(ParticleGroupManager::GetInstance()->GetParticleGroup(particleGroupname));
    }
    if (!particleGroupNames_.empty())
    {
        ImGuiNotification::Post("パーティクルグループを読み込みました: " + name_, {0.2f, 0.8f, 0.8f, 1.0f});
    }
}

ParticleSetting ParticleEmitter::DefaultSetting()
{
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
    setting.startRotate = {0, 0, 0};
    setting.endRotate = {0, 0, 0};
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

void ParticleEmitter::Debug()
{
#ifdef USE_IMGUI
    if (!name_.empty() && particleManager_)
    {
        DebugParticleData();
    }
#endif
}
#pragma endregion

bool ParticleEmitter::IsAllParticlesComplete()
{
    return particleManager_->IsAllParticlesComplete();
}

void ParticleEmitter::AddParticleGroup(ParticleGroup *particleGroup)
{
    if (!particleGroup)
        return;

    const std::string &groupName = particleGroup->GetGroupName();

    ParticleGroup *independentGroup = ParticleGroupManager::GetInstance()->GetIndependentParticleGroup(groupName);
    if (!independentGroup)
    {
        return;
    }
    ownedIndependentGroups_.push_back(independentGroup); // 破棄時に返すため覚えておく

    auto it = particleSettings_.find(groupName);
    if (it == particleSettings_.end())
    {
        particleSettings_[groupName] = DefaultSetting();
    }

    particleManager_->AddParticleGroup(independentGroup);
}

std::unique_ptr<ParticleEmitter> ParticleEmitter::Clone() const
{
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

    newEmitter->particleManager_ = std::make_unique<ParticleManager>();
    newEmitter->particleManager_->Initialize(SrvManager::GetInstance());

    for (const auto &groupName : particleGroupNames_)
    {
        ParticleGroup *pGroup = ParticleGroupManager::GetInstance()->GetParticleGroup(groupName);
        if (pGroup)
        {
            newEmitter->AddParticleGroup(pGroup);
        }
    }
    return newEmitter;
}

void ParticleEmitter::SetTrailEnabled(const std::string &groupName, bool enabled)
{
    if (particleSettings_.find(groupName) != particleSettings_.end())
    {
        particleSettings_[groupName].enableTrail = enabled;
        if (particleManager_)
        {
            particleManager_->SetTrailEnabled(groupName, enabled);
        }
    }
}

void ParticleEmitter::SetTrailInterval(const std::string &groupName, float interval)
{
    if (particleSettings_.find(groupName) != particleSettings_.end())
    {
        particleSettings_[groupName].trailSpawnInterval = interval;
    }
}

void ParticleEmitter::SetMaxTrailParticles(const std::string &groupName, int maxTrails)
{
    if (particleSettings_.find(groupName) != particleSettings_.end())
    {
        particleSettings_[groupName].maxTrailParticles = maxTrails;
        if (particleManager_)
        {
            particleManager_->SetTrailSettings(groupName,
                                       particleSettings_[groupName].trailSpawnInterval, maxTrails);
        }
    }
}

void ParticleEmitter::SetTrailLifeScale(const std::string &groupName, float scale)
{
    if (particleSettings_.find(groupName) != particleSettings_.end())
    {
        particleSettings_[groupName].trailLifeScale = scale;
    }
}

void ParticleEmitter::SetTrailScaleMultiplier(const std::string &groupName, const Vector3 &multiplier)
{
    if (particleSettings_.find(groupName) != particleSettings_.end())
    {
        particleSettings_[groupName].trailScaleMultiplier = multiplier;
    }
}

void ParticleEmitter::SetTrailColorMultiplier(const std::string &groupName, const Vector4 &multiplier)
{
    if (particleSettings_.find(groupName) != particleSettings_.end())
    {
        particleSettings_[groupName].trailColorMultiplier = multiplier;
    }
}

void ParticleEmitter::SetTrailVelocityInheritance(const std::string &groupName, bool inherit, float scale)
{
    if (particleSettings_.find(groupName) != particleSettings_.end())
    {
        particleSettings_[groupName].trailInheritVelocity = inherit;
        particleSettings_[groupName].trailVelocityScale = scale;
    }
}

void ParticleEmitter::ShowBlendModeCombo(BlendMode &currentMode)
{
#ifdef USE_IMGUI
    static const char *blendModeItems[] = {
        "なし",
        "通常",
        "加算",
        "減算",
        "乗算",
        "スクリーン"};

    int currentIndex = static_cast<int>(currentMode);

    if (ImGui::Combo("ブレンドモード", &currentIndex, blendModeItems, IM_ARRAYSIZE(blendModeItems)))
    {
        currentMode = static_cast<BlendMode>(currentIndex);
    }
#endif // USE_IMGUI
}

#ifdef USE_IMGUI
// -------------------------------------------------------
// Undo/Redo 用の状態キャプチャ・復元
// -------------------------------------------------------

// ParticleSetting 全フィールドのJSON変換（Undoスナップショット用）
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ParticleSetting,
    maxTrailParticles, gatherStartRatio, gatherStrength, trailSpawnInterval, trailLifeScale,
    lifeTimeMin, lifeTimeMax, gravity, alphaMin, alphaMax, scaleMin, scaleMax, trailVelocityScale,
    translate, rotation, scale, velocityMin, velocityMax, particleStartScale, particleEndScale,
    startAcce, endAcce, startRotate, endRotate, rotateVelocityMin, rotateVelocityMax,
    allScaleMax, allScaleMin, rotateStartMax, rotateStartMin, trailScaleMultiplier,
    startColor, endColor, trailColorMultiplier, count,
    enableTrail, trailInheritVelocity, isRandomColor, isBillboard, isBillboardX, isBillboardY, isBillboardZ,
    isRandomRotate, isRotateVelocity, isAcceMultiply, isRandomSize, isRandomAllSize, isSinMove,
    isFaceDirection, isEndScale, isEmitOnEdge, isGatherMode, blendMode)

nlohmann::json ParticleEmitter::CaptureUndoState() const
{
    json s;
    s["frequency"] = emitFrequency_;
    s["isVisible"] = isVisible_;
    s["isActive"] = isActive_;
    s["isAuto"] = isAuto_;
    s["drawGroup"] = drawGroup_;
    s["translation"] = transform_.translation_;
    s["rotation"] = transform_.quaternionRotation_;
    s["scale"] = transform_.scale_;

    json groups = json::object();
    for (const auto &[groupName, setting] : particleSettings_)
    {
        groups[groupName] = setting;
    }
    s["groups"] = groups;
    return s;
}

void ParticleEmitter::RestoreUndoState(const nlohmann::json &state)
{
    if (!state.is_object())
    {
        return;
    }

    emitFrequency_ = state.value("frequency", emitFrequency_);
    isVisible_ = state.value("isVisible", isVisible_);
    isActive_ = state.value("isActive", isActive_);
    isAuto_ = state.value("isAuto", isAuto_);
    drawGroup_ = state.value("drawGroup", drawGroup_);

    if (state.contains("translation"))
    {
        transform_.translation_ = state["translation"].get<Vector3>();
    }
    if (state.contains("rotation"))
    {
        transform_.quaternionRotation_ = state["rotation"].get<Quaternion>();
    }
    if (state.contains("scale"))
    {
        transform_.scale_ = state["scale"].get<Vector3>();
    }

    // 既存グループの設定のみ復元する（グループの追加・削除自体は対象外）
    if (state.contains("groups") && state["groups"].is_object())
    {
        for (auto it = state["groups"].begin(); it != state["groups"].end(); ++it)
        {
            auto found = particleSettings_.find(it.key());
            if (found == particleSettings_.end() || it.value().is_null())
            {
                continue;
            }
            found->second = it.value().get<ParticleSetting>();
            FlushSetting(it.key());
        }
    }
}
#endif // USE_IMGUI
} // namespace Hagine
