#define NOMINMAX
#include "BaseObject.h"
#include "BaseObjectManager.h"
#include "browser/ShowFolder.h"
#include "collider/CollisionManager.h"
#include "collider/ColliderTagManager.h"
#include "debug/profiler/CpuProfiler.h"
#include "frame/Frame.h"
#include "model/material/Material.h"
#include "object/Object3dInstancing.h"
#include "scene/SceneManager.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include <algorithm>
#include <cmath>
#ifdef USE_IMGUI
#include "utility/debug/imgui/AssetDragDrop.h"
#include <asset/AssetPath.h>
#include <edit/play/PlayModeManager.h>
#include <graphics/texture/TextureManager.h>
#include <imgui_internal.h>
#include <implot.h>
#endif // DEBUG

// コライダーの追加と簡易物理（押し戻し・重力）。剛体シミュレーションは PhysicsWorld
namespace Hagine {

// 保存済みのコライダー設定を読み込み（CollisionManager への登録前に呼ぶ）
void BaseObject::LoadColliderIfSaved(ColliderBase *collider) {
    if (!collider)
        return;
    DataHandler probe("Collider", collider->GetName());
    if (probe.Exists()) {
        collider->LoadFromJson();
    }
}

std::string BaseObject::MakeColliderName(ColliderType type) const {
    // 連番前の名前を基準に、同名オブジェクト間で保存ファイルを共有
    const std::string base = dataName_ + "_" + ColliderTypeName(type) + "Collider_";
    for (int index = 0;; ++index) {
        std::string candidate = base + std::to_string(index);
        bool used = false;
        for (const auto &collider : colliders_) {
            if (collider && collider->GetName() == candidate) {
                used = true;
                break;
            }
        }
        if (!used) {
            return candidate;
        }
    }
}

SphereCollider *BaseObject::AddSphereCollider(const std::string &name) {
    auto collider = std::make_unique<SphereCollider>();

    std::string colliderName = name.empty() ? MakeColliderName(ColliderType::Sphere) : name;
    collider->SetName(colliderName);
    collider->SetOwnerName(objectName_);

    collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
    collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });

    SphereCollider *raw = collider.get();
    colliders_.push_back(std::move(collider));
    LoadColliderIfSaved(raw);
    CollisionManager::GetInstance()->Register(raw);

    if (resolveCollision_) {
        raw->SetOnCollision([this, raw](ColliderBase *other) { this->ResolveCollisionWith(raw, other); });
    }
    return raw;
}

AABBCollider *BaseObject::AddAABBCollider(const std::string &name) {
    auto collider = std::make_unique<AABBCollider>();

    std::string colliderName = name.empty() ? MakeColliderName(ColliderType::AABB) : name;
    collider->SetName(colliderName);
    collider->SetOwnerName(objectName_);

    collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
    collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });

    AABBCollider *raw = collider.get();
    colliders_.push_back(std::move(collider));
    LoadColliderIfSaved(raw);
    CollisionManager::GetInstance()->Register(raw);

    if (resolveCollision_) {
        raw->SetOnCollision([this, raw](ColliderBase *other) { this->ResolveCollisionWith(raw, other); });
    }
    return raw;
}

OBBCollider *BaseObject::AddOBBCollider(const std::string &name) {
    auto collider = std::make_unique<OBBCollider>();

    std::string colliderName = name.empty() ? MakeColliderName(ColliderType::OBB) : name;
    collider->SetName(colliderName);
    collider->SetOwnerName(objectName_);

    collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
    collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });

    OBBCollider *raw = collider.get();
    colliders_.push_back(std::move(collider));
    LoadColliderIfSaved(raw);
    CollisionManager::GetInstance()->Register(raw);

    if (resolveCollision_) {
        raw->SetOnCollision([this, raw](ColliderBase *other) { this->ResolveCollisionWith(raw, other); });
    }
    return raw;
}

