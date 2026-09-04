#include "LightGroup.h"
#include "DirectXCommon.h"
#include <line/LineRenderer.h>
#include <render/deferred/DeferredRenderer.h>
#include <utility/debug/imgui/DebugUIHelper.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#ifdef USE_IMGUI
#include <utility/debug/imgui/ImGuizmoManager.h>
#endif

namespace Hagine {
namespace {
// ギズモ登録名の接頭辞。オブジェクトやスプライトと名前が衝突しないよう名前空間を分ける
constexpr const char *kGizmoPrefix = "光源/";
// スポットライトの向きハンドルにつける接尾辞
constexpr const char *kGizmoAimSuffix = " (向き)";

#ifdef USE_IMGUI
/// <summary>
/// 「ラベル + 全幅ウィジェット」の2列行を描く
/// </summary>
/// <param name="label">左に出す項目名</param>
/// <param name="tip">ラベルにつけるツールチップ（不要なら nullptr）</param>
/// <param name="drawWidget">右列に描くウィジェット</param>
template <typename F>
void LabeledRow(const char *label, const char *tip, F &&drawWidget)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (tip && tip[0])
        ImGui::SetItemTooltip("%s", tip);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    drawWidget();
}

/// <summary>
/// 一覧行の先頭に置く色見本。ColorButton と違いクリックしてもピッカーが開かない
/// </summary>
/// <param name="color">表示する色</param>
void ColorSwatch(const Vector4 &color)
{
    const float height = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 1.0f));
    drawList->AddRectFilled(pos, ImVec2(pos.x + 10.0f, pos.y + height), fill, 2.0f);
    drawList->AddRect(pos, ImVec2(pos.x + 10.0f, pos.y + height),
                      ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.5f)), 2.0f);
    ImGui::Dummy(ImVec2(10.0f, height));
}

/// <summary>
/// 大文字小文字を無視した部分一致（一覧の絞り込み用）
/// </summary>
bool ContainsIgnoreCase(const std::string &haystack, const char *needle)
{
    if (!needle || !needle[0])
        return true;
    std::string lowerHay = haystack;
    std::string lowerNeedle = needle;
    std::transform(lowerHay.begin(), lowerHay.end(), lowerHay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowerHay.find(lowerNeedle) != std::string::npos;
}
#endif // USE_IMGUI
} // namespace

void LightGroup::Finalize()
{
    // ギズモが持っているポインタはこの後 dangling になるので必ず先に外す
    UnregisterGizmoTargets();

    directionalLightResource_.Reset();
    pointLightsResource_.Reset();
    pPointLightBufferData_ = nullptr;
    pointLightUploadResource_.Reset();
    pointLightBufferResource_.Reset();
    pLightCounterUploadData_ = nullptr;
    lightCounterUploadResource_.Reset();
    lightCounterReadbackResource_.Reset();
    lightCounterResource_.Reset();
    spotLightsResource_.Reset();
    cameraForGPUResource_.Reset();
    pointLights_.clear();
    spotLights_.clear();
    dynamicPointLights_.clear();
    lightDataHandler_.reset();
}

void LightGroup::Initialize()
{
    pDxCommon_ = DirectXCommon::GetInstance();
    CreateCamera();
    CreatePointLights();
    CreatePointLightBuffer();
    CreateDirectionLight();
    CreateSpotLights();
}

void LightGroup::Update(const ViewProjection &viewProjection)
{
    pCameraForGPUData_->worldPosition = viewProjection.translation_;
    cameraPosition_ = viewProjection.translation_;

    pDirectionalLightData_->active = isDirectionalLight_;

#ifdef USE_IMGUI
    // ImGuizmoManager 側の登録が失われていたら（Finalize 後の再初期化など）登録し直す。
    // 先頭1件の有無だけ見れば足りる。
    // ※ かつては3Dオブジェクトの一括削除でも巻き添えで消えていたが、そちらは
    //    BaseObjectManager が自分の登録だけを外すようになったため起きなくなっている。
    if (!gizmoNames_.empty() && !ImGuizmoManager::GetInstance()->HasTarget(gizmoNames_.front()))
    {
        SyncGizmoTargets();
    }
#endif

    // ギズモで動かされた向きハンドルをスポットライトの向きへ反映する
    UpdateSpotAimPoints();

    // ポイントライトデータ更新
    UpdatePointLightBuffer();

    // スポットライトデータ更新
    UpdateSpotLightBuffer();

    DrawLightVisualization();
}

void LightGroup::Draw()
{
    // DirectionalLight用のCBufferの場所を設定
    // 通常描画・スキニング・G-Buffer で番号が違うのでレジスタ番号で引く
    const ShaderRootSignature *rootSignature = PipelineManager::GetInstance()->GetCurrentRootSignature();
    assert(rootSignature && "ライトを使うパイプラインのルートシグネチャが未生成です");

    pDxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(
        rootSignature->GetCbvIndex(1, D3D12_SHADER_VISIBILITY_PIXEL), directionalLightResource_->GetGPUVirtualAddress());

    pDxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(
        rootSignature->GetCbvIndex(2, D3D12_SHADER_VISIBILITY_PIXEL), cameraForGPUResource_->GetGPUVirtualAddress());

    pDxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(
        rootSignature->GetCbvIndex(3, D3D12_SHADER_VISIBILITY_PIXEL), pointLightsResource_->GetGPUVirtualAddress());

    pDxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(
        rootSignature->GetCbvIndex(4, D3D12_SHADER_VISIBILITY_PIXEL), spotLightsResource_->GetGPUVirtualAddress());
}

// ===================================================
// ライトの追加・削除・複製
// ===================================================

int LightGroup::AddPointLight()
{
    // ディファードのStructuredBufferに収まる範囲までは自由に置ける。
    // （前方描画では MAX_POINT_LIGHTS 個までしか反映されない点はUIで警告する）
    if (pointLights_.size() >= kMaxBufferedPointLights)
    {
        ImGuiNotification::Post("ポイントライトはこれ以上追加できません", {0.9f, 0.5f, 0.3f, 1.0f});
        return -1;
    }

    PointLightEntry entry;
    entry.gpu.color = {1.0f, 1.0f, 1.0f, 1.0f};
    entry.gpu.position = {-1.0f, 4.0f, -3.0f};
    entry.gpu.intensity = 1.0f;
    entry.gpu.decay = 1.0f;
    entry.gpu.radius = 5.0f;
    entry.gpu.active = true;
    entry.gpu.HalfLambert = false;
    entry.gpu.BlinnPhong = true;
    entry.name = MakeUniqueLightName("点光源1");

    pointLights_.push_back(entry);
    SyncGizmoTargets();
    ImGuiNotification::Post("ポイントライトを追加しました", {0.4f, 0.8f, 1.0f, 1.0f});
    return static_cast<int>(pointLights_.size()) - 1;
}

void LightGroup::RemovePointLight(int index)
{
    if (index < 0 || index >= static_cast<int>(pointLights_.size()))
        return;

    pointLights_.erase(pointLights_.begin() + index);
    SyncGizmoTargets();
    ImGuiNotification::Post("ポイントライトを削除しました", {0.9f, 0.7f, 0.2f, 1.0f});
}

int LightGroup::DuplicatePointLight(int index)
{
    if (index < 0 || index >= static_cast<int>(pointLights_.size()))
        return -1;
    if (pointLights_.size() >= kMaxBufferedPointLights)
        return -1;

    PointLightEntry copy = pointLights_[index];
    copy.name = MakeUniqueLightName(pointLights_[index].name);
    pointLights_.push_back(copy);
    SyncGizmoTargets();
    ImGuiNotification::Post("ポイントライトを複製しました", {0.4f, 0.8f, 1.0f, 1.0f});
    return static_cast<int>(pointLights_.size()) - 1;
}

int LightGroup::AddSpotLight()
{
    // スポットライトは定数バッファ経由なので上限あり
    if (spotLights_.size() >= MAX_SPOT_LIGHTS)
    {
        ImGuiNotification::Post("スポットライトはこれ以上追加できません", {0.9f, 0.5f, 0.3f, 1.0f});
        return -1;
    }

    SpotLightEntry entry;
    entry.gpu.color = {1.0f, 1.0f, 1.0f, 1.0f};
    entry.gpu.position = {0.0f, 8.0f, 0.0f};
    entry.gpu.direction = {0.0f, -1.0f, 0.0f};
    entry.gpu.intensity = 1.0f;
    entry.gpu.active = true;
    entry.gpu.distance = 10.0f;
    entry.gpu.decay = 1.0f;
    entry.gpu.cosAngle = 0.7f; // 約45度
    entry.gpu.HalfLambert = false;
    entry.gpu.BlinnPhong = true;
    entry.name = MakeUniqueLightName("スポット1");
    entry.aimPoint = entry.gpu.position + entry.gpu.direction * entry.gpu.distance;
    entry.prevAim = entry.aimPoint;

    spotLights_.push_back(entry);
    SyncGizmoTargets();
    ImGuiNotification::Post("スポットライトを追加しました", {0.4f, 0.8f, 1.0f, 1.0f});
    return static_cast<int>(spotLights_.size()) - 1;
}

