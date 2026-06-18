#include "Skin.h"
#include "algorithm"
#include <DirectXCommon.h>
#include <Graphics/Srv/SrvManager.h>
#include <cassert>
#include <myMath.h>

namespace Hagine {
void Skin::Initialize(const Skeleton &skeleton, const ModelData &modelData) {
    dxCommon_ = DirectXCommon::GetInstance();
    srvManager_ = SrvManager::GetInstance();
    // モデルデータとスケルトンに基づいて、GPUスキニングに必要なバッファ類を作成
    skinCluster_ = CreateSkinCluster(skeleton, modelData);
}

void Skin::Update(const Skeleton &skeleton) {
    // 各ジョイントについて、現在のスケルトン空間行列と逆バインドポーズ行列を掛け合わせ、
    // シェーダーが頂点変形に使える行列（Palette）を算出
    for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex) {
        assert(jointIndex < skinCluster_.inverseBindPoseMatrices.size());
        skinCluster_.mappedPalette[jointIndex].skeletonSpaceMatrix =
            skinCluster_.inverseBindPoseMatrices[jointIndex] * skeleton.joints[jointIndex].skeletonSpaceMatrix;
        skinCluster_.mappedPalette[jointIndex].skeletonSpaceInverseTransposeMatrix =
            Transpose(Inverse(skinCluster_.mappedPalette[jointIndex].skeletonSpaceMatrix));
    }
}

void Skin::UpdateInputVertices(const ModelData &modelData) {
    // モデルデータから各メッシュの頂点情報を取得し、GPU上のバッファへコピー
    for (const auto &mesh : modelData.meshes) {
        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            if (vertexOffset_ + i < totalVertexCount_) {
                skinCluster_.mappedVertex[vertexOffset_ + i] = mesh.vertices[i];
            }
        }
        vertexOffset_ += mesh.vertices.size();
    }
}

void Skin::ExecuteSkinning(ID3D12GraphicsCommandList *commandList) {
    // ComputeShaderで使用するリソース（パレット、頂点、ウェイト等）をバインド
    commandList->SetComputeRootDescriptorTable(0, skinCluster_.paletteSrvHandle.second);
    commandList->SetComputeRootDescriptorTable(1, skinCluster_.inputVertexSrvHandle.second);
    commandList->SetComputeRootDescriptorTable(2, skinCluster_.influenceSrvHandle.second);
    commandList->SetComputeRootDescriptorTable(3, skinCluster_.outputVertexSrvHandle.second);
    commandList->SetComputeRootConstantBufferView(4,
                                                  skinCluster_.skinningInformationResource->GetGPUVirtualAddress());

    // 頂点数に応じてスレッドグループ数を計算し、スキニング計算シェーダーを実行
    uint32_t numGroups = (static_cast<uint32_t>(totalVertexCount_) + 1023) / 1024;
    commandList->Dispatch(numGroups, 1, 1);
}

