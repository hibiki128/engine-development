#include "Skin.h"
#include "algorithm"
#include <DirectXCommon.h>
#include <graphics/srv/SrvManager.h>
#include <cassert>
#include <cmath>
#include <MyMath.h>
#include <format>
#include <string>
#include <unordered_map>

namespace Hagine {
Skin::~Skin()
{
    if (!pSrvManager_)
    {
        return; // 初期化されていない（ボーンの無いモデル）
    }

    // 確保したのは4つ。**渡すのは予約番号なので -1 する**（+1規約）。
    // 解放は数フレーム後。GPU がまだ前のフレームでこのスロットを読んでいる最中に
    // ディスクリプタを潰すと絵が壊れるため
    for (uint32_t srvIndex : {skinClusterPaletteSrvIndex_, skinClusterInfluenceSrvIndex_,
                              skinClusterInputVertexSrvIndex_, skinClusterOutputVertexSrvIndex_})
    {
        if (srvIndex != 0)
        {
            pSrvManager_->FreeDeferred(srvIndex - 1);
        }
    }
}

void Skin::Initialize(const Skeleton &skeleton, const ModelData &modelData)
{
    pDxCommon_ = DirectXCommon::GetInstance();
    pSrvManager_ = SrvManager::GetInstance();
    // モデルデータとスケルトンに基づいて、GPUスキニングに必要なバッファ類を作成
    skinCluster_ = CreateSkinCluster(skeleton, modelData);

    // 入力頂点とウェイトは、同じモデル（ModelData の実体が同じ = Model が共有している読み込み結果）の体と共有する。
    // 体が全部消えると共有も解放され、同じ所に別のモデルが来たときは作り直す
    static std::unordered_map<std::string, std::weak_ptr<SharedInputs>> sharedCache;
    const std::string cacheKey = std::format("{}|{}|{}", static_cast<const void *>(&modelData), totalVertexCount_, skeleton.joints.size());
    std::shared_ptr<SharedInputs> shared = sharedCache[cacheKey].lock();
    if (!shared && totalVertexCount_ > 0)
    {
        shared = CreateSharedInputs(modelData);
        sharedCache[cacheKey] = shared;
    }
    sharedInputs_ = shared;
    if (sharedInputs_)
    {
        skinCluster_.inputVertexResource = sharedInputs_->inputVertices;
        skinCluster_.influenceResource = sharedInputs_->influences;
        pSrvManager_->CreateSRVforStructuredBuffer(skinClusterInputVertexSrvIndex_, skinCluster_.inputVertexResource.Get(),
                                                   UINT(totalVertexCount_), sizeof(VertexData));
        pSrvManager_->CreateSRVforStructuredBuffer(skinClusterInfluenceSrvIndex_, skinCluster_.influenceResource.Get(),
                                                   UINT(totalVertexCount_), sizeof(VertexInfluence));
    }
    // 作業場所はもう要らない
    skinCluster_.mappedInfluence = {};
    skinCluster_.mappedVertex = {};
    influenceScratch_.clear();
    influenceScratch_.shrink_to_fit();
    inputVerticesUploaded_ = true;
}

std::shared_ptr<Skin::SharedInputs> Skin::CreateSharedInputs(const ModelData &modelData) const
{
    auto shared = std::make_shared<SharedInputs>();
    // 入力頂点（全メッシュぶんを1本に並べる。描画の BaseVertexLocation と同じ並び）
    std::vector<VertexData> vertices;
    vertices.reserve(totalVertexCount_);
    for (const auto &mesh : modelData.meshes)
    {
        vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    }
    vertices.resize(totalVertexCount_);
    shared->inputVertices = pDxCommon_->CreateStaticBuffer(vertices.data(), sizeof(VertexData) * totalVertexCount_,
                                                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // ウェイト（CreateSkinCluster が作業場所に組んだ物）
    shared->influences = pDxCommon_->CreateStaticBuffer(influenceScratch_.data(), sizeof(VertexInfluence) * totalVertexCount_,
                                                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    return shared;
}

namespace {
/// <summary>
/// 法線用の逆転置行列。シェーダーは上の 3x3 しか使わないので、4x4 の逆行列を作らず
/// 3x3 の余因子行列 / 行列式で求める（関節の数×体の数だけ毎フレーム呼ぶので軽くしておく）
/// </summary>
Matrix4x4 InverseTranspose3x3(const Matrix4x4 &m)
{
    const float a = m.m[0][0], b = m.m[0][1], c = m.m[0][2];
    const float d = m.m[1][0], e = m.m[1][1], f = m.m[1][2];
    const float g = m.m[2][0], h = m.m[2][1], i = m.m[2][2];
    // 余因子（逆行列の転置 = 余因子行列 / 行列式）
    const float c00 = e * i - f * h, c01 = -(d * i - f * g), c02 = d * h - e * g;
    const float c10 = -(b * i - c * h), c11 = a * i - c * g, c12 = -(a * h - b * g);
    const float c20 = b * f - c * e, c21 = -(a * f - c * d), c22 = a * e - b * d;
    const float det = a * c00 + b * c01 + c * c02;
    const float invDet = (std::abs(det) > 1.0e-12f) ? 1.0f / det : 0.0f;
    Matrix4x4 result = MakeIdentity4x4();
    result.m[0][0] = c00 * invDet;
    result.m[0][1] = c01 * invDet;
    result.m[0][2] = c02 * invDet;
    result.m[1][0] = c10 * invDet;
    result.m[1][1] = c11 * invDet;
    result.m[1][2] = c12 * invDet;
    result.m[2][0] = c20 * invDet;
    result.m[2][1] = c21 * invDet;
    result.m[2][2] = c22 * invDet;
    return result;
}
} // namespace

void Skin::Update(const Skeleton &skeleton)
{
    // 各ジョイントについて、現在のスケルトン空間行列と逆バインドポーズ行列を掛け合わせ、
    // シェーダーが頂点変形に使える行列（Palette）を算出。
    // mappedPalette はアップロードヒープ（書き込み結合メモリ）なので、手元で作ってから1回で書く
    for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
    {
        assert(jointIndex < skinCluster_.inverseBindPoseMatrices.size());
        WellForGPU palette;
        palette.skeletonSpaceMatrix = skinCluster_.inverseBindPoseMatrices[jointIndex] * skeleton.joints[jointIndex].skeletonSpaceMatrix;
        palette.skeletonSpaceInverseTransposeMatrix = InverseTranspose3x3(palette.skeletonSpaceMatrix);
        skinCluster_.mappedPalette[jointIndex] = palette;
    }
}

void Skin::UpdateInputVertices(const ModelData &modelData)
{
    // 入力頂点はバインドポーズ（静的データ）でありスキニングでも書き換わらないため、
    // 毎フレーム転送する必要はない。マップ済みバッファへ初回だけコピーすれば十分。
    if (inputVerticesUploaded_)
    {
        return;
    }

    // モデルデータから各メッシュの頂点情報を取得し、GPU上のバッファへコピー
    size_t vertexOffset = 0;
    for (const auto &mesh : modelData.meshes)
    {
        for (size_t i = 0; i < mesh.vertices.size(); ++i)
        {
            if (vertexOffset + i < totalVertexCount_)
            {
                skinCluster_.mappedVertex[vertexOffset + i] = mesh.vertices[i];
            }
        }
        vertexOffset += mesh.vertices.size();
    }

    inputVerticesUploaded_ = true;
}

void Skin::ExecuteSkinning(ID3D12GraphicsCommandList *pCommandList)
{
    // ComputeShaderで使用するリソース（パレット、頂点、ウェイト等）をバインド
    pCommandList->SetComputeRootDescriptorTable(0, skinCluster_.paletteSrvHandle.second);
    pCommandList->SetComputeRootDescriptorTable(1, skinCluster_.inputVertexSrvHandle.second);
    pCommandList->SetComputeRootDescriptorTable(2, skinCluster_.influenceSrvHandle.second);
    pCommandList->SetComputeRootDescriptorTable(3, skinCluster_.outputVertexSrvHandle.second);
    pCommandList->SetComputeRootConstantBufferView(4,
                                                  skinCluster_.skinningInformationResource->GetGPUVirtualAddress());

    // 頂点数に応じてスレッドグループ数を計算し、スキニング計算シェーダーを実行
    uint32_t numGroups = (static_cast<uint32_t>(totalVertexCount_) + 1023) / 1024;
    pCommandList->Dispatch(numGroups, 1, 1);
}

SkinCluster Skin::CreateSkinCluster(const Skeleton &skeleton, const ModelData &modelData)
{
    SkinCluster skinCluster;

    // 全頂点数を集計し、必要なバッファサイズを確定
    for (const auto &mesh : modelData.meshes)
    {
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
    for (const auto &mesh : modelData.meshes)
    {
        meshVertexOffsets.push_back(vertexOffset_);
        vertexOffset_ += mesh.vertices.size();
    }

    // モデルデータ内のウェイト情報を解析し、頂点ごとの影響ボーンとウェイト値を設定
    for (const auto &jointWeight : modelData.skinClusterData)
    {
        const std::string &key = jointWeight.first;
        size_t colonPos = key.find(':');
        if (colonPos == std::string::npos)
            continue;

        size_t keyMeshIndex = std::stoul(key.substr(0, colonPos));
        std::string jointName = key.substr(colonPos + 1);

        auto it = skeleton.jointMap.find(jointName);
        if (it == skeleton.jointMap.end())
        {
            continue;
        }

        skinCluster.inverseBindPoseMatrices[(*it).second] = jointWeight.second.inverseBindPoseMatrix;

        for (const auto &vertexWeight : jointWeight.second.vertexWeights)
        {
            size_t meshIndex = vertexWeight.meshIndex;
            size_t localVertexIndex = vertexWeight.vertexIndex;
            if (meshIndex >= meshVertexOffsets.size())
                continue;
            size_t globalVertexIndex = meshVertexOffsets[meshIndex] + localVertexIndex;
            if (globalVertexIndex >= totalVertexCount_)
                continue;

            // 頂点に影響を与えるボーンのインデックスと重みを書き込む
            auto &currentInfluence = skinCluster.mappedInfluence[globalVertexIndex];
            for (uint32_t index = 0; index < kNumMaxInfluence; ++index)
            {
                if (currentInfluence.weights[index] == 0.0f)
                {
                    currentInfluence.weights[index] = vertexWeight.weight;
                    currentInfluence.jointIndices[index] = (*it).second;
                    break;
                }
            }
        }
    }

    meshVertexOffsets_.clear();
    size_t offset = 0;
    for (const auto &mesh : modelData.meshes)
    {
        meshVertexOffsets_.push_back(offset);
        offset += mesh.vertices.size();
    }

    return skinCluster;
}

void Skin::CreatePaletteResource(SkinCluster &skinCluster, const Skeleton &skeleton)
{
    // ボーン行列パレット用のバッファ生成とSRV登録
    skinCluster.paletteResource = pDxCommon_->CreateBufferResource(sizeof(WellForGPU) * skeleton.joints.size());
    WellForGPU *mappedPalette = nullptr;
    skinCluster.paletteResource->Map(0, nullptr, reinterpret_cast<void **>(&mappedPalette));
    skinCluster.mappedPalette = {mappedPalette, skeleton.joints.size()};
    skinClusterPaletteSrvIndex_ = pSrvManager_->Allocate() + 1;
    skinCluster.paletteSrvHandle.first = pSrvManager_->GetCPUDescriptorHandle(skinClusterPaletteSrvIndex_);
    skinCluster.paletteSrvHandle.second = pSrvManager_->GetGPUDescriptorHandle(skinClusterPaletteSrvIndex_);

    pSrvManager_->CreateSRVforStructuredBuffer(skinClusterPaletteSrvIndex_, skinCluster.paletteResource.Get(), UINT(skeleton.joints.size()), sizeof(WellForGPU));
}

void Skin::CreateInfluenceResource(SkinCluster &skinCluster, const Skeleton &skeleton)
{
    // 頂点ごとのボーン影響データは、まず CPU の作業場所に組む（GPU のバッファは同じモデルの体と共有する）
    influenceScratch_.assign(totalVertexCount_, VertexInfluence{});
    skinCluster.mappedInfluence = {influenceScratch_.data(), totalVertexCount_};
    skinClusterInfluenceSrvIndex_ = pSrvManager_->Allocate() + 1;
    skinCluster.influenceSrvHandle.first = pSrvManager_->GetCPUDescriptorHandle(skinClusterInfluenceSrvIndex_);
    skinCluster.influenceSrvHandle.second = pSrvManager_->GetGPUDescriptorHandle(skinClusterInfluenceSrvIndex_);
}

void Skin::CreateInputVertexResource(SkinCluster &skinCluster, const Skeleton &skeleton)
{
    // スキニング前の入力頂点は同じモデルの体と共有する（Initialize で割り当てる）ので、ここではSRVの枠だけ取る
    skinClusterInputVertexSrvIndex_ = pSrvManager_->Allocate() + 1;
    skinCluster.inputVertexSrvHandle.first = pSrvManager_->GetCPUDescriptorHandle(skinClusterInputVertexSrvIndex_);
    skinCluster.inputVertexSrvHandle.second = pSrvManager_->GetGPUDescriptorHandle(skinClusterInputVertexSrvIndex_);
}

void Skin::CreateOutputVertexResource(SkinCluster &skinCluster, const Skeleton &skeleton)
{
    // スキニング後の出力頂点バッファ（UAV）生成とVBV設定
    skinCluster.outputVertexResource = pDxCommon_->CreateBufferResource(sizeof(VertexData) * totalVertexCount_, true);
    skinClusterOutputVertexSrvIndex_ = pSrvManager_->Allocate() + 1;
    skinCluster.outputVertexSrvHandle.first = pSrvManager_->GetCPUDescriptorHandle(skinClusterOutputVertexSrvIndex_);
    skinCluster.outputVertexSrvHandle.second = pSrvManager_->GetGPUDescriptorHandle(skinClusterOutputVertexSrvIndex_);

    pSrvManager_->CreateUAVStructuredBuffer(skinClusterOutputVertexSrvIndex_, skinCluster.outputVertexResource.Get(), static_cast<uint32_t>(totalVertexCount_), sizeof(VertexData));

    skinCluster.outputVertexBufferView.BufferLocation = skinCluster.outputVertexResource->GetGPUVirtualAddress();
    skinCluster.outputVertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * totalVertexCount_);
    skinCluster.outputVertexBufferView.StrideInBytes = sizeof(VertexData);
}

void Skin::CreateSkinningInformationResource(SkinCluster &skinCluster, const Skeleton &skeleton)
{
    // スキニングに必要な定数情報（頂点数など）を格納するバッファ生成
    skinCluster.skinningInformationResource = pDxCommon_->CreateBufferResource(sizeof(SkinningInformationForGPU));
    skinCluster.SkinningInformationData = nullptr;
    skinCluster.skinningInformationResource->Map(0, nullptr, reinterpret_cast<void **>(&skinCluster.SkinningInformationData));
    skinCluster.SkinningInformationData->numVertices = static_cast<uint32_t>(totalVertexCount_);
}

} // namespace Hagine
