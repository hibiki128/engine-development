#include "DirectXCommon.h"
#include "cassert"
#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#include "format"
#include "thread"
#include <Debug/Log/Logger.h>
#include <Graphics/Srv/SrvManager.h>
#include <String/StringUtility.h>
namespace Hagine {
using namespace Logger;
using namespace StringUtility;

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")

using namespace Microsoft::WRL;

void DirectXCommon::Finalize() {
    // フェンスイベントハンドルを閉じる
    CloseHandle(fenceEvent_);

    // DXCコンパイラ関連の生ポインタを解放
    // ComPtrではなく生ポインタで保持しているため手動でReleaseが必要
    if (includeHandler_) {
        includeHandler_->Release();
        includeHandler_ = nullptr;
    }
    if (dxcCompiler_) {
        dxcCompiler_->Release();
        dxcCompiler_ = nullptr;
    }
    if (dxcUtils_) {
        dxcUtils_->Release();
        dxcUtils_ = nullptr;
    }

    // オフスクリーン・深度リソースの解放
    offScreenResource_.Reset();
    depthStencilResource_.Reset();

    // デスクリプタヒープの解放
    rtvDescriptorHeap_.Reset();
    dsvDescriptorHeap_.Reset();

    // バックバッファの解放
    backBuffers_.clear();

    // コンピュートキューの完了を待ってから解放
    FlushComputeQueue();
    computeCommandList_.Reset();
    for (auto &a : computeCommandAllocators_) a.Reset();
    computeFence_.Reset();
    computeCommandQueue_.Reset();

    // コマンド関連の解放
    commandList_.Reset();
    for (auto &allocator : commandAllocators_) {
        allocator.Reset();
    }
    commandQueue_.Reset();

    // フェンスの解放
    fence_.Reset();

    // スワップチェーンの解放
    swapChain_.Reset();

    // DXGIファクトリの解放
    dxgiFactory_.Reset();

    // デバイスは全リソース解放後に最後に解放する
    device_.Reset();
}

void DirectXCommon::Initialize(WinApp *winApp) {

    // NULL検出
    assert(winApp);

    // メンバ変数に記録
    this->winApp_ = winApp;

    // FPS固定初期化
    InitializeFixFPS();

    // デバイスの生成
    DeviceInitialize();
    // コマンド関連の初期化
    CommandInitialize();
    // スワップチェーンの生成
    CreateSwapChain();
    // 深度バッファの生成
    CreateDepthBaffer();
    // 各種デスクリプタヒープの生成
    CreateVariousDesctiptorHeap();
    // レンダーターゲットビューの初期化
    RenderTargetViewInitialize();
    // 深度ステンシルビューの初期化
    DepthStencilViewInitialize();
    // フェンスの初期化
    CreateFence();
    // ビューポート矩形の初期化
    ViewPortRectInitialize();
    // シザリング矩形の初期化
    ScissorRectInitialize();
    // DXCコンパイラの生成
    CreateDXCompiler();
    // 非同期コンピュートキューの初期化
    ComputeQueueInitialize();
}

void DirectXCommon::CreateOffscreenSRV() {
    offScreenSrvIndex_ = SrvManager::GetInstance()->Allocate();
    SrvManager::GetInstance()->CreateSRVforRenderTexture(offScreenSrvIndex_, offScreenResource_.Get());
    offScreenSrvHandleCPU_ = SrvManager::GetInstance()->GetCPUDescriptorHandle(offScreenSrvIndex_);
    offScreenSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(offScreenSrvIndex_);
}

void DirectXCommon::CreateDepthSRV() {
    depthSrvIndex_ = SrvManager::GetInstance()->Allocate();
    SrvManager::GetInstance()->CreateSRVforDepth(depthSrvIndex_, depthStencilResource_.Get());
    depthSrvHandleCPU_ = SrvManager::GetInstance()->GetCPUDescriptorHandle(depthSrvIndex_);
    depthSrvHandleGPU_ = SrvManager::GetInstance()->GetGPUDescriptorHandle(depthSrvIndex_);
}

void DirectXCommon::PreRenderTexture() {
    BarrierTransition(offScreenResource_.Get(),
                      D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);

    // 描画先のRTVとDSVを設定する
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetDSVCPUDescriptorHandle(0);
    commandList_->OMSetRenderTargets(1, &rtvHandles_[2], false, &dsvHandle);
    commandList_->ClearRenderTargetView(rtvHandles_[2], clearColorValue_.Color, 0, nullptr);
    // 指定した深度で画面全体をクリアする
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    commandList_->RSSetViewports(1, &viewport_);       // Viewportを設定
    commandList_->RSSetScissorRects(1, &scissorRect_); // Scissorを設定
}

void DirectXCommon::PreDraw() {
    // 深度リソースをピクセルシェーダーリソースとして読み取る準備
    BarrierTransition(depthStencilResource_.Get(),
                      D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    BarrierTransition(offScreenResource_.Get(),
                      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);

    // ゲームの処理
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
    BarrierTransition(backBuffers_[backBufferIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetDSVCPUDescriptorHandle(0);
    commandList_->OMSetRenderTargets(1, &rtvHandles_[backBufferIndex], false, &dsvHandle);
    commandList_->ClearRenderTargetView(rtvHandles_[backBufferIndex], clearColorValue_.Color, 0, nullptr);

    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissorRect_);
}

void DirectXCommon::TransitionDepthBarrier() {
    BarrierTransition(depthStencilResource_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void DirectXCommon::PreDrawForEffects() {
    // マルチステージ用: バックバッファは既に遷移済みなので深度とオフスクリーンのみ遷移
    BarrierTransition(depthStencilResource_.Get(),
                      D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    BarrierTransition(offScreenResource_.Get(),
                      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ);
    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissorRect_);
}

void DirectXCommon::PostDraw() {
    HRESULT hr;

    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

    // バックバッファを PRESENT 状態に遷移
    BarrierTransition(backBuffers_[backBufferIndex].Get(),
                      D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    // コマンドリストを確定してGPUに送信
    hr = commandList_->Close();
    assert(SUCCEEDED(hr));
    Microsoft::WRL::ComPtr<ID3D12CommandList> commandLists[] = {commandList_};
    commandQueue_->ExecuteCommandLists(1, commandLists->GetAddressOf());

    // 画面を表示（VSync 待ち）
    swapChain_->Present(1, 0);

    // ---- ダブルバッファフェンス管理 ----
    // 現フレームスロットに完了シグナルを送る
    fenceCounter_++;
    fenceValues_[frameIndex_] = fenceCounter_;
    commandQueue_->Signal(fence_.Get(), fenceValues_[frameIndex_]);

    // FPS固定（VSync後の余剰時間を調整）
    UpdateFixFPS();

    // 次フレームスロットに切り替える
    frameIndex_ = (frameIndex_ + 1) % kFrameCount;

    // 次スロットの前回使用分が GPU で完了するまで待つ
    // （同じアロケータを 2 フレーム後に安全に再利用するため）
    if (fence_->GetCompletedValue() < fenceValues_[frameIndex_]) {
        fence_->SetEventOnCompletion(fenceValues_[frameIndex_], fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }

    // 次フレームのコマンドアロケータ・リストをリセット
    hr = commandAllocators_[frameIndex_]->Reset();
    assert(SUCCEEDED(hr));
    hr = commandList_->Reset(commandAllocators_[frameIndex_].Get(), nullptr);
    assert(SUCCEEDED(hr));

    // Compute 側も同スロットをリセット（Direct の完了が Compute 完了を含意するため安全）
    // このフレームでパーティクルが1つも描画されなかった場合はリストが Open のままなので先に Close する
    if (computeListIsOpen_) {
        computeCommandList_->Close();
        computeListIsOpen_ = false;
    }
    hr = computeCommandAllocators_[frameIndex_]->Reset();
    assert(SUCCEEDED(hr));
    hr = computeCommandList_->Reset(computeCommandAllocators_[frameIndex_].Get(), nullptr);
    assert(SUCCEEDED(hr));
    computeListIsOpen_ = true;
}

void DirectXCommon::TransitionUAVBarrier(ID3D12Resource *pResource) {
    barrier_.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier_.Transition.pResource = pResource;
    barrier_.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrier_.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commandList_->ResourceBarrier(1, &barrier_);
}

void DirectXCommon::TransitionSRVBarrier() {
    barrier_.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier_.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    commandList_->ResourceBarrier(1, &barrier_);
}

void DirectXCommon::DeviceInitialize() {

#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        // デバッグレイヤーを有効化する
        debugController->EnableDebugLayer();
        // さらにGPU側でもチェックを行うようにする
        debugController->SetEnableGPUBasedValidation(TRUE);
    }
#endif

    //	//DXGIファクトリーの生成
    // HRESULTはwindows系のエラーコード
    // 関数が成功したかどうかをSUCCEEDEDマクロで判定できる
    HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory_));
    // 初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、どうにもできない場合が多いのでassertにしておく
    assert(SUCCEEDED(hr));

    // 使用するアダプタ用の変数。最初にnullptrを入れておく
    Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;
    // 良い順にアダプタを頼む
    for (UINT i = 0; dxgiFactory_->EnumAdapterByGpuPreference(i,
                                                             DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
                     DXGI_ERROR_NOT_FOUND;
         ++i) {
        // アダプターの情報を取得する
        DXGI_ADAPTER_DESC3 adapterDesc{};
        hr = useAdapter->GetDesc3(&adapterDesc);
        assert(SUCCEEDED(hr)); // 取得できないのは一大事
        // ソフトウェアアダプタでなければ採用！
        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
            // 採用したアダプタ情報をログに出力。wstringの方なので注意
            Log(ConvertString(std::format(L"Use Adapter:{}\n", adapterDesc.Description)));
            break;
        }
        useAdapter = nullptr; // ソフトウェアアダプタの場合は見なかったことにする
    }
    // 適切なアダプタが見つからなかったので起動できない
    assert(useAdapter != nullptr);

    // 機能レベルとログ出力用の文字列
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0};
    const char *fetureLevelStrings[] = {"12.2", "12.1", "12.0"};
    // 高い順に生成できるか試していく
    for (size_t i = 0; i < _countof(featureLevels); ++i) {
        // 採用したアダプターでデバイスを生成
        hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device_));
        // 指定した機能レベルでデバイスが生成できたかを確認
        if (SUCCEEDED(hr)) {
            // 生成できたのでログ出力を行ってループを抜ける
            Log(std::format("FeatureLevel : {}\n", fetureLevelStrings[i]));
            break;
        }
    }
    // デバイスの生成がうまくいかなかったので起動できない
    assert(device_ != nullptr);
    Log("Complete create D3D12Device!!!\n");
#ifdef _DEBUG
    ID3D12InfoQueue *infoQueue = nullptr;
    if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        // やばいエラー時に止まる
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        // エラー時に止まる
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        //// 警告時に止まる
        // infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
        //  抑制するメッセージのID
        D3D12_MESSAGE_ID denyIds[] = {
            D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE};
        // 抑制するレベル
        D3D12_MESSAGE_SEVERITY severities[] = {D3D12_MESSAGE_SEVERITY_INFO};
        D3D12_INFO_QUEUE_FILTER filter{};
        filter.DenyList.NumIDs = _countof(denyIds);
        filter.DenyList.pIDList = denyIds;
        filter.DenyList.NumSeverities = _countof(severities);
        filter.DenyList.pSeverityList = severities;
        // 指定したメッセージの表示を抑制する
        infoQueue->PushStorageFilter(&filter);

        // 解放
        infoQueue->Release();
    }
#endif
}

