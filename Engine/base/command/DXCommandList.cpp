#include "DXCommandList.h"
#include "DXDevice.h"
#include "cassert"

namespace Hagine {

void DXCommandList::Initialize(DXDevice *device, D3D12_COMMAND_LIST_TYPE type)
{
    HRESULT hr;

    // コマンドアロケータをフレーム数分生成する
    for (UINT i = 0; i < kFrameCount; ++i)
    {
        hr = device->Get()->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocators_[i]));
        assert(SUCCEEDED(hr));
    }

    // コマンドリストを生成する（初期スロット0）
    // CreateCommandList は記録状態で返るのでそのまま使える
    hr = device->Get()->CreateCommandList(0, type, commandAllocators_[0].Get(), nullptr, IID_PPV_ARGS(&commandList_));
    assert(SUCCEEDED(hr));
    isOpen_ = true;
}

void DXCommandList::Close()
{
    HRESULT hr = commandList_->Close();
    assert(SUCCEEDED(hr));
    isOpen_ = false;
}

void DXCommandList::Reset(UINT frameIndex)
{
    HRESULT hr = commandAllocators_[frameIndex]->Reset();
    assert(SUCCEEDED(hr));
    hr = commandList_->Reset(commandAllocators_[frameIndex].Get(), nullptr);
    assert(SUCCEEDED(hr));
    isOpen_ = true;
}
} // namespace Hagine
