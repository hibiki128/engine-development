#include "PointLightGroup.h"
#include "DirectXCommon.h"
#include <algorithm>
#include <data/DataHandler.h>
#include <format>
#include <line/LineRenderer.h>
#include <render/deferred/DeferredRenderer.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#ifdef USE_IMGUI
#include "LightUIHelper.h"
#include <imgui_stdlib.h>
#endif

namespace Hagine {

void PointLightGroup::Initialize(DirectXCommon *pDxCommon)
{
    pDxCommon_ = pDxCommon;
    CreateConstantBuffer();
    CreateStructuredBuffer();
}

void PointLightGroup::Finalize()
{
    constantResource_.Reset();
    pConstantData_ = nullptr;
    pBufferData_ = nullptr;
    uploadResource_.Reset();
    bufferResource_.Reset();
    pCounterUploadData_ = nullptr;
    counterUploadResource_.Reset();
    counterReadbackResource_.Reset();
    counterResource_.Reset();
    entries_.clear();
    dynamicLights_.clear();
    sortScratch_.clear();
    pDxCommon_ = nullptr;
}

// ===================================================
//  バッファ生成
// ===================================================

void PointLightGroup::CreateConstantBuffer()
{
    constantResource_ = pDxCommon_->CreateBufferResource(sizeof(PointLightsCB));
    constantResource_->Map(0, nullptr, reinterpret_cast<void **>(&pConstantData_));

    for (int i = 0; i < MAX_POINT_LIGHTS; i++)
    {
        pConstantData_->lights[i].color = {1.0f, 1.0f, 1.0f, 1.0f};
        pConstantData_->lights[i].position = {-1.0f, 4.0f, -3.0f};
        pConstantData_->lights[i].intensity = 1.0f;
        pConstantData_->lights[i].decay = 1.0f;
        pConstantData_->lights[i].radius = 2.0f;
        pConstantData_->lights[i].active = false;
        pConstantData_->lights[i].HalfLambert = false;
        pConstantData_->lights[i].BlinnPhong = true;
    }

    pConstantData_->count = 0;
}

void PointLightGroup::CreateStructuredBuffer()
{
    const size_t bufferSize = sizeof(PointLightGPU) * kMaxBufferedLights;

    // GPU側の実体。粒子光源CSがここへ追記するので DEFAULTヒープ＋UAV で作る
    bufferResource_ = pDxCommon_->CreateBufferResource(bufferSize, true);
    bufferState_ = D3D12_RESOURCE_STATE_COMMON;

    // CPU（手置き＋動的ライト）の書き込み先。毎フレーム上のバッファ先頭へコピーする
    uploadResource_ = pDxCommon_->CreateBufferResource(bufferSize);
    uploadResource_->Map(0, nullptr, reinterpret_cast<void **>(&pBufferData_));
    bufferCount_ = 0;

    // ライト総数カウンタ（先頭1要素）
    counterResource_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t), true);
    counterState_ = D3D12_RESOURCE_STATE_COMMON;
    counterUploadResource_ = pDxCommon_->CreateBufferResource(sizeof(uint32_t));
    counterUploadResource_->Map(0, nullptr, reinterpret_cast<void **>(&pCounterUploadData_));
    *pCounterUploadData_ = 0;

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
                                                     IID_PPV_ARGS(&counterReadbackResource_));
}

// ===================================================
//  追加・削除
// ===================================================

int PointLightGroup::Add(const std::string &name)
{
    // ディファードのStructuredBufferに収まる範囲までは自由に置ける。
    // （前方描画では MAX_POINT_LIGHTS 個までしか反映されない点はUIで警告する）
    if (!CanAdd())
    {
        ImGuiNotification::Post("ポイントライトはこれ以上追加できません", {0.9f, 0.5f, 0.3f, 1.0f});
        return -1;
    }

    Entry entry;
    entry.gpu.color = {1.0f, 1.0f, 1.0f, 1.0f};
    entry.gpu.position = {-1.0f, 4.0f, -3.0f};
    entry.gpu.intensity = 1.0f;
    entry.gpu.decay = 1.0f;
    entry.gpu.radius = 5.0f;
    entry.gpu.active = true;
    entry.gpu.HalfLambert = false;
    entry.gpu.BlinnPhong = true;
    entry.name = name;

    entries_.push_back(entry);
    ImGuiNotification::Post("ポイントライトを追加しました", {0.4f, 0.8f, 1.0f, 1.0f});
    return static_cast<int>(entries_.size()) - 1;
}