void DirectXCommon::CommandInitialize() {
    HRESULT hr;
    ///===========コマンドキューを生成する==============
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
    hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
    // コマンドキューの生成がうまくいかなかったので起動できない
    assert(SUCCEEDED(hr));
    ///============================================

    ///============コマンドアロケータをフレーム数分生成する===========
    for (UINT i = 0; i < kFrameCount; ++i) {
        hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators_[i]));
        assert(SUCCEEDED(hr));
    }
    ///=============================================================

    ///============コマンドリストを生成する=============
    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators_[0].Get(), nullptr, IID_PPV_ARGS(&commandList_));
    // コマンドリストを生成する
    assert(SUCCEEDED(hr));
    ///=============================================
}

void DirectXCommon::CreateSwapChain() {
    HRESULT hr;
    ///==========スワップチェーンを生成する=============
    swapChainDesc_.Width = WinApp::kClientWidth;                  // 画面の幅。ウィンドウのクライアント領域を同じものにしておく
    swapChainDesc_.Height = WinApp::kClientHeight;                // 　画面の高さ。ウィンドウのクライアント領域を同じものにしておく
    swapChainDesc_.Format = DXGI_FORMAT_R8G8B8A8_UNORM;           // 色の形式
    swapChainDesc_.SampleDesc.Count = 1;                          // マルチサンプルしない
    swapChainDesc_.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画のターゲットとして利用する
    swapChainDesc_.BufferCount = 2;                               // 　ダブルバッファ
    swapChainDesc_.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;    // モニタに移したら、中身を破棄
    ///=================================================

    ///========コマンドキュー、ウィンドウハンドル、設定を渡して生成する=============
    hr = dxgiFactory_->CreateSwapChainForHwnd(commandQueue_.Get(), winApp_->GetHwnd(), &swapChainDesc_, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1 **>(swapChain_.GetAddressOf()));
    assert(SUCCEEDED(hr));
    ///=================================================
}

