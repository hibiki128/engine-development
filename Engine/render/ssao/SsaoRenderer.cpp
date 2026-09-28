#include "SsaoRenderer.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include <data/DataHandler.h>
#include <debug/log/Logger.h>
#include <graphics/pipeline/ComputeEffectPipeline.h>
#include <graphics/srv/SrvManager.h>

#ifdef USE_IMGUI
#include "imgui.h"
#include <debug/imgui/DebugUIHelper.h>
#include <debug/imgui/ImGuiNotification.h>
#include <icon/IconsFontAwesome5.h>
#endif // USE_IMGUI

#include <memory>

namespace Hagine {
namespace {
// 遮蔽は1チャンネルで足りる。UAV として確実に書ける R32_FLOAT を使う
constexpr DXGI_FORMAT kAmbientOcclusionFormat = DXGI_FORMAT_R32_FLOAT;

constexpr const char *kSsaoShader = "Deferred/Ssao.CS.hlsl";
constexpr const char *kSsaoBlurShader = "Deferred/SsaoBlur.CS.hlsl";
constexpr const char *kDataFileName = "SsaoData";

// SSAO 本体は t0=深度, t1=法線。ならしは t0=遮蔽, t1=深度。どちらも2枚
constexpr uint32_t kSrvTableSize = 2;
} // namespace

void SsaoRenderer::Initialize()
{
    pDxCommon_ = DirectXCommon::GetInstance();
    pSrvManager_ = SrvManager::GetInstance();

    constantBuffer_ = pDxCommon_->CreateBufferResource(sizeof(SsaoConstants));
    constantBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&pConstants_));
    blurConstantBuffer_ = pDxCommon_->CreateBufferResource(sizeof(SsaoBlurConstants));
    blurConstantBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&pBlurConstants_));

    LoadData(kDataFileName);

    CreateResources(WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight());

    // コンピュートの入力テーブルぶんの連続領域を押さえる。
    // SrvManager は「Allocate() が返した番号 r に対して、実際に書き込むのは r+1」という
    // 規約なので、確保した先頭 +1 をテーブルの先頭として使う
    {
        uint32_t firstAllocated = 0;
        bool contiguous = true;
        for (uint32_t i = 0; i < kSrvTableSize * 2; ++i)
        {
            const uint32_t index = pSrvManager_->Allocate();
            if (i == 0)
                firstAllocated = index;
            else if (index != firstAllocated + i)
                contiguous = false;
        }
        if (contiguous)
        {
            ssaoTableIndex_ = firstAllocated + 1;
            blurTableIndex_ = ssaoTableIndex_ + kSrvTableSize;
            tablesReady_ = true;
        }
        else
        {
            Logger::Error("SSAO 用のディスクリプタを連続で確保できませんでした。SSAO は無効になります。");
        }
    }

    initialized_ = (rawResource_ != nullptr && blurredResource_ != nullptr && tablesReady_);
}

void SsaoRenderer::BuildInputTable(uint32_t tableIndex,
                                   ID3D12Resource *first, DXGI_FORMAT firstFormat,
                                   ID3D12Resource *second, DXGI_FORMAT secondFormat)
{
    // シェーダー可視ヒープは CPU から読めないのでデスクリプタのコピーができない。
    // 代わりに、テーブルの各スロットへその場で SRV を作り直す（作るのは許されている）
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipLevels = 1;

    desc.Format = firstFormat;
    pDxCommon_->GetDevice()->CreateShaderResourceView(first, &desc,
                                                      pSrvManager_->GetCPUDescriptorHandle(tableIndex));
    desc.Format = secondFormat;
    pDxCommon_->GetDevice()->CreateShaderResourceView(second, &desc,
                                                      pSrvManager_->GetCPUDescriptorHandle(tableIndex + 1));
}

void SsaoRenderer::Finalize()
{
    pConstants_ = nullptr;
    pBlurConstants_ = nullptr;
    constantBuffer_.Reset();
    blurConstantBuffer_.Reset();
    rawResource_.Reset();
    blurredResource_.Reset();
    initialized_ = false;
    pDxCommon_ = nullptr;
    pSrvManager_ = nullptr;
}

