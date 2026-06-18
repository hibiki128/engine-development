#define NOMINMAX
#include "ParticleCSEmitter.h"
#include "ParticleCSFieldManager.h"
#include "ParticleCSGroupManager.h"
#include <Graphics/PipeLine/ComputePipeLineManager.h>
#include <Frame.h>
#include <Line/DrawLine3D.h>
#include <Shadow/ShadowMap.h>
#include <Particle/ParticleCommon.h>
#include <random>
#include <regex>
#include"../Utility/Debug/ImGui/ImGuizmoManager.h"
#include"../Utility/Debug/ImGui/ImGuiNotification.h"

namespace Hagine {
ParticleCSEmitter::~ParticleCSEmitter() {
    // 保有していた独立グループを破棄せず再利用プールへ返却する。
    // これにより弾・ヒット等の高頻度スポーンでもバッファが累積しない。
    // 注意: group は Finalize 順序によっては既に破棄済みの場合がある。
    // ReleaseIndependentGroup はポインタ比較のみで deref しないため安全。
    for (ParticleCSGroup *group : particleGroups_) {
        if (group) {
            ParticleCSGroupManager::GetInstance()->ReleaseIndependentGroup(group);
        }
    }
    particleGroups_.clear();
    particleGroupNames_.clear();
}

void ParticleCSEmitter::Initialize(const std::string &name) {
    particleCommon_ = ParticleCommon::GetInstance();
    dxCommon_ = ParticleCommon::GetInstance()->GetDxCommon();
    commandList_ = dxCommon_->GetCommandList().Get();
    srvManager_ = SrvManager::GetInstance();
    name_ = name;
    CreateEmitterMeshResource();
    LoadSetting();
#ifdef _DEBUG
    if (emitterMeshData_) {
        ImGuizmoManager::GetInstance()->AddTarget(
            name_,
            &emitterMeshData_->translate,
            nullptr, // 必要に応じて回転のVector3を追加
            &emitterMeshData_->scale,
            isGizmoSelectable_);
    }
#endif
}

void ParticleCSEmitter::Initialize(const std::string &name, const std::string &modelPath) {
    Initialize(name);
    modelPath_ = modelPath;
    LoadModel(modelPath);
    CreateModelTriangles();
    CreateModelEdges();
}

void ParticleCSEmitter::Initialize(const std::string &name, PrimitiveType primitiveType) {
    Initialize(name);
    primitiveType_ = primitiveType;
    LoadPrimitiveModel(primitiveType);
    CreateModelTriangles();
    CreateModelEdges();
}

void ParticleCSEmitter::DrawCompute(const ViewProjection &vp) {
    if (ShadowMap::GetInstance()->IsShadowPassActive()) return;
    if (particleGroups_.empty()) return;

    auto *computeCmdList = dxCommon_->GetComputeCommandList().Get();
    dxCommon_->BeginComputeFrame();

    for (auto &group : particleGroups_) {
        group->Update(vp);

        auto *fieldMgr = ParticleCSFieldManager::GetInstance();
        auto fieldCountRes = receiveFields_ ? fieldMgr->GetFieldCountResource()
                                            : fieldMgr->GetZeroFieldCountResource();

        EmitterDisPatch(computeCmdList);

        D3D12_RESOURCE_BARRIER uavBarrier{};
        uavBarrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = group->GetOutputParticleResource().Get();
        computeCmdList->ResourceBarrier(1, &uavBarrier);

        // 生存コンパクションカウンタを 0 にリセット（Update の append より前）
        group->ResetAliveCounterDispatch(computeCmdList);

        group->UpdateParticleCSDisPatch(
            fieldMgr->GetFieldsSrvHandle(),
            fieldCountRes,
            fieldMgr->GetOverrideSrvHandle(),
            computeCmdList);

        // 生存数を readback バッファへコピー（compute キュー上で記録）
        group->RecordAliveCountReadback(computeCmdList);
    }
    // Execute は DrawSystem（または呼び出し元）が一括で行う
}

void ParticleCSEmitter::DrawGraphics(const ViewProjection &vp) {
    if (ShadowMap::GetInstance()->IsShadowPassActive()) return;
    if (particleGroups_.empty()) return;

    DrawEmitter();

    for (auto &group : particleGroups_) {
        // Phase 2: 旧 CountParticle 全Nディスパッチは廃止（生存数は aliveCounter に統合）

        // 生存数を読み戻し、描画 instanceCount を決定する。
        // VS 側で instanceId >= 実生存数 を確実にカリングするため、
        // ここでは生存数概算＋マージンで instance を発行する。
        group->FetchAliveDrawCount();
        const uint32_t maxCount = group->GetSettingsData()->maxParticleCount;
        uint32_t drawCount = group->GetAliveDrawCount();
        drawCount = drawCount + (drawCount / 4) + 256; // 急増分のマージン
        if (drawCount > maxCount) {
            drawCount = maxCount;
        }
        if (drawCount == 0) {
            continue; // 生存ゼロなら描画スキップ
        }

        particleCommon_->GPUDrawCommonSetting(group->GetParticleGroupData().blendMode);
        const auto &meshes = group->GetModelData().meshes;
        for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
            D3D12_INDEX_BUFFER_VIEW indexBufferView = group->GetIndexBufferView();
            D3D12_VERTEX_BUFFER_VIEW vertexBufferView = group->GetVertexBufferView();
            commandList_->IASetIndexBuffer(&indexBufferView);
            commandList_->IASetVertexBuffers(0, 1, &vertexBufferView);
            commandList_->SetGraphicsRootConstantBufferView(0, group->GetPerViewResource()->GetGPUVirtualAddress());
            srvManager_->SetGraphicsRootDescriptorTable(1, group->GetOutputParticleSrvForVSIndex());
            srvManager_->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetTextureIndexByFilePath(group->GetParticleGroupData().materials[meshIndex].textureFilePath));
            commandList_->SetGraphicsRootConstantBufferView(3, group->GetMaterialResource()->GetGPUVirtualAddress());
            // 生存コンパクション SRV (t2: aliveList, t3: aliveCount)
            srvManager_->SetGraphicsRootDescriptorTable(4, group->GetAliveListSrvForVSIndex());
            srvManager_->SetGraphicsRootDescriptorTable(5, group->GetAliveCounterSrvForVSIndex());
            commandList_->DrawIndexedInstanced(UINT(meshes[meshIndex].indices.size()), drawCount, 0, 0, 0);
        }
    }
}

void ParticleCSEmitter::Draw(const ViewProjection &vp) {
    // 後方互換: 単体で呼ばれる場合は Compute→Execute→Wait→Graphics を自前で完結させる
    DrawCompute(vp);
    dxCommon_->ExecuteComputeCommands();
    dxCommon_->WaitForComputeOnDirectQueue();
    DrawGraphics(vp);
}

void ParticleCSEmitter::DrawGraphicsForPreview(D3D12_GPU_VIRTUAL_ADDRESS perViewGpuAddress) {
    if (ShadowMap::GetInstance()->IsShadowPassActive()) return;
    if (particleGroups_.empty()) return;

    // ワイヤーフレーム(DrawEmitter)はプレビューでは描かない。
    for (auto &group : particleGroups_) {
        group->FetchAliveDrawCount();
        const uint32_t maxCount = group->GetSettingsData()->maxParticleCount;
        uint32_t drawCount = group->GetAliveDrawCount();
        drawCount = drawCount + (drawCount / 4) + 256; // 急増分のマージン
        if (drawCount > maxCount) {
            drawCount = maxCount;
        }
        if (drawCount == 0) {
            continue;
        }

        particleCommon_->GPUDrawCommonSetting(group->GetParticleGroupData().blendMode);
        const auto &meshes = group->GetModelData().meshes;
        for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
            D3D12_INDEX_BUFFER_VIEW indexBufferView = group->GetIndexBufferView();
            D3D12_VERTEX_BUFFER_VIEW vertexBufferView = group->GetVertexBufferView();
            commandList_->IASetIndexBuffer(&indexBufferView);
            commandList_->IASetVertexBuffers(0, 1, &vertexBufferView);
            // root param 0 のみプレビュー専用 per-view CB に差し替える（共有グループの VP は不変）
            commandList_->SetGraphicsRootConstantBufferView(0, perViewGpuAddress);
            srvManager_->SetGraphicsRootDescriptorTable(1, group->GetOutputParticleSrvForVSIndex());
            srvManager_->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetTextureIndexByFilePath(group->GetParticleGroupData().materials[meshIndex].textureFilePath));
            commandList_->SetGraphicsRootConstantBufferView(3, group->GetMaterialResource()->GetGPUVirtualAddress());
            srvManager_->SetGraphicsRootDescriptorTable(4, group->GetAliveListSrvForVSIndex());
            srvManager_->SetGraphicsRootDescriptorTable(5, group->GetAliveCounterSrvForVSIndex());
            commandList_->DrawIndexedInstanced(UINT(meshes[meshIndex].indices.size()), drawCount, 0, 0, 0);
        }
    }
}

void ParticleCSEmitter::LoadModel(const std::string &modelPath) {
    ModelManager::GetInstance()->LoadModel(modelPath);
    model_ = ModelManager::GetInstance()->FindModel(modelPath);
    if (model_) {
        modelData_ = model_->GetModelData();
    }
}

void ParticleCSEmitter::LoadPrimitiveModel(PrimitiveType type) {
    std::string modelKey = ModelManager::GetInstance()->CreatePrimitiveModel(type, "");
    model_ = ModelManager::GetInstance()->FindModel(modelKey);
    if (model_) {
        modelData_ = model_->GetModelData();
    }
}

void ParticleCSEmitter::Update() {
    if (isAuto_) {
        EmitterUpdate();
    } else if (emitOnce_) {
        emitterMeshData_->emit = 1;
        emitOnce_ = false;
    } else {
        emitterMeshData_->emit = 0;
    }
}

void ParticleCSEmitter::EmitOnce() {
    emitOnce_ = true;
}

