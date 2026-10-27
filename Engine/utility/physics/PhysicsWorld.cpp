#define NOMINMAX
#include "PhysicsWorld.h"
#include "collider/CollisionManager.h"
#include "object/base/BaseObject.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#ifdef USE_IMGUI
#include <imgui.h>
#endif // USE_IMGUI

namespace Hagine {
namespace {

constexpr float kMaxAngularSpeed = 100.0f;

float SafeInverse(float value) {
    return (std::abs(value) > 1e-8f) ? 1.0f / value : 0.0f;
}

// 法線に直交する2軸
void MakeTangents(const Vector3 &normal, Vector3 &outTangent1, Vector3 &outTangent2) {
    const Vector3 seed = (std::abs(normal.x) > 0.57735f) ? Vector3{normal.y, -normal.x, 0.0f}
                                                          : Vector3{0.0f, normal.z, -normal.y};
    outTangent1 = seed.Normalize();
    outTangent2 = normal.Cross(outTangent1);
}

// ワールド行列はクォータニオンの共役回転として作用するため、ワールド角速度の回転は右から逆回転を合成
Quaternion RotateByAngularVelocity(const Quaternion &rotation, const Vector3 &angularVelocity, float deltaTime) {
    const float angularSpeed = angularVelocity.Length();
    if (angularSpeed * deltaTime <= 1e-7f) {
        return rotation;
    }
    const Quaternion delta = Quaternion::FromAxisAngle(angularVelocity / angularSpeed, angularSpeed * deltaTime);
    return (rotation * delta.Conjugate()).Normalize();
}

// 回転差分からワールド角速度を算出
Vector3 AngularVelocityFromRotations(const Quaternion &previous, const Quaternion &current, float deltaTime) {
    Quaternion delta = (current.Conjugate() * previous).Normalize();
    if (delta.w < 0.0f) {
        delta = {-delta.x, -delta.y, -delta.z, -delta.w};
    }
    const Vector3 axis = {delta.x, delta.y, delta.z};
    const float sinHalf = axis.Length();
    if (sinHalf < 1e-6f || deltaTime <= 0.0f) {
        return {0.0f, 0.0f, 0.0f};
    }
    const float angle = 2.0f * std::atan2(sinHalf, delta.w);
    return axis / sinHalf * (angle / deltaTime);
}

uint64_t MakeContactKey(const ColliderBase *a, const ColliderBase *b, uint32_t feature) {
    const uint64_t ha = static_cast<uint64_t>(std::hash<const void *>{}(a));
    const uint64_t hb = static_cast<uint64_t>(std::hash<const void *>{}(b));
    return ha ^ (hb * 0x9E3779B97F4A7C15ull) ^ (static_cast<uint64_t>(feature) * 0xC2B2AE3D27D4EB4Full);
}

bool ShouldCollide(const ColliderBase *a, const ColliderBase *b) {
    if (!a->IsEnabled() || !b->IsEnabled()) {
        return false;
    }
    return a->ShouldCollideWith(b) || b->ShouldCollideWith(a) || a->CollidesWithAll() || b->CollidesWithAll();
}

// コライダーを包む球
bool ColliderBounds(ColliderBase *collider, Vector3 &outCenter, float &outRadius) {
    switch (collider->GetType()) {
    case ColliderType::OBB: {
        const OBB obb = static_cast<OBBCollider *>(collider)->GetOBB();
        outCenter = obb.scaleCenterRotated;
        outRadius = obb.size.Length();
        return true;
    }
    case ColliderType::Sphere: {
        const Sphere sphere = static_cast<SphereCollider *>(collider)->GetSphere();
        outCenter = sphere.center;
        outRadius = sphere.radius;
        return true;
    }
    case ColliderType::AABB: {
        const AABB box = static_cast<AABBCollider *>(collider)->GetAABB();
        outCenter = (box.min + box.max) * 0.5f;
        outRadius = ((box.max - box.min) * 0.5f).Length();
        return true;
    }
    case ColliderType::Cylinder: {
        auto *pCylinder = static_cast<CylinderCollider *>(collider);
        outCenter = pCylinder->GetCenterPosition();
        outRadius = pCylinder->GetRadius() + pCylinder->GetHeight();
        return true;
    }
    default:
        return false;
    }
}

// ===================================================
// 箱同士の接触点（SAT + 面クリッピング）
// ===================================================

struct Box {
    Vector3 center;
    Vector3 axis[3];
    float extent[3];
};

Box ToBox(const OBB &obb) {
    Box box;
    box.center = obb.scaleCenterRotated;
    for (int i = 0; i < 3; ++i) {
        box.axis[i] = obb.orientations[i];
    }
    box.extent[0] = obb.size.x;
    box.extent[1] = obb.size.y;
    box.extent[2] = obb.size.z;
    return box;
}

float ProjectRadius(const Box &box, const Vector3 &axis) {
    return std::abs(box.axis[0].Dot(axis)) * box.extent[0] +
           std::abs(box.axis[1].Dot(axis)) * box.extent[1] +
           std::abs(box.axis[2].Dot(axis)) * box.extent[2];
}

struct ClipVertex {
    Vector3 position;
    uint32_t id;
};

// 平面 normal·p <= offset の内側へ多角形をクリップ
int ClipPolygon(const ClipVertex *input, int count, const Vector3 &normal, float offset, int planeIndex, ClipVertex *output) {
    int outCount = 0;
    for (int i = 0; i < count; ++i) {
        const ClipVertex &a = input[i];
        const ClipVertex &b = input[(i + 1) % count];
        const float da = a.position.Dot(normal) - offset;
        const float db = b.position.Dot(normal) - offset;
        if (da <= 0.0f) {
            output[outCount++] = a;
        }
        if ((da <= 0.0f) != (db <= 0.0f)) {
            const float t = da / (da - db);
            const uint32_t id = (a.id & 0xFu) | ((b.id & 0xFu) << 4) | (static_cast<uint32_t>(planeIndex + 1) << 8);
            output[outCount++] = {a.position + (b.position - a.position) * t, id};
        }
    }
    return outCount;
}

// 参照面へ相手の面をクリップして接触点を生成
void BuildFaceContact(const Box &ref, const Box &inc, int refAxis, bool refIsA, ContactManifold &out) {
    const float sign = ((inc.center - ref.center).Dot(ref.axis[refAxis]) >= 0.0f) ? 1.0f : -1.0f;
    const Vector3 normal = ref.axis[refAxis] * sign; // 参照 → 相手

    // 参照面に最も向かい合う相手の面
    int incAxis = 0;
    float maxDot = -1.0f;
    for (int k = 0; k < 3; ++k) {
        const float d = std::abs(inc.axis[k].Dot(normal));
        if (d > maxDot) {
            maxDot = d;
            incAxis = k;
        }
    }
    const float incSign = (inc.axis[incAxis].Dot(normal) > 0.0f) ? -1.0f : 1.0f;
    const Vector3 incCenter = inc.center + inc.axis[incAxis] * (incSign * inc.extent[incAxis]);
    const int i1 = (incAxis + 1) % 3;
    const int i2 = (incAxis + 2) % 3;
    const Vector3 e1 = inc.axis[i1] * inc.extent[i1];
    const Vector3 e2 = inc.axis[i2] * inc.extent[i2];

    ClipVertex bufferA[16];
    ClipVertex bufferB[16];
    bufferA[0] = {incCenter + e1 + e2, 0};
    bufferA[1] = {incCenter - e1 + e2, 1};
    bufferA[2] = {incCenter - e1 - e2, 2};
    bufferA[3] = {incCenter + e1 - e2, 3};
    int count = 4;

    // 参照面の側面4枚でクリップ
    const int r1 = (refAxis + 1) % 3;
    const int r2 = (refAxis + 2) % 3;
    const Vector3 sideNormals[4] = {ref.axis[r1], -ref.axis[r1], ref.axis[r2], -ref.axis[r2]};
    const float sideOffsets[4] = {
        ref.center.Dot(ref.axis[r1]) + ref.extent[r1],
        -ref.center.Dot(ref.axis[r1]) + ref.extent[r1],
        ref.center.Dot(ref.axis[r2]) + ref.extent[r2],
        -ref.center.Dot(ref.axis[r2]) + ref.extent[r2],
    };
    ClipVertex *pIn = bufferA;
    ClipVertex *pOut = bufferB;
    for (int plane = 0; plane < 4 && count > 0; ++plane) {
        count = ClipPolygon(pIn, count, sideNormals[plane], sideOffsets[plane], plane, pOut);
        std::swap(pIn, pOut);
    }

    const float refOffset = ref.center.Dot(normal) + ref.extent[refAxis];
    const uint32_t faceId = (refIsA ? 0x80000000u : 0u) | (static_cast<uint32_t>(refAxis) << 28) |
                            (static_cast<uint32_t>(incAxis) << 25) | (incSign > 0.0f ? 0x01000000u : 0u);
    constexpr float kSpeculative = 0.005f;
    out.normal = refIsA ? -normal : normal;
    out.depth = 0.0f;
    out.pointCount = 0;
    for (int i = 0; i < count && out.pointCount < ContactManifold::kMaxPoints; ++i) {
        const float depth = refOffset - pIn[i].position.Dot(normal);
        if (depth < -kSpeculative) {
            continue;
        }
        ContactPoint &point = out.points[out.pointCount++];
        point.position = pIn[i].position + normal * (depth * 0.5f);
        point.depth = (std::max)(depth, 0.0f);
        point.feature = faceId | (pIn[i].id & 0xFFFFu);
        out.depth = (std::max)(out.depth, point.depth);
    }
}

// エッジ同士の最近点で接触点を生成
void BuildEdgeContact(const Box &a, const Box &b, int edgeA, int edgeB, const Vector3 &axis, float depth, ContactManifold &out) {
    Vector3 normal = axis;
    if (normal.Dot(a.center - b.center) < 0.0f) {
        normal = -normal;
    }

    Vector3 pointA = a.center;
    Vector3 pointB = b.center;
    for (int k = 0; k < 3; ++k) {
        if (k != edgeA) {
            pointA += a.axis[k] * ((a.axis[k].Dot(normal) > 0.0f) ? -a.extent[k] : a.extent[k]);
        }
        if (k != edgeB) {
            pointB += b.axis[k] * ((b.axis[k].Dot(normal) > 0.0f) ? b.extent[k] : -b.extent[k]);
        }
    }

    const Vector3 &ua = a.axis[edgeA];
    const Vector3 &ub = b.axis[edgeB];
    const Vector3 r = pointA - pointB;
    const float cosine = ua.Dot(ub);
    const float denominator = 1.0f - cosine * cosine;
    const float c = ua.Dot(r);
    const float f = ub.Dot(r);
    float s = (denominator > 1e-6f) ? (cosine * f - c) / denominator : 0.0f;
    s = std::clamp(s, -a.extent[edgeA], a.extent[edgeA]);
    float t = std::clamp(f + cosine * s, -b.extent[edgeB], b.extent[edgeB]);
    s = std::clamp(cosine * t - c, -a.extent[edgeA], a.extent[edgeA]);

    out.normal = normal;
    out.depth = depth;
    out.pointCount = 1;
    out.points[0].position = ((pointA + ua * s) + (pointB + ub * t)) * 0.5f;
    out.points[0].depth = depth;
    out.points[0].feature = 0x40000000u | static_cast<uint32_t>(edgeA * 3 + edgeB);
}

bool CollideBoxes(const Box &a, const Box &b, ContactManifold &out) {
    const Vector3 d = a.center - b.center;

    float faceOverlapA = FLT_MAX;
    float faceOverlapB = FLT_MAX;
    int faceAxisA = 0;
    int faceAxisB = 0;
    for (int k = 0; k < 3; ++k) {
        const float overlapA = a.extent[k] + ProjectRadius(b, a.axis[k]) - std::abs(d.Dot(a.axis[k]));
        if (overlapA < 0.0f) {
            return false;
        }
        if (overlapA < faceOverlapA) {
            faceOverlapA = overlapA;
            faceAxisA = k;
        }
        const float overlapB = b.extent[k] + ProjectRadius(a, b.axis[k]) - std::abs(d.Dot(b.axis[k]));
        if (overlapB < 0.0f) {
            return false;
        }
        if (overlapB < faceOverlapB) {
            faceOverlapB = overlapB;
            faceAxisB = k;
        }
    }

    float edgeOverlap = FLT_MAX;
    int edgeA = 0;
    int edgeB = 0;
    Vector3 edgeAxis{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            Vector3 axis = a.axis[i].Cross(b.axis[j]);
            const float length = axis.Length();
            if (length < 1e-4f) {
                continue;
            }
            axis = axis / length;
            const float overlap = ProjectRadius(a, axis) + ProjectRadius(b, axis) - std::abs(d.Dot(axis));
            if (overlap < 0.0f) {
                return false;
            }
            if (overlap < edgeOverlap) {
                edgeOverlap = overlap;
                edgeA = i;
                edgeB = j;
                edgeAxis = axis;
            }
        }
    }