void SsaoRenderer::CreateResources(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;

    width_ = width;
    height_ = height;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kAmbientOcclusionFormat;
    clearValue.Color[0] = 1.0f; // 遮蔽なし＝1.0

    rawResource_ = pDxCommon_->CreateRenderTextureResource(width, height, kAmbientOcclusionFormat,
                                                           clearValue, /*allowUAV=*/true);
    blurredResource_ = pDxCommon_->CreateRenderTextureResource(width, height, kAmbientOcclusionFormat,
                                                               clearValue, /*allowUAV=*/true);
    if (!rawResource_ || !blurredResource_)
    {
        Logger::Error("SSAO 用のテクスチャを作成できませんでした。");
        return;
    }

    // 遮蔽テクスチャ自身の SRV / UAV
    rawSrvIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateSRVforRenderTexture(rawSrvIndex_, rawResource_.Get(), kAmbientOcclusionFormat);
    rawUavIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateUAVforTexture2D(rawUavIndex_, rawResource_.Get(), kAmbientOcclusionFormat);

    blurredSrvIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateSRVforRenderTexture(blurredSrvIndex_, blurredResource_.Get(), kAmbientOcclusionFormat);
    blurredUavIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateUAVforTexture2D(blurredUavIndex_, blurredResource_.Get(), kAmbientOcclusionFormat);
}

void SsaoRenderer::EnsureResolution()
{
    const uint32_t width = WinApp::GetVirtualWidth();
    const uint32_t height = WinApp::GetVirtualHeight();
    if (width == width_ && height == height_)
        return;
    CreateResources(width, height);
}

