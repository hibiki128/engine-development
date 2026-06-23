#define NOMINMAX
#include "ParticleCSEditor.h"
#include "../Utility/Debug/ImGui/ImGuizmoManager.h"
#include <Camera/ViewProjection/ViewProjection.h>
#include <Particle/ParticleEditor.h>
#include <Utility/Debug/ImGui/ImGuiNotification.h>
#include <ShowFolder/ShowFolder.h>
#include <algorithm>
#include <myMath.h>
#include <vector>

namespace Hagine {
void ParticleCSEditor::Finalize() {
    emitters_.clear();
}

void ParticleCSEditor::Initialize() {
    particleGroupManager_ = ParticleCSGroupManager::GetInstance();
    // カラーテーマの初期設定
    SetupColors();
    // プレビュー窓用オフスクリーンを生成（シングルトンなので初回のみ）
    InitializePreview();
}

// =============================================
// プレビュー窓 (Phase 8a: 専用RT生成 + 暗くクリア + ImGui表示)
// =============================================
void ParticleCSEditor::InitializePreview() {
    if (previewInitialized_) {
        return;
    }
    DirectXCommon *dxCommon = ParticleCommon::GetInstance()->GetDxCommon();
    SrvManager *srvManager = SrvManager::GetInstance();

    // 暗い背景色（Effekseer 風）でクリアされる色RTを生成
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    clearValue.Color[0] = 0.02f;
    clearValue.Color[1] = 0.02f;
    clearValue.Color[2] = 0.03f;
    clearValue.Color[3] = 1.0f;

    previewColorResource_ = dxCommon->CreateRenderTextureResource(
        kPreviewMaxWidth_, kPreviewMaxHeight_, clearValue.Format, clearValue);
    previewColorState_ = D3D12_RESOURCE_STATE_GENERIC_READ; // CreateRenderTextureResource の初期状態

    // ImGui 表示用 SRV
    previewColorSrvIndex_ = srvManager->Allocate() + 1;
    srvManager->CreateSRVforRenderTexture(previewColorSrvIndex_, previewColorResource_.Get());

    // RTV（拡張した RTV ヒープの slot 6 を使用）
    D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = dxCommon->GetRTVDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
    UINT rtvSize = dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    previewRtvHandle_.ptr = rtvStart.ptr + (6 * rtvSize);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    dxCommon->GetDevice()->CreateRenderTargetView(previewColorResource_.Get(), &rtvDesc, previewRtvHandle_);

    // 専用深度バッファ＋DSV（拡張した DSV ヒープの slot1）。kLine3d/パーティクル PSO は D24_UNORM_S8_UINT を要求する。
    previewDepthResource_ = dxCommon->CreateAdditionalDepthResource(kPreviewMaxWidth_, kPreviewMaxHeight_);
    previewDsvHandle_ = dxCommon->GetDSVCPUDescriptorHandle(1);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dxCommon->GetDevice()->CreateDepthStencilView(previewDepthResource_.Get(), &dsvDesc, previewDsvHandle_);

    // 白グリッドの頂点バッファと、カメラ viewProject 用の定数バッファを構築。
    BuildPreviewGrid();
    BuildPreviewWireBuffer();
    previewLineCB_ = dxCommon->CreateBufferResource(sizeof(Matrix4x4));
    previewLineCB_->Map(0, nullptr, reinterpret_cast<void **>(&previewLineCBData_));
    *previewLineCBData_ = MakeIdentity4x4();

    // 選択エミッタ隔離描画用の per-view CB。共有グループの per-view を汚さないため専用に持つ。
    previewPerViewCB_ = dxCommon->CreateBufferResource(sizeof(PerView));
    previewPerViewCB_->Map(0, nullptr, reinterpret_cast<void **>(&previewPerViewData_));
    previewPerViewData_->viewProjection = MakeIdentity4x4();
    previewPerViewData_->billboardMatrix = MakeIdentity4x4();
    previewPerViewData_->enableBillboard = 1;
    previewPerViewData_->enableVelocityStretch = 0;
    previewPerViewData_->velocityStretchFactor = 0.1f;
    previewPerViewData_->enableRotation = 1; // プレビューは常に回転を計算（正確さ優先・コストは僅少）

    previewInitialized_ = true;
}

// グリッド線VBを最大容量で確保し永続マップする。内容は RebuildPreviewGridContents で書き込む。
void ParticleCSEditor::BuildPreviewGrid() {
    DirectXCommon *dxCommon = ParticleCommon::GetInstance()->GetDxCommon();

    // 分割数の上限ぶん（XZ各 (div+1) 本 × 2頂点）を確保。
    const UINT maxVerts = static_cast<UINT>((kPreviewGridMaxDivision_ + 1) * 4);
    const UINT vbSize = static_cast<UINT>(sizeof(PreviewLineVertex) * maxVerts);
    previewGridVB_ = dxCommon->CreateBufferResource(vbSize);
    previewGridVB_->Map(0, nullptr, reinterpret_cast<void **>(&previewGridMapped_));

    previewGridVBView_.BufferLocation = previewGridVB_->GetGPUVirtualAddress();
    previewGridVBView_.StrideInBytes = sizeof(PreviewLineVertex);
    previewGridVBView_.SizeInBytes = vbSize;

    RebuildPreviewGridContents();
}

// ワイヤーフレーム用VBを最大容量で確保し永続マップする。内容は RenderPreview で毎フレーム書き込む。
void ParticleCSEditor::BuildPreviewWireBuffer() {
    DirectXCommon *dxCommon = ParticleCommon::GetInstance()->GetDxCommon();
    const UINT vbSize = static_cast<UINT>(sizeof(PreviewLineVertex) * kPreviewWireMaxVerts_);
    previewWireVB_ = dxCommon->CreateBufferResource(vbSize);
    previewWireVB_->Map(0, nullptr, reinterpret_cast<void **>(&previewWireMapped_));

    previewWireVBView_.BufferLocation = previewWireVB_->GetGPUVirtualAddress();
    previewWireVBView_.StrideInBytes = sizeof(PreviewLineVertex);
    previewWireVBView_.SizeInBytes = vbSize;
    previewWireVertexCount_ = 0;
}

// 現在のグリッド設定をマップ済みVBへ書き込み、描画頂点数を更新する。
// カメラ注視点を中心に追従し、線間隔にスナップすることで「ほぼ無限」のグリッドに見せる。
// 毎フレーム呼ばれる（DrawLine3D と同じ毎フレーム書き換えパターン）。
void ParticleCSEditor::RebuildPreviewGridContents() {
    if (!previewGridMapped_) {
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
    for (int i = 0; i <= division; ++i) {
        float offset = -halfSize + i * interval;
        float worldZ = centerZ + offset;
        float worldX = centerX + offset;
        // X方向の線（Z=worldZ 固定）。ワールド原点(z=0)を通る線を軸色に。
        const Vector4 &cX = (std::fabs(worldZ) < axisEps) ? axisColorX : gridColor;
        previewGridMapped_[v++] = {{centerX - halfSize, 0.0f, worldZ}, cX};
        previewGridMapped_[v++] = {{centerX + halfSize, 0.0f, worldZ}, cX};
        // Z方向の線（X=worldX 固定）。ワールド原点(x=0)を通る線を軸色に。
        const Vector4 &cZ = (std::fabs(worldX) < axisEps) ? axisColorZ : gridColor;
        previewGridMapped_[v++] = {{worldX, 0.0f, centerZ - halfSize}, cZ};
        previewGridMapped_[v++] = {{worldX, 0.0f, centerZ + halfSize}, cZ};
    }
    previewGridVertexCount_ = v;
}

// オービットカメラパラメータから view 行列と view*projection 行列を計算する。
void ParticleCSEditor::ComputePreviewMatrices(Matrix4x4 &outView, Matrix4x4 &outViewProj) const {
    // 球面座標からカメラ位置を求める。
    float cp = std::cos(previewCamPitch_);
    Vector3 eye = {
        previewCamTarget_.x + previewCamDistance_ * cp * std::sin(previewCamYaw_),
        previewCamTarget_.y + previewCamDistance_ * std::sin(previewCamPitch_),
        previewCamTarget_.z + previewCamDistance_ * cp * std::cos(previewCamYaw_),
    };

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
}

void ParticleCSEditor::RenderPreview() {
    if (!previewInitialized_) {
        return;
    }
    DirectXCommon *dxCommon = ParticleCommon::GetInstance()->GetDxCommon();
    ID3D12GraphicsCommandList *cl = dxCommon->GetCommandList().Get();

    // 色RT を RENDER_TARGET へ遷移
    D3D12_RESOURCE_BARRIER toRT{};
    toRT.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRT.Transition.pResource = previewColorResource_.Get();
    toRT.Transition.StateBefore = previewColorState_;
    toRT.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cl->ResourceBarrier(1, &toRT);
    previewColorState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // グリッドはカメラ注視点に追従させるため毎フレーム再構築する（内容のみ書き換え）。
    RebuildPreviewGridContents();
    previewGridDirty_ = false;

    // プレビューRT＋専用深度を束ねて背景色でクリア
    cl->OMSetRenderTargets(1, &previewRtvHandle_, false, &previewDsvHandle_);
    cl->ClearRenderTargetView(previewRtvHandle_, previewBgColor_, 0, nullptr);
    cl->ClearDepthStencilView(previewDsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // 実描画サイズ（ImGuiウィンドウ依存）でビューポート/シザーを設定。RTの左上部分のみに描く。
    // 後続ステージは PreRenderTexture で全画面へ復元される。
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(previewRenderWidth_);
    viewport.Height = static_cast<float>(previewRenderHeight_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    cl->RSSetViewports(1, &viewport);
    D3D12_RECT scissor{};
    scissor.right = static_cast<LONG>(previewRenderWidth_);
    scissor.bottom = static_cast<LONG>(previewRenderHeight_);
    cl->RSSetScissorRects(1, &scissor);

    // プレビューカメラ行列を計算（グリッド用 viewProject と、パーティクル用 per-view を構築）
    Matrix4x4 view{}, viewProj{};
    ComputePreviewMatrices(view, viewProj);

    // 白グリッドを描画（共有 DrawLine3D の頂点バッファとは衝突しない専用VB＋kLine3d PSO）
    if (previewShowGrid_ && previewGridVertexCount_ > 0) {
        *previewLineCBData_ = viewProj;
        PipeLineManager::GetInstance()->DrawCommonSetting(PipelineType::kLine3d);
        cl->IASetVertexBuffers(0, 1, &previewGridVBView_);
        cl->SetGraphicsRootConstantBufferView(0, previewLineCB_->GetGPUVirtualAddress());
        cl->DrawInstanced(previewGridVertexCount_, 1, 0, 0);
    }

    // 選択中エミッタのワイヤーフレームをプレビューVPで描画（共有 DrawLine3D は使わず専用VB＋kLine3d PSO）。
    // DrawEmitter は共有 DrawLine3D に積みシーン側VPで描かれてしまうため、ここで隔離描画する。
    if (previewShowEmitterWire_ && !selectedEmitterName_.empty() && previewWireMapped_) {
        auto itWire = emitters_.find(selectedEmitterName_);
        if (itWire != emitters_.end() && itWire->second) {
            auto segs = itWire->second->GetWireframeSegments();
            uint32_t v = 0;
            for (const auto &s : segs) {
                if (v + 2 > kPreviewWireMaxVerts_) {
                    break; // 上限超過分は切り捨て（プレビューのオーバーレイなので許容）
                }
                previewWireMapped_[v++] = {s.a, s.color};
                previewWireMapped_[v++] = {s.b, s.color};
            }
            previewWireVertexCount_ = v;
            if (v > 0) {
                *previewLineCBData_ = viewProj;
                PipeLineManager::GetInstance()->DrawCommonSetting(PipelineType::kLine3d);
                cl->IASetVertexBuffers(0, 1, &previewWireVBView_);
                cl->SetGraphicsRootConstantBufferView(0, previewLineCB_->GetGPUVirtualAddress());
                cl->DrawInstanced(v, 1, 0, 0);
            }
        }
    }

    // 選択中エミッタのパーティクルを隔離描画（Compute 済みバッファをプレビューVPで再描画）
    if (!selectedEmitterName_.empty()) {
        auto it = emitters_.find(selectedEmitterName_);
        if (it != emitters_.end() && it->second) {
            // 専用 per-view CB をプレビューVP＋ビルボードで更新（共有グループの per-view は汚さない）
            previewPerViewData_->viewProjection = viewProj;
            Matrix4x4 billboard = view;
            billboard.m[3][0] = 0.0f;
            billboard.m[3][1] = 0.0f;
            billboard.m[3][2] = 0.0f;
            billboard.m[3][3] = 1.0f;
            previewPerViewData_->billboardMatrix = Inverse(billboard);
            previewPerViewData_->enableBillboard = 1;
            previewPerViewData_->enableVelocityStretch = 0;

            // パーティクル PSO は SRV ディスクリプタテーブルを使うのでヒープを束ねる
            SrvManager::GetInstance()->SetDescriptorHeap();
            // RT/DSV/Viewport は上で束ね済み（ヒープ設定で解除されないが念のため再設定）
            cl->OMSetRenderTargets(1, &previewRtvHandle_, false, &previewDsvHandle_);
            cl->RSSetViewports(1, &viewport);
            cl->RSSetScissorRects(1, &scissor);
            it->second->DrawGraphicsForPreview(previewPerViewCB_->GetGPUVirtualAddress());
        }
    }

    // CPU パーティクル（ParticleEditor の選択中エミッタ）を同じプレビューRTへ描画する。
    // 編集中の CPU エミッタはシーンには描かれないため、このプレビューでのみ確認できる。
    {
        // CPU パーティクルPSOは SRV ディスクリプタテーブルを使うのでヒープを束ね直し、
        // RT/DSV/Viewport も（GPU側で未設定のケースに備え）念のため再設定する。
        SrvManager::GetInstance()->SetDescriptorHeap();
        cl->OMSetRenderTargets(1, &previewRtvHandle_, false, &previewDsvHandle_);
        cl->RSSetViewports(1, &viewport);
        cl->RSSetScissorRects(1, &scissor);

        // プレビューカメラの view / projection から CPU 用 ViewProjection を組む
        // （matView_ でビルボード、matView_×matProjection_ で WVP が決まる）。
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
    cl->ResourceBarrier(1, &toSRV);
    previewColorState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void ParticleCSEditor::ShowPreviewWindow(bool *pOpen) {
#ifdef USE_IMGUI
    if (!previewInitialized_) {
        return;
    }
    // 初回サイズ（以降ユーザーが自由にリサイズ）
    ImGui::SetNextWindowSize(ImVec2(1000.0f, 600.0f), ImGuiCond_FirstUseEver);
    // pOpen を渡すとウィンドウのXボタンが表示メニューのフラグと連動する
    if (!ImGui::Begin("CSパーティクル プレビュー", pOpen)) {
        ImGui::End();
        return;
    }

    // 右側エディタパネルの幅。左のビューポートは残り全幅を使う。
    const float kRightPanelWidth = 400.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float leftWidth = avail.x - kRightPanelWidth - spacing;
    if (leftWidth < 240.0f) {
        leftWidth = 240.0f; // ウィンドウが狭くてもビューポートの最低幅を確保
    }

    // ============ 左: プレビュービューポート（ImGuiウィンドウに追従して可変） ============
    ImGui::BeginChild("##previewViewport", ImVec2(leftWidth, 0.0f), true);
    {
        // 再生 / 一時停止 / 単発（選択中エミッタの自動発生を制御）
        if (selectedEmitterName_.empty()) {
            ImGui::TextDisabled("エミッタ未選択（グリッドのみ表示）");
        } else {
            auto it = emitters_.find(selectedEmitterName_);
            if (it != emitters_.end() && it->second) {
                ParticleCSEmitter *emitter = it->second.get();
                if (emitter->GetAuto()) {
                    if (ImGui::Button("一時停止##preview"))
                        emitter->SetAuto(false);
                } else {
                    if (ImGui::Button("再生##preview"))
                        emitter->SetAuto(true);
                }
                ImGui::SameLine();
                if (ImGui::Button("単発##preview"))
                    emitter->EmitOnce();
                ImGui::SameLine();
                ImGui::Text("選択中: %s", selectedEmitterName_.c_str());
            }
        }

        // 画像サイズ = 残りのコンテンツ領域。これを実描画サイズとして記録し、RT の左上部分を UV 表示する。
        ImVec2 region = ImGui::GetContentRegionAvail();
        uint32_t w = static_cast<uint32_t>((std::max)(16.0f, region.x));
        uint32_t h = static_cast<uint32_t>((std::max)(16.0f, region.y));
        w = (std::min)(w, kPreviewMaxWidth_);
        h = (std::min)(h, kPreviewMaxHeight_);
        previewRenderWidth_ = w;
        previewRenderHeight_ = h;

        ImTextureID texId = static_cast<ImTextureID>(
            SrvManager::GetInstance()->GetGPUDescriptorHandle(previewColorSrvIndex_).ptr);
        ImVec2 uv1(static_cast<float>(w) / static_cast<float>(kPreviewMaxWidth_),
                   static_cast<float>(h) / static_cast<float>(kPreviewMaxHeight_));
        ImGui::Image(texId, ImVec2(static_cast<float>(w), static_cast<float>(h)), ImVec2(0.0f, 0.0f), uv1);

        // 画像上でのオービットカメラ操作（左ドラッグ=回転 / ホイールクリックドラッグ=注視点パン / ホイール=ズーム）
        if (ImGui::IsItemHovered()) {
            ImGuiIO &io = ImGui::GetIO();
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                previewCamYaw_ += io.MouseDelta.x * 0.01f; // 左右回転（方向を反転）
                previewCamPitch_ += io.MouseDelta.y * 0.01f;
                const float pitchLimit = 1.55f; // ≒89°でジンバル反転を防ぐ
                previewCamPitch_ = (std::max)(-pitchLimit, (std::min)(pitchLimit, previewCamPitch_));
            }
            // ホイールクリック（中ボタン）ドラッグ = 注視点を画面平面に沿ってパン
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                // 現在のカメラパラメータから right / up ベクトルを求める（ComputePreviewMatrices と同一）
                float cp = std::cos(previewCamPitch_);
                Vector3 eye = {
                    previewCamTarget_.x + previewCamDistance_ * cp * std::sin(previewCamYaw_),
                    previewCamTarget_.y + previewCamDistance_ * std::sin(previewCamPitch_),
                    previewCamTarget_.z + previewCamDistance_ * cp * std::cos(previewCamYaw_),
                };
                Vector3 forward = (previewCamTarget_ - eye).Normalize();
                Vector3 worldUp = {0.0f, 1.0f, 0.0f};
                Vector3 right = worldUp.Cross(forward).Normalize();
                Vector3 up = forward.Cross(right);
                // 距離に比例したパン速度（遠いほど大きく動く＝直感的）。
                float panScale = previewCamDistance_ * 0.0015f;
                previewCamTarget_ = previewCamTarget_
                                    - right * (io.MouseDelta.x * panScale)
                                    + up * (io.MouseDelta.y * panScale);
            }
            if (io.MouseWheel != 0.0f) {
                previewCamDistance_ -= io.MouseWheel;
                previewCamDistance_ = (std::max)(1.0f, (std::min)(100.0f, previewCamDistance_));
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ============ 右: エディタパネル（作成・選択・動き設定・カメラ・表示設定すべて） ============
    ImGui::BeginChild("##previewEditor", ImVec2(0.0f, 0.0f), true);
    {
        // ---- カメラ操作 ----
        if (ImGui::CollapsingHeader("カメラ##preview", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("距離##preview", &previewCamDistance_, 0.1f, 1.0f, 100.0f);
            ImGui::DragFloat3("注視点##preview", &previewCamTarget_.x, 0.1f);
            if (ImGui::Button("カメラリセット##preview")) {
                previewCamYaw_ = 0.6f;
                previewCamPitch_ = 0.45f;
                previewCamDistance_ = 16.0f;
                previewCamTarget_ = {0.0f, 0.0f, 0.0f};
            }
        }

        // ---- 表示設定（背景色・グリッド・エミッタ枠）----
        if (ImGui::CollapsingHeader("表示設定##preview", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("背景色##preview", previewBgColor_);
            ImGui::Checkbox("エミッタ枠を表示##preview", &previewShowEmitterWire_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("選択中エミッタの発生形状（球/メッシュ/エッジ）の枠線をプレビューに表示します。");

            ImGui::Separator();
            ImGui::Checkbox("グリッド表示##preview", &previewShowGrid_);
            // グリッドは毎フレーム再構築するため dirty フラグは不要だが互換のため残す。
            ImGui::DragInt("分割数##preview", &previewGridDivision_, 1.0f, 2, kPreviewGridMaxDivision_);
            previewGridDivision_ = (std::max)(2, (std::min)(kPreviewGridMaxDivision_, previewGridDivision_));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("グリッド線の本数。半径 ÷ 本数 が線間隔になります（最大 %d）。", kPreviewGridMaxDivision_);
            ImGui::DragFloat("グリッド半径##preview", &previewGridHalfSize_, 0.5f, 0.1f, 2000.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("カメラ注視点を中心とした描画半径。大きくするほど遠くまで伸びます。");
            ImGui::ColorEdit3("グリッド色##preview", &previewGridColor_.x);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("線PSOは不透明描画です。暗い背景に対し暗いグレーにすると半透明風に見えます。");
        }

        ImGui::Separator();

        // ---- エミッタ/グループ作成・選択・動き設定（GPU / CPU をタブで切り替え）----
        if (ImGui::BeginTabBar("##previewParticleKind")) {
            if (ImGui::BeginTabItem("GPUパーティクル")) {
                ShowImGuiEditor(); // 「パーティクル作成」タブ
                DebugAll();        // 「GPUエミッター設定」タブ（選択コンボ＋選択エミッタの詳細設定）
                ImGui::EndTabItem();
            }
            // CPU パーティクルも同じプレビュー窓で確認・編集できるようにする。
            // 選択中の CPU エミッタは RenderPreview でプレビューRTへ描画される。
            if (ImGui::BeginTabItem("CPUパーティクル")) {
                ParticleEditor::GetInstance()->ShowImGuiEditor();
                ParticleEditor::GetInstance()->DebugAll();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();

    ImGui::End();
#endif // USE_IMGUI
}

// カラーテーマを設定する
void ParticleCSEditor::SetupColors() {
#ifdef USE_IMGUI
    // 各CollapsingHeaderに使用する色を定義
    headerColors_[0] = ImVec4(0.30f, 0.38f, 0.50f, 0.55f); // 青系
    headerColors_[1] = ImVec4(0.50f, 0.38f, 0.24f, 0.55f); // オレンジ系
    headerColors_[2] = ImVec4(0.30f, 0.44f, 0.34f, 0.55f); // 緑系
    headerColors_[3] = ImVec4(0.40f, 0.33f, 0.48f, 0.55f); // 紫系
    headerColors_[4] = ImVec4(0.50f, 0.46f, 0.28f, 0.55f); // 黄色系
    headerColors_[5] = ImVec4(0.32f, 0.33f, 0.36f, 0.55f); // グレー系
#endif                                                 // USE_IMGUI
}

void ParticleCSEditor::AddParticleEmitter(const std::string &name) {
    // Create standard sphere emitter
    auto emitter = std::make_unique<ParticleCSEmitter>();
    emitter->Initialize(name);
    emitters_[name] = std::move(emitter);
    ImGuiNotification::Post("GPUパーティクルエミッターを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void ParticleCSEditor::AddParticleEmitter(const std::string &name, const std::string &modelPath) {
    // Create model-based emitter
    auto emitter = std::make_unique<ParticleCSEmitter>();
    emitter->Initialize(name, modelPath);
    emitters_[name] = std::move(emitter);
    ImGuiNotification::Post("GPUパーティクルエミッターを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void ParticleCSEditor::AddParticleEmitter(const std::string &name, PrimitiveType primitiveType) {
    // Create primitive model-based emitter
    auto emitter = std::make_unique<ParticleCSEmitter>();
    emitter->Initialize(name, primitiveType);
    emitters_[name] = std::move(emitter);
    ImGuiNotification::Post("GPUパーティクルエミッターを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
}

void ParticleCSEditor::DrawAllCompute(const ViewProjection &vp_) {
    for (auto &[name, emitter] : emitters_) {
        emitter->Update();
        emitter->DrawCompute(vp_);
    }
}

void ParticleCSEditor::DrawAllGraphics(const ViewProjection &vp_) {
    for (auto &[name, emitter] : emitters_) {
        emitter->DrawGraphics(vp_);
    }
}

std::vector<std::string> ParticleCSEditor::GetEmitterNames() const {
    std::vector<std::string> names;
    names.reserve(emitters_.size());
    for (const auto &[name, emitter] : emitters_) {
        names.push_back(name);
    }
    return names;
}

ParticleCSEmitter *ParticleCSEditor::GetEmitterByName(const std::string &name) {
    auto it = emitters_.find(name);
    return (it != emitters_.end()) ? it->second.get() : nullptr;
}

void ParticleCSEditor::DrawAll(const ViewProjection &vp_) {
    // 後方互換: バッチなしで呼ばれた場合は内部で 2 フェーズを完結させる
    for (auto &[name, emitter] : emitters_) {
        emitter->Update();
        emitter->Draw(vp_);
    }
}

void ParticleCSEditor::Load() {
}

// 指定名のエミッターをmapから削除し、選択状態をリセットする
void ParticleCSEditor::RemoveParticleEmitter(const std::string &name) {
    auto it = emitters_.find(name);
    if (it == emitters_.end()) {
        return;
    }
    ImGuiNotification::Post("GPUパーティクルエミッターを削除しました: " + name, {0.9f, 0.7f, 0.2f, 1.0f});
    emitters_.erase(it);

    // 削除したエミッターが選択中だった場合はリセット
    if (selectedEmitterName_ == name) {
        selectedEmitterName_.clear();
        selectedEmitterIndex_ = 0;
    }
}

// エミッターとパーティクルグループの一覧表示・削除UIを描画する
void ParticleCSEditor::ShowDeleteSection() {
#ifdef USE_IMGUI
    // エミッター一覧と削除ボタン
    if (ColoredCollapsingHeader("エミッター一覧・削除", 0)) {
        if (emitters_.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "エミッターがありません");
        } else {
            // ループ中の削除を避けるため、削除対象を先に確定してから消す
            std::string toDelete;
            for (const auto &[name, emitter] : emitters_) {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", name.c_str());
                ImGui::SameLine();

                // ボタンIDをエミッター名で一意にする
                std::string btnLabel = "削除##Emitter_" + name;
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));
                if (ImGui::SmallButton(btnLabel.c_str())) {
                    toDelete = name;
                }
                ImGui::PopStyleColor(3);
            }
            if (!toDelete.empty()) {
                RemoveParticleEmitter(toDelete);
                ImGuizmoManager::GetInstance()->RemoveTarget(toDelete);
            }
        }
    }

    ImGui::Spacing();

    // パーティクルグループ一覧と削除ボタン
    if (ColoredCollapsingHeader("グループ一覧・削除", 1)) {
        auto groups = particleGroupManager_->GetParticleGroups();
        if (groups.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "グループがありません");
        } else {
            // ループ中の削除を避けるため、削除対象を先に確定してから消す
            std::string toDelete;
            for (const auto &group : groups) {
                if (!group) {
                    continue;
                }
                const std::string &groupName = group->GetGroupName();
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", groupName.c_str());
                ImGui::SameLine();

                // ボタンIDをグループ名で一意にする
                std::string btnLabel = "削除##Group_" + groupName;
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));
                if (ImGui::SmallButton(btnLabel.c_str())) {
                    toDelete = groupName;
                }
                ImGui::PopStyleColor(3);
            }
            if (!toDelete.empty()) {
                particleGroupManager_->RemoveParticleCSGroup(toDelete);
                std::unique_ptr<DataHandler> groupData = std::make_unique<DataHandler>("ParticleCSGroup", toDelete);
                groupData->DeleteJson(toDelete);
            }
        }
    }
#endif // USE_IMGUI
}

void ParticleCSEditor::AddParticleGroup(const std::string &name, const std::string &fileName, uint32_t maxParticleCount, const std::string &texturePath) {
    ImGuiNotification::Post("パーティクルグループを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
    // 新しい ParticleGroup を作成
    auto group = std::make_unique<ParticleCSGroup>();
    // パーティクルグループを作成
    group->CreateParticleGroup(name, fileName, maxParticleCount, texturePath);

    // マップに追加
    particleGroupManager_->AddParticleCSGroup(std::move(group));
}

void ParticleCSEditor::AddPrimitiveParticleGroup(const std::string &name, PrimitiveType type, uint32_t maxParticleCount, const std::string &texturePath) {
    ImGuiNotification::Post("プリミティブパーティクルグループを追加しました: " + name, {0.4f, 0.8f, 1.0f, 1.0f});
    // 新しい ParticleGroup を作成
    auto group = std::make_unique<ParticleCSGroup>();
    // プリミティブパーティクルグループを作成
    group->CreatePrimitiveParticleGroup(name, type, maxParticleCount, texturePath);
    // マップに追加
    particleGroupManager_->AddParticleCSGroup(std::move(group));
}

std::unique_ptr<ParticleCSEmitter> ParticleCSEditor::CreateEmitterFromTemplate(const std::string &name) {
    ParticleCSEmitter *originalEmitter = nullptr;
    auto it = emitters_.find(name);
    if (it != emitters_.end()) {
        originalEmitter = it->second.get();
    }
    if (!originalEmitter) {
        return nullptr;
    }
    return originalEmitter->Clone();
}

void ParticleCSEditor::ShowGPUParticleStatistics() {
#ifdef USE_IMGUI
    if (ImGui::CollapsingHeader("GPUパーティクル統計")) {

        // エミッター名ごとに集計
        std::map<std::string, size_t> emitterStats;
        size_t grandTotal = 0;

        for (const auto &[name, emitter] : emitters_) {
            if (!emitter)
                continue;

            size_t emitterTotal = emitter->GetTotalAliveParticles();

            emitterStats[name] = emitterTotal;
            grandTotal += emitterTotal;
        }

        // ヘッダー情報
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "合計: %zu個", grandTotal);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "(%zu種類)", emitterStats.size());

        if (!emitterStats.empty()) {
            ImGui::Separator();

            // エミッターごとに表示
            for (const auto &[emitterName, count] : emitterStats) {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", emitterName.c_str());
                ImGui::SameLine();
                ImGui::Text(": %zu", count);
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "エミッターなし");
        }
    }
#endif // USE_IMGUI
}

void ParticleCSEditor::DebugAll() {
#ifdef USE_IMGUI
    if (ImGui::BeginTabBar("GPUパーティクル")) {
        if (ImGui::BeginTabItem("GPUエミッター設定")) {
            if (emitters_.empty()) {
                ImGui::Text("エミッターがありません");
            } else {
                // エミッター名のリストを作成
                std::vector<std::string> emitterNames;
                for (const auto &[name, emitter] : emitters_) {
                    emitterNames.push_back(name);
                }

                // インデックスの範囲チェック
                if (selectedEmitterIndex_ >= emitterNames.size()) {
                    selectedEmitterIndex_ = std::max(0, (int)emitterNames.size() - 1);
                }

                // エミッター選択用のCombo
                std::vector<const char *> emitterNameCStrs;
                for (const auto &name : emitterNames) {
                    emitterNameCStrs.push_back(name.c_str());
                }

                if (ImGui::Combo("GPU エミッター選択", &selectedEmitterIndex_,
                                 emitterNameCStrs.data(), (int)emitterNameCStrs.size())) {
                    selectedEmitterName_ = emitterNames[selectedEmitterIndex_];
                }

                // 初回選択時の処理
                if (selectedEmitterName_.empty() && !emitterNames.empty()) {
                    selectedEmitterName_ = emitterNames[selectedEmitterIndex_];
                }

                // 選択されたエミッターのDebugを実行
                if (!selectedEmitterName_.empty()) {
                    auto it = emitters_.find(selectedEmitterName_);
                    if (it != emitters_.end() && it->second) {
                        it->second->DrawImGui();
                    }
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
#endif // USE_IMGUI
}

void ParticleCSEditor::EditorWindow() {
#ifdef USE_IMGUI
    ImGui::Begin("CSパーティクルエディター");
    ShowImGuiEditor();
    ImGui::End();
#endif // USE_IMGUI
}

// カラー付きCollapsingHeaderを表示するヘルパー関数
bool ParticleCSEditor::ColoredCollapsingHeader(const char *label, int colorIndex) {
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

void ParticleCSEditor::ShowImGuiEditor() {
#ifdef USE_IMGUI
    // プレビュー窓の表示は ImGuiManager の「表示 > ウィンドウ > パーティクルプレビュー」で管理する
    // （ここでは描画しない）

    if (ImGui::BeginTabBar("GPUパーティクル")) {
        if (ImGui::BeginTabItem("パーティクル作成")) {
            // エミッター追加のCollapsingHeader
            if (ColoredCollapsingHeader("エミッター追加", 0)) {
                // 名前の入力
                char nameBuffer[256];
                strcpy_s(nameBuffer, sizeof(nameBuffer), localEmitterName_.c_str());
                ImGui::Text("エミッターの名前");
                if (ImGui::InputText(" ", nameBuffer, sizeof(nameBuffer))) {
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
                if (selectedEmitterType == 0) {
                    // エミッター用モデル選択セクション
                    if (ColoredCollapsingHeader("モデル選択##EmitterModel", 2)) {
                        // モデルファイル選択 (既存のコードと同じ)
                        static std::filesystem::path baseDirObj = "resources/models/";
                        static std::filesystem::path currentDirObj = "resources/models";
                        static std::string selectedFolderObj = "";
                        static std::string selectedFileObj = "";

                        // 「戻る」ボタン（上の階層に戻る）
                        if (currentDirObj != "resources/models") {
                            if (ImGui::Button("< 戻る(Emitter Model)")) {
                                currentDirObj = currentDirObj.parent_path();
                                selectedFolderObj = "";
                                selectedFileObj = "";
                            }
                        }

                        // フォルダ一覧
                        std::vector<std::string> foldersObj;
                        std::vector<std::string> objFiles;

                        for (const auto &entry : std::filesystem::directory_iterator(currentDirObj)) {
                            if (entry.is_directory()) {
                                foldersObj.push_back(entry.path().filename().string());
                            } else if (entry.path().extension() == ".obj") {
                                objFiles.push_back(entry.path().filename().string());
                            }
                        }

                        // フォルダ選択 (クリックで移動)
                        if (!foldersObj.empty()) {
                            ImGui::Text("フォルダ");
                            ImGui::Separator();
                            for (const auto &folder : foldersObj) {
                                std::string folderNameTex = folder + " (Emitter Model)";
                                if (ImGui::Selectable(folderNameTex.c_str(), selectedFolderObj == folder)) {
                                    selectedFolderObj = folderNameTex;
                                    currentDirObj = currentDirObj / folder;
                                    selectedFileObj = "";
                                }
                                ImGui::Separator();
                            }
                        }

                        // `.obj` ファイル選択
                        if (!objFiles.empty()) {
                            ImGui::Text("モデルファイル:");
                            if (ImGui::BeginCombo("ファイル選択##EmitterModel", selectedFileObj.empty() ? "なし" : selectedFileObj.c_str())) {
                                for (const auto &file : objFiles) {
                                    bool isSelected = (file == selectedFileObj);
                                    if (ImGui::Selectable(file.c_str(), isSelected)) {
                                        selectedFileObj = file;

                                        // `baseDirObj` からの相対パスを取得
                                        std::filesystem::path relativePath = (currentDirObj / file).lexically_relative(baseDirObj);

                                        // Windowsのバックスラッシュをスラッシュに変換
                                        std::string pathStr = relativePath.string();
                                        std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

                                        // `localEmitterModelPath_` に保存
                                        localEmitterModelPath_ = pathStr;
                                    }
                                    if (isSelected) {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                ImGui::EndCombo();
                            }
                        }
                    }

                    if (!localEmitterName_.empty() && !localEmitterModelPath_.empty()) {
                        if (ImGui::Button("モデルエミッター生成")) {
                            AddParticleEmitter(localEmitterName_, localEmitterModelPath_);
                            localEmitterName_.clear();
                            localEmitterModelPath_.clear();
                        }
                    }
                }
                // プリミティブエミッター選択時
                else if (selectedEmitterType == 1) {
                    // エミッター用プリミティブタイプ選択セクション
                    if (ColoredCollapsingHeader("プリミティブタイプ選択##EmitterPrimitive", 4)) {
                        const char *primitiveType[] = {"未選択", "プレーン", "球", "キューブ", "シリンダー", "リング", "三角形", "円錐", "四角錐", "円柱"};
                        int currentPrimitiveType = static_cast<int>(localEmitterType_);
                        if (ImGui::Combo("タイプ選択##EmitterPrimitive", &currentPrimitiveType, primitiveType, IM_ARRAYSIZE(primitiveType))) {
                            localEmitterType_ = static_cast<PrimitiveType>(currentPrimitiveType);
                        }
                    }

                    if (!localEmitterName_.empty() && localEmitterType_ != PrimitiveType::None) {
                        if (ImGui::Button("プリミティブエミッター生成")) {
                            AddParticleEmitter(localEmitterName_, localEmitterType_);
                            localEmitterName_.clear();
                            localEmitterType_ = PrimitiveType::None;
                        }
                    }
                }
            }

            // パーティクルグループ作成のCollapsingHeader
            if (ColoredCollapsingHeader("パーティクルグループ作成", 1)) {
                // 名前の入力
                char nameBuffer[256];
                strcpy_s(nameBuffer, sizeof(nameBuffer), localName_.c_str());
                ImGui::Text("パーティクルグループの名前");
                if (ImGui::InputText("  ", nameBuffer, sizeof(nameBuffer))) {
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
                if (selectedType == 0) {
                    // グループ用モデル選択セクション (青色)
                    if (ColoredCollapsingHeader("モデル選択##GroupModel", 2)) {
                        // モデルファイル選択
                        static std::filesystem::path baseDirObj = "resources/models/";
                        static std::filesystem::path currentDirObj = "resources/models";
                        static std::string selectedFolderObj = "";
                        static std::string selectedFileObj = "";

                        // 「戻る」ボタン（上の階層に戻る）
                        if (currentDirObj != "resources/models") {
                            if (ImGui::Button("< 戻る(Model)")) {
                                currentDirObj = currentDirObj.parent_path();
                                selectedFolderObj = "";
                                selectedFileObj = "";
                            }
                        }

                        // フォルダ一覧
                        std::vector<std::string> foldersObj;
                        std::vector<std::string> objFiles;

                        for (const auto &entry : std::filesystem::directory_iterator(currentDirObj)) {
                            if (entry.is_directory()) {
                                foldersObj.push_back(entry.path().filename().string());
                            } else if (entry.path().extension() == ".obj") {
                                objFiles.push_back(entry.path().filename().string());
                            }
                        }

                        // フォルダ選択 (クリックで移動)
                        if (!foldersObj.empty()) {
                            ImGui::Text("フォルダ");
                            ImGui::Separator();
                            for (const auto &folder : foldersObj) {
                                std::string folderNameTex = folder + " (Model)"; // フォルダ名に "(Model)" を追加
                                if (ImGui::Selectable(folderNameTex.c_str(), selectedFolderObj == folder)) {
                                    selectedFolderObj = folderNameTex;
                                    currentDirObj = currentDirObj / folder; // フォルダ移動
                                    selectedFileObj = "";                   // 新しいフォルダを開いたらファイル選択をリセット
                                }
                                ImGui::Separator();
                            }
                        }

                        // `.obj` ファイル選択
                        if (!objFiles.empty()) {
                            ImGui::Text("モデルファイル:");
                            if (ImGui::BeginCombo("ファイル選択##GroupModel", selectedFileObj.empty() ? "なし" : selectedFileObj.c_str())) {
                                for (const auto &file : objFiles) {
                                    bool isSelected = (file == selectedFileObj);
                                    if (ImGui::Selectable(file.c_str(), isSelected)) {
                                        selectedFileObj = file;

                                        // `baseDirObj` からの相対パスを取得
                                        std::filesystem::path relativePath = (currentDirObj / file).lexically_relative(baseDirObj);

                                        // Windowsのバックスラッシュをスラッシュに変換
                                        std::string pathStr = relativePath.string();
                                        std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

                                        // `fileNameObj_` に保存
                                        localFileObj_ = pathStr;
                                    }
                                    if (isSelected) {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                ImGui::EndCombo();
                            }
                        }
                    }

                    // グループ用テクスチャ選択セクション (緑色)
                    if (ColoredCollapsingHeader("テクスチャ選択##GroupModel", 3)) {
#ifdef _DEBUG
                        ShowTextureFile(localTexturePath_);
#endif // _DEBUG
                    }

                    if (localMaxParticleCount_ == 0) {
                        localMaxParticleCount_ = 100; // デフォルト
                    }
                    ImGui::Spacing();
                    ImGui::Text("最大パーティクル数（注意: 多すぎると重くなる可能性あり）");
                    ImGui::DragInt("##MaxParticleCountModel", &localMaxParticleCount_, 100);

                    // ボタン
                    if (!localName_.empty() && !localFileObj_.empty()) {
                        if (ImGui::Button("モデルパーティクルグループ生成")) {
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
                else if (selectedType == 1) {
                    // グループ用プリミティブタイプ選択セクション (紫色)
                    if (ColoredCollapsingHeader("プリミティブタイプ選択##GroupPrimitive", 4)) {
                        const char *primitiveType[] = {"未選択", "プレーン", "球", "キューブ", "シリンダー", "リング", "三角形", "円錐", "四角錐"};
                        int currentPrimitiveType = static_cast<int>(localType_);
                        // 初期値が未選択（None = -1）の場合に対応するため +1 して選択肢に表示
                        if (ImGui::Combo("タイプ選択##GroupPrimitive", &currentPrimitiveType, primitiveType, IM_ARRAYSIZE(primitiveType))) {
                            localType_ = static_cast<PrimitiveType>(currentPrimitiveType);
                        }
                    }

                    // グループ用テクスチャ選択セクション (オレンジ色)
                    if (ColoredCollapsingHeader("テクスチャ選択##GroupPrimitive", 5)) {
#ifdef _DEBUG
                        ShowTextureFile(localTexturePath_);
#endif // _DEBUG
                    }

                    if (localMaxParticleCount_ == 0) {
                        localMaxParticleCount_ = 10000; // デフォルト
                    }
                    ImGui::Spacing();
                    ImGui::Text("最大パーティクル数");
                    ImGui::DragInt("##MaxParticleCountPrimitive", &localMaxParticleCount_, 100);

                    // ボタン
                    if (!localName_.empty()) {
                        bool isTypeInvalid = (localType_ == PrimitiveType::None);
                        if (isTypeInvalid) {
                            ImGui::BeginDisabled();
                        }

                        if (ImGui::Button("プリミティブパーティクルグループ生成")) {
                            if (localMaxParticleCount_ <= 0)
                                localMaxParticleCount_ = 10000; // 保険
                            AddPrimitiveParticleGroup(localName_, localType_, localMaxParticleCount_, localTexturePath_);
                            localName_.clear();
                            localTexturePath_.clear();
                            localType_ = PrimitiveType::None;
                            localMaxParticleCount_ = 0;
                        }

                        if (isTypeInvalid) {
                            ImGui::EndDisabled();
                        }
                    }
                }
            }

            // パーティクルデータのロードセクション (黄色系)
            if (ColoredCollapsingHeader("パーティクルデータのロード", 2)) {
                ShowFileSelector();
            }

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));        // 赤系
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f)); // ホバー時
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));  // 押下時
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);                        // 角丸
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20.0f, 10.0f));         // パディング

            if (ImGui::Button("全パーティクルを止める", ImVec2(200, 40))) {
                for (auto &emitter : emitters_) {
                    emitter.second->SetAuto(false);
                }
            }

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);

            ImGui::EndTabItem();
        }

        // エミッター・グループの一覧表示と削除管理タブ
        if (ImGui::BeginTabItem("削除管理")) {
            ShowDeleteSection();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

#endif // USE_IMGUI
}

void ParticleCSEditor::ShowFileSelector() {
#ifdef USE_IMGUI
    static int selectedIndex = -1;
    std::vector<std::string> jsonFiles = GetJsonFiles();

    // JSONファイルがない場合のチェック
    if (jsonFiles.empty()) {
        ImGui::Text("Jsonファイルが見つかりませんでした");
        return;
    }

    // ファイルリストをCスタイル文字列の配列に変換
    std::vector<const char *> fileNames;
    for (const auto &filePath : jsonFiles) {
        fileNames.push_back(filePath.c_str());
    }

    ImGui::Text("Jsonファイルの選択:");
    ImGui::Separator();

    // Comboボックスでファイル選択
    if (ImGui::Combo("JSON Files", &selectedIndex, fileNames.data(), static_cast<int>(fileNames.size()))) {
        // ファイル選択時の動作（選択されたファイル名を表示）
        if (selectedIndex >= 0) {
            ImGui::Text("ファイル選択:");
            ImGui::TextWrapped("%s", jsonFiles[selectedIndex].c_str());
        }
    }

    // ボタンでパーティクルデータをセット
    if (selectedIndex >= 0 && ImGui::Button("パーティクルデータのセット")) {
        isLoad_ = true;
        // name_ に ".json" を除いた名前を設定
        std::string selectedFileName = jsonFiles[selectedIndex];
        name_ = selectedFileName.substr(0, selectedFileName.find_last_of('.')); // ".json" を除去
        AddParticleEmitter(name_);
        isLoad_ = false;
    }
#endif // USE_IMGUI
}

std::vector<std::string> ParticleCSEditor::GetJsonFiles() {
    static std::vector<std::string> jsonFiles; // キャッシュされたJSONファイルリスト
    static size_t lastFileCount = 0;           // 最後に取得したJSONファイル数
    std::filesystem::path baseDir = "resources/jsons/ParticleCS";

    // ディレクトリが存在しない場合はキャッシュをクリア
    if (!std::filesystem::exists(baseDir) || !std::filesystem::is_directory(baseDir)) {
        jsonFiles.clear();
        lastFileCount = 0;
        return jsonFiles;
    }

    // 現在のファイル数をカウント
    size_t currentFileCount = std::count_if(
        std::filesystem::directory_iterator(baseDir),
        std::filesystem::directory_iterator{},
        [](const std::filesystem::directory_entry &entry) {
            return entry.path().extension() == ".json";
        });

    // ファイル数が変わった場合のみ更新
    if (currentFileCount != lastFileCount) {
        jsonFiles.clear(); // リストをクリア
        for (const auto &entry : std::filesystem::directory_iterator(baseDir)) {
            if (entry.path().extension() == ".json") {
                jsonFiles.push_back(entry.path().filename().string());
            }
        }
        lastFileCount = currentFileCount; // 更新したファイル数を記録
    }

    return jsonFiles;
}
} // namespace Hagine