    // 面軸を優先（ばたつき防止）
    constexpr float kRelativeTolerance = 0.95f;
    constexpr float kAbsoluteTolerance = 0.005f;
    bool refIsA = true;
    float faceOverlap = faceOverlapA;
    if (faceOverlapB < faceOverlapA * kRelativeTolerance - kAbsoluteTolerance) {
        refIsA = false;
        faceOverlap = faceOverlapB;
    }
    if (edgeOverlap < faceOverlap * kRelativeTolerance - kAbsoluteTolerance) {
        BuildEdgeContact(a, b, edgeA, edgeB, edgeAxis, edgeOverlap, out);
        return true;
    }

    if (refIsA) {
        BuildFaceContact(a, b, faceAxisA, true, out);
    } else {
        BuildFaceContact(b, a, faceAxisB, false, out);
    }
    return out.pointCount > 0;
}

} // namespace

// ===================================================
// Matrix3
// ===================================================

Vector3 PhysicsWorld::Matrix3::operator*(const Vector3 &v) const {
    return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}

PhysicsWorld::Matrix3 PhysicsWorld::Matrix3::Inverse() const {
    Matrix3 result;
    const float c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
    const float c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
    const float c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    const float det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    if (std::abs(det) < 1e-12f) {
        return result;
    }
    const float inv = 1.0f / det;
    result.m[0][0] = c00 * inv;
    result.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * inv;
    result.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inv;
    result.m[1][0] = c01 * inv;
    result.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inv;
    result.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * inv;
    result.m[2][0] = c02 * inv;
    result.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * inv;
    result.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inv;
    return result;
}

