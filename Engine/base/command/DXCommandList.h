#pragma once
#include "d3d12.h"
#include "wrl.h"

namespace Hagine {
class DXDevice;

/// <summary>
/// コマンドリストクラス
/// ID3D12GraphicsCommandList とフレーム数分のコマンドアロケータを管理する
/// Direct / Compute それぞれのリストを別インスタンスとして生成して使う
/// </summary>
class DXCommandList
{
  public:
    // フレームごとのコマンドアロケータ数（ダブルバッファ）
    static constexpr UINT kFrameCount = 2;

    DXCommandList() = default;
    ~DXCommandList() = default;
    DXCommandList(const DXCommandList &) = delete;
    DXCommandList &operator=(const DXCommandList &) = delete;

    /// <summary>
    /// 初期化（アロケータをフレーム数分生成し、記録状態のリストを作る）
    /// </summary>
    /// <param name="device">デバイス</param>
    /// <param name="type">リストの種類（DIRECT / COMPUTE）</param>
    void Initialize(DXDevice *device, D3D12_COMMAND_LIST_TYPE type);

    /// <summary>
    /// 記録を確定する
    /// </summary>
    void Close();

    /// <summary>
    /// 指定フレームスロットのアロケータでリセットし、記録を再開する
    /// </summary>
    /// <param name="frameIndex">フレームスロット（0 ～ kFrameCount-1）</param>
    void Reset(UINT frameIndex);

    /// <summary>
    /// 記録中かどうか
    /// </summary>
    bool IsOpen() const { return isOpen_; }

    ID3D12GraphicsCommandList *Get() const { return commandList_.Get(); }
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> GetComPtr() const { return commandList_; }

  private:
    // フレームごとのコマンドアロケータ
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocators_[kFrameCount];
    // コマンドリスト
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    // 記録中かどうか
    bool isOpen_ = false;
};
} // namespace Hagine