void ParticleCSEmitter::DrawEmitter() {
    if (!isVisible_)
        return;
    Vector3 translate = emitterMeshData_->translate;
    Quaternion rotation = emitterMeshData_->rotation;
    Vector3 scale = emitterMeshData_->scale;

    Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
    Matrix4x4 rotateMatrix = MakeRotateXYZMatrix(rotation);
    Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);
    Matrix4x4 transformMatrix = MakeAffineMatrix(scale, rotation, translate);

    if (emitterMeshData_->emitFromSurface == 2 && !edgeInfoList_.empty()) {
        Vector4 color = {1.0f, 0.5f, 0.0f, 1.0f};
        for (const auto &edge : edgeInfoList_) {
            Vector3 v0 = Transformation(edge.v0, transformMatrix);
            Vector3 v1 = Transformation(edge.v1, transformMatrix);
            DrawLine3D::GetInstance()->SetPoints(v0, v1);
        }
    } else if (!triangleInfoList_.empty()) {
        Vector4 color = {0.0f, 1.0f, 0.0f, 1.0f};
        for (const auto &tri : triangleInfoList_) {
            Vector3 v0 = Transformation(tri.v0, transformMatrix);
            Vector3 v1 = Transformation(tri.v1, transformMatrix);
            Vector3 v2 = Transformation(tri.v2, transformMatrix);

            DrawLine3D::GetInstance()->SetPoints(v0, v1);
            DrawLine3D::GetInstance()->SetPoints(v1, v2);
            DrawLine3D::GetInstance()->SetPoints(v2, v0);
        }
    } else {
        Vector3 center = emitterMeshData_->translate;
        Vector4 color = {1.0f, 1.0f, 0.0f, 1.0f};
        float maxRadius = std::max(std::max(scale.x, scale.y), scale.z);
        DrawLine3D::GetInstance()->DrawSphere(center, color, maxRadius, 16);
    }
}

std::vector<ParticleCSEmitter::WireSegment> ParticleCSEmitter::GetWireframeSegments() const {
    std::vector<WireSegment> segs;
    if (!emitterMeshData_) {
        return segs;
    }
    Vector3 translate = emitterMeshData_->translate;
    Quaternion rotation = emitterMeshData_->rotation;
    Vector3 scale = emitterMeshData_->scale;
    Matrix4x4 transformMatrix = MakeAffineMatrix(scale, rotation, translate);

    if (emitterMeshData_->emitFromSurface == 2 && !edgeInfoList_.empty()) {
        const Vector4 color = {1.0f, 0.5f, 0.0f, 1.0f}; // 橙: エッジ
        for (const auto &edge : edgeInfoList_) {
            segs.push_back({Transformation(edge.v0, transformMatrix),
                            Transformation(edge.v1, transformMatrix), color});
        }
    } else if (!triangleInfoList_.empty()) {
        const Vector4 color = {0.0f, 1.0f, 0.0f, 1.0f}; // 緑: 三角形
        for (const auto &tri : triangleInfoList_) {
            Vector3 v0 = Transformation(tri.v0, transformMatrix);
            Vector3 v1 = Transformation(tri.v1, transformMatrix);
            Vector3 v2 = Transformation(tri.v2, transformMatrix);
            segs.push_back({v0, v1, color});
            segs.push_back({v1, v2, color});
            segs.push_back({v2, v0, color});
        }
    } else {
        // メッシュ無し: 半径を示す球ワイヤー（XY/XZ/YZ の3円）
        const Vector3 center = emitterMeshData_->translate;
        const Vector4 color = {1.0f, 1.0f, 0.0f, 1.0f}; // 黄: 球
        const float r = std::max(std::max(scale.x, scale.y), scale.z);
        const int N = 24;
        const float twoPi = 6.28318530718f;
        for (int i = 0; i < N; ++i) {
            float a0 = twoPi * static_cast<float>(i) / N;
            float a1 = twoPi * static_cast<float>(i + 1) / N;
            float c0 = std::cos(a0), s0 = std::sin(a0);
            float c1 = std::cos(a1), s1 = std::sin(a1);
            segs.push_back({{center.x + c0 * r, center.y + s0 * r, center.z},
                            {center.x + c1 * r, center.y + s1 * r, center.z}, color}); // XY
            segs.push_back({{center.x + c0 * r, center.y, center.z + s0 * r},
                            {center.x + c1 * r, center.y, center.z + s1 * r}, color}); // XZ
            segs.push_back({{center.x, center.y + c0 * r, center.z + s0 * r},
                            {center.x, center.y + c1 * r, center.z + s1 * r}, color}); // YZ
        }
    }
    return segs;
}

