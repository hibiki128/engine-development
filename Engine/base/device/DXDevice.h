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

    /// <summary>
    /// レイトレーシング用のデバイスインターフェースを取得する。
    /// 加速構造の作成・サイズ問い合わせにはこちらが要る
    /// </summary>
    /// <returns>ID3D12Device5*: 非対応環境では nullptr</returns>
    ID3D12Device5 *GetDevice5() const { return device5_.Get(); }

    /// <summary>
    /// インラインレイトレーシング（RayQuery）が使えるか。
    /// DXR Tier 1.1 以上かつシェーダーモデル 6.5 以上で使える
    /// </summary>
    /// <returns>bool: 使えるなら true</returns>
    bool IsRaytracingSupported() const { return raytracingSupported_; }

    /// <summary>
    /// 対応しているレイトレーシングの段階（表示用）
    /// </summary>
    /// <returns>D3D12_RAYTRACING_TIER: 非対応なら NOT_SUPPORTED</returns>
    D3D12_RAYTRACING_TIER GetRaytracingTier() const { return raytracingTier_; }

  private:
    /// <summary>
    /// レイトレーシングの対応状況を調べて控える
    /// </summary>
    void QueryRaytracingSupport();

    // DXGIファクトリ
    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
    // DirectX12デバイス
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    // レイトレーシング用のインターフェース（非対応環境では null のまま）
    Microsoft::WRL::ComPtr<ID3D12Device5> device5_;
    // インラインRTが使えるか
    bool raytracingSupported_ = false;
    // 対応段階
    D3D12_RAYTRACING_TIER raytracingTier_ = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
};
} // namespace Hagine
