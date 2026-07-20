#pragma once
#include "d3d12.h"
#include "dxgi1_6.h"
#include "wrl.h"

namespace Hagine {

/// <summary>
/// D3D12デバイスクラス
/// DXGIファクトリの生成・アダプタ選択・デバイス生成・デバッグレイヤー設定を担当する
/// </summary>
class DXDevice
{
  public:
    DXDevice() = default;
    ~DXDevice() = default;
    DXDevice(const DXDevice &) = delete;
    DXDevice &operator=(const DXDevice &) = delete;

    /// <summary>
    /// 初期化
    /// デバッグレイヤー有効化 → ファクトリ生成 → 高性能アダプタ選択 → デバイス生成
    /// </summary>
    void Initialize();

    /// <summary>
    /// デスクリプタヒープを作成する
    /// </summary>
    /// <param name="heapType">ヒープの種類</param>
    /// <param name="numDescriptors">デスクリプタ数</param>
    /// <param name="shaderVisible">シェーダーから参照可能にするか</param>
    /// <returns>作成したデスクリプタヒープ</returns>
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);

    ID3D12Device *Get() const { return device_.Get(); }
    Microsoft::WRL::ComPtr<ID3D12Device> GetComPtr() const { return device_; }
    IDXGIFactory7 *GetFactory() const { return dxgiFactory_.Get(); }

  private:
    // DXGIファクトリ
    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
    // DirectX12デバイス
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
};
} // namespace Hagine
