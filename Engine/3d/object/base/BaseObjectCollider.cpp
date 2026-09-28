#define NOMINMAX
#include "BaseObject.h"
#include "BaseObjectManager.h"
#include "browser/ShowFolder.h"
#include "collider/CollisionManager.h"
#include "debug/profiler/CpuProfiler.h"
#include "frame/Frame.h"
#include "model/material/Material.h"
#include "object/Object3dInstancing.h"
#include "scene/SceneManager.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#ifdef USE_IMGUI
#include "utility/debug/imgui/AssetDragDrop.h"
#include <asset/AssetPath.h>
#include <edit/play/PlayModeManager.h>
#include <graphics/texture/TextureManager.h>
#include <imgui_internal.h>
#include <implot.h>
#endif // DEBUG

// コライダーの追加と簡易物理（押し戻し・重力）。
namespace Hagine {
// コライダー生成時、保存フォルダ(jsons/Collider)に同名のJSONがあれば
// その設定（サイズ・オフセット・表示/判定・タグ・マスク）を読み込む。
// 無ければ何もしない（コードの既定値のまま）。
// CollisionManager への登録より前に呼ぶことで、保存されたタグで正しく登録される
void BaseObject::LoadColliderIfSaved(ColliderBase *collider) {
    if (!collider)
        return;
    DataHandler probe("Collider", collider->GetName());
    if (probe.Exists()) {
        collider->LoadFromJson();
    }
}

std::string BaseObject::MakeColliderName(ColliderType type) const {
    const std::string base = objectName_ + "_" + ColliderTypeName(type) + "Collider_";
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
    LoadColliderIfSaved(raw); // 保存済み設定があれば反映（登録より前）
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
    LoadColliderIfSaved(raw); // 保存済み設定があれば反映（登録より前）
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
    LoadColliderIfSaved(raw); // 保存済み設定があれば反映（登録より前）
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
    LoadColliderIfSaved(raw); // 保存済み設定があれば反映（登録より前）
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

    // 位置・回転に加え、スケールを含むワールド行列も取得できるよう配線する
    collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
    collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });
    collider->SetMatrixGetter([this]() { return this->GetWorldMatrix(); });

    // 自身のモデル形状（ローカル空間の頂点）から三角形群とBVHを構築する
    if (obj3d_) {
        collider->SetSourceModelPath(modelPath_);
        collider->BuildFromModel(obj3d_->GetModel());
    }

    MeshCollider *raw = collider.get();
    colliders_.push_back(std::move(collider));
    LoadColliderIfSaved(raw); // 保存済み設定があれば反映（登録より前）
    CollisionManager::GetInstance()->Register(raw);

    // 押し出しが有効なら、追加したコライダーにも押し出しコールバックを仕込む
    if (resolveCollision_) {
        raw->SetOnCollision([this, raw](ColliderBase *other) {
            this->ResolveCollisionWith(raw, other);
        });
    }
    return raw;
}

#ifdef USE_IMGUI
nlohmann::json BaseObject::CaptureColliderState() const {
    nlohmann::json list = nlohmann::json::array();
    for (const auto &collider : colliders_) {
        if (!collider) {
            continue;
        }
        nlohmann::json entry = collider->CaptureState();
        // 誰なのかは名前で突き合わせる（添字だと途中で消した瞬間に総入れ替えになる）
        entry["name"] = collider->GetName();
        list.push_back(std::move(entry));
    }
    return list;
}