void DirectXCommon::CreateDepthBaffer() {
    depthStencilResource_ = CreateDepthStencilTextureResource(device_, WinApp::kClientWidth, WinApp::kClientHeight);
}

void DirectXCommon::CreateVariousDesctiptorHeap() {
    ///==========DescriptorSizeの取得==========
    descriptorSizeRTV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    descriptorSizeDSV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    ///=======================================

    ///==========ディスクリプタヒープの生成=============
    // RTV用のヒープ。RTVはShader内で触るものではないが、ShaderVisibleはfalse
    //   slot 0,1: バックバッファ / 2: オフスクリーン / 3,4: ピンポン / 5: 最終結果
    //   slot 6  : GPUパーティクル プレビュー窓用 RT (Phase 8) ← 拡張で確保
    rtvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 8, false);
    // DSV用のヒープ。DSVはShader内で触るものではないので、ShaderVisibleはfalse
    //   slot 0: メイン深度 / slot 1: プレビュー窓用 深度 (Phase 8) ← 拡張で確保
    dsvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false);
    ///=================================================
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::CreateAdditionalRTV(ID3D12Resource *resource, int index) {
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc_{};
    rtvDesc_.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc_.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = GetRTVCPUDescriptorHandle(3 + index);
    device_->CreateRenderTargetView(resource, &rtvDesc_, rtvHandle);
    return rtvHandle;
}

