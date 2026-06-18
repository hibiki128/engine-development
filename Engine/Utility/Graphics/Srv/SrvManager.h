#pragma once
#include <cstdint>
#include <d3d12.h>
#include <queue> // 空きインデックスの管理用
#include <wrl.h>
#include"externals/DirectXTex/DirectXTex.h"

namespace Hagine {
class DirectXCommon;

class SrvManager {
  private:

    SrvManager() = default;
    ~SrvManager() = default;
    SrvManager(SrvManager &) = delete;
    SrvManager &operator=(SrvManager &) = delete;

  private:
    DirectXCommon *dxCommon_ = nullptr;

    // SRV用のでスクリプタサイズ
    uint32_t descriptorSize_;
    // SRV用デスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap_;
    // 次に使用するSRVインデックス
    uint32_t useIndex_ = 0;
    // 空きインデックスを管理するキュー
    std::queue<uint32_t> freeIndices_; // 解放されたSRVインデックスを保存

  public:
    // 最大SRV数(最大テクスチャ枚数)
    static const uint32_t kMaxSRVCount;

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns></returns>
    static SrvManager* GetInstance() {
        static SrvManager instance;
        return &instance;
    }

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

  public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 描画前処理
    /// </summary>
    void SetDescriptorHeap();

    /// <summary>
    /// SRV生成(テクスチャ用)
    /// </summary>
    /// <param name="srvIndex"></param>
    /// <param name="pResource"></param>
    /// <param name="Format"></param>
    /// <param name="MipLevels"></param>
    void CreateSRVforTexture2D(uint32_t srvIndex, ID3D12Resource *pResource, DirectX::TexMetadata metaData, UINT MipLevels);

    /// <summary>
    /// SRV生成(Structured Buffer用)
    /// </summary>
    /// <param name="srvIndex"></param>
    /// <param name="pResource"></param>
    /// <param name="numElements"></param>
    /// <param name="structureByteStride"></param>
    void CreateSRVforStructuredBuffer(uint32_t srvIndex, ID3D12Resource *pResource, UINT numElements, UINT structureByteStride);

    /// <summary>
    /// SRV生成(RenderTexture用)
    /// </summary>
    /// <param name="srvIndex"></param>
    /// <param name="pResource"></param>
    /// <param name="numElements"></param>
    /// <param name="structureByteStride"></param>
    void CreateSRVforRenderTexture(uint32_t srvIndex, ID3D12Resource *pResource);

    /// <summary>
    /// SRV生成(Depth用)
    /// </summary>
    /// <param name="srvIndex"></param>
    /// <param name="pResource"></param>
    /// <param name="numElements"></param>
    /// <param name="structureByteStride"></param>
    void CreateSRVforDepth(uint32_t srvIndex, ID3D12Resource *pResource);

    /// <summary>
    /// UAV作成
    /// </summary>
    /// <param name="srvIndex"></param>
    /// <param name="pResource"></param>
    /// <param name="numElements"></param>
    /// <param name="structureByteStride"></param>
    void CreateUAVStructuredBuffer(uint32_t srvIndex, ID3D12Resource *pResource, UINT numElements, UINT structureByteStride);

    /// <summary>
    /// インデックス割り当て
    /// </summary>
    /// <returns></returns>
    uint32_t Allocate();

    /// <summary>
    /// インデックス解放
    /// </summary>
    /// <param name="srvIndex"></param>
    void Free(uint32_t srvIndex);

    bool CanAllocate() const;
    void ClearDescriptor(uint32_t srvIndex);
    /// <summary>
    /// getter
    /// </summary>
    /// <param name="index"></param>
    /// <returns></returns>
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);
    ID3D12DescriptorHeap *GetDescriptorHeap() const {
        return descriptorHeap_.Get(); // 管理してるSRVヒープ
    }

    /// <summary>
    /// setter
    /// </summary>
    /// <param name="RootParameterIndex"></param>
    /// <param name="srvIndex"></param>
    void SetGraphicsRootDescriptorTable(UINT RootParameterIndex, uint32_t srvIndex);
};
} // namespace Hagine
