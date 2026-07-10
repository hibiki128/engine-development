#pragma once
#include "d3d12.h"
#include "wrl.h"
#include <cstdint>

namespace Hagine {
class DXDevice;

/// <summary>
/// DSV（深度ステンシルビュー）管理クラス
/// DSV用デスクリプタヒープの生成と、各スロットへのDSV作成・ハンドル取得を担当する
/// スロット割り当て: 0: メイン深度 / 1: プレビュー窓用深度
/// </summary>
class DsvManager {
  public:
    // DSVの最大数
    static constexpr uint32_t kMaxDSVCount = 2;

    DsvManager() = default;
    ~DsvManager() = default;
    DsvManager(const DsvManager &) = delete;
    DsvManager &operator=(const DsvManager &) = delete;

    /// <summary>
    /// 初期化（DSV用デスクリプタヒープの生成）
    /// </summary>
    /// <param name="device">デバイス</param>
    void Initialize(DXDevice *device);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 指定スロットにDSVを作成する
    /// </summary>
    /// <param name="index">スロット番号</param>
    /// <param name="resource">対象の深度リソース</param>
    /// <param name="format">深度フォーマット</param>
    /// <returns>作成したDSVのCPUデスクリプタハンドル</returns>
    D3D12_CPU_DESCRIPTOR_HANDLE Create(uint32_t index, ID3D12Resource *resource, DXGI_FORMAT format);

    /// <summary>
    /// 指定番号のCPUデスクリプタハンドルを取得する
    /// </summary>
    /// <param name="index">スロット番号</param>
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t index) const;

    /// <summary>
    /// 指定番号のGPUデスクリプタハンドルを取得する
    /// </summary>
    /// <param name="index">スロット番号</param>
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t index) const;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> GetHeap() const { return heap_; }

  private:
    // デバイス（DirectXCommon が所有）
    DXDevice *device_ = nullptr;
    // DSV用デスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    // デスクリプタ1つ分のサイズ
    uint32_t descriptorSize_ = 0;
};
} // namespace Hagine
