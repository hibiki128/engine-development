#include "RtAoPass.h"
#include "RaytracingScene.h"
#include <DirectXCommon.h>
#include <WinApp.h>
#include <data/DataHandler.h>
#include <debug/log/Logger.h>
#include <graphics/pipeline/ComputeEffectPipeline.h>
#include <graphics/srv/SrvManager.h>
#include <memory>
#include <vector>
#ifdef USE_IMGUI
#include "imgui.h"
#include <debug/imgui/DebugUIHelper.h>
#include <debug/imgui/ImGuiNotification.h>
#include <icon/IconsFontAwesome5.h>
#endif // USE_IMGUI

namespace Hagine {
namespace {

// 遮蔽は1チャンネルで足りる。SSAO と同じ R32_FLOAT にして、ならしを共用できるようにする
constexpr DXGI_FORMAT kAmbientOcclusionFormat = DXGI_FORMAT_R32_FLOAT;
// 深度は D24_UNORM_S8_UINT なので、読むときは R24_UNORM_X8_TYPELESS にする
constexpr DXGI_FORMAT kDepthReadFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

// 遮蔽本体は t0=TLAS, t1=深度, t2=法線 の3枚
constexpr uint32_t kInputTableSize = 3;
// ならしは t0=遮蔽, t1=深度 の2枚
constexpr uint32_t kBlurTableSize = 2;

// RayQuery を使うので cs_6_5 以上でないとコンパイルが通らない
constexpr const wchar_t *kRayQueryProfile = L"cs_6_5";
const std::string kShaderFile = "Raytracing/RtAo.CS.hlsl";
// ならしは SSAO のものをそのまま使う（入力の並びも定数の並びも同じ）
const std::string kBlurShaderFile = "Deferred/SsaoBlur.CS.hlsl";
constexpr const char *kDataFileName = "RtAoData";
} // namespace

void RtAoPass::Initialize(DirectXCommon *pDxCommon, SrvManager *pSrvManager)
{
    pDxCommon_ = pDxCommon;
    pSrvManager_ = pSrvManager;

    if (!pDxCommon_->IsRaytracingSupported())
    {
        Logger::Log("RtAoPass: レイトレーシング非対応のため無効です\n");
        return;
    }

    constantBuffer_ = pDxCommon_->CreateBufferResource(sizeof(Constants));
    constantBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&pConstants_));
    blurConstantBuffer_ = pDxCommon_->CreateBufferResource(sizeof(BlurConstants));
    blurConstantBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&pBlurConstants_));

    LoadData(kDataFileName);

    CreateResources(WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight());

    inputTableIndex_ = AllocateContiguousTable(kInputTableSize);
    blurTableIndex_ = AllocateContiguousTable(kBlurTableSize);

    initialized_ = (rawResource_ != nullptr && blurredResource_ != nullptr &&
                    inputTableIndex_ != UINT32_MAX && blurTableIndex_ != UINT32_MAX);
    if (initialized_)
    {
        Logger::Log("RtAoPass: 初期化しました\n");
    }
    else
    {
        Logger::Error("RtAoPass: 初期化に失敗しました。RTの遮蔽は無効になります。");
    }
}

uint32_t RtAoPass::AllocateContiguousTable(uint32_t count)
{
    // デスクリプタテーブルはヒープ上で連続している必要があるので、まとめて押さえる。
    // SrvManager は「Allocate() が返した番号 r に対して、実際に書き込むのは r+1」という規約
    uint32_t firstAllocated = 0;
    bool contiguous = true;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t index = pSrvManager_->Allocate();
        if (i == 0)
        {
            firstAllocated = index;
        }
        else if (index != firstAllocated + i)
        {
            contiguous = false;
        }
    }
    if (!contiguous)
    {
        Logger::Error("RtAoPass: ディスクリプタを連続で確保できませんでした。");
        return UINT32_MAX;
    }
    return firstAllocated + 1;
}

void RtAoPass::Finalize()
{
    pConstants_ = nullptr;
    pBlurConstants_ = nullptr;
    constantBuffer_.Reset();
    blurConstantBuffer_.Reset();
    rawResource_.Reset();
    blurredResource_.Reset();
    initialized_ = false;
    resultReady_ = false;
}

void RtAoPass::CreateResources(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return;
    }
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
        Logger::Error("RtAoPass: 遮蔽テクスチャを作成できませんでした。");
        return;
    }

    rawSrvIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateSRVforRenderTexture(rawSrvIndex_, rawResource_.Get(), kAmbientOcclusionFormat);
    rawUavIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateUAVforTexture2D(rawUavIndex_, rawResource_.Get(), kAmbientOcclusionFormat);

    blurredSrvIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateSRVforRenderTexture(blurredSrvIndex_, blurredResource_.Get(), kAmbientOcclusionFormat);
    blurredUavIndex_ = pSrvManager_->Allocate() + 1;
    pSrvManager_->CreateUAVforTexture2D(blurredUavIndex_, blurredResource_.Get(), kAmbientOcclusionFormat);
}

