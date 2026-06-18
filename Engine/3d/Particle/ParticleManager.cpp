#define NOMINMAX
#include "ParticleManager.h"
#include "Engine/Frame/Frame.h"
#include "Graphics/Texture/TextureManager.h"
#include <fstream>
#include <random>

namespace Hagine {
void ParticleManager::Initialize(SrvManager *srvManager) {
    particleCommon_ = ParticleCommon::GetInstance();
    srvManager_ = srvManager;
    randomEngine_.seed(seedGenerator_());
}

void ParticleManager::Update(const ViewProjection &viewProjection) {
    Matrix4x4 viewProjectionMatrix = viewProjection.matView_ * viewProjection.matProjection_;
    Matrix4x4 billboardMatrix = viewProjection.matView_;
    billboardMatrix.m[3][0] = 0.0f;
    billboardMatrix.m[3][1] = 0.0f;
    billboardMatrix.m[3][2] = 0.0f;
    billboardMatrix.m[3][3] = 1.0f;
    billboardMatrix = Inverse(billboardMatrix);

    for (auto &[groupName, particleGroup] : particleGroups_) {
        uint32_t numInstance = 0;
        ParticleSetting &particleSetting = particleSettings_[groupName];

        auto &particles = particleGroup->GetParticleGroupData().particles;
        std::list<Particle> aliveParticles;
        for (auto &particle : particles) {
            if (particle.lifeTime <= particle.currentTime) {
                continue;
            }

            // ブレンドモード設定
            particleGroup->GetParticleGroupData().blendMode = particle.blendMode;

            // 軌跡パーティクル生成処理
            if (particleSetting.enableTrail && !particle.isChild) {
                particle.trailSpawnTimer += Frame::DeltaTime();
                if (particle.trailSpawnTimer >= particleSetting.trailSpawnInterval) {
                    CreateTrailParticle(particle, particleSetting);
                    particle.trailSpawnTimer = 0.0f;
                }
            }

            float t = particle.currentTime / particle.lifeTime;
            t = std::clamp(t, 0.0f, 1.0f);

            // --- 色補間処理 ---
            if (!particle.isChild && !particleSetting.isRandomColor) {
                // 通常パーティクルのみ補間
                const Vector4 &startColor = particleSetting.startColor;
                const Vector4 &endColor = particleSetting.endColor;
                particle.color.x = (1.0f - t) * startColor.x + t * endColor.x;
                particle.color.y = (1.0f - t) * startColor.y + t * endColor.y;
                particle.color.z = (1.0f - t) * startColor.z + t * endColor.z;
                // アルファは既存ロジック
            }

            if (particleSetting.isSinMove) {
                float waveScale = 0.5f * (sin(t * DirectX::XM_PI * 18.0f) + 1.0f);
                float maxScale = (1.0f - t);
                particle.transform.scale_ =
                    particle.startScale * waveScale * maxScale;
            } else {
                particle.transform.scale_ =
                    (1.0f - t) * particle.startScale + t * particle.endScale;
                if (!(particleSetting.isGatherMode && t >= particleSetting.gatherStartRatio)) {
                    particle.color.w = particle.initialAlpha - (particle.currentTime / particle.lifeTime);
                }
            }

            bool isGathering = false;
            // トレイルパーティクルで速度継承がオフの場合はギャザリングしない
            bool shouldGather = particleSetting.isGatherMode && t >= particleSetting.gatherStartRatio;
            if (particle.isChild && !particleSetting.trailInheritVelocity) {
                shouldGather = false;
            }

            if (shouldGather) {
                particle.emitterPosition = emitterCenter_;
                isGathering = true;
                float gatherFactor = (t - particleSetting.gatherStartRatio) / (1.0f - particleSetting.gatherStartRatio);
                gatherFactor = std::clamp(gatherFactor, 0.0f, 1.0f);
                Vector3 toEmitter = particle.emitterPosition - particle.transform.translation_;
                float distance = toEmitter.Length();
                float distanceBasedAlpha = distance / (distance + 0.5f);
                particle.color.w = particle.initialAlpha * (1.0f - gatherFactor) * distanceBasedAlpha;
                if (distance < 0.05f) {
                    particle.currentTime = particle.lifeTime;
                    continue;
                }
                float distanceFactor = std::min(1.0f, distance);
                toEmitter = toEmitter.Normalize();
                float gatherSpeed = particleSetting.gatherStrength * gatherFactor * distanceFactor * 3.0f;
                Vector3 gatherVelocity = toEmitter * gatherSpeed * Frame::DeltaTime();
                particle.velocity = gatherVelocity;
                particle.transform.translation_ += particle.velocity;
            }

            if (!isGathering) {
                particle.Acce = (1.0f - t) * particle.startAcce + t * particle.endAcce;

                if (particleSetting.isFaceDirection) {
                    Vector3 forward = particle.fixedDirection;
                    Vector3 initialUp = {0.0f, 1.0f, 0.0f};
                    Vector3 rotationAxis = initialUp.Cross(forward).Normalize();
                    float dotProduct = initialUp.Dot(forward);
                    float angle = acosf(std::clamp(dotProduct, -1.0f, 1.0f));
                    particle.transform.eulerRotation_.x = rotationAxis.x * angle;
                    particle.transform.eulerRotation_.y = rotationAxis.y * angle;
                    particle.transform.eulerRotation_.z = rotationAxis.z * angle;
                } else if (particleSetting.isRandomRotate) {
                    particle.transform.eulerRotation_ += particle.rotateVelocity;
                } else {
                    particle.transform.eulerRotation_ =
                        (1.0f - t) * particle.startRote + t * particle.endRote;
                }

                if (particleSetting.isAcceMultiply) {
                    particle.velocity *= particle.Acce;
                } else {
                    particle.velocity += particle.Acce;
                }
                particle.transform.translation_ +=
                    particle.velocity * Frame::DeltaTime();
            }

            particle.velocity.y -= particleSetting.gravity * Frame::DeltaTime();
            particle.currentTime += Frame::DeltaTime();

            Matrix4x4 worldMatrix{};

            // === 各軸ビルボード処理 ===
            if (particleSetting.isBillboard || particleSetting.isBillboardX ||
                particleSetting.isBillboardY || particleSetting.isBillboardZ) {

                Matrix4x4 customBillboardMatrix = MakeIdentity4x4();
                Matrix4x4 viewMatrix = viewProjection.matView_;

                // ビューマトリックスから回転成分を抽出
                Vector3 right = {viewMatrix.m[0][0], viewMatrix.m[1][0], viewMatrix.m[2][0]};
                Vector3 up = {viewMatrix.m[0][1], viewMatrix.m[1][1], viewMatrix.m[2][1]};
                Vector3 forward = {viewMatrix.m[0][2], viewMatrix.m[1][2], viewMatrix.m[2][2]};

                if (particleSetting.isBillboard) {
                    // 従来の完全ビルボード
                    customBillboardMatrix = billboardMatrix;
                } else {
                    // 各軸のビルボード処理
                    Vector3 finalRight = right;
                    Vector3 finalUp = up;
                    Vector3 finalForward = forward;

                    if (!particleSetting.isBillboardX) {
                        // X軸を固定（World空間のX軸を使用）
                        finalRight = {1.0f, 0.0f, 0.0f};
                        finalForward = finalUp.Cross(finalRight).Normalize();
                        finalUp = finalRight.Cross(finalForward).Normalize();
                    }

                    if (!particleSetting.isBillboardY) {
                        // Y軸を固定（World空間のY軸を使用）
                        finalUp = {0.0f, 1.0f, 0.0f};
                        finalRight = finalUp.Cross(finalForward).Normalize();
                        finalForward = finalRight.Cross(finalUp).Normalize();
                    }

                    if (!particleSetting.isBillboardZ) {
                        // Z軸を固定（World空間のZ軸を使用）
                        finalForward = {0.0f, 0.0f, 1.0f};
                        finalRight = finalUp.Cross(finalForward).Normalize();
                        finalUp = finalForward.Cross(finalRight).Normalize();
                    }

                    // カスタムビルボードマトリックス構築
                    customBillboardMatrix.m[0][0] = finalRight.x;
                    customBillboardMatrix.m[1][0] = finalRight.y;
                    customBillboardMatrix.m[2][0] = finalRight.z;
                    customBillboardMatrix.m[0][1] = finalUp.x;
                    customBillboardMatrix.m[1][1] = finalUp.y;
                    customBillboardMatrix.m[2][1] = finalUp.z;
                    customBillboardMatrix.m[0][2] = finalForward.x;
                    customBillboardMatrix.m[1][2] = finalForward.y;
                    customBillboardMatrix.m[2][2] = finalForward.z;
                    customBillboardMatrix.m[3][3] = 1.0f;
                }

                // ビルボード後にZ軸回転を適用
                Matrix4x4 zRotationMatrix = MakeIdentity4x4();
                if (particleSetting.isRandomRotate ||
                    (!particleSetting.isFaceDirection && (particle.transform.eulerRotation_.z != 0.0f || particle.rotateVelocity.z != 0.0f))) {
                    // Z軸回転マトリックスを作成
                    float cosZ = cosf(particle.transform.eulerRotation_.z);
                    float sinZ = sinf(particle.transform.eulerRotation_.z);
                    zRotationMatrix.m[0][0] = cosZ;
                    zRotationMatrix.m[0][1] = -sinZ;
                    zRotationMatrix.m[1][0] = sinZ;
                    zRotationMatrix.m[1][1] = cosZ;
                }

                worldMatrix = MakeScaleMatrix(particle.transform.scale_) * zRotationMatrix * customBillboardMatrix *
                              MakeTranslateMatrix(particle.transform.translation_);
            } else {
                worldMatrix = MakeAffineMatrix(particle.transform.scale_,
                                               particle.transform.eulerRotation_,
                                               particle.transform.translation_);
            }

            Matrix4x4 worldViewProjectionMatrix = worldMatrix * viewProjectionMatrix;
            if (numInstance < particleGroup->GetMaxInstance()) {
                particleGroup->GetParticleGroupData().instancingData[numInstance].WVP = worldViewProjectionMatrix;
                particleGroup->GetParticleGroupData().instancingData[numInstance].World = worldMatrix;
                particleGroup->GetParticleGroupData().instancingData[numInstance].color = particle.color;
                particleGroup->GetParticleGroupData().instancingData[numInstance].color.w = particle.color.w;
                ++numInstance;
            }
            aliveParticles.push_back(std::move(particle));
        }
        particles.swap(aliveParticles);
        particleGroup->GetParticleGroupData().instanceCount = numInstance;
    }
}

// 軌跡パーティクル生成メソッド
void ParticleManager::CreateTrailParticle(const Particle &parent, const ParticleSetting &setting) {
    // 軌跡パーティクルを作成
    Particle trailParticle;
    trailParticle.isChild = true;
    trailParticle.createTrail = false; // 軌跡は軌跡を作らない

    // 親の現在位置に配置
    trailParticle.transform.translation_ = parent.transform.translation_;
    trailParticle.transform.eulerRotation_ = parent.transform.eulerRotation_;
    trailParticle.transform.scale_ = parent.transform.scale_ * setting.trailScaleMultiplier;

    // 速度の設定
    if (setting.trailInheritVelocity) {
        trailParticle.velocity = parent.velocity * setting.trailVelocityScale;
    } else {
        trailParticle.velocity = {0.0f, 0.0f, 0.0f};
    }

    // 色と透明度
    trailParticle.color = parent.color * setting.trailColorMultiplier;
    trailParticle.initialAlpha = trailParticle.color.w;

    // 寿命
    trailParticle.lifeTime = parent.lifeTime * setting.trailLifeScale;
    trailParticle.currentTime = 0.0f;

    // その他のプロパティをコピー
    trailParticle.startScale = parent.startScale * setting.trailScaleMultiplier;
    trailParticle.endScale = parent.endScale * setting.trailScaleMultiplier;
    trailParticle.startAcce = parent.startAcce;
    trailParticle.endAcce = parent.endAcce;
    trailParticle.blendMode = parent.blendMode;

    // パーティクルグループに追加
    for (auto &[groupName, particleGroup] : particleGroups_) {
        if (particleSettings_[groupName].enableTrail) {
            particleGroup->GetParticleGroupData().particles.push_back(trailParticle);
            break;
        }
    }
}

void ParticleManager::SetTrailEnabled(const std::string &groupName, bool enabled) {
    if (particleSettings_.find(groupName) != particleSettings_.end()) {
        particleSettings_[groupName].enableTrail = enabled;
    }
}

void ParticleManager::SetTrailSettings(const std::string &groupName, float interval, int maxTrails) {
    if (particleSettings_.find(groupName) != particleSettings_.end()) {
        particleSettings_[groupName].trailSpawnInterval = interval;
        particleSettings_[groupName].maxTrailParticles = maxTrails;
    }
}

void ParticleManager::Draw() {
    for (auto &[groupName, particleGroup] : particleGroups_) {
        particleCommon_->DrawCommonSetting(particleGroup->GetParticleGroupData().blendMode);
        const auto &meshes = particleGroup->GetModelData().meshes;
        for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
            D3D12_INDEX_BUFFER_VIEW indexBufferView = particleGroup->GetIndexBufferView();
            D3D12_VERTEX_BUFFER_VIEW vertexBufferView = particleGroup->GetVertexBufferView();
            particleCommon_->GetDxCommon()->GetCommandList()->IASetIndexBuffer(&indexBufferView);
            particleCommon_->GetDxCommon()->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView);
            if (particleGroup->GetParticleGroupData().instanceCount > 0) {
                particleCommon_->GetDxCommon()->GetCommandList()->SetGraphicsRootConstantBufferView(0, particleGroup->GetmaterialResource()->GetGPUVirtualAddress());
                srvManager_->SetGraphicsRootDescriptorTable(1, particleGroup->GetParticleGroupData().instancingSRVIndex);
                srvManager_->SetGraphicsRootDescriptorTable(2, particleGroup->GetParticleGroupData().materials[meshIndex].textureIndex);
                particleCommon_->GetDxCommon()->GetCommandList()->DrawIndexedInstanced(
                    UINT(meshes[meshIndex].indices.size()),
                    particleGroup->GetParticleGroupData().instanceCount,
                    0, 0, 0);
            }
        }
    }
}

