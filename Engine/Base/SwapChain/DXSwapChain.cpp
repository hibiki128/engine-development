#include "DXSwapChain.h"
#include "cassert"

namespace Hagine {

void DXSwapChain::Initialize(IDXGIFactory7 *factory, ID3D12CommandQueue *commandQueue, HWND hwnd, uint32_t width, uint32_t height) {
    HRESULT hr;

    // スワップチェーンの設定
    swapChainDesc_.Width = width;                                 // 画面の幅。ウィンドウのクライアント領域と同じものにしておく
    swapChainDesc_.Height = height;                               // 画面の高さ。ウィンドウのクライアント領域と同じものにしておく
    swapChainDesc_.Format = DXGI_FORMAT_R8G8B8A8_UNORM;           // 色の形式
    swapChainDesc_.SampleDesc.Count = 1;                          // マルチサンプルしない
    swapChainDesc_.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画のターゲットとして利用する
    swapChainDesc_.BufferCount = 2;                               // ダブルバッファ
    swapChainDesc_.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;    // モニタに移したら、中身を破棄

    // コマンドキュー、ウィンドウハンドル、設定を渡して生成する
    hr = factory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc_, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1 **>(swapChain_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    // スワップチェーンからバックバッファを引っ張ってくる
    backBuffers_.resize(swapChainDesc_.BufferCount);
    for (uint32_t i = 0; i < swapChainDesc_.BufferCount; ++i) {
        hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i]));
        assert(SUCCEEDED(hr));
    }
}

void DXSwapChain::Finalize() {
    backBuffers_.clear();
    swapChain_.Reset();
}

void DXSwapChain::Present() {
    swapChain_->Present(1, 0);
}
} // namespace Hagine
