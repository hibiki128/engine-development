#include "ShadowMap.h"
#include "Data/DataHandler.h"
#include "DirectXCommon.h"
#include "Utility/Debug/ImGui/ImGuiNotification.h"
#include "Graphics/Srv/SrvManager.h"
#include "Light/LightGroup.h"
#include <cmath>
#include <myMath.h>
#ifdef _DEBUG
#include "imgui.h"
#include "Utility/Debug/ImGui/Debugui_improved.h"
#endif

namespace Hagine {
using namespace Microsoft::WRL;

void ShadowMap::Initialize() {
    dxCommon_   = DirectXCommon::GetInstance();
    srvManager_ = SrvManager::GetInstance();

    // ----- 深度テクスチャ作成 (R32_TYPELESS) -----
    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width            = kShadowMapSize;
    depthDesc.Height           = kShadowMapSize;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels        = 1;
    depthDesc.Format           = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format               = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth   = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&depthResource_));
    assert(SUCCEEDED(hr));

    // ----- DSV ヒープ・ビュー作成 -----
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = dxCommon_->GetDevice()->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_));
    assert(SUCCEEDED(hr));

    dsvHandle_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format             = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags              = D3D12_DSV_FLAG_NONE;
    dxCommon_->GetDevice()->CreateDepthStencilView(depthResource_.Get(), &dsvDesc, dsvHandle_);

    // ----- SRV 作成 (R32_FLOAT) -----
    CreateShadowSRV();

    // ----- ShadowData 定数バッファ -----
    shadowDataResource_ = dxCommon_->CreateBufferResource(sizeof(ShadowDataGPU));
    shadowDataResource_->Map(0, nullptr, reinterpret_cast<void **>(&shadowDataPtr_));

    Update();
}

void ShadowMap::Finalize() {
    if (shadowDataResource_ && shadowDataPtr_) {
        shadowDataResource_->Unmap(0, nullptr);
        shadowDataPtr_ = nullptr;
    }
    if (srvIndex_ != UINT32_MAX) {
        srvManager_->Free(srvIndex_);
        srvIndex_ = UINT32_MAX;
    }
    depthResource_.Reset();
    dsvHeap_.Reset();
    shadowDataResource_.Reset();
}

void ShadowMap::CreateShadowSRV() {
    srvIndex_ = srvManager_->Allocate();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;
    srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels       = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    dxCommon_->GetDevice()->CreateShaderResourceView(
        depthResource_.Get(), &srvDesc, srvManager_->GetCPUDescriptorHandle(srvIndex_));
}

void ShadowMap::BeginShadowPass() {
    auto *cmdList = dxCommon_->GetCommandList().Get();

    if (currentState_ != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = depthResource_.Get();
        barrier.Transition.StateBefore = currentState_;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);
    }
    currentState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    // レンダーターゲットなし + シャドウ DSV
    cmdList->OMSetRenderTargets(0, nullptr, false, &dsvHandle_);
    cmdList->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // ビューポート・シザーRect
    D3D12_VIEWPORT vp{};
    vp.Width    = static_cast<float>(kShadowMapSize);
    vp.Height   = static_cast<float>(kShadowMapSize);
    vp.MaxDepth = 1.0f;
    cmdList->RSSetViewports(1, &vp);

    D3D12_RECT sc{};
    sc.right  = kShadowMapSize;
    sc.bottom = kShadowMapSize;
    cmdList->RSSetScissorRects(1, &sc);
}

void ShadowMap::EndShadowPass() {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = depthResource_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_GENERIC_READ;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    dxCommon_->GetCommandList()->ResourceBarrier(1, &barrier);
    currentState_ = D3D12_RESOURCE_STATE_GENERIC_READ;
}

