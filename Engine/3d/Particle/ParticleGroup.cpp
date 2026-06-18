#include "ParticleGroup.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Srv/SrvManager.h"
#include "fstream"
#include <Graphics/Texture/TextureManager.h>

namespace Hagine {
std::unordered_map<std::string, ModelData> ParticleGroup::modelCache_;

void ParticleGroup::Initialize() {
}

void ParticleGroup::Update() {
}
ParticleGroupData ParticleGroup::CreateParticleGroup(const std::string &groupName, const std::string &filename, const std::string &texturePath) {
    particleGroupData_.groupName = groupName;
    modelFilePath_ = filename;
    ModelManager::GetInstance()->LoadModel(filename);
    model_ = ModelManager::GetInstance()->FindModel(filename);
    modelData_ = model_->GetModelData();
    CreateVertexData();
    CreateIndexResource();
    // マテリアルが複数ある場合は最初のものを使う
    particleGroupData_.materials.clear();
    if (texturePath.empty()) {
        if (!modelData_.materials.empty()) {
            particleGroupData_.materials = ForParticleMaterials(modelData_.materials);
        } else {
            particleGroupData_.materials.push_back(ParticleMaterial{});
        }
    } else {
        ParticleMaterial mat;
        mat.textureFilePath = texturePath;
        mat.textureIndex = TextureManager::GetInstance()->GetTextureIndexByFilePath(texturePath);
        particleGroupData_.materials.push_back(mat);
    }
    // すべてのマテリアルのテクスチャをロード
    for (auto &mat : particleGroupData_.materials) {
        TextureManager::GetInstance()->LoadTexture(mat.textureFilePath);
        mat.textureIndex = TextureManager::GetInstance()->GetTextureIndexByFilePath(mat.textureFilePath);
    }
    particleGroupData_.instancingResource = ParticleCommon::GetInstance()->GetDxCommon()->CreateBufferResource(sizeof(ParticleForGPU) * kNumMaxInstance);
    particleGroupData_.instancingSRVIndex = SrvManager::GetInstance()->Allocate() + 1;
    particleGroupData_.instancingResource->Map(0, nullptr, reinterpret_cast<void **>(&particleGroupData_.instancingData));

    SrvManager::GetInstance()->CreateSRVforStructuredBuffer(particleGroupData_.instancingSRVIndex, particleGroupData_.instancingResource.Get(), kNumMaxInstance, sizeof(ParticleForGPU));

    CreateMaterial();
    particleGroupData_.instanceCount = 0;
    return particleGroupData_;
}

ParticleGroupData ParticleGroup::CreatePrimitiveParticleGroup(const std::string &groupName, PrimitiveType type, const std::string &texturePath) {
    particleGroupData_.groupName = groupName;
    type_ = type;
    model_ = ModelManager::GetInstance()->FindModel(ModelManager::GetInstance()->CreatePrimitiveModel(type, texturePath));
    TextureManager::GetInstance()->LoadTexture(texturePath);
    modelData_ = model_->GetModelData();
    CreateVertexData();
    CreateIndexResource();
    // マテリアルが複数ある場合は最初のものを使う
    particleGroupData_.materials.clear();
    if (texturePath.empty()) {
        if (!modelData_.materials.empty()) {
            particleGroupData_.materials = ForParticleMaterials(modelData_.materials);
        } else {
            particleGroupData_.materials.push_back(ParticleMaterial{});
        }
    } else {
        ParticleMaterial mat;
        mat.textureFilePath = texturePath;
        mat.textureIndex = TextureManager::GetInstance()->GetTextureIndexByFilePath(texturePath);
        particleGroupData_.materials.push_back(mat);
    }
    // すべてのマテリアルのテクスチャをロード
    for (auto &mat : particleGroupData_.materials) {
        TextureManager::GetInstance()->LoadTexture(mat.textureFilePath);
        mat.textureIndex = TextureManager::GetInstance()->GetTextureIndexByFilePath(mat.textureFilePath);
    }
    particleGroupData_.instancingResource = ParticleCommon::GetInstance()->GetDxCommon()->CreateBufferResource(sizeof(ParticleForGPU) * kNumMaxInstance);
    particleGroupData_.instancingSRVIndex = SrvManager::GetInstance()->Allocate() + 1;
    particleGroupData_.instancingResource->Map(0, nullptr, reinterpret_cast<void **>(&particleGroupData_.instancingData));

    SrvManager::GetInstance()->CreateSRVforStructuredBuffer(particleGroupData_.instancingSRVIndex, particleGroupData_.instancingResource.Get(), kNumMaxInstance, sizeof(ParticleForGPU));

    CreateMaterial();
    particleGroupData_.instanceCount = 0;
    return particleGroupData_;
}

void ParticleGroup::CreateVertexData() {
    // 複数メッシュ対応: 全メッシュの頂点を連結
    std::vector<VertexData> allVertices;
    for (const auto &mesh : modelData_.meshes) {
        allVertices.insert(allVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    }
    vertexResource_ = ParticleCommon::GetInstance()->GetDxCommon()->CreateBufferResource(sizeof(VertexData) * allVertices.size());
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * allVertices.size());
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
    vertexResource_->Map(0, nullptr, reinterpret_cast<void **>(&vertexData_));
    std::memcpy(vertexData_, allVertices.data(), sizeof(VertexData) * allVertices.size());
}

void ParticleGroup::CreateIndexResource() {
    // 複数メッシュ対応: 全メッシュのインデックスを連結し、頂点オフセットを考慮
    std::vector<uint32_t> allIndices;
    uint32_t vertexOffset = 0;
    for (const auto &mesh : modelData_.meshes) {
        for (auto idx : mesh.indices) {
            allIndices.push_back(idx + vertexOffset);
        }
        vertexOffset += static_cast<uint32_t>(mesh.vertices.size());
    }
    indexResource_ = ParticleCommon::GetInstance()->GetDxCommon()->CreateBufferResource(sizeof(uint32_t) * allIndices.size());
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = UINT(sizeof(uint32_t) * allIndices.size());
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
    indexResource_->Map(0, nullptr, reinterpret_cast<void **>(&indexData_));
    std::memcpy(indexData_, allIndices.data(), sizeof(uint32_t) * allIndices.size());
}

void ParticleGroup::CreateMaterial() {
    // Sprite用のマテリアルリソースをつくる
    materialResource_ = ParticleCommon::GetInstance()->GetDxCommon()->CreateBufferResource(sizeof(ParticleMaterial));
    // 書き込むためのアドレスを取得
    materialResource_->Map(0, nullptr, reinterpret_cast<void **>(&materialData_));
    materialData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    materialData_->uvTransform = MakeIdentity4x4();
}
} // namespace Hagine