void DirectXCommon::RenderTargetViewInitialize() {
    HRESULT hr;

    // backBuffersのサイズを2に確保
    backBuffers_.resize(2);

    // SwapChainからResourceを引っ張ってくる
    hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffers_[0]));
    assert(SUCCEEDED(hr));
    hr = swapChain_->GetBuffer(1, IID_PPV_ARGS(&backBuffers_[1]));
    assert(SUCCEEDED(hr));

    //=================バックバッファ用のRTVの設定======================
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc_{};
    rtvDesc_.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc_.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = GetCPUDescriptorHandle(rtvDescriptorHeap_, descriptorSizeRTV_, 0);

    // まず一つ目を作る。一つ目は最初のところに作る。作る場所をこちらで指定してあげる必要がある
    rtvHandles_[0] = rtvStartHandle;
    device_->CreateRenderTargetView(backBuffers_[0].Get(), &rtvDesc_, rtvHandles_[0]);
    // 2つ目のディスクリプタハンドルを得る
    rtvHandles_[1].ptr = rtvHandles_[0].ptr + device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    // 2つ目を作る
    device_->CreateRenderTargetView(backBuffers_[1].Get(), &rtvDesc_, rtvHandles_[1]);
    //==============================================================

    //=================RenderTextureResource用のRTVの設定======================
    // RenderTextureResourceの作成
    clearColorValue_.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    clearColorValue_.Color[0] = 0.1f;  // 赤成分 (非常に暗い)
    clearColorValue_.Color[1] = 0.25f; // 緑成分 (非常に暗い)
    clearColorValue_.Color[2] = 0.5f;  // 青成分 (少し強め)
    clearColorValue_.Color[3] = 1.0f;  // アルファ値 (完全な不透明)
    // clearColorValue.Color[0] = 0.02f;  // 赤成分 (非常に暗い)
    // clearColorValue.Color[1] = 0.02f;  // 緑成分 (非常に暗い)
    // clearColorValue.Color[2] = 0.05f;  // 青成分 (少し強め)
    // clearColorValue.Color[3] = 1.0f;   // アルファ値 (完全な不透明)
    /*0.1f, 0.25f, 0.5f, 1.0f*/
    offScreenResource_ = CreateRenderTextureResource(WinApp::kClientWidth, WinApp::kClientHeight, clearColorValue_.Format, clearColorValue_);

    rtvHandles_[2].ptr = rtvHandles_[1].ptr + device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // RTVの作成
    device_->CreateRenderTargetView(offScreenResource_.Get(), &rtvDesc_, rtvHandles_[2]);
    //==============================================================
}

