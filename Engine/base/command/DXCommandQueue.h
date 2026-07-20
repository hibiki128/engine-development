#pragma once
#include "Windows.h"
#include "d3d12.h"
#include "wrl.h"
#include <cstdint>

namespace Hagine {
class DXDevice;

/// <summary>
/// コマンドキュークラス
/// ID3D12CommandQueue と完了確認用フェンスを一体で管理する
/// Direct / Compute それぞれのキューを別インスタンスとして生成して使う
/// </summary>
class DXCommandQueue
{
  public:
    DXCommandQueue() = default;
    ~DXCommandQueue();
    DXCommandQueue(const DXCommandQueue &) = delete;
    DXCommandQueue &operator=(const DXCommandQueue &) = delete;

    /// <summary>
    /// 初期化（キュー・フェンス・待機イベントの生成）
    /// </summary>
    /// <param name="device">デバイス</param>
    /// <param name="type">キューの種類（DIRECT / COMPUTE）</param>
    void Initialize(DXDevice *device, D3D12_COMMAND_LIST_TYPE type);

    /// <summary>
    /// コマンドリストをGPUに送信する
    /// </summary>
    /// <param name="commandList">実行するコマンドリスト</param>
    void Execute(ID3D12CommandList *commandList);

    /// <summary>
    /// フェンスにシグナルを発行する
    /// </summary>
    /// <returns>UINT64: 発行したフェンス値</returns>
    UINT64 Signal();

    /// <summary>
    /// 指定したフェンス値の完了をCPU側で待機する
    /// </summary>
    /// <param name="value">待機するフェンス値</param>
    void WaitForFenceCPU(UINT64 value);

    /// <summary>
    /// 他キューの最終シグナル完了をGPU側で待機する（CPUはブロックしない）
    /// </summary>
    /// <param name="other">完了を待つ相手のキュー</param>
    void WaitOnGPU(const DXCommandQueue &other);

    /// <summary>
    /// 発行済みの全シグナルの完了をCPU側で待機する
    /// </summary>
    void Flush();

    ID3D12CommandQueue *Get() const { return queue_.Get(); }
    ID3D12Fence *GetFence() const { return fence_.Get(); }
    UINT64 GetLastSignaledValue() const { return fenceCounter_; }

  private:
    // コマンドキュー
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    // 完了確認用フェンス
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    // 単調増加のフェンスカウンタ
    UINT64 fenceCounter_ = 0;
    // フェンス待機用イベント
    HANDLE fenceEvent_ = nullptr;
};
} // namespace Hagine