void SsaoRenderer::Render(ID3D12Resource *depthResource, ID3D12Resource *normalResource,
                          const Matrix4x4 &invViewProjection, const Matrix4x4 &view,
                          const Matrix4x4 &projection)
{
    if (!initialized_ || !enabled_ || depthResource == nullptr || normalResource == nullptr)
        return;

    EnsureResolution();
    if (!rawResource_ || !blurredResource_)
        return;

    // 深度は D24_UNORM_S8_UINT なので、読むときは R24_UNORM_X8_TYPELESS にする
    constexpr DXGI_FORMAT kDepthReadFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    BuildInputTable(ssaoTableIndex_, depthResource, kDepthReadFormat,
                    normalResource, DXGI_FORMAT_R16G16B16A16_FLOAT);
    BuildInputTable(blurTableIndex_, rawResource_.Get(), kAmbientOcclusionFormat,
                    depthResource, kDepthReadFormat);

    // 深度はポイントサンプリングで読む（補間すると輪郭の深度が混ざる）
    const std::vector<ShaderRootSignature::SamplerPreset> samplers = {
        ShaderRootSignature::SamplerPreset::PointClamp,
    };

    const ComputeEffectProgram *ssaoProgram = ComputeEffectPipeline::GetInstance()->Get(kSsaoShader, samplers);
    const ComputeEffectProgram *blurProgram = ComputeEffectPipeline::GetInstance()->Get(kSsaoBlurShader, samplers);
    if (!ssaoProgram || !blurProgram)
        return;

    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();

    // 定数を今フレームの値で埋める
    settings_.invViewProjection = invViewProjection;
    settings_.view = view;
    settings_.projection = projection;
    settings_.screenWidth = width_;
    settings_.screenHeight = height_;
    *pConstants_ = settings_;

    blurSettings_.screenWidth = width_;
    blurSettings_.screenHeight = height_;
    *pBlurConstants_ = blurSettings_;

    const UINT groupX = (width_ + ssaoProgram->threadGroupSizeX - 1) / ssaoProgram->threadGroupSizeX;
    const UINT groupY = (height_ + ssaoProgram->threadGroupSizeY - 1) / ssaoProgram->threadGroupSizeY;

    // ── 遮蔽の計算 ──
    {
        pDxCommon_->BarrierTransition(rawResource_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        pCommandList->SetPipelineState(ssaoProgram->pipelineState.Get());
        pCommandList->SetComputeRootSignature(ssaoProgram->rootSignature.Get());

        // t0=深度, t1=法線（BuildInputTable で連続領域へ作り済み）
        if (ssaoProgram->rootSignature.GetSrvTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(ssaoProgram->rootSignature.GetSrvTableIndex(),
                                                        pSrvManager_->GetGPUDescriptorHandle(ssaoTableIndex_));
        }
        if (ssaoProgram->rootSignature.GetUavTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(ssaoProgram->rootSignature.GetUavTableIndex(),
                                                        pSrvManager_->GetGPUDescriptorHandle(rawUavIndex_));
        }
        const UINT cbvIndex = ssaoProgram->rootSignature.GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 0);
        if (cbvIndex != UINT_MAX)
            pCommandList->SetComputeRootConstantBufferView(cbvIndex, constantBuffer_->GetGPUVirtualAddress());

        pCommandList->Dispatch(groupX, groupY, 1);

        pDxCommon_->BarrierTransition(rawResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
    }

    // ── ならし ──
    {
        pDxCommon_->BarrierTransition(blurredResource_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        pCommandList->SetPipelineState(blurProgram->pipelineState.Get());
        pCommandList->SetComputeRootSignature(blurProgram->rootSignature.Get());

        if (blurProgram->rootSignature.GetSrvTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(blurProgram->rootSignature.GetSrvTableIndex(),
                                                        pSrvManager_->GetGPUDescriptorHandle(blurTableIndex_));
        }
        if (blurProgram->rootSignature.GetUavTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(blurProgram->rootSignature.GetUavTableIndex(),
                                                        pSrvManager_->GetGPUDescriptorHandle(blurredUavIndex_));
        }
        const UINT cbvIndex = blurProgram->rootSignature.GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 0);
        if (cbvIndex != UINT_MAX)
            pCommandList->SetComputeRootConstantBufferView(cbvIndex, blurConstantBuffer_->GetGPUVirtualAddress());

        pCommandList->Dispatch(groupX, groupY, 1);

        pDxCommon_->BarrierTransition(blurredResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
    }
}

void SsaoRenderer::SaveData(const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>("Ssao", fileName);
    dataHandler->Save<bool>("enabled", enabled_);
    dataHandler->Save<float>("strength", strength_);
    dataHandler->Save<float>("radius", settings_.radius);
    dataHandler->Save<float>("bias", settings_.bias);
    dataHandler->Save<float>("intensity", settings_.intensity);
    dataHandler->Save<float>("power", settings_.power);
    dataHandler->Save<int>("sampleCount", settings_.sampleCount);
    dataHandler->Save<float>("maxDistance", settings_.maxDistance);
    dataHandler->Save<int>("blurRadius", blurSettings_.radius);
    dataHandler->Save<float>("blurDepthThreshold", blurSettings_.depthThreshold);
}

void SsaoRenderer::LoadData(const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>("Ssao", fileName);
    enabled_ = dataHandler->Load<bool>("enabled", true);
    strength_ = dataHandler->Load<float>("strength", 0.7f);
    settings_.radius = dataHandler->Load<float>("radius", 0.6f);
    settings_.bias = dataHandler->Load<float>("bias", 0.03f);
    settings_.intensity = dataHandler->Load<float>("intensity", 1.0f);
    settings_.power = dataHandler->Load<float>("power", 1.6f);
    settings_.sampleCount = dataHandler->Load<int>("sampleCount", 16);
    settings_.maxDistance = dataHandler->Load<float>("maxDistance", 12.0f);
    blurSettings_.radius = dataHandler->Load<int>("blurRadius", 2);
    blurSettings_.depthThreshold = dataHandler->Load<float>("blurDepthThreshold", 0.0015f);
}

void SsaoRenderer::DrawImGui()
{
#ifdef USE_IMGUI
    SectionHeader(ICON_FA_CIRCLE " SSAO（接地の陰り）", DebugTheme::kAccentOrange);
    DimText("物と床の接地部分やへこみに陰りを入れて、置いてある感じを出します");

    ToggleRow("SSAO を使う", "##ssaoEnabled", &enabled_, DebugTheme::kAccentOrange);
    if (!enabled_)
        return;

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##ssaoStrength", &strength_, 0.0f, 1.0f, "効かせ具合 %.2f");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##ssaoRadius", &settings_.radius, 0.05f, 3.0f, "届く範囲 %.2f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("どれだけ離れた所まで遮蔽として数えるか。大きいほど広く柔らかい陰りになります");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##ssaoIntensity", &settings_.intensity, 0.0f, 3.0f, "濃さ %.2f");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##ssaoPower", &settings_.power, 0.5f, 4.0f, "コントラスト %.2f");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##ssaoBias", &settings_.bias, 0.0f, 0.2f, "自己遮蔽の余裕 %.3f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("平らな面に縞模様が出るときは上げてください");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##ssaoSamples", &settings_.sampleCount, 4, 32, "サンプル数 %d");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##ssaoMaxDistance", &settings_.maxDistance, 1.0f, 50.0f, "遠景の打ち切り %.1f");

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##ssaoBlurRadius", &blurSettings_.radius, 0, 4, "ならし幅 %d");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##ssaoBlurDepth", &blurSettings_.depthThreshold, 0.0001f, 0.01f,
                       "輪郭を守る強さ %.4f");

    ImGui::Spacing();
    if (ConfirmButton(ICON_FA_SAVE " SSAO設定を保存", ImVec2(-1.0f, 0.0f)))
    {
        SaveData(kDataFileName);
        ImGuiNotification::Post("SSAO設定を保存しました");
    }
#endif // USE_IMGUI
}

} // namespace Hagine