void DirectXCommon::DepthStencilViewInitialize() {
    ///=================DSVの設定=======================
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;        // Format。基本的にはResourceに合わせる
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2dTexture

    // DSVHeapの先頭にDSVを作る
    device_->CreateDepthStencilView(depthStencilResource_.Get(), &dsvDesc, dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());
    ///=================================================
}

void DirectXCommon::CreateFence() {
    HRESULT hr;
    ///============初期値0でFenceを作る================
    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    assert(SUCCEEDED(hr));
    // fenceValues と fenceCounter_ を 0 で初期化
    fenceCounter_ = 0;
    for (auto &v : fenceValues_) v = 0;
    // FenceのSignalを待つためのイベントを作成する
    fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
    assert(fenceEvent_ != nullptr);
    ///==================================================
}

void DirectXCommon::ComputeQueueInitialize() {
    HRESULT hr;

    // コンピュートキュー作成
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&computeCommandQueue_));
    assert(SUCCEEDED(hr));

    // フレーム数分のコンピュートアロケータ作成
    for (UINT i = 0; i < kFrameCount; ++i) {
        hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                            IID_PPV_ARGS(&computeCommandAllocators_[i]));
        assert(SUCCEEDED(hr));
    }

    // コンピュートコマンドリスト作成（初期スロット0）
    // CreateCommandList は記録状態で返るのでそのまま使える
    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                   computeCommandAllocators_[0].Get(), nullptr,
                                   IID_PPV_ARGS(&computeCommandList_));
    assert(SUCCEEDED(hr));
    computeListIsOpen_ = true;

    // コンピュート用フェンス
    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&computeFence_));
    assert(SUCCEEDED(hr));
    computeFenceCounter_ = 0;
}

void DirectXCommon::BeginComputeFrame() {
    // 同フレーム内で前のエミッターが既に Close+Execute 済みの場合
    // → CPU 側で完了を待ち、アロケータをリセットして再オープンする
    if (!computeListIsOpen_) {
        if (computeFence_->GetCompletedValue() < computeFenceCounter_) {
            HANDLE ev = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            computeFence_->SetEventOnCompletion(computeFenceCounter_, ev);
            WaitForSingleObject(ev, INFINITE);
            CloseHandle(ev);
        }
        HRESULT hr = computeCommandAllocators_[frameIndex_]->Reset();
        assert(SUCCEEDED(hr));
        hr = computeCommandList_->Reset(computeCommandAllocators_[frameIndex_].Get(), nullptr);
        assert(SUCCEEDED(hr));
        computeListIsOpen_ = true;
    }

    // デスクリプタヒープをコンピュートコマンドリストに設定
    ID3D12DescriptorHeap *heaps[] = {SrvManager::GetInstance()->GetDescriptorHeap()};
    computeCommandList_->SetDescriptorHeaps(_countof(heaps), heaps);
}

void DirectXCommon::ExecuteComputeCommands() {
    // 記録されていない（リストが既に閉じている）場合は何もしない
    if (!computeListIsOpen_) return;

    HRESULT hr = computeCommandList_->Close();
    assert(SUCCEEDED(hr));
    computeListIsOpen_ = false;

    ID3D12CommandList *lists[] = {computeCommandList_.Get()};
    computeCommandQueue_->ExecuteCommandLists(1, lists);

    // 完了シグナルを発行
    computeFenceCounter_++;
    computeCommandQueue_->Signal(computeFence_.Get(), computeFenceCounter_);
}

void DirectXCommon::WaitForComputeOnDirectQueue() {
    // GPU 側で Direct Queue が Compute Queue の完了を待つ（CPU はブロックしない）
    commandQueue_->Wait(computeFence_.Get(), computeFenceCounter_);
}

