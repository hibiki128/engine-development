#pragma once
#include "d3d12.h"
#include "DirectXTex/DirectXTex.h"
#include "wrl.h"
#include <cstdint>

namespace Hagine {
class DXDevice;

/// <summary>
/// GPUリソース生成クラス
/// バッファ・テクスチャ・レンダーテクスチャ・深度ステンシルなど各種リソースの生成と
/// テクスチャデータのアップロードを担当する
/// </summary>
class ResourceFactory
{
  public:
    ResourceFactory() = default;
    ~ResourceFactory() = default;
    ResourceFactory(const ResourceFactory &) = delete;
    ResourceFactory &operator=(const ResourceFactory &) = delete;

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="device">デバイス</param>
    void Initialize(DXDevice *device);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// バッファリソースを作成する
    /// </summary>
    /// <param name="sizeInBytes">バッファサイズ</param>
    /// <param name="isUAV">UAVとして使うか（trueならデフォルトヒープ）</param>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes, bool isUAV = false);

    /// <summary>
    /// テクスチャリソースを作成する
    /// </summary>
    /// <param name="metadata">テクスチャのメタデータ</param>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata &metadata);

    /// <summary>
    /// レンダーテクスチャリソースを作成する
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateRenderTextureResource(uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_CLEAR_VALUE color);

    /// <summary>
    /// 深度ステンシルリソースを作成する（DEPTH_WRITE 状態で返る）
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(int32_t width, int32_t height);

    /// <summary>
    /// テクスチャデータをGPUへ転送する
    /// </summary>
    /// <param name="texture">転送先テクスチャ</param>
    /// <param name="mipImages">転送するミップ画像</param>
    /// <param name="commandList">転送コマンドを積むコマンドリスト</param>
    /// <returns>中間リソース（転送完了まで保持が必要）</returns>
    [[nodiscard]]
    Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage &mipImages, ID3D12GraphicsCommandList *commandList);

    /// <summary>
    /// ExecuteIndirect(DispatchIndirect) 用のコマンドシグネチャを取得する
    /// 初回呼び出し時に遅延生成（未使用なら一切作られない）
    /// </summary>
    ID3D12CommandSignature *GetDispatchIndirectCommandSignature();

  private:
    // デバイス（DirectXCommon が所有）
    DXDevice *pDevice_ = nullptr;
    // DispatchIndirect 用コマンドシグネチャ（遅延生成）
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> dispatchIndirectCommandSignature_;
};
} // namespace Hagine