void LightGroup::RemoveSpotLight(int index)
{
    if (index < 0 || index >= static_cast<int>(spotLights_.size()))
        return;

    spotLights_.erase(spotLights_.begin() + index);
    SyncGizmoTargets();
    ImGuiNotification::Post("スポットライトを削除しました", {0.9f, 0.7f, 0.2f, 1.0f});
}

int LightGroup::DuplicateSpotLight(int index)
{
    if (index < 0 || index >= static_cast<int>(spotLights_.size()))
        return -1;
    if (spotLights_.size() >= MAX_SPOT_LIGHTS)
        return -1;

    SpotLightEntry copy = spotLights_[index];
    copy.name = MakeUniqueLightName(spotLights_[index].name);
    spotLights_.push_back(copy);
    SyncGizmoTargets();
    ImGuiNotification::Post("スポットライトを複製しました", {0.4f, 0.8f, 1.0f, 1.0f});
    return static_cast<int>(spotLights_.size()) - 1;
}

std::string LightGroup::MakeUniqueLightName(const std::string &desired, int ignorePoint, int ignoreSpot) const
{
    // 末尾の連番を外した「素の名前」を求める（"点光源3" → "点光源"）
    std::string base = desired.empty() ? std::string("光源") : desired;
    size_t digitStart = base.size();
    while (digitStart > 0 && std::isdigit(static_cast<unsigned char>(base[digitStart - 1])))
    {
        --digitStart;
    }
    if (digitStart > 0 && digitStart < base.size())
    {
        base = base.substr(0, digitStart);
    }

    auto IsTaken = [&](const std::string &candidate) {
        for (int i = 0; i < static_cast<int>(pointLights_.size()); ++i)
        {
            if (i != ignorePoint && pointLights_[i].name == candidate)
                return true;
        }
        for (int i = 0; i < static_cast<int>(spotLights_.size()); ++i)
        {
            if (i != ignoreSpot && spotLights_[i].name == candidate)
                return true;
        }
        return false;
    };

    if (!IsTaken(desired) && !desired.empty())
    {
        return desired;
    }
    for (int suffix = 1; suffix < 100000; ++suffix)
    {
        std::string candidate = std::format("{}{}", base, suffix);
        if (!IsTaken(candidate))
        {
            return candidate;
        }
    }
    return base;
}

void LightGroup::UpdateSpotAimPoints()
{
    for (SpotLightEntry &entry : spotLights_)
    {
        // 前フレームから動いていればギズモで掴まれたと判断し、その向きを採用する。
        // （ImGuiで direction を直接いじった場合はハンドルが動いていないので上書きされない）
        const Vector3 handleDelta = entry.aimPoint - entry.prevAim;
        if (handleDelta.Length() > 1e-4f)
        {
            const Vector3 toAim = entry.aimPoint - entry.gpu.position;
            if (toAim.Length() > 1e-4f)
            {
                entry.gpu.direction = toAim.Normalize();
            }
        }

        // ハンドルは常に「照射距離の先」へ張り付かせる（本体を動かしても追従する）
        entry.aimPoint = entry.gpu.position + entry.gpu.direction * entry.gpu.distance;
        entry.prevAim = entry.aimPoint;
    }
}

// ===================================================
// 動的ポイントライト
// ===================================================

void LightGroup::AddDynamicPointLight(const DynamicPointLightDesc &desc)
{
    // 実質見えない光は積まない（優先度枠を無駄に消費させない）
    if (desc.intensity <= 0.0f || desc.radius <= 0.0f)
    {
        return;
    }
    dynamicPointLights_.push_back(desc);
}

void LightGroup::CommitPointLights()
{
    UpdatePointLightBuffer();
    UploadPointLightBuffer();
}

// ===================================================
// GPUバッファ
// ===================================================

void LightGroup::CreatePointLightBuffer()
{
    const size_t bufferSize = sizeof(PointLightGPU) * kMaxBufferedPointLights;

    // GPU側の実体。粒子光源CSがここへ追記するので DEFAULTヒープ＋UAV で作る
    pointLightBufferResource_ = pDxCommon_->CreateBufferResource(bufferSize, true);
    pointLightBufferState_ = D3D12_RESOURCE_STATE_COMMON;

    // CPU（手置き＋動的ライト）の書き込み先。毎フレーム上のバッファ先頭へコピーする
    pointLightUploadResource_ = pDxCommon_->CreateBufferResource(bufferSize);
    pointLightUploadResource_->Map(0, nullptr, reinterpret_cast<void **>(&pPointLightBufferData_));
    pointLightBufferCount_ = 0;

    // ライト総数カウンタ（先頭1要素）
    lightCounterResource_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t), true);
    lightCounterState_ = D3D12_RESOURCE_STATE_COMMON;
    lightCounterUploadResource_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t));
    lightCounterUploadResource_->Map(0, nullptr, reinterpret_cast<void **>(&pLightCounterUploadData_));
    *pLightCounterUploadData_ = 0;

    // 読み戻し用（統計表示。溢れているかを見るためだけなので遅延は問わない）
    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc{};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = sizeof(uint32_t);
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    pDxCommon_->GetDevice()->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&lightCounterReadbackResource_));
}

void LightGroup::TransitionPointLightBuffer(ID3D12GraphicsCommandList *pCommandList, D3D12_RESOURCE_STATES after)
{
    if (!pointLightBufferResource_ || pointLightBufferState_ == after)
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = pointLightBufferResource_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = pointLightBufferState_;
    barrier.Transition.StateAfter = after;
    pCommandList->ResourceBarrier(1, &barrier);
    pointLightBufferState_ = after;
}

void LightGroup::TransitionLightCounter(ID3D12GraphicsCommandList *pCommandList, D3D12_RESOURCE_STATES after)
{
    if (!lightCounterResource_ || lightCounterState_ == after)
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = lightCounterResource_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = lightCounterState_;
    barrier.Transition.StateAfter = after;
    pCommandList->ResourceBarrier(1, &barrier);
    lightCounterState_ = after;
}

