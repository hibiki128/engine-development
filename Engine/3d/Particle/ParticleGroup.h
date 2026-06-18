#pragma once
#include "Model/Model.h"
#include "Primitive/PrimitiveModel.h"
#include <Model/ModelStructs.h>
#include"ParticleStruct.h"
#include"ParticleCommon.h"
#include <Transform/WorldTransform.h>
#include <list>

namespace Hagine {

/// <summary>
/// 同一モデル・マテリアルを共有するパーティクルのグループ
/// 頂点・マテリアル・インデックスの各バッファを保持し、インスタンシング描画に使う
/// </summary>
class ParticleGroup {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    /// <summary>
    /// モデルファイルからパーティクルグループを生成
    /// </summary>
    /// <param name="groupName">グループ名</param>
    /// <param name="filename">モデルファイル名</param>
    /// <param name="texturePath">テクスチャパス（省略可）</param>
    /// <returns>ParticleGroupData: 生成されたグループデータ</returns>
    ParticleGroupData CreateParticleGroup(const std::string &groupName, const std::string &filename, const std::string &texturePath = {});

    /// <summary>
    /// プリミティブ形状からパーティクルグループを生成
    /// </summary>
    /// <param name="groupName">グループ名</param>
    /// <param name="type">プリミティブの種類</param>
    /// <param name="texturePath">テクスチャパス（省略可）</param>
    /// <returns>ParticleGroupData: 生成されたグループデータ</returns>
    ParticleGroupData CreatePrimitiveParticleGroup(const std::string &groupName, PrimitiveType type, const std::string &texturePath = {});

    const std::string GetGroupName() { return particleGroupData_.groupName; }                                  // グループ名を取得
    uint32_t GetMaxInstance() { return kNumMaxInstance; }                                                      // 最大インスタンス数を取得
    ParticleGroupData &GetParticleGroupData() { return particleGroupData_; }                                   // グループデータを取得
    std::string &GetTexturePath(uint32_t index) { return particleGroupData_.materials[index].textureFilePath; } // テクスチャパスを取得
    std::string &GetModelPath() { return modelFilePath_; }                                                     // モデルパスを取得
    D3D12_VERTEX_BUFFER_VIEW &GetVertexBufferView() { return vertexBufferView_; }                              // 頂点バッファビューを取得
    D3D12_INDEX_BUFFER_VIEW &GetIndexBufferView() { return indexBufferView_; }                                 // インデックスバッファビューを取得
    ModelData GetModelData() { return modelData_; }                                                            // モデルデータを取得
    MaterialData GetMaterialData(uint32_t index) { return modelData_.materials[index]; }                       // マテリアルデータを取得
    PrimitiveType GetPrimitiveType() { return type_; }                                                         // プリミティブ種別を取得
    Microsoft::WRL::ComPtr<ID3D12Resource> GetVertexResource() { return vertexResource_; }                     // 頂点リソースを取得
    Microsoft::WRL::ComPtr<ID3D12Resource> GetmaterialResource() { return materialResource_; }                 // マテリアルリソースを取得

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// 頂点データを生成
    /// </summary>
    void CreateVertexData();

    /// <summary>
    /// マテリアルを生成
    /// </summary>
    void CreateMaterial();

    /// <summary>
    /// インデックスリソースを生成
    /// </summary>
    void CreateIndexResource();

  private:
    /// ===================================================
    /// private variants
    /// ===================================================

    static std::unordered_map<std::string, ModelData> modelCache_; // モデルのキャッシュ
    static const uint32_t kNumMaxInstance = 10000;                 // 最大インスタンス数の制限

    // 頂点バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr; // バッファリソース
    VertexData *vertexData_ = nullptr;                                // バッファ内データへのポインタ
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};                     // 頂点バッファビュー

    // マテリアルバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr; // バッファリソース
    ParticleMaterial *materialData_ = nullptr;                          // バッファ内データへのポインタ

    // インデックスバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_ = nullptr; // バッファリソース
    uint32_t *indexData_{};                                          // バッファ内データへのポインタ
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};                      // インデックスバッファビュー

    Model *model_{};                          // モデル
    ModelData modelData_{};                   // モデルデータ
    ParticleGroupData particleGroupData_{};   // パーティクルグループデータ
    PrimitiveType type_{};                    // プリミティブ種別
    std::string modelFilePath_{};             // モデルファイルパス
};
} // namespace Hagine
