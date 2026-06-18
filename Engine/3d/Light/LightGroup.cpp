#include "LightGroup.h"
#include "DirectXCommon.h"
#include <Engine/Utility/Debug/ImGui/ImGuiNotification.h>
#include <Engine/Utility/Debug/ImGui/Debugui_improved.h>
#include <Line/DrawLine3D.h>
#include <filesystem>
#include <fstream>

namespace Hagine {
void LightGroup::Finalize() {
    directionalLightResource_.Reset();
    pointLightsResource_.Reset();
    spotLightsResource_.Reset();
    cameraForGPUResource_.Reset();
    pointLights_.clear();
    spotLights_.clear();
    DLightData_.reset();
}

void LightGroup::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();
    CreateCamera();
    CreatePointLights();
    CreateDirectionLight();
    CreateSpotLights();
}

void LightGroup::Update(const ViewProjection &viewProjection) {
    cameraForGPUData_->worldPosition = viewProjection.translation_;

    if (isDirectionalLight_) {
        directionalLightData_->active = true;
    } else {
        directionalLightData_->active = false;
    }

    // ポイントライトデータ更新
    UpdatePointLightBuffer();

    // スポットライトデータ更新
    UpdateSpotLightBuffer();

    DrawLightVisualization();
}

void LightGroup::Draw() {
    // DirectionalLight用のCBufferの場所を設定
    dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(3, directionalLightResource_->GetGPUVirtualAddress());

    dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(4, cameraForGPUResource_->GetGPUVirtualAddress());

    dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(5, pointLightsResource_->GetGPUVirtualAddress());

    dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(6, spotLightsResource_->GetGPUVirtualAddress());
}

void LightGroup::AddPointLight() {
    if (pointLights_.size() >= MAX_POINT_LIGHTS) {
        return; // 最大数に達している場合は追加しない
    }

    PointLight newLight = {};
    newLight.color = {1.0f, 1.0f, 1.0f, 1.0f};
    newLight.position = {-1.0f, 4.0f, -3.0f};
    newLight.intensity = 1.0f;
    newLight.decay = 1.0f;
    newLight.radius = 2.0f;
    newLight.active = true;
    newLight.HalfLambert = false;
    newLight.BlinnPhong = true;

    pointLights_.push_back(newLight);
    ImGuiNotification::Post("ポイントライトを追加しました", {0.4f, 0.8f, 1.0f, 1.0f});
}

void LightGroup::RemovePointLight(int index) {
    if (index >= 0 && index < static_cast<int>(pointLights_.size())) {
        pointLights_.erase(pointLights_.begin() + index);
        ImGuiNotification::Post("ポイントライトを削除しました", {0.9f, 0.7f, 0.2f, 1.0f});
    }
}

void LightGroup::AddSpotLight() {
    if (spotLights_.size() >= MAX_SPOT_LIGHTS) {
        return; // 最大数に達している場合は追加しない
    }

    SpotLight newLight = {};
    newLight.color = {1.0f, 1.0f, 1.0f, 1.0f};
    newLight.position = {0.0f, 2.0f, 0.0f};
    newLight.direction = {0.0f, -1.0f, 0.0f};
    newLight.intensity = 1.0f;
    newLight.active = true;
    newLight.distance = 10.0f;
    newLight.decay = 1.0f;
    newLight.cosAngle = 0.7f; // 約45度
    newLight.HalfLambert = false;
    newLight.BlinnPhong = true;

    spotLights_.push_back(newLight);
    ImGuiNotification::Post("スポットライトを追加しました", {0.4f, 0.8f, 1.0f, 1.0f});
}

void LightGroup::RemoveSpotLight(int index) {
    if (index >= 0 && index < static_cast<int>(spotLights_.size())) {
        spotLights_.erase(spotLights_.begin() + index);
        ImGuiNotification::Post("スポットライトを削除しました", {0.9f, 0.7f, 0.2f, 1.0f});
    }
}