// ===================================================
// PhysicsWorld
// ===================================================

PhysicsWorld *PhysicsWorld::GetInstance() {
    static PhysicsWorld instance;
    return &instance;
}

void PhysicsWorld::Reset() {
    bodies_.clear();
    contacts_.clear();
    manifolds_.clear();
    bodyIndex_.clear();
    records_.clear();
    impulseCache_.clear();
    discontinuity_ = true;
}

bool PhysicsWorld::IsSleeping(const BaseObject *pObject) const {
    const auto it = records_.find(pObject);
    return it != records_.end() && it->second.sleeping;
}

void PhysicsWorld::WakeUp(const BaseObject *pObject) {
    const auto it = records_.find(pObject);
    if (it != records_.end()) {
        it->second.sleeping = false;
        it->second.sleepTimer = 0.0f;
    }
}

void PhysicsWorld::Step(const std::unordered_map<std::string, BaseObject *> &objects, float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }

    // 1フレームの上限（30fps 相当）
    constexpr float kMaxFrameTime = 1.0f / 30.0f;
    const float frameTime = (std::min)(deltaTime, kMaxFrameTime);
    const int subSteps = (std::max)(settings_.subSteps, 1);
    const float stepTime = frameTime / static_cast<float>(subSteps);

    BuildBodies(objects, frameTime);

    SetKinematicPoses(0.0f);
    for (int step = 0; step < subSteps; ++step) {
        SubStep(stepTime, static_cast<float>(step + 1) / static_cast<float>(subSteps));
    }

    for (Body &body : bodies_) {
        if (!body.dynamic) {
            continue;
        }
        BaseObject *pObject = body.pObject;
        pObject->accumulatedForce_ = {0.0f, 0.0f, 0.0f};
        pObject->accumulatedTorque_ = {0.0f, 0.0f, 0.0f};
        pObject->UpdateWorldTransformHierarchy();
    }

    UpdateSleepAndRecords(frameTime);
    discontinuity_ = false;
}

