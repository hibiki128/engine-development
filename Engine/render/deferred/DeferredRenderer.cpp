#include "DeferredRenderer.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include "graphics/pipeline/ComputePipelineManager.h"
#include "graphics/pipeline/PipelineManager.h"
#include "graphics/srv/SrvManager.h"
#include "light/LightGroup.h"
#include "light/ToonSettings.h"
#include "shadow/ShadowMap.h"
#include "skybox/SkyBox.h"
#include <MyMath.h>
#ifdef USE_IMGUI
#include <utility/debug/imgui/DebugUIHelper.h>
#include <imgui.h>
#endif

namespace Hagine {
namespace {
// G-Buffer のフォーマット（PipelineManager::CreateGBufferGraphicsPipeline と一致させること）
constexpr DXGI_FORMAT kGBufferFormats[DeferredRenderer::kGBufferCount] = {
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,  // GB0: アルベド
    DXGI_FORMAT_R16G16B16A16_FLOAT,   // GB1: 法線＋光沢度
    DXGI_FORMAT_R8G8B8A8_UNORM,       // GB2: マテリアル種別
};

// クリア値。法線は0ベクトル、マテリアルは「ライティング無効」で埋めておく
constexpr float kGBufferClearColors[DeferredRenderer::kGBufferCount][4] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
};
} // namespace

void DeferredRenderer::Initialize()
{
    pDxCommon_ = DirectXCommon::GetInstance();
    pSrvManager_ = SrvManager::GetInstance();
    pPsoManager_ = PipelineManager::GetInstance();

    // 定数バッファ
    constantBuffer_ = pDxCommon_->CreateBufferResource(sizeof(DeferredConstants));
    constantBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&pConstants_));
    *pConstants_ = {};

    // SRV/UAV 番号はここで一度だけ確保する（解像度変更時はディスクリプタを作り直すだけ）
    // 「予約した r に対し実書き込みは r+1」というエンジン共通の +1 規約に従う
    // （付け忘れると直前に +1 で確保したリソースのディスクリプタを上書きしてしまう）
    for (GBufferTarget &target : gBuffers_)
    {
        target.srvIndex = pSrvManager_->Allocate() + 1;
    }
    tileLightSrvIndex_ = pSrvManager_->Allocate() + 1;
    tileLightUavIndex_ = pSrvManager_->Allocate() + 1;

    CreateResources(WinApp::GetVirtualWidth(), WinApp::GetVirtualHeight());
    initialized_ = true;
}

void DeferredRenderer::Finalize()
{
    initialized_ = false;
    pConstants_ = nullptr;
    constantBuffer_.Reset();
    tileLightBuffer_.Reset();
    for (GBufferTarget &target : gBuffers_)
    {
        target.resource.Reset();
    }
}

void DeferredRenderer::CreateResources(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return;
    }

    width_ = width;
    height_ = height;
    tileCountX_ = (width_ + kTileSize - 1) / kTileSize;
    tileCountY_ = (height_ + kTileSize - 1) / kTileSize;

    ID3D12Device *pDevice = pDxCommon_->GetDevice().Get();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = pDxCommon_->GetRTVDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
    const UINT rtvSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (uint32_t i = 0; i < kGBufferCount; ++i)
    {
        GBufferTarget &target = gBuffers_[i];

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = kGBufferFormats[i];
        for (int c = 0; c < 4; ++c)
        {
            clearValue.Color[c] = kGBufferClearColors[i][c];
        }

        target.resource = pDxCommon_->CreateRenderTextureResource(width_, height_, kGBufferFormats[i], clearValue);
        target.state = D3D12_RESOURCE_STATE_GENERIC_READ; // CreateRenderTextureResource の初期状態

        // RTV
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = kGBufferFormats[i];
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        target.rtvHandle.ptr = rtvStart.ptr + static_cast<SIZE_T>(kGBufferRtvSlot + i) * rtvSize;
        pDevice->CreateRenderTargetView(target.resource.Get(), &rtvDesc, target.rtvHandle);

        // SRV
        pSrvManager_->CreateSRVforRenderTexture(target.srvIndex, target.resource.Get(), kGBufferFormats[i]);
    }

    // タイルごとのライトインデックスバッファ
    const uint32_t tileElementCount = tileCountX_ * tileCountY_ * kTileStride;
    tileLightBuffer_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t) * tileElementCount, true);
    tileLightState_ = D3D12_RESOURCE_STATE_COMMON;
    pSrvManager_->CreateSRVforStructuredBuffer(tileLightSrvIndex_, tileLightBuffer_.Get(), tileElementCount, sizeof(uint32_t));
    pSrvManager_->CreateUAVStructuredBuffer(tileLightUavIndex_, tileLightBuffer_.Get(), tileElementCount, sizeof(uint32_t));
}