void LightGroup::UpdatePointLightBuffer() {
    pointLightsData_->count = static_cast<int32_t>(pointLights_.size());

    for (size_t i = 0; i < pointLights_.size() && i < MAX_POINT_LIGHTS; ++i) {
        pointLightsData_->lights[i] = pointLights_[i];
    }
}

void LightGroup::UpdateSpotLightBuffer() {
    spotLightsData_->count = static_cast<int32_t>(spotLights_.size());

    for (size_t i = 0; i < spotLights_.size() && i < MAX_SPOT_LIGHTS; ++i) {
        spotLightsData_->lights[i] = spotLights_[i];
    }
}

void LightGroup::CreatePointLights() {
    // サイズを明示的に計算
    size_t bufferSize = sizeof(PointLights);
    pointLightsResource_ = dxCommon_->CreateBufferResource(bufferSize);
    pointLightsResource_->Map(0, nullptr, reinterpret_cast<void **>(&pointLightsData_));

    for (int i = 0; i < MAX_POINT_LIGHTS; i++) {
        pointLightsData_->lights[i].color = {1.0f, 1.0f, 1.0f, 1.0f};
        pointLightsData_->lights[i].position = {-1.0f, 4.0f, -3.0f};
        pointLightsData_->lights[i].intensity = 1.0f;
        pointLightsData_->lights[i].decay = 1.0f;
        pointLightsData_->lights[i].radius = 2.0f;
        pointLightsData_->lights[i].active = false;
        pointLightsData_->lights[i].HalfLambert = false;
        pointLightsData_->lights[i].BlinnPhong = true;
    }

    pointLightsData_->count = 0;
}

void LightGroup::CreateSpotLights() {
    spotLightsResource_ = dxCommon_->CreateBufferResource(sizeof(SpotLights));
    // 書き込むためのアドレスを取得
    spotLightsResource_->Map(0, nullptr, reinterpret_cast<void **>(&spotLightsData_));

    for (int i = 0; i < MAX_SPOT_LIGHTS; i++) {
        spotLightsData_->lights[i].color = {1.0f, 1.0f, 1.0f, 1.0f};
        spotLightsData_->lights[i].position = {0.0f, -4.0f, -3.0f};
        spotLightsData_->lights[i].direction = {0.0f, -1.0f, 0.0f};
        spotLightsData_->lights[i].intensity = 1.0f;
        spotLightsData_->lights[i].distance = 10.0f;
        spotLightsData_->lights[i].decay = 1.0f;
        spotLightsData_->lights[i].cosAngle = 3.0f;
        spotLightsData_->lights[i].active = false;
        spotLightsData_->lights[i].HalfLambert = false;
        spotLightsData_->lights[i].BlinnPhong = true;
    }

    spotLightsData_->count = 0;
}

void LightGroup::CreateDirectionLight() {
    directionalLightResource_ = dxCommon_->CreateBufferResource(sizeof(DirectionLight));
    // 書き込むためのアドレスを取得
    directionalLightResource_->Map(0, nullptr, reinterpret_cast<void **>(&directionalLightData_));
    // デフォルト値
    directionalLightData_->color = {1.0f, 1.0f, 1.0f, 1.0f};
    directionalLightData_->direction = {0.0f, -1.0f, 0.0f};
    directionalLightData_->intensity = 1.0f;
    directionalLightData_->active = true;
    directionalLightData_->HalfLambert = false;
    directionalLightData_->BlinnPhong = true;
}

void LightGroup::CreateCamera() {
    cameraForGPUResource_ = dxCommon_->CreateBufferResource(sizeof(CameraForGPU));
    cameraForGPUResource_->Map(0, nullptr, reinterpret_cast<void **>(&cameraForGPUData_));
    cameraForGPUData_->worldPosition = {0.0f, 0.0f, -50.0f};
}