void ParticleCSEmitter::AddParticleGroup(ParticleCSGroup *group) {
    if (!group)
        return;
    const std::string &name = group->GetGroupName();
    ParticleCSGroup *independentGroup = ParticleCSGroupManager::GetInstance()->GetIndependentParticleGroup(name);
    if (!independentGroup) {
        return;
    }
    independentGroup->SetSettingData(*group->GetSettingsData());
    independentGroup->SetBlendMode(group->GetParticleGroupData().blendMode);
    independentGroup->SetBillboard(group->GetPerView()->enableBillboard);
    particleGroups_.push_back(independentGroup);
    particleGroupNames_.insert(name);
    ImGuiNotification::Post("パーティクルグループを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void ParticleCSEmitter::RemoveParticleGroup(const std::string &groupName) {
    auto it = std::remove_if(particleGroups_.begin(), particleGroups_.end(),
                             [&](ParticleCSGroup *group) {
                                 return group->GetGroupName() == groupName;
                             });
    if (it != particleGroups_.end()) {
        particleGroups_.erase(it, particleGroups_.end());
    }
    particleGroupNames_.erase(groupName);
    ImGuiNotification::Post("パーティクルグループを削除しました: " + groupName, {0.9f, 0.7f, 0.2f, 1.0f});
}

void ParticleCSEmitter::EmitterUpdate() {
    emitterMeshData_->frequencyTime += Frame::DeltaTime();
    if (emitterMeshData_->frequency <= emitterMeshData_->frequencyTime) {
        emitterMeshData_->frequencyTime -= emitterMeshData_->frequency;
        emitterMeshData_->emit = 1;
    } else {
        emitterMeshData_->emit = 0;
    }
}
void ParticleCSEmitter::CreateEmitterMeshResource() {
    emitterMeshResource_ = dxCommon_->CreateBufferResource(sizeof(EmitterMesh));
    emitterMeshResource_->Map(0, nullptr, reinterpret_cast<void **>(&emitterMeshData_));
    emitterMeshData_->frequency = 0.5f;
    emitterMeshData_->frequencyTime = 0.0f;
    emitterMeshData_->translate = Vector3(0.0f, 0.0f, 0.0f);
    emitterMeshData_->rotation = Quaternion::IdentityQuaternion();
    emitterMeshData_->scale = Vector3(1.0f, 1.0f, 1.0f);
    emitterMeshData_->triangleCount = 0;
    emitterMeshData_->emit = 0;
    emitterMeshData_->emitFromSurface = 1;
    emitterMeshData_->edgeCount = 0;
    emitterMeshData_->anchorPoint = Vector3(0.5f, 0.5f, 0.5f);
}

void ParticleCSEmitter::EmitterDisPatch(ID3D12GraphicsCommandList *cmdList) {
    // cmdList が渡された場合はそちら（Compute Queue）を使う
    ID3D12GraphicsCommandList *cl = cmdList ? cmdList : commandList_;
    ComputePipeLineManager::GetInstance()->DrawCommonSetting(
        ComputePipelineType::kEmitter,
        BlendMode::kNormal, ShaderMode::kNone, cl);

    uint32_t groupIndex = 0;
    for (auto &group : particleGroups_) {
        group->GetPerFrameData()->groupId = groupIndex;
        group->GetPerFrameData()->emitterFieldGroupId = fieldGroupId_;

        ParticleCSSettings *settings = group->GetSettingsData();

        settings->gatherTarget = emitterMeshData_->translate + settings->gatherTargetOffset;
        settings->vortexTarget = emitterMeshData_->translate + settings->vortexTargetOffset;

        cl->SetComputeRootDescriptorTable(0, group->GetOutputParticleSrvHandle().second);
        cl->SetComputeRootDescriptorTable(1, group->GetFreeListIndexSrvHandle().second);
        cl->SetComputeRootDescriptorTable(2, group->GetFreeListSrvHandle().second);
        cl->SetComputeRootDescriptorTable(3, group->GetFreeListTrailIndexSrvHandle().second);

        cl->SetComputeRootConstantBufferView(4, emitterMeshResource_->GetGPUVirtualAddress());
        cl->SetComputeRootConstantBufferView(5, group->GetPerFrameResource()->GetGPUVirtualAddress());
        cl->SetComputeRootConstantBufferView(6, group->GetSettingsResource()->GetGPUVirtualAddress());

        if (emitterMeshData_->triangleCount > 0 && triangleInfoResource_ && triangleCDFResource_) {
            cl->SetComputeRootDescriptorTable(8, triangleInfoSrvHandle_.second);
            cl->SetComputeRootDescriptorTable(9, triangleCDFSrvHandle_.second);
        }

        if (emitterMeshData_->edgeCount > 0 && edgeInfoResource_) {
            cl->SetComputeRootDescriptorTable(10, edgeInfoSrvHandle_.second);
        }

        {
            auto *fieldManager = ParticleCSFieldManager::GetInstance();
            cl->SetComputeRootDescriptorTable(11, fieldManager->GetFieldsSrvHandle().second);

            if (emitOnlyOnFieldContact_ && receiveFields_) {
                bool hasEmitSpawnField = false;
                for (const auto &field : fieldManager->GetFields()) {
                    if (!field.enabled || !field.data.enableEmitSpawn)
                        continue;
                    bool groupMatch = (field.data.groupId == -1) ||
                                      (fieldGroupId_ == -1) ||
                                      (field.data.groupId == fieldGroupId_);
                    if (groupMatch) {
                        hasEmitSpawnField = true;
                        break;
                    }
                }

                if (!hasEmitSpawnField) {
                    cl->SetComputeRootConstantBufferView(7, fieldManager->GetZeroFieldCountResource()->GetGPUVirtualAddress());
                    groupIndex++;
                    continue;
                }

                uint32_t savedEmitCount = settings->emitCount;
                float savedLifeMin = settings->lifeTimeMin;
                float savedLifeMax = settings->lifeTimeMax;

                settings->emitCount = fieldContactEmitCount_;

                for (const auto &field : fieldManager->GetFields()) {
                    if (!field.enabled || !field.data.enableEmitSpawn)
                        continue;
                    bool groupMatch = (field.data.groupId == -1) ||
                                      (fieldGroupId_ == -1) ||
                                      (field.data.groupId == fieldGroupId_);
                    if (!groupMatch)
                        continue;
                    if (field.data.emitSpawnLifeTimeMax > 0.0f) {
                        settings->lifeTimeMin = field.data.emitSpawnLifeTimeMin;
                        settings->lifeTimeMax = field.data.emitSpawnLifeTimeMax;
                    }
                    break;
                }

                cl->SetComputeRootConstantBufferView(7, fieldManager->GetFieldCountResource()->GetGPUVirtualAddress());

                int dispatchCount = (settings->emitCount + threadGroupSize_ - 1) / threadGroupSize_;
                cl->Dispatch(dispatchCount, 1, 1);

                settings->emitCount = savedEmitCount;
                settings->lifeTimeMin = savedLifeMin;
                settings->lifeTimeMax = savedLifeMax;

                groupIndex++;
                continue;

            } else {
                cl->SetComputeRootConstantBufferView(7, fieldManager->GetZeroFieldCountResource()->GetGPUVirtualAddress());
            }
        }

        if (emitterMeshData_->emit == 0 || settings->emitCount == 0) {
            groupIndex++;
            continue;
        }

        int dispatchCount = (group->GetSettingsData()->emitCount + threadGroupSize_ - 1) / threadGroupSize_;
        cl->Dispatch(dispatchCount, 1, 1);

        groupIndex++;
    }
}

std::unique_ptr<ParticleCSEmitter> ParticleCSEmitter::Clone() const {
    auto newEmitter = std::make_unique<ParticleCSEmitter>();

    auto &nameCounter = GetNameCounter();
    std::string baseName = name_;
    std::regex suffixRegex("_(\\d+)$");
    baseName = std::regex_replace(baseName, suffixRegex, "");

    int &counter = nameCounter[baseName];
    ++counter;

    std::string newName = baseName + "_" + std::to_string(counter);

    newEmitter->Initialize(baseName);
    newEmitter->SetName(newName);
#ifdef _DEBUG
    ImGuizmoManager::GetInstance()->RemoveTarget(baseName);
    if (newEmitter->emitterMeshData_) {
        ImGuizmoManager::GetInstance()->AddTarget(
            newName,
            &newEmitter->emitterMeshData_->translate,
            nullptr,
            &newEmitter->emitterMeshData_->scale,
            newEmitter->isGizmoSelectable_);
    }
#endif
    newEmitter->LoadCloneSetting();
    newEmitter->SetActive(this->isActive_);
    newEmitter->isAuto_ = this->isAuto_;
    newEmitter->isVisible_ = this->isVisible_;

    *newEmitter->emitterMeshData_ = *this->emitterMeshData_;

    return newEmitter;
}
void ParticleCSEmitter::CreateModelTriangles() {
    if (modelData_.meshes.empty())
        return;

    triangleInfoList_.clear();
    triangleCDF_.clear();
    std::vector<float> triangleAreas;

    for (const auto &mesh : modelData_.meshes) {
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            uint32_t i0 = mesh.indices[i];
            uint32_t i1 = mesh.indices[i + 1];
            uint32_t i2 = mesh.indices[i + 2];

            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
                continue;

            Vector3 v0(mesh.vertices[i0].position.x, mesh.vertices[i0].position.y, mesh.vertices[i0].position.z);
            Vector3 v1(mesh.vertices[i1].position.x, mesh.vertices[i1].position.y, mesh.vertices[i1].position.z);
            Vector3 v2(mesh.vertices[i2].position.x, mesh.vertices[i2].position.y, mesh.vertices[i2].position.z);

            Vector3 edge1 = v1 - v0;
            Vector3 edge2 = v2 - v0;
            Vector3 crossProd = edge1.Cross(edge2);
            float area = crossProd.Length() * 0.5f;

            if (area > 1e-6f) {
                triangleAreas.push_back(area);

                TriangleInfo triInfo;
                triInfo.v0 = v0;
                triInfo.v1 = v1;
                triInfo.v2 = v2;
                triInfo.padding0 = 0.0f;
                triInfo.padding1 = 0.0f;
                triInfo.padding2 = 0.0f;

                triangleInfoList_.push_back(triInfo);
            }
        }
    }

    if (triangleInfoList_.empty())
        return;

    std::vector<size_t> indices(triangleInfoList_.size());
    for (size_t i = 0; i < indices.size(); i++) {
        indices[i] = i;
    }

    float totalArea = 0.0f;
    for (float area : triangleAreas) {
        totalArea += area;
    }

    triangleCDF_.resize(triangleAreas.size());
    float accum = 0.0f;
    for (size_t i = 0; i < triangleAreas.size(); i++) {
        accum += triangleAreas[i] / totalArea;
        triangleCDF_[i] = accum;
    }

    // 最後の値を強制的に1.0にして誤差を修正
    if (!triangleCDF_.empty()) {
        triangleCDF_.back() = 1.0f;
    }

    int histogram[10] = {0};
    for (float cdf : triangleCDF_) {
        int bucket = static_cast<int>(cdf * 10.0f);
        if (bucket >= 10)
            bucket = 9;
        histogram[bucket]++;
    }

    size_t triangleInfoBufferSize = sizeof(TriangleInfo) * triangleInfoList_.size();
    triangleInfoResource_ = dxCommon_->CreateBufferResource(triangleInfoBufferSize);
    triangleInfoResource_->Map(0, nullptr, reinterpret_cast<void **>(&triangleInfoData_));
    std::memcpy(triangleInfoData_, triangleInfoList_.data(), triangleInfoBufferSize);

    triangleInfoSrvIndex_ = srvManager_->Allocate() + 1;
    triangleInfoSrvHandle_.first = srvManager_->GetCPUDescriptorHandle(triangleInfoSrvIndex_);
    triangleInfoSrvHandle_.second = srvManager_->GetGPUDescriptorHandle(triangleInfoSrvIndex_);
    srvManager_->CreateSRVforStructuredBuffer(triangleInfoSrvIndex_, triangleInfoResource_.Get(),
                                              static_cast<uint32_t>(triangleInfoList_.size()), sizeof(TriangleInfo));

    size_t cdfBufferSize = sizeof(float) * triangleCDF_.size();
    triangleCDFResource_ = dxCommon_->CreateBufferResource(cdfBufferSize);
    triangleCDFResource_->Map(0, nullptr, reinterpret_cast<void **>(&triangleCDFData_));
    std::memcpy(triangleCDFData_, triangleCDF_.data(), cdfBufferSize);

    triangleCDFSrvIndex_ = srvManager_->Allocate() + 1;
    triangleCDFSrvHandle_.first = srvManager_->GetCPUDescriptorHandle(triangleCDFSrvIndex_);
    triangleCDFSrvHandle_.second = srvManager_->GetGPUDescriptorHandle(triangleCDFSrvIndex_);
    srvManager_->CreateSRVforStructuredBuffer(triangleCDFSrvIndex_, triangleCDFResource_.Get(),
                                              static_cast<uint32_t>(triangleCDF_.size()), sizeof(float));

    emitterMeshData_->triangleCount = static_cast<uint32_t>(triangleInfoList_.size());
}

void ParticleCSEmitter::CreateModelEdges() {
    if (modelData_.meshes.empty())
        return;

    edgeInfoList_.clear();

    std::map<std::pair<uint32_t, uint32_t>, int> edgeMap;

    for (const auto &mesh : modelData_.meshes) {
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            uint32_t i0 = mesh.indices[i];
            uint32_t i1 = mesh.indices[i + 1];
            uint32_t i2 = mesh.indices[i + 2];

            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
                continue;

            std::array<std::pair<uint32_t, uint32_t>, 3> edges = {{{std::min(i0, i1), std::max(i0, i1)},
                                                                   {std::min(i1, i2), std::max(i1, i2)},
                                                                   {std::min(i2, i0), std::max(i2, i0)}}};

            for (const auto &edge : edges) {
                edgeMap[edge]++;
            }
        }
    }

    bool isClosedMesh = true;
    for (const auto &[edge, count] : edgeMap) {
        if (count == 1) {
            isClosedMesh = false;
            break;
        }
    }

    for (const auto &[edge, count] : edgeMap) {
        if (!isClosedMesh && count != 1)
            continue;

        uint32_t idx0 = edge.first;
        uint32_t idx1 = edge.second;

        Vector3 v0, v1;
        bool found = false;
        for (const auto &mesh : modelData_.meshes) {
            if (idx0 < mesh.vertices.size() && idx1 < mesh.vertices.size()) {
                v0 = Vector3(mesh.vertices[idx0].position.x,
                             mesh.vertices[idx0].position.y,
                             mesh.vertices[idx0].position.z);
                v1 = Vector3(mesh.vertices[idx1].position.x,
                             mesh.vertices[idx1].position.y,
                             mesh.vertices[idx1].position.z);
                found = true;
                break;
            }
        }

        if (found) {
            EdgeInfo edgeInfo;
            edgeInfo.v0 = v0;
            edgeInfo.v1 = v1;
            edgeInfo.padding0 = 0.0f;
            edgeInfo.padding1 = 0.0f;
            edgeInfoList_.push_back(edgeInfo);
        }
    }

    if (edgeInfoList_.empty())
        return;

    size_t edgeInfoBufferSize = sizeof(EdgeInfo) * edgeInfoList_.size();
    edgeInfoResource_ = dxCommon_->CreateBufferResource(edgeInfoBufferSize);
    edgeInfoResource_->Map(0, nullptr, reinterpret_cast<void **>(&edgeInfoData_));
    std::memcpy(edgeInfoData_, edgeInfoList_.data(), edgeInfoBufferSize);

    edgeInfoSrvIndex_ = srvManager_->Allocate() + 1;
    edgeInfoSrvHandle_.first = srvManager_->GetCPUDescriptorHandle(edgeInfoSrvIndex_);
    edgeInfoSrvHandle_.second = srvManager_->GetGPUDescriptorHandle(edgeInfoSrvIndex_);
    srvManager_->CreateSRVforStructuredBuffer(edgeInfoSrvIndex_, edgeInfoResource_.Get(),
                                              static_cast<uint32_t>(edgeInfoList_.size()), sizeof(EdgeInfo));

    emitterMeshData_->edgeCount = static_cast<uint32_t>(edgeInfoList_.size());
}

