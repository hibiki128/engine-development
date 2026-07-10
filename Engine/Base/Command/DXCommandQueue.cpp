#include "DXCommandQueue.h"
#include "DXDevice.h"
#include "cassert"

namespace Hagine {

DXCommandQueue::~DXCommandQueue() {
    // フェンスイベントハンドルを閉じる
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void DXCommandQueue::Initialize(DXDevice *device, D3D12_COMMAND_LIST_TYPE type) {
    HRESULT hr;

    // コマンドキューを生成する
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = type;
    hr = device->Get()->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_));
    // コマンドキューの生成がうまくいかなかったので起動できない
    assert(SUCCEEDED(hr));

    // 初期値0でフェンスを作る
    hr = device->Get()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    assert(SUCCEEDED(hr));
    fenceCounter_ = 0;

    // フェンスのSignalを待つためのイベントを作成する
    fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(fenceEvent_ != nullptr);
}

void DXCommandQueue::Execute(ID3D12CommandList *commandList) {
    ID3D12CommandList *lists[] = {commandList};
    queue_->ExecuteCommandLists(1, lists);
}

UINT64 DXCommandQueue::Signal() {
    fenceCounter_++;
    queue_->Signal(fence_.Get(), fenceCounter_);
    return fenceCounter_;
}

void DXCommandQueue::WaitForFenceCPU(UINT64 value) {
    if (fence_->GetCompletedValue() < value) {
        fence_->SetEventOnCompletion(value, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void DXCommandQueue::WaitOnGPU(const DXCommandQueue &other) {
    // GPU側で相手キューの最終シグナル完了を待つ（CPUはブロックしない）
    queue_->Wait(other.GetFence(), other.GetLastSignaledValue());
}

void DXCommandQueue::Flush() {
    if (fenceCounter_ == 0 || !fence_) {
        return;
    }
    WaitForFenceCPU(fenceCounter_);
}
} // namespace Hagine