void LightGroup::imgui() {
#ifdef USE_IMGUI
    // ラベル + 全幅ウィジェットの2列行を描くヘルパー
    auto Row = [](const char *label, const char *tip, auto &&drawWidget) {
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
    };

    // ── デバッグ描画 ──
    SectionHeader("[ デバッグ描画 ]", DebugTheme::kAccentCyan);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);
    ImGui::Checkbox("光源を可視化##lightvis", &showLightVisualization_);
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("光源の位置・方向・範囲を線で表示します");

    ImGui::Spacing();

    if (ImGui::BeginTabBar("LightTypeTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {

        // ============================================================
        // 平行光源
        // ============================================================
        if (ImGui::BeginTabItem("平行光源")) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
            ImGui::Checkbox("平行光源を有効にする##diren", &isDirectionalLight_);
            ImGui::PopStyleColor();

            if (directionalLightData_->active) {
                ImGui::Spacing();
                SectionHeader("[ 基本設定 ]", DebugTheme::kAccentBlue);

                if (ImGui::BeginTable("##DirTable", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);

                    Row("方向", "光の進む方向（自動で正規化）", [&] {
                        if (ImGui::DragFloat3("##direction", &directionalLightData_->direction.x, 0.01f, -1.0f, 1.0f, "%.2f"))
                            directionalLightData_->direction = directionalLightData_->direction.Normalize();
                    });
                    Row("輝度", "光の明るさ", [&] {
                        ImGui::DragFloat("##intensity", &directionalLightData_->intensity, 0.01f, 0.0f, 10.0f, "%.2f");
                    });
                    Row("色", "光の色", [&] {
                        ImGui::ColorEdit3("##color", &directionalLightData_->color.x, ImGuiColorEditFlags_NoInputs);
                    });
                    Row("ライティング", "陰影計算モデル", [&] {
                        const char *types[] = {"HalfLambert", "BlinnPhong"};
                        int sel = directionalLightData_->BlinnPhong ? 1 : 0;
                        if (ImGui::Combo("##lightingType", &sel, types, IM_ARRAYSIZE(types))) {
                            directionalLightData_->HalfLambert = (sel == 0) ? 1 : 0;
                            directionalLightData_->BlinnPhong = (sel == 1) ? 1 : 0;
                        }
                    });

                    ImGui::EndTable();
                }
                ImGui::Spacing();
            }
            ImGui::EndTabItem();
        }

        // ============================================================
        // 点光源
        // ============================================================
        if (ImGui::BeginTabItem("点光源")) {
            ImGui::Spacing();

            // 追加ボタン（上限到達時は無効化）+ 個数表示
            const bool canAddPoint = pointLights_.size() < MAX_POINT_LIGHTS;
            ImGui::BeginDisabled(!canAddPoint);
            ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgGreen);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.68f, 0.52f, 0.40f));
            if (ImGui::Button("点光源を追加")) {
                AddPointLight();
            }
            ImGui::PopStyleColor(2);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::Text("(%d / %d)", static_cast<int>(pointLights_.size()), MAX_POINT_LIGHTS);
            ImGui::PopStyleColor();

            ImGui::Spacing();

            for (int i = 0; i < static_cast<int>(pointLights_.size()); ++i) {
                ImGui::PushID(i);

                std::string headerLabel = std::format("点光源 #{}", i + 1);
                if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {

                    // 削除
                    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgRed);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.46f, 0.46f, 0.40f));
                    bool del = ImGui::SmallButton("削除");
                    ImGui::PopStyleColor(2);
                    if (del) {
                        RemovePointLight(i);
                        ImGui::PopID();
                        break;
                    }

                    if (ImGui::BeginTable("##PtTable", 2, ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                        ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);

                        Row("有効", "この光源の有効 / 無効", [&] {
                            bool active = pointLights_[i].active;
                            if (ImGui::Checkbox("##active", &active))
                                pointLights_[i].active = active;
                        });
                        Row("位置", nullptr, [&] {
                            ImGui::DragFloat3("##position", &pointLights_[i].position.x, 0.1f, 0.0f, 0.0f, "%.2f");
                        });
                        Row("色", nullptr, [&] {
                            ImGui::ColorEdit3("##color", &pointLights_[i].color.x, ImGuiColorEditFlags_NoInputs);
                        });
                        Row("輝度", nullptr, [&] {
                            ImGui::DragFloat("##intensity", &pointLights_[i].intensity, 0.01f, 0.0f, 10.0f, "%.2f");
                        });
                        Row("半径", nullptr, [&] {
                            ImGui::DragFloat("##radius", &pointLights_[i].radius, 0.1f, 0.1f, 100.0f, "%.2f");
                        });
                        Row("減衰率", nullptr, [&] {
                            ImGui::DragFloat("##decay", &pointLights_[i].decay, 0.1f, 0.0f, 5.0f, "%.2f");
                        });
                        Row("ライティング", nullptr, [&] {
                            const char *types[] = {"HalfLambert", "BlinnPhong"};
                            int sel = pointLights_[i].BlinnPhong ? 1 : 0;
                            if (ImGui::Combo("##lighting", &sel, types, IM_ARRAYSIZE(types))) {
                                pointLights_[i].HalfLambert = (sel == 0) ? 1 : 0;
                                pointLights_[i].BlinnPhong = (sel == 1) ? 1 : 0;
                            }
                        });

                        ImGui::EndTable();
                    }
                }

                ImGui::PopID();
            }

            ImGui::Spacing();
            ImGui::EndTabItem();
        }

        // ============================================================
        // スポットライト
        // ============================================================
        if (ImGui::BeginTabItem("スポットライト")) {
            ImGui::Spacing();

            const bool canAddSpot = spotLights_.size() < MAX_SPOT_LIGHTS;
            ImGui::BeginDisabled(!canAddSpot);
            ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgGreen);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.68f, 0.52f, 0.40f));
            if (ImGui::Button("スポットライトを追加")) {
                AddSpotLight();
            }
            ImGui::PopStyleColor(2);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::Text("(%d / %d)", static_cast<int>(spotLights_.size()), MAX_SPOT_LIGHTS);
            ImGui::PopStyleColor();

            ImGui::Spacing();

            for (int i = 0; i < static_cast<int>(spotLights_.size()); ++i) {
                ImGui::PushID(i);

                std::string headerLabel = std::format("スポットライト #{}", i + 1);
                if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {

                    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgRed);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.46f, 0.46f, 0.40f));
                    bool del = ImGui::SmallButton("削除");
                    ImGui::PopStyleColor(2);
                    if (del) {
                        RemoveSpotLight(i);
                        ImGui::PopID();
                        break;
                    }

                    if (ImGui::BeginTable("##SpTable", 2, ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                        ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);

                        Row("有効", "この光源の有効 / 無効", [&] {
                            bool active = spotLights_[i].active;
                            if (ImGui::Checkbox("##active", &active))
                                spotLights_[i].active = active;
                        });
                        Row("位置", nullptr, [&] {
                            ImGui::DragFloat3("##position", &spotLights_[i].position.x, 0.1f, 0.0f, 0.0f, "%.2f");
                        });
                        Row("方向", "自動で正規化", [&] {
                            if (ImGui::DragFloat3("##direction", &spotLights_[i].direction.x, 0.01f, -1.0f, 1.0f, "%.2f"))
                                spotLights_[i].direction = spotLights_[i].direction.Normalize();
                        });
                        Row("色", nullptr, [&] {
                            ImGui::ColorEdit3("##color", &spotLights_[i].color.x, ImGuiColorEditFlags_NoInputs);
                        });
                        Row("輝度", nullptr, [&] {
                            ImGui::DragFloat("##intensity", &spotLights_[i].intensity, 0.01f, 0.0f, 10.0f, "%.2f");
                        });
                        Row("距離", nullptr, [&] {
                            ImGui::DragFloat("##distance", &spotLights_[i].distance, 0.1f, 0.1f, 100.0f, "%.2f");
                        });
                        Row("減衰率", nullptr, [&] {
                            ImGui::DragFloat("##decay", &spotLights_[i].decay, 0.1f, 0.0f, 5.0f, "%.2f");
                        });
                        Row("余弦", "スポットの広がり (cos)", [&] {
                            ImGui::DragFloat("##cosAngle", &spotLights_[i].cosAngle, 0.01f, -1.0f, 1.0f, "%.2f");
                        });
                        Row("ライティング", nullptr, [&] {
                            const char *types[] = {"HalfLambert", "BlinnPhong"};
                            int sel = spotLights_[i].BlinnPhong ? 1 : 0;
                            if (ImGui::Combo("##lighting", &sel, types, IM_ARRAYSIZE(types))) {
                                spotLights_[i].HalfLambert = (sel == 0) ? 1 : 0;
                                spotLights_[i].BlinnPhong = (sel == 1) ? 1 : 0;
                            }
                        });

                        ImGui::EndTable();
                    }
                }

                ImGui::PopID();
            }

            ImGui::Spacing();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // ── セーブ / ロード ──
    ImGui::Spacing();
    SectionHeader("[ セーブ / ロード ]", DebugTheme::kAccentPurple);

    static char saveFileName[256] = "DefaultLightSetting";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##lightfile", "ファイル名", saveFileName, sizeof(saveFileName));
    ImGui::Spacing();

    // 保存・読込（通知は SaveLightData / LoadLightData 側で投稿する）
    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.58f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.52f, 0.70f, 0.95f));
    if (ImGui::Button("セーブ", ImVec2(bw, 0.0f))) {
        SaveLightData(std::string(saveFileName));
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.40f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.60f, 0.50f, 0.95f));
    if (ImGui::Button("ロード", ImVec2(bw, 0.0f))) {
        LoadLightData(std::string(saveFileName));
    }
    ImGui::PopStyleColor(2);
