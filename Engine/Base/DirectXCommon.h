#pragma once
#include "WinApp.h"
#include "chrono"
#include "d3d12.h"
#include "dxcapi.h"
#include "dxgi1_6.h"
#include "externals/DirectXTex/DirectXTex.h"
#include "string"
#include "wrl.h"
#include <type/Vector4.h>

// DirectX基盤
namespace Hagine {
class DirectXCommon {
  private:
    DirectXCommon() = default;
    ~DirectXCommon() = default;
    DirectXCommon(DirectXCommon &) = delete;
    DirectXCommon &operator=(DirectXCommon &) = delete;

  public: // メンバ関数
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns></returns>
    static DirectXCommon *GetInstance() {
        static DirectXCommon instance; // プログラム終了時に自動でデストラクタが呼ばれる
        return &instance;
    }

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(WinApp *winApp);

    /// <summary>
    /// オフスクリーンのSRV作成
    /// </summary>
    void CreateOffscreenSRV();

    /// <summary>
    /// depthのSRV作成
    /// </summary>
    void CreateDepthSRV();

    /// <summary>
    /// 描画前処理(RenderTexture)
    /// </summary>
    void PreRenderTexture();

    /// <summary>
    /// 描画前処理
    /// </summary>
    void PreDraw();

    /// <summary>
    /// 深度のバリア
    /// </summary>
    void TransitionDepthBarrier();

    /// <summary>
    /// マルチステージ用描画前処理（バックバッファ遷移なし）
    /// </summary>
    void PreDrawForEffects();

    /// <summary>
    /// 描画後処理
    /// </summary>
    void PostDraw();

    void TransitionUAVBarrier(ID3D12Resource* pResource);
    void TransitionSRVBarrier();

    IDxcBlob *CompileShader(
        // CompilerするShaderファイルへのパス
        const std::wstring &filePath,
        // Compilerに使用するProfile
        const wchar_t *profile);

    // Resourceの作成
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes, bool isUAV = false);

    // DirectX12のTextureResourceを作る
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata &metadata);

    Microsoft::WRL::ComPtr<ID3D12Resource> CreateRenderTextureResource(uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_CLEAR_VALUE color);

    // メイン深度以外の追加深度ステンシルリソースを生成する（プレビュー窓など）。DEPTH_WRITE 状態で返る。
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateAdditionalDepthResource(int32_t width, int32_t height);

    // ExecuteIndirect(DispatchIndirect) 用のコマンドシグネチャ（Phase 3 の sim 生存数ディスパッチ基盤）。
    // 引数は D3D12_DISPATCH_ARGUMENTS（ThreadGroupCountX/Y/Z の3×uint）のみ。ルート引数の差し替えは無し。
    // 生成は GetDispatchIndirectCommandSignature() の初回呼び出しで遅延生成（未使用なら一切作られない）。
    ID3D12CommandSignature *GetDispatchIndirectCommandSignature();

    [[nodiscard]]
    Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage &mipImages);

    /// <summary>
    /// バリアを貼る
    /// </summary>
    /// <param name="pResource"></param>
    /// <param name="Before"></param>
    /// <param name="After"></param>
    void BarrierTransition(ID3D12Resource *pResource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After);
    D3D12_CPU_DESCRIPTOR_HANDLE CreateAdditionalRTV(ID3D12Resource *resource, int index);