CylinderCollider *BaseObject::AddCylinderCollider(const std::string &name) {
    auto collider = std::make_unique<CylinderCollider>();

    std::string colliderName = name.empty() ? MakeColliderName(ColliderType::Cylinder) : name;
    collider->SetName(colliderName);
    collider->SetOwnerName(objectName_);

    collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
    collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });

    CylinderCollider *raw = collider.get();
    colliders_.push_back(std::move(collider));
    LoadColliderIfSaved(raw);
    CollisionManager::GetInstance()->Register(raw);

    if (resolveCollision_) {
        raw->SetOnCollision([this, raw](ColliderBase *other) { this->ResolveCollisionWith(raw, other); });
    }
    return raw;
}

MeshCollider *BaseObject::AddMeshCollider(const std::string &name) {
    auto collider = std::make_unique<MeshCollider>();

    std::string colliderName = name.empty() ? MakeColliderName(ColliderType::Mesh) : name;
    collider->SetName(colliderName);
    collider->SetOwnerName(objectName_);

    collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
    collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });
    collider->SetMatrixGetter([this]() { return this->GetWorldMatrix(); });

    // モデル形状から三角形群とBVHを構築
    if (obj3d_) {
        collider->SetSourceModelPath(modelPath_);
        collider->BuildFromModel(obj3d_->GetModel());
    }

    MeshCollider *raw = collider.get();
    colliders_.push_back(std::move(collider));
    LoadColliderIfSaved(raw);
    CollisionManager::GetInstance()->Register(raw);

    if (resolveCollision_) {
        raw->SetOnCollision([this, raw](ColliderBase *other) {
            this->ResolveCollisionWith(raw, other);
        });
    }
    return raw;
}

nlohmann::json BaseObject::CaptureColliderState() const {
    nlohmann::json list = nlohmann::json::array();
    for (const auto &collider : colliders_) {
        if (!collider) {
            continue;
        }
        nlohmann::json entry = collider->CaptureState();
        // 名前で突き合わせ
        entry["name"] = collider->GetName();
        list.push_back(std::move(entry));
    }
    return list;
}

void BaseObject::RestoreColliderState(const nlohmann::json &state) {
    if (!state.is_array()) {
        return;
    }

    // スナップショットに無いコライダーを削除（デストラクタで CollisionManager から登録解除）
    for (size_t i = colliders_.size(); i-- > 0;) {
        const ColliderBase *collider = colliders_[i].get();
        bool keep = false;
        if (collider) {
            for (const nlohmann::json &entry : state) {
                if (entry.value("name", std::string()) == collider->GetName()) {
                    keep = true;
                    break;
                }
            }
        }
        if (!keep) {
            colliders_.erase(colliders_.begin() + i);
        }
    }

    for (const nlohmann::json &entry : state) {
        const std::string colliderName = entry.value("name", std::string());
        if (colliderName.empty()) {
            continue;
        }
        const ColliderType type = static_cast<ColliderType>(entry.value("type", 0));

        ColliderBase *collider = nullptr;
        for (size_t i = 0; i < colliders_.size(); ++i) {
            if (!colliders_[i] || colliders_[i]->GetName() != colliderName) {
                continue;
            }
            if (colliders_[i]->GetType() != type) {
                // 形状種別の変更は作り直し
                colliders_.erase(colliders_.begin() + i);
                break;
            }
            collider = colliders_[i].get();
            break;
        }

        if (!collider) {
            switch (type) {
            case ColliderType::Sphere:
                collider = AddSphereCollider(colliderName);
                break;
            case ColliderType::AABB:
                collider = AddAABBCollider(colliderName);
                break;
            case ColliderType::OBB:
                collider = AddOBBCollider(colliderName);
                break;
            case ColliderType::Cylinder:
                collider = AddCylinderCollider(colliderName);
                break;
            case ColliderType::Mesh:
                collider = AddMeshCollider(colliderName);
                break;
            }
        }

        if (collider) {
            collider->RestoreState(entry);
        }
    }
}