void DeferredRenderer::EnsureResolution()
{
    const uint32_t width = WinApp::GetVirtualWidth();
    const uint32_t height = WinApp::GetVirtualHeight();
    if (width == width_ && height == height_)
    {
        return;
    }
    // GPUが使用中のリソースを解放するとデバッグレイヤーがエラーを出すため、
    // 作り直す前に必ず完了を待つ
    pDxCommon_->WaitForGPU();
    CreateResources(width, height);
}

void DeferredRenderer::TransitionGBuffer(GBufferTarget &target, D3D12_RESOURCE_STATES after)
{
    if (target.state == after)
    {
        return;
    }
    pDxCommon_->BarrierTransition(target.resource.Get(), target.state, after);
    target.state = after;
}

void DeferredRenderer::UpdateConstants(const ViewProjection &viewProjection)
{
    const Matrix4x4 viewProj = viewProjection.matView_ * viewProjection.matProjection_;

    pConstants_->invViewProjection = Inverse(viewProj);
    pConstants_->view = viewProjection.matView_;
    pConstants_->projection = viewProjection.matProjection_;
    pConstants_->lightViewProjection = ShadowMap::GetInstance()->GetLightViewProjection();
    pConstants_->cameraPosition = viewProjection.translation_;
    pConstants_->pointLightCount = LightGroup::GetInstance()->GetPointLightBufferCount();
    pConstants_->screenWidth = width_;
    pConstants_->screenHeight = height_;
    pConstants_->tileCountX = tileCountX_;
    pConstants_->tileCountY = tileCountY_;
    pConstants_->nearZ = viewProjection.nearZ_;
    pConstants_->farZ = viewProjection.farZ_;
    pConstants_->pointLightCapacity = LightGroup::kMaxBufferedPointLights;

    lastPointLightCount_ = pConstants_->pointLightCount;
}

void DeferredRenderer::BeginGBufferPass(const ViewProjection &viewProjection)
{
    if (!IsEnabled())
    {
        return;
    }

    EnsureResolution();
    UpdateConstants(viewProjection);

    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[kGBufferCount]{};
    for (uint32_t i = 0; i < kGBufferCount; ++i)
    {
        TransitionGBuffer(gBuffers_[i], D3D12_RESOURCE_STATE_RENDER_TARGET);
        rtvHandles[i] = gBuffers_[i].rtvHandle;
    }

    // 深度は PreRenderTexture 側で既にクリア済み。ここでは G-Buffer だけを消す
    const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = pDxCommon_->GetDSVCPUDescriptorHandle(0);
    pCommandList->OMSetRenderTargets(kGBufferCount, rtvHandles, false, &dsvHandle);
    for (uint32_t i = 0; i < kGBufferCount; ++i)
    {
        pCommandList->ClearRenderTargetView(rtvHandles[i], kGBufferClearColors[i], 0, nullptr);
    }

    // 背景合成(Blit)でビューポートが変わっている可能性があるので設定し直す
    const D3D12_VIEWPORT viewport = pDxCommon_->GetRenderViewport();
    const D3D12_RECT scissorRect = pDxCommon_->GetRenderScissorRect();
    pCommandList->RSSetViewports(1, &viewport);
    pCommandList->RSSetScissorRects(1, &scissorRect);

    gBufferPassActive_ = true;
}