void PhysicsWorld::BuildBodies(const std::unordered_map<std::string, BaseObject *> &objects, float frameTime) {
    bodies_.clear();
    bodyIndex_.clear();

    for (const auto &[name, pObject] : objects) {
        if (!pObject || !pObject->transform_ || !pObject->GetIsAlive()) {
            continue;
        }
        BaseObject::RigidBodyParams &rigidBody = pObject->rigidBody_;
        const bool dynamic = rigidBody.enabled && rigidBody.enableRotation && pObject->GetParent() == nullptr;

        Body body;
        body.pObject = pObject;
        body.dynamic = dynamic;
        body.friction = rigidBody.friction;
        body.restitution = rigidBody.restitution;
        for (const std::unique_ptr<ColliderBase> &collider : pObject->colliders_) {
            if (collider && collider->IsEnabled()) {
                collider->UpdateWorldTransform();
                body.colliders.push_back(collider.get());
            }
        }
        if (!dynamic && body.colliders.empty()) {
            continue;
        }

        const Vector3 position = pObject->GetWorldPosition();
        const Quaternion rotation = pObject->GetWorldRotation();
        auto recordIt = records_.find(pObject);
        const bool hasRecord = recordIt != records_.end() && recordIt->second.name == pObject->GetName();
        const bool poseChanged = hasRecord &&
                                 ((position - recordIt->second.position).LengthSq() > 1e-10f ||
                                  std::abs(rotation.Dot(recordIt->second.rotation)) < 0.9999999f);

        if (dynamic) {
            const bool hasExternalInput = pObject->accumulatedForce_.LengthSq() > 0.0f ||
                                          pObject->accumulatedTorque_.LengthSq() > 0.0f ||
                                          pObject->accumulatedImpulse_.LengthSq() > 0.0f ||
                                          pObject->accumulatedAngularImpulse_.LengthSq() > 0.0f;
            if (poseChanged) {
                // 外部からの移動は速度を破棄
                rigidBody.velocity = {0.0f, 0.0f, 0.0f};
                rigidBody.angularVelocity = {0.0f, 0.0f, 0.0f};
            }
            body.sleeping = hasRecord && recordIt->second.sleeping && !poseChanged && !hasExternalInput &&
                            rigidBody.velocity.Length() <= settings_.sleepLinearSpeed &&
                            rigidBody.angularVelocity.Length() <= settings_.sleepAngularSpeed;
            ComputeMassProperties(body);

            // 撃力（原点まわりの角撃力を重心まわりへ変換）
            const Vector3 impulse = pObject->accumulatedImpulse_;
            const Vector3 angularImpulse = pObject->accumulatedAngularImpulse_ + (position - body.centerOfMass).Cross(impulse);
            rigidBody.velocity += impulse * body.inverseMass;
            rigidBody.angularVelocity += body.inverseInertia * angularImpulse;
            pObject->accumulatedImpulse_ = {0.0f, 0.0f, 0.0f};
            pObject->accumulatedAngularImpulse_ = {0.0f, 0.0f, 0.0f};

            body.linearVelocity = rigidBody.velocity;
            body.angularVelocity = rigidBody.angularVelocity;
        } else {
            // 姿勢の差分から速度を推定
            body.centerOfMass = position;
            if (hasRecord && !discontinuity_) {
                const Vector3 velocity = (position - recordIt->second.position) / frameTime;
                const Vector3 angularVelocity = AngularVelocityFromRotations(recordIt->second.rotation, rotation, frameTime);
                if (velocity.Length() <= settings_.maxKinematicSpeed &&
                    angularVelocity.Length() <= settings_.maxKinematicAngularSpeed) {
                    body.linearVelocity = velocity;
                    body.angularVelocity = angularVelocity;

                    // サブステップ間の姿勢補間（ルートのみ。ローカル＝ワールド）
                    if (poseChanged && pObject->GetParent() == nullptr) {
                        body.interpolate = true;
                        body.startPosition = recordIt->second.position;
                        body.startRotation = recordIt->second.rotation;
                        body.endPosition = pObject->transform_->translation_;
                        body.endRotation = pObject->transform_->quaternionRotation_;
                    }
                }
            }
            body.moved = poseChanged;
        }

        // 包む球
        Vector3 centerSum{};
        int boundsCount = 0;
        bool unbounded = false;
        for (ColliderBase *pCollider : body.colliders) {
            Vector3 center;
            float radius = 0.0f;
            if (ColliderBounds(pCollider, center, radius)) {
                centerSum += center;
                ++boundsCount;
            } else {
                unbounded = true;
            }
        }
        body.boundsCenter = (boundsCount > 0) ? centerSum / static_cast<float>(boundsCount) : position;
        body.boundingRadius = unbounded ? FLT_MAX : 0.0f;
        if (!unbounded) {
            for (ColliderBase *pCollider : body.colliders) {
                Vector3 center;
                float radius = 0.0f;
                ColliderBounds(pCollider, center, radius);
                body.boundingRadius = (std::max)(body.boundingRadius, (center - body.boundsCenter).Length() + radius);
            }
            if (body.interpolate) {
                body.boundingRadius += (body.endPosition - body.startPosition).Length();
            }
        }

        bodyIndex_[pObject] = static_cast<int>(bodies_.size());
        bodies_.push_back(std::move(body));
    }
}

