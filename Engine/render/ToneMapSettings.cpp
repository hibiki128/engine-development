#include "ToneMapSettings.h"
#include "DirectXCommon.h"
#include <data/DataHandler.h>

#ifdef USE_IMGUI
#include "imgui.h"
#include <debug/imgui/DebugUIHelper.h>
#include <debug/imgui/ImGuiNotification.h>
#include <icon/IconsFontAwesome5.h>
#endif // USE_IMGUI

#include <iterator>
#include <memory>

namespace Hagine {
namespace {
// 設定の保存先
constexpr const char *kDataFileName = "ToneMapData";
} // namespace

void ToneMapSettings::Initialize()
{
    pDxCommon_ = DirectXCommon::GetInstance();
    resource_ = pDxCommon_->CreateBufferResource(sizeof(ToneMapSettingsGPU));
    resource_->Map(0, nullptr, reinterpret_cast<void **>(&pMapped_));
    LoadData(kDataFileName);
    Update();
}

void ToneMapSettings::Finalize()
{
    pMapped_ = nullptr;
    resource_.Reset();
    pDxCommon_ = nullptr;
}

void ToneMapSettings::Update()
{
    if (pMapped_)
    {
        *pMapped_ = settings_;
    }
}

const char *ToneMapSettings::GetModeName(int mode)
{
    static const char *kNames[] = {"なし（そのまま）", "Reinhard（やわらかい）", "ACES（映画寄り・推奨）",
                                   "Uncharted2（コントラスト強め）"};
    if (mode < 0 || mode >= static_cast<int>(std::size(kNames)))
        return "";
    return kNames[mode];
}

void ToneMapSettings::SaveData(const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>("ToneMap", fileName);
    dataHandler->Save<float>("exposure", settings_.exposure);
    dataHandler->Save<int>("mode", settings_.mode);
    dataHandler->Save<float>("contrast", settings_.contrast);
    dataHandler->Save<float>("saturation", settings_.saturation);
    dataHandler->Save<float>("whitePoint", settings_.whitePoint);
    dataHandler->Save<Vector3>("colorFilter", settings_.colorFilter);
}

void ToneMapSettings::LoadData(const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>("ToneMap", fileName);
    settings_.exposure = dataHandler->Load<float>("exposure", 1.0f);
    settings_.mode = dataHandler->Load<int>("mode", 2);
    settings_.contrast = dataHandler->Load<float>("contrast", 1.0f);
    settings_.saturation = dataHandler->Load<float>("saturation", 1.0f);
    settings_.whitePoint = dataHandler->Load<float>("whitePoint", 4.0f);
    settings_.colorFilter = dataHandler->Load<Vector3>("colorFilter", Vector3(1.0f, 1.0f, 1.0f));
}

void ToneMapSettings::DrawImGui()
{
#ifdef USE_IMGUI
    SectionHeader(ICON_FA_ADJUST " トーンマップ / 露出", DebugTheme::kAccentYellow);

    DimText("シーンは1.0を超える明るさのまま描かれます。ここでそれを画面に出せる範囲へ収めます");

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##tonemapMode", GetModeName(settings_.mode)))
    {
        for (int i = 0; i < 4; ++i)
        {
            if (ImGui::Selectable(GetModeName(i), settings_.mode == i))
                settings_.mode = i;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("「なし」は 1.0 で頭打ちにするだけ（HDR化前と同じ見え方）。\n"
                          "見比べたいときはここを切り替えてください");
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##exposure", &settings_.exposure, 0.05f, 8.0f, "露出 %.2f",
                       ImGuiSliderFlags_Logarithmic);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("写真の露出と同じです。2.0 で1段明るく、0.5 で1段暗くなります");

    if (settings_.mode == 1 || settings_.mode == 3)
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##whitePoint", &settings_.whitePoint, 1.0f, 16.0f, "白とみなす明るさ %.1f");
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##contrast", &settings_.contrast, 0.5f, 2.0f, "コントラスト %.2f");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##saturation", &settings_.saturation, 0.0f, 2.0f, "彩度 %.2f");
    ImGui::ColorEdit3("画面全体の色味", &settings_.colorFilter.x, ImGuiColorEditFlags_Float);

    ImGui::Spacing();
    if (NeutralButton("既定値に戻す", ImVec2(-1.0f, 0.0f)))
    {
        settings_ = ToneMapSettingsGPU{};
    }
    if (ConfirmButton(ICON_FA_SAVE " トーンマップ設定を保存", ImVec2(-1.0f, 0.0f)))
    {
        SaveData(kDataFileName);
        ImGuiNotification::Post("トーンマップ設定を保存しました");
    }
#endif // USE_IMGUI
}

} // namespace Hagine
