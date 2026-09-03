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
#include <graphics/texture/TextureManager.h>
#include <imgui_internal.h>
#include <implot.h>
#endif // DEBUG

// コライダーの追加と簡易物理（押し戻し・重力）。
namespace Hagine {
namespace {
/// <summary>
/// コライダー生成時、保存フォルダ(jsons/Collider)に同名のJSONがあれば
/// その設定（サイズ・オフセット・色・表示/判定・タグ・マスク）を読み込む。
/// 無ければ何もしない（コードの既定値のまま）。
/// CollisionManager への登録より前に呼ぶことで、保存されたタグで正しく登録される。
/// </summary>
void LoadColliderIfSaved(ColliderBase *collider) {
    if (!collider)
        return;
    DataHandler probe("Collider", collider->GetName());
    if (probe.Exists()) {
        collider->LoadFromJson();
    }
}
} // namespace

SphereCollider *BaseObject::AddSphereCollider(const std::string &name) {
    auto collider = std::make_unique<SphereCollider>();

    std::string colliderName = name.empty() ? objectName_ + "_SphereCollider" : name;
    collider->SetName(colliderName);

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

    std::string colliderName = name.empty() ? objectName_ + "_AABBCollider" : name;
    collider->SetName(colliderName);

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

    std::string colliderName = name.empty() ? objectName_ + "_OBBCollider" : name;
    collider->SetName(colliderName);

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

    std::string colliderName = name.empty() ? objectName_ + "_CylinderCollider" : name;
    collider->SetName(colliderName);

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

    std::string colliderName = name.empty() ? objectName_ + "_MeshCollider" : name;
    collider->SetName(colliderName);

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

} // namespace Hagine