void BaseObject::UpdatePhysics(float deltaTime) {
    // 剛体シミュレーション対象は PhysicsWorld が積分
    if (rigidBody_.enabled && rigidBody_.enableRotation) {
        return;
    }
    if (!rigidBody_.enabled || deltaTime <= 0.0f || !transform_) {
        return;
    }

    // 1ステップの上限（30fps 相当）
    constexpr float kMaxPhysicsDeltaTime = 1.0f / 30.0f;
    if (deltaTime > kMaxPhysicsDeltaTime) {
        deltaTime = kMaxPhysicsDeltaTime;
    }

    // 重力
    if (rigidBody_.useGravity) {
        rigidBody_.velocity += rigidBody_.gravity * deltaTime;
    }

    // 外力（a = F / m）と撃力
    if (rigidBody_.mass > 1e-4f) {
        rigidBody_.velocity += (accumulatedForce_ / rigidBody_.mass) * deltaTime;
        rigidBody_.velocity += accumulatedImpulse_ / rigidBody_.mass;
    }
    accumulatedForce_ = {0.0f, 0.0f, 0.0f};
    accumulatedTorque_ = {0.0f, 0.0f, 0.0f};
    accumulatedImpulse_ = {0.0f, 0.0f, 0.0f};
    accumulatedAngularImpulse_ = {0.0f, 0.0f, 0.0f};
    rigidBody_.angularVelocity = {0.0f, 0.0f, 0.0f};

    // 速度の減衰
    const float damp = (std::max)(1.0f - rigidBody_.linearDamping * deltaTime, 0.0f);
    rigidBody_.velocity *= damp;

    // 位置の積分（親なしのルートオブジェクト前提）
    transform_->translation_ += rigidBody_.velocity * deltaTime;
}

void BaseObject::ResolveCollisionWith(ColliderBase *self, ColliderBase *other) {
    // 剛体シミュレーション対象は PhysicsWorld が接触を解決
    if (!resolveCollision_ || !transform_ || (rigidBody_.enabled && rigidBody_.enableRotation)) {
        return;
    }

    Vector3 mtv;
    if (!CollisionManager::GetInstance()->ComputeDepenetration(self, other, mtv)) {
        return;
    }
    if (mtv.LengthSq() < 1e-10f) {
        return;
    }

    // 押し出し
    transform_->translation_ += mtv;

    // 同フレーム内の後続判定用に行列とコライダーを即時更新
    transform_->UpdateMatrix();
    if (self) {
        self->UpdateWorldTransform();
    }

    // 接触面に沿う速度へ補正
    if (rigidBody_.enabled) {
        Vector3 n = mtv.Normalize();
        float vn = rigidBody_.velocity.Dot(n);
        if (vn < 0.0f) {
            // 低速の接触は反発なし
            constexpr float kRestingSpeed = 0.5f;
            const float restitution = (-vn < kRestingSpeed) ? 0.0f : rigidBody_.restitution;
            rigidBody_.velocity -= n * (vn * (1.0f + restitution));
        }
        // 接線方向の摩擦
        Vector3 vTangent = rigidBody_.velocity - n * rigidBody_.velocity.Dot(n);
        rigidBody_.velocity -= vTangent * rigidBody_.friction;
    }
}

void BaseObject::InstallResolveCallbacks() {
    for (auto &c : colliders_) {
        if (!c) {
            continue;
        }
        ColliderBase *self = c.get();
        self->SetOnCollision([this, self](ColliderBase *other) {
            this->ResolveCollisionWith(self, other);
        });
    }
}

void BaseObject::ClearResolveCallbacks() {
    for (auto &c : colliders_) {
        if (c) {
            c->SetOnCollision(nullptr);
        }
    }
}

void BaseObject::SetResolveCollision(bool enable) {
    resolveCollision_ = enable;
    if (enable) {
        InstallResolveCallbacks();
    } else {
        ClearResolveCallbacks();
    }
}

void BaseObject::SavePhysics() {
    if (!objectData_) {
        return;
    }
    objectData_->Save<bool>("rb_enabled", rigidBody_.enabled);
    objectData_->Save<bool>("rb_useGravity", rigidBody_.useGravity);
    objectData_->Save<bool>("rb_enableRotation", rigidBody_.enableRotation);
    objectData_->Save<float>("rb_mass", rigidBody_.mass);
    objectData_->Save<Vector3>("rb_gravity", rigidBody_.gravity);
    objectData_->Save<float>("rb_linearDamping", rigidBody_.linearDamping);
    objectData_->Save<float>("rb_angularDamping", rigidBody_.angularDamping);
    objectData_->Save<float>("rb_restitution", rigidBody_.restitution);
    objectData_->Save<float>("rb_friction", rigidBody_.friction);
    objectData_->Save<bool>("resolveCollision", resolveCollision_);
}