void BaseObject::RestoreColliderState(const nlohmann::json &state) {
    if (!state.is_array()) {
        return;
    }

    // スナップショットに載っていないコライダーを消す。
    // colliders_ は unique_ptr 所有なので、erase で ~ColliderBase が走り
    // CollisionManager からも自動で外れる（delete は呼ばない）
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
                // 同じ名前で形状だけ違う（種別を変えた後の Undo）。作り直すしかない
                colliders_.erase(colliders_.begin() + i);
                break;
            }
            collider = colliders_[i].get();
            break;
        }

        if (!collider) {
            // Add 系は名前・所有者・座標取得関数・CollisionManager への登録まで面倒を見てくれる
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
#endif // USE_IMGUI

void BaseObject::UpdatePhysics(float deltaTime) {
    if (!rigidBody_.enabled || deltaTime <= 0.0f || !transform_) {
        return;
    }

    // 重力（加速度）を速度へ積分
    if (rigidBody_.useGravity) {
        rigidBody_.velocity += rigidBody_.gravity * deltaTime;
    }

    // 外力を加速度（a = F / m）として速度へ積分
    if (rigidBody_.mass > 1e-4f) {
        rigidBody_.velocity += (accumulatedForce_ / rigidBody_.mass) * deltaTime;
    }
    accumulatedForce_ = {0.0f, 0.0f, 0.0f};

    // 速度の減衰（空気抵抗）
    float damp = 1.0f - rigidBody_.linearDamping * deltaTime;
    if (damp < 0.0f) {
        damp = 0.0f;
    }
    rigidBody_.velocity *= damp;

    // 速度を位置へ積分（ローカル座標。物理は親なしルートオブジェクト向け）
    transform_->translation_ += rigidBody_.velocity * deltaTime;
}

void BaseObject::ResolveCollisionWith(ColliderBase *self, ColliderBase *other) {
    if (!resolveCollision_ || !transform_) {
        return;
    }

    // self を other から押し出す MTV を統一APIで取得する
    Vector3 mtv;
    if (!CollisionManager::GetInstance()->ComputeDepenetration(self, other, mtv)) {
        return;
    }
    if (mtv.LengthSq() < 1e-10f) {
        return;
    }

    // めり込み解消（押し出し）
    transform_->translation_ += mtv;

    // 押し出した結果を即座にワールド行列とコライダーへ反映する。
    // CollisionManager は1フレーム中に同じ組み合わせを複数回判定する（A対B と B対A）ので、
    // ここで位置を更新しておかないと、2回目も同じめり込み量を見て二重に押してしまう。
    // 描画も押し出し後の位置になるので、着地フレームの見た目のズレも消える。
    transform_->UpdateMatrix();
    if (self) {
        self->UpdateWorldTransform();
    }

    // リジッドボディなら、接触面に沿うよう速度を補正する
    if (rigidBody_.enabled) {
        Vector3 n = mtv.Normalize();
        float vn = rigidBody_.velocity.Dot(n);
        if (vn < 0.0f) {
            // 接地して静止したいだけの接触で跳ね返さないよう、
            // 衝突速度が十分小さいときは反発を切る（restingContact）。
            // これが無いと反発係数 > 0 のオブジェクトが床の上で永久に細かく跳ね続ける。
            // しきい値は重力が数フレームで得る速度（9.8 * 1/60 ≒ 0.16）より少し上に取る
            constexpr float kRestingSpeed = 0.5f;
            const float restitution = (-vn < kRestingSpeed) ? 0.0f : rigidBody_.restitution;
            // 法線方向の侵入成分を除去（反発係数で跳ね返り）
            rigidBody_.velocity -= n * (vn * (1.0f + restitution));
        }
        // 接線方向に摩擦をかける（坂を滑り落ちる挙動になる）
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
    objectData_->Save<float>("rb_mass", rigidBody_.mass);
    objectData_->Save<Vector3>("rb_gravity", rigidBody_.gravity);
    objectData_->Save<float>("rb_linearDamping", rigidBody_.linearDamping);
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
    rigidBody_.mass = objectData_->Load<float>("rb_mass", 1.0f);
    rigidBody_.gravity = objectData_->Load<Vector3>("rb_gravity", {0.0f, -9.8f, 0.0f});
    rigidBody_.linearDamping = objectData_->Load<float>("rb_linearDamping", 0.05f);
    rigidBody_.restitution = objectData_->Load<float>("rb_restitution", 0.0f);
    rigidBody_.friction = objectData_->Load<float>("rb_friction", 0.3f);
    resolveCollision_ = objectData_->Load<bool>("resolveCollision", false);
    rigidBody_.velocity = {0.0f, 0.0f, 0.0f}; // 速度はランタイム状態なのでリセット
}

// ===================================================
// 足IK（接地）
// ===================================================

FootIkSolver *BaseObject::AcquireFootIk() {
    // スキンの入っていないモデルには脚が無いので持たせない
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
    // 一時停止中は足を動かさない。ゲーム世界が止まっているのに接地だけ追従すると、
    // コマ送りでポーズを見たいときに絵が変わってしまう
    if (!PlayModeManager::GetInstance()->ShouldUpdateGame()) {
        return;
    }
#endif // USE_IMGUI

    // 自分の体のコライダーは接地の相手ではない。名前は改名で変わりうるので毎回入れ直す
    footIk_->GetSettings().ignoreOwnerName = objectName_;

    // 地面を探すレイはワールド空間なので、描画に使うのと同じ行列で解く。
    // transform_ の行列をそのまま使うと、描画オフセットを掛けているキャラだけ
    // 足が地面から浮く（レイトレの加速構造で踏んだのと同じ話）
    footIk_->Solve(pBone->GetSkeletonRef(), GetRenderWorldMatrix(), Frame::DeltaTime());

    // ポーズを書き換えたので、シェーダーへ渡すパレットを作り直す。
    // GPUスキニングの実行は描画フェーズなので、ここで間に合う
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
        return pSolver; // 保存済み（または既に設定済み）の設定をそのまま使う
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
    // 一時停止中は首の向きを進めない（止めた絵のまま見られるように。ポーズ自体は毎フレーム掛ける）
    if (!PlayModeManager::GetInstance()->ShouldUpdateGame()) {
        deltaTime = 0.0f;
    }
#endif // USE_IMGUI

    // 体の正面（ワールド）。キャラは 回転 × (+Z) を正面として作ってある
    const Vector3 face = TransformNormal(kWorldForward, QuaternionToMatrix4x4(transform_->quaternionRotation_));
    if (lookAt_->Solve(pBone->GetSkeletonRef(), GetRenderWorldMatrix(), face, deltaTime)) {
        // ポーズを書き換えたので、シェーダーへ渡すパレットを作り直す
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
    // 外したときも空文字で上書きしておく（読み込み時に前の名前が復活しないように）
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
        // 一度も使っていないなら何も書かない（既存のJSONを無駄に太らせない）
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

} // namespace Hagine