void PhysicsWorld::ComputeMassProperties(Body &body) const {
    BaseObject *pObject = body.pObject;
    const float mass = pObject->rigidBody_.mass;
    const Vector3 origin = pObject->GetWorldPosition();
    body.inverseMass = (mass > 1e-4f) ? 1.0f / mass : 0.0f;
    body.inverseInertia = Matrix3{};
    body.centerOfMass = origin;

    // 質量を持つ形状（OBB・球・AABB）
    struct Part {
        Vector3 center;
        Vector3 axes[3];
        Vector3 moments; // 単位質量あたりの主慣性モーメント
        float volume;
    };
    Part parts[16];
    int partCount = 0;
    float totalVolume = 0.0f;
    for (ColliderBase *pCollider : body.colliders) {
        if (partCount >= 16) {
            break;
        }
        Part part{};
        part.axes[0] = {1.0f, 0.0f, 0.0f};
        part.axes[1] = {0.0f, 1.0f, 0.0f};
        part.axes[2] = {0.0f, 0.0f, 1.0f};
        Vector3 size{};
        switch (pCollider->GetType()) {
        case ColliderType::OBB: {
            auto *pObb = static_cast<OBBCollider *>(pCollider);
            const OBB obb = pObb->GetOBB();
            part.center = obb.scaleCenterRotated;
            for (int i = 0; i < 3; ++i) {
                part.axes[i] = obb.orientations[i];
            }
            size = pObb->GetSize();
            break;
        }
        case ColliderType::AABB: {
            const AABB box = static_cast<AABBCollider *>(pCollider)->GetAABB();
            part.center = (box.min + box.max) * 0.5f;
            size = box.max - box.min;
            break;
        }
        case ColliderType::Sphere: {
            const Sphere sphere = static_cast<SphereCollider *>(pCollider)->GetSphere();
            part.center = sphere.center;
            const float r2 = sphere.radius * sphere.radius;
            part.moments = {0.4f * r2, 0.4f * r2, 0.4f * r2};
            part.volume = 4.18879f * r2 * sphere.radius;
            break;
        }
        default:
            continue;
        }
        if (pCollider->GetType() != ColliderType::Sphere) {
            const float xx = size.x * size.x;
            const float yy = size.y * size.y;
            const float zz = size.z * size.z;
            part.moments = {(yy + zz) / 12.0f, (xx + zz) / 12.0f, (xx + yy) / 12.0f};
            part.volume = std::abs(size.x * size.y * size.z);
        }
        if (part.volume <= 1e-9f) {
            continue;
        }
        totalVolume += part.volume;
        parts[partCount++] = part;
    }
    if (partCount == 0 || mass <= 1e-4f) {
        return;
    }

    // 体積比で質量を分配した重心
    Vector3 center{};
    for (int i = 0; i < partCount; ++i) {
        center += parts[i].center * (parts[i].volume / totalVolume);
    }
    body.centerOfMass = center;

    // 平行軸の定理で合成した慣性テンソル
    Matrix3 inertia;
    for (int p = 0; p < partCount; ++p) {
        const Part &part = parts[p];
        const float partMass = mass * part.volume / totalVolume;
        const float moments[3] = {part.moments.x, part.moments.y, part.moments.z};
        for (int k = 0; k < 3; ++k) {
            const Vector3 &axis = part.axes[k];
            const float axisArray[3] = {axis.x, axis.y, axis.z};
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    inertia.m[r][c] += partMass * moments[k] * axisArray[r] * axisArray[c];
                }
            }
        }
        const Vector3 d = part.center - center;
        const float dArray[3] = {d.x, d.y, d.z};
        const float d2 = d.LengthSq();
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                inertia.m[r][c] += partMass * ((r == c ? d2 : 0.0f) - dArray[r] * dArray[c]);
            }
        }
    }
    body.inverseInertia = inertia.Inverse();
}

void PhysicsWorld::SetKinematicPoses(float fraction) {
    for (Body &body : bodies_) {
        if (!body.interpolate) {
            continue;
        }
        WorldTransform *pTransform = body.pObject->transform_.get();
        if (fraction >= 1.0f) {
            pTransform->translation_ = body.endPosition;
            pTransform->quaternionRotation_ = body.endRotation;
        } else {
            pTransform->translation_ = body.startPosition + (body.endPosition - body.startPosition) * fraction;
            pTransform->quaternionRotation_ = Quaternion::Slerp(body.startRotation, body.endRotation, fraction);
        }
        body.centerOfMass = pTransform->translation_;
        pTransform->UpdateMatrix();
        for (ColliderBase *pCollider : body.colliders) {
            pCollider->UpdateWorldTransform();
        }
    }
}