void BaseObject::LoadPhysics() {
    if (!objectData_) {
        return;
    }
    rigidBody_.enabled = objectData_->Load<bool>("rb_enabled", false);
    rigidBody_.useGravity = objectData_->Load<bool>("rb_useGravity", true);
    rigidBody_.enableRotation = objectData_->Load<bool>("rb_enableRotation", false);
    rigidBody_.mass = objectData_->Load<float>("rb_mass", 1.0f);
    rigidBody_.gravity = objectData_->Load<Vector3>("rb_gravity", {0.0f, -9.8f, 0.0f});
    rigidBody_.linearDamping = objectData_->Load<float>("rb_linearDamping", 0.05f);
    rigidBody_.angularDamping = objectData_->Load<float>("rb_angularDamping", 0.05f);
    rigidBody_.restitution = objectData_->Load<float>("rb_restitution", 0.0f);
    rigidBody_.friction = objectData_->Load<float>("rb_friction", 0.3f);
    resolveCollision_ = objectData_->Load<bool>("resolveCollision", false);
    rigidBody_.velocity = {0.0f, 0.0f, 0.0f};
    rigidBody_.angularVelocity = {0.0f, 0.0f, 0.0f};
}

// ===================================================
// 足IK（接地）
// ===================================================

FootIkSolver *BaseObject::AcquireFootIk() {
    // スキンモデルのみ
    if (!obj3d_ || !obj3d_->GetHaveAnimation()) {
        return nullptr;
    }
    if (!footIk_) {
        footIk_ = std::make_unique<FootIkSolver>();
    }
    return footIk_.get();
}

void BaseObject::SolveFootIk() {
    if (!footIk_ || !footIk_->GetSettings().enabled || !obj3d_) {
        return;
    }

    ModelAnimation *pAnimation = obj3d_->GetCurrentModelAnimation();
    if (!pAnimation) {
        return;
    }
    Bone *pBone = pAnimation->GetBone();
    Skin *pSkin = pAnimation->GetSkin();
    if (!pBone || !pSkin || pBone->GetSkeletonRef().joints.empty()) {
        return;
    }

#ifdef USE_IMGUI
    // 一時停止中は更新しない
    if (!PlayModeManager::GetInstance()->ShouldUpdateGame()) {
        return;
    }
#endif // USE_IMGUI

    // 自身のコライダーを接地対象から除外
    footIk_->GetSettings().ignoreOwnerName = objectName_;

    // 描画と同じワールド行列で解く
    footIk_->Solve(pBone->GetSkeletonRef(), GetRenderWorldMatrix(), Frame::DeltaTime());

    // スキンパレットの再構築
    pSkin->Update(pBone->GetSkeletonRef());
}

// ===================================================
// 注視IK
// ===================================================

LookAtSolver *BaseObject::AcquireLookAt() {
    if (!obj3d_ || !obj3d_->GetHaveAnimation()) {
        return nullptr;
    }
    if (!lookAt_) {
        lookAt_ = std::make_unique<LookAtSolver>();
    }
    return lookAt_.get();
}

LookAtSolver *BaseObject::EnableLookAtByDefault() {
    const bool existed = (lookAt_ != nullptr);
    LookAtSolver *pSolver = AcquireLookAt();
    if (!pSolver || existed) {
        return pSolver;
    }
    ModelAnimation *pAnimation = obj3d_->GetCurrentModelAnimation();
    Bone *pBone = pAnimation ? pAnimation->GetBone() : nullptr;
    if (pBone && pSolver->AutoDetect(pBone->GetSkeletonRef())) {
        pSolver->GetSettings().enabled = true;
    }
    return pSolver;
}

