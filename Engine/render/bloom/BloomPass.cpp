#include "BloomPass.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include <data/DataHandler.h>
#include <debug/log/Logger.h>
#include <graphics/pipeline/ComputeEffectPipeline.h>
#include <Frame.h>
#include <graphics/srv/SrvManager.h>

#ifdef USE_IMGUI
#include "imgui.h"
#include <debug/imgui/DebugUIHelper.h>
#include <icon/IconsFontAwesome5.h>
#endif // USE_IMGUI

#include <algorithm>

namespace Hagine {
namespace {
constexpr const char *kPrefilterShader = "OffScreen/BloomPrefilter.CS.hlsl";
constexpr const char *kDownsampleShader = "OffScreen/BloomDownsample.CS.hlsl";
constexpr const char *kUpsampleShader = "OffScreen/BloomUpsample.CS.hlsl";
constexpr const char *kCompositeShader = "OffScreen/BloomComposite.CS.hlsl";
constexpr const char *kDistortionShader = "OffScreen/BloomDistortion.CS.hlsl";
constexpr const char *kDataFileName = "BloomData";

// プリフィルタは t0=パーティクル後 / t1=パーティクル前 の2枚を連続で要求する
constexpr uint32_t kPrefilterTableSize = 2;
// 歪みは t0=パーティクル前の控え / t1=マスクに使うミップ の2枚
constexpr uint32_t kDistortTableSize = 2;
} // namespace

void BloomPass::Initialize()
{
    pDxCommon_ = DirectXCommon::GetInstance();
    pSrvManager_ = SrvManager::GetInstance();

    LoadData();

    prefilterCb_ = pDxCommon_->CreateBufferResource(sizeof(PrefilterConstants));
    prefilterCb_->Map(0, nullptr, reinterpret_cast<void **>(&pPrefilterCb_));
    compositeCb_ = pDxCommon_->CreateBufferResource(sizeof(CompositeConstants));
    compositeCb_->Map(0, nullptr, reinterpret_cast<void **>(&pCompositeCb_));
    distortionCb_ = pDxCommon_->CreateBufferResource(sizeof(DistortionConstants));
    distortionCb_->Map(0, nullptr, reinterpret_cast<void **>(&pDistortionCb_));
    for (uint32_t i = 0; i < kMipCount; ++i)
    {
        downsampleCb_[i] = pDxCommon_->CreateBufferResource(sizeof(DownsampleConstants));
        downsampleCb_[i]->Map(0, nullptr, reinterpret_cast<void **>(&pDownsampleCb_[i]));
        upsampleCb_[i] = pDxCommon_->CreateBufferResource(sizeof(UpsampleConstants));
        upsampleCb_[i]->Map(0, nullptr, reinterpret_cast<void **>(&pUpsampleCb_[i]));
    }

    // 入力テーブルぶんの連続領域を押さえる。
    // SrvManager は「Allocate() が返した番号 r に対し、実際に書き込むのは r+1」という
    // +1規約なので、確保した先頭 +1 をテーブルの先頭として使う
    {
        auto allocateTable = [&](uint32_t size) -> uint32_t {
            uint32_t firstAllocated = 0;
            bool contiguous = true;
            for (uint32_t i = 0; i < size; ++i)
            {
                const uint32_t index = pSrvManager_->Allocate();
                if (i == 0)
                    firstAllocated = index;
                else if (index != firstAllocated + i)
                    contiguous = false;
            }
            return contiguous ? (firstAllocated + 1) : 0;
        };
        prefilterTableIndex_ = allocateTable(kPrefilterTableSize);
        distortTableIndex_ = allocateTable(kDistortTableSize);
        tablesReady_ = (prefilterTableIndex_ != 0 && distortTableIndex_ != 0);
        if (!tablesReady_)
        {
            Logger::Error("ブルーム用のディスクリプタを連続で確保できませんでした。ブルームは無効になります。");
        }
    }

    CreateResources(WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight());

    initialized_ = (mipResources_[0] != nullptr && beforeParticleResource_ != nullptr && tablesReady_);
}

void BloomPass::Finalize()
{
    pPrefilterCb_ = nullptr;
    pCompositeCb_ = nullptr;
    pDistortionCb_ = nullptr;
    prefilterCb_.Reset();
    compositeCb_.Reset();
    distortionCb_.Reset();
    for (uint32_t i = 0; i < kMipCount; ++i)
    {
        pDownsampleCb_[i] = nullptr;
        pUpsampleCb_[i] = nullptr;
        downsampleCb_[i].Reset();
        upsampleCb_[i].Reset();
        mipResources_[i].Reset();
    }
    beforeParticleResource_.Reset();
    initialized_ = false;
    pDxCommon_ = nullptr;
    pSrvManager_ = nullptr;
}

void BloomPass::CreateResources(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;

    width_ = width;
    height_ = height;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kBloomFormat;

    // パーティクル描画前の控え。シーンと同じ寸法・同じフォーマット（CopyResource するため）
    beforeParticleResource_ = pDxCommon_->CreateRenderTextureResource(width, height, kBloomFormat,
                                                                      clearValue, /*allowUAV=*/false);
    if (beforeParticleResource_)
    {
        beforeParticleResource_->SetName(L"Bloom_BeforeParticle");
        beforeParticleSrvIndex_ = pSrvManager_->Allocate() + 1;
        pSrvManager_->CreateSRVforRenderTexture(beforeParticleSrvIndex_, beforeParticleResource_.Get(), kBloomFormat);
    }

    // ミップ列。半解像度から1段ずつ半分にする
    uint32_t w = width;
    uint32_t h = height;
    for (uint32_t i = 0; i < kMipCount; ++i)
    {
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
        mipWidth_[i] = w;
        mipHeight_[i] = h;

        mipResources_[i] = pDxCommon_->CreateRenderTextureResource(w, h, kBloomFormat,
                                                                    clearValue, /*allowUAV=*/true);
        if (!mipResources_[i])
        {
            Logger::Error("ブルーム用のミップテクスチャを作成できませんでした。");
            return;
        }
        mipSrvIndex_[i] = pSrvManager_->Allocate() + 1;
        pSrvManager_->CreateSRVforRenderTexture(mipSrvIndex_[i], mipResources_[i].Get(), kBloomFormat);
        mipUavIndex_[i] = pSrvManager_->Allocate() + 1;
        pSrvManager_->CreateUAVforTexture2D(mipUavIndex_[i], mipResources_[i].Get(), kBloomFormat);
    }
}

void BloomPass::EnsureResolution()
{
    const uint32_t width = WinApp::GetVirtualWidth();
    const uint32_t height = WinApp::GetVirtualHeight();
    if (width == width_ && height == height_)
        return;
    CreateResources(width, height);
}

void BloomPass::WriteSrv(uint32_t tableIndex, uint32_t slot, ID3D12Resource *resource)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipLevels = 1;
    desc.Format = kBloomFormat;
    pDxCommon_->GetDevice()->CreateShaderResourceView(resource, &desc,
                                                      pSrvManager_->GetCPUDescriptorHandle(tableIndex + slot));
}