#endif // USE_IMGUI
}

void LightGroup::SaveLightData(const std::string &fileName) {
    auto dataHandler = std::make_unique<DataHandler>("LightGroup", fileName);

    // Directional Light
    dataHandler->Save<bool>("directional_active", isDirectionalLight_);
    dataHandler->Save<Vector3>("directional_direction", directionalLightData_->direction);
    dataHandler->Save<float>("directional_intensity", directionalLightData_->intensity);
    dataHandler->Save<Vector4>("directional_color", directionalLightData_->color);
    dataHandler->Save<int32_t>("directional_HalfLambert", directionalLightData_->HalfLambert);
    dataHandler->Save<int32_t>("directional_BlinnPhong", directionalLightData_->BlinnPhong);

    // Point Lights
    dataHandler->Save<int32_t>("pointLight_count", static_cast<int32_t>(pointLights_.size()));
    for (size_t i = 0; i < pointLights_.size(); ++i) {
        std::string prefix = std::format("pointLight_{:02d}_", i);
        dataHandler->Save<bool>(prefix + "active", pointLights_[i].active);
        dataHandler->Save<Vector4>(prefix + "color", pointLights_[i].color);
        dataHandler->Save<Vector3>(prefix + "position", pointLights_[i].position);
        dataHandler->Save<int32_t>(prefix + "HalfLambert", pointLights_[i].HalfLambert);
        dataHandler->Save<int32_t>(prefix + "BlinnPhong", pointLights_[i].BlinnPhong);
        dataHandler->Save<float>(prefix + "intensity", pointLights_[i].intensity);
        dataHandler->Save<float>(prefix + "radius", pointLights_[i].radius);
        dataHandler->Save<float>(prefix + "decay", pointLights_[i].decay);
    }

    // Spot Lights
    dataHandler->Save<int32_t>("spotLight_count", static_cast<int32_t>(spotLights_.size()));
    for (size_t i = 0; i < spotLights_.size(); ++i) {
        std::string prefix = std::format("spotLight_{:02d}_", i);
        dataHandler->Save<bool>(prefix + "active", spotLights_[i].active);
        dataHandler->Save<Vector4>(prefix + "color", spotLights_[i].color);
        dataHandler->Save<Vector3>(prefix + "position", spotLights_[i].position);
        dataHandler->Save<Vector3>(prefix + "direction", spotLights_[i].direction);
        dataHandler->Save<int32_t>(prefix + "HalfLambert", spotLights_[i].HalfLambert);
        dataHandler->Save<int32_t>(prefix + "BlinnPhong", spotLights_[i].BlinnPhong);
        dataHandler->Save<float>(prefix + "intensity", spotLights_[i].intensity);
        dataHandler->Save<float>(prefix + "distance", spotLights_[i].distance);
        dataHandler->Save<float>(prefix + "cosAngle", spotLights_[i].cosAngle);
        dataHandler->Save<float>(prefix + "decay", spotLights_[i].decay);
    }
    dataHandler->Flush();
    ImGuiNotification::Post("ライトデータを保存しました: " + fileName, {0.2f, 0.8f, 0.2f, 1.0f});
}