void BaseObject::SetLookAtTarget(const Vector3 &targetWorld) {
    if (lookAt_) {
        lookAt_->SetTarget(targetWorld);
    }
}

void BaseObject::ClearLookAtTarget() {
    if (lookAt_) {
        lookAt_->ClearTarget();
    }
}

void BaseObject::SolveLookAt() {
    if (!lookAt_ || !obj3d_) {
        return;
    }
    ModelAnimation *pAnimation = obj3d_->GetCurrentModelAnimation();
    if (!pAnimation) {
        return;
    }
    Bone *pBone = pAnimation->GetBone();
    Skin *pSkin = pAnimation->GetSkin();
    if (!pBone || !pSkin || pBone->GetSkeletonRef().joints.empty()) {
        return;
    }

    float deltaTime = Frame::DeltaTime();
#ifdef USE_IMGUI
    // 一時停止中は時間を止める
    if (!PlayModeManager::GetInstance()->ShouldUpdateGame()) {
        deltaTime = 0.0f;
    }
#endif // USE_IMGUI

    // 体の正面（回転 × +Z）
    const Vector3 face = TransformNormal(kWorldForward, QuaternionToMatrix4x4(transform_->quaternionRotation_));
    if (lookAt_->Solve(pBone->GetSkeletonRef(), GetRenderWorldMatrix(), face, deltaTime)) {
        pSkin->Update(pBone->GetSkeletonRef());
    }
}

void BaseObject::SaveLookAt() {
    if (!objectData_ || !lookAt_) {
        return;
    }
    objectData_->Save<nlohmann::json>("lookAt", lookAt_->ToJson());
}

void BaseObject::LoadLookAt() {
    if (!objectData_ || !objectData_->Contains("lookAt")) {
        return;
    }
    if (LookAtSolver *pSolver = AcquireLookAt()) {
        pSolver->FromJson(objectData_->Load<nlohmann::json>("lookAt", nlohmann::json::object()));
    }
}

// ===================================================
// エディタからのコライダーの追加・削除
// ===================================================

ColliderBase *BaseObject::AddColliderForEditor(ColliderType type) {
    // 既定の衝突マスクは ColliderTagManager の設定を使用
    auto makeDefault = [](ColliderBase *c) {
        c->SetTag("Environment");
        for (const std::string &mask : ColliderTagManager::GetInstance()->GetDefaultCollisionMasks()) {
            c->AddCollisionMask(mask);
        }
    };
    // 保存済み設定が無いときのみ既定値を適用
    auto isFresh = [](const ColliderBase *c) { return !DataHandler("Collider", c->GetName()).Exists(); };

    switch (type) {
    case ColliderType::Sphere: {
        auto *c = AddSphereCollider();
        if (isFresh(c)) {
            makeDefault(c);
            c->SetRadius(1.0f);
        }
        return c;
    }
    case ColliderType::AABB: {
        auto *c = AddAABBCollider();
        if (isFresh(c)) {
            makeDefault(c);
            c->SetSize({2.0f, 2.0f, 2.0f});
        }
        return c;
    }
    case ColliderType::OBB: {
        auto *c = AddOBBCollider();
        if (isFresh(c)) {
            makeDefault(c);
            c->SetSize({2.0f, 2.0f, 2.0f});
        }
        return c;
    }
    case ColliderType::Cylinder: {
        auto *c = AddCylinderCollider();
        if (isFresh(c)) {
            makeDefault(c);
            c->SetRadius(2.0f);
            c->SetHeight(4.0f);
            c->SetInward(false); // 外側へ押し出す障害物
        }
        return c;
    }
    case ColliderType::Mesh: {
        auto *c = AddMeshCollider();
        makeDefault(c);
        return c;
    }
    }
    return nullptr;
}

bool BaseObject::RemoveCollider(const ColliderBase *pCollider) {
    // erase でデストラクタが走り CollisionManager から登録解除
    auto it = std::find_if(colliders_.begin(), colliders_.end(),
                           [pCollider](const std::unique_ptr<ColliderBase> &c) { return c.get() == pCollider; });
    if (it == colliders_.end()) {
        return false;
    }
    colliders_.erase(it);
    return true;
}

