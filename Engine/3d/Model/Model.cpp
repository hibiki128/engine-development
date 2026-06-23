#include "Model.h"
#include "Frame/Frame.h"
#include "Graphics/Texture/TextureManager.h"
#include "Object/Object3dCommon.h"
#include "fstream"
#include "myMath.h"
#include "sstream"
#include <Shadow/ShadowMap.h>
#include <SkyBox/SkyBox.h>

namespace Hagine {
void Model::Initialize(ModelCommon *modelCommon) {
    modelCommon_ = modelCommon;
    srvManager_ = SrvManager::GetInstance();
}

void Model::CreateModel(const std::string &directorypath, const std::string &filename) {
    // 引数で受け取ってメンバ変数に記録する
    directorypath_ = directorypath;
    filename_ = filename;
   
    // モデル読み込み
    modelData_ = LoadModelFile(directorypath_, filename_);

    // メッシュ配列のサイズを調整
    meshes_.resize(modelData_.meshes.size());

    // 各メッシュの初期化
    for (size_t i = 0; i < modelData_.meshes.size(); ++i) {
        meshes_[i] = std::make_unique<Mesh>();
        meshes_[i]->GetMeshData() = modelData_.meshes[i];
        meshes_[i]->Initialize();
    }
}

void Model::CreatePrimitiveModel(const PrimitiveType &type, std::string texPath) {
    // プリミティブモデルは通常単一メッシュ・単一マテリアルなので、
    // 配列サイズを1に設定
    meshes_.resize(1);
    modelData_.meshes.resize(1);

    // メッシュの初期化
    meshes_[0] = std::make_unique<Mesh>();
    meshes_[0]->PrimitiveInitialize(type);
    meshes_[0]->Initialize();

    modelData_.meshes[0] = meshes_[0]->GetMeshData();

    // メッシュのマテリアルインデックスを設定
    modelData_.meshes[0].materialIndex = 0;
}

void Model::Update() {
    if (isGltf_ && animator_ && modelData_.hasAnimations && modelData_.hasBones) {
        skin_->UpdateInputVertices(modelData_);

        ID3D12GraphicsCommandList *commandList = modelCommon_->GetDxCommon()->GetCommandList().Get();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Transition.pResource   = skin_->GetOutputVertexResource();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        if (skinOutputInVertexState_) {
            // VERTEX → UAV（2フレーム目以降: 前フレームでVERTEX状態になっているので遷移）
            barrier.Type                    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.StateBefore  = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            barrier.Transition.StateAfter   = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            commandList->ResourceBarrier(1, &barrier);
        }

        skin_->ExecuteSkinning(commandList);

        // UAV → VERTEX
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        commandList->ResourceBarrier(1, &barrier);

        skinOutputInVertexState_ = true;
    }
}

void Model::Draw(const std::vector<std::unique_ptr<Material>> &materials, std::vector<ObjColor> &color, bool lighting, bool reflect) {
    ID3D12GraphicsCommandList *commandList = modelCommon_->GetDxCommon()->GetCommandList().Get();

    INT vertexOffset = 0;

    for (size_t meshIndex = 0; meshIndex < meshes_.size(); ++meshIndex) {
        Mesh *currentMesh = meshes_[meshIndex].get();
        uint32_t materialIndex = modelData_.meshes[meshIndex].materialIndex;
        if (materialIndex >= materials.size()) {
            materialIndex = 0;
        }
        Material *currentMaterial = materials[materialIndex].get();

        // インデックスバッファ設定
        D3D12_INDEX_BUFFER_VIEW indexBufferView = currentMesh->GetIndexBufferView();
        commandList->IASetIndexBuffer(&indexBufferView);

        // 頂点バッファ設定 - アニメーション有無で使用するバッファを切り替え
        if (isGltf_ && animator_ && modelData_.hasAnimations && modelData_.hasBones) {
            // スキニング後の頂点バッファのみを使用
            D3D12_VERTEX_BUFFER_VIEW vbv = skin_->GetOutputVertexBufferView();
            commandList->IASetVertexBuffers(0, 1, &vbv);

            // パレット情報をシェーダーに渡す（必要に応じて）
            srvManager_->SetGraphicsRootDescriptorTable(8, skin_->GetPaletteSrvIndex());
            vertexOffset = static_cast<INT>(skin_->GetMeshVertexOffset(meshIndex));
        } else {
            // 元の頂点バッファを使用
            D3D12_VERTEX_BUFFER_VIEW vbv = currentMesh->GetVertexBufferView();
            commandList->IASetVertexBuffers(0, 1, &vbv);
        }

        commandList->SetGraphicsRootDescriptorTable(7, srvManager_->GetGPUDescriptorHandle(SkyBox::GetInstance()->GetTextureIndex()));

        // シャドウマップをバインド
        {
            ShadowMap *shadowMap = ShadowMap::GetInstance();
            bool isSkinned = isGltf_ && animator_ && modelData_.hasAnimations && modelData_.hasBones;
            uint32_t shadowSrvSlot    = isSkinned ? 9 : 8;
            uint32_t shadowDataSlot   = isSkinned ? 10 : 9;
            srvManager_->SetGraphicsRootDescriptorTable(shadowSrvSlot, shadowMap->GetShadowSrvIndex());
            commandList->SetGraphicsRootConstantBufferView(shadowDataSlot, shadowMap->GetShadowDataGpuAddress());
        }

        if (reflect) {
            // 環境係数を有効化
            currentMaterial->SetEnvironmentCoefficients(1.0f);
        } else {
            currentMaterial->SetEnvironmentCoefficients(0.0f); // 環境係数を無効化
        }

        // マテリアル描画
        currentMaterial->Draw(color[materialIndex].GetColor(), lighting);

        // 描画コール
        commandList->DrawIndexedInstanced(
            UINT(modelData_.meshes[meshIndex].indices.size()), 1, 0, vertexOffset, 0);
    }
}

void Model::DrawShadow() {
    ID3D12GraphicsCommandList *commandList = modelCommon_->GetDxCommon()->GetCommandList().Get();

    for (size_t meshIndex = 0; meshIndex < meshes_.size(); ++meshIndex) {
        Mesh *currentMesh = meshes_[meshIndex].get();

        D3D12_INDEX_BUFFER_VIEW indexBufferView = currentMesh->GetIndexBufferView();
        commandList->IASetIndexBuffer(&indexBufferView);

        INT vertexOffset = 0;
        if (isGltf_ && animator_ && modelData_.hasAnimations && modelData_.hasBones) {
            D3D12_VERTEX_BUFFER_VIEW vbv = skin_->GetOutputVertexBufferView();
            commandList->IASetVertexBuffers(0, 1, &vbv);
            vertexOffset = static_cast<INT>(skin_->GetMeshVertexOffset(meshIndex));
            skinOutputInVertexState_ = true;
        } else {
            D3D12_VERTEX_BUFFER_VIEW vbv = currentMesh->GetVertexBufferView();
            commandList->IASetVertexBuffers(0, 1, &vbv);
        }

        commandList->DrawIndexedInstanced(
            UINT(modelData_.meshes[meshIndex].indices.size()), 1, 0, vertexOffset, 0);
    }
}

ModelData Model::LoadModelFile(const std::string &directoryPath, const std::string &filename) {
    ModelData modelData;

    // 拡張子に応じたisGltfフラグの設定
    isGltf_ = false;
    if (filename.size() >= 5 && filename.substr(filename.size() - 5) == ".gltf") {
        isGltf_ = true;
    } else if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".obj") {
        isGltf_ = false;
    } else {
        assert(false && "Unsupported file format");
    }

