#include "ToonSettings.h"
#include "DirectXCommon.h"
#include <data/DataHandler.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#ifdef USE_IMGUI
#include "imgui.h"
#include <utility/debug/imgui/DebugUIHelper.h>
#endif
#include <memory>

namespace Hagine {
namespace {
// 設定の保存先。Framework の起動時にもこの名前で読み込んでいる
constexpr const char *kDataFileName = "ToonData";
} // namespace


void ToonSettings::Initialize()
{
    pDxCommon_ = DirectXCommon::GetInstance();
    resource_ = pDxCommon_->CreateBufferResource(sizeof(ToonSettingsGPU));
    resource_->Map(0, nullptr, reinterpret_cast<void **>(&pMapped_));
    Update();
}

void ToonSettings::Finalize()
{
    pMapped_ = nullptr;
    resource_.Reset();
    pDxCommon_ = nullptr;
}

void ToonSettings::Update()
{
    if (pMapped_)
    {
        *pMapped_ = settings_;
    }
}

void ToonSettings::ApplyPreset(int index)
{
    // 影の色・段数・境目の固さだけを差し替える。リムやハイライトの色は触らない
    switch (index)
    {
    case 0: // 標準セル（2階調・やや青い影）
        settings_.steps = 1.0f;
        settings_.threshold = 0.5f;
        settings_.softness = 0.03f;
        settings_.shadeColor = {0.55f, 0.56f, 0.68f, 1.0f};
        settings_.shadowSharpness = 1.0f;
        break;
    case 1: // アニメ調（3階調・影を浅く）
        settings_.steps = 2.0f;
        settings_.threshold = 0.5f;
        settings_.softness = 0.05f;
        settings_.shadeColor = {0.70f, 0.70f, 0.80f, 1.0f};
        settings_.shadowSharpness = 1.0f;
        break;
    case 2: // コミック調（完全な硬いエッジ・影を濃く）
        settings_.steps = 1.0f;
        settings_.threshold = 0.55f;
        settings_.softness = 0.001f;
        settings_.shadeColor = {0.32f, 0.33f, 0.45f, 1.0f};
        settings_.shadowSharpness = 1.0f;
        break;
    default:
        break;
    }
}

void ToonSettings::DrawImGui()
{
#ifdef USE_IMGUI
    SectionHeader("[ トゥーンシェーディング ]", DebugTheme::kAccentPurple);

    bool enabled = IsEnabled();
    if (ThemedToggle("トゥーンを使う##toonEnabled", &enabled, DebugTheme::kAccentPurple))
    {
        SetEnabled(enabled);
    }
    ImGui::SetItemTooltip("陰影を段階的にして、影を面で塗ったセル画風の絵にします。\n"
                          "個別に外したいオブジェクトはマテリアル側のトゥーンを切ってください");

    ImGui::SameLine();
    StatusBadge(enabled ? "ON" : "OFF", enabled ? DebugTheme::kAccentGreen : DebugTheme::kTextDim);

    if (!enabled)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("OFFのあいだは従来のハーフランバート／ブリンフォンで描画されます");
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::Spacing();

        // ── プリセット ──
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
        ImGui::TextUnformatted("プリセット");
        ImGui::PopStyleColor();
        const float presetWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
        if (ImGui::Button("標準セル", ImVec2(presetWidth, 0.0f)))
            ApplyPreset(0);
        ImGui::SetItemTooltip("2階調・少し青みのある影。まず試すならこれ");
        ImGui::SameLine();
        if (ImGui::Button("アニメ調", ImVec2(presetWidth, 0.0f)))
            ApplyPreset(1);
        ImGui::SetItemTooltip("3階調。中間色が入るぶん柔らかい印象になります");
        ImGui::SameLine();
        if (ImGui::Button("コミック調", ImVec2(presetWidth, 0.0f)))
            ApplyPreset(2);
        ImGui::SetItemTooltip("完全に硬いエッジ・濃い影。劇画寄りの見た目");

        ImGui::Spacing();

        // ── 陰影の段 ──
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kHeaderPurple);
        const bool shadeOpen = ImGui::CollapsingHeader("陰影の段", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor();
        if (shadeOpen)
        {
            ImGui::SliderFloat("段数##toonSteps", &settings_.steps, 1.0f, 5.0f, "%.0f");
            ImGui::SetItemTooltip("1で明るい／暗いの2階調。増やすほど中間の段が入ります");

            ImGui::SliderFloat("明暗の境目##toonThreshold", &settings_.threshold, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("小さくすると影が減り、大きくすると影が広がります");

            ImGui::SliderFloat("境目のぼかし##toonSoftness", &settings_.softness, 0.0f, 0.3f, "%.3f");
            ImGui::SetItemTooltip("0に近いほど輪郭のはっきりした固い影になります");

            ImGui::ColorEdit3("影の色##toonShadeColor", &settings_.shadeColor.x);
            ImGui::SetItemTooltip("影側にかける色。黒ではなく少し青や紫に寄せるとトゥーンらしくなります");

            ImGui::SliderFloat("落ち影も段にする##toonShadowSharp", &settings_.shadowSharpness, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("1にするとシャドウマップの落ち影も同じ段で切られ、\n"
                                  "陰影と落ち影が同じ色になって貼り絵のような画になります。\n"
                                  "0にすると落ち影だけ従来どおりなめらかにボケます");
        }

        // ── ハイライト ──
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kHeaderYellow);
        const bool specOpen = ImGui::CollapsingHeader("ハイライト");
        ImGui::PopStyleColor();
        if (specOpen)
        {
            ImGui::ColorEdit3("色##toonSpecColor", &settings_.specularColor.x);
            ImGui::SliderFloat("強さ##toonSpecStrength", &settings_.specularColor.w, 0.0f, 2.0f, "%.2f");
            ImGui::SetItemTooltip("0にするとハイライトが消えます");
            ImGui::SliderFloat("出はじめ##toonSpecThreshold", &settings_.specularThreshold, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("大きくするほどハイライトが小さく鋭くなります");
            ImGui::SliderFloat("境目のぼかし##toonSpecSoftness", &settings_.specularSoftness, 0.0f, 0.3f, "%.3f");
            ImGui::SetItemTooltip("0に近いほど、輪郭のくっきりした板のようなハイライトになります");
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextWrapped("ハイライトの広さはマテリアルの光沢度にも影響されます");
            ImGui::PopStyleColor();
        }

        // ── リムライト ──
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kHeaderBlue);
        const bool rimOpen = ImGui::CollapsingHeader("リムライト（輪郭の光）");
        ImGui::PopStyleColor();
        if (rimOpen)
        {
            ImGui::ColorEdit3("色##toonRimColor", &settings_.rimColor.x);
            ImGui::SliderFloat("強さ##toonRimStrength", &settings_.rimColor.w, 0.0f, 2.0f, "%.2f");
            ImGui::SetItemTooltip("0にするとリムライトが消えます");
            ImGui::SliderFloat("絞り##toonRimPower", &settings_.rimPower, 0.5f, 12.0f, "%.1f");
            ImGui::SetItemTooltip("大きいほど輪郭のきわだけが光ります");
            ImGui::SliderFloat("出はじめ##toonRimThreshold", &settings_.rimThreshold, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("境目のぼかし##toonRimSoftness", &settings_.rimSoftness, 0.0f, 0.5f, "%.3f");
            ImGui::SliderFloat("光の当たる側だけ##toonRimMask", &settings_.rimLightMask, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("1にすると影側の輪郭は光りません。0で全周に出ます");
        }
    }

    // ── セーブ / ロード ──
    ImGui::Spacing();
    ImGui::Separator();
    const float saveWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kButtonPrimary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, DebugTheme::kButtonPrimaryHover);
    if (ImGui::Button("トゥーン設定を保存", ImVec2(saveWidth, 0.0f)))
    {
        SaveData(kDataFileName);
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kButtonConfirm);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, DebugTheme::kButtonConfirmHover);
    if (ImGui::Button("読み込み", ImVec2(saveWidth, 0.0f)))
    {
        LoadData(kDataFileName);
    }
    ImGui::PopStyleColor(2);
    ImGui::SetItemTooltip("保存した内容は次回の起動時にも自動で読み込まれます");
#endif // USE_IMGUI
}

void ToonSettings::SaveData(const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>("Toon", fileName);

    dataHandler->Save<bool>("enabled", IsEnabled());
    dataHandler->Save<float>("steps", settings_.steps);
    dataHandler->Save<float>("threshold", settings_.threshold);
    dataHandler->Save<float>("softness", settings_.softness);
    dataHandler->Save<Vector4>("shadeColor", settings_.shadeColor);
    dataHandler->Save<float>("shadowSharpness", settings_.shadowSharpness);

    dataHandler->Save<Vector4>("specularColor", settings_.specularColor);
    dataHandler->Save<float>("specularThreshold", settings_.specularThreshold);
    dataHandler->Save<float>("specularSoftness", settings_.specularSoftness);

    dataHandler->Save<Vector4>("rimColor", settings_.rimColor);
    dataHandler->Save<float>("rimPower", settings_.rimPower);
    dataHandler->Save<float>("rimThreshold", settings_.rimThreshold);
    dataHandler->Save<float>("rimSoftness", settings_.rimSoftness);
    dataHandler->Save<float>("rimLightMask", settings_.rimLightMask);

    dataHandler->Flush();
    ImGuiNotification::Post("トゥーン設定を保存しました: " + fileName, {0.2f, 0.8f, 0.2f, 1.0f});
}

void ToonSettings::LoadData(const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>("Toon", fileName);

    SetEnabled(dataHandler->Load<bool>("enabled", false));
    settings_.steps = dataHandler->Load<float>("steps", 2.0f);
    settings_.threshold = dataHandler->Load<float>("threshold", 0.5f);
    settings_.softness = dataHandler->Load<float>("softness", 0.02f);
    settings_.shadeColor = dataHandler->Load<Vector4>("shadeColor", {0.55f, 0.56f, 0.68f, 1.0f});
    settings_.shadowSharpness = dataHandler->Load<float>("shadowSharpness", 1.0f);

    settings_.specularColor = dataHandler->Load<Vector4>("specularColor", {1.0f, 1.0f, 1.0f, 0.35f});
    settings_.specularThreshold = dataHandler->Load<float>("specularThreshold", 0.5f);
    settings_.specularSoftness = dataHandler->Load<float>("specularSoftness", 0.05f);

    settings_.rimColor = dataHandler->Load<Vector4>("rimColor", {1.0f, 1.0f, 1.0f, 0.35f});
    settings_.rimPower = dataHandler->Load<float>("rimPower", 4.0f);
    settings_.rimThreshold = dataHandler->Load<float>("rimThreshold", 0.35f);
    settings_.rimSoftness = dataHandler->Load<float>("rimSoftness", 0.1f);
    settings_.rimLightMask = dataHandler->Load<float>("rimLightMask", 1.0f);

    Update();
}
} // namespace Hagine