void LightGroup::LoadLightData(const std::string &fileName) {
    auto dataHandler = std::make_unique<DataHandler>("LightGroup", fileName);

    // Directional Light
    isDirectionalLight_ = dataHandler->Load<bool>("directional_active", true);
    directionalLightData_->color = dataHandler->Load<Vector4>("directional_color", {1.0f, 1.0f, 1.0f, 1.0f});
    directionalLightData_->direction = dataHandler->Load<Vector3>("directional_direction", {0.0f, -1.0f, 0.0f});
    directionalLightData_->HalfLambert = dataHandler->Load<int32_t>("directional_HalfLambert", false);
    directionalLightData_->BlinnPhong = dataHandler->Load<int32_t>("directional_BlinnPhong", true);
    directionalLightData_->intensity = dataHandler->Load<float>("directional_intensity", 1.0f);

    // Point Lights
    pointLights_.clear();
    int32_t pointLightCount = dataHandler->Load<int32_t>("pointLight_count", 0);
    for (int32_t i = 0; i < pointLightCount && i < MAX_POINT_LIGHTS; ++i) {
        std::string prefix = std::format("pointLight_{:02d}_", i);
        PointLight light = {};
        light.active = dataHandler->Load<bool>(prefix + "active", true);
        light.color = dataHandler->Load<Vector4>(prefix + "color", {1.0f, 1.0f, 1.0f, 1.0f});
        light.position = dataHandler->Load<Vector3>(prefix + "position", {0.0f, 2.0f, 0.0f});
        light.HalfLambert = dataHandler->Load<int32_t>(prefix + "HalfLambert", false);
        light.BlinnPhong = dataHandler->Load<int32_t>(prefix + "BlinnPhong", true);
        light.intensity = dataHandler->Load<float>(prefix + "intensity", 1.0f);
        light.radius = dataHandler->Load<float>(prefix + "radius", 5.0f);
        light.decay = dataHandler->Load<float>(prefix + "decay", 1.0f);
        pointLights_.push_back(light);
    }

    // Spot Lights
    spotLights_.clear();
    int32_t spotLightCount = dataHandler->Load<int32_t>("spotLight_count", 0);
    for (int32_t i = 0; i < spotLightCount && i < MAX_SPOT_LIGHTS; ++i) {
        std::string prefix = std::format("spotLight_{:02d}_", i);
        SpotLight light = {};
        light.active = dataHandler->Load<bool>(prefix + "active", true);
        light.color = dataHandler->Load<Vector4>(prefix + "color", {1.0f, 1.0f, 1.0f, 1.0f});
        light.position = dataHandler->Load<Vector3>(prefix + "position", {0.0f, 2.0f, 0.0f});
        light.direction = dataHandler->Load<Vector3>(prefix + "direction", {0.0f, -1.0f, 0.0f});
        light.HalfLambert = dataHandler->Load<int32_t>(prefix + "HalfLambert", false);
        light.BlinnPhong = dataHandler->Load<int32_t>(prefix + "BlinnPhong", true);
        light.intensity = dataHandler->Load<float>(prefix + "intensity", 1.0f);
        light.distance = dataHandler->Load<float>(prefix + "distance", 10.0f);
        light.cosAngle = dataHandler->Load<float>(prefix + "cosAngle", 0.7f);
        light.decay = dataHandler->Load<float>(prefix + "decay", 1.0f);
        spotLights_.push_back(light);
    }
    ImGuiNotification::Post("ライトデータを読み込みました: " + fileName, {0.2f, 0.8f, 0.8f, 1.0f});
}

