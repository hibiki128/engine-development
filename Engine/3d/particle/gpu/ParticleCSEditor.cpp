#define NOMINMAX
#include "ParticleCSEditor.h"
#include "DirectXCommon.h"
#include <asset/AssetPath.h>
#include "../utility/debug/imgui/ImGuizmoManager.h"
// DebugUIHelper.h は ImGui:: を使うので imgui.h（ImGuizmoManager.h 経由）の後に include する
#include "../utility/debug/imgui/DebugUIHelper.h"
#include <camera/projection/ViewProjection.h>
#include <line/LineRenderer.h>
#include <particle/ParticleEditor.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#include <browser/ShowFolder.h>
#include <icon/IconsFontAwesome5.h>
#include "render/DrawGroupManager.h"
#include <algorithm>
#include <format>
#include <MyMath.h>
#include <vector>

namespace Hagine {
void ParticleCSEditor::Finalize()
{
    emitters_.clear();

    // プレビュー窓のGPUリソースを手放す。
    // このクラスはシングルトンなので、ここで解放しないと終了時のリークチェックまで
    // 描画先・深度・線のVB・CB が残り続ける（プレビューを一度も開いていなければ
    // Initialize の時点で作られているぶんがそのまま残る）
    if (previewGridVB_ && pPreviewGridMapped_)
    {
        previewGridVB_->Unmap(0, nullptr);
    }
    if (previewWireVB_ && pPreviewWireMapped_)
    {
        previewWireVB_->Unmap(0, nullptr);
    }
    if (previewLineCB_ && pPreviewLineCBData_)
    {
        previewLineCB_->Unmap(0, nullptr);
    }
    if (previewPerViewCB_ && pPreviewPerViewData_)
    {
        previewPerViewCB_->Unmap(0, nullptr);
    }
    pPreviewGridMapped_ = nullptr;
    pPreviewWireMapped_ = nullptr;
    pPreviewLineCBData_ = nullptr;
    pPreviewPerViewData_ = nullptr;

    previewColorResource_.Reset();
    previewDepthResource_.Reset();
    previewGridVB_.Reset();
    previewWireVB_.Reset();
    previewLineCB_.Reset();
    previewPerViewCB_.Reset();

    // 次に使われたら作り直せるようにしておく
    previewInitialized_ = false;
}

void ParticleCSEditor::Initialize()
{
    pParticleGroupManager_ = ParticleCSGroupManager::GetInstance();
    // カラーテーマの初期設定
    SetupColors();
    // プレビュー窓用オフスクリーンを生成（シングルトンなので初回のみ）
    InitializePreview();
}

void ParticleCSEditor::InitializePreview()
{
    if (previewInitialized_)
    {
        return;
    }
    DirectXCommon *pDxCommon = ParticleCommon::GetInstance()->GetDxCommon();
    SrvManager *pSrvManager = SrvManager::GetInstance();

    // 暗い背景色（Effekseer 風）でクリアされる色RTを生成
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kSceneColorFormat; // 粒子のPSOがHDR前提になったのでプレビュー先もそろえる
    clearValue.Color[0] = 0.02f;
    clearValue.Color[1] = 0.02f;
    clearValue.Color[2] = 0.03f;
    clearValue.Color[3] = 1.0f;

    previewColorResource_ = pDxCommon->CreateRenderTextureResource(
        kPreviewMaxWidth_, kPreviewMaxHeight_, clearValue.Format, clearValue);
    previewColorState_ = D3D12_RESOURCE_STATE_GENERIC_READ; // CreateRenderTextureResource の初期状態

    // ImGui 表示用 SRV
    previewColorSrvIndex_ = pSrvManager->Allocate() + 1;
    pSrvManager->CreateSRVforRenderTexture(previewColorSrvIndex_, previewColorResource_.Get());

    // RTV（拡張した RTV ヒープの slot 6 を使用）
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = pDxCommon->GetRTVDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
    UINT rtvSize = pDxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    previewRtvHandle_.ptr = rtvStart.ptr + (6 * rtvSize);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = kSceneColorFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    pDxCommon->GetDevice()->CreateRenderTargetView(previewColorResource_.Get(), &rtvDesc, previewRtvHandle_);

    // 専用深度バッファ＋DSV（拡張した DSV ヒープの slot1）。kLine3d/パーティクル PSO は D24_UNORM_S8_UINT を要求する。
    previewDepthResource_ = pDxCommon->CreateAdditionalDepthResource(kPreviewMaxWidth_, kPreviewMaxHeight_);
    previewDsvHandle_ = pDxCommon->GetDSVCPUDescriptorHandle(1);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    pDxCommon->GetDevice()->CreateDepthStencilView(previewDepthResource_.Get(), &dsvDesc, previewDsvHandle_);

    // 白グリッドの頂点バッファと、カメラ viewProject 用の定数バッファを構築。
    BuildPreviewGrid();
    BuildPreviewWireBuffer();
    previewLineCB_ = pDxCommon->CreateBufferResource(sizeof(Matrix4x4));
    previewLineCB_->Map(0, nullptr, reinterpret_cast<void **>(&pPreviewLineCBData_));
    *pPreviewLineCBData_ = MakeIdentity4x4();

    // 選択エミッタ隔離描画用の per-view CB。共有グループの per-view を汚さないため専用に持つ。
    previewPerViewCB_ = pDxCommon->CreateBufferResource(sizeof(PerView));
    previewPerViewCB_->Map(0, nullptr, reinterpret_cast<void **>(&pPreviewPerViewData_));
    pPreviewPerViewData_->viewProjection = MakeIdentity4x4();
    pPreviewPerViewData_->billboardMatrix = MakeIdentity4x4();
    pPreviewPerViewData_->enableBillboard = 1;
    pPreviewPerViewData_->enableVelocityStretch = 0;
    pPreviewPerViewData_->velocityStretchFactor = 0.1f;
    pPreviewPerViewData_->enableRotation = 1; // プレビューは常に回転を計算（正確さ優先・コストは僅少）

    previewInitialized_ = true;
}

// グリッド線VBを最大容量で確保し永続マップする。内容は RebuildPreviewGridContents で書き込む。
void ParticleCSEditor::BuildPreviewGrid()
{
    DirectXCommon *pDxCommon = ParticleCommon::GetInstance()->GetDxCommon();

    // 分割数の上限ぶん（XZ各 (div+1) 本 × 2頂点）を確保。
    const UINT maxVerts = static_cast<UINT>((kPreviewGridMaxDivision_ + 1) * 4);
    const UINT vbSize = static_cast<UINT>(sizeof(LineVertex) * maxVerts);
    previewGridVB_ = pDxCommon->CreateBufferResource(vbSize);
    previewGridVB_->Map(0, nullptr, reinterpret_cast<void **>(&pPreviewGridMapped_));

    previewGridVBView_.BufferLocation = previewGridVB_->GetGPUVirtualAddress();
    previewGridVBView_.StrideInBytes = sizeof(LineVertex);
    previewGridVBView_.SizeInBytes = vbSize;

    RebuildPreviewGridContents();
}

// ワイヤーフレーム用VBを最大容量で確保し永続マップする。内容は RenderPreview で毎フレーム書き込む。
void ParticleCSEditor::BuildPreviewWireBuffer()
{
    DirectXCommon *pDxCommon = ParticleCommon::GetInstance()->GetDxCommon();
    const UINT vbSize = static_cast<UINT>(sizeof(LineVertex) * kPreviewWireMaxVerts_);
    previewWireVB_ = pDxCommon->CreateBufferResource(vbSize);
    previewWireVB_->Map(0, nullptr, reinterpret_cast<void **>(&pPreviewWireMapped_));

    previewWireVBView_.BufferLocation = previewWireVB_->GetGPUVirtualAddress();
    previewWireVBView_.StrideInBytes = sizeof(LineVertex);
    previewWireVBView_.SizeInBytes = vbSize;
    previewWireVertexCount_ = 0;
}

// 現在のグリッド設定をマップ済みVBへ書き込み、描画頂点数を更新する。
// カメラ注視点を中心に追従し、線間隔にスナップすることで「ほぼ無限」のグリッドに見せる。
// 毎フレーム呼ばれる（LineRenderer と同じ毎フレーム書き換えパターン）。
void ParticleCSEditor::RebuildPreviewGridContents()
{
    if (!pPreviewGridMapped_)
    {
        return;
    }
    int division = previewGridDivision_;
    division = (std::max)(2, (std::min)(kPreviewGridMaxDivision_, division));
    const float halfSize = (std::max)(0.1f, previewGridHalfSize_);
    const float interval = (halfSize * 2.0f) / division;
    const Vector4 gridColor = previewGridColor_;
    const Vector4 axisColorX = {0.8f, 0.25f, 0.25f, 1.0f};
    const Vector4 axisColorZ = {0.25f, 0.4f, 0.85f, 1.0f};

    // 注視点に追従。線間隔にスナップしてカメラを動かしても線がちらつかないようにする。
    const float centerX = std::round(previewCamTarget_.x / interval) * interval;
    const float centerZ = std::round(previewCamTarget_.z / interval) * interval;
    const float axisEps = interval * 0.5f;

    uint32_t v = 0;
    for (int i = 0; i <= division; ++i)
    {
        float offset = -halfSize + i * interval;
        float worldZ = centerZ + offset;
        float worldX = centerX + offset;
        // X方向の線（Z=worldZ 固定）。ワールド原点(z=0)を通る線を軸色に。
        const Vector4 &cX = (std::fabs(worldZ) < axisEps) ? axisColorX : gridColor;
        pPreviewGridMapped_[v++] = {{centerX - halfSize, 0.0f, worldZ}, PackLineColor(cX)};
        pPreviewGridMapped_[v++] = {{centerX + halfSize, 0.0f, worldZ}, PackLineColor(cX)};
        // Z方向の線（X=worldX 固定）。ワールド原点(x=0)を通る線を軸色に。
        const Vector4 &cZ = (std::fabs(worldX) < axisEps) ? axisColorZ : gridColor;
        pPreviewGridMapped_[v++] = {{worldX, 0.0f, centerZ - halfSize}, PackLineColor(cZ)};
        pPreviewGridMapped_[v++] = {{worldX, 0.0f, centerZ + halfSize}, PackLineColor(cZ)};
    }
    previewGridVertexCount_ = v;
}

// オービットカメラパラメータから view 行列と view*projection 行列を計算する。
void ParticleCSEditor::ComputePreviewMatrices(Matrix4x4 &outView, Matrix4x4 &outViewProj, Vector3 &outEye, float &outProjScaleY) const
{
    // 球面座標からカメラ位置を求める。
    float cp = std::cos(previewCamPitch_);
    Vector3 eye = {
        previewCamTarget_.x + previewCamDistance_ * cp * std::sin(previewCamYaw_),
        previewCamTarget_.y + previewCamDistance_ * std::sin(previewCamPitch_),
        previewCamTarget_.z + previewCamDistance_ * cp * std::cos(previewCamYaw_),
    };
    outEye = eye; // プレビューの距離カリング用カメラワールド座標

    // LookAt（左手系）。forward = target - eye。
    Vector3 forward = (previewCamTarget_ - eye).Normalize();
    Vector3 worldUp = {0.0f, 1.0f, 0.0f};
    Vector3 right = worldUp.Cross(forward).Normalize();
    Vector3 up = forward.Cross(right);

    Matrix4x4 cameraWorld = MakeRotateMatrix(right, up, forward);
    cameraWorld.m[3][0] = eye.x;
    cameraWorld.m[3][1] = eye.y;
    cameraWorld.m[3][2] = eye.z;
    cameraWorld.m[3][3] = 1.0f;

    outView = Inverse(cameraWorld);
    float fovY = 45.0f * 3.14159265358979323846f / 180.0f;
    // アスペクトは今フレームの実描画サイズ（ImGuiウィンドウ依存）に合わせる
    uint32_t h = (previewRenderHeight_ > 0) ? previewRenderHeight_ : 1;
    float aspect = static_cast<float>(previewRenderWidth_) / static_cast<float>(h);
    Matrix4x4 proj = MakePerspectiveFovMatrix(fovY, aspect, 0.1f, 1000.0f);
    outViewProj = outView * proj;
    outProjScaleY = proj.m[1][1]; // = cot(fovY/2)。プレビューの画面サイズカリング用
}

void ParticleCSEditor::RenderPreview()
{
    if (!previewInitialized_)
    {
        return;
    }
    DirectXCommon *pDxCommon = ParticleCommon::GetInstance()->GetDxCommon();
    ID3D12GraphicsCommandList *pCommandList = pDxCommon->GetCommandList().Get();

    // 色RT を RENDER_TARGET へ遷移
    D3D12_RESOURCE_BARRIER toRT{};
    toRT.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRT.Transition.pResource = previewColorResource_.Get();
    toRT.Transition.StateBefore = previewColorState_;
    toRT.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    pCommandList->ResourceBarrier(1, &toRT);
    previewColorState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // グリッドはカメラ注視点に追従させるため毎フレーム再構築する（内容のみ書き換え）。
    RebuildPreviewGridContents();
    previewGridDirty_ = false;

    // プレビューRT＋専用深度を束ねて背景色でクリア
    pCommandList->OMSetRenderTargets(1, &previewRtvHandle_, false, &previewDsvHandle_);
    pCommandList->ClearRenderTargetView(previewRtvHandle_, previewBgColor_, 0, nullptr);
    pCommandList->ClearDepthStencilView(previewDsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // 実描画サイズ（ImGuiウィンドウ依存）でビューポート/シザーを設定。RTの左上部分のみに描く。
    // 後続ステージは PreRenderTexture で全画面へ復元される。
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(previewRenderWidth_);
    viewport.Height = static_cast<float>(previewRenderHeight_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    pCommandList->RSSetViewports(1, &viewport);
    D3D12_RECT scissor{};
    scissor.right = static_cast<LONG>(previewRenderWidth_);
    scissor.bottom = static_cast<LONG>(previewRenderHeight_);
    pCommandList->RSSetScissorRects(1, &scissor);

    // プレビューカメラ行列を計算（グリッド用 viewProject と、パーティクル用 per-view を構築）
    Matrix4x4 view{}, viewProj{};
    Vector3 previewEye{};
    float previewProjScaleY = 1.0f;
    ComputePreviewMatrices(view, viewProj, previewEye, previewProjScaleY);

    // 白グリッドを描画（共有 LineRenderer の頂点バッファとは衝突しない専用VB＋Line3d PSO）
    if (previewShowGrid_ && previewGridVertexCount_ > 0)
    {
        *pPreviewLineCBData_ = viewProj;
        PipelineManager::GetInstance()->DrawCommonSetting(PipelineType::Line3d);
        pCommandList->IASetVertexBuffers(0, 1, &previewGridVBView_);
        const ShaderRootSignature *lineRS =
            PipelineManager::GetInstance()->GetReflectedRootSignature(PipelineType::Line3d);
        assert(lineRS && "3Dラインのルートシグネチャが未生成です");
        pCommandList->SetGraphicsRootConstantBufferView(lineRS->GetCbvIndex(0), previewLineCB_->GetGPUVirtualAddress());
        LineRenderer::SetDrawConstants(pCommandList, MakeIdentity4x4(), {1.0f, 1.0f, 1.0f, 1.0f});
        pCommandList->DrawInstanced(previewGridVertexCount_, 1, 0, 0);
    }

    // 選択中エミッタのワイヤーフレームをプレビューVPで描画（共有 LineRenderer は使わず専用VB＋Line3d PSO）。
    // DrawEmitter は共有 LineRenderer に積みシーン側VPで描かれてしまうため、ここで隔離描画する。
    if (previewShowEmitterWire_ && !selectedEmitterName_.empty() && pPreviewWireMapped_)
    {
        auto itWire = emitters_.find(selectedEmitterName_);
        if (itWire != emitters_.end() && itWire->second)
        {
            auto segs = itWire->second->GetWireframeSegments();
            uint32_t v = 0;
            for (const auto &s : segs)
            {
                if (v + 2 > kPreviewWireMaxVerts_)
                {
                    break; // 上限超過分は切り捨て（プレビューのオーバーレイなので許容）
                }
                pPreviewWireMapped_[v++] = {s.a, PackLineColor(s.color)};
                pPreviewWireMapped_[v++] = {s.b, PackLineColor(s.color)};
            }
            previewWireVertexCount_ = v;
            if (v > 0)
            {
                *pPreviewLineCBData_ = viewProj;
                PipelineManager::GetInstance()->DrawCommonSetting(PipelineType::Line3d);
                pCommandList->IASetVertexBuffers(0, 1, &previewWireVBView_);
                const ShaderRootSignature *lineRS =
                    PipelineManager::GetInstance()->GetReflectedRootSignature(PipelineType::Line3d);
                assert(lineRS && "3Dラインのルートシグネチャが未生成です");
                pCommandList->SetGraphicsRootConstantBufferView(lineRS->GetCbvIndex(0), previewLineCB_->GetGPUVirtualAddress());
                LineRenderer::SetDrawConstants(pCommandList, MakeIdentity4x4(), {1.0f, 1.0f, 1.0f, 1.0f});
                pCommandList->DrawInstanced(v, 1, 0, 0);
            }
        }
    }

    // フィールド枠・ギャザー/ボルテックス点など、Update フェーズ(ImGui)で共有 LineRenderer に
    // 積まれたデバッグ線をプレビューVPでも再描画する。この RenderPreview はシーンの
    // LineRenderer::Render(sceneVP) より前に呼ばれるため、線バッファはまだ生きている。
    // リセットしないので後段のシーン描画（シーンVP）にも同じ線がそのまま出る。
    {
        *pPreviewLineCBData_ = viewProj;
        LineRenderer::GetInstance()->RenderWithExternalCamera(pCommandList, previewLineCB_->GetGPUVirtualAddress());
    }

    // 選択中エミッタのパーティクルを隔離描画（Compute 済みバッファをプレビューVPで再描画）
    if (!selectedEmitterName_.empty())
    {
        auto it = emitters_.find(selectedEmitterName_);
        if (it != emitters_.end() && it->second)
        {
            // 専用 per-view CB をプレビューVP＋ビルボードで更新（共有グループの per-view は汚さない）
            pPreviewPerViewData_->viewProjection = viewProj;
            Matrix4x4 billboard = view;
            billboard.m[3][0] = 0.0f;
            billboard.m[3][1] = 0.0f;
            billboard.m[3][2] = 0.0f;
            billboard.m[3][3] = 1.0f;
            pPreviewPerViewData_->billboardMatrix = Inverse(billboard);
            // enableBillboard / enableVelocityStretch / velocityStretchFactor は
            // DrawGraphicsForPreview 内で各グループの設定から流し込む
            // （ここで固定するとビルボードOFFや速度ストレッチをプレビューで確認できない）。
            // billboardMatrix はビルボードON時に使うのでプレビューカメラ基準で設定しておく。

            // パーティクル PSO は SRV ディスクリプタテーブルを使うのでヒープを束ねる
            SrvManager::GetInstance()->SetDescriptorHeap();
            // RT/DSV/Viewport は上で束ね済み（ヒープ設定で解除されないが念のため再設定）
            pCommandList->OMSetRenderTargets(1, &previewRtvHandle_, false, &previewDsvHandle_);
            pCommandList->RSSetViewports(1, &viewport);
            pCommandList->RSSetScissorRects(1, &scissor);
            // 描画カリング(距離/サイズ)をプレビューでも効かせるため、プレビューカメラ位置・射影と
            // 各グループのカリング設定を per-view へ流し込む（DrawGraphicsForPreview 内でグループ毎に反映）。
            it->second->DrawGraphicsForPreview(previewPerViewCB_->GetGPUVirtualAddress(),
                                               pPreviewPerViewData_, previewEye, previewProjScaleY);
        }
    }

    // CPU パーティクル（ParticleEditor の選択中エミッタ）を同じプレビューRTへ描画する。
    // 編集中の CPU エミッタはシーンには描かれないため、このプレビューでのみ確認できる。
    {
        // CPU パーティクルPSOは SRV ディスクリプタテーブルを使うのでヒープを束ね直し、
        // RT/DSV/Viewport も（GPU側で未設定のケースに備え）念のため再設定する。
        SrvManager::GetInstance()->SetDescriptorHeap();
        pCommandList->OMSetRenderTargets(1, &previewRtvHandle_, false, &previewDsvHandle_);
        pCommandList->RSSetViewports(1, &viewport);
        pCommandList->RSSetScissorRects(1, &scissor);

        // プレビューカメラの view / projection から CPU 用 ViewProjection を組む
        // （matView_ でビルボード、matView_×matProjection_ で WVP が決まる）。
        // ここは「行列を描画APIへ渡すだけの入れ物」でカメラ状態は持たない（定数バッファも作らない）。
        // カメラとして扱うものは Camera クラスが持つ。
        ViewProjection cpuVP;
        cpuVP.matView_ = view;
        const float fovY = 45.0f * 3.14159265358979323846f / 180.0f;
        const uint32_t ph = (previewRenderHeight_ > 0) ? previewRenderHeight_ : 1;
        const float aspect = static_cast<float>(previewRenderWidth_) / static_cast<float>(ph);
        cpuVP.matProjection_ = MakePerspectiveFovMatrix(fovY, aspect, 0.1f, 1000.0f);
        ParticleEditor::GetInstance()->DrawSelectedForPreview(cpuVP);
    }

    // ImGui サンプリング用に PIXEL_SHADER_RESOURCE へ戻す
    D3D12_RESOURCE_BARRIER toSRV{};
    toSRV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSRV.Transition.pResource = previewColorResource_.Get();
    toSRV.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toSRV.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toSRV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    pCommandList->ResourceBarrier(1, &toSRV);
    previewColorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void ParticleCSEditor::ShowPreviewWindow(bool *pOpen)
{
#ifdef USE_IMGUI
    if (!previewInitialized_)
    {
        return;
    }
    // 初回サイズ（以降ユーザーが自由にリサイズ）
    ImGui::SetNextWindowSize(ImVec2(1100.0f, 640.0f), ImGuiCond_FirstUseEver);
    // pOpen を渡すとウィンドウのXボタンが表示メニューのフラグと連動する
    if (!ImGui::Begin("CSパーティクル プレビュー", pOpen))
    {
        ImGui::End();
        return;
    }

    // 左: ビューポート / 中: 仕切り（ドラッグで幅を変える）/ 右: エディタ
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    constexpr float kSplitterWidth = 6.0f;
    previewPanelWidth_ = std::clamp(previewPanelWidth_, 280.0f, (std::max)(280.0f, avail.x - 240.0f - kSplitterWidth));
    const float leftWidth = (std::max)(240.0f, avail.x - previewPanelWidth_ - kSplitterWidth);

    // 今見ているもの（GPU タブなら GPU の選択、CPU タブなら CPU の選択）
    ParticleCSEmitter *gpuEmitter = nullptr;
    if (auto it = emitters_.find(selectedEmitterName_); it != emitters_.end())
    {
        gpuEmitter = it->second.get();
    }
    ParticleEmitter *cpuEmitter = ParticleEditor::GetInstance()->GetSelectedEmitter();
    const bool showingCpu = previewShowingCpu_;

    // ============ 左: プレビュービューポート ============
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##previewViewport", ImVec2(leftWidth, 0.0f), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    {
        // 画像サイズ = 子ウィンドウ全体。これを実描画サイズとして記録し、RT の左上部分を UV 表示する。
        const ImVec2 region = ImGui::GetContentRegionAvail();
        uint32_t w = static_cast<uint32_t>((std::max)(16.0f, region.x));
        uint32_t h = static_cast<uint32_t>((std::max)(16.0f, region.y));
        w = (std::min)(w, kPreviewMaxWidth_);
        h = (std::min)(h, kPreviewMaxHeight_);
        previewRenderWidth_ = w;
        previewRenderHeight_ = h;

        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
        ImTextureID texId = static_cast<ImTextureID>(SrvManager::GetInstance()->GetGPUDescriptorHandle(previewColorSrvIndex_).ptr);
        ImVec2 uv1(static_cast<float>(w) / static_cast<float>(kPreviewMaxWidth_), static_cast<float>(h) / static_cast<float>(kPreviewMaxHeight_));
        ImGui::Image(texId, ImVec2(static_cast<float>(w), static_cast<float>(h)), ImVec2(0.0f, 0.0f), uv1);
        const bool imageHovered = ImGui::IsItemHovered();

        auto resetCamera = [this]() {
            previewCamYaw_ = 0.6f;
            previewCamPitch_ = 0.45f;
            previewCamDistance_ = 16.0f;
            previewCamTarget_ = {0.0f, 0.0f, 0.0f};
        };
        auto focusEmitter = [&]() {
            if (showingCpu && cpuEmitter)
                previewCamTarget_ = cpuEmitter->GetPosition();
            else if (!showingCpu && gpuEmitter)
                previewCamTarget_ = gpuEmitter->GetTranslate();
        };
        auto togglePlay = [&]() {
            if (showingCpu && cpuEmitter)
                cpuEmitter->SetIsAuto(!cpuEmitter->GetIsAuto());
            else if (!showingCpu && gpuEmitter)
                gpuEmitter->SetAuto(!gpuEmitter->GetAuto());
        };
        auto emitOnce = [&]() {
            if (showingCpu && cpuEmitter)
                cpuEmitter->UpdateOnce();
            else if (!showingCpu && gpuEmitter)
                gpuEmitter->EmitOnce();
        };

        // 画像上でのオービットカメラ操作（左/右ドラッグ=回転 / 中ドラッグ=注視点パン / ホイール=ズーム）
        if (imageHovered)
        {
            ImGuiIO &io = ImGui::GetIO();
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            {
                previewCamYaw_ += io.MouseDelta.x * 0.01f;
                previewCamPitch_ += io.MouseDelta.y * 0.01f;
                const float pitchLimit = 1.55f; // ≒89°でジンバル反転を防ぐ
                previewCamPitch_ = std::clamp(previewCamPitch_, -pitchLimit, pitchLimit);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            {
                // 現在のカメラパラメータから right / up ベクトルを求める（ComputePreviewMatrices と同一）
                const float cp = std::cos(previewCamPitch_);
                const Vector3 eye = {
                    previewCamTarget_.x + previewCamDistance_ * cp * std::sin(previewCamYaw_),
                    previewCamTarget_.y + previewCamDistance_ * std::sin(previewCamPitch_),
                    previewCamTarget_.z + previewCamDistance_ * cp * std::cos(previewCamYaw_),
                };
                const Vector3 forward = (previewCamTarget_ - eye).Normalize();
                const Vector3 worldUp = {0.0f, 1.0f, 0.0f};
                const Vector3 right = worldUp.Cross(forward).Normalize();
                const Vector3 up = forward.Cross(right);
                // 距離に比例したパン速度（遠いほど大きく動く＝直感的）
                const float panScale = previewCamDistance_ * 0.0015f;
                previewCamTarget_ = previewCamTarget_ - right * (io.MouseDelta.x * panScale) + up * (io.MouseDelta.y * panScale);
            }
            if (io.MouseWheel != 0.0f)
            {
                // 距離に比例して寄る（近いときは細かく、遠いときは大きく）
                previewCamDistance_ *= (io.MouseWheel > 0.0f) ? 0.9f : 1.1f;
                previewCamDistance_ = std::clamp(previewCamDistance_, 0.5f, 300.0f);
            }
            // ショートカット（プレビューにマウスがあるときだけ）
            if (!io.WantTextInput)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
                    togglePlay();
                if (ImGui::IsKeyPressed(ImGuiKey_E, false))
                    emitOnce();
                if (ImGui::IsKeyPressed(ImGuiKey_F, false))
                    focusEmitter();
                if (ImGui::IsKeyPressed(ImGuiKey_R, false))
                    resetCamera();
                if (ImGui::IsKeyPressed(ImGuiKey_G, false))
                    previewShowGrid_ = !previewShowGrid_;
            }
        }

        // ---- 左上: ツールバー（子ウィンドウなので、上にマウスがある間はカメラが動かない）----
        ImGui::SetCursorScreenPos(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.09f, 0.78f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 3.0f));
        ImGui::BeginChild("##previewToolbar", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            const float size = ImGui::GetFrameHeight();
            auto toolButton = [size](const char *icon, bool active, const char *tip) {
                ScopedButtonColors colors(active ? DebugTheme::kButtonPrimary : DebugTheme::kButtonGhost,
                                          active ? DebugTheme::kButtonPrimaryHover : DebugTheme::kButtonGhostHover);
                const bool pressed = ImGui::Button(icon, ImVec2(size, size));
                ImGui::SetItemTooltip("%s", tip);
                return pressed;
            };
            const bool hasEmitter = showingCpu ? (cpuEmitter != nullptr) : (gpuEmitter != nullptr);
            const bool playing = showingCpu ? (cpuEmitter && cpuEmitter->GetIsAuto()) : (gpuEmitter && gpuEmitter->GetAuto());
            ImGui::BeginDisabled(!hasEmitter);
            if (toolButton(playing ? ICON_FA_PAUSE : ICON_FA_PLAY, playing, playing ? "自動発生を止める (Space)" : "自動発生させる (Space)"))
                togglePlay();
            ImGui::SameLine();
            if (toolButton(ICON_FA_BOLT, false, "1回だけ出す (E)"))
                emitOnce();
            ImGui::SameLine();
            if (toolButton(ICON_FA_CROSSHAIRS, false, "エミッターを画面の中心へ (F)"))
                focusEmitter();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (toolButton(ICON_FA_UNDO, false, "カメラを最初の位置に戻す (R)"))
                resetCamera();
            ImGui::SameLine();
            if (toolButton(ICON_FA_BORDER_ALL, previewShowGrid_, "グリッド (G)"))
                previewShowGrid_ = !previewShowGrid_;
            ImGui::SameLine();
            if (toolButton(ICON_FA_VECTOR_SQUARE, previewShowEmitterWire_, "発生形状の枠"))
                previewShowEmitterWire_ = !previewShowEmitterWire_;
            ImGui::SameLine();
            if (toolButton(ICON_FA_COG, false, "カメラと表示の設定"))
                ImGui::OpenPopup("##previewSettings");
            if (ImGui::BeginPopup("##previewSettings"))
            {
                ImGui::SeparatorText("カメラ");
                ImGui::SetNextItemWidth(180.0f);
                ImGui::DragFloat("距離##preview", &previewCamDistance_, 0.1f, 0.5f, 300.0f);
                ImGui::SetNextItemWidth(180.0f);
                ImGui::DragFloat3("注視点##preview", &previewCamTarget_.x, 0.1f);
                ImGui::SeparatorText("表示");
                ImGui::ColorEdit3("背景色##preview", previewBgColor_);
                ImGui::SetNextItemWidth(180.0f);
                ImGui::DragInt("グリッドの分割数##preview", &previewGridDivision_, 1.0f, 2, kPreviewGridMaxDivision_);
                previewGridDivision_ = std::clamp(previewGridDivision_, 2, kPreviewGridMaxDivision_);
                ImGui::SetItemTooltip("グリッド線の本数。半径 ÷ 本数 が線間隔になります（最大 %d）。", kPreviewGridMaxDivision_);
                ImGui::SetNextItemWidth(180.0f);
                ImGui::DragFloat("グリッド半径##preview", &previewGridHalfSize_, 0.5f, 0.1f, 2000.0f);
                ImGui::SetItemTooltip("カメラ注視点を中心とした描画半径。大きくするほど遠くまで伸びます。");
                ImGui::ColorEdit3("グリッド色##preview", &previewGridColor_.x);
                ImGui::SetItemTooltip("線PSOは不透明描画です。暗い背景に対し暗いグレーにすると半透明風に見えます。");
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();

        // ---- 左下: いま見ているもの・粒子の数・操作の説明 ----
        {
            std::string info;
            if (showingCpu)
            {
                info = cpuEmitter ? std::format(ICON_FA_FEATHER " {}  ・  粒子 {} 個", cpuEmitter->GetName(), cpuEmitter->GetActiveParticleCount())
                                  : std::string(ICON_FA_FEATHER " CPU のエミッターが選ばれていません");
            }
            else if (gpuEmitter)
            {
                uint32_t alive = 0;
                for (const ParticleCSGroup *group : gpuEmitter->GetParticleGroups())
                {
                    alive += group ? group->GetAliveDrawCount() : 0u;
                }
                info = std::format(ICON_FA_BOLT " {}  ・  粒子 {} 個  ・  グループ {} 個", selectedEmitterName_, alive, gpuEmitter->GetParticleGroups().size());
            }
            else
            {
                info = ICON_FA_BOLT " エミッターが選ばれていません（グリッドのみ）";
            }
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const float lineHeight = ImGui::GetTextLineHeight();
            const ImVec2 infoPos = ImVec2(imageMin.x + 10.0f, imageMin.y + static_cast<float>(h) - lineHeight * 2.0f - 12.0f);
            drawList->AddText(ImVec2(infoPos.x + 1.0f, infoPos.y + 1.0f), IM_COL32(0, 0, 0, 180), info.c_str());
            drawList->AddText(infoPos, IM_COL32(235, 235, 240, 255), info.c_str());
            const char *help = "左/右ドラッグ: 回る  中ドラッグ: 動かす  ホイール: 寄る  Space: 再生  E: 1回  F: 中心へ";
            drawList->AddText(ImVec2(infoPos.x, infoPos.y + lineHeight + 4.0f), IM_COL32(170, 170, 180, 200), help);
        }
    }
    ImGui::EndChild();

    // ============ 仕切り（ドラッグで右パネルの幅を変える）============
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::InvisibleButton("##previewSplitter", ImVec2(kSplitterWidth, (std::max)(avail.y, 1.0f)));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive())
    {
        previewPanelWidth_ -= ImGui::GetIO().MouseDelta.x;
    }
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const float x = (min.x + max.x) * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x, min.y + 4.0f), ImVec2(x, max.y - 4.0f),
                                            ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive : ImGuiCol_Separator), 2.0f);
    }
    ImGui::SameLine(0.0f, 0.0f);

    // ============ 右: エディタパネル（GPU / CPU をタブで切り替え。中は「エミッター・作成・削除」のタブ）============
    ImGui::BeginChild("##previewEditor", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (ImGui::BeginTabBar("##previewParticleKind"))
    {
        if (ImGui::BeginTabItem(ICON_FA_BOLT " パーティクル (GPU)###gpuParticleTab"))
        {
            previewShowingCpu_ = false;
            if (ImGui::BeginTabBar("##previewGpuTabs"))
            {
                if (ImGui::BeginTabItem(ICON_FA_LIST " エミッター"))
                {
                    DebugAll(false);
                    ImGui::EndTabItem();
                }
                ShowImGuiEditor(false); // 「作成」「削除」のタブ
                ImGui::EndTabBar();
            }
            ImGui::EndTabItem();
        }
        // CPU パーティクルも同じプレビュー窓で確認・編集できるようにする。
        // 選択中の CPU エミッタは RenderPreview でプレビューRTへ描画される。
        if (ImGui::BeginTabItem(ICON_FA_FEATHER " パーティクル (CPU)###cpuParticleTab"))
        {
            previewShowingCpu_ = true;
            if (ImGui::BeginTabBar("##previewCpuTabs"))
            {
                if (ImGui::BeginTabItem(ICON_FA_LIST " エミッター"))
                {
                    ParticleEditor::GetInstance()->DebugAll(false);
                    ImGui::EndTabItem();
                }
                ParticleEditor::GetInstance()->ShowImGuiEditor(false); // 「作成」タブ
                ImGui::EndTabBar();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::End();
#endif // USE_IMGUI
}

// カラーテーマを設定する
void ParticleCSEditor::SetupColors()
{
#ifdef USE_IMGUI
    // 各CollapsingHeaderに使用する色を定義
    headerColors_[0] = ImVec4(0.30f, 0.38f, 0.50f, 0.55f); // 青系
    headerColors_[1] = ImVec4(0.50f, 0.38f, 0.24f, 0.55f); // オレンジ系
    headerColors_[2] = ImVec4(0.30f, 0.44f, 0.34f, 0.55f); // 緑系
    headerColors_[3] = ImVec4(0.40f, 0.33f, 0.48f, 0.55f); // 紫系
    headerColors_[4] = ImVec4(0.50f, 0.46f, 0.28f, 0.55f); // 黄色系
    headerColors_[5] = ImVec4(0.32f, 0.33f, 0.36f, 0.55f); // グレー系
#endif                                                     // USE_IMGUI
}

void ParticleCSEditor::AddParticleEmitter(const std::string &name)
{
    // Create standard sphere emitter
    auto emitter = std::make_unique<ParticleCSEmitter>();
    emitter->SetPreviewOnly(true); // 編集用インスタンス。プレビュー窓にしか描かないのでシーンを照らさない
    emitter->Initialize(name);
    emitters_[name] = std::move(emitter);
    DrawGroupManager::GetInstance()->RegisterGroup(emitters_[name]->GetDrawGroup()); // 所属グループを登録
    ImGuiNotification::Post("GPUパーティクルエミッターを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void ParticleCSEditor::AddParticleEmitter(const std::string &name, const std::string &modelPath)
{
    // Create model-based emitter
    auto emitter = std::make_unique<ParticleCSEmitter>();
    emitter->SetPreviewOnly(true); // 編集用インスタンス。プレビュー窓にしか描かないのでシーンを照らさない
    emitter->Initialize(name, modelPath);
    emitters_[name] = std::move(emitter);
    DrawGroupManager::GetInstance()->RegisterGroup(emitters_[name]->GetDrawGroup()); // 所属グループを登録
    ImGuiNotification::Post("GPUパーティクルエミッターを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void ParticleCSEditor::AddParticleEmitter(const std::string &name, PrimitiveType primitiveType)
{
    // Create primitive model-based emitter
    auto emitter = std::make_unique<ParticleCSEmitter>();
    emitter->SetPreviewOnly(true); // 編集用インスタンス。プレビュー窓にしか描かないのでシーンを照らさない
    emitter->Initialize(name, primitiveType);
    emitters_[name] = std::move(emitter);
    DrawGroupManager::GetInstance()->RegisterGroup(emitters_[name]->GetDrawGroup()); // 所属グループを登録
    ImGuiNotification::Post("GPUパーティクルエミッターを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void ParticleCSEditor::DrawAllCompute(const ViewProjection &vp_)
{
    for (auto &[name, emitter] : emitters_)
    {
        emitter->Update();
        emitter->DrawCompute(vp_);
    }
}

void ParticleCSEditor::DrawAllGraphics(const ViewProjection &vp_)
{
    for (auto &[name, emitter] : emitters_)
    {
        emitter->DrawGraphics(vp_);
    }
}

std::vector<std::string> ParticleCSEditor::GetEmitterNames() const
{
    std::vector<std::string> names;
    names.reserve(emitters_.size());
    for (const auto &[name, emitter] : emitters_)
    {
        names.push_back(name);
    }
    return names;
}

ParticleCSEmitter *ParticleCSEditor::GetEmitterByName(const std::string &name)
{
    auto it = emitters_.find(name);
    return (it != emitters_.end()) ? it->second.get() : nullptr;
}

void ParticleCSEditor::DrawAll(const ViewProjection &vp_)
{
    // 後方互換: バッチなしで呼ばれた場合は内部で 2 フェーズを完結させる
    for (auto &[name, emitter] : emitters_)
    {
        emitter->Update();
        emitter->Draw(vp_);
    }
}

void ParticleCSEditor::Load()
{
}

// 指定名のエミッターをmapから削除し、選択状態をリセットする
void ParticleCSEditor::RemoveParticleEmitter(const std::string &name)
{
    auto it = emitters_.find(name);
    if (it == emitters_.end())
    {
        return;
    }
    ImGuiNotification::Post("GPUパーティクルエミッターを削除しました: " + name, {0.9f, 0.7f, 0.2f, 1.0f});
    // 描画中のフレームがこのエミッターのバッファを使っているかもしれないので、終わるのを待ってから消す
    DirectXCommon::GetInstance()->WaitForGPU();
    emitters_.erase(it);

    // 削除したエミッターが選択中だった場合はリセット
    if (selectedEmitterName_ == name)
    {
        selectedEmitterName_.clear();
        selectedEmitterIndex_ = 0;
    }
}

void ParticleCSEditor::AddParticleGroup(const std::string &name, const std::string &fileName, uint32_t maxParticleCount, const std::string &texturePath)
{
    ImGuiNotification::Post("パーティクルグループを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
    // 新しい ParticleGroup を作成
    auto group = std::make_unique<ParticleCSGroup>();
    // パーティクルグループを作成
    group->CreateParticleGroup(name, fileName, maxParticleCount, texturePath);

    // マップに追加
    pParticleGroupManager_->AddParticleCSGroup(std::move(group));
}

void ParticleCSEditor::AddPrimitiveParticleGroup(const std::string &name, PrimitiveType type, uint32_t maxParticleCount, const std::string &texturePath)
{
    ImGuiNotification::Post("プリミティブパーティクルグループを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
    // 新しい ParticleGroup を作成
    auto group = std::make_unique<ParticleCSGroup>();
    // プリミティブパーティクルグループを作成
    group->CreatePrimitiveParticleGroup(name, type, maxParticleCount, texturePath);
    // マップに追加
    pParticleGroupManager_->AddParticleCSGroup(std::move(group));
}

void ParticleCSEditor::ShowGPUParticleStatistics()
{
#ifdef USE_IMGUI
    if (ImGui::CollapsingHeader("GPUパーティクル統計"))
    {
        // 生存中の全エミッター（エディタ登録・Spawner 生成・ゲームクラス所有）が対象。
        // エディタのプレビュー専用エミッターはゲーム画面に出ていないので別枠で表示する。
        const auto allStats = ParticleCSEmitter::GetAllEmitterStatistics(true);

        // エミッター名ごとに合算（同じテンプレートを複数出したときは1行にまとめる）
        std::map<std::string, size_t> sceneStats;
        size_t sceneTotal = 0;
        size_t previewTotal = 0;
        size_t sceneEmitterCount = 0;
        for (const auto &stat : allStats)
        {
            if (stat.previewOnly)
            {
                previewTotal += stat.aliveCount;
                continue;
            }
            sceneStats[stat.emitterName] += stat.aliveCount;
            sceneTotal += stat.aliveCount;
            ++sceneEmitterCount;
        }

        // ヘッダー情報（シーンに出ている数）
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "シーン合計: %zu個", sceneTotal);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "(エミッター %zu 個)", sceneEmitterCount);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Spawn で実行時に出したものも含めた、今シーンに出ている総数です\n"
                              "（GPUからの読み戻しなので1〜2フレーム遅延します）");
        if (previewTotal > 0)
        {
            ImGui::TextColored(DebugTheme::kTextDim, "プレビュー窓: %zu個（画面には出ていない）", previewTotal);
        }

        if (!sceneStats.empty())
        {
            ImGui::Separator();

            // エミッターごとに表示
            for (const auto &[emitterName, count] : sceneStats)
            {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", emitterName.c_str());
                ImGui::SameLine();
                ImGui::Text(": %zu", count);
            }
        }
        else
        {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "エミッターなし");
        }

        // 再利用プール（使い終わったグループを GPU バッファごと取っておく所）
        size_t pooledGroups = 0;
        size_t pooledParticles = 0;
        pParticleGroupManager_->GetPoolUsage(pooledGroups, pooledParticles);
        ImGui::Spacing();
        ImGui::Text("再利用プール: %zu グループ（粒 %zu 個ぶん）", pooledGroups, pooledParticles);
        ImGui::SetItemTooltip("消えたエミッターのグループを、次に同じ物を出すときのために取っておいた分。シーンをまたいでも残る");
        ImGui::SameLine();
        ImGui::BeginDisabled(pooledGroups == 0);
        if (NeutralButton("空にする##particlePool"))
        {
            DirectXCommon::GetInstance()->WaitForGPU(); // 直前まで使っていたかもしれないので描画が終わるのを待つ
            pParticleGroupManager_->ClearPool();
            ImGuiNotification::Post(std::format("再利用プールを空にしました（{} グループ）", pooledGroups), {0.45f, 0.68f, 0.52f, 1.0f});
        }
        ImGui::EndDisabled();

        // 長く使われていないプール分を自動で捨てる（同じ演出を出し続けている間は残る）
        ImGui::Checkbox("使われていない分を自動で捨てる##particlePoolPrune", &pParticleGroupManager_->PoolAutoPrune());
        ImGui::SetItemTooltip("返してから指定の秒数、一度も使い回されなかったグループの GPU バッファを返す（保存しない）");
        if (pParticleGroupManager_->PoolAutoPrune())
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            // 1秒未満にすると、返した直後（GPU がまだ触っているかもしれない間）に捨てることになるので下限を置く
            ImGui::DragFloat("秒##particlePoolIdle", &pParticleGroupManager_->PoolIdleSeconds(), 1.0f, 1.0f, 600.0f, "%.0f",
                             ImGuiSliderFlags_AlwaysClamp);
        }
    }
#endif // USE_IMGUI
}

void ParticleCSEditor::DebugAll(bool ownTabBar)
{
#ifdef USE_IMGUI
    // ownTabBar=false のときは呼び出し元のタブの中身として描く（タブバー・タブ項目を作らない）
    if (!ownTabBar || ImGui::BeginTabBar("GPUパーティクル"))
    {
        if (!ownTabBar || ImGui::BeginTabItem("GPUエミッター設定"))
        {
            if (emitters_.empty())
            {
                ImGui::Text("エミッターがありません");
            }
            else
            {
                // エミッター名は名前順に並べる（unordered_map のままだと並びが毎回変わって探しにくい）
                std::vector<std::string> emitterNames;
                for (const auto &[name, emitter] : emitters_)
                {
                    emitterNames.push_back(name);
                }
                std::sort(emitterNames.begin(), emitterNames.end());

                // 選択が消えていたら先頭へ
                if (selectedEmitterName_.empty() || emitters_.find(selectedEmitterName_) == emitters_.end())
                {
                    selectedEmitterName_ = emitterNames.front();
                }

                // ---- 検索 ----
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##emitterSearch", ICON_FA_SEARCH " エミッターを名前で絞り込み", &emitterSearch_);
                if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    emitterSearch_.clear();
                }
                auto lower = [](std::string text) {
                    for (char &c : text)
                    {
                        if (c >= 'A' && c <= 'Z')
                            c = static_cast<char>(c - 'A' + 'a');
                    }
                    return text;
                };
                const std::string query = lower(emitterSearch_);

                // ---- 一覧（● = 自動発生中 / 目の斜線 = 非表示）。高さは下端をドラッグで変えられる ----
                ImGui::BeginChild("##emitterList", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 6.5f),
                                  ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
                int shown = 0;
                for (size_t i = 0; i < emitterNames.size(); ++i)
                {
                    const std::string &name = emitterNames[i];
                    if (!query.empty() && lower(name).find(query) == std::string::npos)
                    {
                        continue;
                    }
                    ++shown;
                    const ParticleCSEmitter *emitter = emitters_[name].get();
                    const bool isAuto = emitter && emitter->GetAuto();
                    const bool visible = !emitter || emitter->GetVisible();
                    ImGui::PushID(name.c_str());
                    ImGui::TextColored(isAuto ? DebugTheme::kAccentGreen : DebugTheme::kTextDim, isAuto ? ICON_FA_CIRCLE : ICON_FA_CIRCLE_NOTCH);
                    ImGui::SetItemTooltip(isAuto ? "自動発生中" : "自動発生していない");
                    ImGui::SameLine();
                    if (!visible)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    }
                    if (ImGui::Selectable(name.c_str(), selectedEmitterName_ == name))
                    {
                        selectedEmitterName_ = name;
                        selectedEmitterIndex_ = static_cast<int>(i);
                    }
                    if (!visible)
                    {
                        ImGui::PopStyleColor();
                        ImGui::SameLine();
                        ImGui::TextDisabled(ICON_FA_EYE_SLASH);
                    }
                    ImGui::PopID();
                }
                if (shown == 0)
                {
                    ImGui::TextDisabled("一致するエミッターがありません");
                }
                ImGui::EndChild();

                // ---- 選択中のエミッターの操作バー ----
                auto it = emitters_.find(selectedEmitterName_);
                if (it != emitters_.end() && it->second)
                {
                    ParticleCSEmitter *emitter = it->second.get();
                    bool isAuto = emitter->GetAuto();
                    if (ThemedToggle("##emitterAuto", &isAuto, DebugTheme::kAccentGreen))
                    {
                        emitter->SetAuto(isAuto);
                    }
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(isAuto ? "自動発生 ON" : "自動発生 OFF");
                    ImGui::SameLine();
                    if (PrimaryButton(ICON_FA_BOLT " 1回出す"))
                    {
                        emitter->EmitOnce();
                    }
                    ImGui::SetItemTooltip("自動発生を止めたまま、1回ぶんだけ発生させて形を確かめる");
                    ImGui::SameLine();
                    bool visible = emitter->GetVisible();
                    if (NeutralButton(visible ? ICON_FA_EYE " 表示中" : ICON_FA_EYE_SLASH " 非表示"))
                    {
                        emitter->SetVisible(!visible);
                    }
                    ImGui::Spacing();

                    // 選択されたエミッターの詳細
                    emitter->DrawImGui();
                }
            }
            if (ownTabBar)
                ImGui::EndTabItem();
        }
        if (ownTabBar)
            ImGui::EndTabBar();
    }
#endif // USE_IMGUI
}

void ParticleCSEditor::EditorWindow()
{
#ifdef USE_IMGUI
    ImGui::Begin("CSパーティクルエディター");
    ShowImGuiEditor();
    ImGui::End();
#endif // USE_IMGUI
}

// カラー付きCollapsingHeaderを表示するヘルパー関数
bool ParticleCSEditor::ColoredCollapsingHeader(const char *label, int colorIndex)
{
#ifdef USE_IMGUI
    // 現在のImGuiカラーを保存
    ImVec4 originalColor = ImGui::GetStyleColorVec4(ImGuiCol_Header);

    // 色を設定
    ImGui::PushStyleColor(ImGuiCol_Header, headerColors_[colorIndex % 6]);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(
                                                      headerColors_[colorIndex % 6].x + 0.1f,
                                                      headerColors_[colorIndex % 6].y + 0.1f,
                                                      headerColors_[colorIndex % 6].z + 0.1f,
                                                      0.9f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(
                                                     headerColors_[colorIndex % 6].x + 0.2f,
                                                     headerColors_[colorIndex % 6].y + 0.2f,
                                                     headerColors_[colorIndex % 6].z + 0.2f,
                                                     1.0f));

    // CollapsingHeaderを表示
    bool opened = ImGui::CollapsingHeader(label);

    // 設定した色をリセット
    ImGui::PopStyleColor(3);

    return opened;
#endif // USE_IMGUI
}

void ParticleCSEditor::ShowImGuiEditor(bool ownTabBar)
{
#ifdef USE_IMGUI
    // プレビュー窓の表示は ImGuiManager の「表示 > ウィンドウ > パーティクルプレビュー」で管理する
    // （ここでは描画しない）

    // ownTabBar=false のときはタブ項目だけを出す（呼び出し元のタブバーに並べる）
    if (!ownTabBar || ImGui::BeginTabBar("GPUパーティクル"))
    {
        if (ImGui::BeginTabItem(ICON_FA_PLUS " 作成"))
        {
            DrawQuickCreate();

            // 形・グループを1つずつ指定して作る従来の画面
            ImGui::Spacing();
            if (ImGui::CollapsingHeader(ICON_FA_SLIDERS_H " 詳しく作る（モデル・プリミティブ・グループを指定）"))
            {
                // エミッター追加のCollapsingHeader
                if (ColoredCollapsingHeader("エミッター追加", 0))
                {
                    // 名前の入力
                    char nameBuffer[256];
                    strcpy_s(nameBuffer, sizeof(nameBuffer), localEmitterName_.c_str());
                    ImGui::Text("エミッターの名前");
                    if (ImGui::InputText(" ", nameBuffer, sizeof(nameBuffer)))
                    {
                        localEmitterName_ = std::string(nameBuffer);
                    }

                    // エミッタータイプ選択
                    ImGui::Spacing();
                    ImGui::Text("エミッタータイプ選択");

                    static int selectedEmitterType = 0; // 0: Model, 1: Primitive
                    ImGui::RadioButton("モデルエミッター", &selectedEmitterType, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("プリミティブエミッター", &selectedEmitterType, 1);
                    ImGui::Separator();

                    // モデルエミッター選択時
                    if (selectedEmitterType == 0)
                    {
                        // エミッター用モデル選択セクション
                        if (ColoredCollapsingHeader("モデル選択##EmitterModel", 2))
                        {
                            // モデルファイル選択 (既存のコードと同じ)
                            // models はエンジン(debug)とアプリの 2 ルートに分割。ラジオで切り替える。
                            static const std::vector<std::string> kRootsObj = AssetPath::ModelScanRoots(); // [0]=エンジン, [1]=アプリ
                            static int rootSelObj = 1; // 既定: App
                            static std::filesystem::path currentDirObj = kRootsObj[rootSelObj];
                            static std::string selectedFolderObj = "";
                            static std::string selectedFileObj = "";

                            for (int i = 0; i < 2; ++i)
                            {
                                if (i > 0)
                                    ImGui::SameLine();
                                if (ImGui::RadioButton(i == 0 ? "Engine(debug)##eobjr" : "App##eobjr", rootSelObj == i))
                                {
                                    rootSelObj = i;
                                    currentDirObj = kRootsObj[rootSelObj];
                                    selectedFolderObj = selectedFileObj = "";
                                }
                            }
                            const std::filesystem::path baseDirObj = kRootsObj[rootSelObj];

                            // 「戻る」ボタン（上の階層に戻る）
                            if (currentDirObj != baseDirObj)
                            {
                                if (ImGui::Button("< 戻る(Emitter Model)"))
                                {
                                    currentDirObj = currentDirObj.parent_path();
                                    selectedFolderObj = "";
                                    selectedFileObj = "";
                                }
                            }

                            // フォルダ一覧
                            std::vector<std::string> foldersObj;
                            std::vector<std::string> objFiles;

                            for (const auto &entry : std::filesystem::directory_iterator(currentDirObj))
                            {
                                if (entry.is_directory())
                                {
                                    foldersObj.push_back(entry.path().filename().string());
                                }
                                else if (entry.path().extension() == ".obj")
                                {
                                    objFiles.push_back(entry.path().filename().string());
                                }
                            }

                            // フォルダ選択 (クリックで移動)
                            if (!foldersObj.empty())
                            {
                                ImGui::Text("フォルダ");
                                ImGui::Separator();
                                for (const auto &folder : foldersObj)
                                {
                                    std::string folderNameTex = folder + " (Emitter Model)";
                                    if (ImGui::Selectable(folderNameTex.c_str(), selectedFolderObj == folder))
                                    {
                                        selectedFolderObj = folderNameTex;
                                        currentDirObj = currentDirObj / folder;
                                        selectedFileObj = "";
                                    }
                                    ImGui::Separator();
                                }
                            }

                            // `.obj` ファイル選択
                            if (!objFiles.empty())
                            {
                                ImGui::Text("モデルファイル:");
                                if (ImGui::BeginCombo("ファイル選択##EmitterModel", selectedFileObj.empty() ? "なし" : selectedFileObj.c_str()))
                                {
                                    for (const auto &file : objFiles)
                                    {
                                        bool isSelected = (file == selectedFileObj);
                                        if (ImGui::Selectable(file.c_str(), isSelected))
                                        {
                                            selectedFileObj = file;

                                            // `baseDirObj` からの相対パスを取得
                                            std::filesystem::path relativePath = (currentDirObj / file).lexically_relative(baseDirObj);

                                            // Windowsのバックスラッシュをスラッシュに変換
                                            std::string pathStr = relativePath.string();
                                            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

                                            // `localEmitterModelPath_` に保存
                                            localEmitterModelPath_ = pathStr;
                                        }
                                        if (isSelected)
                                        {
                                            ImGui::SetItemDefaultFocus();
                                        }
                                    }
                                    ImGui::EndCombo();
                                }
                            }
                        }

                        if (!localEmitterName_.empty() && !localEmitterModelPath_.empty())
                        {
                            if (ImGui::Button("モデルエミッター生成"))
                            {
                                AddParticleEmitter(localEmitterName_, localEmitterModelPath_);
                                localEmitterName_.clear();
                                localEmitterModelPath_.clear();
                            }
                        }
                    }
                    // プリミティブエミッター選択時
                    else if (selectedEmitterType == 1)
                    {
                        // エミッター用プリミティブタイプ選択セクション
                        if (ColoredCollapsingHeader("プリミティブタイプ選択##EmitterPrimitive", 4))
                        {
                            const char *primitiveType[] = {"未選択", "プレーン", "球", "キューブ", "シリンダー", "リング", "三角形", "円錐", "四角錐", "円柱", "岩"};
                            int currentPrimitiveType = static_cast<int>(localEmitterType_);
                            if (ImGui::Combo("タイプ選択##EmitterPrimitive", &currentPrimitiveType, primitiveType, IM_ARRAYSIZE(primitiveType)))
                            {
                                localEmitterType_ = static_cast<PrimitiveType>(currentPrimitiveType);
                            }
                        }

                        if (!localEmitterName_.empty() && localEmitterType_ != PrimitiveType::None)
                        {
                            if (ImGui::Button("プリミティブエミッター生成"))
                            {
                                AddParticleEmitter(localEmitterName_, localEmitterType_);
                                localEmitterName_.clear();
                                localEmitterType_ = PrimitiveType::None;
                            }
                        }
                    }
                }

                // パーティクルグループ作成のCollapsingHeader
                if (ColoredCollapsingHeader("パーティクルグループ作成", 1))
                {
                    // 名前の入力
                    char nameBuffer[256];
                    strcpy_s(nameBuffer, sizeof(nameBuffer), localName_.c_str());
                    ImGui::Text("パーティクルグループの名前");
                    if (ImGui::InputText("  ", nameBuffer, sizeof(nameBuffer)))
                    {
                        localName_ = std::string(nameBuffer);
                    }

                    // パーティクルタイプ選択（ラジオボタン）
                    ImGui::Spacing();
                    ImGui::Text("パーティクルタイプ選択");

                    static int selectedType = 0; // 0: モデル, 1: プリミティブ
                    ImGui::RadioButton("モデルパーティクル", &selectedType, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("プリミティブモデル", &selectedType, 1);
                    ImGui::Separator();

                    // モデルパーティクル選択時
                    if (selectedType == 0)
                    {
                        // グループ用モデル選択セクション (青色)
                        if (ColoredCollapsingHeader("モデル選択##GroupModel", 2))
                        {
                            // モデルファイル選択
                            // models はエンジン(debug)とアプリの 2 ルートに分割。ラジオで切り替える。
                            static const std::vector<std::string> kRootsObj = AssetPath::ModelScanRoots(); // [0]=エンジン, [1]=アプリ
                            static int rootSelObj = 1; // 既定: App
                            static std::filesystem::path currentDirObj = kRootsObj[rootSelObj];
                            static std::string selectedFolderObj = "";
                            static std::string selectedFileObj = "";

                            for (int i = 0; i < 2; ++i)
                            {
                                if (i > 0)
                                    ImGui::SameLine();
                                if (ImGui::RadioButton(i == 0 ? "Engine(debug)##gobjr" : "App##gobjr", rootSelObj == i))
                                {
                                    rootSelObj = i;
                                    currentDirObj = kRootsObj[rootSelObj];
                                    selectedFolderObj = selectedFileObj = "";
                                }
                            }
                            const std::filesystem::path baseDirObj = kRootsObj[rootSelObj];

                            // 「戻る」ボタン（上の階層に戻る）
                            if (currentDirObj != baseDirObj)
                            {
                                if (ImGui::Button("< 戻る(Model)"))
                                {
                                    currentDirObj = currentDirObj.parent_path();
                                    selectedFolderObj = "";
                                    selectedFileObj = "";
                                }
                            }

                            // フォルダ一覧
                            std::vector<std::string> foldersObj;
                            std::vector<std::string> objFiles;

                            for (const auto &entry : std::filesystem::directory_iterator(currentDirObj))
                            {
                                if (entry.is_directory())
                                {
                                    foldersObj.push_back(entry.path().filename().string());
                                }
                                else if (entry.path().extension() == ".obj")
                                {
                                    objFiles.push_back(entry.path().filename().string());
                                }
                            }

                            // フォルダ選択 (クリックで移動)
                            if (!foldersObj.empty())
                            {
                                ImGui::Text("フォルダ");
                                ImGui::Separator();
                                for (const auto &folder : foldersObj)
                                {
                                    std::string folderNameTex = folder + " (Model)"; // フォルダ名に "(Model)" を追加
                                    if (ImGui::Selectable(folderNameTex.c_str(), selectedFolderObj == folder))
                                    {
                                        selectedFolderObj = folderNameTex;
                                        currentDirObj = currentDirObj / folder; // フォルダ移動
                                        selectedFileObj = "";                   // 新しいフォルダを開いたらファイル選択をリセット
                                    }
                                    ImGui::Separator();
                                }
                            }

                            // `.obj` ファイル選択
                            if (!objFiles.empty())
                            {
                                ImGui::Text("モデルファイル:");
                                if (ImGui::BeginCombo("ファイル選択##GroupModel", selectedFileObj.empty() ? "なし" : selectedFileObj.c_str()))
                                {
                                    for (const auto &file : objFiles)
                                    {
                                        bool isSelected = (file == selectedFileObj);
                                        if (ImGui::Selectable(file.c_str(), isSelected))
                                        {
                                            selectedFileObj = file;

                                            // `baseDirObj` からの相対パスを取得
                                            std::filesystem::path relativePath = (currentDirObj / file).lexically_relative(baseDirObj);

                                            // Windowsのバックスラッシュをスラッシュに変換
                                            std::string pathStr = relativePath.string();
                                            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

                                            // `fileNameObj_` に保存
                                            localFileObj_ = pathStr;
                                        }
                                        if (isSelected)
                                        {
                                            ImGui::SetItemDefaultFocus();
                                        }
                                    }
                                    ImGui::EndCombo();
                                }
                            }
                        }

                        // グループ用テクスチャ選択セクション (緑色)
                        if (ColoredCollapsingHeader("テクスチャ選択##GroupModel", 3))
                        {
    #ifdef USE_IMGUI
                            ShowTextureFile(localTexturePath_);
    #endif // USE_IMGUI
                        }

                        if (localMaxParticleCount_ == 0)
                        {
                            localMaxParticleCount_ = 100; // デフォルト
                        }
                        ImGui::Spacing();
                        ImGui::Text("最大パーティクル数（注意: 多すぎると重くなる可能性あり）");
                        ImGui::DragInt("##MaxParticleCountModel", &localMaxParticleCount_, 100);

                        // ボタン
                        if (!localName_.empty() && !localFileObj_.empty())
                        {
                            if (ImGui::Button("モデルパーティクルグループ生成"))
                            {
                                if (localMaxParticleCount_ <= 0)
                                    localMaxParticleCount_ = 100; // 保険
                                AddParticleGroup(localName_, localFileObj_, localMaxParticleCount_, localTexturePath_);
                                localName_.clear();
                                localFileObj_.clear();
                                localTexturePath_.clear();
                                localMaxParticleCount_ = 0;
                            }
                        }
                    }
                    // プリミティブモデル選択時
                    else if (selectedType == 1)
                    {
                        // グループ用プリミティブタイプ選択セクション (紫色)
                        if (ColoredCollapsingHeader("プリミティブタイプ選択##GroupPrimitive", 4))
                        {
                            const char *primitiveType[] = {"未選択", "プレーン", "球", "キューブ", "シリンダー", "リング", "三角形", "円錐", "四角錐", "円柱", "岩"};
                            int currentPrimitiveType = static_cast<int>(localType_);
                            // 初期値が未選択（None = -1）の場合に対応するため +1 して選択肢に表示
                            if (ImGui::Combo("タイプ選択##GroupPrimitive", &currentPrimitiveType, primitiveType, IM_ARRAYSIZE(primitiveType)))
                            {
                                localType_ = static_cast<PrimitiveType>(currentPrimitiveType);
                            }
                        }

                        // グループ用テクスチャ選択セクション (オレンジ色)
                        if (ColoredCollapsingHeader("テクスチャ選択##GroupPrimitive", 5))
                        {
    #ifdef USE_IMGUI
                            ShowTextureFile(localTexturePath_);
    #endif // USE_IMGUI
                        }

                        if (localMaxParticleCount_ == 0)
                        {
                            localMaxParticleCount_ = 10000; // デフォルト
                        }
                        ImGui::Spacing();
                        ImGui::Text("最大パーティクル数");
                        ImGui::DragInt("##MaxParticleCountPrimitive", &localMaxParticleCount_, 100);

                        // ボタン
                        if (!localName_.empty())
                        {
                            bool isTypeInvalid = (localType_ == PrimitiveType::None);
                            if (isTypeInvalid)
                            {
                                ImGui::BeginDisabled();
                            }

                            if (ImGui::Button("プリミティブパーティクルグループ生成"))
                            {
                                if (localMaxParticleCount_ <= 0)
                                    localMaxParticleCount_ = 10000; // 保険
                                AddPrimitiveParticleGroup(localName_, localType_, localMaxParticleCount_, localTexturePath_);
                                localName_.clear();
                                localTexturePath_.clear();
                                localType_ = PrimitiveType::None;
                                localMaxParticleCount_ = 0;
                            }

                            if (isTypeInvalid)
                            {
                                ImGui::EndDisabled();
                            }
                        }
                    }
                }

            } // 詳しく作る

            ImGui::EndTabItem();
        }

        // エミッター・グループの一覧表示と削除管理タブ
        if (ImGui::BeginTabItem(ICON_FA_TRASH " 削除"))
        {
            ShowDeleteSection();
            ImGui::EndTabItem();
        }
        if (ownTabBar)
            ImGui::EndTabBar();
    }

#endif // USE_IMGUI
}

} // namespace Hagine