void DirectXCommon::FlushComputeQueue() {
    if (computeFenceCounter_ == 0 || !computeFence_)
        return;
    if (computeFence_->GetCompletedValue() < computeFenceCounter_) {
        HANDLE ev = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        computeFence_->SetEventOnCompletion(computeFenceCounter_, ev);
        WaitForSingleObject(ev, INFINITE);
        CloseHandle(ev);
    }
}

void DirectXCommon::ViewPortRectInitialize() {
    // クライアント領域のサイズと一緒にして画面全体に表示
    viewport_.Width = FLOAT(WinApp::kClientWidth);
    viewport_.Height = FLOAT(WinApp::kClientHeight);
    viewport_.TopLeftX = 0;
    viewport_.TopLeftY = 0;
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;
}

void DirectXCommon::ScissorRectInitialize() {
    // 基本的にビューポートと同じ矩形が構成されるようにする
    scissorRect_.left = 0;
    scissorRect_.right = WinApp::kClientWidth;
    scissorRect_.top = 0;
    scissorRect_.bottom = WinApp::kClientHeight;
}

void DirectXCommon::CreateDXCompiler() {
    HRESULT hr;
    ///=========dxcCompilerを初期化=================
    hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
    assert(SUCCEEDED(hr));
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
    assert(SUCCEEDED(hr));

    // 現時点でincludeはしないが、includeに対応するための設定を行っておく
    hr = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
    assert(SUCCEEDED(hr));
    ///==============================================
}

void DirectXCommon::InitializeFixFPS() {
    // 現在時間を記録する
    reference_ = std::chrono::steady_clock::now();
}

void DirectXCommon::UpdateFixFPS() {
    // 現在時間を取得する
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    // 前回記録からの経過時間を取得する
    std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);

    // 次のフレームまでの待機時間を計算
    if (elapsed < frameTime_) {
        // 待機すべき時間
        std::chrono::microseconds sleepTime = frameTime_ - elapsed;

        // より正確なスリープのためのスピンロック
        auto sleepEnd = now + sleepTime;
        // まず大部分の時間をsleep_forで待機
        if (sleepTime > std::chrono::microseconds(1000)) {
            std::this_thread::sleep_for(sleepTime - std::chrono::microseconds(1000));
        }
        // 残りの短い時間はスピンロックで正確に待機
        while (std::chrono::steady_clock::now() < sleepEnd) {
            // スピンロック（何もしない）
        }
    }

    // 次のフレームの基準時間を更新
    // 精確な60FPSを維持するため、理想的なフレーム時間を加算
    reference_ += frameTime_;

    // もし大幅に遅れている場合は現在時刻に調整（フレームスキップ）
    if (std::chrono::steady_clock::now() > reference_ + frameTime_) {
        reference_ = std::chrono::steady_clock::now();
    }
}

#pragma region 必要な関数
Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateAdditionalDepthResource(int32_t width, int32_t height) {
    return CreateDepthStencilTextureResource(device_, width, height);
}

