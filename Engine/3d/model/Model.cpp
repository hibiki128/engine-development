#include "Model.h"
#include "frame/Frame.h"
#include "graphics/texture/TextureManager.h"
#include "object/Object3dCommon.h"
#include "fstream"
#include "MyMath.h"
#include "sstream"
#include <algorithm>
#include <debug/log/Logger.h>
#include <shadow/ShadowMap.h>
#include <skybox/SkyBox.h>

namespace Hagine {
void Model::Initialize(ModelCommon *modelCommon)
{
    pModelCommon_ = modelCommon;
    pSrvManager_ = SrvManager::GetInstance();
}

void Model::CreateModel(const std::string &directorypath, const std::string &filename)
{
    // 引数で受け取ってメンバ変数に記録する
    directorypath_ = directorypath;
    filename_ = filename;

    // モデル読み込み
    modelData_ = LoadModelFile(directorypath_, filename_);

    // メッシュ配列のサイズを調整
    meshes_.resize(modelData_.meshes.size());

    // 各メッシュの初期化
    for (size_t i = 0; i < modelData_.meshes.size(); ++i)
    {
        meshes_[i] = std::make_unique<Mesh>();
        meshes_[i]->GetMeshData() = modelData_.meshes[i];
        meshes_[i]->Initialize();
    }

    CalcLocalBounds();
}

void Model::CreatePrimitiveModel(const PrimitiveType &type, std::string texPath)
{
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

    CalcLocalBounds();
}

void Model::CreatePrimitiveModel(const PrimitiveType &type, std::string texPath, const PrimitiveParams &params)
{
    (void)texPath;
    // プリミティブモデルは単一メッシュ・単一マテリアル
    meshes_.resize(1);
    modelData_.meshes.resize(1);

    meshes_[0] = std::make_unique<Mesh>();
    meshes_[0]->PrimitiveInitialize(type, params); // 分割数・形状パラメータ反映
    meshes_[0]->Initialize();

    modelData_.meshes[0] = meshes_[0]->GetMeshData();
    modelData_.meshes[0].materialIndex = 0;

    CalcLocalBounds();
}

void Model::CreateDynamicModel(uint32_t vertexCapacity, uint32_t indexCapacity)
{
    // 動的メッシュは単一メッシュ・単一マテリアル
    meshes_.resize(1);
    modelData_.meshes.resize(1);

    meshes_[0] = std::make_unique<Mesh>();
    meshes_[0]->InitializeDynamic(vertexCapacity, indexCapacity);

    // 動的メッシュの頂点は Mesh 側が持つ。ここは materialIndex を伝えるためだけに使う
    modelData_.meshes[0] = MeshData{};
    modelData_.meshes[0].materialIndex = 0;

    CalcLocalBounds();
}

void Model::RebuildDynamicMesh(MeshData &&data)
{
    if (meshes_.empty() || !meshes_[0] || !meshes_[0]->IsDynamic())
    {
        return;
    }
    meshes_[0]->Rebuild(std::move(data));
    // 生成後の実サイズでレイ判定用の AABB を取り直す
    CalcLocalBounds();
}

void Model::CreateGpuWritableModel(uint32_t maxVertexCount)
{
    // 動的メッシュと同じく単一メッシュ・単一マテリアル
    meshes_.resize(1);
    modelData_.meshes.resize(1);

    meshes_[0] = std::make_unique<Mesh>();
    meshes_[0]->InitializeGpuWritable(maxVertexCount);

    // 頂点は GPU 側にしか無いので materialIndex を伝えるためだけに持つ
    modelData_.meshes[0] = MeshData{};
    modelData_.meshes[0].materialIndex = 0;

    // 中身が CPU から見えないぶん、境界は単位サイズ扱いになる（ギズモの選択にしか使わない）
    CalcLocalBounds();
}

ID3D12Resource *Model::GetGpuVertexResource() const
{
    if (meshes_.empty() || !meshes_[0] || !meshes_[0]->IsGpuWritable())
    {
        return nullptr;
    }
    return meshes_[0]->GetGpuVertexResource();
}

uint32_t Model::GetGpuVertexCapacity() const
{
    if (meshes_.empty() || !meshes_[0] || !meshes_[0]->IsGpuWritable())
    {
        return 0;
    }
    return meshes_[0]->GetVertexCount();
}

void Model::CalcLocalBounds()
{
    bool hasVertex = false;
    Vector3 minPoint = {0.0f, 0.0f, 0.0f};
    Vector3 maxPoint = {0.0f, 0.0f, 0.0f};

    // 動的メッシュでは modelData_ 側に頂点を持たないので、Mesh が持つ実データを見る
    for (const std::unique_ptr<Mesh> &meshPtr : meshes_)
    {
        if (!meshPtr)
        {
            continue;
        }
        for (const VertexData &vertex : meshPtr->GetMeshData().vertices)
        {
            const Vector3 position = {vertex.position.x, vertex.position.y, vertex.position.z};
            if (!hasVertex)
            {
                minPoint = position;
                maxPoint = position;
                hasVertex = true;
                continue;
            }
            minPoint.x = (std::min)(minPoint.x, position.x);
            minPoint.y = (std::min)(minPoint.y, position.y);
            minPoint.z = (std::min)(minPoint.z, position.z);
            maxPoint.x = (std::max)(maxPoint.x, position.x);
            maxPoint.y = (std::max)(maxPoint.y, position.y);
            maxPoint.z = (std::max)(maxPoint.z, position.z);
        }
    }

    if (!hasVertex)
    {
        // 頂点が取れないモデルは選択判定が消えないよう単位サイズにしておく
        localBounds_ = {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
        return;
    }

    // 板ポリのように厚みが0の軸があるとレイが当たらないので、最低限の厚みを持たせる
    constexpr float kMinHalfExtent = 0.01f;
    float halfExtent[3] = {
        (maxPoint.x - minPoint.x) * 0.5f,
        (maxPoint.y - minPoint.y) * 0.5f,
        (maxPoint.z - minPoint.z) * 0.5f};
    float center[3] = {
        (maxPoint.x + minPoint.x) * 0.5f,
        (maxPoint.y + minPoint.y) * 0.5f,
        (maxPoint.z + minPoint.z) * 0.5f};
    for (int axis = 0; axis < 3; ++axis)
    {
        halfExtent[axis] = (std::max)(halfExtent[axis], kMinHalfExtent);
    }

    localBounds_.min = {center[0] - halfExtent[0], center[1] - halfExtent[1], center[2] - halfExtent[2]};
    localBounds_.max = {center[0] + halfExtent[0], center[1] + halfExtent[1], center[2] + halfExtent[2]};
}

void Model::Update()
{
    if (isGltf_ && pAnimator_ && modelData_.hasAnimations && modelData_.hasBones)
    {
        pSkin_->UpdateInputVertices(modelData_);

        ID3D12GraphicsCommandList *pCommandList = pModelCommon_->GetDxCommon()->GetCommandList().Get();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Transition.pResource = pSkin_->GetOutputVertexResource();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        if (skinOutputInVertexState_)
        {
            // VERTEX → UAV（2フレーム目以降: 前フレームでVERTEX状態になっているので遷移）
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            pCommandList->ResourceBarrier(1, &barrier);
        }

        pSkin_->ExecuteSkinning(pCommandList);

        // UAV → VERTEX
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        pCommandList->ResourceBarrier(1, &barrier);

        skinOutputInVertexState_ = true;
    }
}

void Model::Draw(const std::vector<std::unique_ptr<Material>> &materials, std::vector<ObjColor> &color, bool lighting, bool reflect, uint32_t instanceCount)
{
    ID3D12GraphicsCommandList *pCommandList = pModelCommon_->GetDxCommon()->GetCommandList().Get();

    // 通常描画・スキニング・G-Buffer のどれで呼ばれても、
    // 今バインドされているルートシグネチャからレジスタ番号で引けばよい
    const ShaderRootSignature *rootSignature = PipelineManager::GetInstance()->GetCurrentRootSignature();
    assert(rootSignature && "モデルを描くパイプラインのルートシグネチャが未生成です");

    INT vertexOffset = 0;

    for (size_t meshIndex = 0; meshIndex < meshes_.size(); ++meshIndex)
    {
        Mesh *currentMesh = meshes_[meshIndex].get();
        uint32_t materialIndex = modelData_.meshes[meshIndex].materialIndex;
        if (materialIndex >= materials.size())
        {
            materialIndex = 0;
        }
        Material *currentMaterial = materials[materialIndex].get();

        // インデックスバッファ設定
        D3D12_INDEX_BUFFER_VIEW indexBufferView = currentMesh->GetIndexBufferView();
        pCommandList->IASetIndexBuffer(&indexBufferView);

        // 頂点バッファ設定 - アニメーション有無で使用するバッファを切り替え
        if (isGltf_ && pAnimator_ && modelData_.hasAnimations && modelData_.hasBones)
        {
            // スキニング後の頂点バッファのみを使用
            D3D12_VERTEX_BUFFER_VIEW vbv = pSkin_->GetOutputVertexBufferView();
            pCommandList->IASetVertexBuffers(0, 1, &vbv);

            // パレット情報をシェーダーに渡す（頂点シェーダーの t0）
            pSrvManager_->SetGraphicsRootDescriptorTable(
                rootSignature->GetSrvIndex(0, D3D12_SHADER_VISIBILITY_VERTEX), pSkin_->GetPaletteSrvIndex());
            vertexOffset = static_cast<INT>(pSkin_->GetMeshVertexOffset(meshIndex));
        }
        else
        {
            // 元の頂点バッファを使用
            D3D12_VERTEX_BUFFER_VIEW vbv = currentMesh->GetVertexBufferView();
            pCommandList->IASetVertexBuffers(0, 1, &vbv);
        }

        // 環境マップ（t1）
        pCommandList->SetGraphicsRootDescriptorTable(
            rootSignature->GetSrvIndex(1, D3D12_SHADER_VISIBILITY_PIXEL),
            pSrvManager_->GetGPUDescriptorHandle(SkyBox::GetInstance()->GetTextureIndex()));

        // シャドウマップ・ノーマルマップをバインド。
        // スロット番号は通常描画とスキニングで違うが、レジスタ番号は同じなので引き方は共通でよい
        {
            ShadowMap *shadowMap = ShadowMap::GetInstance();
            pSrvManager_->SetGraphicsRootDescriptorTable(
                rootSignature->GetSrvIndex(2, D3D12_SHADER_VISIBILITY_PIXEL), shadowMap->GetShadowSrvIndex());
            pCommandList->SetGraphicsRootConstantBufferView(
                rootSignature->GetCbvIndex(5, D3D12_SHADER_VISIBILITY_PIXEL), shadowMap->GetShadowDataGpuAddress());

            // ノーマルマップ SRV (t3)。未設定マテリアルでも albedo を束ねて常に有効にする
            pSrvManager_->SetGraphicsRootDescriptorTable(
                rootSignature->GetSrvIndex(3, D3D12_SHADER_VISIBILITY_PIXEL), currentMaterial->GetNormalMapIndex());
        }

        if (reflect)
        {
            // 環境係数を有効化
            currentMaterial->SetEnvironmentCoefficients(1.0f);
        }
        else
        {
            currentMaterial->SetEnvironmentCoefficients(0.0f); // 環境係数を無効化
        }

        // マテリアル描画
        currentMaterial->Draw(color[materialIndex].GetColor(), lighting);

        // 描画コール（instanceCount>1 なら同じモデルをまとめて描くインスタンシング描画）
        pCommandList->DrawIndexedInstanced(
            currentMesh->GetIndexCount(), instanceCount, 0, vertexOffset, 0);
    }
}

void Model::DrawShadow(uint32_t instanceCount)
{
    ID3D12GraphicsCommandList *pCommandList = pModelCommon_->GetDxCommon()->GetCommandList().Get();

    for (size_t meshIndex = 0; meshIndex < meshes_.size(); ++meshIndex)
    {
        Mesh *currentMesh = meshes_[meshIndex].get();

        D3D12_INDEX_BUFFER_VIEW indexBufferView = currentMesh->GetIndexBufferView();
        pCommandList->IASetIndexBuffer(&indexBufferView);

        INT vertexOffset = 0;
        if (isGltf_ && pAnimator_ && modelData_.hasAnimations && modelData_.hasBones)
        {
            D3D12_VERTEX_BUFFER_VIEW vbv = pSkin_->GetOutputVertexBufferView();
            pCommandList->IASetVertexBuffers(0, 1, &vbv);
            vertexOffset = static_cast<INT>(pSkin_->GetMeshVertexOffset(meshIndex));
            skinOutputInVertexState_ = true;
        }
        else
        {
            D3D12_VERTEX_BUFFER_VIEW vbv = currentMesh->GetVertexBufferView();
            pCommandList->IASetVertexBuffers(0, 1, &vbv);
        }

        pCommandList->DrawIndexedInstanced(
            currentMesh->GetIndexCount(), instanceCount, 0, vertexOffset, 0);
    }
}

ModelData Model::LoadModelFile(const std::string &directoryPath, const std::string &filename)
{
    ModelData modelData;

    // 拡張子に応じたisGltfフラグの設定
    isGltf_ = false;
    if (filename.size() >= 5 && filename.substr(filename.size() - 5) == ".gltf")
    {
        isGltf_ = true;
    }
    else if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".obj")
    {
        isGltf_ = false;
    }
    else
    {
        Logger::Error("Unsupported model format: \"" + filename + "\". Only .gltf and .obj are supported.");
        assert(false && "Unsupported file format");
    }

    Assimp::Importer importer;
    std::string filePath = directoryPath + "/" + filename;
    const aiScene *scene = importer.ReadFile(filePath.c_str(), aiProcess_FlipWindingOrder | aiProcess_FlipUVs);

    if (scene && scene->HasAnimations())
    {
        modelData.hasAnimations = true;
    }
    else
    {
        modelData.hasAnimations = false;
    }

    // メッシュが存在しない場合
    if (!scene || !scene->HasMeshes())
    {
        if (!scene)
        {
            // ファイルが見つからない・破損しているなど、読み込み自体に失敗したケース
            Logger::Error("Failed to load model: \"" + filePath + "\". " + importer.GetErrorString());
        }
        else
        {
            // 読み込めたがメッシュが無いケース（デフォルトメッシュで代替する）
            Logger::Warn("Model has no meshes: \"" + filePath + "\". Using a default mesh instead.");
        }
        // デフォルトのメッシュとマテリアルを作成
        MeshData defaultMesh;
        MaterialData defaultMaterial;
        defaultMaterial.textureFilePath = "debug/white1x1.png";

        modelData.meshes.push_back(defaultMesh);
        modelData.materials.push_back(defaultMaterial);
        return modelData;
    }

    // メッシュ配列のサイズを事前に確保
    modelData.meshes.resize(scene->mNumMeshes);

    // メッシュの処理
    for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
    {
        aiMesh *pMesh = scene->mMeshes[meshIndex];
        assert(pMesh->HasNormals()); // 法線がないMeshは今回は非対応（これは残す）

        if (pMesh->HasBones())
        {
            modelData.hasBones = true;
        }
        else
        {
            modelData.hasBones = false;
        }

        MeshData &currentMesh = modelData.meshes[meshIndex];
        currentMesh.vertices.resize(pMesh->mNumVertices);

        bool hasTexcoord = pMesh->HasTextureCoords(0); // Texcoordの有無を確認

        // 頂点データの処理
        for (uint32_t vertexIndex = 0; vertexIndex < pMesh->mNumVertices; ++vertexIndex)
        {
            aiVector3D &position = pMesh->mVertices[vertexIndex];
            aiVector3D &normal = pMesh->mNormals[vertexIndex];

            // 右手系→左手系変換
            currentMesh.vertices[vertexIndex].position = {-position.x, position.y, position.z, 1.0f};
            currentMesh.vertices[vertexIndex].normal = {-normal.x, normal.y, normal.z};

            if (hasTexcoord)
            {
                aiVector3D &texcoord = pMesh->mTextureCoords[0][vertexIndex];
                currentMesh.vertices[vertexIndex].texcoord = {texcoord.x, texcoord.y};
            }
            else
            {
                // Texcoord が無い場合は (0.0, 0.0) を代入
                currentMesh.vertices[vertexIndex].texcoord = {0.0f, 0.0f};
            }
        }

        // インデックスの処理
        for (uint32_t faceIndex = 0; faceIndex < pMesh->mNumFaces; ++faceIndex)
        {
            aiFace &face = pMesh->mFaces[faceIndex];
            assert(face.mNumIndices == 3); // トライアングルのみ対応
            for (uint32_t element = 0; element < face.mNumIndices; ++element)
            {
                uint32_t vertexIndex = face.mIndices[element];
                currentMesh.indices.push_back(vertexIndex);
            }
        }

        // スキニング情報の処理（各メッシュごとに）
        for (uint32_t boneIndex = 0; boneIndex < pMesh->mNumBones; ++boneIndex)
        {
            aiBone *bone = pMesh->mBones[boneIndex];
            std::string jointName = bone->mName.C_Str();

            // キーを "メッシュインデックス:ジョイント名" にしてメッシュごとに独立管理
            std::string skinKey = std::to_string(meshIndex) + ":" + jointName;

            // キーが既に存在するか確認してから挿入
            bool isNewEntry = (modelData.skinClusterData.find(skinKey) == modelData.skinClusterData.end());
            JointWeightData &jointWeightData = modelData.skinClusterData[skinKey];

            // 新規エントリの場合のみ inverseBindPoseMatrix を計算
            if (isNewEntry)
            {
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
            for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex)
            {
                uint32_t localVertexIndex = bone->mWeights[weightIndex].mVertexId;
                jointWeightData.vertexWeights.push_back({bone->mWeights[weightIndex].mWeight,
                                                         localVertexIndex,
                                                         meshIndex});
            }
        }

        // メッシュに関連するマテリアルインデックスを保存
        currentMesh.materialIndex = pMesh->mMaterialIndex;
    }

    // マテリアル配列のサイズを事前に確保
    modelData.materials.resize(scene->mNumMaterials);

    // マテリアルの処理（すべてのマテリアルを処理）
    for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex)
    {
        aiMaterial *material = scene->mMaterials[materialIndex];
        MaterialData &currentMaterial = modelData.materials[materialIndex];

        // ディフューズテクスチャの取得
        if (material->GetTextureCount(aiTextureType_DIFFUSE) != 0)
        {
            aiString textureFilePath;
            material->GetTexture(aiTextureType_DIFFUSE, 0, &textureFilePath);
            currentMaterial.textureFilePath = textureFilePath.C_Str();
        }
        else
        {
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

    if (modelData.materials.empty())
    {
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

Node Model::ReadNode(aiNode *node)
{
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
    for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
    {
        // 再帰的に読んで階層構造を作っていく
        result.children[childIndex] = ReadNode(node->mChildren[childIndex]);
    }
    return result;
}
} // namespace Hagine