Matrix4x4 ShadowMap::MakeLightViewMatrix() const {
    Vector3 dir = lightDir_.Normalize();

    // ライトカメラ位置
    Vector3 eye = {
        lightTarget_.x - dir.x * lightDistance_,
        lightTarget_.y - dir.y * lightDistance_,
        lightTarget_.z - dir.z * lightDistance_
    };

    // forward = dir（シーン中心を向く）
    Vector3 forward = dir;

    // up ベクトル（forward と並行しない軸を選ぶ）
    Vector3 worldUp = {0.f, 1.f, 0.f};
    if (std::fabsf(forward.Dot(worldUp)) > 0.99f) {
        worldUp = {0.f, 0.f, 1.f};
    }

    Vector3 right   = worldUp.Cross(forward).Normalize();
    Vector3 actualUp = forward.Cross(right);

    float tx = -right.Dot(eye);
    float ty = -actualUp.Dot(eye);
    float tz = -forward.Dot(eye);

    Matrix4x4 view{};
    view.m[0][0] = right.x;    view.m[0][1] = actualUp.x; view.m[0][2] = forward.x; view.m[0][3] = 0.f;
    view.m[1][0] = right.y;    view.m[1][1] = actualUp.y; view.m[1][2] = forward.y; view.m[1][3] = 0.f;
    view.m[2][0] = right.z;    view.m[2][1] = actualUp.z; view.m[2][2] = forward.z; view.m[2][3] = 0.f;
    view.m[3][0] = tx;         view.m[3][1] = ty;          view.m[3][2] = tz;         view.m[3][3] = 1.f;
    return view;
}

void ShadowMap::Update() {
    // DirectionalLight の方向を同期
    if (syncWithDirectionalLight_) {
        LightGroup *lg = LightGroup::GetInstance();
        if (lg->IsDirectionalLightActive()) {
            lightDir_ = lg->GetDirectionalLightDirection();
        }
    }

    Matrix4x4 lightView = MakeLightViewMatrix();
    Matrix4x4 lightProj = MakeOrthographicMatrix(
        -orthoWidth_, orthoHeight_, orthoWidth_, -orthoHeight_, nearZ_, farZ_);
    lightViewProj_ = lightView * lightProj;

    if (shadowDataPtr_) {
        shadowDataPtr_->enabled  = enabled_ ? 1 : 0;
        shadowDataPtr_->bias     = bias_;
        shadowDataPtr_->strength = strength_;
        shadowDataPtr_->padding  = 0.f;
    }
}

D3D12_GPU_VIRTUAL_ADDRESS ShadowMap::GetShadowDataGpuAddress() const {
    return shadowDataResource_->GetGPUVirtualAddress();
}

void ShadowMap::UpdateImGui(bool *open) {
#ifdef _DEBUG
    // ラベル列を作って次の列に全幅ウィジェットを置く準備をする
    auto label = [](const char *text) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
    };

    // 表示名は日本語、ウィンドウIDは "ShadowMap" のまま（保存済みレイアウトとの互換維持）
    if (ImGui::Begin("シャドウマップ###ShadowMap", open, ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
        ImGui::Checkbox("シャドウ有効", &enabled_);
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("シャドウマップ描画の ON / OFF");

        ImGui::Spacing();
        SectionHeader("[ ライト ]", DebugTheme::kAccentBlue);
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);
        ImGui::Checkbox("DirectionalLight と同期", &syncWithDirectionalLight_);
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("ライト方向を平行光源から自動取得します");

        if (ImGui::BeginTable("##ShadowLight", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);

            label("ライト方向");
            if (!syncWithDirectionalLight_) {
                ImGui::DragFloat3("##lightDir", &lightDir_.x, 0.01f, -1.f, 1.f, "%.2f");
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("自動取得中");
                ImGui::PopStyleColor();
            }
            label("シャドウ中心");
            ImGui::DragFloat3("##target", &lightTarget_.x, 0.1f, 0.0f, 0.0f, "%.2f");
            label("ライト距離");
            ImGui::DragFloat("##dist", &lightDistance_, 0.5f, 1.f, 300.f, "%.1f");
            label("正射影幅");
            ImGui::DragFloat("##ow", &orthoWidth_, 0.5f, 1.f, 200.f, "%.1f");
            label("正射影高さ");
            ImGui::DragFloat("##oh", &orthoHeight_, 0.5f, 1.f, 200.f, "%.1f");
            label("Near");
            ImGui::DragFloat("##near", &nearZ_, 0.01f, 0.01f, 10.f, "%.2f");
            label("Far");
            ImGui::DragFloat("##far", &farZ_, 1.f, 10.f, 1000.f, "%.1f");

            ImGui::EndTable();
        }

        ImGui::Spacing();
        SectionHeader("[ シャドウ品質 ]", DebugTheme::kAccentOrange);
        if (ImGui::BeginTable("##ShadowQual", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);

            label("バイアス");
            ImGui::DragFloat("##bias", &bias_, 0.0001f, 0.f, 0.1f, "%.5f");
            ImGui::SetItemTooltip("シャドウアクネ対策の深度オフセット");
            label("強度");
            ImGui::DragFloat("##strength", &strength_, 0.01f, 0.f, 1.f, "%.2f");

            ImGui::EndTable();
        }

        ImGui::Spacing();
        SectionHeader("[ セーブ / ロード ]", DebugTheme::kAccentPurple);
        float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.58f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.52f, 0.70f, 0.95f));
        if (ImGui::Button("保存", ImVec2(bw, 0))) { SaveConfig(); }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.40f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.60f, 0.50f, 0.95f));
        if (ImGui::Button("読み込み", ImVec2(bw, 0))) { LoadConfig(); }
        ImGui::PopStyleColor(2);
    }
    ImGui::End();
