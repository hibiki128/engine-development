#pragma once
#include "Model/ModelStructs.h"
#include <cstdint>

namespace Hagine {
class DirectXCommon;
class SrvManager;

/// <summary>
/// スキニング管理クラス
/// ボーンの変形情報を頂点に適用し、スキンメッシュアニメーションを実現する
/// </summary>
class Skin {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="skeleton">スケルトンデータ</param>
    /// <param name="modelData">モデルデータ</param>
    void Initialize(const Skeleton &skeleton, const ModelData &modelData);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="skeleton">スケルトンデータ</param>
    void Update(const Skeleton &skeleton);

    /// <summary>
    /// 入力頂点データを更新
    /// </summary>
    /// <param name="modelData">モデルデータ</param>
    void UpdateInputVertices(const ModelData &modelData);

    /// <summary>
    /// スキニング実行
    /// </summary>
    /// <param name="commandList">コマンドリスト</param>
    void ExecuteSkinning(ID3D12GraphicsCommandList *commandList);

    /// <summary>
    /// メッシュの頂点オフセットを取得
    /// </summary>
    /// <param name="meshIndex">メッシュインデックス</param>
    /// <returns>size_t: 頂点オフセット</returns>
    size_t GetMeshVertexOffset(size_t meshIndex) const {
        if (meshIndex < meshVertexOffsets_.size()) {
            return meshVertexOffsets_[meshIndex];
        }
        return 0;
    }

    /// <summary>
    /// Getter
    /// </summary>
    uint32_t GetPaletteSrvIndex() { return skinClusterPaletteSrvIndex_; }
    uint32_t GetInfluenceSrvIndex() { return skinClusterInfluenceSrvIndex_; }
    uint32_t GetInputVertexSrvIndex() { return skinClusterInputVertexSrvIndex_; }
    uint32_t GetOutputVertexSrvIndex() { return skinClusterOutputVertexSrvIndex_; }
    uint32_t GetTotalVertex() { return static_cast<uint32_t>(totalVertexCount_); }
    Microsoft::WRL::ComPtr<ID3D12Resource> GetSkinningInformationResource() { return skinCluster_.skinningInformationResource; }
    ID3D12Resource *GetOutputVertexResource() { return skinCluster_.outputVertexResource.Get(); }
    D3D12_VERTEX_BUFFER_VIEW GetOutputVertexBufferView() { return skinCluster_.outputVertexBufferView; }
    SkinCluster GetSkinCluster() { return skinCluster_; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// SkinClusterの生成
    /// </summary>
    /// <param name="skeleton">スケルトンデータ</param>
    /// <param name="modelData">モデルデータ</param>
    /// <returns>SkinCluster: 生成したスキンクラスター</returns>
    SkinCluster CreateSkinCluster(const Skeleton &skeleton, const ModelData &modelData);

    /// <summary>
    /// パレットリソースの生成
    /// </summary>
    /// <param name="skinCluster">スキンクラスター</param>
    /// <param name="skeleton">スケルトンデータ</param>
    void CreatePaletteResource(SkinCluster &skinCluster, const Skeleton &skeleton);

    /// <summary>
    /// 影響度リソースの生成
    /// </summary>
    /// <param name="skinCluster">スキンクラスター</param>
    /// <param name="skeleton">スケルトンデータ</param>
    void CreateInfluenceResource(SkinCluster &skinCluster, const Skeleton &skeleton);

    /// <summary>
    /// 入力頂点リソースの生成
    /// </summary>
    /// <param name="skinCluster">スキンクラスター</param>
    /// <param name="skeleton">スケルトンデータ</param>
    void CreateInputVertexResource(SkinCluster &skinCluster, const Skeleton &skeleton);

    /// <summary>
    /// 出力頂点リソースの生成
    /// </summary>
    /// <param name="skinCluster">スキンクラスター</param>
    /// <param name="skeleton">スケルトンデータ</param>
    void CreateOutputVertexResource(SkinCluster &skinCluster, const Skeleton &skeleton);

    /// <summary>
    /// スキニング情報リソースの生成
    /// </summary>
    /// <param name="skinCluster">スキンクラスター</param>
    /// <param name="skeleton">スケルトンデータ</param>
    void CreateSkinningInformationResource(SkinCluster &skinCluster, const Skeleton &skeleton);

  private:
    /// ===================================================
    /// private varians
    /// ===================================================

    SkinCluster skinCluster_;                      // スキンクラスター
    uint32_t skinClusterPaletteSrvIndex_ = 0;      // パレットSRVインデックス
    uint32_t skinClusterInfluenceSrvIndex_ = 0;    // 影響度SRVインデックス
    uint32_t skinClusterOutputVertexSrvIndex_ = 0; // 出力頂点SRVインデックス
    uint32_t skinClusterInputVertexSrvIndex_ = 0;  // 入力頂点SRVインデックス
    size_t totalVertexCount_ = 0;                   // 総頂点数
    size_t vertexOffset_ = 0;                       // 頂点オフセット
    DirectXCommon *dxCommon_ = nullptr;            // DirectX共通クラス
    SrvManager *srvManager_ = nullptr;             // SRVマネージャー
    std::vector<size_t> meshVertexOffsets_;        // メッシュごとの頂点オフセット
};
} // namespace Hagine