void PhysicsWorld::SubStep(float stepTime, float endFraction) {
    // 速度の積分（重力・外力・減衰）
    for (Body &body : bodies_) {
        if (!body.dynamic || body.sleeping) {
            continue;
        }
        ComputeMassProperties(body);
        BaseObject *pObject = body.pObject;
        const BaseObject::RigidBodyParams &rigidBody = pObject->rigidBody_;

        if (rigidBody.useGravity) {
            body.linearVelocity += rigidBody.gravity * stepTime;
        }
        body.linearVelocity += pObject->accumulatedForce_ * (body.inverseMass * stepTime);
        const Vector3 torque = pObject->accumulatedTorque_ +
                               (pObject->GetWorldPosition() - body.centerOfMass).Cross(pObject->accumulatedForce_);
        body.angularVelocity += (body.inverseInertia * torque) * stepTime;

        body.linearVelocity *= 1.0f / (1.0f + rigidBody.linearDamping * stepTime);
        body.angularVelocity *= 1.0f / (1.0f + rigidBody.angularDamping * stepTime);
        const float angularSpeed = body.angularVelocity.Length();
        if (angularSpeed > kMaxAngularSpeed) {
            body.angularVelocity *= kMaxAngularSpeed / angularSpeed;
        }
    }

    BuildContacts(stepTime);

    // 速度拘束の反復
    const int iterations = (std::max)(settings_.velocityIterations, 1);
    for (int iteration = 0; iteration < iterations; ++iteration) {
        for (ContactConstraint &c : contacts_) {
            // 法線方向
            const float normalSpeed = RelativeVelocity(c).Dot(c.normal);
            const float previousNormal = c.normalImpulse;
            c.normalImpulse = (std::max)(previousNormal + (c.targetSpeed - normalSpeed) * c.normalMass, 0.0f);
            ApplyImpulse(c, c.normal * (c.normalImpulse - previousNormal));

            // クーロン摩擦（円錐の近似として接線インパルスの大きさを制限）
            const Vector3 relative = RelativeVelocity(c);
            const float previous1 = c.tangentImpulse1;
            const float previous2 = c.tangentImpulse2;
            float tangent1 = previous1 - relative.Dot(c.tangent1) * c.tangentMass1;
            float tangent2 = previous2 - relative.Dot(c.tangent2) * c.tangentMass2;
            const float maxFriction = c.friction * c.normalImpulse;
            const float tangentLength = std::sqrt(tangent1 * tangent1 + tangent2 * tangent2);
            if (tangentLength > maxFriction && tangentLength > 1e-12f) {
                const float scale = maxFriction / tangentLength;
                tangent1 *= scale;
                tangent2 *= scale;
            }
            c.tangentImpulse1 = tangent1;
            c.tangentImpulse2 = tangent2;
            ApplyImpulse(c, c.tangent1 * (tangent1 - previous1) + c.tangent2 * (tangent2 - previous2));
        }
    }

    // ウォームスタート用に保存
    impulseCache_.clear();
    for (const ContactConstraint &c : contacts_) {
        CachedImpulse &cached = impulseCache_[c.key];
        cached.normal = c.normalImpulse;
        cached.tangent = c.tangent1 * c.tangentImpulse1 + c.tangent2 * c.tangentImpulse2;
    }

    IntegratePositions(stepTime);
    SetKinematicPoses(endFraction);
    CorrectPositions();
}

bool PhysicsWorld::CollideColliders(ColliderBase *a, ColliderBase *b, ContactManifold &outManifold) const {
    if (a->GetType() == ColliderType::OBB && b->GetType() == ColliderType::OBB) {
        const Box boxA = ToBox(static_cast<OBBCollider *>(a)->GetOBB());
        const Box boxB = ToBox(static_cast<OBBCollider *>(b)->GetOBB());
        if (CollideBoxes(boxA, boxB, outManifold)) {
            return true;
        }
    }
    return CollisionManager::GetInstance()->ComputeContactManifold(a, b, outManifold);
}

