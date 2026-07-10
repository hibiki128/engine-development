#pragma once
#include "d3d12.h"
#include "wrl.h"
#include <cstdint>

namespace Hagine {
class DXDevice;

/// <summary>
/// RTV（レンダーターゲットビュー）管理クラス
/// RTV用デスクリプタヒープの生成と、各スロットへのRTV作成・ハンドル取得を担当する
/// スロット割り当て: 0,1: バックバッファ / 2: オフスクリーン / 3,4: ピンポン / 5: 最終結果 / 6: プレビュー窓用RT
/// </summary>
class RtvManager {
  public:
    // RTVの最大数
    static constexpr uint32_t kMaxRTVCount = 8;

    RtvManager() = default;
    ~RtvManager() = default;
    RtvManager(const RtvManager &) = delete;
    RtvManager &operator=(const RtvManager &) = delete;

    /// <summary>
    /// 初期化（RTV用デスクリプタヒープの生成）
    /// </summary>
    /// <param name="device">デバイス</param>
    void Initialize(DXDevice *device);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 指定スロットにRTVを作成する
    /// </summary>
    /// <param name="index">スロット番号</param>
    /// <param name="resource">対象リソース</param>
    /// <param name="desc">RTVの設定</param>
    /// <returns>作成したRTVのCPUデスクリプタハンドル</returns>
    D3D12_CPU_DESCRIPTOR_HANDLE Create(uint32_t index, ID3D12Resource *resource, const D3D12_RENDER_TARGET_VIEW_DESC &desc);

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
    // RTV用デスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    // デスクリプタ1つ分のサイズ
    uint32_t descriptorSize_ = 0;
};
} // namespace Hagine