void LightGroup::BeginGpuLightAppend(ID3D12GraphicsCommandList *pCommandList)
{
    if (!pCommandList || !pointLightBufferResource_ || !lightCounterResource_)
    {
        return;
    }

    // 前フレームのライト総数を取り込む（統計表示用。溢れの検出に使う）
    if (lightCounterReadbackResource_)
    {
        uint32_t *mapped = nullptr;
        D3D12_RANGE range{0, sizeof(uint32_t)};
        if (SUCCEEDED(lightCounterReadbackResource_->Map(0, &range, reinterpret_cast<void **>(&mapped))) && mapped)
        {
            gpuTotalLightCount_ = *mapped;
            D3D12_RANGE emptyRange{0, 0};
            lightCounterReadbackResource_->Unmap(0, &emptyRange);
        }
    }

    // CPU分をGPUバッファの先頭へ転送する。GPUはこの後ろへ追記する
    *pLightCounterUploadData_ = pointLightBufferCount_;

    TransitionPointLightBuffer(pCommandList, D3D12_RESOURCE_STATE_COPY_DEST);
    TransitionLightCounter(pCommandList, D3D12_RESOURCE_STATE_COPY_DEST);

    if (pointLightBufferCount_ > 0)
    {
        pCommandList->CopyBufferRegion(pointLightBufferResource_.Get(), 0, pointLightUploadResource_.Get(), 0,
                                  sizeof(PointLightGPU) * pointLightBufferCount_);
    }
    pCommandList->CopyBufferRegion(lightCounterResource_.Get(), 0, lightCounterUploadResource_.Get(), 0, sizeof(uint32_t));

    // 粒子光源CSが追記できる状態にする
    TransitionPointLightBuffer(pCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionLightCounter(pCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void LightGroup::EndGpuLightAppend(ID3D12GraphicsCommandList *pCommandList)
{
    if (!pCommandList || !pointLightBufferResource_ || !lightCounterResource_)
    {
        return;
    }

    // 統計用にライト総数を読み戻す（次フレームの Begin で取り込む）
    if (lightCounterReadbackResource_)
    {
        TransitionLightCounter(pCommandList, D3D12_RESOURCE_STATE_COPY_SOURCE);
        pCommandList->CopyBufferRegion(lightCounterReadbackResource_.Get(), 0, lightCounterResource_.Get(), 0, sizeof(uint32_t));
    }

    // カリングCS（非ピクセル）とライティングPS（ピクセル）の両方から読むので合成状態にする
    const D3D12_RESOURCE_STATES readState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    TransitionPointLightBuffer(pCommandList, readState);
    TransitionLightCounter(pCommandList, readState);
}

void LightGroup::UploadPointLightBuffer()
{
    if (!pPointLightBufferData_)
    {
        return;
    }

    uint32_t count = 0;

    // オーサリング済みライト（ディファードは個数制限を受けないので全部入れる）
    for (const PointLightEntry &entry : pointLights_)
    {
        if (count >= kMaxBufferedPointLights)
            break;
        if (!entry.gpu.active)
            continue;
        PointLightGPU &dst = pPointLightBufferData_[count++];
        dst.position = entry.gpu.position;
        dst.radius = entry.gpu.radius;
        dst.color = {entry.gpu.color.x, entry.gpu.color.y, entry.gpu.color.z};
        dst.intensity = entry.gpu.intensity;
        dst.decay = entry.gpu.decay;
        dst.flags = (entry.gpu.HalfLambert ? 1u : 0u) | (entry.gpu.BlinnPhong ? 2u : 0u);
    }

    // 動的ライト（CBと違い上限で切り捨てない。ディファードは全数処理できる）
    for (const DynamicPointLightDesc &src : dynamicPointLights_)
    {
        if (count >= kMaxBufferedPointLights)
            break;
        PointLightGPU &dst = pPointLightBufferData_[count++];
        dst.position = src.position;
        dst.radius = src.radius;
        dst.color = {src.color.x, src.color.y, src.color.z};
        dst.intensity = src.intensity;
        dst.decay = src.decay;
        dst.flags = 1u; // ハーフランバート（粒子まわりの陰影は柔らかいほうが自然）
    }

    pointLightBufferCount_ = count;

    // ディファードOFFのときはGPU追記パスが走らないので、読み戻し値が古いまま残る。
    // UIに嘘の粒子光源数を出さないようCPU分で埋めておく。
    if (!DeferredRenderer::GetInstance()->IsEnabled())
    {
        gpuTotalLightCount_ = count;
    }
}

D3D12_GPU_VIRTUAL_ADDRESS LightGroup::GetPointLightBufferAddress() const
{
    return pointLightBufferResource_ ? pointLightBufferResource_->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS LightGroup::GetPointLightUavAddress() const
{
    return pointLightBufferResource_ ? pointLightBufferResource_->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS LightGroup::GetLightCounterAddress() const
{
    return lightCounterResource_ ? lightCounterResource_->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS LightGroup::GetDirectionalLightAddress() const
{
    return directionalLightResource_ ? directionalLightResource_->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS LightGroup::GetSpotLightsAddress() const
{
    return spotLightsResource_ ? spotLightsResource_->GetGPUVirtualAddress() : 0;
}

void LightGroup::UpdatePointLightBuffer()
{
    // 画面への寄与の目安。「明るさ×届く範囲 ÷ カメラからの距離」
    auto Priority = [this](const Vector3 &position, float intensity, float radius) {
        const float distance = (position - cameraPosition_).Length();
        return (intensity * radius) / (distance + 1.0f);
    };

    int32_t count = 0;

    // ---- オーサリング済みライト ----
    // 定数バッファ（前方描画用）は MAX_POINT_LIGHTS 個までしか持てないので、
    // 溢れる場合は寄与の大きい順に採用する。ディファードON時はこの制限を受けない
    // （UploadPointLightBuffer 側で全数がStructuredBufferへ入る）。
    cbSortScratch_.clear();
    for (const PointLightEntry &entry : pointLights_)
    {
        if (entry.gpu.active)
        {
            cbSortScratch_.push_back(&entry.gpu);
        }
    }

    if (cbSortScratch_.size() > static_cast<size_t>(MAX_POINT_LIGHTS))
    {
        std::partial_sort(cbSortScratch_.begin(), cbSortScratch_.begin() + MAX_POINT_LIGHTS, cbSortScratch_.end(),
                          [&Priority](const PointLight *a, const PointLight *b) {
                              return Priority(a->position, a->intensity, a->radius) >
                                     Priority(b->position, b->intensity, b->radius);
                          });
    }

    const size_t takeAuthored = (std::min)(cbSortScratch_.size(), static_cast<size_t>(MAX_POINT_LIGHTS));
    for (size_t i = 0; i < takeAuthored; ++i)
    {
        pPointLightsData_->lights[count++] = *cbSortScratch_[i];
    }

    // ---- 動的ライト ----
    const int32_t freeSlots = MAX_POINT_LIGHTS - count;
    if (freeSlots <= 0 || dynamicPointLights_.empty())
    {
        pPointLightsData_->count = count;
        return;
    }

    if (static_cast<int32_t>(dynamicPointLights_.size()) > freeSlots)
    {
        std::partial_sort(dynamicPointLights_.begin(), dynamicPointLights_.begin() + freeSlots,
                          dynamicPointLights_.end(),
                          [&Priority](const DynamicPointLightDesc &a, const DynamicPointLightDesc &b) {
                              return Priority(a.position, a.intensity, a.radius) >
                                     Priority(b.position, b.intensity, b.radius);
                          });
    }

    const size_t take = (std::min)(static_cast<size_t>(freeSlots), dynamicPointLights_.size());
    for (size_t i = 0; i < take; ++i)
    {
        const DynamicPointLightDesc &src = dynamicPointLights_[i];
        PointLight &dst = pPointLightsData_->lights[count++];
        dst = {};
        dst.color = src.color;
        dst.position = src.position;
        dst.intensity = src.intensity;
        dst.radius = src.radius;
        dst.decay = src.decay;
        dst.active = 1;
        // 発光体まわりの陰影は柔らかいほうが自然なのでハーフランバートを使う
        // （BlinnPhongだと粒子の明滅がスペキュラでちらつきやすい）
        dst.HalfLambert = 1;
        dst.BlinnPhong = 0;
    }

    pPointLightsData_->count = count;
}

void LightGroup::UpdateSpotLightBuffer()
{
    int32_t count = 0;
    for (const SpotLightEntry &entry : spotLights_)
    {
        if (count >= MAX_SPOT_LIGHTS)
            break;
        pSpotLightsData_->lights[count++] = entry.gpu;
    }
    pSpotLightsData_->count = count;
}

void LightGroup::CreatePointLights()
{
    // サイズを明示的に計算
    size_t bufferSize = sizeof(PointLights);
    pointLightsResource_ = pDxCommon_->CreateBufferResource(bufferSize);
    pointLightsResource_->Map(0, nullptr, reinterpret_cast<void **>(&pPointLightsData_));

    for (int i = 0; i < MAX_POINT_LIGHTS; i++)
    {
        pPointLightsData_->lights[i].color = {1.0f, 1.0f, 1.0f, 1.0f};
        pPointLightsData_->lights[i].position = {-1.0f, 4.0f, -3.0f};
        pPointLightsData_->lights[i].intensity = 1.0f;
        pPointLightsData_->lights[i].decay = 1.0f;
        pPointLightsData_->lights[i].radius = 2.0f;
        pPointLightsData_->lights[i].active = false;
        pPointLightsData_->lights[i].HalfLambert = false;
        pPointLightsData_->lights[i].BlinnPhong = true;
    }

    pPointLightsData_->count = 0;
}

void LightGroup::CreateSpotLights()
{
    spotLightsResource_ = pDxCommon_->CreateBufferResource(sizeof(SpotLights));
    // 書き込むためのアドレスを取得
    spotLightsResource_->Map(0, nullptr, reinterpret_cast<void **>(&pSpotLightsData_));

    for (int i = 0; i < MAX_SPOT_LIGHTS; i++)
    {
        pSpotLightsData_->lights[i].color = {1.0f, 1.0f, 1.0f, 1.0f};
        pSpotLightsData_->lights[i].position = {0.0f, -4.0f, -3.0f};
        pSpotLightsData_->lights[i].direction = {0.0f, -1.0f, 0.0f};
        pSpotLightsData_->lights[i].intensity = 1.0f;
        pSpotLightsData_->lights[i].distance = 10.0f;
        pSpotLightsData_->lights[i].decay = 1.0f;
        pSpotLightsData_->lights[i].cosAngle = 3.0f;
        pSpotLightsData_->lights[i].active = false;
        pSpotLightsData_->lights[i].HalfLambert = false;
        pSpotLightsData_->lights[i].BlinnPhong = true;
    }

    pSpotLightsData_->count = 0;
}

void LightGroup::CreateDirectionLight()
{
    directionalLightResource_ = pDxCommon_->CreateBufferResource(sizeof(DirectionLight));
    // 書き込むためのアドレスを取得
    directionalLightResource_->Map(0, nullptr, reinterpret_cast<void **>(&pDirectionalLightData_));
    // デフォルト値
    pDirectionalLightData_->color = {1.0f, 1.0f, 1.0f, 1.0f};
    pDirectionalLightData_->direction = {0.0f, -1.0f, 0.0f};
    pDirectionalLightData_->intensity = 1.0f;
    pDirectionalLightData_->active = true;
    pDirectionalLightData_->HalfLambert = false;
    pDirectionalLightData_->BlinnPhong = true;
}

void LightGroup::CreateCamera()
{
    cameraForGPUResource_ = pDxCommon_->CreateBufferResource(sizeof(CameraForGPU));
    cameraForGPUResource_->Map(0, nullptr, reinterpret_cast<void **>(&pCameraForGPUData_));
    pCameraForGPUData_->worldPosition = {0.0f, 0.0f, -50.0f};
}

// ===================================================
// ギズモ連携
// ===================================================

std::string LightGroup::PointGizmoName(int index) const
{
    if (index < 0 || index >= static_cast<int>(pointLights_.size()))
        return {};
    return kGizmoPrefix + pointLights_[index].name;
}

std::string LightGroup::SpotGizmoName(int index) const
{
    if (index < 0 || index >= static_cast<int>(spotLights_.size()))
        return {};
    return kGizmoPrefix + spotLights_[index].name;
}

std::string LightGroup::SpotAimGizmoName(int index) const
{
    if (index < 0 || index >= static_cast<int>(spotLights_.size()))
        return {};
    return kGizmoPrefix + spotLights_[index].name + kGizmoAimSuffix;
}

void LightGroup::UnregisterGizmoTargets()
{
#ifdef USE_IMGUI
    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
    for (const std::string &name : gizmoNames_)
    {
        gizmo->RemoveTarget(name);
    }
#endif
    gizmoNames_.clear();
}

void LightGroup::SyncGizmoTargets()
{
#ifdef USE_IMGUI
    // std::vector の再確保でポインタが変わるため、登録は毎回作り直す
    UnregisterGizmoTargets();

    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();

    for (int i = 0; i < static_cast<int>(pointLights_.size()); ++i)
    {
        const std::string name = PointGizmoName(i);
        gizmo->AddTarget(name, &pointLights_[i].gpu.position, nullptr, nullptr, true,
                         [this, i]() { DrawGizmoInspector(false, i); });
        gizmo->SetCategory(name, GizmoCategory::Light);
        gizmoNames_.push_back(name);
    }

    for (int i = 0; i < static_cast<int>(spotLights_.size()); ++i)
    {
        const std::string name = SpotGizmoName(i);
        gizmo->AddTarget(name, &spotLights_[i].gpu.position, nullptr, nullptr, true,
                         [this, i]() { DrawGizmoInspector(true, i); });
        gizmo->SetCategory(name, GizmoCategory::Light);
        gizmoNames_.push_back(name);

        // 向きは回転ギズモではなく「照射先の点」を掴んで決める。
        // ImGuizmoManager は平行移動しか各ターゲットへ反映しないため、この形が確実。
        const std::string aimName = SpotAimGizmoName(i);
        gizmo->AddTarget(aimName, &spotLights_[i].aimPoint, nullptr, nullptr, true,
                         [this, i]() { DrawGizmoInspector(true, i); });
        gizmo->SetCategory(aimName, GizmoCategory::Light);
        gizmoNames_.push_back(aimName);
    }
#endif
}

void LightGroup::SyncSelectionToGizmo()
{
#ifdef USE_IMGUI
    if (!syncGizmoSelection_)
        return;

    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
    switch (selectedKind_)
    {
    case SelectionKind::Point:
        gizmo->SelectOnly(PointGizmoName(selectedIndex_));
        break;
    case SelectionKind::Spot:
        gizmo->SelectOnly(SpotGizmoName(selectedIndex_));
        break;
    default:
        // 平行光源・未選択のときはライトのギズモを外す（空文字は未登録なので選択解除になる）
        gizmo->SelectOnly("");
        break;
    }
#endif
}

// ===================================================
// ImGui
// ===================================================

void LightGroup::DrawImGui()
{
#ifdef USE_IMGUI
#ifdef USE_IMGUI
    // シーン上でライトのギズモを掴んだら、一覧の選択もそちらへ合わせる。
    // 毎フレーム全走査すると重いので「今の選択がまだ生きているか」を先に見て早期に抜ける。
    if (syncGizmoSelection_)
    {
        ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
        const std::string current = (selectedKind_ == SelectionKind::Point)  ? PointGizmoName(selectedIndex_)
                                    : (selectedKind_ == SelectionKind::Spot) ? SpotGizmoName(selectedIndex_)
                                                                             : std::string();
        const bool currentStillSelected =
            !current.empty() && (gizmo->IsSelected(current) ||
                                 (selectedKind_ == SelectionKind::Spot && gizmo->IsSelected(SpotAimGizmoName(selectedIndex_))));
        if (!currentStillSelected)
        {
            bool found = false;
            for (int i = 0; i < static_cast<int>(pointLights_.size()); ++i)
            {
                if (gizmo->IsSelected(PointGizmoName(i)))
                {
                    selectedKind_ = SelectionKind::Point;
                    selectedIndex_ = i;
                    found = true;
                    break;
                }
            }
            for (int i = 0; !found && i < static_cast<int>(spotLights_.size()); ++i)
            {
                if (gizmo->IsSelected(SpotGizmoName(i)) || gizmo->IsSelected(SpotAimGizmoName(i)))
                {
                    selectedKind_ = SelectionKind::Spot;
                    selectedIndex_ = i;
                    break;
                }
            }
        }
    }
#endif // USE_IMGUI

    DrawStatusHeader();

    ImGui::Spacing();
    SectionHeader("[ デバッグ描画 ]", DebugTheme::kAccentCyan);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);
    ImGui::Checkbox("光源を可視化##lightvis", &showLightVisualization_);
    ImGui::SetItemTooltip("光源の位置・向き・届く範囲を線で表示します");
    ImGui::SameLine();
    ImGui::BeginDisabled(!showLightVisualization_);
    ImGui::Checkbox("選択中だけ詳細##lightvisSel", &visualizeSelectedOnly_);
    ImGui::SetItemTooltip("選択していない光源は小さな十字だけにします。\n大量に配置したときの線描画の負荷対策です");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("ギズモと選択を同期##lightvisGizmo", &syncGizmoSelection_);
    ImGui::SetItemTooltip("一覧で選んだ光源をシーン上のギズモでも選択します。\nギズモ側で掴んだときは一覧の選択が追従します");
    ImGui::PopStyleColor();

#ifdef USE_IMGUI
    if (syncGizmoSelection_ && !ImGuizmoManager::GetInstance()->IsCategoryEnabled(GizmoCategory::Light))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
        ImGui::TextWrapped("※ ギズモの「操作対象フィルタ」で【ライト】がOFFのため、シーン上では掴めません");
        ImGui::PopStyleColor();
    }
#endif

    ImGui::Spacing();

    // 一覧とプロパティを左右に並べる。セーブ/ロードぶんの高さを残しておく
    const float paneHeight = (std::max)(240.0f, ImGui::GetContentRegionAvail().y - 130.0f);
    DrawLightListPanel(paneHeight);
    ImGui::SameLine();
    DrawPropertyPanel(paneHeight);

    ImGui::Spacing();
    DrawSaveLoadSection();
#endif // USE_IMGUI
}

void LightGroup::DrawStatusHeader()
{
#ifdef USE_IMGUI
    const bool deferred = DeferredRenderer::GetInstance()->IsEnabled();

    int activePoint = 0;
    for (const PointLightEntry &entry : pointLights_)
        activePoint += entry.gpu.active ? 1 : 0;
    int activeSpot = 0;
    for (const SpotLightEntry &entry : spotLights_)
        activeSpot += entry.gpu.active ? 1 : 0;

    StatusBadge(deferred ? "ディファード ON : 点光源の個数制限なし" : "前方描画 : 点光源は16個まで",
                deferred ? DebugTheme::kAccentGreen : DebugTheme::kAccentOrange);

    if (ImGui::BeginTable("##LightStats", 5, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
    {
        auto Cell = [](const char *label, const std::string &value, ImVec4 color) {
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(value.c_str());
            ImGui::PopStyleColor();
        };

        ImGui::TableNextRow();
        Cell("点光源", std::format("{} / {}", activePoint, pointLights_.size()), DebugTheme::kAccentYellow);
        Cell("スポット", std::format("{} / {}", activeSpot, MAX_SPOT_LIGHTS), DebugTheme::kAccentBlue);
        Cell("動的（粒子等）", std::format("{}", dynamicPointLights_.size()), DebugTheme::kAccentPurple);
        Cell("GPU転送数", std::format("{} / {}", pointLightBufferCount_, kMaxBufferedPointLights), DebugTheme::kAccentCyan);
        Cell("粒子光源(GPU生成)", std::format("{}", GetParticleLightCount()),
             gpuTotalLightCount_ > kMaxBufferedPointLights ? DebugTheme::kAccentOrange : DebugTheme::kAccentGreen);
        ImGui::EndTable();
    }

    if (gpuTotalLightCount_ > kMaxBufferedPointLights)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
        ImGui::TextWrapped("光源の合計が %u 個で上限 %u を超えています。溢れた粒子光源は捨てられます。\n"
                           "パーティクル側の「間引き」を大きくするか「光源の上限」を下げてください。",
                           gpuTotalLightCount_, kMaxBufferedPointLights);
        ImGui::PopStyleColor();
    }

    if (!deferred && activePoint > MAX_POINT_LIGHTS)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
        ImGui::TextWrapped("前方描画では点光源は %d 個までしか反映されません（明るさ×半径÷カメラ距離が大きい順に採用）。\n"
                           "「描画システム」でディファードをONにすると全部反映されます。",
                           MAX_POINT_LIGHTS);
        ImGui::PopStyleColor();
    }
#endif // USE_IMGUI
}

void LightGroup::DrawLightListPanel(float height)
{
#ifdef USE_IMGUI
    ImGui::BeginChild("##LightList", ImVec2(250.0f, height),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    SectionHeader("[ 光源一覧 ]", DebugTheme::kAccentPurple);

    // ---- 追加ボタン ----
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgGreen);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.68f, 0.52f, 0.40f));
    if (ImGui::Button("＋ 点光源", ImVec2(buttonWidth, 0.0f)))
    {
        const int added = AddPointLight();
        if (added >= 0)
        {
            selectedKind_ = SelectionKind::Point;
            selectedIndex_ = added;
            SyncSelectionToGizmo();
        }
    }
    ImGui::SetItemTooltip("周囲を等方向に照らす光源を追加します");
    ImGui::SameLine();
    ImGui::BeginDisabled(spotLights_.size() >= MAX_SPOT_LIGHTS);
    if (ImGui::Button("＋ スポット", ImVec2(buttonWidth, 0.0f)))
    {
        const int added = AddSpotLight();
        if (added >= 0)
        {
            selectedKind_ = SelectionKind::Spot;
            selectedIndex_ = added;
            SyncSelectionToGizmo();
        }
    }
    ImGui::SetItemTooltip("円錐状に照らす光源を追加します（上限 32個）");
    ImGui::EndDisabled();
    ImGui::PopStyleColor(2);

    // ---- 絞り込み ----
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##LightFilter", "名前で絞り込み...", listFilter_, sizeof(listFilter_));

    ImGui::Separator();

    // ---- 平行光源（常に先頭）----
    {
        const bool selected = (selectedKind_ == SelectionKind::Directional);
        ImGui::PushID("dirLight");
        bool active = isDirectionalLight_;
        if (ThemedToggle("##on", &active, DebugTheme::kAccentYellow))
        {
            isDirectionalLight_ = active;
        }
        ImGui::SameLine(0.0f, 4.0f);
        ColorSwatch(pDirectionalLightData_ ? pDirectionalLightData_->color : Vector4{1, 1, 1, 1});
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::AlignTextToFramePadding();
        if (ImGui::Selectable("平行光源", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetFrameHeight())))
        {
            selectedKind_ = SelectionKind::Directional;
            selectedIndex_ = -1;
            SyncSelectionToGizmo();
        }
        ImGui::PopID();
    }

    // 走査中に配列を増減させると参照が壊れるので、構造変化はループ後にまとめて行う
    int pendingRemovePoint = -1;
    int pendingRemoveSpot = -1;
    int pendingDuplicatePoint = -1;
    int pendingDuplicateSpot = -1;

    // ---- ポイントライト ----
    for (int i = 0; i < static_cast<int>(pointLights_.size()); ++i)
    {
        PointLightEntry &entry = pointLights_[i];
        if (!ContainsIgnoreCase(entry.name, listFilter_))
            continue;

        ImGui::PushID("point");
        ImGui::PushID(i);
        bool active = entry.gpu.active != 0;
        if (ThemedToggle("##on", &active, DebugTheme::kAccentYellow))
        {
            entry.gpu.active = active;
        }
        ImGui::SetItemTooltip("この光源の有効 / 無効");
        ImGui::SameLine(0.0f, 4.0f);
        ColorSwatch(entry.gpu.color);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::AlignTextToFramePadding();

        const bool selected = (selectedKind_ == SelectionKind::Point && selectedIndex_ == i);
        if (!active)
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        if (ImGui::Selectable(entry.name.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetFrameHeight())))
        {
            selectedKind_ = SelectionKind::Point;
            selectedIndex_ = i;
            SyncSelectionToGizmo();
        }
        if (!active)
            ImGui::PopStyleColor();

        // 右クリックメニュー（複製・削除）
        if (ImGui::BeginPopupContextItem("##ctx"))
        {
            if (ImGui::MenuItem("複製"))
            {
                pendingDuplicatePoint = i;
            }
            if (ImGui::MenuItem("削除"))
            {
                pendingRemovePoint = i;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::PopID();
    }

    // ---- スポットライト ----
    for (int i = 0; i < static_cast<int>(spotLights_.size()); ++i)
    {
        SpotLightEntry &entry = spotLights_[i];
        if (!ContainsIgnoreCase(entry.name, listFilter_))
            continue;

        ImGui::PushID("spot");
        ImGui::PushID(i);
        bool active = entry.gpu.active != 0;
        if (ThemedToggle("##on", &active, DebugTheme::kAccentYellow))
        {
            entry.gpu.active = active;
        }
        ImGui::SetItemTooltip("この光源の有効 / 無効");
        ImGui::SameLine(0.0f, 4.0f);
        ColorSwatch(entry.gpu.color);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::AlignTextToFramePadding();

        const bool selected = (selectedKind_ == SelectionKind::Spot && selectedIndex_ == i);
        if (!active)
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        if (ImGui::Selectable(entry.name.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetFrameHeight())))
        {
            selectedKind_ = SelectionKind::Spot;
            selectedIndex_ = i;
            SyncSelectionToGizmo();
        }
        if (!active)
            ImGui::PopStyleColor();

        if (ImGui::BeginPopupContextItem("##ctx"))
        {
            if (ImGui::MenuItem("複製"))
            {
                pendingDuplicateSpot = i;
            }
            if (ImGui::MenuItem("削除"))
            {
                pendingRemoveSpot = i;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::PopID();
    }

    // 走査が終わってから構造を変える
    if (pendingDuplicatePoint >= 0)
    {
        const int added = DuplicatePointLight(pendingDuplicatePoint);
        if (added >= 0)
        {
            selectedKind_ = SelectionKind::Point;
            selectedIndex_ = added;
            SyncSelectionToGizmo();
        }
    }
    if (pendingDuplicateSpot >= 0)
    {
        const int added = DuplicateSpotLight(pendingDuplicateSpot);
        if (added >= 0)
        {
            selectedKind_ = SelectionKind::Spot;
            selectedIndex_ = added;
            SyncSelectionToGizmo();
        }
    }
    if (pendingRemovePoint >= 0)
    {
        RemovePointLight(pendingRemovePoint);
        selectedKind_ = SelectionKind::Directional;
        selectedIndex_ = -1;
        SyncSelectionToGizmo();
    }
    if (pendingRemoveSpot >= 0)
    {
        RemoveSpotLight(pendingRemoveSpot);
        selectedKind_ = SelectionKind::Directional;
        selectedIndex_ = -1;
        SyncSelectionToGizmo();
    }

    if (pointLights_.empty() && spotLights_.empty())
    {
        ImGui::Spacing();
        ImGui::TextDisabled("光源がありません。上のボタンで追加できます");
    }

    ImGui::EndChild();
#else
    (void)height;
#endif // USE_IMGUI
}

void LightGroup::DrawPropertyPanel(float height)
{
#ifdef USE_IMGUI
    ImGui::BeginChild("##LightProps", ImVec2(0.0f, height), ImGuiChildFlags_Borders);

    switch (selectedKind_)
    {
    case SelectionKind::Directional:
        DrawDirectionalProperties();
        break;
    case SelectionKind::Point:
        DrawPointProperties(selectedIndex_);
        break;
    case SelectionKind::Spot:
        DrawSpotProperties(selectedIndex_);
        break;
    default:
        ImGui::TextDisabled("左の一覧から光源を選んでください");
        break;
    }

    ImGui::EndChild();
#else
    (void)height;
#endif // USE_IMGUI
}

void LightGroup::DrawDirectionalProperties()
{
#ifdef USE_IMGUI
    SectionHeader("[ 平行光源 ]", DebugTheme::kAccentBlue);

    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
    ImGui::Checkbox("有効にする##diren", &isDirectionalLight_);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("シーン全体を一方向から照らす光源です。太陽光にあたります");

    if (!isDirectionalLight_)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("無効になっています");
        return;
    }

    ImGui::Spacing();
    if (ImGui::BeginTable("##DirTable", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);

        LabeledRow("方向", "光の進む方向（自動で正規化されます）", [&] {
            if (ImGui::DragFloat3("##direction", &pDirectionalLightData_->direction.x, 0.01f, -1.0f, 1.0f, "%.2f"))
                pDirectionalLightData_->direction = pDirectionalLightData_->direction.Normalize();
        });
        LabeledRow("輝度", "光の明るさ", [&] {
            ThemedKnob("##intensity", &pDirectionalLightData_->intensity, 0.0f, 10.0f, "%.2f",
                       DebugTheme::kAccentYellow, 44.0f,
                       ImGuiKnobFlags_NoTitle | ImGuiKnobFlags_ValueTooltip);
        });
        LabeledRow("色", "光の色", [&] {
            ImGui::ColorEdit3("##color", &pDirectionalLightData_->color.x, ImGuiColorEditFlags_NoInputs);
        });
        LabeledRow("ライティング", "陰影計算モデル", [&] {
            const char *types[] = {"HalfLambert", "BlinnPhong"};
            int sel = pDirectionalLightData_->BlinnPhong ? 1 : 0;
            if (ImGui::Combo("##lightingType", &sel, types, IM_ARRAYSIZE(types)))
            {
                pDirectionalLightData_->HalfLambert = (sel == 0) ? 1 : 0;
                pDirectionalLightData_->BlinnPhong = (sel == 1) ? 1 : 0;
            }
        });

        ImGui::EndTable();
    }
#endif // USE_IMGUI
}

void LightGroup::DrawPointProperties(int index)
{
#ifdef USE_IMGUI
    if (index < 0 || index >= static_cast<int>(pointLights_.size()))
    {
        ImGui::TextDisabled("光源が選択されていません");
        return;
    }

    PointLightEntry &entry = pointLights_[index];

    SectionHeader("[ 点光源 ]", DebugTheme::kAccentYellow);

    // ---- 名前 ----
    // 編集中に毎フレーム上書きすると入力が消えるので、対象が変わったときだけ流し込む
    const std::string owner = std::format("P{}", index);
    if (nameEditOwner_ != owner)
    {
        nameEditBuffer_ = entry.name;
        nameEditOwner_ = owner;
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", &nameEditBuffer_, ImGuiInputTextFlags_EnterReturnsTrue))
    {
        entry.name = MakeUniqueLightName(nameEditBuffer_, index, -1);
        nameEditBuffer_ = entry.name;
        SyncGizmoTargets(); // ギズモの登録名も変わるので付け直す
        SyncSelectionToGizmo();
    }
    ImGui::SetItemTooltip("Enterで確定します。ギズモの一覧にもこの名前で出ます");

    ImGui::Spacing();
    if (ImGui::BeginTable("##PtTable", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);

        LabeledRow("有効", "この光源の有効 / 無効", [&] {
            bool active = entry.gpu.active != 0;
            if (ThemedToggle("##active", &active, DebugTheme::kAccentYellow))
                entry.gpu.active = active;
        });
        LabeledRow("位置", "シーン上のギズモでも動かせます", [&] {
            ImGui::DragFloat3("##position", &entry.gpu.position.x, 0.1f, 0.0f, 0.0f, "%.2f");
        });
        LabeledRow("色", "光の色", [&] {
            ImGui::ColorEdit3("##color", &entry.gpu.color.x, ImGuiColorEditFlags_NoInputs);
        });
        LabeledRow("輝度", "光の明るさ", [&] {
            ThemedKnob("##intensity", &entry.gpu.intensity, 0.0f, 10.0f, "%.2f",
                       DebugTheme::kAccentYellow, 44.0f,
                       ImGuiKnobFlags_NoTitle | ImGuiKnobFlags_ValueTooltip);
        });
        LabeledRow("届く距離", "この距離を超えると完全に減衰します", [&] {
            ImGui::DragFloat("##radius", &entry.gpu.radius, 0.1f, 0.1f, 500.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        });
        LabeledRow("減衰", "大きいほど光源のすぐ近くだけが明るくなります", [&] {
            ImGui::DragFloat("##decay", &entry.gpu.decay, 0.05f, 0.0f, 5.0f, "%.2f");
        });
        LabeledRow("ライティング", "陰影計算モデル", [&] {
            const char *types[] = {"HalfLambert", "BlinnPhong"};
            int sel = entry.gpu.BlinnPhong ? 1 : 0;
            if (ImGui::Combo("##lighting", &sel, types, IM_ARRAYSIZE(types)))
            {
                entry.gpu.HalfLambert = (sel == 0) ? 1 : 0;
                entry.gpu.BlinnPhong = (sel == 1) ? 1 : 0;
            }
        });

        ImGui::EndTable();
    }

    ImGui::Spacing();
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("複製", ImVec2(buttonWidth, 0.0f)))
    {
        const int added = DuplicatePointLight(index);
        if (added >= 0)
        {
            selectedKind_ = SelectionKind::Point;
            selectedIndex_ = added;
            SyncSelectionToGizmo();
        }
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgRed);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.46f, 0.46f, 0.40f));
    const bool remove = ImGui::Button("削除", ImVec2(buttonWidth, 0.0f));
    ImGui::PopStyleColor(2);
    if (remove)
    {
        RemovePointLight(index);
        selectedKind_ = SelectionKind::Directional;
        selectedIndex_ = -1;
        SyncSelectionToGizmo();
    }
#else
    (void)index;
#endif // USE_IMGUI
}

void LightGroup::DrawSpotProperties(int index)
{
#ifdef USE_IMGUI
    if (index < 0 || index >= static_cast<int>(spotLights_.size()))
    {
        ImGui::TextDisabled("光源が選択されていません");
        return;
    }

    SpotLightEntry &entry = spotLights_[index];

    SectionHeader("[ スポットライト ]", DebugTheme::kAccentBlue);

    const std::string owner = std::format("S{}", index);
    if (nameEditOwner_ != owner)
    {
        nameEditBuffer_ = entry.name;
        nameEditOwner_ = owner;
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", &nameEditBuffer_, ImGuiInputTextFlags_EnterReturnsTrue))
    {
        entry.name = MakeUniqueLightName(nameEditBuffer_, -1, index);
        nameEditBuffer_ = entry.name;
        SyncGizmoTargets();
        SyncSelectionToGizmo();
    }
    ImGui::SetItemTooltip("Enterで確定します。ギズモの一覧にもこの名前で出ます");

    ImGui::Spacing();
    ImGui::TextDisabled("向きはシーン上の「%s%s」ハンドルを動かしても変えられます", entry.name.c_str(), kGizmoAimSuffix);

    ImGui::Spacing();
    if (ImGui::BeginTable("##SpTable", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);

        LabeledRow("有効", "この光源の有効 / 無効", [&] {
            bool active = entry.gpu.active != 0;
            if (ThemedToggle("##active", &active, DebugTheme::kAccentYellow))
                entry.gpu.active = active;
        });
        LabeledRow("位置", "シーン上のギズモでも動かせます", [&] {
            ImGui::DragFloat3("##position", &entry.gpu.position.x, 0.1f, 0.0f, 0.0f, "%.2f");
        });
        LabeledRow("向き", "自動で正規化されます", [&] {
            if (ImGui::DragFloat3("##direction", &entry.gpu.direction.x, 0.01f, -1.0f, 1.0f, "%.2f"))
            {
                entry.gpu.direction = entry.gpu.direction.Normalize();
            }
        });
        LabeledRow("色", "光の色", [&] {
            ImGui::ColorEdit3("##color", &entry.gpu.color.x, ImGuiColorEditFlags_NoInputs);
        });
        LabeledRow("輝度", "光の明るさ", [&] {
            ThemedKnob("##intensity", &entry.gpu.intensity, 0.0f, 10.0f, "%.2f",
                       DebugTheme::kAccentYellow, 44.0f,
                       ImGuiKnobFlags_NoTitle | ImGuiKnobFlags_ValueTooltip);
        });
        LabeledRow("届く距離", "この距離を超えると完全に減衰します", [&] {
            ImGui::DragFloat("##distance", &entry.gpu.distance, 0.1f, 0.1f, 500.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        });
        LabeledRow("減衰", "大きいほど光源のすぐ近くだけが明るくなります", [&] {
            ImGui::DragFloat("##decay", &entry.gpu.decay, 0.05f, 0.0f, 5.0f, "%.2f");
        });
        LabeledRow("広がり", "円錐の半頂角。小さいほど絞られます", [&] {
            // 内部は cos で持っているが、角度のほうが直感的なので度数で見せる
            float degree = std::acos(std::clamp(entry.gpu.cosAngle, -1.0f, 1.0f)) * 180.0f / 3.14159265f;
            if (ImGui::SliderFloat("##coneAngle", &degree, 1.0f, 89.0f, "%.1f 度"))
            {
                entry.gpu.cosAngle = std::cos(degree * 3.14159265f / 180.0f);
            }
        });
        LabeledRow("ライティング", "陰影計算モデル", [&] {
            const char *types[] = {"HalfLambert", "BlinnPhong"};
            int sel = entry.gpu.BlinnPhong ? 1 : 0;
            if (ImGui::Combo("##lighting", &sel, types, IM_ARRAYSIZE(types)))
            {
                entry.gpu.HalfLambert = (sel == 0) ? 1 : 0;
                entry.gpu.BlinnPhong = (sel == 1) ? 1 : 0;
            }
        });

        ImGui::EndTable();
    }

    ImGui::Spacing();
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(spotLights_.size() >= MAX_SPOT_LIGHTS);
    if (ImGui::Button("複製", ImVec2(buttonWidth, 0.0f)))
    {
        const int added = DuplicateSpotLight(index);
        if (added >= 0)
        {
            selectedKind_ = SelectionKind::Spot;
            selectedIndex_ = added;
            SyncSelectionToGizmo();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgRed);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.46f, 0.46f, 0.40f));
    const bool remove = ImGui::Button("削除", ImVec2(buttonWidth, 0.0f));
    ImGui::PopStyleColor(2);
    if (remove)
    {
        RemoveSpotLight(index);
        selectedKind_ = SelectionKind::Directional;
        selectedIndex_ = -1;
        SyncSelectionToGizmo();
    }
#else
    (void)index;
#endif // USE_IMGUI
}

void LightGroup::DrawGizmoInspector(bool isSpot, int index)
{
#ifdef USE_IMGUI
    // ImGuizmoManager がターゲットを走査している最中に呼ばれるので、
    // ここではライトの増減・登録し直し（SyncGizmoTargets）を絶対に行わない。
    // 名前変更や複製・削除は「ライト設定」ウィンドウ側で行う。
    if (isSpot)
    {
        if (index < 0 || index >= static_cast<int>(spotLights_.size()))
            return;
        SpotLightEntry &entry = spotLights_[index];
        ImGui::TextDisabled("スポットライト : %s", entry.name.c_str());
        ImGui::DragFloat3("位置", &entry.gpu.position.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::DragFloat3("照射先", &entry.aimPoint.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::ColorEdit3("色", &entry.gpu.color.x, ImGuiColorEditFlags_NoInputs);
        ImGui::DragFloat("輝度", &entry.gpu.intensity, 0.05f, 0.0f, 10.0f, "%.2f");
        ImGui::DragFloat("届く距離", &entry.gpu.distance, 0.1f, 0.1f, 500.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    }
    else
    {
        if (index < 0 || index >= static_cast<int>(pointLights_.size()))
            return;
        PointLightEntry &entry = pointLights_[index];
        ImGui::TextDisabled("点光源 : %s", entry.name.c_str());
        ImGui::DragFloat3("位置", &entry.gpu.position.x, 0.1f, 0.0f, 0.0f, "%.2f");
        ImGui::ColorEdit3("色", &entry.gpu.color.x, ImGuiColorEditFlags_NoInputs);
        ImGui::DragFloat("輝度", &entry.gpu.intensity, 0.05f, 0.0f, 10.0f, "%.2f");
        ImGui::DragFloat("届く距離", &entry.gpu.radius, 0.1f, 0.1f, 500.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    }
    ImGui::TextDisabled("※ 詳細な設定は「ライト設定」ウィンドウで行えます");
#else
    (void)isSpot;
    (void)index;
#endif // USE_IMGUI
}

void LightGroup::DrawSaveLoadSection()
{
#ifdef USE_IMGUI
    SectionHeader("[ セーブ / ロード ]", DebugTheme::kAccentPurple);

    static char saveFileName[256] = "DefaultLightSetting";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##lightfile", "ファイル名", saveFileName, sizeof(saveFileName));
    ImGui::Spacing();

    // 保存・読込（通知は SaveLightData / LoadLightData 側で投稿する）
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.58f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.52f, 0.70f, 0.95f));
    if (ImGui::Button("セーブ", ImVec2(buttonWidth, 0.0f)))
    {
        SaveLightData(std::string(saveFileName));
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.40f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.60f, 0.50f, 0.95f));
    if (ImGui::Button("ロード", ImVec2(buttonWidth, 0.0f)))
    {
        LoadLightData(std::string(saveFileName));
    }
    ImGui::PopStyleColor(2);
#endif // USE_IMGUI
}

// ===================================================
// セーブ / ロード
// ===================================================

void LightGroup::SaveLightData(const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>("LightGroup", fileName);

    // Directional Light
    dataHandler->Save<bool>("directional_active", isDirectionalLight_);
    dataHandler->Save<Vector3>("directional_direction", pDirectionalLightData_->direction);
    dataHandler->Save<float>("directional_intensity", pDirectionalLightData_->intensity);
    dataHandler->Save<Vector4>("directional_color", pDirectionalLightData_->color);
    dataHandler->Save<int32_t>("directional_HalfLambert", pDirectionalLightData_->HalfLambert);
    dataHandler->Save<int32_t>("directional_BlinnPhong", pDirectionalLightData_->BlinnPhong);

    // Point Lights
    dataHandler->Save<int32_t>("pointLight_count", static_cast<int32_t>(pointLights_.size()));
    for (size_t i = 0; i < pointLights_.size(); ++i)
    {
        const PointLightEntry &entry = pointLights_[i];
        std::string prefix = std::format("pointLight_{:02d}_", i);
        dataHandler->Save<std::string>(prefix + "name", entry.name);
        dataHandler->Save<bool>(prefix + "active", entry.gpu.active != 0);
        dataHandler->Save<Vector4>(prefix + "color", entry.gpu.color);
        dataHandler->Save<Vector3>(prefix + "position", entry.gpu.position);
        dataHandler->Save<int32_t>(prefix + "HalfLambert", entry.gpu.HalfLambert);
        dataHandler->Save<int32_t>(prefix + "BlinnPhong", entry.gpu.BlinnPhong);
        dataHandler->Save<float>(prefix + "intensity", entry.gpu.intensity);
        dataHandler->Save<float>(prefix + "radius", entry.gpu.radius);
        dataHandler->Save<float>(prefix + "decay", entry.gpu.decay);
    }

    // Spot Lights
    dataHandler->Save<int32_t>("spotLight_count", static_cast<int32_t>(spotLights_.size()));
    for (size_t i = 0; i < spotLights_.size(); ++i)
    {
        const SpotLightEntry &entry = spotLights_[i];
        std::string prefix = std::format("spotLight_{:02d}_", i);
        dataHandler->Save<std::string>(prefix + "name", entry.name);
        dataHandler->Save<bool>(prefix + "active", entry.gpu.active != 0);
        dataHandler->Save<Vector4>(prefix + "color", entry.gpu.color);
        dataHandler->Save<Vector3>(prefix + "position", entry.gpu.position);
        dataHandler->Save<Vector3>(prefix + "direction", entry.gpu.direction);
        dataHandler->Save<int32_t>(prefix + "HalfLambert", entry.gpu.HalfLambert);
        dataHandler->Save<int32_t>(prefix + "BlinnPhong", entry.gpu.BlinnPhong);
        dataHandler->Save<float>(prefix + "intensity", entry.gpu.intensity);
        dataHandler->Save<float>(prefix + "distance", entry.gpu.distance);
        dataHandler->Save<float>(prefix + "cosAngle", entry.gpu.cosAngle);
        dataHandler->Save<float>(prefix + "decay", entry.gpu.decay);
    }
    dataHandler->Flush();
    ImGuiNotification::Post("ライトデータを保存しました: " + fileName, {0.2f, 0.8f, 0.2f, 1.0f});
}

void LightGroup::LoadLightData(const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>("LightGroup", fileName);

    // Directional Light
    isDirectionalLight_ = dataHandler->Load<bool>("directional_active", true);
    pDirectionalLightData_->color = dataHandler->Load<Vector4>("directional_color", {1.0f, 1.0f, 1.0f, 1.0f});
    pDirectionalLightData_->direction = dataHandler->Load<Vector3>("directional_direction", {0.0f, -1.0f, 0.0f});
    pDirectionalLightData_->HalfLambert = dataHandler->Load<int32_t>("directional_HalfLambert", false);
    pDirectionalLightData_->BlinnPhong = dataHandler->Load<int32_t>("directional_BlinnPhong", true);
    pDirectionalLightData_->intensity = dataHandler->Load<float>("directional_intensity", 1.0f);

    // Point Lights
    pointLights_.clear();
    int32_t pointLightCount = dataHandler->Load<int32_t>("pointLight_count", 0);
    for (int32_t i = 0; i < pointLightCount && i < static_cast<int32_t>(kMaxBufferedPointLights); ++i)
    {
        std::string prefix = std::format("pointLight_{:02d}_", i);
        PointLightEntry entry;
        entry.gpu.active = dataHandler->Load<bool>(prefix + "active", true);
        entry.gpu.color = dataHandler->Load<Vector4>(prefix + "color", {1.0f, 1.0f, 1.0f, 1.0f});
        entry.gpu.position = dataHandler->Load<Vector3>(prefix + "position", {0.0f, 2.0f, 0.0f});
        entry.gpu.HalfLambert = dataHandler->Load<int32_t>(prefix + "HalfLambert", false);
        entry.gpu.BlinnPhong = dataHandler->Load<int32_t>(prefix + "BlinnPhong", true);
        entry.gpu.intensity = dataHandler->Load<float>(prefix + "intensity", 1.0f);
        entry.gpu.radius = dataHandler->Load<float>(prefix + "radius", 5.0f);
        entry.gpu.decay = dataHandler->Load<float>(prefix + "decay", 1.0f);
        // 名前は後から追加した項目なので、無い場合は連番で補う
        entry.name = dataHandler->Load<std::string>(prefix + "name", std::format("点光源{}", i + 1));
        pointLights_.push_back(entry);
        pointLights_.back().name = MakeUniqueLightName(pointLights_.back().name, static_cast<int>(pointLights_.size()) - 1, -1);
    }

    // Spot Lights
    spotLights_.clear();
    int32_t spotLightCount = dataHandler->Load<int32_t>("spotLight_count", 0);
    for (int32_t i = 0; i < spotLightCount && i < MAX_SPOT_LIGHTS; ++i)
    {
        std::string prefix = std::format("spotLight_{:02d}_", i);
        SpotLightEntry entry;
        entry.gpu.active = dataHandler->Load<bool>(prefix + "active", true);
        entry.gpu.color = dataHandler->Load<Vector4>(prefix + "color", {1.0f, 1.0f, 1.0f, 1.0f});
        entry.gpu.position = dataHandler->Load<Vector3>(prefix + "position", {0.0f, 2.0f, 0.0f});
        entry.gpu.direction = dataHandler->Load<Vector3>(prefix + "direction", {0.0f, -1.0f, 0.0f});
        entry.gpu.HalfLambert = dataHandler->Load<int32_t>(prefix + "HalfLambert", false);
        entry.gpu.BlinnPhong = dataHandler->Load<int32_t>(prefix + "BlinnPhong", true);
        entry.gpu.intensity = dataHandler->Load<float>(prefix + "intensity", 1.0f);
        entry.gpu.distance = dataHandler->Load<float>(prefix + "distance", 10.0f);
        entry.gpu.cosAngle = dataHandler->Load<float>(prefix + "cosAngle", 0.7f);
        entry.gpu.decay = dataHandler->Load<float>(prefix + "decay", 1.0f);
        entry.name = dataHandler->Load<std::string>(prefix + "name", std::format("スポット{}", i + 1));
        entry.aimPoint = entry.gpu.position + entry.gpu.direction * entry.gpu.distance;
        entry.prevAim = entry.aimPoint;
        spotLights_.push_back(entry);
        spotLights_.back().name = MakeUniqueLightName(spotLights_.back().name, -1, static_cast<int>(spotLights_.size()) - 1);
    }

    // 一覧の選択とギズモ登録を作り直す
    selectedKind_ = SelectionKind::Directional;
    selectedIndex_ = -1;
    SyncGizmoTargets();
    SyncSelectionToGizmo();

    ImGuiNotification::Post("ライトデータを読み込みました: " + fileName, {0.2f, 0.8f, 0.8f, 1.0f});
}

// ===================================================
// デバッグ描画
// ===================================================

void LightGroup::DrawLightVisualization()
{
    if (!showLightVisualization_)
        return;

    LineRenderer *drawLine = LineRenderer::GetInstance();

    // 位置が分かる最小限のマーカー（3軸の十字）
    auto DrawMarker = [drawLine](const Vector3 &position, const Vector4 &color, float size) {
        drawLine->AddLine({position.x - size, position.y, position.z}, {position.x + size, position.y, position.z}, color);
        drawLine->AddLine({position.x, position.y - size, position.z}, {position.x, position.y + size, position.z}, color);
        drawLine->AddLine({position.x, position.y, position.z - size}, {position.x, position.y, position.z + size}, color);
    };

    // ---- 平行光源 ----
    const bool dirSelected = (selectedKind_ == SelectionKind::Directional);
    if (isDirectionalLight_ && pDirectionalLightData_->active && (!visualizeSelectedOnly_ || dirSelected))
    {
        Vector4 dirColor = {pDirectionalLightData_->color.x, pDirectionalLightData_->color.y, pDirectionalLightData_->color.z, 0.8f};

        // 複数の平行線で方向を表示
        for (int i = -2; i <= 2; i++)
        {
            for (int j = -2; j <= 2; j++)
            {
                Vector3 startPos = {i * 5.0f, 20.0f, j * 5.0f};
                Vector3 endPos = startPos + pDirectionalLightData_->direction * 15.0f;
                drawLine->AddLine(startPos, endPos, dirColor);
            }
        }
    }

    // ---- ポイントライト ----
    for (int i = 0; i < static_cast<int>(pointLights_.size()); ++i)
    {
        const PointLightEntry &entry = pointLights_[i];
        if (!entry.gpu.active)
            continue;

        const Vector4 lightColor = {entry.gpu.color.x, entry.gpu.color.y, entry.gpu.color.z, 0.8f};
        const bool isSelected = (selectedKind_ == SelectionKind::Point && selectedIndex_ == i);

        if (visualizeSelectedOnly_ && !isSelected)
        {
            // 選択外は位置が分かるだけの十字にとどめる（大量配置でも線が破綻しないように）
            DrawMarker(entry.gpu.position, lightColor, 0.5f);
            continue;
        }

        // ライト位置に球体を描画
        drawLine->AddSphere(entry.gpu.position, 0.3f, lightColor, 8);
        // 光の届く範囲
        drawLine->AddSphere(entry.gpu.position, entry.gpu.radius, {lightColor.x, lightColor.y, lightColor.z, 0.3f}, 16);
    }

    // ---- スポットライト ----
    for (int i = 0; i < static_cast<int>(spotLights_.size()); ++i)
    {
        const SpotLightEntry &entry = spotLights_[i];
        if (!entry.gpu.active)
            continue;

        const SpotLight &light = entry.gpu;
        const Vector4 lightColor = {light.color.x, light.color.y, light.color.z, 0.8f};
        const bool isSelected = (selectedKind_ == SelectionKind::Spot && selectedIndex_ == i);

        if (visualizeSelectedOnly_ && !isSelected)
        {
            DrawMarker(light.position, lightColor, 0.5f);
            continue;
        }

        // ライト位置に小さな球体
        drawLine->AddSphere(light.position, 0.3f, lightColor, 8);

        // 向きハンドル（ギズモで掴む点）まで線を引き、掴める場所を分かるようにする
        drawLine->AddLine(light.position, entry.aimPoint, lightColor);
        drawLine->AddSphere(entry.aimPoint, 0.35f, {lightColor.x, lightColor.y, lightColor.z, 0.9f}, 8);

        // コーン形状
        const Vector3 centerRay = light.position + light.direction * light.distance;
        const float coneAngle = std::acos(std::clamp(light.cosAngle, -1.0f, 1.0f));
        const float coneRadius = light.distance * std::tan(coneAngle);

        Vector3 right;
        if (std::abs(light.direction.y) < 0.9f)
        {
            right = Vector3(0, 1, 0).Cross(light.direction).Normalize();
        }
        else
        {
            right = Vector3(1, 0, 0).Cross(light.direction).Normalize();
        }
        const Vector3 up = light.direction.Cross(right).Normalize();

        constexpr int kConeEdges = 8;
        for (int edge = 0; edge < kConeEdges; edge++)
        {
            const float angle = static_cast<float>(edge) * (3.14159265f * 2.0f) / kConeEdges;
            const Vector3 coneOffset = (right * std::cos(angle) + up * std::sin(angle)) * coneRadius;
            const Vector3 coneEnd = centerRay + coneOffset;

            drawLine->AddLine(light.position, coneEnd, {lightColor.x, lightColor.y, lightColor.z, 0.6f});

            const int nextEdge = (edge + 1) % kConeEdges;
            const float nextAngle = static_cast<float>(nextEdge) * (3.14159265f * 2.0f) / kConeEdges;
            const Vector3 nextConeOffset = (right * std::cos(nextAngle) + up * std::sin(nextAngle)) * coneRadius;
            drawLine->AddLine(coneEnd, centerRay + nextConeOffset, {lightColor.x, lightColor.y, lightColor.z, 0.4f});
        }
    }
}
} // namespace Hagine