void PhysicsWorld::BuildContacts(float stepTime) {
    contacts_.clear();
    manifolds_.clear();

    const int bodyCount = static_cast<int>(bodies_.size());
    for (int i = 0; i < bodyCount; ++i) {
        if (!bodies_[i].dynamic) {
            continue;
        }
        for (int j = 0; j < bodyCount; ++j) {
            if (i == j || (bodies_[j].dynamic && j < i)) {
                continue;
            }
            Body &bodyA = bodies_[i];
            Body &bodyB = bodies_[j];

            // 両方とも止まっている組は対象外
            const bool activeA = !bodyA.sleeping;
            const bool activeB = bodyB.dynamic ? !bodyB.sleeping : bodyB.moved;
            if (!activeA && !activeB) {
                continue;
            }

            if (bodyA.boundingRadius < FLT_MAX && bodyB.boundingRadius < FLT_MAX) {
                const float reach = bodyA.boundingRadius + bodyB.boundingRadius;
                if ((bodyA.boundsCenter - bodyB.boundsCenter).LengthSq() > reach * reach) {
                    continue;
                }
            }

            // 近くの静的物体が動いたら起こす（支えが離れた場合の落下用）
            if (bodyA.sleeping && !bodyB.dynamic && bodyB.moved) {
                WakeBody(bodyA);
            }

            for (ColliderBase *pColliderA : bodyA.colliders) {
                for (ColliderBase *pColliderB : bodyB.colliders) {
                    if (!ShouldCollide(pColliderA, pColliderB)) {
                        continue;
                    }
                    ContactManifold manifold;
                    if (!CollideColliders(pColliderA, pColliderB, manifold) || manifold.pointCount == 0) {
                        continue;
                    }

                    // 動いている物体に触れられたら起こす（静止中の相手は静的物体として扱う）
                    if (bodyA.sleeping && (bodyB.dynamic ? IsMoving(bodyB) : bodyB.moved)) {
                        WakeBody(bodyA);
                    }
                    if (bodyB.dynamic && bodyB.sleeping && IsMoving(bodyA)) {
                        WakeBody(bodyB);
                    }
                    if (!IsSimulated(bodyA) && !IsSimulated(bodyB)) {
                        continue;
                    }
                    manifolds_.push_back({i, j, pColliderA, pColliderB});

                    for (int p = 0; p < manifold.pointCount; ++p) {
                        const ContactPoint &point = manifold.points[p];
                        ContactConstraint c;
                        c.bodyA = i;
                        c.bodyB = j;
                        c.key = MakeContactKey(pColliderA, pColliderB, point.feature);
                        c.normal = manifold.normal;
                        MakeTangents(c.normal, c.tangent1, c.tangent2);
                        c.rA = point.position - bodyA.centerOfMass;
                        c.rB = point.position - bodyB.centerOfMass;
                        c.depth = point.depth;
                        c.normalMass = EffectiveMass(c, c.normal);
                        c.tangentMass1 = EffectiveMass(c, c.tangent1);
                        c.tangentMass2 = EffectiveMass(c, c.tangent2);
                        c.friction = std::sqrt((std::max)(bodyA.friction, 0.0f) * (std::max)(bodyB.friction, 0.0f));

                        // 反発
                        const float normalSpeed = RelativeVelocity(c).Dot(c.normal);
                        const float restitution = (std::max)(bodyA.restitution, bodyB.restitution);
                        c.targetSpeed = (normalSpeed < -settings_.restitutionThreshold) ? -restitution * normalSpeed : 0.0f;

                        // ウォームスタート
                        if (const auto cached = impulseCache_.find(c.key); cached != impulseCache_.end()) {
                            c.normalImpulse = cached->second.normal;
                            c.tangentImpulse1 = cached->second.tangent.Dot(c.tangent1);
                            c.tangentImpulse2 = cached->second.tangent.Dot(c.tangent2);
                            ApplyImpulse(c, c.normal * c.normalImpulse + c.tangent1 * c.tangentImpulse1 + c.tangent2 * c.tangentImpulse2);
                        }
                        contacts_.push_back(c);
                    }
                }
            }
        }
    }
    (void)stepTime;
}

Vector3 PhysicsWorld::RelativeVelocity(const ContactConstraint &c) const {
    const Body &a = bodies_[c.bodyA];
    const Body &b = bodies_[c.bodyB];
    const Vector3 velocityA = a.linearVelocity + a.angularVelocity.Cross(c.rA);
    const Vector3 velocityB = b.linearVelocity + b.angularVelocity.Cross(c.rB);
    return velocityA - velocityB;
}

void PhysicsWorld::ApplyImpulse(const ContactConstraint &c, const Vector3 &impulse) {
    Body &a = bodies_[c.bodyA];
    Body &b = bodies_[c.bodyB];
    if (IsSimulated(a)) {
        a.linearVelocity += impulse * a.inverseMass;
        a.angularVelocity += a.inverseInertia * c.rA.Cross(impulse);
    }
    if (IsSimulated(b)) {
        b.linearVelocity -= impulse * b.inverseMass;
        b.angularVelocity -= b.inverseInertia * c.rB.Cross(impulse);
    }
}

float PhysicsWorld::EffectiveMass(const ContactConstraint &c, const Vector3 &direction) const {
    const Body &a = bodies_[c.bodyA];
    const Body &b = bodies_[c.bodyB];
    float k = 0.0f;
    if (IsSimulated(a)) {
        k += a.inverseMass + (a.inverseInertia * c.rA.Cross(direction)).Cross(c.rA).Dot(direction);
    }
    if (IsSimulated(b)) {
        k += b.inverseMass + (b.inverseInertia * c.rB.Cross(direction)).Cross(c.rB).Dot(direction);
    }
    return SafeInverse(k);
}

void PhysicsWorld::IntegratePositions(float stepTime) {
    for (Body &body : bodies_) {
        if (!body.dynamic || body.sleeping) {
            continue;
        }
        WorldTransform *pTransform = body.pObject->transform_.get();

        // 重心まわりの回転と並進
        const Vector3 offset = pTransform->translation_ - body.centerOfMass;
        const float angularSpeed = body.angularVelocity.Length();
        Vector3 rotatedOffset = offset;
        if (angularSpeed * stepTime > 1e-7f) {
            const Quaternion delta = Quaternion::FromAxisAngle(body.angularVelocity / angularSpeed, angularSpeed * stepTime);
            rotatedOffset = delta.Rotate(offset);
        }
        body.centerOfMass += body.linearVelocity * stepTime;
        pTransform->translation_ = body.centerOfMass + rotatedOffset;
        pTransform->quaternionRotation_ = RotateByAngularVelocity(pTransform->quaternionRotation_, body.angularVelocity, stepTime);

        pTransform->UpdateMatrix();
        for (ColliderBase *pCollider : body.colliders) {
            pCollider->UpdateWorldTransform();
        }
    }
}

