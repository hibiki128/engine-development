#include "DXDevice.h"
#include "cassert"
#include "format"
#include <debug/log/Logger.h>
#include <string/StringUtility.h>

namespace Hagine {
using namespace Logger;
using namespace StringUtility;

void DXDevice::Initialize()
{

#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        // デバッグレイヤーを有効化する（API誤用の検出。相対的に軽い）
        debugController->EnableDebugLayer();

        // GPUベース検証(GBV)はシェーダを差し替えて全リソースアクセスを毎回検証するため、
        // 描画/ディスパッチを数倍重くする。Debugビルドが極端に重くなる主因になり得るので
        // 通常はOFF。ディスクリプタ範囲外アクセス等のGPU側クラッシュ/破損を追う時だけ true にする。
        constexpr bool kEnableGpuBasedValidation = false;
        if (kEnableGpuBasedValidation)
        {
            debugController->SetEnableGPUBasedValidation(TRUE);
        }
    }
#endif

    // DXGIファクトリーの生成
    HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory_));
    // 初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、どうにもできない場合が多いのでassertにしておく
    assert(SUCCEEDED(hr));

    // 使用するアダプタ用の変数。最初にnullptrを入れておく
    Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;
    // 良い順にアダプタを頼む
    for (UINT i = 0; dxgiFactory_->EnumAdapterByGpuPreference(i,
                                                              DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
                     DXGI_ERROR_NOT_FOUND;
         ++i)
    {
        // アダプターの情報を取得する
        DXGI_ADAPTER_DESC3 adapterDesc{};
        hr = useAdapter->GetDesc3(&adapterDesc);
        assert(SUCCEEDED(hr)); // 取得できないのは一大事
        // ソフトウェアアダプタでなければ採用！
        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE))
        {
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
    const char *featureLevelStrings[] = {"12.2", "12.1", "12.0"};
    // 高い順に生成できるか試していく
    for (size_t i = 0; i < _countof(featureLevels); ++i)
    {
        // 採用したアダプターでデバイスを生成
        hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device_));
        // 指定した機能レベルでデバイスが生成できたかを確認
        if (SUCCEEDED(hr))
        {
            // 生成できたのでログ出力を行ってループを抜ける
            Log(std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
            break;
        }
    }
    // デバイスの生成がうまくいかなかったので起動できない
    assert(device_ != nullptr);
    Log("Complete create D3D12Device!!!\n");

#ifdef _DEBUG
    ID3D12InfoQueue *infoQueue = nullptr;
    if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue))))
    {
        // やばいエラー時に止まる
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        // エラー時に止まる
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        // 抑制するメッセージのID
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

        // エラーの文面を GameLog.txt にも残す。デバッガを付けずに起動してエラーで止まった場合でも、
        // ログを見れば何が起きたか分かるようにする（止まる前にこのコールバックが呼ばれる）
        ID3D12InfoQueue1 *infoQueue1 = nullptr;
        if (SUCCEEDED(infoQueue->QueryInterface(IID_PPV_ARGS(&infoQueue1))))
        {
            DWORD cookie = 0;
            infoQueue1->RegisterMessageCallback(
                [](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id,
                   LPCSTR description, void *) {
                    if (severity <= D3D12_MESSAGE_SEVERITY_WARNING)
                    {
                        Logger::Log(severity <= D3D12_MESSAGE_SEVERITY_ERROR ? Logger::LogLevel::Error
                                                                             : Logger::LogLevel::Warning,
                                    std::format("D3D12 (id={}): {}", static_cast<int>(id), description));
                    }
                },
                D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie);
            infoQueue1->Release();
        }

        // 解放
        infoQueue->Release();
    }
#endif

    QueryRaytracingSupport();
}

void DXDevice::QueryRaytracingSupport()
{
    // 加速構造を作るには ID3D12Device5 が要る。古い環境では取れないので、
    // 取れなかった時点でレイトレーシングは無効として扱う
    if (FAILED(device_->QueryInterface(IID_PPV_ARGS(&device5_))))
    {
        Log("Raytracing: ID3D12Device5 を取得できませんでした（レイトレーシングは無効）\n");
        return;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
    {
        Log("Raytracing: 対応状況を問い合わせられませんでした（レイトレーシングは無効）\n");
        return;
    }
    raytracingTier_ = options5.RaytracingTier;

    // インラインRT（RayQuery）は Tier 1.1 から。1.0 ではフルDXRパイプラインしか使えない
    if (raytracingTier_ < D3D12_RAYTRACING_TIER_1_1)
    {
        Log(std::format("Raytracing: Tier が足りません（RayQuery には 1.1 以上が必要 / 現在 tier={}）\n",
                        static_cast<int>(raytracingTier_)));
        return;
    }

    // RayQuery はシェーダーモデル 6.5 以上でしか書けない
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_5};
    if (FAILED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))) ||
        shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_5)
    {
        Log("Raytracing: シェーダーモデル 6.5 に対応していません（レイトレーシングは無効）\n");
        return;
    }

    raytracingSupported_ = true;
    Log(std::format("Raytracing: 利用可能（tier={} / shaderModel=0x{:x}）\n",
                    static_cast<int>(raytracingTier_),
                    static_cast<int>(shaderModel.HighestShaderModel)));
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DXDevice::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible)
{
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = heapType;
    descriptorHeapDesc.NumDescriptors = numDescriptors;
    descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = device_->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
    assert(SUCCEEDED(hr));
    return descriptorHeap;
}
} // namespace Hagine