void LightGroup::DrawLightVisualization() {
    if (!showLightVisualization_)
        return;

    DrawLine3D *drawLine = DrawLine3D::GetInstance();

    // 平行光源の可視化
    if (isDirectionalLight_ && directionalLightData_->active) {
        Vector4 dirColor = {directionalLightData_->color.x, directionalLightData_->color.y, directionalLightData_->color.z, 0.8f};

        // 複数の平行線で方向を表示
        for (int i = -2; i <= 2; i++) {
            for (int j = -2; j <= 2; j++) {
                Vector3 startPos = {i * 5.0f, 20.0f, j * 5.0f};
                Vector3 endPos = startPos + directionalLightData_->direction * 15.0f;
                drawLine->SetPoints(startPos, endPos, dirColor);
            }
        }
    }

    // ポイントライトの可視化
    for (size_t i = 0; i < pointLights_.size(); ++i) {
        if (!pointLights_[i].active)
            continue;

        const PointLight &light = pointLights_[i];
        Vector4 lightColor = {light.color.x, light.color.y, light.color.z, 0.8f};

        // ライト位置に球体を描画
        drawLine->DrawSphere(light.position, lightColor, 0.3f, 8);

        // 光の範囲を表示（ワイヤーフレーム球体）
        drawLine->DrawSphere(light.position, {lightColor.x, lightColor.y, lightColor.z, 0.3f}, light.radius, 16);

        // 放射線を描画（8方向）
        for (int dir = 0; dir < 8; dir++) {
            float angle = (float)dir * (3.14159f * 2.0f) / 8.0f;
            Vector3 rayDirection = {cosf(angle), 0.0f, sinf(angle)};
            Vector3 rayEnd = light.position + rayDirection * light.radius;
            drawLine->SetPoints(light.position, rayEnd, {lightColor.x, lightColor.y, lightColor.z, 0.5f});
        }
    }

    // スポットライトの可視化
    for (size_t i = 0; i < spotLights_.size(); ++i) {
        if (!spotLights_[i].active)
            continue;

        const SpotLight &light = spotLights_[i];
        Vector4 lightColor = {light.color.x, light.color.y, light.color.z, 0.8f};

        // ライト位置に小さな球体
        drawLine->DrawSphere(light.position, lightColor, 0.3f, 8);

        // スポットライトの方向線（中央）
        Vector3 centerRay = light.position + light.direction * light.distance;
        drawLine->SetPoints(light.position, centerRay, lightColor);

        // スポットライトのコーン形状を描画
        float coneAngle = acosf(light.cosAngle); // cosから角度を計算
        float coneRadius = light.distance * tanf(coneAngle);

        // コーンの外周を8本の線で表現
        for (int edge = 0; edge < 8; edge++) {
            float angle = (float)edge * (3.14159f * 2.0f) / 8.0f;

            // 方向ベクトルに垂直な2つのベクトルを計算
            Vector3 right;
            if (abs(light.direction.y) < 0.9f) {
                right = Vector3(0, 1, 0).Cross(light.direction).Normalize();
            } else {
                right = Vector3(1, 0, 0).Cross(light.direction).Normalize();
            }
            Vector3 up = light.direction.Cross(right).Normalize();

            // コーンの端点を計算
            Vector3 coneOffset = (right * cosf(angle) + up * sinf(angle)) * coneRadius;
            Vector3 coneEnd = centerRay + coneOffset;

            // ライト位置からコーンの端点への線
            drawLine->SetPoints(light.position, coneEnd, {lightColor.x, lightColor.y, lightColor.z, 0.6f});

            // コーンの円周
            int nextEdge = (edge + 1) % 8;
            float nextAngle = (float)nextEdge * (3.14159f * 2.0f) / 8.0f;
            Vector3 nextConeOffset = (right * cosf(nextAngle) + up * sinf(nextAngle)) * coneRadius;
            Vector3 nextConeEnd = centerRay + nextConeOffset;
            drawLine->SetPoints(coneEnd, nextConeEnd, {lightColor.x, lightColor.y, lightColor.z, 0.4f});
        }
    }
}
} // namespace Hagine