void ParticleManager::AddParticleGroup(ParticleGroup *particleGroup) {
    assert(particleGroup);
    std::string groupName = particleGroup->GetGroupName();
    particleGroups_.insert(std::pair(groupName, particleGroup));
    particleGroupNames_.push_back(groupName);
    // デフォルト設定を追加
    if (particleSettings_.find(groupName) == particleSettings_.end()) {
        particleSettings_[groupName] = ParticleSetting{};
    }
}

void ParticleManager::RemoveParticleGroup(const std::string &name) {
    // マップから削除
    particleGroups_.erase(name);
    particleSettings_.erase(name);

    // vector からも削除
    auto it = std::find(particleGroupNames_.begin(), particleGroupNames_.end(), name);
    if (it != particleGroupNames_.end()) {
        particleGroupNames_.erase(it);
    }
}

void ParticleManager::SetParticleSetting(const std::string &groupName, const ParticleSetting &setting) {
    particleSettings_[groupName] = setting;
}
ParticleSetting &ParticleManager::GetParticleSetting(const std::string &groupName) {
    return particleSettings_[groupName];
}
std::vector<std::string> ParticleManager::GetParticleGroupsName() {
    return particleGroupNames_;
}

Particle ParticleManager::MakeNewParticle(std::mt19937 &randomEngine_, const ParticleSetting &setting) {
    std::uniform_real_distribution<float> distribution(-1.0f, 1.0f);
    std::uniform_real_distribution<float> distVelocityX(setting.velocityMin.x, setting.velocityMax.x);
    std::uniform_real_distribution<float> distVelocityY(setting.velocityMin.y, setting.velocityMax.y);
    std::uniform_real_distribution<float> distVelocityZ(setting.velocityMin.z, setting.velocityMax.z);
    std::uniform_real_distribution<float> distLifeTime(setting.lifeTimeMin, setting.lifeTimeMax);
    std::uniform_real_distribution<float> distAlpha(setting.alphaMin, setting.alphaMax);

    Particle particle;
    particle.transform.isUseQuaternion_ = false;
    Vector3 randomTranslate;
    particle.emitterPosition = setting.translate;
    if (setting.isEmitOnEdge) {
        std::uniform_int_distribution<int> edgeSelector(0, 11);
        std::uniform_real_distribution<float> edgePosition(0.0f, 1.0f);
        int selectedEdge = edgeSelector(randomEngine_);
        float position = edgePosition(randomEngine_);
        const Vector3 v0 = {-1.0f, -1.0f, -1.0f};
        const Vector3 v1 = {1.0f, -1.0f, -1.0f};
        const Vector3 v2 = {-1.0f, 1.0f, -1.0f};
        const Vector3 v3 = {1.0f, 1.0f, -1.0f};
        const Vector3 v4 = {-1.0f, -1.0f, 1.0f};
        const Vector3 v5 = {1.0f, -1.0f, 1.0f};
        const Vector3 v6 = {-1.0f, 1.0f, 1.0f};
        const Vector3 v7 = {1.0f, 1.0f, 1.0f};
        const std::pair<Vector3, Vector3> edges[] = {
            {v0, v1}, {v1, v3}, {v3, v2}, {v2, v0}, {v4, v5}, {v5, v7}, {v7, v6}, {v6, v4}, {v0, v4}, {v1, v5}, {v2, v6}, {v3, v7}};
        const Vector3 &start = edges[selectedEdge].first;
        const Vector3 &end = edges[selectedEdge].second;
        randomTranslate = {
            start.x + (end.x - start.x) * position,
            start.y + (end.y - start.y) * position,
            start.z + (end.z - start.z) * position};
        randomTranslate.x *= setting.scale.x;
        randomTranslate.y *= setting.scale.y;
        randomTranslate.z *= setting.scale.z;
    } else {
        randomTranslate = {
            distribution(randomEngine_) * setting.scale.x,
            distribution(randomEngine_) * setting.scale.y,
            distribution(randomEngine_) * setting.scale.z};
    }
    Matrix4x4 rotationMatrix = MakeRotateXYZMatrix(setting.rotation);
    Vector3 rotatedPosition = {
        randomTranslate.x * rotationMatrix.m[0][0] + randomTranslate.y * rotationMatrix.m[1][0] + randomTranslate.z * rotationMatrix.m[2][0],
        randomTranslate.x * rotationMatrix.m[0][1] + randomTranslate.y * rotationMatrix.m[1][1] + randomTranslate.z * rotationMatrix.m[2][1],
        randomTranslate.x * rotationMatrix.m[0][2] + randomTranslate.y * rotationMatrix.m[1][2] + randomTranslate.z * rotationMatrix.m[2][2]};
    particle.transform.translation_ = setting.translate + rotatedPosition;

    if (setting.isRandomAllSize) {
        std::uniform_real_distribution<float> distScaleX(setting.allScaleMin.x, setting.allScaleMax.x);
        std::uniform_real_distribution<float> distScaleY(setting.allScaleMin.y, setting.allScaleMax.y);
        std::uniform_real_distribution<float> distScaleZ(setting.allScaleMin.z, setting.allScaleMax.z);
        particle.startScale = {distScaleX(randomEngine_), distScaleY(randomEngine_), distScaleZ(randomEngine_)};
        if (setting.isEndScale) {
            particle.endScale = particle.startScale;
        }
    } else if (setting.isRandomSize) {
        std::uniform_real_distribution<float> distScale(setting.scaleMin, setting.scaleMax);
        particle.startScale.x = distScale(randomEngine_);
        particle.startScale.y = particle.startScale.x;
        particle.startScale.z = particle.startScale.x;
    } else {
        particle.startScale = setting.particleStartScale;
    }
    if (!setting.isEndScale) {
        particle.endScale = setting.particleEndScale;
    }
    particle.startAcce = setting.startAcce;
    particle.endAcce = setting.endAcce;
    Vector3 randomVelocity = {
        distVelocityX(randomEngine_),
        distVelocityY(randomEngine_),
        distVelocityZ(randomEngine_)};
    particle.velocity = {
        randomVelocity.x * rotationMatrix.m[0][0] + randomVelocity.y * rotationMatrix.m[1][0] + randomVelocity.z * rotationMatrix.m[2][0],
        randomVelocity.x * rotationMatrix.m[0][1] + randomVelocity.y * rotationMatrix.m[1][1] + randomVelocity.z * rotationMatrix.m[2][1],
        randomVelocity.x * rotationMatrix.m[0][2] + randomVelocity.y * rotationMatrix.m[1][2] + randomVelocity.z * rotationMatrix.m[2][2]};
    if (setting.isRandomRotate) {
        std::uniform_real_distribution<float> distRotateX(setting.rotateStartMin.x, setting.rotateStartMax.x);
        std::uniform_real_distribution<float> distRotateY(setting.rotateStartMin.y, setting.rotateStartMax.y);
        std::uniform_real_distribution<float> distRotateZ(setting.rotateStartMin.z, setting.rotateStartMax.z);
        particle.transform.eulerRotation_.x = distRotateX(randomEngine_);
        particle.transform.eulerRotation_.y = distRotateY(randomEngine_);
        particle.transform.eulerRotation_.z = distRotateZ(randomEngine_);
        if (setting.isRotateVelocity) {
            std::uniform_real_distribution<float> distRotateXVelocity(setting.rotateVelocityMin.x, setting.rotateVelocityMax.x);
            std::uniform_real_distribution<float> distRotateYVelocity(setting.rotateVelocityMin.y, setting.rotateVelocityMax.y);
            std::uniform_real_distribution<float> distRotateZVelocity(setting.rotateVelocityMin.z, setting.rotateVelocityMax.z);
            particle.rotateVelocity.x = distRotateXVelocity(randomEngine_);
            particle.rotateVelocity.y = distRotateYVelocity(randomEngine_);
            particle.rotateVelocity.z = distRotateZVelocity(randomEngine_);
        }
    } else {
        particle.startRote = setting.startRote;
        particle.endRote = setting.endRote;
    }

    if (setting.isRandomColor) {
        std::uniform_real_distribution<float> distColor(0.0f, 1.0f);
        particle.color = {distColor(randomEngine_), distColor(randomEngine_), distColor(randomEngine_), distAlpha(randomEngine_)};
    } else {
        particle.color = setting.startColor;
        particle.color.w = distAlpha(randomEngine_);
    }
    if (setting.isFaceDirection) {
        Vector3 initialUp = {0.0f, 1.0f, 0.0f};
        Vector3 forward = particle.velocity.Normalize();
        particle.fixedDirection = forward;
        Vector3 rotationAxis = initialUp.Cross(forward).Normalize();
        float dotProduct = initialUp.Dot(forward);
        float angle = acosf(std::clamp(dotProduct, -1.0f, 1.0f));
        particle.transform.eulerRotation_.x = rotationAxis.x * angle;
        particle.transform.eulerRotation_.y = rotationAxis.y * angle;
        particle.transform.eulerRotation_.z = rotationAxis.z * angle;
    }
    particle.initialAlpha = distAlpha(randomEngine_);
    particle.lifeTime = distLifeTime(randomEngine_);
    particle.currentTime = 0.0f;

    particle.blendMode = setting.blendMode;

    return particle;
}