// ===================================================
// 手のIK
// ===================================================

HandIkSolver *BaseObject::AcquireHandIk() {
    if (!obj3d_ || !obj3d_->GetHaveAnimation()) {
        return nullptr;
    }
    if (!handIk_) {
        handIk_ = std::make_unique<HandIkSolver>();
    }
    return handIk_.get();
}

void BaseObject::SetHandIkTarget(const std::string &limbLabel, const Vector3 &targetWorld) {
    if (handIk_) {
        handIk_->SetTarget(limbLabel, targetWorld);
    }
}

void BaseObject::ClearHandIkTargets() {
    if (handIk_) {
        handIk_->ClearAllTargets();
    }
}

void BaseObject::SolveHandIk() {
    if (!handIk_ || !obj3d_) {
        return;
    }
    ModelAnimation *pAnimation = obj3d_->GetCurrentModelAnimation();
    if (!pAnimation) {
        return;
    }
    Bone *pBone = pAnimation->GetBone();
    Skin *pSkin = pAnimation->GetSkin();
    if (!pBone || !pSkin || pBone->GetSkeletonRef().joints.empty()) {
        return;
    }

    float deltaTime = Frame::DeltaTime();
#ifdef USE_IMGUI
    // 一時停止中は時間を止める
    if (!PlayModeManager::GetInstance()->ShouldUpdateGame()) {
        deltaTime = 0.0f;
    }
#endif // USE_IMGUI

    if (handIk_->Solve(pBone->GetSkeletonRef(), GetRenderWorldMatrix(), deltaTime)) {
        pSkin->Update(pBone->GetSkeletonRef());
    }
}

void BaseObject::SaveHandIk() {
    if (!objectData_ || !handIk_) {
        return;
    }
    objectData_->Save<nlohmann::json>("handIk", handIk_->ToJson());
}

void BaseObject::LoadHandIk() {
    if (!objectData_ || !objectData_->Contains("handIk")) {
        return;
    }
    if (HandIkSolver *pSolver = AcquireHandIk()) {
        pSolver->FromJson(objectData_->Load<nlohmann::json>("handIk", nlohmann::json::object()));
    }
}

// ===================================================
// 揺れ物（髪・布・しっぽ）
// ===================================================

SpringBoneSolver *BaseObject::AcquireSpringBone() {
    if (!obj3d_ || !obj3d_->GetHaveAnimation()) {
        return nullptr;
    }
    if (!springBone_) {
        springBone_ = std::make_unique<SpringBoneSolver>();
    }
    return springBone_.get();
}

void BaseObject::ResetSpringBone() {
    if (springBone_) {
        springBone_->Reset();
    }
}

void BaseObject::SolveSpringBone() {
    if (!springBone_ || !springBone_->GetSettings().enabled || !obj3d_) {
        return;
    }
    ModelAnimation *pAnimation = obj3d_->GetCurrentModelAnimation();
    if (!pAnimation) {
        return;
    }
    Bone *pBone = pAnimation->GetBone();
    Skin *pSkin = pAnimation->GetSkin();
    if (!pBone || !pSkin || pBone->GetSkeletonRef().joints.empty()) {
        return;
    }

    float deltaTime = Frame::DeltaTime();
#ifdef USE_IMGUI
    // 一時停止中は時間を止める
    if (!PlayModeManager::GetInstance()->ShouldUpdateGame()) {
        deltaTime = 0.0f;
    }
#endif // USE_IMGUI

    if (springBone_->Solve(pBone->GetSkeletonRef(), GetRenderWorldMatrix(), deltaTime)) {
        pSkin->Update(pBone->GetSkeletonRef());
    }
}

void BaseObject::SaveSpringBone() {
    if (!objectData_ || !springBone_) {
        return;
    }
    objectData_->Save<nlohmann::json>("springBone", springBone_->ToJson());
}

void BaseObject::LoadSpringBone() {
    if (!objectData_ || !objectData_->Contains("springBone")) {
        return;
    }
    if (SpringBoneSolver *pSolver = AcquireSpringBone()) {
        pSolver->FromJson(objectData_->Load<nlohmann::json>("springBone", nlohmann::json::object()));
    }
}

