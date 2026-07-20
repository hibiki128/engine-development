#pragma once
#include "Windows.h"
#include "d3d12.h"
#include "dxgi1_6.h"
#include "wrl.h"
#include <cstdint>
#include <vector>

namespace Hagine {

/// <summary>
/// スワップチェーンクラス
/// IDXGISwapChain4 とバックバッファの管理・Present を担当する
/// </summary>
class DXSwapChain
{
  public:
    DXSwapChain() = default;
    ~DXSwapChain() = default;
    DXSwapChain(const DXSwapChain &) = delete;
    DXSwapChain &operator=(const DXSwapChain &) = delete;

    /// <summary>
    /// 初期化（スワップチェーン生成とバックバッファの取得）
    /// </summary>
    /// <param name="factory">DXGIファクトリ</param>
    /// <param name="commandQueue">Directコマンドキュー</param>
    /// <param name="hwnd">ウィンドウハンドル</param>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void Initialize(IDXGIFactory7 *factory, ID3D12CommandQueue *commandQueue, HWND hwnd, uint32_t width, uint32_t height);

    /// <summary>
    /// 終了処理（バックバッファとスワップチェーンの解放）
    /// </summary>
    void Finalize();

    /// <summary>
    /// バックバッファをリサイズする（呼び出し前にGPUの完了待ちが必要）
    /// </summary>
    /// <param name="width">新しい幅</param>
    /// <param name="height">新しい高さ</param>
    void Resize(uint32_t width, uint32_t height);

    /// <summary>
    /// 画面を表示する（VSync 待ち）
    /// </summary>
    void Present();

    UINT GetCurrentBackBufferIndex() const { return swapChain_->GetCurrentBackBufferIndex(); }
    uint32_t GetWidth() const { return swapChainDesc_.Width; }   // 現在のバックバッファ幅
    uint32_t GetHeight() const { return swapChainDesc_.Height; } // 現在のバックバッファ高さ
    ID3D12Resource *GetBackBuffer(uint32_t index) const { return backBuffers_[index].Get(); }
    size_t GetBackBufferCount() const { return backBuffers_.size(); }
    IDXGISwapChain4 *Get() const { return swapChain_.Get(); }

  private:
    // スワップチェーン
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
    // バックバッファ
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> backBuffers_;
    // スワップチェーンの設定
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc_{};
};
} // namespace Hagine