bool PointLightGroup::Remove(int index)
{
    if (!IsValidIndex(index))
        return false;

    entries_.erase(entries_.begin() + index);
    ImGuiNotification::Post("ポイントライトを削除しました", {0.9f, 0.7f, 0.2f, 1.0f});
    return true;
}

int PointLightGroup::Duplicate(int index, const std::string &name)
{
    if (!IsValidIndex(index) || !CanAdd())
        return -1;

    Entry copy = entries_[index];
    copy.name = name;
    entries_.push_back(copy);
    ImGuiNotification::Post("ポイントライトを複製しました", {0.4f, 0.8f, 1.0f, 1.0f});
    return static_cast<int>(entries_.size()) - 1;
}

int PointLightGroup::GetActiveCount() const
{
    int count = 0;
    for (const Entry &entry : entries_)
    {
        count += entry.gpu.active ? 1 : 0;
    }
    return count;
}

// ===================================================
//  動的ライト
// ===================================================

void PointLightGroup::AddDynamic(const DynamicPointLightDesc &desc)
{
    // 実質見えない光は積まない（優先度枠を無駄に消費させない）
    if (desc.intensity <= 0.0f || desc.radius <= 0.0f)
    {
        return;
    }
    dynamicLights_.push_back(desc);
}

// ===================================================
//  GPU転送
// ===================================================