#endif
}

void ShadowMap::SaveConfig(const std::string &fileName) {
    auto data = std::make_unique<DataHandler>("ShadowMap", fileName);
    data->Save("enabled", static_cast<int>(enabled_));
    data->Save("syncWithDirectionalLight", static_cast<int>(syncWithDirectionalLight_));
    data->Save("bias", bias_);
    data->Save("strength", strength_);
    data->Save("lightDirX", lightDir_.x);
    data->Save("lightDirY", lightDir_.y);
    data->Save("lightDirZ", lightDir_.z);
    data->Save("lightTargetX", lightTarget_.x);
    data->Save("lightTargetY", lightTarget_.y);
    data->Save("lightTargetZ", lightTarget_.z);
    data->Save("lightDistance", lightDistance_);
    data->Save("orthoWidth", orthoWidth_);
    data->Save("orthoHeight", orthoHeight_);
    data->Save("nearZ", nearZ_);
    data->Save("farZ", farZ_);
    ImGuiNotification::Post("シャドウ設定を保存しました: " + fileName, {0.2f, 0.8f, 0.2f, 1.0f});
}

void ShadowMap::LoadConfig(const std::string &fileName) {
    auto data = std::make_unique<DataHandler>("ShadowMap", fileName);
    enabled_                  = static_cast<bool>(data->Load<int>("enabled", 1));
    syncWithDirectionalLight_ = static_cast<bool>(data->Load<int>("syncWithDirectionalLight", 1));
    bias_          = data->Load<float>("bias", 0.001f);
    strength_      = data->Load<float>("strength", 0.7f);
    lightDir_.x    = data->Load<float>("lightDirX", 0.f);
    lightDir_.y    = data->Load<float>("lightDirY", -1.f);
    lightDir_.z    = data->Load<float>("lightDirZ", 0.5f);
    lightTarget_.x = data->Load<float>("lightTargetX", 0.f);
    lightTarget_.y = data->Load<float>("lightTargetY", 0.f);
    lightTarget_.z = data->Load<float>("lightTargetZ", 0.f);
    lightDistance_ = data->Load<float>("lightDistance", 80.f);
    orthoWidth_    = data->Load<float>("orthoWidth", 40.f);
    orthoHeight_   = data->Load<float>("orthoHeight", 40.f);
    nearZ_         = data->Load<float>("nearZ", 0.1f);
    farZ_          = data->Load<float>("farZ", 200.f);
    Update();
    ImGuiNotification::Post("シャドウ設定を読み込みました: " + fileName, {0.2f, 0.8f, 0.8f, 1.0f});
}
} // namespace Hagine