ID3D12CommandSignature *DirectXCommon::GetDispatchIndirectCommandSignature() {
    // Phase 3 基盤: 初回呼び出し時に遅延生成する（未使用なら一切作られない）。
    if (!dispatchIndirectCommandSignature_) {
        D3D12_INDIRECT_ARGUMENT_DESC arg{};
        arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

        D3D12_COMMAND_SIGNATURE_DESC desc{};
        desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS); // 12 バイト (uint x3)
        desc.NumArgumentDescs = 1;
        desc.pArgumentDescs = &arg;
        // DISPATCH のみでルート引数を差し替えないため pRootSignature は nullptr で良い。
        HRESULT hr = device_->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&dispatchIndirectCommandSignature_));
        assert(SUCCEEDED(hr));
    }
    return dispatchIndirectCommandSignature_.Get();
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateDepthStencilTextureResource(Microsoft::WRL::ComPtr<ID3D12Device> device_, int32_t width, int32_t height) {
    // 生成するResourceの設定
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = width;                          // Textureのは幅
    resourceDesc.Height = height;                        // Textureの高さ
    resourceDesc.MipLevels = 1;                          // mipmapの数
    resourceDesc.DepthOrArraySize = 1;                   // 奥行き or 配列Textureの配列数
    resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // Depthstencilとして利用可能なフォーマット
    resourceDesc.SampleDesc.Count = 1;                   // サンプリングカウント。1固定。
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    // 2次元
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // DepthStencilとして使う通知
    //.利用するHeapの設定
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作る

    // 深度値のクリア設定
    D3D12_CLEAR_VALUE depthClearValue{};
    depthClearValue.DepthStencil.Depth = 1.0f;              // 1.0f (最大値)　でクリア
    depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット。Resourceと合わせる

    // Resourceの生成
    Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
    HRESULT hr = device_->CreateCommittedResource(
        &heapProperties,                  // Heapの設定
        D3D12_HEAP_FLAG_NONE,             // Heapの特殊な設定。特になし。
        &resourceDesc,                    // Resourceの設定
        D3D12_RESOURCE_STATE_DEPTH_WRITE, // 深度値を書き込む状態にしておく
        &depthClearValue,                 // clear最適値
        IID_PPV_ARGS(&resource));         // 作成するResourceポインタへのポインタ
    assert(SUCCEEDED(hr));

    return resource;
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DirectXCommon::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = heapType;
    descriptorHeapDesc.NumDescriptors = numDescriptors;
    descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = device_->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
    assert(SUCCEEDED(hr));
    return descriptorHeap;
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetRTVCPUDescriptorHandle(uint32_t index) {
    return GetCPUDescriptorHandle(rtvDescriptorHeap_, descriptorSizeRTV_, index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetRTVGPUDescriptorHandle(uint32_t index) {
    return GetGPUDescriptorHandle(rtvDescriptorHeap_, descriptorSizeRTV_, index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetDSVCPUDescriptorHandle(uint32_t index) {
    return GetCPUDescriptorHandle(dsvDescriptorHeap_, descriptorSizeDSV_, index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetDSVGPUDescriptorHandle(uint32_t index) {
    return GetGPUDescriptorHandle(dsvDescriptorHeap_, descriptorSizeDSV_, index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap, uint32_t descriptorSize, uint32_t index) {
    D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    handleCPU.ptr += (descriptorSize * index);
    return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetGPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap, uint32_t descriptorSize, uint32_t index) {
    D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    handleGPU.ptr += (descriptorSize * index);
    return handleGPU;
}

void DirectXCommon::BarrierTransition(ID3D12Resource *pResource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After) {
    // 今回のバリアはTransition
    barrier_.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    // Noneにしておく
    barrier_.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    // バリアを張る対象のリソース。現在のバックバッファに対して行う
    barrier_.Transition.pResource = pResource;
    // 遷移前(現在)のResourceState
    barrier_.Transition.StateBefore = Before;
    // 遷移後のResourceState
    barrier_.Transition.StateAfter = After;
    // TransitionBarrierを張る
    commandList_->ResourceBarrier(1, &barrier_);
}

IDxcBlob *DirectXCommon::CompileShader(
    // CompilerするShaderファイルへのパス
    const std::wstring &filePath,
    // Compilerに使用するProfile
    const wchar_t *profile) {
    // これからシェーダーをコンパイルする旨をログに出す
    Log(ConvertString(std::format(L"Begin CompileSharder, path:{}, profile:{}\n", filePath, profile)));
    // hlslファイルを読む
    IDxcBlobEncoding *shaderSource = nullptr;
    HRESULT hr = dxcUtils_->LoadFile(filePath.c_str(), nullptr, &shaderSource);
    // 読めなかったら止める
    assert(SUCCEEDED(hr));
    // 読み込んだファイルの内容を設定する
    DxcBuffer shaderSourceBuffer;
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8; // UTF8の文字コードであることを通知

    LPCWSTR arguments[] = {
        filePath.c_str(),         // コンパイル対象のhlslファイル名
        L"-E", L"main",           // エントリーポイントの指定。基本的にmain以外にはしない
        L"-T", profile,           // ShaderProfileの設定
        L"-Zi", L"-Qembed_debug", // デバッグ用の情報を埋め込む
        L"-Od",                   // 最適化を外しておく
        L"-Zpr",                  // 　メモリレイアウトは行優先
    };
    // 実際にShaderをコンパイルする
    IDxcResult *shaderResult = nullptr;
    hr = dxcCompiler_->Compile(
        &shaderSourceBuffer,        // 読み込んだファイル
        arguments,                  // コンパイルオプション
        _countof(arguments),        // コンパイルオプションの数
        includeHandler_,             // includeが含まれた諸々
        IID_PPV_ARGS(&shaderResult) // コンパイル結果
    );
    // コンパイルエラーではなくdxcが起動できないなど致命的な状況
    assert(SUCCEEDED(hr));

    // 警告・エラーが出てたらログに出して止める
    IDxcBlobUtf8 *shaderError = nullptr;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        Log(shaderError->GetStringPointer());
        // 警告・エラーダメゼッタイ
        assert(false);
    }
    // コンパイル結果から実行用のバイナリ部分を取得
    IDxcBlob *shaderBlob = nullptr;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    assert(SUCCEEDED(hr));
    // 成功したログを出す
    Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));
    // もう使わないリソースを解放
    shaderSource->Release();
    shaderResult->Release();
    // 実行用のパイナリを返却
    return shaderBlob;
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateBufferResource(size_t sizeInBytes, bool isUAV) {
    if (!isUAV) {
        // リソース用のヒープの設定
        D3D12_HEAP_PROPERTIES uploadHeapProperties{};
        uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD; // UploadHeapを使う
        // リソースの設定
        D3D12_RESOURCE_DESC resourceDesc{};
        // バッファリソース。テクスチャの場合はまた別の設定をする
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = sizeInBytes; // リソースのサイズ。
        // バッファの場合はこれらを1にする決まり
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.SampleDesc.Count = 1;
        // バッファの場合はこれにする決まり
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        // 実際にリソースを作る
        Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
        HRESULT hr = device_->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
                                                     &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&resource));
        assert(SUCCEEDED(hr));

        return resource;
    } else {

        // UAVを使う場合は、デフォルトヒープを使う
        D3D12_HEAP_PROPERTIES defaultHeapProperties{};
        defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        // UAV用バッファリソースの設定
        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = sizeInBytes;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        // UAVを使うためのフラグ
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
        HRESULT hr = device_->CreateCommittedResource(&defaultHeapProperties, D3D12_HEAP_FLAG_NONE,
                                                     &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
        return resource;
    }
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateTextureResource(const DirectX::TexMetadata &metadata) {
    // metadataを基にResourceの設定
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = UINT(metadata.width);                             // Textureの幅
    resourceDesc.Height = UINT(metadata.height);                           // Textureの高さ
    resourceDesc.MipLevels = UINT16(metadata.mipLevels);                   // mipmapの数
    resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);            // 奥行き or 配列Textureの配列数
    resourceDesc.Format = metadata.format;                                 // TextureのFormat
    resourceDesc.SampleDesc.Count = 1;                                     // サンプリングカウント。1固定
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension); // Textureの次元数。普段使っているのはZ次元

    // 利用するHeapの設定。非常に特殊な運用。02_04exで一般的なケース版がある
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;                    // 細かい設定を行う
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN; // WriteBackポリシーでCPUアクセス可能
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;  // プロセッサの近くに配置

    // Resourcesの生成
    Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
    HRESULT hr = device_->CreateCommittedResource(
        &heapProperties,                // Heapの設定
        D3D12_HEAP_FLAG_NONE,           // Heapの特殊な設定。
        &resourceDesc,                  // Resourceの設定
        D3D12_RESOURCE_STATE_COPY_DEST, // 初回のResourceState。Textureは基本読むだけ
        nullptr,                        // Clear最適値。使わないのでnullptr
        IID_PPV_ARGS(&resource));       // 作成するResourceポインタへのポインタ
    assert(SUCCEEDED(hr));
    return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateRenderTextureResource(uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_CLEAR_VALUE color) {
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Format = format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
    HRESULT hr = device_->CreateCommittedResource(&heapProperties,
                                                 D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, &color,
                                                 IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));

    return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage &mipImages) {
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    DirectX::PrepareUpload(device_.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
    uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size()));
    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = CreateBufferResource(intermediateSize);
    UpdateSubresources(commandList_.Get(), texture.Get(), intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());
    // Textureへの転送後は利用できるよう、D3D12_RESOURCE_STATE_COPY_DESTからD3D12_RESOURCE_STATE_GENERIC_READへのResourceStateを変更する
    D3D12_RESOURCE_BARRIER barrier_{};
    barrier_.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier_.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier_.Transition.pResource = texture.Get();
    barrier_.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier_.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier_.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList_->ResourceBarrier(1, &barrier_);
    return intermediateResource;
}

#pragma endregion
} // namespace Hagine