void PointLightGroup::UpdateConstantBuffer(const Vector3 &cameraPosition)
{
    if (!pConstantData_)
    {
        return;
    }

    // 画面への寄与の目安。「明るさ×届く範囲 ÷ カメラからの距離」
    auto Priority = [&cameraPosition](const Vector3 &position, float intensity, float radius) {
        const float distance = (position - cameraPosition).Length();
        return (intensity * radius) / (distance + 1.0f);
    };

    int32_t count = 0;

    // ---- 手で置いたライト ----
    // 定数バッファ（前方描画用）は MAX_POINT_LIGHTS 個までしか持てないので、
    // 溢れる場合は寄与の大きい順に採用する。ディファードON時はこの制限を受けない
    // （UploadStructuredBuffer 側で全数がStructuredBufferへ入る）。
    sortScratch_.clear();
    for (const Entry &entry : entries_)
    {
        if (entry.gpu.active)
        {
            sortScratch_.push_back(&entry.gpu);
        }
    }

    if (sortScratch_.size() > static_cast<size_t>(MAX_POINT_LIGHTS))
    {
        std::partial_sort(sortScratch_.begin(), sortScratch_.begin() + MAX_POINT_LIGHTS, sortScratch_.end(),
                          [&Priority](const PointLightData *a, const PointLightData *b) {
                              return Priority(a->position, a->intensity, a->radius) >
                                     Priority(b->position, b->intensity, b->radius);
                          });
    }

    const size_t takeAuthored = (std::min)(sortScratch_.size(), static_cast<size_t>(MAX_POINT_LIGHTS));
    for (size_t i = 0; i < takeAuthored; ++i)
    {
        pConstantData_->lights[count++] = *sortScratch_[i];
    }

    // ---- 動的ライト ----
    const int32_t freeSlots = MAX_POINT_LIGHTS - count;
    if (freeSlots <= 0 || dynamicLights_.empty())
    {
        pConstantData_->count = count;
        return;
    }

    if (static_cast<int32_t>(dynamicLights_.size()) > freeSlots)
    {
        std::partial_sort(dynamicLights_.begin(), dynamicLights_.begin() + freeSlots, dynamicLights_.end(),
                          [&Priority](const DynamicPointLightDesc &a, const DynamicPointLightDesc &b) {
                              return Priority(a.position, a.intensity, a.radius) >
                                     Priority(b.position, b.intensity, b.radius);
                          });
    }

    const size_t take = (std::min)(static_cast<size_t>(freeSlots), dynamicLights_.size());
    for (size_t i = 0; i < take; ++i)
    {
        const DynamicPointLightDesc &src = dynamicLights_[i];
        PointLightData &dst = pConstantData_->lights[count++];
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

    pConstantData_->count = count;
}

void PointLightGroup::UploadStructuredBuffer()
{
    if (!pBufferData_)
    {
        return;
    }

    uint32_t count = 0;

    // 手で置いたライト（ディファードは個数制限を受けないので全部入れる）
    for (const Entry &entry : entries_)
    {
        if (count >= kMaxBufferedLights)
            break;
        if (!entry.gpu.active)
            continue;
        PointLightGPU &dst = pBufferData_[count++];
        dst.position = entry.gpu.position;
        dst.radius = entry.gpu.radius;
        dst.color = {entry.gpu.color.x, entry.gpu.color.y, entry.gpu.color.z};
        dst.intensity = entry.gpu.intensity;
        dst.decay = entry.gpu.decay;
        dst.flags = (entry.gpu.HalfLambert ? 1u : 0u) | (entry.gpu.BlinnPhong ? 2u : 0u);
    }

    // 動的ライト（CBと違い上限で切り捨てない。ディファードは全数処理できる）
    for (const DynamicPointLightDesc &src : dynamicLights_)
    {
        if (count >= kMaxBufferedLights)
            break;
        PointLightGPU &dst = pBufferData_[count++];
        dst.position = src.position;
        dst.radius = src.radius;
        dst.color = {src.color.x, src.color.y, src.color.z};
        dst.intensity = src.intensity;
        dst.decay = src.decay;
        dst.flags = 1u; // ハーフランバート（粒子まわりの陰影は柔らかいほうが自然）
    }

    bufferCount_ = count;

    // ディファードOFFのときはGPU追記パスが走らないので、読み戻し値が古いまま残る。
    // UIに嘘の粒子光源数を出さないようCPU分で埋めておく。
    if (!DeferredRenderer::GetInstance()->IsEnabled())
    {
        gpuTotalCount_ = count;
    }
}

void PointLightGroup::TransitionBuffer(ID3D12GraphicsCommandList *pCommandList, D3D12_RESOURCE_STATES after)
{
    if (!bufferResource_ || bufferState_ == after)
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = bufferResource_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = bufferState_;
    barrier.Transition.StateAfter = after;
    pCommandList->ResourceBarrier(1, &barrier);
    bufferState_ = after;
}

void PointLightGroup::TransitionCounter(ID3D12GraphicsCommandList *pCommandList, D3D12_RESOURCE_STATES after)
{
    if (!counterResource_ || counterState_ == after)
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = counterResource_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = counterState_;
    barrier.Transition.StateAfter = after;
    pCommandList->ResourceBarrier(1, &barrier);
    counterState_ = after;
}

void PointLightGroup::BeginGpuAppend(ID3D12GraphicsCommandList *pCommandList)
{
    if (!pCommandList || !bufferResource_ || !counterResource_)
    {
        return;
    }

    // 前フレームのライト総数を取り込む（統計表示用。溢れの検出に使う）
    if (counterReadbackResource_)
    {
        uint32_t *mapped = nullptr;
        D3D12_RANGE range{0, sizeof(uint32_t)};
        if (SUCCEEDED(counterReadbackResource_->Map(0, &range, reinterpret_cast<void **>(&mapped))) && mapped)
        {
            gpuTotalCount_ = *mapped;
            D3D12_RANGE emptyRange{0, 0};
            counterReadbackResource_->Unmap(0, &emptyRange);
        }
    }

    // CPU分をGPUバッファの先頭へ転送する。GPUはこの後ろへ追記する
    *pCounterUploadData_ = bufferCount_;

    TransitionBuffer(pCommandList, D3D12_RESOURCE_STATE_COPY_DEST);
    TransitionCounter(pCommandList, D3D12_RESOURCE_STATE_COPY_DEST);

    if (bufferCount_ > 0)
    {
        pCommandList->CopyBufferRegion(bufferResource_.Get(), 0, uploadResource_.Get(), 0,
                                       sizeof(PointLightGPU) * bufferCount_);
    }
    pCommandList->CopyBufferRegion(counterResource_.Get(), 0, counterUploadResource_.Get(), 0, sizeof(uint32_t));

    // 粒子光源CSが追記できる状態にする
    TransitionBuffer(pCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionCounter(pCommandList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void PointLightGroup::EndGpuAppend(ID3D12GraphicsCommandList *pCommandList)
{
    if (!pCommandList || !bufferResource_ || !counterResource_)
    {
        return;
    }

    // 統計用にライト総数を読み戻す（次フレームの Begin で取り込む）
    if (counterReadbackResource_)
    {
        TransitionCounter(pCommandList, D3D12_RESOURCE_STATE_COPY_SOURCE);
        pCommandList->CopyBufferRegion(counterReadbackResource_.Get(), 0, counterResource_.Get(), 0, sizeof(uint32_t));
    }

    // カリングCS（非ピクセル）とライティングPS（ピクセル）の両方から読むので合成状態にする
    const D3D12_RESOURCE_STATES readState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    TransitionBuffer(pCommandList, readState);
    TransitionCounter(pCommandList, readState);
}

D3D12_GPU_VIRTUAL_ADDRESS PointLightGroup::GetConstantBufferAddress() const
{
    return constantResource_ ? constantResource_->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS PointLightGroup::GetStructuredBufferAddress() const
{
    return bufferResource_ ? bufferResource_->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS PointLightGroup::GetUavAddress() const
{
    return bufferResource_ ? bufferResource_->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS PointLightGroup::GetCounterAddress() const
{
    return counterResource_ ? counterResource_->GetGPUVirtualAddress() : 0;
}

// ===================================================
//  UI
// ===================================================

LightListResult PointLightGroup::DrawListRows(const char *filter, int selectedIndex)
{
    LightListResult result;
#ifdef USE_IMGUI
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
    {
        Entry &entry = entries_[i];
        if (!LightUI::ContainsIgnoreCase(entry.name, filter))
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
        LightUI::ColorSwatch(entry.gpu.color);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::AlignTextToFramePadding();

        const bool selected = (selectedIndex == i);
        if (!active)
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        if (ImGui::Selectable(entry.name.c_str(), selected, ImGuiSelectableFlags_None,
                              ImVec2(0.0f, ImGui::GetFrameHeight())))
        {
            result.clickedIndex = i;
        }
        if (!active)
            ImGui::PopStyleColor();

        // 右クリックメニュー（複製・削除）
        if (ImGui::BeginPopupContextItem("##ctx"))
        {
            if (ImGui::MenuItem("複製"))
            {
                result.duplicateIndex = i;
            }
            if (ImGui::MenuItem("削除"))
            {
                result.removeIndex = i;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::PopID();
    }
#else
    (void)filter;
    (void)selectedIndex;
#endif // USE_IMGUI
    return result;
}

LightEditRequest PointLightGroup::DrawProperties(int index, std::string &nameEditBuffer)
{
    LightEditRequest request;
#ifdef USE_IMGUI
    if (!IsValidIndex(index))
    {
        ImGui::TextDisabled("光源が選択されていません");
        return request;
    }

    Entry &entry = entries_[index];

    SectionHeader("[ 点光源 ]", DebugTheme::kAccentYellow);

    // ---- 名前 ----
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", &nameEditBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
    {
        request.kind = LightEditRequest::Kind::Rename;
        request.newName = nameEditBuffer;
    }
    ImGui::SetItemTooltip("Enterで確定します。ギズモの一覧にもこの名前で出ます");

    ImGui::Spacing();
    if (ImGui::BeginTable("##PtTable", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);

        LightUI::LabeledRow("有効", "この光源の有効 / 無効", [&] {
            bool active = entry.gpu.active != 0;
            if (ThemedToggle("##active", &active, DebugTheme::kAccentYellow))
                entry.gpu.active = active;
        });
        LightUI::LabeledRow("位置", "シーン上のギズモでも動かせます", [&] {
            ImGui::DragFloat3("##position", &entry.gpu.position.x, 0.1f, 0.0f, 0.0f, "%.2f");
        });
        LightUI::LabeledRow("色", "光の色", [&] {
            ImGui::ColorEdit3("##color", &entry.gpu.color.x, ImGuiColorEditFlags_NoInputs);
        });
        LightUI::LabeledRow("輝度", "光の明るさ", [&] {
            ThemedKnob("##intensity", &entry.gpu.intensity, 0.0f, 10.0f, "%.2f",
                       DebugTheme::kAccentYellow, 44.0f,
                       ImGuiKnobFlags_NoTitle | ImGuiKnobFlags_ValueTooltip);
        });
        LightUI::LabeledRow("届く距離", "この距離を超えると完全に減衰します", [&] {
            ImGui::DragFloat("##radius", &entry.gpu.radius, 0.1f, 0.1f, 500.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        });
        LightUI::LabeledRow("減衰", "大きいほど光源のすぐ近くだけが明るくなります", [&] {
            ImGui::DragFloat("##decay", &entry.gpu.decay, 0.05f, 0.0f, 5.0f, "%.2f");
        });
        LightUI::LabeledRow("ライティング", "陰影計算モデル", [&] {
            LightUI::LightingModelCombo(entry.gpu.HalfLambert, entry.gpu.BlinnPhong);
        });

        ImGui::EndTable();
    }

    ImGui::Spacing();
    const LightEditRequest buttons = LightUI::DrawDuplicateRemoveButtons(CanAdd());
    if (buttons.kind != LightEditRequest::Kind::None)
    {
        request = buttons;
    }
#else
    (void)index;
    (void)nameEditBuffer;
#endif // USE_IMGUI
    return request;
}

void PointLightGroup::DrawGizmoInspector(int index)
{
#ifdef USE_IMGUI
    if (!IsValidIndex(index))
        return;

    Entry &entry = entries_[index];
    ImGui::TextDisabled("点光源 : %s", entry.name.c_str());
    ImGui::DragFloat3("位置", &entry.gpu.position.x, 0.1f, 0.0f, 0.0f, "%.2f");
    ImGui::ColorEdit3("色", &entry.gpu.color.x, ImGuiColorEditFlags_NoInputs);
    ImGui::DragFloat("輝度", &entry.gpu.intensity, 0.05f, 0.0f, 10.0f, "%.2f");
    ImGui::DragFloat("届く距離", &entry.gpu.radius, 0.1f, 0.1f, 500.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
#else
    (void)index;
#endif // USE_IMGUI
}

void PointLightGroup::DrawVisualization(LineRenderer *drawLine, int selectedIndex, bool selectedOnly) const
{
    if (!drawLine)
    {
        return;
    }

    // 位置が分かる最小限のマーカー（3軸の十字）
    auto DrawMarker = [drawLine](const Vector3 &position, const Vector4 &color, float size) {
        drawLine->AddLine({position.x - size, position.y, position.z}, {position.x + size, position.y, position.z}, color);
        drawLine->AddLine({position.x, position.y - size, position.z}, {position.x, position.y + size, position.z}, color);
        drawLine->AddLine({position.x, position.y, position.z - size}, {position.x, position.y, position.z + size}, color);
    };

    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
    {
        const Entry &entry = entries_[i];
        if (!entry.gpu.active)
            continue;

        const Vector4 lightColor = {entry.gpu.color.x, entry.gpu.color.y, entry.gpu.color.z, 0.8f};

        if (selectedOnly && selectedIndex != i)
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
}

// ===================================================
//  セーブ / ロード
// ===================================================

void PointLightGroup::Save(DataHandler *handler) const
{
    if (!handler)
    {
        return;
    }

    handler->Save<int32_t>("pointLight_count", static_cast<int32_t>(entries_.size()));
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        const Entry &entry = entries_[i];
        const std::string prefix = std::format("pointLight_{:02d}_", i);
        handler->Save<std::string>(prefix + "name", entry.name);
        handler->Save<bool>(prefix + "active", entry.gpu.active != 0);
        handler->Save<Vector4>(prefix + "color", entry.gpu.color);
        handler->Save<Vector3>(prefix + "position", entry.gpu.position);
        handler->Save<int32_t>(prefix + "HalfLambert", entry.gpu.HalfLambert);
        handler->Save<int32_t>(prefix + "BlinnPhong", entry.gpu.BlinnPhong);
        handler->Save<float>(prefix + "intensity", entry.gpu.intensity);
        handler->Save<float>(prefix + "radius", entry.gpu.radius);
        handler->Save<float>(prefix + "decay", entry.gpu.decay);
    }
}

void PointLightGroup::Load(DataHandler *handler)
{
    if (!handler)
    {
        return;
    }

    entries_.clear();
    const int32_t count = handler->Load<int32_t>("pointLight_count", 0);
    for (int32_t i = 0; i < count && i < static_cast<int32_t>(kMaxBufferedLights); ++i)
    {
        const std::string prefix = std::format("pointLight_{:02d}_", i);
        Entry entry;
        entry.gpu.active = handler->Load<bool>(prefix + "active", true);
        entry.gpu.color = handler->Load<Vector4>(prefix + "color", {1.0f, 1.0f, 1.0f, 1.0f});
        entry.gpu.position = handler->Load<Vector3>(prefix + "position", {0.0f, 2.0f, 0.0f});
        entry.gpu.HalfLambert = handler->Load<int32_t>(prefix + "HalfLambert", false);
        entry.gpu.BlinnPhong = handler->Load<int32_t>(prefix + "BlinnPhong", true);
        entry.gpu.intensity = handler->Load<float>(prefix + "intensity", 1.0f);
        entry.gpu.radius = handler->Load<float>(prefix + "radius", 5.0f);
        entry.gpu.decay = handler->Load<float>(prefix + "decay", 1.0f);
        // 名前は後から追加した項目なので、無い場合は連番で補う
        entry.name = handler->Load<std::string>(prefix + "name", std::format("点光源{}", i + 1));
        entries_.push_back(entry);
    }
}
} // namespace Hagine