size_t ParticleCSEmitter::GetTotalAliveParticles() {
    size_t total = 0;
    for (auto &group : particleGroups_) {
        total += group->GetAliveParticleCount();
    }
    return total;
}

std::vector<ParticleCSEmitter::GroupStatistics> ParticleCSEmitter::GetGroupStatistics() {
    std::vector<GroupStatistics> stats;

    for (auto &group : particleGroups_) {
        GroupStatistics stat;
        stat.groupName = group->GetGroupName();
        stat.aliveCount = group->GetAliveParticleCount();
        stats.push_back(stat);
    }

    return stats;
}

void ParticleCSEmitter::SaveSetting() {
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ParticleCS", name_);

    data->Save("isAuto", isAuto_);
    data->Save("isVisible", isVisible_);
    data->Save("isGizmoSelectable", isGizmoSelectable_);
    data->Save("drawGroup", drawGroup_);
    data->Save("frequency", emitterMeshData_->frequency);
    data->Save("frequencyTime", emitterMeshData_->frequencyTime);
    data->Save<Vector3>("translate", emitterMeshData_->translate);
    data->Save<Quaternion>("rotation", emitterMeshData_->rotation);
    data->Save<Vector3>("scale", emitterMeshData_->scale);
    data->Save("emitFromSurface", emitterMeshData_->emitFromSurface);
    data->Save("modelPath", modelPath_);
    data->Save("primitiveType", static_cast<int>(primitiveType_));

    // フィールド影響設定
    data->Save("receiveFields", receiveFields_);
    data->Save("fieldGroupId", fieldGroupId_);
    data->Save("emitOnlyOnFieldContact", emitOnlyOnFieldContact_);
    data->Save("fieldContactEmitCount", static_cast<int>(fieldContactEmitCount_));

    data->Save("particleGroupCount", static_cast<int>(particleGroups_.size()));

    for (int i = 0; i < particleGroups_.size(); i++) {
        auto &group = particleGroups_[i];
        std::string prefix = "group_" + std::to_string(i) + "_";

        data->Save(prefix + "name", group->GetGroupName());
        data->Save(prefix + "minLifetime", group->GetSettingsData()->lifeTimeMin);
        data->Save(prefix + "maxLifetime", group->GetSettingsData()->lifeTimeMax);
        data->Save(prefix + "minScale", group->GetSettingsData()->scaleMin);
        data->Save(prefix + "maxScale", group->GetSettingsData()->scaleMax);
        data->Save(prefix + "minVelocity", group->GetSettingsData()->velocityMin);
        data->Save(prefix + "maxVelocity", group->GetSettingsData()->velocityMax);
        data->Save(prefix + "startColor", group->GetSettingsData()->startColor);
        data->Save(prefix + "endColor", group->GetSettingsData()->endColor);
        data->Save(prefix + "enableLifetimeScale", group->GetSettingsData()->enableLifetimeScale);
        data->Save(prefix + "enableRandomColor", group->GetSettingsData()->enableRandomColor);
        data->Save(prefix + "enableSinScale", group->GetSettingsData()->enableSinScale);
        data->Save(prefix + "sinScaleFrequency", group->GetSettingsData()->sinScaleFrequency);
        data->Save(prefix + "sinScaleAmplitude", group->GetSettingsData()->sinScaleAmplitude);
        data->Save(prefix + "emitCount", group->GetSettingsData()->emitCount);
        data->Save(prefix + "enableGravity", group->GetSettingsData()->enableGravity);
        data->Save(prefix + "gravity", group->GetSettingsData()->gravity);
        data->Save(prefix + "blendMode", static_cast<int>(group->GetParticleGroupData().blendMode));

        // ★ トレイル設定の保存
        data->Save(prefix + "enableTrail", group->GetSettingsData()->enableTrail);
        data->Save(prefix + "trailSpawnDistance", group->GetSettingsData()->trailSpawnDistance);
        data->Save(prefix + "maxTrailPerParticle", group->GetSettingsData()->maxTrailPerParticle);
        data->Save(prefix + "trailLifeTimeScale", group->GetSettingsData()->trailLifeTimeScale);
        data->Save(prefix + "trailScaleMultiplier", group->GetSettingsData()->trailScaleMultiplier);
        data->Save(prefix + "trailColorMultiplier", group->GetSettingsData()->trailColorMultiplier);
        data->Save(prefix + "trailVelocityScale", group->GetSettingsData()->trailVelocityScale);
        data->Save(prefix + "trailInheritVelocity", group->GetSettingsData()->trailInheritVelocity);
        data->Save(prefix + "trailMinLifeTime", group->GetSettingsData()->trailMinLifeTime);

        data->Save(prefix + "enableGather", group->GetSettingsData()->enableGather);
        data->Save(prefix + "gatherStartRatio", group->GetSettingsData()->gatherStartRatio);
        data->Save(prefix + "gatherStrength", group->GetSettingsData()->gatherStrength);
        data->Save(prefix + "gatherTarget", group->GetSettingsData()->gatherTarget);
        data->Save(prefix + "gatherTargetOffset", group->GetSettingsData()->gatherTargetOffset);
        data->Save(prefix + "enableGatherForTrail", group->GetSettingsData()->enableGatherForTrail);
        data->Save(prefix + "enableVortex", group->GetSettingsData()->enableVortex);
        data->Save(prefix + "vortexTarget", group->GetSettingsData()->vortexTarget);
        data->Save(prefix + "vortexTargetOffset", group->GetSettingsData()->vortexTargetOffset);
        data->Save(prefix + "vortexStrength", group->GetSettingsData()->vortexStrength);
        data->Save(prefix + "enableVortexForTrail", group->GetSettingsData()->enableVortexForTrail);
        data->Save(prefix + "vortexAxis", group->GetSettingsData()->vortexAxis);

        data->Save(prefix + "enableAcceleration", group->GetSettingsData()->enableAcceleration);
        data->Save(prefix + "acceleration", group->GetSettingsData()->acceleration);
        data->Save(prefix + "enableVelocityDamping", group->GetSettingsData()->enableVelocityDamping);
        data->Save(prefix + "velocityDampingFactor", group->GetSettingsData()->velocityDampingFactor);
        data->Save(prefix + "enableLifetimeVelocityDamping", group->GetSettingsData()->enableLifetimeVelocityDamping);
        data->Save(prefix + "lifetimeVelocityDampingStart", group->GetSettingsData()->lifetimeVelocityDampingStart);
        data->Save(prefix + "enableRadialVelocity", group->GetSettingsData()->enableRadialVelocity);
        data->Save(prefix + "radialVelocityStrength", group->GetSettingsData()->radialVelocityStrength);
        data->Save(prefix + "radialVelocityRandomness", group->GetSettingsData()->radialVelocityRandomness);
        data->Save(prefix + "radialVelocityCenter", group->GetSettingsData()->radialVelocityCenter);

        data->Save(prefix + "enableCurlNoise", group->GetSettingsData()->enableCurlNoise);
        data->Save(prefix + "curlNoiseScale", group->GetSettingsData()->curlNoiseScale);
        data->Save(prefix + "curlNoiseStrength", group->GetSettingsData()->curlNoiseStrength);
        data->Save(prefix + "curlNoiseTimeScale", group->GetSettingsData()->curlNoiseTimeScale);
        data->Save(prefix + "curlNoiseOctaves", group->GetSettingsData()->curlNoiseOctaves);
        data->Save(prefix + "curlNoiseAttractStrength", group->GetSettingsData()->curlNoiseAttractStrength);
        data->Save(prefix + "curlNoiseBlendMode", group->GetSettingsData()->curlNoiseBlendMode);
        data->Save(prefix + "curlNoisePosRandomStrength", group->GetSettingsData()->curlNoisePosRandomStrength);
        data->Save(prefix + "curlNoiseAttractCenter", group->GetSettingsData()->curlNoiseAttractCenter);

        // ★ 終了スケール設定の保存
        data->Save(prefix + "enableEndScale", group->GetSettingsData()->enableEndScale);
        data->Save(prefix + "endScaleValue", group->GetSettingsData()->endScaleValue);

        // ★ 回転設定の保存
        data->Save(prefix + "enableRandomRotation", group->GetSettingsData()->enableRandomRotation);
        data->Save<Vector3>(prefix + "rotationMin", group->GetSettingsData()->rotationMin);
        data->Save<Vector3>(prefix + "rotationMax", group->GetSettingsData()->rotationMax);
        data->Save(prefix + "enableRandomAngularVelocity", group->GetSettingsData()->enableRandomAngularVelocity);
        data->Save<Vector3>(prefix + "angularVelocityMin", group->GetSettingsData()->angularVelocityMin);
        data->Save<Vector3>(prefix + "angularVelocityMax", group->GetSettingsData()->angularVelocityMax);

        data->Save(prefix + "enableBillboard", group->GetPerView()->enableBillboard);

        // ★ 速度ストレッチ設定の保存
        data->Save(prefix + "enableVelocityStretch", group->GetPerView()->enableVelocityStretch);
        data->Save(prefix + "velocityStretchFactor", group->GetPerView()->velocityStretchFactor);

        // ★ 中間カラー設定の保存
        data->Save(prefix + "enableMidColor", group->GetSettingsData()->enableMidColor);
        data->Save(prefix + "midColorRatio", group->GetSettingsData()->midColorRatio);
        data->Save(prefix + "midColor", group->GetSettingsData()->midColor);

        // ★ タービュランス設定の保存
        data->Save(prefix + "enableTurbulence", group->GetSettingsData()->enableTurbulence);
        data->Save(prefix + "turbulenceStrength", group->GetSettingsData()->turbulenceStrength);
        data->Save(prefix + "turbulenceFrequency", group->GetSettingsData()->turbulenceFrequency);

        // ★ 発生形状設定の保存
        data->Save(prefix + "emitShape", group->GetSettingsData()->emitShape);
        data->Save(prefix + "emitSphereRadius", group->GetSettingsData()->emitSphereRadius);
        data->Save(prefix + "emitConeAngle", group->GetSettingsData()->emitConeAngle);
    }
    ImGuiNotification::Post("パーティクル設定を保存しました: " + name_, {0.2f, 0.8f, 0.2f, 1.0f});
}

