#include "DirectionalLight.h"
#include "DirectXCommon.h"
#include <data/DataHandler.h>
#include <line/LineRenderer.h>
#ifdef USE_IMGUI
#include "LightUIHelper.h"
#endif

namespace Hagine {

void DirectionalLight::Initialize(DirectXCommon *pDxCommon)
{
    pDxCommon_ = pDxCommon;
    resource_ = pDxCommon_->CreateBufferResource(sizeof(DirectionalLightData));
    // 書き込むためのアドレスを取得
    resource_->Map(0, nullptr, reinterpret_cast<void **>(&pData_));
    // デフォルト値
    pData_->color = {1.0f, 1.0f, 1.0f, 1.0f};
    pData_->direction = {0.0f, -1.0f, 0.0f};
    pData_->intensity = 1.0f;
    pData_->active = true;
    pData_->HalfLambert = false;
    pData_->BlinnPhong = true;
}

void DirectionalLight::Finalize()
{
    pData_ = nullptr;
    resource_.Reset();
    pDxCommon_ = nullptr;
}

void DirectionalLight::Update()
{
    if (pData_)
    {
        pData_->active = enabled_;
    }
}

D3D12_GPU_VIRTUAL_ADDRESS DirectionalLight::GetGpuAddress() const
{
    return resource_ ? resource_->GetGPUVirtualAddress() : 0;
}

Vector3 DirectionalLight::GetDirection() const
{
    return pData_ ? pData_->direction : Vector3{0.0f, -1.0f, 0.0f};
}

Vector4 DirectionalLight::GetColor() const
{
    return pData_ ? pData_->color : Vector4{1.0f, 1.0f, 1.0f, 1.0f};
}

void DirectionalLight::DrawProperties()
{
#ifdef USE_IMGUI
    if (!pData_)
    {
        return;
    }

    SectionHeader("[ 平行光源 ]", DebugTheme::kAccentBlue);

    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
    ImGui::Checkbox("有効にする##diren", &enabled_);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("シーン全体を一方向から照らす光源です。太陽光にあたります");

    if (!enabled_)
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

        LightUI::LabeledRow("方向", "光の進む方向（自動で正規化されます）", [&] {
            if (ImGui::DragFloat3("##direction", &pData_->direction.x, 0.01f, -1.0f, 1.0f, "%.2f"))
                pData_->direction = pData_->direction.Normalize();
        });
        LightUI::LabeledRow("輝度", "光の明るさ", [&] {
            ThemedKnob("##intensity", &pData_->intensity, 0.0f, 10.0f, "%.2f",
                       DebugTheme::kAccentYellow, 44.0f,
                       ImGuiKnobFlags_NoTitle | ImGuiKnobFlags_ValueTooltip);
        });
        LightUI::LabeledRow("色", "光の色", [&] {
            ImGui::ColorEdit3("##color", &pData_->color.x, ImGuiColorEditFlags_NoInputs);
        });
        LightUI::LabeledRow("ライティング", "陰影計算モデル", [&] {
            LightUI::LightingModelCombo(pData_->HalfLambert, pData_->BlinnPhong);
        });

        ImGui::EndTable();
    }
#endif // USE_IMGUI
}

void DirectionalLight::DrawVisualization(LineRenderer *drawLine) const
{
    if (!drawLine || !pData_ || !enabled_ || pData_->active == 0)
    {
        return;
    }

    const Vector4 dirColor = {pData_->color.x, pData_->color.y, pData_->color.z, 0.8f};

    // 複数の平行線で方向を表示
    for (int i = -2; i <= 2; i++)
    {
        for (int j = -2; j <= 2; j++)
        {
            const Vector3 startPos = {i * 5.0f, 20.0f, j * 5.0f};
            const Vector3 endPos = startPos + pData_->direction * 15.0f;
            drawLine->AddLine(startPos, endPos, dirColor);
        }
    }
}

void DirectionalLight::Save(DataHandler *handler) const
{
    if (!handler || !pData_)
    {
        return;
    }
    handler->Save<bool>("directional_active", enabled_);
    handler->Save<Vector3>("directional_direction", pData_->direction);
    handler->Save<float>("directional_intensity", pData_->intensity);
    handler->Save<Vector4>("directional_color", pData_->color);
    handler->Save<int32_t>("directional_HalfLambert", pData_->HalfLambert);
    handler->Save<int32_t>("directional_BlinnPhong", pData_->BlinnPhong);
}

void DirectionalLight::Load(DataHandler *handler)
{
    if (!handler || !pData_)
    {
        return;
    }
    enabled_ = handler->Load<bool>("directional_active", true);
    pData_->color = handler->Load<Vector4>("directional_color", {1.0f, 1.0f, 1.0f, 1.0f});
    pData_->direction = handler->Load<Vector3>("directional_direction", {0.0f, -1.0f, 0.0f});
    pData_->HalfLambert = handler->Load<int32_t>("directional_HalfLambert", false);
    pData_->BlinnPhong = handler->Load<int32_t>("directional_BlinnPhong", true);
    pData_->intensity = handler->Load<float>("directional_intensity", 1.0f);
}
} // namespace Hagine
