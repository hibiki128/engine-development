#pragma once
#include "wrl.h"
#include <model/ModelStructs.h>
#include <primitive/PrimitiveModel.h>
#include <d3d12.h>

namespace Hagine {
class DirectXCommon;

/// <summary>
/// メッシュクラス
/// 頂点データとインデックスデータを管理する
///
/// 静的メッシュ（モデル読み込み・プリミティブ）は Initialize() で
/// 頂点数ちょうどのバッファを 1 つ作る。
/// メタボールのように毎回頂点数が変わるものは InitializeDynamic() で
/// 容量を確保しておき、以後 Rebuild() で中身を差し替える。
/// </summary>
class Mesh
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化（静的メッシュ。meshData_ の頂点数ちょうどのバッファを作る）
    /// </summary>
    void Initialize();

    /// <summary>
    /// 動的メッシュとして初期化する。
    /// 指定容量のバッファをフレーム数ぶん確保し、以後 Rebuild() で中身だけ差し替える。
    /// </summary>
    /// <param name="vertexCapacity">確保する頂点数</param>
    /// <param name="indexCapacity">確保するインデックス数</param>
    void InitializeDynamic(uint32_t vertexCapacity, uint32_t indexCapacity);

    /// <summary>
    /// 動的メッシュの中身を差し替える。容量が足りなければ確保し直す。
    /// 書き込み先はフレームごとに切り替わるので、1 フレームに 1 回まで呼べる。
    /// </summary>
    /// <param name="data">新しいメッシュデータ（ムーブされる）</param>
    void Rebuild(MeshData &&data);

    /// <summary>
    /// GPU が中身を書くメッシュとして初期化する。
    ///
    /// 頂点バッファを DEFAULT ヒープ（UAV 付き）で作り、コンピュートシェーダーが
    /// 直接書き込む。インデックスは 0,1,2... の固定並びで、描画数は常に容量ぶん。
    /// 書かれなかった頂点は面積 0 の三角形になり、ラスタライザが捨てるので何も映らない。
    /// （メタボールのように、形が毎フレーム GPU 上で決まるもの向け）
    /// </summary>
    /// <param name="maxVertexCount">確保する頂点数の上限（3の倍数へ切り下げられる）</param>
    void InitializeGpuWritable(uint32_t maxVertexCount);

    /// <summary>
    /// プリミティブ初期化
    /// </summary>
    /// <param name="type">プリミティブタイプ</param>
    void PrimitiveInitialize(const PrimitiveType &type);

    /// <summary>
    /// プリミティブ初期化（分割数・形状パラメータ指定版）
    /// </summary>
    void PrimitiveInitialize(const PrimitiveType &type, const PrimitiveParams &params);

    /// <summary>
    /// Getter
    /// </summary>
    MeshData &GetMeshData() { return meshData_; }
    const MeshData &GetMeshData() const { return meshData_; }
    D3D12_VERTEX_BUFFER_VIEW GetVertexBufferView() { return vertexBufferView_; }
    D3D12_INDEX_BUFFER_VIEW GetIndexBufferView() { return indexBufferView_; }
    /// 実際に描画すべきインデックス数。動的メッシュでは容量ではなく現在の中身の数を返す
    uint32_t GetIndexCount() const { return indexCount_; }
    uint32_t GetVertexCount() const { return vertexCount_; }
    bool IsDynamic() const { return isDynamic_; }

    /// GPU が書き込むメッシュか
    bool IsGpuWritable() const { return isGpuWritable_; }
    /// コンピュートシェーダーの書き込み先（UAV を張るリソース）
    ID3D12Resource *GetGpuVertexResource() const { return vertexResource_.Get(); }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// 頂点データ作成
    /// </summary>
    void CreateVertexData();

    /// <summary>
    /// インデックスリソース作成
    /// </summary>
    void CreateIndexResource();

    /// <summary>
    /// 動的メッシュ用のバッファを確保し直す（既存の中身は破棄される）
    /// </summary>
    void AllocateDynamicBuffers(uint32_t vertexCapacity, uint32_t indexCapacity);

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    /// 描画中のフレームと書き込み中のフレームがぶつからないよう、
    /// DXCommandList::kFrameCount と同じ数だけバッファを持って交互に使う
    static constexpr uint32_t kDynamicBufferCount = 2;

    DirectXCommon *pDxCommon_ = nullptr; // DirectX共通クラス
    MeshData meshData_;                  // メッシュデータ

    uint32_t vertexCount_ = 0; // 現在の頂点数
    uint32_t indexCount_ = 0;  // 現在のインデックス数

    // 頂点バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr; // バッファリソース
    VertexData *pVertexData_ = nullptr;                               // データポインタ
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};                     // バッファビュー

    // インデックスバッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_ = nullptr; // バッファリソース
    uint32_t *pIndexData_ = nullptr;                                 // データポインタ
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};                      // バッファビュー

    // 動的メッシュ用
    bool isDynamic_ = false;         // 動的メッシュか
    bool isGpuWritable_ = false;     // GPU が中身を書くメッシュか
    uint32_t dynamicSlot_ = 0;       // 次に書き込むスロット
    uint32_t vertexCapacity_ = 0;    // 確保済みの頂点数
    uint32_t indexCapacity_ = 0;     // 確保済みのインデックス数
    Microsoft::WRL::ComPtr<ID3D12Resource> dynamicVertexResource_[kDynamicBufferCount];
    VertexData *pDynamicVertexData_[kDynamicBufferCount] = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> dynamicIndexResource_[kDynamicBufferCount];
    uint32_t *pDynamicIndexData_[kDynamicBufferCount] = {};
};
} // namespace Hagine
