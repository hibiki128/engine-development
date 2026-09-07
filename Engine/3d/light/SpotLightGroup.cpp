#include "SpotLightGroup.h"
#include "DirectXCommon.h"
#include <algorithm>
#include <cmath>
#include <data/DataHandler.h>
#include <format>
#include <line/LineRenderer.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#ifdef USE_IMGUI
#include "LightUIHelper.h"
#include <imgui_stdlib.h>
#endif

namespace Hagine {

void SpotLightGroup::Initialize(DirectXCommon *pDxCommon)
{
    pDxCommon_ = pDxCommon;
    resource_ = pDxCommon_->CreateBufferResource(sizeof(SpotLightsCB));
    // 書き込むためのアドレスを取得
    resource_->Map(0, nullptr, reinterpret_cast<void **>(&pData_));

    for (int i = 0; i < MAX_SPOT_LIGHTS; i++)
    {
        pData_->lights[i].color = {1.0f, 1.0f, 1.0f, 1.0f};
        pData_->lights[i].position = {0.0f, -4.0f, -3.0f};
        pData_->lights[i].direction = {0.0f, -1.0f, 0.0f};
        pData_->lights[i].intensity = 1.0f;
        pData_->lights[i].distance = 10.0f;
        pData_->lights[i].decay = 1.0f;
        pData_->lights[i].cosAngle = 3.0f;
        pData_->lights[i].active = false;
        pData_->lights[i].HalfLambert = false;
        pData_->lights[i].BlinnPhong = true;
    }

    pData_->count = 0;
}

void SpotLightGroup::Finalize()
{
    pData_ = nullptr;
    resource_.Reset();
    entries_.clear();
    pDxCommon_ = nullptr;
}

// ===================================================
//  追加・削除
// ===================================================

int SpotLightGroup::Add(const std::string &name)
{
    // スポットライトは定数バッファ経由なので上限あり
    if (!CanAdd())
    {
        ImGuiNotification::Post("スポットライトはこれ以上追加できません", {0.9f, 0.5f, 0.3f, 1.0f});
        return -1;
    }

    Entry entry;
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
    entry.name = name;
    entry.aimPoint = entry.gpu.position + entry.gpu.direction * entry.gpu.distance;
    entry.prevAim = entry.aimPoint;

    entries_.push_back(entry);
    ImGuiNotification::Post("スポットライトを追加しました", {0.4f, 0.8f, 1.0f, 1.0f});
    return static_cast<int>(entries_.size()) - 1;
}

bool SpotLightGroup::Remove(int index)
{
    if (!IsValidIndex(index))
        return false;

    entries_.erase(entries_.begin() + index);
    ImGuiNotification::Post("スポットライトを削除しました", {0.9f, 0.7f, 0.2f, 1.0f});
    return true;
}

int SpotLightGroup::Duplicate(int index, const std::string &name)
{
    if (!IsValidIndex(index) || !CanAdd())
        return -1;

    Entry copy = entries_[index];
    copy.name = name;
    entries_.push_back(copy);
    ImGuiNotification::Post("スポットライトを複製しました", {0.4f, 0.8f, 1.0f, 1.0f});
    return static_cast<int>(entries_.size()) - 1;
}

int SpotLightGroup::GetActiveCount() const
{
    int count = 0;
    for (const Entry &entry : entries_)
    {
        count += entry.gpu.active ? 1 : 0;
    }
    return count;
}

// ===================================================
//  更新・GPU転送
// ===================================================

void SpotLightGroup::UpdateAimPoints()
{
    for (Entry &entry : entries_)
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

void SpotLightGroup::UpdateConstantBuffer()
{
    if (!pData_)
    {
        return;
    }

    int32_t count = 0;
    for (const Entry &entry : entries_)
    {
        if (count >= MAX_SPOT_LIGHTS)
            break;
        pData_->lights[count++] = entry.gpu;
    }
    pData_->count = count;
}

D3D12_GPU_VIRTUAL_ADDRESS SpotLightGroup::GetConstantBufferAddress() const
{
    return resource_ ? resource_->GetGPUVirtualAddress() : 0;
}

// ===================================================
//  UI
// ===================================================

LightListResult SpotLightGroup::DrawListRows(const char *filter, int selectedIndex)
{
    LightListResult result;
#ifdef USE_IMGUI
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
    {
        Entry &entry = entries_[i];
        if (!LightUI::ContainsIgnoreCase(entry.name, filter))
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

LightEditRequest SpotLightGroup::DrawProperties(int index, std::string &nameEditBuffer, const char *aimHandleSuffix)
{
    LightEditRequest request;
#ifdef USE_IMGUI
    if (!IsValidIndex(index))
    {
        ImGui::TextDisabled("光源が選択されていません");
        return request;
    }

    Entry &entry = entries_[index];

    SectionHeader("[ スポットライト ]", DebugTheme::kAccentBlue);

    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##name", &nameEditBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
    {
        request.kind = LightEditRequest::Kind::Rename;
        request.newName = nameEditBuffer;
    }
    ImGui::SetItemTooltip("Enterで確定します。ギズモの一覧にもこの名前で出ます");

    ImGui::Spacing();
    ImGui::TextDisabled("向きはシーン上の「%s%s」ハンドルを動かしても変えられます", entry.name.c_str(),
                        aimHandleSuffix ? aimHandleSuffix : "");

    ImGui::Spacing();
    if (ImGui::BeginTable("##SpTable", 2, ImGuiTableFlags_SizingStretchProp))
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
        LightUI::LabeledRow("向き", "自動で正規化されます", [&] {
            if (ImGui::DragFloat3("##direction", &entry.gpu.direction.x, 0.01f, -1.0f, 1.0f, "%.2f"))
            {
                entry.gpu.direction = entry.gpu.direction.Normalize();
            }
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
            ImGui::DragFloat("##distance", &entry.gpu.distance, 0.1f, 0.1f, 500.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        });
        LightUI::LabeledRow("減衰", "大きいほど光源のすぐ近くだけが明るくなります", [&] {
            ImGui::DragFloat("##decay", &entry.gpu.decay, 0.05f, 0.0f, 5.0f, "%.2f");
        });
        LightUI::LabeledRow("広がり", "円錐の半頂角。小さいほど絞られます", [&] {
            // 内部は cos で持っているが、角度のほうが直感的なので度数で見せる
            float degree = std::acos(std::clamp(entry.gpu.cosAngle, -1.0f, 1.0f)) * 180.0f / 3.14159265f;
            if (ImGui::SliderFloat("##coneAngle", &degree, 1.0f, 89.0f, "%.1f 度"))
            {
                entry.gpu.cosAngle = std::cos(degree * 3.14159265f / 180.0f);
            }
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
    (void)aimHandleSuffix;
#endif // USE_IMGUI
    return request;
}

void SpotLightGroup::DrawGizmoInspector(int index)
{
#ifdef USE_IMGUI
    if (!IsValidIndex(index))
        return;

    Entry &entry = entries_[index];
    ImGui::TextDisabled("スポットライト : %s", entry.name.c_str());
    ImGui::DragFloat3("位置", &entry.gpu.position.x, 0.1f, 0.0f, 0.0f, "%.2f");
    ImGui::DragFloat3("照射先", &entry.aimPoint.x, 0.1f, 0.0f, 0.0f, "%.2f");
    ImGui::ColorEdit3("色", &entry.gpu.color.x, ImGuiColorEditFlags_NoInputs);
    ImGui::DragFloat("輝度", &entry.gpu.intensity, 0.05f, 0.0f, 10.0f, "%.2f");
    ImGui::DragFloat("届く距離", &entry.gpu.distance, 0.1f, 0.1f, 500.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
#else
    (void)index;
#endif // USE_IMGUI
}

void SpotLightGroup::DrawVisualization(LineRenderer *drawLine, int selectedIndex, bool selectedOnly) const
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

        const SpotLightData &light = entry.gpu;
        const Vector4 lightColor = {light.color.x, light.color.y, light.color.z, 0.8f};

        if (selectedOnly && selectedIndex != i)
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

// ===================================================
//  セーブ / ロード
// ===================================================

void SpotLightGroup::Save(DataHandler *handler) const
{
    if (!handler)
    {
        return;
    }

    handler->Save<int32_t>("spotLight_count", static_cast<int32_t>(entries_.size()));
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        const Entry &entry = entries_[i];
        const std::string prefix = std::format("spotLight_{:02d}_", i);
        handler->Save<std::string>(prefix + "name", entry.name);
        handler->Save<bool>(prefix + "active", entry.gpu.active != 0);
        handler->Save<Vector4>(prefix + "color", entry.gpu.color);
        handler->Save<Vector3>(prefix + "position", entry.gpu.position);
        handler->Save<Vector3>(prefix + "direction", entry.gpu.direction);
        handler->Save<int32_t>(prefix + "HalfLambert", entry.gpu.HalfLambert);
        handler->Save<int32_t>(prefix + "BlinnPhong", entry.gpu.BlinnPhong);
        handler->Save<float>(prefix + "intensity", entry.gpu.intensity);
        handler->Save<float>(prefix + "distance", entry.gpu.distance);
        handler->Save<float>(prefix + "cosAngle", entry.gpu.cosAngle);
        handler->Save<float>(prefix + "decay", entry.gpu.decay);
    }
}

void SpotLightGroup::Load(DataHandler *handler)
{
    if (!handler)
    {
        return;
    }

    entries_.clear();
    const int32_t count = handler->Load<int32_t>("spotLight_count", 0);
    for (int32_t i = 0; i < count && i < MAX_SPOT_LIGHTS; ++i)
    {
        const std::string prefix = std::format("spotLight_{:02d}_", i);
        Entry entry;
        entry.gpu.active = handler->Load<bool>(prefix + "active", true);
        entry.gpu.color = handler->Load<Vector4>(prefix + "color", {1.0f, 1.0f, 1.0f, 1.0f});
        entry.gpu.position = handler->Load<Vector3>(prefix + "position", {0.0f, 2.0f, 0.0f});
        entry.gpu.direction = handler->Load<Vector3>(prefix + "direction", {0.0f, -1.0f, 0.0f});
        entry.gpu.HalfLambert = handler->Load<int32_t>(prefix + "HalfLambert", false);
        entry.gpu.BlinnPhong = handler->Load<int32_t>(prefix + "BlinnPhong", true);
        entry.gpu.intensity = handler->Load<float>(prefix + "intensity", 1.0f);
        entry.gpu.distance = handler->Load<float>(prefix + "distance", 10.0f);
        entry.gpu.cosAngle = handler->Load<float>(prefix + "cosAngle", 0.7f);
        entry.gpu.decay = handler->Load<float>(prefix + "decay", 1.0f);
        entry.name = handler->Load<std::string>(prefix + "name", std::format("スポット{}", i + 1));
        entry.aimPoint = entry.gpu.position + entry.gpu.direction * entry.gpu.distance;
        entry.prevAim = entry.aimPoint;
        entries_.push_back(entry);
    }
}
} // namespace Hagine