void BloomPass::CaptureBeforeParticles(ID3D12Resource *sceneResource)
{
    beforeParticleCaptured_ = false;
    if (!initialized_ || !enabled_ || !particleOnly_ || sceneResource == nullptr)
        return;

    EnsureResolution();
    if (!beforeParticleResource_)
        return;

    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();

    // シーンをそのままコピーして控える。ここで取ったものと描画後の差が
    // 「パーティクルがこのフレームで足した色」になる
    pDxCommon_->BarrierTransition(sceneResource, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                  D3D12_RESOURCE_STATE_COPY_SOURCE);
    pDxCommon_->BarrierTransition(beforeParticleResource_.Get(), D3D12_RESOURCE_STATE_GENERIC_READ,
                                  D3D12_RESOURCE_STATE_COPY_DEST);

    pCommandList->CopyResource(beforeParticleResource_.Get(), sceneResource);

    pDxCommon_->BarrierTransition(beforeParticleResource_.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                  D3D12_RESOURCE_STATE_GENERIC_READ);
    pDxCommon_->BarrierTransition(sceneResource, D3D12_RESOURCE_STATE_COPY_SOURCE,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET);

    beforeParticleCaptured_ = true;
}

void BloomPass::Render(ID3D12Resource *sceneResource)
{
    if (!initialized_ || !enabled_ || sceneResource == nullptr)
        return;
    // パーティクルのみモードなのに控えが取れていないフレームは何もしない
    // （取れていない状態で差を取ると画面全体が光ってしまう）
    if (particleOnly_ && !beforeParticleCaptured_)
        return;

    EnsureResolution();
    if (!mipResources_[0])
        return;

    const uint32_t mipCount = std::clamp(activeMipCount_, 1u, kMipCount);

    // ポストエフェクトと同じく線形補間・端クランプで読む
    const std::vector<ShaderRootSignature::SamplerPreset> samplers = {
        ShaderRootSignature::SamplerPreset::LinearClamp,
    };
    ComputeEffectPipeline *pipeline = ComputeEffectPipeline::GetInstance();
    const ComputeEffectProgram *prefilter = pipeline->Get(kPrefilterShader, samplers);
    const ComputeEffectProgram *downsample = pipeline->Get(kDownsampleShader, samplers);
    const ComputeEffectProgram *upsample = pipeline->Get(kUpsampleShader, samplers);
    const ComputeEffectProgram *composite = pipeline->Get(kCompositeShader, samplers);
    // 歪みは使うときだけ取りに行く（未使用ならコンパイルもされない）
    const bool wantDistortion = distortionEnabled_ && particleOnly_ && beforeParticleCaptured_;
    const ComputeEffectProgram *distortion = wantDistortion ? pipeline->Get(kDistortionShader, samplers) : nullptr;
    if (!prefilter || !prefilter->IsValid() || !downsample || !downsample->IsValid() ||
        !upsample || !upsample->IsValid() || !composite || !composite->IsValid())
    {
        return;
    }

    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();
    pSrvManager_->SetDescriptorHeap();

    // ディスパッチ1回ぶんの共通処理。SRV/UAV/CBV をバインドして投げる
    auto dispatch = [&](const ComputeEffectProgram *program, uint32_t srvTable, uint32_t uavIndex,
                        ID3D12Resource *cb, uint32_t dstW, uint32_t dstH) {
        pCommandList->SetPipelineState(program->pipelineState.Get());
        pCommandList->SetComputeRootSignature(program->rootSignature.Get());
        if (program->rootSignature.GetSrvTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(program->rootSignature.GetSrvTableIndex(),
                                                        pSrvManager_->GetGPUDescriptorHandle(srvTable));
        }
        if (program->rootSignature.GetUavTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(program->rootSignature.GetUavTableIndex(),
                                                        pSrvManager_->GetGPUDescriptorHandle(uavIndex));
        }
        const UINT cbvIndex = program->rootSignature.GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 0);
        if (cbvIndex != UINT_MAX)
            pCommandList->SetComputeRootConstantBufferView(cbvIndex, cb->GetGPUVirtualAddress());

        const UINT groupX = (dstW + program->threadGroupSizeX - 1) / program->threadGroupSizeX;
        const UINT groupY = (dstH + program->threadGroupSizeY - 1) / program->threadGroupSizeY;
        pCommandList->Dispatch(groupX, groupY, 1);
    };

    // ── 1. 抽出（シーン → ミップ0）──
    {
        // シーンを読むために一度シェーダーリソースへ移す
        pDxCommon_->BarrierTransition(sceneResource, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // t0 = パーティクル後のシーン / t1 = 描画前の控え。
        // 全画面モードでは t1 も使わないが、テーブルに穴を空けられないので同じものを入れる
        WriteSrv(prefilterTableIndex_, 0, sceneResource);
        WriteSrv(prefilterTableIndex_, 1,
                 particleOnly_ ? beforeParticleResource_.Get() : sceneResource);

        pPrefilterCb_->threshold = threshold_;
        pPrefilterCb_->knee = knee_;
        pPrefilterCb_->particleOnly = particleOnly_ ? 1u : 0u;
        pPrefilterCb_->dstSize[0] = static_cast<int32_t>(mipWidth_[0]);
        pPrefilterCb_->dstSize[1] = static_cast<int32_t>(mipHeight_[0]);
        pPrefilterCb_->srcSize[0] = static_cast<int32_t>(width_);
        pPrefilterCb_->srcSize[1] = static_cast<int32_t>(height_);

        pDxCommon_->BarrierTransition(mipResources_[0].Get(), D3D12_RESOURCE_STATE_GENERIC_READ,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(prefilter, prefilterTableIndex_, mipUavIndex_[0], prefilterCb_.Get(),
                 mipWidth_[0], mipHeight_[0]);
        pDxCommon_->BarrierTransition(mipResources_[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);

        pDxCommon_->BarrierTransition(sceneResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                      D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    // ── 2. 縮小（ミップ i-1 → i）──
    for (uint32_t i = 1; i < mipCount; ++i)
    {
        pDownsampleCb_[i]->dstSize[0] = static_cast<int32_t>(mipWidth_[i]);
        pDownsampleCb_[i]->dstSize[1] = static_cast<int32_t>(mipHeight_[i]);
        pDownsampleCb_[i]->srcSize[0] = static_cast<int32_t>(mipWidth_[i - 1]);
        pDownsampleCb_[i]->srcSize[1] = static_cast<int32_t>(mipHeight_[i - 1]);

        pDxCommon_->BarrierTransition(mipResources_[i].Get(), D3D12_RESOURCE_STATE_GENERIC_READ,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(downsample, mipSrvIndex_[i - 1], mipUavIndex_[i], downsampleCb_[i].Get(),
                 mipWidth_[i], mipHeight_[i]);
        pDxCommon_->BarrierTransition(mipResources_[i].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
    }

    // ── 2.5 歪み（熱揺らぎ）──
    // ミップ列が出来上がった今なら、縮小済み＝ぼけた「パーティクルだけの絵」が
    // そのまま使える。これをマスクにして背景をずらす。
    // 拡大加算の前に済ませるのは、ここでミップを書き換えられてしまう前だから
    if (distortion && distortion->IsValid())
    {
        const uint32_t maskMip = std::min(distortionMaskMip_, mipCount - 1);
        distortionTime_ += Frame::UnscaledDeltaTime();

        // t0 = 粒を描く前の控え（背景） / t1 = マスクに使うミップ
        WriteSrv(distortTableIndex_, 0, beforeParticleResource_.Get());
        WriteSrv(distortTableIndex_, 1, mipResources_[maskMip].Get());

        pDistortionCb_->dstSize[0] = static_cast<int32_t>(width_);
        pDistortionCb_->dstSize[1] = static_cast<int32_t>(height_);
        pDistortionCb_->strength = distortionStrength_;
        pDistortionCb_->frequency = distortionFrequency_;
        pDistortionCb_->speed = distortionSpeed_;
        pDistortionCb_->time = distortionTime_;
        pDistortionCb_->maskGain = distortionMaskGain_;

        pDxCommon_->BarrierTransition(sceneResource, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(distortion, distortTableIndex_, pDxCommon_->GetOffScreenUavIndex(), distortionCb_.Get(),
                 width_, height_);
        pDxCommon_->BarrierTransition(sceneResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    // ── 3. 拡大して加算（ミップ i → i-1）──
    // 一番小さい段から上へ。各段は自分の値へ足し込むので UAV として読み書きする
    for (uint32_t i = mipCount - 1; i >= 1; --i)
    {
        pUpsampleCb_[i]->dstSize[0] = static_cast<int32_t>(mipWidth_[i - 1]);
        pUpsampleCb_[i]->dstSize[1] = static_cast<int32_t>(mipHeight_[i - 1]);
        pUpsampleCb_[i]->srcSize[0] = static_cast<int32_t>(mipWidth_[i]);
        pUpsampleCb_[i]->srcSize[1] = static_cast<int32_t>(mipHeight_[i]);
        pUpsampleCb_[i]->filterRadius = filterRadius_;
        pUpsampleCb_[i]->blend = 1.0f;

        pDxCommon_->BarrierTransition(mipResources_[i - 1].Get(), D3D12_RESOURCE_STATE_GENERIC_READ,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(upsample, mipSrvIndex_[i], mipUavIndex_[i - 1], upsampleCb_[i].Get(),
                 mipWidth_[i - 1], mipHeight_[i - 1]);
        pDxCommon_->BarrierTransition(mipResources_[i - 1].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);
    }

    // ── 4. 合成（ミップ0 → シーンへ加算）──
    {
        pCompositeCb_->dstSize[0] = static_cast<int32_t>(width_);
        pCompositeCb_->dstSize[1] = static_cast<int32_t>(height_);
        pCompositeCb_->intensity = intensity_;

        // シーンは SRV ではなく UAV として読み書きする（同じリソースを
        // SRV と UAV へ同時に入れることはできないため）
        pDxCommon_->BarrierTransition(sceneResource, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch(composite, mipSrvIndex_[0], pDxCommon_->GetOffScreenUavIndex(), compositeCb_.Get(),
                 width_, height_);
        pDxCommon_->BarrierTransition(sceneResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    beforeParticleCaptured_ = false;
}

void BloomPass::LoadData()
{
    DataHandler data("PostEffect", kDataFileName);
    enabled_ = data.Load<bool>("enabled", true);
    particleOnly_ = data.Load<bool>("particleOnly", true);
    threshold_ = data.Load<float>("threshold", 1.0f);
    knee_ = data.Load<float>("knee", 0.5f);
    intensity_ = data.Load<float>("intensity", 0.6f);
    filterRadius_ = data.Load<float>("filterRadius", 0.005f);
    activeMipCount_ = data.Load<uint32_t>("mipCount", kMipCount);
    distortionEnabled_ = data.Load<bool>("distortionEnabled", false);
    distortionStrength_ = data.Load<float>("distortionStrength", 0.015f);
    distortionFrequency_ = data.Load<float>("distortionFrequency", 26.0f);
    distortionSpeed_ = data.Load<float>("distortionSpeed", 0.35f);
    distortionMaskGain_ = data.Load<float>("distortionMaskGain", 20.0f);
    distortionMaskMip_ = data.Load<uint32_t>("distortionMaskMip", 2);
    distortionMaskMip_ = std::clamp(distortionMaskMip_, 0u, kMipCount - 1u);
    activeMipCount_ = std::clamp(activeMipCount_, 1u, kMipCount);
}

void BloomPass::SaveData()
{
    DataHandler data("PostEffect", kDataFileName);
    data.Save<bool>("enabled", enabled_);
    data.Save<bool>("particleOnly", particleOnly_);
    data.Save<float>("threshold", threshold_);
    data.Save<float>("knee", knee_);
    data.Save<float>("intensity", intensity_);
    data.Save<float>("filterRadius", filterRadius_);
    data.Save<uint32_t>("mipCount", activeMipCount_);
    data.Save<bool>("distortionEnabled", distortionEnabled_);
    data.Save<float>("distortionStrength", distortionStrength_);
    data.Save<float>("distortionFrequency", distortionFrequency_);
    data.Save<float>("distortionSpeed", distortionSpeed_);
    data.Save<float>("distortionMaskGain", distortionMaskGain_);
    data.Save<uint32_t>("distortionMaskMip", distortionMaskMip_);
}

void BloomPass::DrawImGui()
{
#ifdef USE_IMGUI
    if (!ThemedHeader(ICON_FA_SUN " ブルーム（ミップ列）", DebugTheme::kAccentOrange, true))
    {
        return;
    }
    ImGui::Indent();

    AccentCheckbox("有効##bloomPass", &enabled_, DebugTheme::kAccentGreen);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ミップ列方式のブルーム。\nポストエフェクト一覧の旧「ブルーム」とは別物で、こちらの方が広く自然に滲む。");

    AccentCheckbox("パーティクルだけを光らせる##bloomParticleOnly", &particleOnly_,
                   DebugTheme::kAccentBlue);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ON : パーティクル描画の前後の差だけを光らせる（キャラや地形は光らない）\n"
                          "OFF: 画面全体の明るい部分を光らせる\n\n"
                          "※ ON のときはGPUパーティクルだけが対象。\n"
                          "　 デバッグ線とUIは対象外（パーティクルより後に描かれるため）。");
    }

    ImGui::DragFloat("しきい値##bloomThreshold", &threshold_, 0.01f, 0.0f, 20.0f, "%.2f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("この明るさを超えた分が光る。\nシーンはHDRなので 1.0 を超える値も普通にある。");

    ImGui::DragFloat("なめらかさ##bloomKnee", &knee_, 0.01f, 0.0f, 2.0f, "%.2f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("しきい値の境目のなめらかさ。\n0 にすると境目でチラつきやすくなる。");

    ImGui::DragFloat("強さ##bloomIntensity", &intensity_, 0.01f, 0.0f, 5.0f, "%.2f");

    ImGui::DragFloat("滲みの広がり##bloomRadius", &filterRadius_, 0.0005f, 0.0f, 0.05f, "%.4f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("拡大するときのテント幅（UV空間）。\n大きいほど柔らかく広がるが、大きすぎると形が崩れる。");

    int mipCount = static_cast<int>(activeMipCount_);
    if (ImGui::SliderInt("段数##bloomMip", &mipCount, 1, static_cast<int>(kMipCount)))
    {
        activeMipCount_ = static_cast<uint32_t>(std::clamp(mipCount, 1, static_cast<int>(kMipCount)));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("多いほど滲みが広い範囲へ届く。\n減らすと光の周りだけが締まって光る。");

    // ---- 歪み（熱揺らぎ）----
    ImGui::Spacing();
    ImGui::SeparatorText("歪み（熱揺らぎ）");
    AccentCheckbox("有効##bloomDistort", &distortionEnabled_, DebugTheme::kAccentBlue);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("強いオーラのまわりの空気が揺れて、向こう側の景色が歪んで見えるやつ。\n"
                          "ブルームが作ったパーティクルだけのミップをそのままマスクに使うので、\n"
                          "法線テクスチャなどの素材は1枚も要りません。\n\n"
                          "歪むのは背景だけで、パーティクル自身は歪みません。");
    }
    if (distortionEnabled_ && !particleOnly_)
    {
        ImGui::TextColored(DebugTheme::kAccentOrange,
                           "※「パーティクルだけを光らせる」がONでないと動きません");
    }
    if (distortionEnabled_)
    {
        ImGui::DragFloat("揺れの大きさ##distStrength", &distortionStrength_, 0.0005f, 0.0f, 0.1f, "%.4f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("背景をずらす量（UV空間）。\n0.03 を超えると景色が割れて見えます。");

        ImGui::DragFloat("ゆらぎの細かさ##distFreq", &distortionFrequency_, 0.5f, 1.0f, 200.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("大きいほど細かく波打ちます。熱揺らぎは 20〜40 くらいが自然。");

        ImGui::DragFloat("ゆらぎの速さ##distSpeed", &distortionSpeed_, 0.01f, 0.0f, 5.0f, "%.2f");

        ImGui::DragFloat("マスクの効き##distGain", &distortionMaskGain_, 0.05f, 0.0f, 20.0f, "%.2f");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("パーティクルの明るさ→揺れ量の倍率。\n"
                              "上げるほど暗い粒のまわりでも揺れますが、上げすぎると画面全体が揺れます。");
        }

        int maskMip = static_cast<int>(distortionMaskMip_);
        if (ImGui::SliderInt("揺れの広がり（段）##distMip", &maskMip, 0, static_cast<int>(kMipCount) - 1))
        {
            distortionMaskMip_ = static_cast<uint32_t>(std::clamp(maskMip, 0, static_cast<int>(kMipCount) - 1));
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("マスクに使うミップの段。\n"
                              "大きいほどぼけたマスクになり、粒の外側まで広く揺れます。\n"
                              "0 は粒のほぼ真上だけが揺れます。");
        }
    }

    if (ConfirmButton(ICON_FA_SAVE " ブルーム設定を保存", ImVec2(-1.0f, 0.0f)))
    {
        SaveData();
    }

    ImGui::Unindent();
#endif // USE_IMGUI
}
} // namespace Hagine