void PhysicsWorld::CorrectPositions() {
    // 移動後の姿勢で再判定し、めり込みを質量比で解消
    for (const ManifoldRecord &record : manifolds_) {
        Body &a = bodies_[record.bodyA];
        Body &b = bodies_[record.bodyB];
        const float weightA = IsSimulated(a) ? a.inverseMass : 0.0f;
        const float weightB = IsSimulated(b) ? b.inverseMass : 0.0f;
        const float totalWeight = weightA + weightB;
        if (totalWeight <= 0.0f) {
            continue;
        }
        Vector3 mtv;
        if (!CollisionManager::GetInstance()->ComputeDepenetration(record.pColliderA, record.pColliderB, mtv)) {
            continue;
        }
        const float depth = mtv.Length();
        const float correction = (std::min)((depth - settings_.penetrationSlop) * settings_.positionCorrection, settings_.maxCorrection);
        if (correction <= 0.0f) {
            continue;
        }
        const Vector3 push = mtv / depth * correction;
        if (weightA > 0.0f) {
            TranslateBody(a, push * (weightA / totalWeight));
        }
        if (weightB > 0.0f) {
            TranslateBody(b, push * (-weightB / totalWeight));
        }
    }
}

void PhysicsWorld::TranslateBody(Body &body, const Vector3 &offset) {
    WorldTransform *pTransform = body.pObject->transform_.get();
    pTransform->translation_ += offset;
    body.centerOfMass += offset;
    pTransform->UpdateMatrix();
    for (ColliderBase *pCollider : body.colliders) {
        pCollider->UpdateWorldTransform();
    }
}

void PhysicsWorld::WakeBody(Body &body) {
    body.sleeping = false;
    WakeUp(body.pObject);
}

bool PhysicsWorld::IsSimulated(const Body &body) {
    return body.dynamic && !body.sleeping;
}

bool PhysicsWorld::IsMoving(const Body &body) const {
    return IsSimulated(body) && (body.linearVelocity.Length() > settings_.sleepLinearSpeed * 2.0f ||
                                 body.angularVelocity.Length() > settings_.sleepAngularSpeed * 2.0f);
}

void PhysicsWorld::UpdateSleepAndRecords(float frameTime) {
    const int bodyCount = static_cast<int>(bodies_.size());
    std::vector<BodyRecord> records(bodyCount);

    // 静止時間の更新
    for (int i = 0; i < bodyCount; ++i) {
        Body &body = bodies_[i];
        BaseObject *pObject = body.pObject;
        BodyRecord &record = records[i];
        if (const auto it = records_.find(pObject); it != records_.end() && it->second.name == pObject->GetName()) {
            record = it->second;
        }
        record.name = pObject->GetName();
        if (IsSimulated(body)) {
            const bool slow = body.linearVelocity.Length() < settings_.sleepLinearSpeed &&
                              body.angularVelocity.Length() < settings_.sleepAngularSpeed;
            record.sleepTimer = slow ? record.sleepTimer + frameTime : 0.0f;
        }
    }

    // 接触でつながった剛体の組（アイランド）単位でスリープ
    std::vector<int> parent(bodyCount);
    for (int i = 0; i < bodyCount; ++i) {
        parent[i] = i;
    }
    std::function<int(int)> findRoot = [&](int i) { return parent[i] == i ? i : (parent[i] = findRoot(parent[i])); };
    for (const ContactConstraint &c : contacts_) {
        if (IsSimulated(bodies_[c.bodyA]) && IsSimulated(bodies_[c.bodyB])) {
            parent[findRoot(c.bodyA)] = findRoot(c.bodyB);
        }
    }
    std::vector<char> islandReady(bodyCount, 1);
    for (int i = 0; i < bodyCount; ++i) {
        if (IsSimulated(bodies_[i]) && records[i].sleepTimer < settings_.sleepTime) {
            islandReady[findRoot(i)] = 0;
        }
    }
    for (int i = 0; i < bodyCount; ++i) {
        if (IsSimulated(bodies_[i]) && settings_.enableSleep && islandReady[findRoot(i)]) {
            bodies_[i].sleeping = true;
        }
    }

    std::unordered_map<const BaseObject *, BodyRecord> nextRecords;
    nextRecords.reserve(bodies_.size());
    for (int i = 0; i < bodyCount; ++i) {
        Body &body = bodies_[i];
        BaseObject *pObject = body.pObject;
        BodyRecord &record = records[i];

        if (body.dynamic) {
            if (body.sleeping) {
                body.linearVelocity = {0.0f, 0.0f, 0.0f};
                body.angularVelocity = {0.0f, 0.0f, 0.0f};
            }
            record.sleeping = body.sleeping;
            pObject->rigidBody_.velocity = body.linearVelocity;
            pObject->rigidBody_.angularVelocity = body.angularVelocity;
        }

        record.position = pObject->GetWorldPosition();
        record.rotation = pObject->GetWorldRotation();
        nextRecords[pObject] = record;
    }
    records_ = std::move(nextRecords);
}

#ifdef USE_IMGUI
void PhysicsWorld::DrawSettingsImGui() {
    ImGui::SliderInt("サブステップ##pwsub", &settings_.subSteps, 1, 16);
    ImGui::SliderInt("反復回数##pwiter", &settings_.velocityIterations, 1, 40);
    ImGui::DragFloat("許容めり込み##pwslop", &settings_.penetrationSlop, 0.001f, 0.0f, 0.1f, "%.3f");
    ImGui::DragFloat("めり込み補正率##pwcorr", &settings_.positionCorrection, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::DragFloat("反発の最小速度##pwrest", &settings_.restitutionThreshold, 0.01f, 0.0f, 5.0f, "%.2f");
    ImGui::Checkbox("スリープ##pwsleep", &settings_.enableSleep);
    ImGui::DragFloat("スリープ秒数##pwsleeptime", &settings_.sleepTime, 0.05f, 0.0f, 10.0f, "%.2f");
}
#endif // USE_IMGUI

} // namespace Hagine
