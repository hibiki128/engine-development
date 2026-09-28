#include "SceneViewRenderer.h"
#include "RenderView.h"
#include <DirectXCommon.h>
#include <MyMath.h>
#include <WinApp.h>
#include <algorithm>
#include <camera/Camera.h>
#include <camera/CameraManager.h>
#include <format>
#include <graphics/pipeline/ComputeEffectPipeline.h>
#include <graphics/srv/SrvManager.h>
#include <render/ToneMapSettings.h>
#include <line/LineRenderer.h>
#include <particle/ParticleEmitter.h>
#include <particle/gpu/ParticleCSSpawner.h>
#include <object/base/BaseObjectManager.h>
#include <skybox/SkyBox.h>
#ifdef USE_IMGUI
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>
#include "utility/debug/imgui/DebugUIHelper.h"
#endif

namespace Hagine {

namespace {
constexpr float kClearColor[4] = {0.07f, 0.08f, 0.10f, 1.0f}; // 空を描かないときの背景
constexpr uint32_t kMinSize = 16;
constexpr const char *kToneMapShader = "OffScreen/SceneViewToneMap.CS.hlsl";
} // namespace

void SceneViewRenderer::Transition(ID3D12GraphicsCommandList *pCommandList, ID3D12Resource *pResource,
                                   D3D12_RESOURCE_STATES &state, D3D12_RESOURCE_STATES next)
{
    if (state == next)
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = pResource;
    barrier.Transition.StateBefore = state;
    barrier.Transition.StateAfter = next;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    pCommandList->ResourceBarrier(1, &barrier);
    state = next;
}

SceneViewRenderer *SceneViewRenderer::GetInstance()
{
    static SceneViewRenderer instance;
    return &instance;
}

bool SceneViewRenderer::IsAnyOpen() const
{
    for (const View &view : views_)
    {
        if (view.open)
        {
            return true;
        }
    }
    return false;
}

void SceneViewRenderer::OpenNextView()
{
    for (View &view : views_)
    {
        if (!view.open)
        {
            view.open = true;
            return;
        }
    }
}

void SceneViewRenderer::Finalize()
{
    for (View &view : views_)
    {
        if (view.lineCamera && view.pLineCamera)
        {
            view.lineCamera->Unmap(0, nullptr);
        }
        view.pLineCamera = nullptr;
        view.lineCamera.Reset();
        view.display.Reset();
        farDepth_.Reset();
        farDepthCleared_ = false;
        RenderView::SetFarDepthSrvIndex(0);
        view.color.Reset();
        view.depth.Reset();
        view.created = false;
    }
}

void SceneViewRenderer::CreateTargets(int index)
{
    View &view = views_[index];
    DirectXCommon *pDxCommon = DirectXCommon::GetInstance();
    SrvManager *pSrvManager = SrvManager::GetInstance();

    // 描画先はシーンの仮想解像度と同じ大きさで作り、窓の大きさぶん（左上）だけ使う
    if (maxWidth_ == 0)
    {
        maxWidth_ = static_cast<uint32_t>(WinApp::GetVirtualWidth());
        maxHeight_ = static_cast<uint32_t>(WinApp::GetVirtualHeight());
    }

    // 色（シーンと同じ HDR の形式。オブジェクトのパイプラインをそのまま使うため）
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kSceneColorFormat;
    std::copy(std::begin(kClearColor), std::end(kClearColor), clearValue.Color);
    view.color = pDxCommon->CreateRenderTextureResource(maxWidth_, maxHeight_, clearValue.Format, clearValue);
    view.colorState = D3D12_RESOURCE_STATE_GENERIC_READ; // CreateRenderTextureResource の初期状態

    view.srvIndex = pSrvManager->Allocate() + 1; // Allocate は「予約した次」に書く決まり
    pSrvManager->CreateSRVforRenderTexture(view.srvIndex, view.color.Get());

    D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = pDxCommon->GetRTVDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
    const UINT rtvSize = pDxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    view.rtv.ptr = rtvStart.ptr + static_cast<SIZE_T>(kFirstRtvSlot + index) * rtvSize;
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = kSceneColorFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    pDxCommon->GetDevice()->CreateRenderTargetView(view.color.Get(), &rtvDesc, view.rtv);

    // 深度（パイプラインが要求する D24S8）
    view.depth = pDxCommon->CreateAdditionalDepthResource(static_cast<int32_t>(maxWidth_), static_cast<int32_t>(maxHeight_));
    view.dsv = pDxCommon->GetDSVCPUDescriptorHandle(kFirstDsvSlot + index);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    pDxCommon->GetDevice()->CreateDepthStencilView(view.depth.Get(), &dsvDesc, view.dsv);

    // 表示用（トーンマップ後）。コンピュートで書くので UAV を許可する
    view.display = pDxCommon->CreateRenderTextureResource(maxWidth_, maxHeight_, clearValue.Format, clearValue, true);
    view.displayState = D3D12_RESOURCE_STATE_GENERIC_READ;
    view.displaySrvIndex = pSrvManager->Allocate() + 1;
    pSrvManager->CreateSRVforRenderTexture(view.displaySrvIndex, view.display.Get());
    view.displayUavIndex = pSrvManager->Allocate() + 1;
    pSrvManager->CreateUAVforTexture2D(view.displayUavIndex, view.display.Get(), kSceneColorFormat);

    // 線を描くときのビュー射影行列
    view.lineCamera = pDxCommon->CreateBufferResource(sizeof(Matrix4x4));
    view.lineCamera->Map(0, nullptr, reinterpret_cast<void **>(&view.pLineCamera));
    *view.pLineCamera = MakeIdentity4x4();

    view.created = true;
}

void SceneViewRenderer::Render()
{
    // GPU パーティクルはメインのカメラの視界で粒を間引くので、窓がある間は間引きを止めてもらう
    const bool anyOpen = IsAnyOpen();
    RenderView::SetExtraViewsActive(anyOpen);
    if (!anyOpen)
    {
        ParticleEmitter::ClearDrawnMarks();
        return;
    }
    CreateFarDepth();
    for (int i = 0; i < kViewCount; ++i)
    {
        if (views_[i].open)
        {
            RenderOne(i);
        }
    }
    RenderView::SetCurrent(0);
    // この後のメインの描画で、改めて「描かれた」印が付く
    ParticleEmitter::ClearDrawnMarks();
}

void SceneViewRenderer::CreateFarDepth()
{
    if (farDepthCleared_)
    {
        return;
    }
    DirectXCommon *pDxCommon = DirectXCommon::GetInstance();
    if (maxWidth_ == 0)
    {
        maxWidth_ = static_cast<uint32_t>(WinApp::GetVirtualWidth());
        maxHeight_ = static_cast<uint32_t>(WinApp::GetVirtualHeight());
    }
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_R32_FLOAT;
    clearValue.Color[0] = 1.0f;
    farDepth_ = pDxCommon->CreateRenderTextureResource(maxWidth_, maxHeight_, clearValue.Format, clearValue);
    const uint32_t srvIndex = SrvManager::GetInstance()->Allocate() + 1;
    SrvManager::GetInstance()->CreateSRVforRenderTexture(srvIndex, farDepth_.Get(), DXGI_FORMAT_R32_FLOAT);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = pDxCommon->GetRTVDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(kFarDepthRtvSlot) * pDxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    pDxCommon->GetDevice()->CreateRenderTargetView(farDepth_.Get(), &rtvDesc, rtv);

    // 1回だけ「一番奥（1.0）」で塗って、以後は読むだけ
    ID3D12GraphicsCommandList *pCommandList = pDxCommon->GetCommandList().Get();
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_GENERIC_READ;
    Transition(pCommandList, farDepth_.Get(), state, D3D12_RESOURCE_STATE_RENDER_TARGET);
    pCommandList->ClearRenderTargetView(rtv, clearValue.Color, 0, nullptr);
    Transition(pCommandList, farDepth_.Get(), state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    RenderView::SetFarDepthSrvIndex(srvIndex);
    farDepthCleared_ = true;
}

void SceneViewRenderer::RenderOne(int index)
{
    View &view = views_[index];
    CameraManager *pCameraManager = CameraManager::GetInstance();
    Camera *pCamera = view.cameraName.empty() ? nullptr : pCameraManager->Find(view.cameraName);
    if (!pCamera)
    {
        pCamera = pCameraManager->GetActive(); // 指定が無い・シーンを切り替えて居なくなったら、今のカメラ
    }
    if (!pCamera)
    {
        return;
    }
    if (!view.created)
    {
        CreateTargets(index);
    }

    DirectXCommon *pDxCommon = DirectXCommon::GetInstance();
    ID3D12GraphicsCommandList *pCommandList = pDxCommon->GetCommandList().Get();

    // 描画先へ切り替える
    Transition(pCommandList, view.color.Get(), view.colorState, D3D12_RESOURCE_STATE_RENDER_TARGET);
    pCommandList->OMSetRenderTargets(1, &view.rtv, false, &view.dsv);
    pCommandList->ClearRenderTargetView(view.rtv, kClearColor, 0, nullptr);
    pCommandList->ClearDepthStencilView(view.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // 窓の大きさだけ使う（左上）。後に続くメインの描画はビューポートを張り直す
    const uint32_t width = std::clamp(view.width, kMinSize, maxWidth_);
    const uint32_t height = std::clamp(view.height, kMinSize, maxHeight_);
    D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    pCommandList->RSSetViewports(1, &viewport);
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    pCommandList->RSSetScissorRects(1, &scissor);
    SrvManager::GetInstance()->SetDescriptorHeap();

    // カメラの位置・向きはそのまま、縦横比だけ窓に合わせる（合わせないと絵が伸びる）
    const ViewProjection &source = pCamera->GetViewProjection();
    ViewProjection &vp = view.viewProjection;
    vp.translation_ = source.translation_;
    vp.eulerRotation_ = source.eulerRotation_;
    vp.quaternionRotation_ = source.quaternionRotation_;
    vp.isUseQuaternion_ = source.isUseQuaternion_;
    vp.fovAngleY_ = source.fovAngleY_;
    vp.nearZ_ = source.nearZ_;
    vp.farZ_ = source.farZ_;
    vp.aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    vp.matWorld_ = source.matWorld_;
    vp.matView_ = source.matView_;
    vp.matProjection_ = MakePerspectiveFovMatrix(vp.fovAngleY_, vp.aspectRatio, vp.nearZ_, vp.farZ_);

    // ここから先の描画はこのビュー専用の定数バッファを使う
    RenderView::SetCurrent(index + 1);
    if (view.drawSky && SkyBox::GetInstance()->IsReady())
    {
        SkyBox::GetInstance()->Draw(vp);
    }
    BaseObjectManager::GetInstance()->Draw(vp);
    if (view.drawParticles)
    {
        // GPU パーティクル（シーンに出ているもの）と CPU パーティクル（前のフレームにメインで描かれたもの）
        ParticleCSSpawner::GetInstance()->DrawGraphicsAllStages(vp);
        ParticleEmitter::DrawAllForView(vp);
    }
    if (view.drawLines)
    {
        *view.pLineCamera = vp.matView_ * vp.matProjection_;
        LineRenderer::GetInstance()->RenderWithExternalCamera(pCommandList, view.lineCamera->GetGPUVirtualAddress());
    }
    RenderView::SetCurrent(0);

    // メインの画面と同じトーンマップを掛けて、ImGui で読める表示用へ書く
    ToneMap(view, width, height);

    view.renderedWidth = width;
    view.renderedHeight = height;
    view.renderedCameraName = pCamera->GetName();
}

void SceneViewRenderer::ToneMap(View &view, uint32_t width, uint32_t height)
{
    ID3D12GraphicsCommandList *pCommandList = DirectXCommon::GetInstance()->GetCommandList().Get();
    SrvManager *pSrvManager = SrvManager::GetInstance();
    const ComputeEffectProgram *program = ComputeEffectPipeline::GetInstance()->Get(kToneMapShader);

    Transition(pCommandList, view.color.Get(), view.colorState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(pCommandList, view.display.Get(), view.displayState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (program && program->IsValid())
    {
        pCommandList->SetPipelineState(program->pipelineState.Get());
        pCommandList->SetComputeRootSignature(program->rootSignature.Get());
        if (program->rootSignature.GetSrvTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(program->rootSignature.GetSrvTableIndex(),
                                                        pSrvManager->GetGPUDescriptorHandle(view.srvIndex));
        }
        if (program->rootSignature.GetUavTableIndex() != UINT_MAX)
        {
            pCommandList->SetComputeRootDescriptorTable(program->rootSignature.GetUavTableIndex(),
                                                        pSrvManager->GetGPUDescriptorHandle(view.displayUavIndex));
        }
        const UINT cbvIndex = program->rootSignature.GetRootParameterIndex(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 0);
        if (cbvIndex != UINT_MAX)
        {
            pCommandList->SetComputeRootConstantBufferView(cbvIndex, ToneMapSettings::GetInstance()->GetGpuAddress());
        }
        pCommandList->Dispatch((width + program->threadGroupSizeX - 1) / program->threadGroupSizeX,
                               (height + program->threadGroupSizeY - 1) / program->threadGroupSizeY, 1);
    }
    Transition(pCommandList, view.display.Get(), view.displayState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SceneViewRenderer::DrawImGui()
{
#ifdef USE_IMGUI
    for (int i = 0; i < kViewCount; ++i)
    {
        if (views_[i].open)
        {
            DrawViewWindow(i);
        }
    }
#endif // USE_IMGUI
}

void SceneViewRenderer::DrawViewWindow(int index)
{
#ifdef USE_IMGUI
    View &view = views_[index];
    const std::string title = std::format(ICON_FA_VIDEO " カメラビュー {}###SceneCameraView{}", index + 1, index);
    // 初めて開くときはメイン画面の中に置く（何もしないと別の OS ウィンドウになる）
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->WorkPos.x + 80.0f + 40.0f * index,
                                   ImGui::GetMainViewport()->WorkPos.y + 120.0f + 40.0f * index),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(560.0f, 380.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &view.open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End();
        return;
    }

    // ---- 見るカメラ ----
    CameraManager *pCameraManager = CameraManager::GetInstance();
    const bool missing = !view.cameraName.empty() && !pCameraManager->Find(view.cameraName);
    const std::string preview = view.cameraName.empty() ? std::string("(今のカメラ)") : view.cameraName + (missing ? " (見つかりません)" : "");
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##viewCamera", preview.c_str()))
    {
        if (ImGui::Selectable("(今のカメラ)", view.cameraName.empty()))
        {
            view.cameraName.clear();
        }
        ImGui::SetItemTooltip("メインの画面と同じカメラ（デバッグカメラを使っていればデバッグカメラ）");
        for (const std::string &name : pCameraManager->GetCameraNames())
        {
            if (ImGui::Selectable(name.c_str(), name == view.cameraName))
            {
                view.cameraName = name;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("このウィンドウに映すカメラ");
    ImGui::SameLine();
    ImGui::Checkbox("空##viewSky", &view.drawSky);
    ImGui::SameLine();
    ImGui::Checkbox("粒##viewParticles", &view.drawParticles);
    ImGui::SetItemTooltip("パーティクルも描く（ブルームは掛からない）");
    ImGui::SameLine();
    ImGui::Checkbox("線##viewLines", &view.drawLines);
    ImGui::SetItemTooltip("コライダー・カリングの箱などのデバッグ線も描く");
    ImGui::SameLine();
    ImGui::BeginDisabled(!std::any_of(views_.begin(), views_.end(), [](const View &v) { return !v.open; }));
    if (NeutralButton(ICON_FA_PLUS "##viewAdd"))
    {
        OpenNextView();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("別のカメラビューを開く（最大 %d 個）", kViewCount);

    // ---- 映像（窓の残りいっぱい。この大きさで次のフレームを描く）----
    const ImVec2 region = ImGui::GetContentRegionAvail();
    view.width = static_cast<uint32_t>((std::max)(region.x, static_cast<float>(kMinSize)));
    view.height = static_cast<uint32_t>((std::max)(region.y, static_cast<float>(kMinSize)));
    if (view.created && view.renderedWidth > 0 && maxWidth_ > 0)
    {
        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
        const ImTextureID texture = static_cast<ImTextureID>(SrvManager::GetInstance()->GetGPUDescriptorHandle(view.displaySrvIndex).ptr);
        const ImVec2 uv1(static_cast<float>(view.renderedWidth) / static_cast<float>(maxWidth_),
                         static_cast<float>(view.renderedHeight) / static_cast<float>(maxHeight_));
        ImGui::Image(texture, region, ImVec2(0.0f, 0.0f), uv1);
        // 左上にどのカメラかを重ねる
        ImDrawList *pDraw = ImGui::GetWindowDrawList();
        const std::string label = view.renderedCameraName;
        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        pDraw->AddRectFilled(ImVec2(imageMin.x + 6.0f, imageMin.y + 6.0f),
                             ImVec2(imageMin.x + 14.0f + textSize.x, imageMin.y + 10.0f + textSize.y), IM_COL32(0, 0, 0, 150), 4.0f);
        pDraw->AddText(ImVec2(imageMin.x + 10.0f, imageMin.y + 8.0f), IM_COL32(235, 235, 240, 255), label.c_str());
    }
    else
    {
        DimText("描画の準備中…（次のフレームから映ります）");
    }
    ImGui::End();
#endif // USE_IMGUI
}

} // namespace Hagine