SkinCluster Skin::CreateSkinCluster(const Skeleton &skeleton, const ModelData &modelData) {
    SkinCluster skinCluster;

    // 全頂点数を集計し、必要なバッファサイズを確定
    for (const auto &mesh : modelData.meshes) {
        totalVertexCount_ += mesh.vertices.size();
    }

    // 各種バッファ（パレット、ウェイト、頂点データ）のリソース生成
    CreatePaletteResource(skinCluster, skeleton);
    CreateInfluenceResource(skinCluster, skeleton);
    CreateInputVertexResource(skinCluster, skeleton);
    CreateOutputVertexResource(skinCluster, skeleton);
    CreateSkinningInformationResource(skinCluster, skeleton);

    // 初期状態として単位行列を設定
    skinCluster.inverseBindPoseMatrices.resize(skeleton.joints.size());
    std::generate(skinCluster.inverseBindPoseMatrices.begin(), skinCluster.inverseBindPoseMatrices.end(), []() { return MakeIdentity4x4(); });

    // メッシュごとの頂点オフセットを計算
    std::vector<size_t> meshVertexOffsets;
    size_t vertexOffset_ = 0;
    meshVertexOffsets.reserve(modelData.meshes.size());
    for (const auto &mesh : modelData.meshes) {
        meshVertexOffsets.push_back(vertexOffset_);
        vertexOffset_ += mesh.vertices.size();
    }

    // モデルデータ内のウェイト情報を解析し、頂点ごとの影響ボーンとウェイト値を設定
    for (const auto &jointWeight : modelData.skinClusterData) {
        const std::string &key = jointWeight.first;
        size_t colonPos = key.find(':');
        if (colonPos == std::string::npos)
            continue;

        size_t keyMeshIndex = std::stoul(key.substr(0, colonPos));
        std::string jointName = key.substr(colonPos + 1);

        auto it = skeleton.jointMap.find(jointName);
        if (it == skeleton.jointMap.end()) {
            continue;
        }

        skinCluster.inverseBindPoseMatrices[(*it).second] = jointWeight.second.inverseBindPoseMatrix;

        for (const auto &vertexWeight : jointWeight.second.vertexWeights) {
            size_t meshIndex = vertexWeight.meshIndex;
            size_t localVertexIndex = vertexWeight.vertexIndex;
            if (meshIndex >= meshVertexOffsets.size())
                continue;
            size_t globalVertexIndex = meshVertexOffsets[meshIndex] + localVertexIndex;
            if (globalVertexIndex >= totalVertexCount_)
                continue;
            
            // 頂点に影響を与えるボーンのインデックスと重みを書き込む
            auto &currentInfluence = skinCluster.mappedInfluence[globalVertexIndex];
            for (uint32_t index = 0; index < kNumMaxInfluence; ++index) {
                if (currentInfluence.weights[index] == 0.0f) {
                    currentInfluence.weights[index] = vertexWeight.weight;
                    currentInfluence.jointIndices[index] = (*it).second;
                    break;
                }
            }
        }
    }
 
    meshVertexOffsets_.clear();
    size_t offset = 0;
    for (const auto &mesh : modelData.meshes) {
        meshVertexOffsets_.push_back(offset);
        offset += mesh.vertices.size();
    }

    return skinCluster;
}

void Skin::CreatePaletteResource(SkinCluster &skinCluster, const Skeleton &skeleton) {
    // ボーン行列パレット用のバッファ生成とSRV登録
    skinCluster.paletteResource = dxCommon_->CreateBufferResource(sizeof(WellForGPU) * skeleton.joints.size());
    WellForGPU *mappedPalette = nullptr;
    skinCluster.paletteResource->Map(0, nullptr, reinterpret_cast<void **>(&mappedPalette));
    skinCluster.mappedPalette = {mappedPalette, skeleton.joints.size()};
    skinClusterPaletteSrvIndex_ = srvManager_->Allocate() + 1;
    skinCluster.paletteSrvHandle.first = srvManager_->GetCPUDescriptorHandle(skinClusterPaletteSrvIndex_);
    skinCluster.paletteSrvHandle.second = srvManager_->GetGPUDescriptorHandle(skinClusterPaletteSrvIndex_);

    srvManager_->CreateSRVforStructuredBuffer(skinClusterPaletteSrvIndex_, skinCluster.paletteResource.Get(), UINT(skeleton.joints.size()), sizeof(WellForGPU));
}