void DeferredRenderer::EndGBufferPass()
{
    if (!IsEnabled() || !gBufferPassActive_)
    {
        return;
    }
    gBufferPassActive_ = false;

    for (GBufferTarget &target : gBuffers_)
    {
        TransitionGBuffer(target, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    // 深度をライトカリング(CS)とライティング(PS)から読めるようにする
    pDxCommon_->BarrierTransition(pDxCommon_->GetDepthStencilResource(),
                                  D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void DeferredRenderer::CullLights()
{
    if (!IsEnabled())
    {
        return;
    }

    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();
    pSrvManager_->SetDescriptorHeap();

    if (tileLightState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    {
        pDxCommon_->BarrierTransition(tileLightBuffer_.Get(), tileLightState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        tileLightState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // Direct Queue 上で実行する（G-Buffer の深度に依存するため非同期にはしない）
    ComputePipelineManager::GetInstance()->DrawCommonSetting(
        ComputePipelineType::LightCulling, BlendMode::Normal, ShaderMode::None, pCommandList);

    pCommandList->SetComputeRootConstantBufferView(0, constantBuffer_->GetGPUVirtualAddress());
    pCommandList->SetComputeRootDescriptorTable(1, pSrvManager_->GetGPUDescriptorHandle(pDxCommon_->GetDepthSrvIndex()));
    pCommandList->SetComputeRootShaderResourceView(2, LightGroup::GetInstance()->GetPointLightBufferAddress());
    pCommandList->SetComputeRootDescriptorTable(3, pSrvManager_->GetGPUDescriptorHandle(tileLightUavIndex_));
    // 粒子光源を含む総数はGPU側のカウンタしか知らないので、CSにそれを読ませる
    pCommandList->SetComputeRootShaderResourceView(4, LightGroup::GetInstance()->GetLightCounterAddress());

    pCommandList->Dispatch(tileCountX_, tileCountY_, 1);

    // ライティングパスが SRV として読むので遷移させる
    pDxCommon_->BarrierTransition(tileLightBuffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    tileLightState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void DeferredRenderer::RenderLighting()
{
    if (!IsEnabled())
    {
        return;
    }

    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();

    // オフスクリーンRTだけをバインドする（深度はSRVとして読むので深度書き込みなし）
    const D3D12_CPU_DESCRIPTOR_HANDLE offScreenRtv = pDxCommon_->GetRTVCPUDescriptorHandle(2);
    pCommandList->OMSetRenderTargets(1, &offScreenRtv, false, nullptr);

    // 背景合成(Blit)などでビューポートが変わっている可能性があるので設定し直す
    const D3D12_VIEWPORT viewport = pDxCommon_->GetRenderViewport();
    const D3D12_RECT scissorRect = pDxCommon_->GetRenderScissorRect();
    pCommandList->RSSetViewports(1, &viewport);
    pCommandList->RSSetScissorRects(1, &scissorRect);

    pPsoManager_->DrawCommonSetting(PipelineType::DeferredLighting);

    LightGroup *lightGroup = LightGroup::GetInstance();
    ShadowMap *shadowMap = ShadowMap::GetInstance();

    const ShaderRootSignature *rootSignature = PipelineManager::GetInstance()->GetCurrentRootSignature();
    assert(rootSignature && "ディファードライティングのルートシグネチャが未生成です");

    pCommandList->SetGraphicsRootConstantBufferView(rootSignature->GetCbvIndex(0), constantBuffer_->GetGPUVirtualAddress());
    pCommandList->SetGraphicsRootConstantBufferView(rootSignature->GetCbvIndex(1), lightGroup->GetDirectionalLightAddress());
    pCommandList->SetGraphicsRootConstantBufferView(rootSignature->GetCbvIndex(2), lightGroup->GetSpotLightsAddress());
    pCommandList->SetGraphicsRootConstantBufferView(rootSignature->GetCbvIndex(3), shadowMap->GetShadowDataGpuAddress());
    pCommandList->SetGraphicsRootConstantBufferView(rootSignature->GetCbvIndex(4), ToonSettings::GetInstance()->GetGpuAddress());

    pSrvManager_->SetGraphicsRootDescriptorTable(rootSignature->GetSrvIndex(0), gBuffers_[0].srvIndex);
    pSrvManager_->SetGraphicsRootDescriptorTable(rootSignature->GetSrvIndex(1), gBuffers_[1].srvIndex);
    pSrvManager_->SetGraphicsRootDescriptorTable(rootSignature->GetSrvIndex(2), gBuffers_[2].srvIndex);
    pSrvManager_->SetGraphicsRootDescriptorTable(rootSignature->GetSrvIndex(3), pDxCommon_->GetDepthSrvIndex());
    pSrvManager_->SetGraphicsRootDescriptorTable(rootSignature->GetSrvIndex(4), shadowMap->GetShadowSrvIndex());
    pSrvManager_->SetGraphicsRootDescriptorTable(rootSignature->GetSrvIndex(5), SkyBox::GetInstance()->GetTextureIndex());

    pCommandList->SetGraphicsRootShaderResourceView(rootSignature->GetSrvIndex(6), lightGroup->GetPointLightBufferAddress());
    pCommandList->SetGraphicsRootShaderResourceView(rootSignature->GetSrvIndex(7), tileLightBuffer_->GetGPUVirtualAddress());

    // 全画面三角形（頂点バッファ不要）
    pCommandList->DrawInstanced(3, 1, 0, 0);
}

void DeferredRenderer::BeginForwardPass()
{
    if (!IsEnabled())
    {
        return;
    }

    // 深度を書き込み可能へ戻し、以降の前方描画（空・半透明・パーティクル・線）が
    // G-Buffer パスで書いた深度に対して正しく前後判定できるようにする
    pDxCommon_->BarrierTransition(pDxCommon_->GetDepthStencilResource(),
                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                  D3D12_RESOURCE_STATE_DEPTH_WRITE);

    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();
    const D3D12_CPU_DESCRIPTOR_HANDLE offScreenRtv = pDxCommon_->GetRTVCPUDescriptorHandle(2);
    const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = pDxCommon_->GetDSVCPUDescriptorHandle(0);
    pCommandList->OMSetRenderTargets(1, &offScreenRtv, false, &dsvHandle);

    const D3D12_VIEWPORT viewport = pDxCommon_->GetRenderViewport();
    const D3D12_RECT scissorRect = pDxCommon_->GetRenderScissorRect();
    pCommandList->RSSetViewports(1, &viewport);
    pCommandList->RSSetScissorRects(1, &scissorRect);
}

void DeferredRenderer::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);
    bool enabled = enabled_;
    if (ImGui::Checkbox("ディファードレンダリングを使う", &enabled))
    {
        enabled_ = enabled;
    }
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip(
        "不透明オブジェクトをG-Bufferへ描いてから1回でライティングします。\n"
        "光源が多いほど前方描画より軽くなります。\n"
        "オフにすると従来の前方描画に戻ります（見た目は同じ）。");

    if (!initialized_)
    {
        ImGui::TextDisabled("未初期化");
        return;
    }

    ImGui::Separator();
    ImGui::Text("G-Buffer: %u x %u", width_, height_);
    ImGui::Text("タイル: %u x %u (%u px/タイル)", tileCountX_, tileCountY_, kTileSize);

    LightGroup *lightGroup = LightGroup::GetInstance();
    const uint32_t totalLights = lightGroup->GetGpuTotalLightCount();
    ImGui::Text("ポイントライト: %u / %u", totalLights, LightGroup::kMaxBufferedPointLights);
    ImGui::SetItemTooltip("CPU側（手置き＋動的）%u個 ＋ GPU生成の粒子光源 %u個",
                          lastPointLightCount_, lightGroup->GetParticleLightCount());
    if (totalLights > LightGroup::kMaxBufferedPointLights)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
        ImGui::TextWrapped("※ 光源が上限を超えています。溢れた粒子光源は捨てられます");
        ImGui::PopStyleColor();
    }
    ImGui::Text("1タイルの上限: %u", kMaxLightsPerTile);
    ImGui::SetItemTooltip("1タイルに届く光源がこれを超えると先着順で切り捨てられます。\n"
                          "粒子光源が密集すると簡単に溢れるので、間引きと半径で調整してください。");
#endif
}
} // namespace Hagine
