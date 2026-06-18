#pragma once
#include "wrl.h"
#include <Model/ModelStructs.h>
#include <Primitive/PrimitiveModel.h>
#include <d3d12.h>

namespace Hagine {
class DirectXCommon;

/// <summary>
/// メッシュクラス
/// 頂点データとインデックスデータを管理する
/// </summary>
class Mesh {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// プリミティブ初期化
    /// </summary>
    /// <param name="type">プリミティブタイプ</param>
    void PrimitiveInitialize(const PrimitiveType &type);

    /// <summary>
    /// Getter
    /// </summary>
    MeshData &GetMeshData() { return meshData_; }
    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() { return vertexBufferView_; }
    D3D12_INDEX_BUFFER_VIEW GetIndexBufferView() { return indexBufferView_; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// 頂点データ作成
    /// </summary>
    void CreateVartexData();

    /// <summary>
    /// インデックスリソース作成
    /// </summary>
    void CreateIndexResource();

  private:
    /// ===================================================
    /// private varians
    /// ===================================================

    DirectXCommon *dxCommon_; // DirectX共通クラス
    MeshData meshData_;       // メッシュデータ

    // 頂点バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr; // バッファリソース
    VertexData *vertexData_ = nullptr;                                // データポインタ
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_;                       // バッファビュー

    // インデックスバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_ = nullptr; // バッファリソース
    uint32_t *indexData_;                                            // データポインタ
    D3D12_INDEX_BUFFER_VIEW indexBufferView_;                        // バッファビュー
};
} // namespace Hagine