void RtAoPass::EnsureResolution()
{
    const uint32_t width = WinApp::GetVirtualWidth();
    const uint32_t height = WinApp::GetVirtualHeight();
    if (width == width_ && height == height_)
    {
        return;
    }
    CreateResources(width, height);
}

void RtAoPass::Render(ID3D12Resource *depthResource, ID3D12Resource *normalResource,
                      const Matrix4x4 &inverseViewProjection)
{
    resultReady_ = false;

    if (!initialized_ || !enabled_ || depthResource == nullptr || normalResource == nullptr)
    {
        return;
    }

    RaytracingScene *pScene = RaytracingScene::GetInstance();
    if (!pScene->IsAvailable())
    {
        return;
    }
    // 使うと伝えておく。伝えていないフレームは加速構造を組まないので、
    // このパスをONにした直後の1フレームだけは SSAO のまま出る
    pScene->RequestUse();
    pScene->EnsureTlas();
    if (!pScene->HasValidTlas())
    {
        return;
    }

    EnsureResolution();
    if (!rawResource_ || !blurredResource_)
    {
        return;
    }

    // 深度はポイントサンプリングで読む（補間すると輪郭の深度が混ざる）
    const std::vector<ShaderRootSignature::SamplerPreset> samplers = {
        ShaderRootSignature::SamplerPreset::PointClamp,
    };

    const ComputeEffectProgram *aoProgram =
        ComputeEffectPipeline::GetInstance()->Get(kShaderFile, samplers, kRayQueryProfile);
    const ComputeEffectProgram *blurProgram =
        ComputeEffectPipeline::GetInstance()->Get(kBlurShaderFile, samplers);
    if (!aoProgram || !blurProgram)
    {
        return;
    }

    // ── 入力テーブルを組み直す ──
    // シェーダー可視ヒープはコピー元にできないので、その場でSRVを作り直す
    ID3D12Device *pDevice = pDxCommon_->GetDevice().Get();
    {
        // t0: TLAS。加速構造のSRVは「リソースではなくアドレス」を指すので pResource は nullptr
        D3D12_SHADER_RESOURCE_VIEW_DESC tlasDesc{};
        tlasDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        tlasDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        tlasDesc.RaytracingAccelerationStructure.Location = pScene->GetTlasGpuAddress();
        pDevice->CreateShaderResourceView(nullptr, &tlasDesc,
                                          pSrvManager_->GetCPUDescriptorHandle(inputTableIndex_));

        D3D12_SHADER_RESOURCE_VIEW_DESC texDesc{};
        texDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        texDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        texDesc.Texture2D.MipLevels = 1;

        // t1: 深度
        texDesc.Format = kDepthReadFormat;
        pDevice->CreateShaderResourceView(depthResource, &texDesc,
                                          pSrvManager_->GetCPUDescriptorHandle(inputTableIndex_ + 1));

        // t2: G-Buffer の法線
        texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        pDevice->CreateShaderResourceView(normalResource, &texDesc,
                                          pSrvManager_->GetCPUDescriptorHandle(inputTableIndex_ + 2));

        // ならし側: t0=遮蔽, t1=深度
        texDesc.Format = kAmbientOcclusionFormat;
        pDevice->CreateShaderResourceView(rawResource_.Get(), &texDesc,
                                          pSrvManager_->GetCPUDescriptorHandle(blurTableIndex_));
        texDesc.Format = kDepthReadFormat;
        pDevice->CreateShaderResourceView(depthResource, &texDesc,
                                          pSrvManager_->GetCPUDescriptorHandle(blurTableIndex_ + 1));
    }

    // ── 定数 ──
    settings_.inverseViewProjection = inverseViewProjection;
    settings_.textureSize[0] = static_cast<int32_t>(width_);
    settings_.textureSize[1] = static_cast<int32_t>(height_);
    *pConstants_ = settings_;

    blurSettings_.screenWidth = width_;
    blurSettings_.screenHeight = height_;
    *pBlurConstants_ = blurSettings_;

    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();

    const UINT groupX = (width_ + aoProgram->threadGroupSizeX - 1) / aoProgram->threadGroupSizeX;
    const UINT groupY = (height_ + aoProgram->threadGroupSizeY - 1) / aoProgram->threadGroupSizeY;

    // ── 遮蔽をレイで求める ──
    {
        pDxCommon_->BarrierTransition(rawResource_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        pCommandList->SetPipelineState(aoProgram->pipelineState.Get());
        pCommandList->SetComputeRootSignature(aoProgram->rootSignature.Get());

        if (aoProgram->rootSignature.GetSrvTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(aoProgram->rootSignature.GetSrvTableIndex(),
                                                        pSrvManager_->GetGPUDescriptorHandle(inputTableIndex_));
        }
        if (aoProgram->rootSignature.GetUavTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(aoProgram->rootSignature.GetUavTableIndex(),
                                                        pSrvManager_->GetGPUDescriptorHandle(rawUavIndex_));
        }
        const UINT cbvIndex = aoProgram->rootSignature.GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 0);
        if (cbvIndex != UINT_MAX)
        {
            pCommandList->SetComputeRootConstantBufferView(cbvIndex, constantBuffer_->GetGPUVirtualAddress());
        }

        pCommandList->Dispatch(groupX, groupY, 1);

        pDxCommon_->BarrierTransition(rawResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
    }

    // ── ならし（SSAO と同じシェーダー）──
    // 1画素あたり数本しか飛ばしていないので、これを通さないとザラザラのまま使うことになる
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
        {
            pCommandList->SetComputeRootConstantBufferView(cbvIndex, blurConstantBuffer_->GetGPUVirtualAddress());
        }

        pCommandList->Dispatch(groupX, groupY, 1);

        pDxCommon_->BarrierTransition(blurredResource_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
    }

    resultReady_ = true;
}

void RtAoPass::SaveData(const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>("RtAo", fileName);
    dataHandler->Save<bool>("enabled", enabled_);
    dataHandler->Save<float>("strength", strength_);
    dataHandler->Save<float>("radius", settings_.radius);
    dataHandler->Save<float>("normalBias", settings_.normalBias);
    dataHandler->Save<float>("intensity", settings_.intensity);
    dataHandler->Save<float>("power", settings_.power);
    dataHandler->Save<int>("sampleCount", settings_.sampleCount);
    dataHandler->Save<int>("blurRadius", blurSettings_.radius);
    dataHandler->Save<float>("blurDepthThreshold", blurSettings_.depthThreshold);
}

void RtAoPass::LoadData(const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>("RtAo", fileName);
    enabled_ = dataHandler->Load<bool>("enabled", false);
    strength_ = dataHandler->Load<float>("strength", 0.7f);
    settings_.radius = dataHandler->Load<float>("radius", 1.2f);
    settings_.normalBias = dataHandler->Load<float>("normalBias", 0.02f);
    settings_.intensity = dataHandler->Load<float>("intensity", 1.0f);
    settings_.power = dataHandler->Load<float>("power", 1.0f);
    settings_.sampleCount = dataHandler->Load<int>("sampleCount", 8);
    blurSettings_.radius = dataHandler->Load<int>("blurRadius", 2);
    blurSettings_.depthThreshold = dataHandler->Load<float>("blurDepthThreshold", 0.0015f);
}

bool RtAoPass::IsSupported() const
{
    return pDxCommon_ && pDxCommon_->IsRaytracingSupported();
}

void RtAoPass::DrawImGui()
{
#ifdef USE_IMGUI
    SectionHeader(ICON_FA_CIRCLE " RTの遮蔽（RT AO）", DebugTheme::kAccentPurple);

    if (!pDxCommon_ || !pDxCommon_->IsRaytracingSupported())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
        ImGui::TextWrapped("この環境ではレイトレーシングが使えません");
        ImGui::PopStyleColor();
        return;
    }

    DimText("レイで遮蔽を測ります。SSAOと違い、画面に写っていない物も遮蔽に数えます");

    ToggleRow("RTの遮蔽を使う", "##rtAoEnabled", &enabled_, DebugTheme::kAccentPurple);
    if (!enabled_)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("ONにすると SSAO の代わりに使われます（両方掛けると二重に暗くなるため）");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##rtAoStrength", &strength_, 0.0f, 1.0f, "効かせ具合 %.2f");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##rtAoRadius", &settings_.radius, 0.05f, 10.0f, "届く範囲 %.2f");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("どれだけ離れた物まで遮蔽として数えるか。\n"
                          "SSAOと違って深度バッファに縛られないので、大きくしても破綻しません");
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##rtAoIntensity", &settings_.intensity, 0.0f, 3.0f, "濃さ %.2f");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##rtAoPower", &settings_.power, 0.5f, 4.0f, "コントラスト %.2f");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##rtAoBias", &settings_.normalBias, 0.0f, 0.2f, "自己遮蔽の余裕 %.3f");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("平らな面に縞模様が出るときは上げてください");
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##rtAoSamples", &settings_.sampleCount, 1, 32, "レイの本数 %d");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("1画素あたりに飛ばす本数。増やすほど滑らかになりますが、そのぶん重くなります。\n"
                          "少ない本数でもならしでかなり消えるので、まず4〜8で様子を見てください");
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##rtAoBlurRadius", &blurSettings_.radius, 0, 4, "ならし幅 %d");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##rtAoBlurDepth", &blurSettings_.depthThreshold, 0.0001f, 0.01f,
                       "輪郭を守る強さ %.4f");

    ImGui::Spacing();
    if (ConfirmButton(ICON_FA_SAVE " RT遮蔽の設定を保存", ImVec2(-1.0f, 0.0f)))
    {
        SaveData(kDataFileName);
        ImGuiNotification::Post("RT遮蔽の設定を保存しました");
    }
#endif // USE_IMGUI
}
} // namespace Hagine