    Assimp::Importer importer;
    std::string filePath = directoryPath + "/" + filename;
    const aiScene *scene = importer.ReadFile(filePath.c_str(), aiProcess_FlipWindingOrder | aiProcess_FlipUVs);

    if (scene && scene->HasAnimations()) {
        modelData.hasAnimations = true;
    } else {
        modelData.hasAnimations = false;
    }

    // メッシュが存在しない場合
    if (!scene || !scene->HasMeshes()) {
        // デフォルトのメッシュとマテリアルを作成
        MeshData defaultMesh;
        MaterialData defaultMaterial;
        defaultMaterial.textureFilePath = "resources/images/debug/white1x1.png";

        modelData.meshes.push_back(defaultMesh);
        modelData.materials.push_back(defaultMaterial);
        return modelData;
    }

    // メッシュ配列のサイズを事前に確保
    modelData.meshes.resize(scene->mNumMeshes);

    // メッシュの処理
    for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        aiMesh *mesh = scene->mMeshes[meshIndex];
        assert(mesh->HasNormals()); // 法線がないMeshは今回は非対応（これは残す）

        if (mesh->HasBones()) {
            modelData.hasBones = true;
        } else {
            modelData.hasBones = false;
        }

        MeshData &currentMesh = modelData.meshes[meshIndex];
        currentMesh.vertices.resize(mesh->mNumVertices);

        bool hasTexcoord = mesh->HasTextureCoords(0); // Texcoordの有無を確認

        // 頂点データの処理
        for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
            aiVector3D &position = mesh->mVertices[vertexIndex];
            aiVector3D &normal = mesh->mNormals[vertexIndex];

            // 右手系→左手系変換
            currentMesh.vertices[vertexIndex].position = {-position.x, position.y, position.z, 1.0f};
            currentMesh.vertices[vertexIndex].normal = {-normal.x, normal.y, normal.z};