// ===================================================
// アニメーションのステートマシン
// ===================================================

bool BaseObject::SetAnimationStateMachine(const std::string &assetName) {
    if (assetName.empty()) {
        animStateMachine_.reset();
        return true;
    }
    if (!obj3d_ || !obj3d_->GetHaveAnimation()) {
        return false;
    }
    auto runner = std::make_unique<AnimationStateMachineRunner>();
    if (!runner->Initialize(obj3d_.get(), assetName, objectName_)) {
        return false;
    }
    animStateMachine_ = std::move(runner);
    return true;
}

void BaseObject::SaveAnimStateMachine() {
    if (!objectData_) {
        return;
    }
    // 解除時も空文字で上書き
    if (animStateMachine_ || objectData_->Contains("animStateMachine")) {
        objectData_->Save<std::string>("animStateMachine", animStateMachine_ ? animStateMachine_->GetAssetName() : std::string());
    }
}

void BaseObject::LoadAnimStateMachine() {
    if (!objectData_ || !objectData_->Contains("animStateMachine")) {
        return;
    }
    SetAnimationStateMachine(objectData_->Load<std::string>("animStateMachine", std::string()));
}

void BaseObject::SaveFootIk() {
    if (!objectData_) {
        return;
    }
    if (!footIk_) {
        return;
    }
    objectData_->Save<nlohmann::json>("footIk", footIk_->ToJson());
}

void BaseObject::LoadFootIk() {
    if (!objectData_ || !objectData_->Contains("footIk")) {
        return;
    }
    if (FootIkSolver *pSolver = AcquireFootIk()) {
        pSolver->FromJson(objectData_->Load<nlohmann::json>("footIk", nlohmann::json::object()));
    }
}

// ---- モデルのホットリロード ----------------------------------------------------

bool BaseObject::ReloadModel() {
    if (isPrimitive_ || modelPath_.empty() || !obj3d_) {
        return false;
    }

    auto jointCount = [this]() -> size_t {
        ModelAnimation *pAnimation = obj3d_->GetCurrentModelAnimation();
        Bone *pBone = pAnimation ? pAnimation->GetBone() : nullptr;
        return pBone ? pBone->GetSkeletonRef().joints.size() : 0;
    };
    const size_t jointsBefore = jointCount();

    if (!obj3d_->ReloadModel(modelPath_)) {
        return false;
    }

    // 増えたマテリアル分のテクスチャパスをモデルから補完
    const size_t materialCount = obj3d_->GetMaterialCount();
    if (texturePaths_.size() < materialCount) {
        const std::vector<std::string> modelTextures = obj3d_->GetAllTexturePath();
        for (size_t i = texturePaths_.size(); i < materialCount && i < modelTextures.size(); ++i) {
            texturePaths_.push_back(modelTextures[i]);
        }
    }

    // メッシュコライダーの再構築
    for (const std::unique_ptr<ColliderBase> &collider : colliders_) {
        if (collider && collider->GetType() == ColliderType::Mesh) {
            auto *pMesh = static_cast<MeshCollider *>(collider.get());
            if (pMesh->GetSourceModelPath() == modelPath_ && obj3d_->GetModel()) {
                pMesh->BuildFromModel(obj3d_->GetModel());
            }
        }
    }

    // 骨数が変わったら各ソルバの骨を名前から再検出
    const size_t jointsAfter = jointCount();
    if (jointsAfter != jointsBefore && jointsAfter > 0) {
        const Skeleton &skeleton = obj3d_->GetCurrentModelAnimation()->GetBone()->GetSkeletonRef();
        if (footIk_) {
            footIk_->AutoDetect(skeleton);
        }
        if (lookAt_) {
            lookAt_->AutoDetect(skeleton);
        }
        if (handIk_) {
            handIk_->AutoDetect(skeleton);
        }
        if (springBone_) {
            springBone_->AutoDetect(skeleton);
        }
    }
    return true;
}

} // namespace Hagine