void ParticleCSEmitter::LoadSetting() {
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ParticleCS", name_);

    isAuto_ = data->Load("isAuto", false);
    isVisible_ = data->Load("isVisible", true);
    isGizmoSelectable_ = data->Load("isGizmoSelectable", true);
    drawGroup_ = data->Load<std::string>("drawGroup", "3D");
    if (drawGroup_ != "UI") {
        drawGroup_ = "3D"; // 旧データは3D扱いに正規化
    }
    emitterMeshData_->frequency = data->Load("frequency", 0.1f);
    emitterMeshData_->frequencyTime = data->Load("frequencyTime", 0.0f);
    emitterMeshData_->translate = data->Load<Vector3>("translate", Vector3(0.0f, 0.0f, 0.0f));
    emitterMeshData_->rotation = data->Load<Quaternion>("rotation", Quaternion::IdentityQuaternion());
    emitterMeshData_->scale = data->Load<Vector3>("scale", Vector3(1.0f, 1.0f, 1.0f));
    emitterMeshData_->emitFromSurface = data->Load<uint32_t>("emitFromSurface", 1);

    modelPath_ = data->Load("modelPath", std::string(""));
    primitiveType_ = static_cast<PrimitiveType>(data->Load("primitiveType", static_cast<int>(PrimitiveType::None)));
    // フィールド影響設定
    receiveFields_ = data->Load("receiveFields", false);
    fieldGroupId_ = data->Load("fieldGroupId", -1);
    emitOnlyOnFieldContact_ = data->Load("emitOnlyOnFieldContact", false);
    fieldContactEmitCount_ = static_cast<uint32_t>(data->Load("fieldContactEmitCount", 1000));

    if (!modelPath_.empty()) {
        LoadModel(modelPath_);
        CreateModelTriangles();
    } else if (primitiveType_ != PrimitiveType::None) {
        LoadPrimitiveModel(primitiveType_);
        CreateModelTriangles();
        CreateModelEdges();
    }

    groupNum_ = data->Load("particleGroupCount", 0);
    for (int i = 0; i < groupNum_; i++) {
        std::string prefix = "group_" + std::to_string(i) + "_";
        std::string groupName = data->Load(prefix + "name", std::string(""));

        auto group = ParticleCSGroupManager::GetInstance()->GetIndependentParticleGroup(groupName);
        if (!group)
            continue;

        ParticleCSSettings settings;
        settings.lifeTimeMin = data->Load(prefix + "minLifetime", 1.0f);
        settings.lifeTimeMax = data->Load(prefix + "maxLifetime", 1.0f);
        settings.scaleMin = data->Load(prefix + "minScale", 1.0f);
        settings.scaleMax = data->Load(prefix + "maxScale", 1.0f);
        settings.velocityMin = data->Load<Vector3>(prefix + "minVelocity", {0.0f, 0.0f, 0.0f});
        settings.velocityMax = data->Load<Vector3>(prefix + "maxVelocity", {0.0f, 0.0f, 0.0f});
        settings.startColor = data->Load(prefix + "startColor", Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        settings.endColor = data->Load(prefix + "endColor", Vector4(1.0f, 1.0f, 1.0f, 0.0f));
        settings.enableLifetimeScale = data->Load<uint32_t>(prefix + "enableLifetimeScale", 0);
        settings.enableRandomColor = data->Load<uint32_t>(prefix + "enableRandomColor", 0);
        settings.enableSinScale = data->Load<uint32_t>(prefix + "enableSinScale", 0);
        settings.sinScaleFrequency = data->Load(prefix + "sinScaleFrequency", 5.0f);
        settings.sinScaleAmplitude = data->Load(prefix + "sinScaleAmplitude", 0.3f);
        settings.emitCount = data->Load<uint32_t>(prefix + "emitCount", 10);
        settings.enableGravity = data->Load<uint32_t>(prefix + "enableGravity", false);
        settings.gravity = data->Load<Vector3>(prefix + "gravity", {0.0f, 0.0f, 0.0f});
        settings.maxParticleCount = group->GetMaxParticleCount();

        // ★ トレイル設定のロード
        settings.enableTrail = data->Load<uint32_t>(prefix + "enableTrail", 0);
        settings.trailSpawnDistance = data->Load(prefix + "trailSpawnDistance", 0.1f);
        settings.maxTrailPerParticle = data->Load<uint32_t>(prefix + "maxTrailPerParticle", 5);
        settings.trailLifeTimeScale = data->Load(prefix + "trailLifeTimeScale", 1.0f);
        settings.trailScaleMultiplier = data->Load<Vector3>(prefix + "trailScaleMultiplier", {0.8f, 0.8f, 0.8f});
        settings.trailColorMultiplier = data->Load(prefix + "trailColorMultiplier", Vector4(1.0f, 1.0f, 1.0f, 0.7f));
        settings.trailVelocityScale = data->Load(prefix + "trailVelocityScale", 0.3f);
        settings.trailInheritVelocity = data->Load<uint32_t>(prefix + "trailInheritVelocity", 1);
        settings.trailMinLifeTime = data->Load(prefix + "trailMinLifeTime", 0.5f);

        settings.enableGather = data->Load(prefix + "enableGather", 0);
        settings.gatherStartRatio = data->Load(prefix + "gatherStartRatio", 0.5f);
        settings.gatherStrength = data->Load(prefix + "gatherStrength", 1.0f);
        settings.gatherTarget = data->Load<Vector3>(prefix + "gatherTarget", {0.0f, 0.0f, 0.0f});
        settings.gatherTargetOffset = data->Load<Vector3>(prefix + "gatherTargetOffset", {0.0f, 0.0f, 0.0f});
        settings.enableGatherForTrail = data->Load<uint32_t>(prefix + "enableGatherForTrail", 0);
        settings.enableVortex = data->Load<uint32_t>(prefix + "enableVortex", 0);
        settings.vortexTarget = data->Load<Vector3>(prefix + "vortexTarget", {0.0f, 0.0f, 0.0f});
        settings.vortexTargetOffset = data->Load<Vector3>(prefix + "vortexTargetOffset", {0.0f, 0.0f, 0.0f});
        settings.vortexStrength = data->Load(prefix + "vortexStrength", 1.0f);
        settings.enableVortexForTrail = data->Load<uint32_t>(prefix + "enableVortexForTrail", 0);
        settings.vortexAxis = data->Load<Vector3>(prefix + "vortexAxis", {0.0f, 1.0f, 0.0f});

        settings.enableAcceleration = data->Load<uint32_t>(prefix + "enableAcceleration", 0);
        settings.acceleration = data->Load<Vector3>(prefix + "acceleration", {0.0f, 0.0f, 0.0f});
        settings.enableVelocityDamping = data->Load<uint32_t>(prefix + "enableVelocityDamping", 0);
        settings.velocityDampingFactor = data->Load(prefix + "velocityDampingFactor", 0.0f);
        settings.enableLifetimeVelocityDamping = data->Load<uint32_t>(prefix + "enableLifetimeVelocityDamping", 0);
        settings.lifetimeVelocityDampingStart = data->Load(prefix + "lifetimeVelocityDampingStart", 0.0f);
        settings.enableRadialVelocity = data->Load<uint32_t>(prefix + "enableRadialVelocity", 0);
        settings.radialVelocityStrength = data->Load(prefix + "radialVelocityStrength", 0.0f);
        settings.radialVelocityRandomness = data->Load(prefix + "radialVelocityRandomness", 0.0f);
        settings.radialVelocityCenter = data->Load<Vector3>(prefix + "radialVelocityCenter", {0.0f, 0.0f, 0.0f});

        settings.enableCurlNoise = data->Load<uint32_t>(prefix + "enableCurlNoise", 0);
        settings.curlNoiseScale = data->Load(prefix + "curlNoiseScale", 1.0f);
        settings.curlNoiseStrength = data->Load(prefix + "curlNoiseStrength", 1.0f);
        settings.curlNoiseTimeScale = data->Load(prefix + "curlNoiseTimeScale", 1.0f);
        settings.curlNoiseOctaves = data->Load<uint32_t>(prefix + "curlNoiseOctaves", 1);
        settings.curlNoiseAttractStrength = data->Load(prefix + "curlNoiseAttractStrength", 0.0f);
        settings.curlNoiseBlendMode = data->Load<uint32_t>(prefix + "curlNoiseBlendMode", 0);
        settings.curlNoisePosRandomStrength = data->Load(prefix + "curlNoisePosRandomStrength", 0.0f);
        settings.curlNoiseAttractCenter = data->Load<Vector3>(prefix + "curlNoiseAttractCenter", {0.0f, 0.0f, 0.0f});

        // ★ 終了スケール設定のロード
        settings.enableEndScale = data->Load<uint32_t>(prefix + "enableEndScale", 0);
        settings.endScaleValue = data->Load<Vector3>(prefix + "endScaleValue", {0.0f, 0.0f, 0.0f});

        // ★ 回転設定のロード
        settings.enableRandomRotation = data->Load<uint32_t>(prefix + "enableRandomRotation", 0);
        settings.rotationMin = data->Load<Vector3>(prefix + "rotationMin", {0.0f, 0.0f, 0.0f});
        settings.rotationMax = data->Load<Vector3>(prefix + "rotationMax", {0.0f, 0.0f, 0.0f});
        settings.enableRandomAngularVelocity = data->Load<uint32_t>(prefix + "enableRandomAngularVelocity", 0);
        settings.angularVelocityMin = data->Load<Vector3>(prefix + "angularVelocityMin", {0.0f, 0.0f, 0.0f});
        settings.angularVelocityMax = data->Load<Vector3>(prefix + "angularVelocityMax", {0.0f, 0.0f, 0.0f});

        group->SetBillboard(data->Load(prefix + "enableBillboard", true));

        // ★ 速度ストレッチ設定のロード
        group->GetPerView()->enableVelocityStretch = data->Load<uint32_t>(prefix + "enableVelocityStretch", 0);
        group->GetPerView()->velocityStretchFactor  = data->Load(prefix + "velocityStretchFactor", 0.1f);

        // ★ 中間カラー設定のロード
        settings.enableMidColor  = data->Load<uint32_t>(prefix + "enableMidColor", 0);
        settings.midColorRatio   = data->Load(prefix + "midColorRatio", 0.5f);
        settings.midColor        = data->Load(prefix + "midColor", Vector4(1.0f, 1.0f, 1.0f, 1.0f));

        // ★ タービュランス設定のロード
        settings.enableTurbulence    = data->Load<uint32_t>(prefix + "enableTurbulence", 0);
        settings.turbulenceStrength  = data->Load(prefix + "turbulenceStrength", 1.0f);
        settings.turbulenceFrequency = data->Load(prefix + "turbulenceFrequency", 2.0f);

        // ★ 発生形状設定のロード
        settings.emitShape        = data->Load<uint32_t>(prefix + "emitShape", 0);
        settings.emitSphereRadius = data->Load(prefix + "emitSphereRadius", 1.0f);
        settings.emitConeAngle    = data->Load(prefix + "emitConeAngle", 0.5236f);

        group->SetSettingData(settings);
        group->SetBlendMode(static_cast<BlendMode>(data->Load<int>(prefix + "blendMode", static_cast<int>(BlendMode::kAdd))));

        AddParticleGroup(group);
    }
    ImGuiNotification::Post("パーティクル設定を読み込みました: " + name_, {0.2f, 0.8f, 0.8f, 1.0f});
}

void ParticleCSEmitter::LoadCloneSetting() {
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ParticleCS", name_);
    if (!data->Exists()) {
        return;
    } else {
        particleGroups_.clear();
        particleGroupNames_.clear();
    }

    isAuto_ = data->Load("isAuto", false);
    isVisible_ = data->Load("isVisible", true);
    isGizmoSelectable_ = data->Load("isGizmoSelectable", true);
    drawGroup_ = data->Load<std::string>("drawGroup", "3D");
    if (drawGroup_ != "UI") {
        drawGroup_ = "3D"; // 旧データは3D扱いに正規化
    }
    emitterMeshData_->frequency = data->Load("frequency", 0.1f);
    emitterMeshData_->frequencyTime = data->Load("frequencyTime", 0.0f);
    emitterMeshData_->translate = data->Load<Vector3>("translate", Vector3(0.0f, 0.0f, 0.0f));
    emitterMeshData_->rotation = data->Load<Quaternion>("rotation", Quaternion::IdentityQuaternion());
    emitterMeshData_->scale = data->Load<Vector3>("scale", Vector3(1.0f, 1.0f, 1.0f));
    emitterMeshData_->emitFromSurface = data->Load<uint32_t>("emitFromSurface", 1);

    modelPath_ = data->Load("modelPath", std::string(""));
    primitiveType_ = static_cast<PrimitiveType>(data->Load("primitiveType", static_cast<int>(PrimitiveType::None)));
    // フィールド影響設定
    receiveFields_ = data->Load("receiveFields", false);
    fieldGroupId_ = data->Load("fieldGroupId", -1);
    emitOnlyOnFieldContact_ = data->Load("emitOnlyOnFieldContact", false);
    fieldContactEmitCount_ = static_cast<uint32_t>(data->Load("fieldContactEmitCount", 1000));

    if (!modelPath_.empty()) {
        LoadModel(modelPath_);
        CreateModelTriangles();
        CreateModelEdges();
    } else if (primitiveType_ != PrimitiveType::None) {
        LoadPrimitiveModel(primitiveType_);
        CreateModelTriangles();
        CreateModelEdges();
    }

    groupNum_ = data->Load("particleGroupCount", 0);
    for (int i = 0; i < groupNum_; i++) {
        std::string prefix = "group_" + std::to_string(i) + "_";
        std::string groupName = data->Load(prefix + "name", std::string(""));

        auto group = ParticleCSGroupManager::GetInstance()->GetIndependentParticleGroup(groupName);
        if (!group)
            continue;

        ParticleCSSettings settings;
        settings.lifeTimeMin = data->Load(prefix + "minLifetime", 1.0f);
        settings.lifeTimeMax = data->Load(prefix + "maxLifetime", 1.0f);
        settings.scaleMin = data->Load(prefix + "minScale", 1.0f);
        settings.scaleMax = data->Load(prefix + "maxScale", 1.0f);
        settings.velocityMin = data->Load<Vector3>(prefix + "minVelocity", {0.0f, 0.0f, 0.0f});
        settings.velocityMax = data->Load<Vector3>(prefix + "maxVelocity", {0.0f, 0.0f, 0.0f});
        settings.startColor = data->Load(prefix + "startColor", Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        settings.endColor = data->Load(prefix + "endColor", Vector4(1.0f, 1.0f, 1.0f, 0.0f));
        settings.enableLifetimeScale = data->Load<uint32_t>(prefix + "enableLifetimeScale", 0);
        settings.enableRandomColor = data->Load<uint32_t>(prefix + "enableRandomColor", 0);
        settings.enableSinScale = data->Load<uint32_t>(prefix + "enableSinScale", 0);
        settings.sinScaleFrequency = data->Load(prefix + "sinScaleFrequency", 5.0f);
        settings.sinScaleAmplitude = data->Load(prefix + "sinScaleAmplitude", 0.3f);
        settings.emitCount = data->Load<uint32_t>(prefix + "emitCount", 10);
        settings.enableGravity = data->Load<uint32_t>(prefix + "enableGravity", false);
        settings.gravity = data->Load<Vector3>(prefix + "gravity", {0.0f, 0.0f, 0.0f});
        settings.maxParticleCount = group->GetMaxParticleCount();

        // ★ トレイル設定のロード
        settings.enableTrail = data->Load<uint32_t>(prefix + "enableTrail", 0);
        settings.trailSpawnDistance = data->Load(prefix + "trailSpawnDistance", 0.1f);
        settings.maxTrailPerParticle = data->Load<uint32_t>(prefix + "maxTrailPerParticle", 5);
        settings.trailLifeTimeScale = data->Load(prefix + "trailLifeTimeScale", 1.0f);
        settings.trailScaleMultiplier = data->Load<Vector3>(prefix + "trailScaleMultiplier", {0.8f, 0.8f, 0.8f});
        settings.trailColorMultiplier = data->Load(prefix + "trailColorMultiplier", Vector4(1.0f, 1.0f, 1.0f, 0.7f));
        settings.trailVelocityScale = data->Load(prefix + "trailVelocityScale", 0.3f);
        settings.trailInheritVelocity = data->Load<uint32_t>(prefix + "trailInheritVelocity", 1);
        settings.trailMinLifeTime = data->Load(prefix + "trailMinLifeTime", 0.5f);

        settings.enableGather = data->Load(prefix + "enableGather", 0);
        settings.gatherStartRatio = data->Load(prefix + "gatherStartRatio", 0.5f);
        settings.gatherStrength = data->Load(prefix + "gatherStrength", 1.0f);
        settings.gatherTarget = data->Load<Vector3>(prefix + "gatherTarget", {0.0f, 0.0f, 0.0f});
        settings.gatherTargetOffset = data->Load<Vector3>(prefix + "gatherTargetOffset", {0.0f, 0.0f, 0.0f});
        settings.enableGatherForTrail = data->Load<uint32_t>(prefix + "enableGatherForTrail", 0);
        settings.enableVortex = data->Load<uint32_t>(prefix + "enableVortex", 0);
        settings.vortexTarget = data->Load<Vector3>(prefix + "vortexTarget", {0.0f, 0.0f, 0.0f});
        settings.vortexTargetOffset = data->Load<Vector3>(prefix + "vortexTargetOffset", {0.0f, 0.0f, 0.0f});
        settings.vortexStrength = data->Load(prefix + "vortexStrength", 1.0f);
        settings.enableVortexForTrail = data->Load<uint32_t>(prefix + "enableVortexForTrail", 0);
        settings.vortexAxis = data->Load<Vector3>(prefix + "vortexAxis", {0.0f, 1.0f, 0.0f});

        settings.enableAcceleration = data->Load<uint32_t>(prefix + "enableAcceleration", 0);
        settings.acceleration = data->Load<Vector3>(prefix + "acceleration", {0.0f, 0.0f, 0.0f});
        settings.enableVelocityDamping = data->Load<uint32_t>(prefix + "enableVelocityDamping", 0);
        settings.velocityDampingFactor = data->Load(prefix + "velocityDampingFactor", 0.0f);
        settings.enableLifetimeVelocityDamping = data->Load<uint32_t>(prefix + "enableLifetimeVelocityDamping", 0);
        settings.lifetimeVelocityDampingStart = data->Load(prefix + "lifetimeVelocityDampingStart", 0.0f);
        settings.enableRadialVelocity = data->Load<uint32_t>(prefix + "enableRadialVelocity", 0);
        settings.radialVelocityStrength = data->Load(prefix + "radialVelocityStrength", 0.0f);
        settings.radialVelocityRandomness = data->Load(prefix + "radialVelocityRandomness", 0.0f);
        settings.radialVelocityCenter = data->Load<Vector3>(prefix + "radialVelocityCenter", {0.0f, 0.0f, 0.0f});

        settings.enableCurlNoise = data->Load<uint32_t>(prefix + "enableCurlNoise", 0);
        settings.curlNoiseScale = data->Load(prefix + "curlNoiseScale", 1.0f);
        settings.curlNoiseStrength = data->Load(prefix + "curlNoiseStrength", 1.0f);
        settings.curlNoiseTimeScale = data->Load(prefix + "curlNoiseTimeScale", 1.0f);
        settings.curlNoiseOctaves = data->Load<uint32_t>(prefix + "curlNoiseOctaves", 1);
        settings.curlNoiseAttractStrength = data->Load(prefix + "curlNoiseAttractStrength", 0.0f);
        settings.curlNoiseBlendMode = data->Load<uint32_t>(prefix + "curlNoiseBlendMode", 0);
        settings.curlNoisePosRandomStrength = data->Load(prefix + "curlNoisePosRandomStrength", 0.0f);
        settings.curlNoiseAttractCenter = data->Load<Vector3>(prefix + "curlNoiseAttractCenter", {0.0f, 0.0f, 0.0f});

        // ★ 終了スケール設定のロード
        settings.enableEndScale = data->Load<uint32_t>(prefix + "enableEndScale", 0);
        settings.endScaleValue = data->Load<Vector3>(prefix + "endScaleValue", {0.0f, 0.0f, 0.0f});

        // ★ 回転設定のロード
        settings.enableRandomRotation = data->Load<uint32_t>(prefix + "enableRandomRotation", 0);
        settings.rotationMin = data->Load<Vector3>(prefix + "rotationMin", {0.0f, 0.0f, 0.0f});
        settings.rotationMax = data->Load<Vector3>(prefix + "rotationMax", {0.0f, 0.0f, 0.0f});
        settings.enableRandomAngularVelocity = data->Load<uint32_t>(prefix + "enableRandomAngularVelocity", 0);
        settings.angularVelocityMin = data->Load<Vector3>(prefix + "angularVelocityMin", {0.0f, 0.0f, 0.0f});
        settings.angularVelocityMax = data->Load<Vector3>(prefix + "angularVelocityMax", {0.0f, 0.0f, 0.0f});

        group->SetBillboard(data->Load(prefix + "enableBillboard", true));

        // ★ 速度ストレッチ設定のロード
        group->GetPerView()->enableVelocityStretch = data->Load<uint32_t>(prefix + "enableVelocityStretch", 0);
        group->GetPerView()->velocityStretchFactor  = data->Load(prefix + "velocityStretchFactor", 0.1f);

        // ★ 中間カラー設定のロード
        settings.enableMidColor  = data->Load<uint32_t>(prefix + "enableMidColor", 0);
        settings.midColorRatio   = data->Load(prefix + "midColorRatio", 0.5f);
        settings.midColor        = data->Load(prefix + "midColor", Vector4(1.0f, 1.0f, 1.0f, 1.0f));

        // ★ タービュランス設定のロード
        settings.enableTurbulence    = data->Load<uint32_t>(prefix + "enableTurbulence", 0);
        settings.turbulenceStrength  = data->Load(prefix + "turbulenceStrength", 1.0f);
        settings.turbulenceFrequency = data->Load(prefix + "turbulenceFrequency", 2.0f);

        // ★ 発生形状設定のロード
        settings.emitShape        = data->Load<uint32_t>(prefix + "emitShape", 0);
        settings.emitSphereRadius = data->Load(prefix + "emitSphereRadius", 1.0f);
        settings.emitConeAngle    = data->Load(prefix + "emitConeAngle", 0.5236f);

        group->SetSettingData(settings);
        group->SetBlendMode(static_cast<BlendMode>(data->Load<int>(prefix + "blendMode", static_cast<int>(BlendMode::kAdd))));

        AddParticleGroup(group);
    }
    ImGuiNotification::Post("パーティクル設定を読み込みました: " + name_, {0.2f, 0.8f, 0.8f, 1.0f});
}

void ParticleCSEmitter::DrawImGui() {
#ifdef USE_IMGUI
    if (ImGui::BeginTabBar("EmitterTabBar")) {
        if (ImGui::BeginTabItem(name_.c_str())) {
            ImGuiStyle &style = ImGui::GetStyle();
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.14f, 0.15f, 1.00f));

            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.34f, 0.26f, 0.26f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.42f, 0.32f, 0.32f, 0.70f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.50f, 0.40f, 0.40f, 0.85f));

            if (ImGui::CollapsingHeader("エミッターデータ##EmitterData")) {
                ImGui::PopStyleColor(3);

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
                ImGui::Text("エミッター設定:");
                ImGui::PopStyleColor();

                ImGui::Separator();

                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.2f, 0.5f));

                ImGui::DragFloat("発生間隔##Freq", &emitterMeshData_->frequency, 0.001f, 0.001f, 10.0f);
                ImGui::DragFloat3("エミッタの座標##Translate", &emitterMeshData_->translate.x, 0.1f);

                Vector3 currentEuler = emitterMeshData_->rotation.ToEulerDegrees();
                ImGui::Text("現在の回転: %.1f° %.1f° %.1f°", currentEuler.x, currentEuler.y, currentEuler.z);

                static Vector3 deltaRotation = {0.0f, 0.0f, 0.0f};
                if (ImGui::DragFloat3("##EmitterRotation", &deltaRotation.x, 0.1f, -10.0f, 10.0f, "%.1f°")) {
                    Quaternion currentRotation = emitterMeshData_->rotation;
                    Quaternion deltaQuatX = Quaternion::FromAxisAngle(Vector3(1, 0, 0), deltaRotation.x * std::numbers::pi_v<float> / 180.0f);
                    Quaternion deltaQuatY = Quaternion::FromAxisAngle(Vector3(0, 1, 0), deltaRotation.y * std::numbers::pi_v<float> / 180.0f);
                    Quaternion deltaQuatZ = Quaternion::FromAxisAngle(Vector3(0, 0, 1), deltaRotation.z * std::numbers::pi_v<float> / 180.0f);
                    Quaternion deltaQuat = deltaQuatY * deltaQuatX * deltaQuatZ;
                    Quaternion newRotation = currentRotation * deltaQuat;
                    emitterMeshData_->rotation = newRotation.Normalize();
                    deltaRotation = {0.0f, 0.0f, 0.0f};
                }

                ImGui::SameLine();
                if (ImGui::Button("リセット##EmitterRotation")) {
                    emitterMeshData_->rotation = Quaternion::IdentityQuaternion();
                    deltaRotation = {0.0f, 0.0f, 0.0f};
                }

                ImGui::DragFloat3("エミッタの大きさ##Scale", &emitterMeshData_->scale.x, 0.1f);

                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::Separator();

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
                ImGui::Text("アンカーポイント:");
                ImGui::PopStyleColor();

                ImGui::DragFloat3("基準点##AnchorPoint", &emitterMeshData_->anchorPoint.x, 0.01f, 0.0f, 1.0f, "%.2f");

                ImGui::Spacing();
                ImGui::Separator();

                // 発生位置設定（ラジオボタンで3択）
                if (emitterMeshData_->triangleCount > 0 || emitterMeshData_->edgeCount > 0) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
                    ImGui::Text("発生位置:");
                    ImGui::PopStyleColor();

                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.3f, 0.4f, 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.8f, 0.6f, 0.2f, 1.0f));

                    int emitMode = static_cast<int>(emitterMeshData_->emitFromSurface);

                    ImGui::RadioButton("内部から発生##EmitInternal", &emitMode, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("表面から発生##EmitSurface", &emitMode, 1);
                    ImGui::SameLine();
                    ImGui::RadioButton("線上から発生##EmitEdge", &emitMode, 2);

                    emitterMeshData_->emitFromSurface = static_cast<uint32_t>(emitMode);

                    ImGui::PopStyleColor(2);

                    // ツールチップ
                    if (ImGui::IsItemHovered()) {
                        const char *tooltip = "";
                        if (emitMode == 0)
                            tooltip = "メッシュの内側全体からパーティクルが発生します";
                        else if (emitMode == 1)
                            tooltip = "メッシュの表面からパーティクルが発生します";
                        else if (emitMode == 2)
                            tooltip = "メッシュのエッジ（線）上からパーティクルが発生します";
                        ImGui::SetTooltip("%s", tooltip);
                    }
                }

                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.2f, 0.5f));

                // モデル情報表示
                if (emitterMeshData_->triangleCount > 0) {
                    ImGui::Spacing();
                    ImGui::Text("三角形数: %d", emitterMeshData_->triangleCount);
                    if (emitterMeshData_->edgeCount > 0) {
                        ImGui::Text("エッジ数: %d", emitterMeshData_->edgeCount);
                    }
                    if (!modelPath_.empty()) {
                        ImGui::Text("モデル: %s", modelPath_.c_str());
                    } else if (primitiveType_ != PrimitiveType::None) {
                        ImGui::Text("プリミティブタイプ");
                    }
                }

                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
                ImGui::Checkbox("自動更新##Auto", &isAuto_);
                ImGui::SameLine();
                {
                    bool noGroups = particleGroups_.empty();
                    bool disabled = isAuto_ || noGroups;
                    uint32_t cnt = noGroups ? 0u : particleGroups_[0]->GetSettingsData()->emitCount;
                    std::string btnLabel = "一回発生 (x" + std::to_string(cnt) + ")##EmitOnce";
                    if (disabled) ImGui::BeginDisabled();
                    if (ImGui::Button(btnLabel.c_str())) {
                        EmitOnce();
                        ImGuiNotification::Post("パーティクルを " + std::to_string(cnt) + " 個発生しました", {0.3f, 1.0f, 0.5f, 1.0f});
                    }
                    if (disabled) ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        if (isAuto_)      ImGui::SetTooltip("自動更新がONのため使用できません\n（自動更新をOFFにしてください）");
                        else if (noGroups) ImGui::SetTooltip("パーティクルグループが未設定です\n（グループを追加してください）");
                        else if (cnt == 0) ImGui::SetTooltip("発生数が0のため効果がありません\n（発生数設定を確認してください）");
                    }
                }
                if (ImGui::Checkbox("ギズモ選択", &isGizmoSelectable_)) {
                    ImGuizmoManager::GetInstance()->SetSelectable(name_, isGizmoSelectable_);
                }
                ImGui::Checkbox("エミッター表示##Visible", &isVisible_);
                ImGui::PopStyleColor();
            } else {
                ImGui::PopStyleColor(3);
            }

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
            ImGui::TextUnformatted("フィールド影響設定");
            ImGui::PopStyleColor();
            bool rf = receiveFields_;
            if (ImGui::Checkbox("フィールドの影響を受ける", &rf)) {
                receiveFields_ = rf;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("オフにするとParticleFieldManagerのフィールドが\nこのエミッターに作用しなくなります");
            }

            if (receiveFields_) {
                ImGui::Indent();
                ImGui::PushItemWidth(120.0f);
                int fgid = fieldGroupId_;
                if (ImGui::DragInt("フィールドグループID##fgid", &fgid, 1, -1, 255)) {
                    fieldGroupId_ = std::max(-1, fgid);
                }
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "-1 = 全フィールドから影響を受ける（デフォルト）\n"
                        "0以上 = 同じIDのフィールドのみから影響を受ける");
                if (fieldGroupId_ == -1) {
                    ImGui::TextDisabled("  全フィールド対象");
                } else {
                    ImGui::Text("  ID: %d のフィールドのみ対象", fieldGroupId_);
                }

                ImGui::Spacing();
                bool eofc = emitOnlyOnFieldContact_;
                if (ImGui::Checkbox("フィールド接触時のみEmit##EmitOnFieldContact", &eofc)) {
                    emitOnlyOnFieldContact_ = eofc;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "ON : シェーダー側でフィールド球内のランダム点を生成し\n"
                        "     エミッター表面に投影してEmit位置を決定します\n"
                        "     全スレッドがフィールド接触部分にEmitするため\n"
                        "     500000のような大量発生数は不要になります\n"
                        "     (推奨: 500〜3000程度)\n"
                        "OFF: 通常通りエミッター全体からランダムEmitします");
                }

                if (emitOnlyOnFieldContact_) {
                    ImGui::Indent();
                    ImGui::PushItemWidth(180.0f);
                    int emitCnt = static_cast<int>(fieldContactEmitCount_);
                    if (ImGui::DragInt("接触Emit発生数/フレーム##FieldContactEmit", &emitCnt, 10, 1, 50000)) {
                        fieldContactEmitCount_ = static_cast<uint32_t>(std::max(1, emitCnt));
                    }
                    ImGui::PopItemWidth();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "フィールド接触Emitモード時の1フレームあたり発生数\n"
                            "全スレッドがフィールド接触点にEmitするため\n"
                            "500〜3000程度で十分密になります\n"
                            "フィールドを動かすほどEmit位置も動的に追従します");
                    }
                    ImGui::Unindent();
                }

                ImGui::Unindent();
            }

            ImGui::Spacing();

            // パーティクルグループ設定セクション（既存のコードと同じ）
            if (!particleGroups_.empty()) {
                static int selectedGroupIndex = 0;
                if (selectedGroupIndex >= static_cast<int>(particleGroups_.size())) {
                    selectedGroupIndex = 0;
                }

                std::vector<std::string> groupNames;
                for (const auto &group : particleGroups_) {
                    groupNames.push_back(group->GetGroupName());
                }

                std::vector<const char *> groupNameCStrs;
                for (auto &n : groupNames)
                    groupNameCStrs.push_back(n.c_str());

                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.18f, 0.22f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.34f, 0.48f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.44f, 0.60f, 0.95f));

                ImGui::SetNextItemWidth(200.0f);
                ImGui::Combo("選択中のグループ##GroupCombo", &selectedGroupIndex, groupNameCStrs.data(), (int)groupNameCStrs.size());

                ImGui::PopStyleColor(3);

                if (selectedGroupIndex >= 0 && selectedGroupIndex < static_cast<int>(particleGroups_.size())) {
                    ImGui::Separator();
                    particleGroups_[selectedGroupIndex]->SetFrequency(emitterMeshData_->frequency);
                    particleGroups_[selectedGroupIndex]->DrawImGui();
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.6f, 0.6f, 1.0f));
                ImGui::Text("GPUパーティクルグループがありません");
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // グループ管理セクション
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.28f, 0.32f, 0.40f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.34f, 0.40f, 0.50f, 0.70f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.42f, 0.48f, 0.58f, 0.85f));

            if (ImGui::CollapsingHeader("GPUグループ管理##GPUGroupManagement")) {
                ImGui::PopStyleColor(3);

                ImGui::Spacing();

                auto allGroups = ParticleCSGroupManager::GetInstance()->GetParticleGroups();

                std::vector<std::string> availableNames;
                std::vector<std::string> attachedNames;

                for (auto *group : allGroups) {
                    const std::string &name = group->GetGroupName();
                    if (particleGroupNames_.contains(name)) {
                        attachedNames.push_back(name);
                    } else {
                        availableNames.push_back(name);
                    }
                }

                static std::vector<int> leftSelected;
                static std::vector<int> rightSelected;

                std::vector<const char *> availableItems;
                for (auto &name : availableNames)
                    availableItems.push_back(name.c_str());

                std::vector<const char *> attachedItems;
                for (auto &name : attachedNames)
                    attachedItems.push_back(name.c_str());

                leftSelected.erase(std::remove_if(leftSelected.begin(), leftSelected.end(),
                                                  [&](int i) { return i >= (int)availableNames.size(); }),
                                   leftSelected.end());
                rightSelected.erase(std::remove_if(rightSelected.begin(), rightSelected.end(),
                                                   [&](int i) { return i >= (int)attachedNames.size(); }),
                                    rightSelected.end());

                float width = ImGui::GetContentRegionAvail().x;
                float halfWidth = width * 0.45f;

                // ヘッダーテキストのスタイル
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.56f, 0.69f, 0.86f, 1.0f));
                ImGui::Text("利用可能なGPUグループ");
                ImGui::SameLine(width - halfWidth - 50);
                ImGui::Text("アタッチ済みGPUグループ");
                ImGui::PopStyleColor();

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));

                // 左リスト用のスタイル設定
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.15f, 0.2f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.4f, 0.5f, 0.8f));

                ImGui::BeginChild("gpu_available_groups##GPUAvailableGroups", ImVec2(halfWidth, 200), true);

                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.28f, 0.34f, 0.42f, 0.55f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.34f, 0.42f, 0.52f, 0.70f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.40f, 0.50f, 0.60f, 0.85f));

                if (availableItems.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    ImGui::Text("利用可能なGPUグループがありません");
                    ImGui::PopStyleColor();
                } else {
                    for (int i = 0; i < availableItems.size(); ++i) {
                        bool selected = std::find(leftSelected.begin(), leftSelected.end(), i) != leftSelected.end();
                        std::string selectableId = std::string(availableItems[i]) + "##GPUAvailable" + std::to_string(i);
                        if (ImGui::Selectable(selectableId.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                            if (!ImGui::GetIO().KeyCtrl)
                                leftSelected.clear();

                            auto it = std::find(leftSelected.begin(), leftSelected.end(), i);
                            if (it != leftSelected.end())
                                leftSelected.erase(it);
                            else
                                leftSelected.push_back(i);

                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                                auto group = ParticleCSGroupManager::GetInstance()->GetParticleCSGroup(availableNames[i]);
                                AddParticleGroup(group);
                                leftSelected.clear();
                            }
                        }
                    }
                }

                ImGui::PopStyleColor(3);
                ImGui::EndChild();

                ImGui::SameLine();

                // 中央のボタン群
                ImGui::BeginGroup();
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 12));

                // ボタンのスタイル設定
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.40f, 0.30f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.50f, 0.38f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.32f, 0.58f, 0.44f, 1.0f));

                bool canMoveRight = !leftSelected.empty();
                if (!canMoveRight) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                }

                if (ImGui::Button("追加 >>##GPUAddButton", ImVec2(80, 35)) && canMoveRight) {
                    for (int idx : leftSelected) {
                        auto group = ParticleCSGroupManager::GetInstance()->GetParticleCSGroup(availableNames[idx]);
                        AddParticleGroup(group);
                    }
                    leftSelected.clear();
                }

                if (!canMoveRight) {
                    ImGui::PopStyleColor(3);
                }

                bool canMoveLeft = !rightSelected.empty();
                if (!canMoveLeft) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                }

                if (ImGui::Button("<< 削除##GPURemoveButton", ImVec2(80, 35)) && canMoveLeft) {
                    for (int idx : rightSelected) {
                        RemoveParticleGroup(attachedNames[idx]);
                    }
                    rightSelected.clear();
                }

                if (!canMoveLeft) {
                    ImGui::PopStyleColor(3);
                }

                ImGui::PopStyleColor(3); // Button colors
                ImGui::PopStyleVar();
                ImGui::EndGroup();

                ImGui::SameLine();

                ImGui::BeginChild("gpu_attached_groups##GPUAttachedGroups", ImVec2(halfWidth, 200), true);

                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.44f, 0.36f, 0.26f, 0.55f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.52f, 0.42f, 0.30f, 0.70f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.58f, 0.48f, 0.36f, 0.85f));

                if (attachedItems.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    ImGui::Text("アタッチされたGPUグループがありません");
                    ImGui::PopStyleColor();
                } else {
                    for (int i = 0; i < attachedItems.size(); ++i) {
                        bool selected = std::find(rightSelected.begin(), rightSelected.end(), i) != rightSelected.end();
                        std::string selectableId = std::string(attachedItems[i]) + "##GPUAttached" + std::to_string(i);
                        if (ImGui::Selectable(selectableId.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                            if (!ImGui::GetIO().KeyCtrl)
                                rightSelected.clear();

                            auto it = std::find(rightSelected.begin(), rightSelected.end(), i);
                            if (it != rightSelected.end())
                                rightSelected.erase(it);
                            else
                                rightSelected.push_back(i);

                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                                RemoveParticleGroup(attachedNames[i]);
                                rightSelected.clear();
                            }
                        }
                    }
                }

                ImGui::PopStyleColor(3);
                ImGui::EndChild();

                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();

                ImGui::Spacing();

                // 操作説明
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                ImGui::Text("操作: Ctrlキー + クリックで複数選択, ダブルクリックで追加/削除");
                ImGui::PopStyleColor();

            } else {
                ImGui::PopStyleColor(3);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ファイル操作セクション
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.38f, 0.32f, 0.26f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.46f, 0.38f, 0.30f, 0.70f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.52f, 0.44f, 0.36f, 0.85f));

            if (ImGui::CollapsingHeader("GPUファイル操作##GPUFileOperations")) {
                ImGui::PopStyleColor(3);

                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.34f, 0.48f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.44f, 0.60f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.36f, 0.52f, 0.70f, 1.0f));

                if (ImGui::Button("GPU設定を保存##GPUSaveButton", ImVec2(120, 35))) {
                    SaveSetting();
                    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ParticleCS", name_);
                    data->Flush();
                    ImGuiNotification::Post("パーティクル設定を保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
                }
                ImGui::PopStyleColor(3);

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("現在のGPUパーティクル設定をファイルに保存します");
                }

                ImGui::Spacing();

            } else {
                ImGui::PopStyleColor(3);
            }

            // メインウィンドウの背景色をポップ
            ImGui::PopStyleColor();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
#endif // USE_IMGUI
}
} // namespace Hagine