#pragma region getter
    /// <summary>
    /// RTVの指定番号のCPUデスクリプタハンドルを取得する
    /// </summary>
    /// <param name="index"></param>
    /// <returns></returns>
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTVCPUDescriptorHandle(uint32_t index);

    /// <summary>
    /// RTVの指定番号のGPUデスクリプタハンドルを取得する
    /// </summary>
    /// <param name="index"></param>
    /// <returns></returns>
    D3D12_GPU_DESCRIPTOR_HANDLE GetRTVGPUDescriptorHandle(uint32_t index);

    /// <summary>
    /// DSVの指定番号のCPUデスクリプタハンドルを取得する
    /// </summary>
    /// <param name="index"></param>
    /// <returns></returns>
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVCPUDescriptorHandle(uint32_t index);

    /// <summary>
    /// DSVの指定番号のGPUデスクリプタハンドルを取得する
    /// </summary>
    /// <param name="index"></param>
    /// <returns></returns>
    D3D12_GPU_DESCRIPTOR_HANDLE GetDSVGPUDescriptorHandle(uint32_t index);

    /// <summary>
    /// コマンドリストの取得
    /// </summary>
    /// <returns></returns>
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> GetCommandList() { return commandList_; }

    /// <summary>
    /// デバイスの取得
    /// </summary>
    /// <returns></returns>
    Microsoft::WRL::ComPtr<ID3D12Device> GetDevice() { return device_; }

    /// <summary>
    /// DescriptorHeapの作成
    /// </summary>
    /// <param name="heapType"></param>
    /// <param name="numDescriptors"></param>
    /// <param name="shaderVisible"></param>
    /// <returns></returns>
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);
    ID3D12Resource *GetOffScreenResource() { return offScreenResource_.Get(); }
    IDxcUtils *GetDxcUtils() { return dxcUtils_; }
    IDxcCompiler3 *GetDxcCompiler() { return dxcCompiler_; }

    Vector4 GetClearColor() const {
        return Vector4(
            clearColorValue_.Color[0], // R
            clearColorValue_.Color[1], // G
            clearColorValue_.Color[2], // B
            clearColorValue_.Color[3]  // A
        );
    }

    // バックバッファの数を取得
    size_t GetBackBufferCount() const { return backBuffers_.size(); }

    D3D12_GPU_DESCRIPTOR_HANDLE GetOffScreenGPUHandle() { return offScreenSrvHandleGPU_; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetOffScreenCPUHandle() { return offScreenSrvHandleCPU_; }
    uint32_t GetOffScreenSrvIndex() { return offScreenSrvIndex_; }

    D3D12_GPU_DESCRIPTOR_HANDLE GetDepthGPUHandle() { return depthSrvHandleGPU_; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDepthCPUHandle() { return depthSrvHandleCPU_; }
    uint32_t GetDepthSrvIndex() { return depthSrvIndex_; }
    D3D12_CLEAR_VALUE GetClearColorValue() const { return clearColorValue_; }
    IDXGISwapChain4 *GetSwapChain() { return swapChain_.Get(); }
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> GetRTVDescriptorHeap() { return rtvDescriptorHeap_; }

    // ---- 非同期コンピュートキュー API ----
    /// コンピュートコマンドリストを取得
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> GetComputeCommandList() { return computeCommandList_; }
    /// コンピュートコマンドをGPUに送信し完了フェンスを発行する
    void ExecuteComputeCommands();
    /// Direct Queue が Compute Queue の完了を GPU 側で待機する（CPU はブロックしない）
    void WaitForComputeOnDirectQueue();
    /// フレーム先頭でコンピュートリストにデスクリプタヒープを設定する
    void BeginComputeFrame();
    /// シャットダウン時：コンピュートキューの全作業を CPU 側で完了させる
    void FlushComputeQueue();
#pragma endregion

  private: // メンバ関数
    /// <summary>
    /// デバイスの初期化
    /// </summary>
    void DeviceInitialize();

    /// <summary>
    /// コマンド関連の初期化
    /// </summary>
    void CommandInitialize();

    /// <summary>
    /// スワップチェーンの生成
    /// </summary>
    void CreateSwapChain();

    /// <summary>
    /// 深度バッファの生成
    /// </summary>
    void CreateDepthBaffer();

    /// <summary>
    /// 各種デスクリプタヒープの生成
    /// </summary>
    void CreateVariousDesctiptorHeap();

    /// <summary>
    /// レンダーターゲットビューの初期化
    /// </summary>
    void RenderTargetViewInitialize();

    /// <summary>
    /// 深度ステンシルビューの初期化
    /// </summary>
    void DepthStencilViewInitialize();

    /// <summary>
    /// フェンス生成
    /// </summary>
    void CreateFence();

    /// <summary>
    /// 非同期コンピュートキューの初期化
    /// </summary>
    void ComputeQueueInitialize();

    /// <summary>
    /// ビューポート矩形の初期化
    /// </summary>
    void ViewPortRectInitialize();

    /// <summary>
    /// シザリング矩形の初期化
    /// </summary>
    void ScissorRectInitialize();

    /// <summary>
    /// DXCコンパイラの生成
    /// </summary>
    void CreateDXCompiler();

    /// <summary>
    /// FPS固定初期化
    /// </summary>
    void InitializeFixFPS();

    /// <summary>
    /// FPS固定更新
    /// </summary>
    void UpdateFixFPS();

    /// <summary>
    ///  DepthStencilTextureの作成
    /// </summary>
    /// <param name="device"></param>
    /// <param name="width"></param>
    /// <param name="height"></param>
    /// <returns></returns>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(Microsoft::WRL::ComPtr<ID3D12Device> device_, int32_t width, int32_t height);

    /// <summary>
    /// 指定番号のCPUデスクリプタハンドルを取得する
    /// </summary>
    /// <param name="descriptorHeap"></param>
    /// <param name="descriptorSize"></param>
    /// <param name="index"></param>
    /// <returns></returns>
    static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap, uint32_t descriptorSize, uint32_t index); // CPU

    /// <summary>
    /// 指定番号のGPUデスクリプタハンドルを取得する
    /// </summary>
    /// <param name="descriptorHeap"></param>
    /// <param name="descriptorSize"></param>
    /// <param name="index"></param>
    /// <returns></returns>
    static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap, uint32_t descriptorSize, uint32_t index); // GPU

  private:
    // WindowsAPI
    WinApp *winApp_ = nullptr;
    // DirectX12デバイス
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    // DXGIファクトリ
    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
    // コマンドキュー
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    // フレームごとのコマンドアロケータ（ダブルバッファ）
    static constexpr UINT kFrameCount = 2;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocators_[kFrameCount];
    // コマンドリスト
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    // フェンス
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

    // ---- 非同期コンピュートキュー ----
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>        computeCommandQueue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    computeCommandAllocators_[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> computeCommandList_;
    Microsoft::WRL::ComPtr<ID3D12Fence>               computeFence_;
    UINT64 computeFenceCounter_ = 0;
    bool   computeListIsOpen_  = true; // Compute コマンドリストが記録中か
    // スワップチェーン
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> backBuffers_;
    Microsoft::WRL::ComPtr<ID3D12Resource> offScreenResource_;
    D3D12_CLEAR_VALUE clearColorValue_{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;

    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;

    // Phase 3 基盤: DispatchIndirect 用コマンドシグネチャ（遅延生成）
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> dispatchIndirectCommandSignature_;

  private:
    uint32_t descriptorSizeRTV_;
    uint32_t descriptorSizeDSV_;

    // DXCコンパイラ関連
    IDxcUtils *dxcUtils_;
    IDxcCompiler3 *dxcCompiler_;

    // RTVを2つ作るのでディスクリプタを2つ用意
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[3];
    // RTV
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc_{};
    // スワップチェーン
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc_{};

    UINT64 fenceCounter_ = 0;            // 単調増加カウンタ
    UINT64 fenceValues_[kFrameCount] = {}; // フレームごとの最終 Signal 値
    UINT   frameIndex_ = 0;              // 現在の描画フレームスロット（0 or 1）
    HANDLE fenceEvent_;
    // ビューポート
    D3D12_VIEWPORT viewport_{};
    // シザー矩形
    D3D12_RECT scissorRect_{};
    // TransitionBarrierの設定
    D3D12_RESOURCE_BARRIER barrier_{};
    // 現時点ではincludeはしないが、includeに対応するための設定を行っておく
    IDxcIncludeHandler *includeHandler_;

    uint32_t offScreenSrvIndex_ = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE offScreenSrvHandleCPU_; // SRV作成時に必要なCPUハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE offScreenSrvHandleGPU_; // 描画コマンドに必要なGPUハンドル

    uint32_t depthSrvIndex_ = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE depthSrvHandleCPU_; // SRV作成時に必要なCPUハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandleGPU_; // 描画コマンドに必要なGPUハンドル

    // FPS固定用の時間計測
    std::chrono::steady_clock::time_point reference_;
    const double targetFPS_ = 60.0;
    const std::chrono::microseconds frameTime_{static_cast<uint64_t>(1000000.0 / targetFPS_)};
};
} // namespace Hagine