void Skin::CreateInfluenceResource(SkinCluster &skinCluster, const Skeleton &skeleton) {
    // 頂点ごとのボーン影響データ用のバッファ生成とSRV登録
    skinCluster.influenceResource = dxCommon_->CreateBufferResource(sizeof(VertexInfluence) * totalVertexCount_);
    VertexInfluence *mappedInfluence = nullptr;
    skinCluster.influenceResource->Map(0, nullptr, reinterpret_cast<void **>(&mappedInfluence));
    std::memset(mappedInfluence, 0, sizeof(VertexInfluence) * totalVertexCount_);
    skinCluster.mappedInfluence = {mappedInfluence, totalVertexCount_};
    skinClusterInfluenceSrvIndex_ = srvManager_->Allocate() + 1;
    skinCluster.influenceSrvHandle.first = srvManager_->GetCPUDescriptorHandle(skinClusterInfluenceSrvIndex_);
    skinCluster.influenceSrvHandle.second = srvManager_->GetGPUDescriptorHandle(skinClusterInfluenceSrvIndex_);

    srvManager_->CreateSRVforStructuredBuffer(skinClusterInfluenceSrvIndex_, skinCluster.influenceResource.Get(), UINT(totalVertexCount_), sizeof(VertexInfluence));
}

void Skin::CreateInputVertexResource(SkinCluster &skinCluster, const Skeleton &skeleton) {
    // スキニング前の入力頂点バッファ生成とSRV登録
    skinCluster.inputVertexResource = dxCommon_->CreateBufferResource(sizeof(VertexData) * totalVertexCount_);
    VertexData *mappedVertex = nullptr;
    skinCluster.inputVertexResource->Map(0, nullptr, reinterpret_cast<void **>(&mappedVertex));
    skinCluster.mappedVertex = {mappedVertex, totalVertexCount_};
    skinClusterInputVertexSrvIndex_ = srvManager_->Allocate() + 1;
    skinCluster.inputVertexSrvHandle.first = srvManager_->GetCPUDescriptorHandle(skinClusterInputVertexSrvIndex_);
    skinCluster.inputVertexSrvHandle.second = srvManager_->GetGPUDescriptorHandle(skinClusterInputVertexSrvIndex_);

    srvManager_->CreateSRVforStructuredBuffer(skinClusterInputVertexSrvIndex_, skinCluster.inputVertexResource.Get(), UINT(totalVertexCount_), sizeof(VertexData));
}

void Skin::CreateOutputVertexResource(SkinCluster &skinCluster, const Skeleton &skeleton) {
    // スキニング後の出力頂点バッファ（UAV）生成とVBV設定
    skinCluster.outputVertexResource = dxCommon_->CreateBufferResource(sizeof(VertexData) * totalVertexCount_, true);
    skinClusterOutputVertexSrvIndex_ = srvManager_->Allocate() + 1;
    skinCluster.outputVertexSrvHandle.first = srvManager_->GetCPUDescriptorHandle(skinClusterOutputVertexSrvIndex_);
    skinCluster.outputVertexSrvHandle.second = srvManager_->GetGPUDescriptorHandle(skinClusterOutputVertexSrvIndex_);

    srvManager_->CreateUAVStructuredBuffer(skinClusterOutputVertexSrvIndex_, skinCluster.outputVertexResource.Get(), static_cast<uint32_t>(totalVertexCount_), sizeof(VertexData));

    skinCluster.outputVertexBufferView.BufferLocation = skinCluster.outputVertexResource->GetGPUVirtualAddress();
    skinCluster.outputVertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * totalVertexCount_);
    skinCluster.outputVertexBufferView.StrideInBytes = sizeof(VertexData);
}

void Skin::CreateSkinningInformationResource(SkinCluster &skinCluster, const Skeleton &skeleton) {
    // スキニングに必要な定数情報（頂点数など）を格納するバッファ生成
    skinCluster.skinningInformationResource = dxCommon_->CreateBufferResource(sizeof(SkinningInformationForGPU));
    skinCluster.SkinningInfomationData = nullptr;
    skinCluster.skinningInformationResource->Map(0, nullptr, reinterpret_cast<void **>(&skinCluster.SkinningInfomationData));
    skinCluster.SkinningInfomationData->numVertices = static_cast<uint32_t>(totalVertexCount_);
}

} // namespace Hagine