std::list<Particle> ParticleManager::Emit() {
    std::list<Particle> allNewParticles;
    for (auto &[groupName, particleGroup] : particleGroups_) {
        std::list<Particle> newParticles;
        ParticleSetting &setting = particleSettings_[groupName];
        for (uint32_t nowCount = 0; nowCount < setting.count; ++nowCount) {
            Particle particle = MakeNewParticle(randomEngine_, setting);
            newParticles.push_back(particle);
        }
        particleGroup->GetParticleGroupData().particles.splice(
            particleGroup->GetParticleGroupData().particles.end(),
            newParticles);
        allNewParticles.splice(allNewParticles.end(), newParticles);
    }
    return allNewParticles;
}

bool ParticleManager::IsAllParticlesComplete() const {
    for (const auto &[groupName, particleGroup] : particleGroups_) {
        const auto &particles = particleGroup->GetParticleGroupData().particles;
        if (!particles.empty()) {
            return false;
        }
    }
    return true;
}

bool ParticleManager::IsParticleGroupComplete(const std::string &groupName) const {
    auto it = particleGroups_.find(groupName);
    if (it == particleGroups_.end()) {
        return true; // グループが存在しない場合はtrueを返す
    }

    const auto &particles = it->second->GetParticleGroupData().particles;
    return particles.empty();
}

size_t ParticleManager::GetActiveParticleCount() const {
    size_t totalCount = 0;
    for (const auto &[groupName, particleGroup] : particleGroups_) {
        const auto &particles = particleGroup->GetParticleGroupData().particles;
        totalCount += particles.size();
    }
    return totalCount;
}

size_t ParticleManager::GetActiveParticleCount(const std::string &groupName) const {
    auto it = particleGroups_.find(groupName);
    if (it == particleGroups_.end()) {
        return 0; // グループが存在しない場合は0を返す
    }
    const auto &particles = it->second->GetParticleGroupData().particles;
    return particles.size();
}
} // namespace Hagine
