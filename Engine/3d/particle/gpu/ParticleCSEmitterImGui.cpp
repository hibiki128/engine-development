#define NOMINMAX
#include "ParticleCSEmitter.h"
#include "ParticleCSFieldManager.h"
#include "ParticleCSGroupManager.h"
#include <graphics/pipeline/ComputePipelineManager.h>
#include <Frame.h>
#include <light/LightGroup.h>
#include <line/LineRenderer.h>
#include <render/deferred/DeferredRenderer.h>
#include <shadow/ShadowMap.h>
#include <particle/ParticleCommon.h>
#include <debug/profiler/GpuProfiler.h>
#include <algorithm>
#include <random>
#include "../utility/debug/imgui/ImGuizmoManager.h"
#include "../utility/debug/imgui/ImGuiNotification.h"
// DebugUIHelper.h は ImGui:: を使うので imgui.h（ImGuizmoManager.h 経由）の後に include する
#include "../utility/debug/imgui/DebugUIHelper.h"
#include <object/base/BaseObject.h>
#include <transform/WorldTransform.h>

// エミッターの設定UI。実行時の処理は ParticleCSEmitter.cpp にある。
namespace Hagine {
void ParticleCSEmitter::DrawImGui()
{
#ifdef USE_IMGUI
    if (ImGui::BeginTabBar("EmitterTabBar"))
    {
        if (ImGui::BeginTabItem(name_.c_str()))
        {
            ImGuiStyle &style = ImGui::GetStyle();
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.14f, 0.15f, 1.00f));

            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.34f, 0.26f, 0.26f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.42f, 0.32f, 0.32f, 0.70f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.50f, 0.40f, 0.40f, 0.85f));

            if (ImGui::CollapsingHeader("エミッターデータ##EmitterData"))
            {
                ImGui::PopStyleColor(3);

                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
                ImGui::Text("エミッター設定:");
                ImGui::PopStyleColor();

                ImGui::Separator();

                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentGreen));

                ImGui::DragFloat("発生間隔##Freq", &pEmitterMeshData_->frequency, 0.001f, 0.001f, 10.0f);
                ImGui::DragFloat3("エミッタの座標##Translate", &pEmitterMeshData_->translate.x, 0.1f);

                Vector3 currentEuler = baseRotation_.ToEulerDegrees();
                ImGui::Text("現在の回転: %.1f° %.1f° %.1f°", currentEuler.x, currentEuler.y, currentEuler.z);

                static Vector3 deltaRotation = {0.0f, 0.0f, 0.0f};
                if (ImGui::DragFloat3("##EmitterRotation", &deltaRotation.x, 0.1f, -10.0f, 10.0f, "%.1f°"))
                {
                    Quaternion currentRotation = baseRotation_;
                    Quaternion deltaQuatX = Quaternion::FromAxisAngle(Vector3(1, 0, 0), deltaRotation.x * std::numbers::pi_v<float> / 180.0f);
                    Quaternion deltaQuatY = Quaternion::FromAxisAngle(Vector3(0, 1, 0), deltaRotation.y * std::numbers::pi_v<float> / 180.0f);
                    Quaternion deltaQuatZ = Quaternion::FromAxisAngle(Vector3(0, 0, 1), deltaRotation.z * std::numbers::pi_v<float> / 180.0f);
                    Quaternion deltaQuat = deltaQuatY * deltaQuatX * deltaQuatZ;
                    Quaternion newRotation = currentRotation * deltaQuat;
                    baseRotation_ = newRotation.Normalize();
                    deltaRotation = {0.0f, 0.0f, 0.0f};
                }

                ImGui::SameLine();
                if (ImGui::Button("リセット##EmitterRotation"))
                {
                    baseRotation_ = Quaternion::IdentityQuaternion();
                    deltaRotation = {0.0f, 0.0f, 0.0f};
                }

                {
                    bool bb = billboardEmitter_;
                    if (ImGui::Checkbox("ビルボード（常にカメラへ正対）##EmitterBillboard", &bb))
                        billboardEmitter_ = bb;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("発生形状の向きを毎フレームカメラの回転で置き換える。\n"
                                          "上の回転はカメラ空間でのオフセットとして残る。\n"
                                          "渦などの「基準空間」を エミッター にしておくと、\n"
                                          "発生形状と渦の軸が一緒にカメラへ追従して\n"
                                          "どの角度から見ても同じ動きになる。");
                    if (billboardEmitter_)
                    {
                        Vector3 resolved = pEmitterMeshData_->rotation.ToEulerDegrees();
                        ImGui::TextDisabled("  解決後: %.1f° %.1f° %.1f°", resolved.x, resolved.y, resolved.z);
                    }
                }

                ImGui::DragFloat3("エミッタの大きさ##Scale", &pEmitterMeshData_->scale.x, 0.1f);

                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::Separator();

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
                ImGui::Text("アンカーポイント:");
                ImGui::PopStyleColor();

                ImGui::DragFloat3("基準点##AnchorPoint", &pEmitterMeshData_->anchorPoint.x, 0.01f, 0.0f, 1.0f, "%.2f");

                ImGui::Spacing();
                ImGui::Separator();

                // 発生位置設定（ラジオボタンで3択）
                if (pEmitterMeshData_->triangleCount > 0 || pEmitterMeshData_->edgeCount > 0)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
                    ImGui::Text("発生位置:");
                    ImGui::PopStyleColor();

                    ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentBlue));
                    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentOrange);

                    int emitMode = static_cast<int>(pEmitterMeshData_->emitFromSurface);

                    ImGui::RadioButton("内部から発生##EmitInternal", &emitMode, 0);
                    ImGui::SameLine();
                    ImGui::RadioButton("表面から発生##EmitSurface", &emitMode, 1);
                    ImGui::SameLine();
                    ImGui::RadioButton("線上から発生##EmitEdge", &emitMode, 2);

                    pEmitterMeshData_->emitFromSurface = static_cast<uint32_t>(emitMode);

                    ImGui::PopStyleColor(2);

                    // ツールチップ
                    if (ImGui::IsItemHovered())
                    {
                        const char *tooltip = "";
                        if (emitMode == 0)
                            tooltip = "メッシュの内側全体からパーティクルが発生します";
                        else if (emitMode == 1)
                            tooltip = "メッシュの表面からパーティクルが発生します";
                        else if (emitMode == 2)
                            tooltip = "メッシュのエッジ（線）上からパーティクルが発生します";
                        ImGui::SetTooltip("%s", tooltip);
                    }
                }

                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::FrameBg(DebugTheme::kAccentGreen));

                // モデル情報表示
                if (pEmitterMeshData_->triangleCount > 0)
                {
                    ImGui::Spacing();
                    ImGui::Text("三角形数: %d", pEmitterMeshData_->triangleCount);
                    if (pEmitterMeshData_->edgeCount > 0)
                    {
                        ImGui::Text("エッジ数: %d", pEmitterMeshData_->edgeCount);
                    }
                    if (!modelPath_.empty())
                    {
                        ImGui::Text("モデル: %s", modelPath_.c_str());
                    }
                    else if (primitiveType_ != PrimitiveType::None)
                    {
                        ImGui::Text("プリミティブタイプ");

                        // 円形プリミティブ(Ring/Sphere/Cylinder/Cone)は分割数・形状を調整できる。
                        if (IsParametricPrimitive(primitiveType_))
                        {
                            ImGui::Spacing();
                            ImGui::TextDisabled("形状パラメータ");
                            bool rebuild = false;

                            int divide = static_cast<int>(primitiveParams_.divide);
                            ImGui::SetNextItemWidth(180.0f);
                            if (ImGui::DragInt("分割数##primDivide", &divide, 0.5f, 3, 256))
                            {
                                primitiveParams_.divide = static_cast<uint32_t>(divide < 3 ? 3 : divide);
                            }
                            if (ImGui::IsItemDeactivatedAfterEdit())
                                rebuild = true;
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("円周方向の分割数。多いほど滑らか（頂点・三角形が増えます）");

                            // Cylinder は高さ方向の分割数（格子の横リング本数）を調整できる。
                            if (primitiveType_ == PrimitiveType::Cylinder)
                            {
                                int heightDivide = static_cast<int>(primitiveParams_.heightDivide);
                                ImGui::SetNextItemWidth(180.0f);
                                if (ImGui::DragInt("高さ分割##primHeightDivide", &heightDivide, 0.5f, 1, 256))
                                {
                                    primitiveParams_.heightDivide = static_cast<uint32_t>(heightDivide < 1 ? 1 : heightDivide);
                                }
                                if (ImGui::IsItemDeactivatedAfterEdit())
                                    rebuild = true;
                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip("高さ方向の分割数。線上発生モードで横リングの本数が増え格子状になります（横リング = 高さ分割 + 1 本）");
                            }

                            // リングは外半径・内半径（円の幅 = 外 - 内）を調整できる。
                            if (primitiveType_ == PrimitiveType::Ring)
                            {
                                ImGui::SetNextItemWidth(180.0f);
                                ImGui::DragFloat("外半径##primOuter", &primitiveParams_.ringOuterRadius, 0.01f, 0.01f, 100.0f, "%.3f");
                                if (ImGui::IsItemDeactivatedAfterEdit())
                                    rebuild = true;
                                ImGui::SetNextItemWidth(180.0f);
                                ImGui::DragFloat("内半径##primInner", &primitiveParams_.ringInnerRadius, 0.01f, 0.0f, 100.0f, "%.3f");
                                if (ImGui::IsItemDeactivatedAfterEdit())
                                    rebuild = true;
                                if (ImGui::IsItemHovered())
                                    ImGui::SetTooltip("円の幅 = 外半径 - 内半径");
                            }

                            if (rebuild)
                            {
                                // 内半径が外半径を超えないようクランプしてから作り直す。
                                if (primitiveParams_.ringInnerRadius > primitiveParams_.ringOuterRadius)
                                    primitiveParams_.ringInnerRadius = primitiveParams_.ringOuterRadius;
                                RebuildPrimitiveModel();
                            }
                        }
                    }
                }

                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
                ImGui::Checkbox("自動更新##Auto", &isAuto_);
                ImGui::SameLine();
                {
                    bool noGroups = particleGroups_.empty();
                    bool disabled = isAuto_ || noGroups;
                    uint32_t cnt = noGroups ? 0u : particleGroups_[0]->GetSettingsData()->emitCount;
                    std::string btnLabel = "一回発生 (x" + std::to_string(cnt) + ")##EmitOnce";
                    if (disabled)
                        ImGui::BeginDisabled();
                    if (ImGui::Button(btnLabel.c_str()))
                    {
                        EmitOnce();
                        ImGuiNotification::Post("パーティクルを " + std::to_string(cnt) + " 個発生しました", {0.3f, 1.0f, 0.5f, 1.0f});
                    }
                    if (disabled)
                        ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        if (isAuto_)
                            ImGui::SetTooltip("自動更新がONのため使用できません\n（自動更新をOFFにしてください）");
                        else if (noGroups)
                            ImGui::SetTooltip("パーティクルグループが未設定です\n（グループを追加してください）");
                        else if (cnt == 0)
                            ImGui::SetTooltip("発生数が0のため効果がありません\n（発生数設定を確認してください）");
                    }
                }
                if (ImGui::Checkbox("ギズモ選択", &isGizmoSelectable_))
                {
                    ImGuizmoManager::GetInstance()->SetSelectable(name_, isGizmoSelectable_);
                }
                ImGui::Checkbox("エミッター表示##Visible", &isVisible_);
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::PopStyleColor(3);
            }

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
            ImGui::TextUnformatted("フィールド影響設定");
            ImGui::PopStyleColor();
            bool rf = receiveFields_;
            if (ImGui::Checkbox("フィールドの影響を受ける", &rf))
            {
                receiveFields_ = rf;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("オフにするとParticleFieldManagerのフィールドが\nこのエミッターに作用しなくなります");
            }

            if (receiveFields_)
            {
                ImGui::Indent();
                ImGui::PushItemWidth(120.0f);
                int fgid = fieldGroupId_;
                if (ImGui::DragInt("フィールドグループID##fgid", &fgid, 1, -1, 255))
                {
                    fieldGroupId_ = std::max(-1, fgid);
                }
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "-1 = 全フィールドから影響を受ける（デフォルト）\n"
                        "0以上 = 同じIDのフィールドのみから影響を受ける");
                if (fieldGroupId_ == -1)
                {
                    ImGui::TextDisabled("  全フィールド対象");
                }
                else
                {
                    ImGui::Text("  ID: %d のフィールドのみ対象", fieldGroupId_);
                }

                ImGui::Spacing();
                bool eofc = emitOnlyOnFieldContact_;
                if (ImGui::Checkbox("フィールド接触部分にのみ発生##EmitOnFieldContact", &eofc))
                {
                    emitOnlyOnFieldContact_ = eofc;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(
                        "ON : 「接触Emit」が有効なフィールドと接触している\n"
                        "     エミッター表面にのみパーティクルを発生させます\n"
                        "OFF: 通常通りエミッター全体からランダムEmitします");
                }

                if (emitOnlyOnFieldContact_)
                {
                    ImGui::Indent();

                    // 発生数・間隔・寿命はフィールド側に一本化されている。
                    // ここでは対象フィールドの状態を読み取り専用で表示し、迷子を防ぐ。
                    ImGui::TextDisabled("発生数・間隔・寿命はフィールド側の「接触Emit」で設定します");

                    int matchCount = 0;
                    for (const auto &field : ParticleCSFieldManager::GetInstance()->GetFields())
                    {
                        if (!field.enabled || !field.data.enableEmitSpawn)
                            continue;
                        bool groupMatch = (field.data.groupId == -1) ||
                                          (fieldGroupId_ == -1) ||
                                          (field.data.groupId == fieldGroupId_);
                        if (!groupMatch)
                            continue;
                        ++matchCount;
                        if (field.emitSpawnInterval > 0.0f)
                        {
                            ImGui::Text("  %s : %u個 / %.2fs間隔",
                                        field.name.c_str(), field.emitSpawnCount, field.emitSpawnInterval);
                        }
                        else
                        {
                            ImGui::Text("  %s : %u個 / 毎フレーム",
                                        field.name.c_str(), field.emitSpawnCount);
                        }
                    }
                    if (matchCount == 0)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.6f, 0.3f, 1.0f));
                        ImGui::TextUnformatted("  対象フィールドがありません（発生しません）");
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip(
                                "パーティクルフィールド管理ウィンドウで\n"
                                "「接触Emit」を有効にしたフィールドを用意し、\n"
                                "グループIDをこのエミッターと合わせてください。");
                    }

                    ImGui::Unindent();
                }

                ImGui::Unindent();
            }

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
            ImGui::TextUnformatted("発光（周囲を照らす）");
            ImGui::PopStyleColor();
            ImGui::Checkbox("発光する##LightEnabled", &lightEnabled_);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "エミッター位置に点光源を置き、地面やキャラクターを照らします。\n"
                    "エネルギー弾が周囲を照らすような演出に使えます。\n"
                    "※ 光源枠はシーンのポイントライトと共有（最大%d個）。\n"
                    "  溢れた場合はカメラに近く明るいものが優先されます。",
                    MAX_POINT_LIGHTS);
            }

            if (lightEnabled_)
            {
                ImGui::Indent();
                ImGui::ColorEdit4("光の色##LightColor", &lightColor_.x, ImGuiColorEditFlags_NoInputs);
                ImGui::PushItemWidth(160.0f);
                ImGui::DragFloat("強さ##LightIntensity", &lightIntensity_, 0.05f, 0.0f, 50.0f, "%.2f");
                ImGui::DragFloat("半径##LightRadius", &lightRadius_, 0.1f, 0.0f, 200.0f, "%.2f");
                ImGui::DragFloat("減衰##LightDecay", &lightDecay_, 0.05f, 0.0f, 10.0f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("大きいほど光が中心付近に集まります");
                ImGui::DragFloat3("オフセット##LightOffset", &lightOffset_.x, 0.05f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("エミッター位置からのずれ。エミッターの向きに追従します");
                ImGui::PopItemWidth();

                ImGui::Checkbox("粒子が無いときは消灯##LightFollow", &lightFollowParticles_);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "オンだと発生が止まり粒子が消えたところで自動的に消灯します。\n"
                        "オフにするとエミッターが有効な間ずっと光り続けます。");

                ImGui::Unindent();
            }

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
            ImGui::TextUnformatted("粒子ごとの発光（粒子1個1個が光源になる）");
            ImGui::PopStyleColor();
            ImGui::Checkbox("粒子を光源にする##ParticleLightEnabled", &particleLightEnabled_);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "生きている粒子そのものを点光源にして周囲を照らします。\n"
                    "弾の軌跡に沿って地面が光るような演出に使えます。\n"
                    "※ ディファードレンダリングがONのときだけ効きます。\n"
                    "※ 光源が増えるほど重くなるので、間引きと上限で必ず絞ってください。");
            }

            if (particleLightEnabled_)
            {
                ImGui::Indent();

                if (!DeferredRenderer::GetInstance()->IsEnabled())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.6f, 0.3f, 1.0f));
                    ImGui::TextWrapped("※ ディファードレンダリングがOFFなので反映されません（描画システムでON）");
                    ImGui::PopStyleColor();
                }

                ImGui::PushItemWidth(160.0f);
                int stride = static_cast<int>(particleLightStride_);
                if (ImGui::DragInt("間引き##ParticleLightStride", &stride, 0.5f, 1, 1024))
                {
                    particleLightStride_ = static_cast<uint32_t>((std::max)(1, stride));
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("この個数ごとに1粒を光源にします。\n1で全粒子（重い）、大きいほど軽くなります。");

                int maxCount = static_cast<int>(particleLightMaxCount_);
                if (ImGui::DragInt("光源の上限##ParticleLightMax", &maxCount, 1.0f, 0, 2048))
                {
                    particleLightMaxCount_ = static_cast<uint32_t>((std::max)(0, maxCount));
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("1グループあたりに作る光源の最大数。\nシーン全体の上限は %u 個です。",
                                      LightGroup::kMaxBufferedPointLights);

                ImGui::DragFloat("強さ##ParticleLightIntensity", &particleLightIntensity_, 0.02f, 0.0f, 20.0f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("1粒あたりの明るさ。粒子のアルファが掛かるので消え際は自然に暗くなります。");
                ImGui::DragFloat("半径##ParticleLightRadius", &particleLightRadius_, 0.05f, 0.0f, 50.0f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("1粒あたりの光の届く距離。大きいほど1タイルに乗る光源が増えて重くなります。");
                ImGui::DragFloat("減衰##ParticleLightDecay", &particleLightDecay_, 0.05f, 0.0f, 10.0f, "%.2f");
                ImGui::DragFloat("表示距離##ParticleLightCull", &particleLightCullDistance_, 0.5f, 0.0f, 500.0f, "%.1f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("カメラからこの距離を超える粒子は光源にしません（0で無効）。");
                ImGui::PopItemWidth();

                ImGui::Checkbox("粒子の色を使う##ParticleLightUseColor", &particleLightUseParticleColor_);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("オンだと粒子の色がそのまま光の色になります。\nオフにすると下の固定色で照らします。");
                if (!particleLightUseParticleColor_)
                {
                    ImGui::ColorEdit4("光の色##ParticleLightColor", &particleLightColor_.x, ImGuiColorEditFlags_NoInputs);
                }

                ImGui::Unindent();
            }

            ImGui::Spacing();

            // パーティクルグループ設定セクション（既存のコードと同じ）
            if (!particleGroups_.empty())
            {
                static int selectedGroupIndex = 0;
                if (selectedGroupIndex >= static_cast<int>(particleGroups_.size()))
                {
                    selectedGroupIndex = 0;
                }

                std::vector<std::string> groupNames;
                for (const auto &group : particleGroups_)
                {
                    groupNames.push_back(group->GetGroupName());
                }

                std::vector<const char *> groupNameCStrs;
                for (auto &n : groupNames)
                    groupNameCStrs.push_back(n.c_str());

                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.18f, 0.22f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.34f, 0.48f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.44f, 0.60f, 0.95f));

                ImGui::SetNextItemWidth(200.0f);
                ImGui::Combo("選択中のグループ##GroupCombo", &selectedGroupIndex, groupNameCStrs.data(), static_cast<int>(groupNameCStrs.size()));

                ImGui::PopStyleColor(3);

                if (selectedGroupIndex >= 0 && selectedGroupIndex < static_cast<int>(particleGroups_.size()))
                {
                    ImGui::Separator();
                    particleGroups_[selectedGroupIndex]->SetFrequency(pEmitterMeshData_->frequency);
                    particleGroups_[selectedGroupIndex]->DrawImGui();
                }
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.6f, 0.6f, 1.0f));
                ImGui::Text("GPUパーティクルグループがありません");
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // グループ管理セクション
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.28f, 0.32f, 0.40f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.34f, 0.40f, 0.50f, 0.70f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.42f, 0.48f, 0.58f, 0.85f));

            if (ImGui::CollapsingHeader("GPUグループ管理##GPUGroupManagement"))
            {
                ImGui::PopStyleColor(3);

                ImGui::Spacing();

                // 遅延生成対応: 実体(GPUバッファ)未確保のグループも選べるよう、
                // ロード済みテンプレートではなく登録簿の全グループ名から一覧を作る。
                auto allGroupNames = ParticleCSGroupManager::GetInstance()->GetAllGroupNames();

                std::vector<std::string> availableNames;
                std::vector<std::string> attachedNames;

                for (const auto &name : allGroupNames)
                {
                    if (particleGroupNames_.contains(name))
                    {
                        attachedNames.push_back(name);
                    }
                    else
                    {
                        availableNames.push_back(name);
                    }
                }

                static std::vector<int> leftSelected;
                static std::vector<int> rightSelected;

                std::vector<const char *> availableItems;
                for (auto &name : availableNames)
                    availableItems.push_back(name.c_str());

                std::vector<const char *> attachedItems;
                for (auto &name : attachedNames)
                    attachedItems.push_back(name.c_str());

                leftSelected.erase(std::remove_if(leftSelected.begin(), leftSelected.end(),
                                                  [&](int i) { return i >= static_cast<int>(availableNames.size()); }),
                                   leftSelected.end());
                rightSelected.erase(std::remove_if(rightSelected.begin(), rightSelected.end(),
                                                   [&](int i) { return i >= static_cast<int>(attachedNames.size()); }),
                                    rightSelected.end());

                float width = ImGui::GetContentRegionAvail().x;
                float halfWidth = width * 0.45f;

                // ヘッダーテキストのスタイル
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextCaption);
                ImGui::Text("利用可能なGPUグループ");
                ImGui::SameLine(width - halfWidth - 50);
                ImGui::Text("アタッチ済みGPUグループ");
                ImGui::PopStyleColor();

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));

                // 左リスト用のスタイル設定
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.15f, 0.2f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.4f, 0.5f, 0.8f));

                ImGui::BeginChild("gpu_available_groups##GPUAvailableGroups", ImVec2(halfWidth, 200), true);

                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.28f, 0.34f, 0.42f, 0.55f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.34f, 0.42f, 0.52f, 0.70f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.40f, 0.50f, 0.60f, 0.85f));

                if (availableItems.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    ImGui::Text("利用可能なGPUグループがありません");
                    ImGui::PopStyleColor();
                }
                else
                {
                    for (int i = 0; i < availableItems.size(); ++i)
                    {
                        bool selected = std::find(leftSelected.begin(), leftSelected.end(), i) != leftSelected.end();
                        std::string selectableId = std::string(availableItems[i]) + "##GPUAvailable" + std::to_string(i);
                        if (ImGui::Selectable(selectableId.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
                        {
                            if (!ImGui::GetIO().KeyCtrl)
                                leftSelected.clear();

                            auto it = std::find(leftSelected.begin(), leftSelected.end(), i);
                            if (it != leftSelected.end())
                                leftSelected.erase(it);
                            else
                                leftSelected.push_back(i);

                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                            {
                                auto group = ParticleCSGroupManager::GetInstance()->GetParticleCSGroup(availableNames[i]);
                                AddParticleGroup(group);
                                leftSelected.clear();
                            }
                        }
                    }
                }

                ImGui::PopStyleColor(3);
                ImGui::EndChild();

                ImGui::SameLine();

                // 中央のボタン群
                ImGui::BeginGroup();
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 12));

                // ボタンのスタイル設定
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.40f, 0.30f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.50f, 0.38f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.32f, 0.58f, 0.44f, 1.0f));

                bool canMoveRight = !leftSelected.empty();
                if (!canMoveRight)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                }

                if (ImGui::Button("追加 >>##GPUAddButton", ImVec2(80, 35)) && canMoveRight)
                {
                    for (int idx : leftSelected)
                    {
                        auto group = ParticleCSGroupManager::GetInstance()->GetParticleCSGroup(availableNames[idx]);
                        AddParticleGroup(group);
                    }
                    leftSelected.clear();
                }

                if (!canMoveRight)
                {
                    ImGui::PopStyleColor(3);
                }

                bool canMoveLeft = !rightSelected.empty();
                if (!canMoveLeft)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
                }

                if (ImGui::Button("<< 削除##GPURemoveButton", ImVec2(80, 35)) && canMoveLeft)
                {
                    for (int idx : rightSelected)
                    {
                        RemoveParticleGroup(attachedNames[idx]);
                    }
                    rightSelected.clear();
                }

                if (!canMoveLeft)
                {
                    ImGui::PopStyleColor(3);
                }

                ImGui::PopStyleColor(3); // Button colors
                ImGui::PopStyleVar();
                ImGui::EndGroup();

                ImGui::SameLine();

                ImGui::BeginChild("gpu_attached_groups##GPUAttachedGroups", ImVec2(halfWidth, 200), true);

                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.44f, 0.36f, 0.26f, 0.55f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.52f, 0.42f, 0.30f, 0.70f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.58f, 0.48f, 0.36f, 0.85f));

                if (attachedItems.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    ImGui::Text("アタッチされたGPUグループがありません");
                    ImGui::PopStyleColor();
                }
                else
                {
                    for (int i = 0; i < attachedItems.size(); ++i)
                    {
                        bool selected = std::find(rightSelected.begin(), rightSelected.end(), i) != rightSelected.end();
                        std::string selectableId = std::string(attachedItems[i]) + "##GPUAttached" + std::to_string(i);
                        if (ImGui::Selectable(selectableId.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick))
                        {
                            if (!ImGui::GetIO().KeyCtrl)
                                rightSelected.clear();

                            auto it = std::find(rightSelected.begin(), rightSelected.end(), i);
                            if (it != rightSelected.end())
                                rightSelected.erase(it);
                            else
                                rightSelected.push_back(i);

                            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                            {
                                RemoveParticleGroup(attachedNames[i]);
                                rightSelected.clear();
                            }
                        }
                    }
                }

                ImGui::PopStyleColor(3);
                ImGui::EndChild();

                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();

                ImGui::Spacing();

                // 操作説明
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                ImGui::Text("操作: Ctrlキー + クリックで複数選択, ダブルクリックで追加/削除");
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::PopStyleColor(3);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ファイル操作セクション
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.38f, 0.32f, 0.26f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.46f, 0.38f, 0.30f, 0.70f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.52f, 0.44f, 0.36f, 0.85f));

            if (ImGui::CollapsingHeader("GPUファイル操作##GPUFileOperations"))
            {
                ImGui::PopStyleColor(3);

                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.34f, 0.48f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.44f, 0.60f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.36f, 0.52f, 0.70f, 1.0f));

                if (ImGui::Button("GPU設定を保存##GPUSaveButton", ImVec2(120, 35)))
                {
                    SaveSetting();
                    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("ParticleCS", name_);
                    data->Flush();
                    ImGuiNotification::Post("パーティクル設定を保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
                }
                ImGui::PopStyleColor(3);

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("現在のGPUパーティクル設定をファイルに保存します");
                }

                ImGui::Spacing();
            }
            else
            {
                ImGui::PopStyleColor(3);
            }

            // メインウィンドウの背景色をポップ
            ImGui::PopStyleColor();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
#endif // USE_IMGUI
}
} // namespace Hagine