            if (hasTexcoord) {
                aiVector3D &texcoord = mesh->mTextureCoords[0][vertexIndex];
                currentMesh.vertices[vertexIndex].texcoord = {texcoord.x, texcoord.y};
            } else {
                // Texcoord が無い場合は (0.0, 0.0) を代入
                currentMesh.vertices[vertexIndex].texcoord = {0.0f, 0.0f};
            }
        }

        // インデックスの処理
        for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
            aiFace &face = mesh->mFaces[faceIndex];
            assert(face.mNumIndices == 3); // トライアングルのみ対応
            for (uint32_t element = 0; element < face.mNumIndices; ++element) {
                uint32_t vertexIndex = face.mIndices[element];
                currentMesh.indices.push_back(vertexIndex);
            }
        }

        // スキニング情報の処理（各メッシュごとに）
        for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
            aiBone *bone = mesh->mBones[boneIndex];
            std::string jointName = bone->mName.C_Str();

            // キーを "メッシュインデックス:ジョイント名" にしてメッシュごとに独立管理
            std::string skinKey = std::to_string(meshIndex) + ":" + jointName;

            // キーが既に存在するか確認してから挿入
            bool isNewEntry = (modelData.skinClusterData.find(skinKey) == modelData.skinClusterData.end());
            JointWeightData &jointWeightData = modelData.skinClusterData[skinKey];

            // 新規エントリの場合のみ inverseBindPoseMatrix を計算
            if (isNewEntry) {
                aiMatrix4x4 bindPoseMatrixAssimp = bone->mOffsetMatrix.Inverse();
                aiVector3D scale, translate;
                aiQuaternion rotate;
                bindPoseMatrixAssimp.Decompose(scale, rotate, translate);

                Matrix4x4 bindPoseMatrix = MakeBoneMatrix(
                    {scale.x, scale.y, scale.z},
                    {rotate.x, -rotate.y, -rotate.z, rotate.w},
                    {-translate.x, translate.y, translate.z});

                jointWeightData.inverseBindPoseMatrix = Inverse(bindPoseMatrix);
            }

            // ウェイト情報の格納
            for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                uint32_t localVertexIndex = bone->mWeights[weightIndex].mVertexId;
                jointWeightData.vertexWeights.push_back({bone->mWeights[weightIndex].mWeight,
                                                         localVertexIndex,
                                                         meshIndex});
            }
        }

        // メッシュに関連するマテリアルインデックスを保存
        currentMesh.materialIndex = mesh->mMaterialIndex;
    }

    // マテリアル配列のサイズを事前に確保
    modelData.materials.resize(scene->mNumMaterials);

    // マテリアルの処理（すべてのマテリアルを処理）
    for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
        aiMaterial *material = scene->mMaterials[materialIndex];
        MaterialData &currentMaterial = modelData.materials[materialIndex];

        // ディフューズテクスチャの取得
        if (material->GetTextureCount(aiTextureType_DIFFUSE) != 0) {
            aiString textureFilePath;
            material->GetTexture(aiTextureType_DIFFUSE, 0, &textureFilePath);
            currentMaterial.textureFilePath = textureFilePath.C_Str();
        } else {
            // テクスチャがない場合はデフォルトのテクスチャを設定
            currentMaterial.textureFilePath = "debug/white1x1.png";
        }

        // その他のマテリアルプロパティの取得
        aiColor3D color(1.0f, 1.0f, 1.0f);
        material->Get(AI_MATKEY_COLOR_DIFFUSE, color);
        currentMaterial.color = {color.r, color.g, color.b, 1.0f};

        // スペキュラー色の取得
        aiColor3D specular(1.0f, 1.0f, 1.0f);
        material->Get(AI_MATKEY_COLOR_SPECULAR, specular);

        // 光沢度の取得
        float shininess = 32.0f;
        material->Get(AI_MATKEY_SHININESS, shininess);
        currentMaterial.shininess = shininess;

        // UV変換行列の初期化
        currentMaterial.uvTransform = MakeIdentity4x4();
    }

    if (modelData.materials.empty()) {
        MaterialData defaultMaterial;
        defaultMaterial.textureFilePath = "debug/white1x1.png";
        defaultMaterial.color = {1.0f, 1.0f, 1.0f, 1.0f};
        defaultMaterial.shininess = 32.0f;
        defaultMaterial.uvTransform = MakeIdentity4x4();
        modelData.materials.push_back(defaultMaterial);
    }

    modelData.rootNode = ReadNode(scene->mRootNode);
    return modelData;
}

Node Model::ReadNode(aiNode *node) {
    Node result;

    aiVector3D scale, translate;
    aiQuaternion rotate;
    node->mTransformation.Decompose(scale, rotate, translate);
    result.transform.scale = {scale.x, scale.y, scale.z};
    result.transform.rotate = {rotate.x, -rotate.y, -rotate.z, rotate.w};
    result.transform.translate = {-translate.x, translate.y, translate.z};

    result.localMatrix = MakeBoneMatrix(result.transform.scale, result.transform.rotate, result.transform.translate);

    result.name = node->mName.C_Str();          // node名を格納
    result.children.resize(node->mNumChildren); // 子供の数だけ確保
    for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
        // 再帰的に読んで階層構造を作っていく
        result.children[childIndex] = ReadNode(node->mChildren[childIndex]);
    }
    return result;
}
} // namespace Hagine
