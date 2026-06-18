#define NOMINMAX
#include "ViewProjection.h"
#include "Data/DataHandler.h"
#include "Engine/Utility/Debug/ImGui/Debugui_improved.h"
#include "Engine/Utility/Debug/ImGui/ImGuiNotification.h"
#include "Frame.h"
#include "cmath"
#include "myMath.h"
#ifdef _DEBUG
#include <implot.h>
#endif
#include <type/Vector2.h>

namespace Hagine {
void ViewProjection::Initialize(std::string jsonFile) {
    // 各種行列を単位行列で初期化
    matView_ = MakeIdentity4x4();
    matProjection_ = MakeIdentity4x4();
    matWorld_ = MakeIdentity4x4();

    dxCommon_ = DirectXCommon::GetInstance();

    CreateConstBuffer();
    Map();
    UpdateMatrix();

    // 設定ファイルがある場合は読み込む
    Load(jsonFile);
}

void ViewProjection::CreateConstBuffer() {
    const UINT bufferSize = sizeof(ConstBufferDataViewProjection);
    constBuffer_ = dxCommon_->CreateBufferResource(bufferSize);
}

void ViewProjection::Map() {
    // 定数バッファをCPUからアクセス可能なメモリにマッピング
    HRESULT hr = constBuffer_->Map(0, nullptr, reinterpret_cast<void **>(&constMap_));
    if (FAILED(hr)) {
        // 必要に応じてエラー処理を記述
    }
}

void ViewProjection::UpdateMatrix() {
    // イージング処理がアクティブな場合
    if (isEasing_) {
        easingTime_ += Frame::DeltaTime();

        if (easingTime_ >= easingDuration_) {
            // イージング終了時に値を目標値で確定させる
            easingTime_ = easingDuration_;
            translation_ = targetTranslation_;
            eulerRotation_ = targetEulerRotation_;
            quateRotation_ = targetQuaternionRotation_;
            isEasing_ = false;
        } else {
            // 経過時間に基づき、イージング関数を適用して現在値を補間
            translation_ = ApplyEasing(currentEasingType_, startTranslation_, targetTranslation_, easingTime_, easingDuration_);
            eulerRotation_ = ApplyEasing(currentEasingType_, startEulerRotation_, targetEulerRotation_, easingTime_, easingDuration_);
            quateRotation_ = ApplyEasing(currentEasingType_, startQuaternionRotation_, targetQuaternionRotation_, easingTime_, easingDuration_);
        }
    }

    // ビュー・投影行列の更新および定数バッファへの転送
    UpdateViewMatrix();
    UpdateProjectionMatrix();
    TransferMatrix();
}

void ViewProjection::TransferMatrix() {
    // マッピングされたメモリへ現在値を書き込み、GPUへ送信
    if (constMap_) {
        constMap_->view = matView_;
        constMap_->projection = matProjection_;
        constMap_->cameraPos = translation_;
    }
}

void ViewProjection::UpdateViewMatrix() {
    Matrix4x4 worldMatrix;

    // クォータニオンまたはオイラー角の選択に基づいてアフィン行列を生成
    if (isUseQuaternion_) {
        worldMatrix = MakeAffineMatrix({1.0f, 1.0f, 1.0f}, quateRotation_, translation_);
    } else {
        worldMatrix = MakeAffineMatrix({1.0f, 1.0f, 1.0f}, eulerRotation_, translation_);
    }

    // ワールド行列を更新
    matWorld_ = worldMatrix;

    // ビュー行列はカメラのワールド変換の逆行列
    matView_ = Inverse(worldMatrix);
}

void ViewProjection::UpdateProjectionMatrix() {
    // 透視投影行列の作成
    matProjection_ = MakePerspectiveFovMatrix(fovAngleY_, aspectRatio, nearZ_, farZ_);
}

void ViewProjection::EaseCameraMove(EasingType easeType, const std::string &jsonName, float duration) {
    if (isEasing_) {
        return; // 既にイージング移動中であれば処理をスキップ
    }

    // 現在のカメラ状態を開始値として保存
    // (目標値は後続のLoad()処理で設定されることを想定)
    startTranslation_ = translation_;
    startEulerRotation_ = eulerRotation_;
    startQuaternionRotation_ = quateRotation_;

    // JSONから目標値を読み込み
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("Camera", jsonName);
    targetTranslation_ = data->Load<Vector3>("translation", translation_);
    targetEulerRotation_ = data->Load<Vector3>("eulerRotation", eulerRotation_);
    targetQuaternionRotation_ = data->Load("quateRotation", quateRotation_);

    // イージング設定
    currentEasingType_ = easeType;
    easingDuration_ = duration;
    easingTime_ = 0.0f;
    isEasing_ = true;
}

void ViewProjection::ShowDebugInfo() {
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);

    if (ImGui::Begin("カメラ設定##vpwin")) {
        // ====================================================
        // [1] Position / Rotation
        // ====================================================
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kHeaderBlue);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.45f, 0.60f, 0.78f, 0.35f});
        bool basicOpen = ImGui::CollapsingHeader("位置 / 回転##vpb",
                                                 ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(2);

        if (basicOpen) {
            ImGui::Indent(6.0f);

            // Quaternion switch
            ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);
            ImGui::Checkbox("クォータニオン使用##vpquatchk", &isUseQuaternion_);
            ImGui::PopStyleColor();
            ImGui::Spacing();

            // Position
            SectionHeader("[ カメラ位置 ]", DebugTheme::kAccentBlue);
            LabeledDrag3("移動 (X / Y / Z)", "##vptrans",
                         &translation_.x, 0.1f, -1000.f, 1000.f, "%.2f",
                         DebugTheme::kBgBlue);
            ImGui::Spacing();

            // Rotation
            SectionHeader("[ 回転 ]", DebugTheme::kAccentCyan);
            if (isUseQuaternion_) {
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("クォータニオン (X / Y / Z / W)");
                ImGui::PopStyleColor();
                ImGui::SetNextItemWidth(-1);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.42f, 0.66f, 0.68f, 0.12f});
                ImGui::DragFloat4("##vpquat", &quateRotation_.x, 0.01f, -1.f, 1.f, "%.3f");
                ImGui::PopStyleColor();

                float d = 180.f / std::numbers::pi_v<float>;
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::Text("  Euler (ref)  X:%.1f  Y:%.1f  Z:%.1f (deg)",
                            eulerRotation_.x * d, eulerRotation_.y * d, eulerRotation_.z * d);
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("オイラー角 (度)  X / Y / Z");
                ImGui::PopStyleColor();
                float r2d = 180.f / std::numbers::pi_v<float>;
                float d2r = std::numbers::pi_v<float> / 180.f;
                Vector3 deg = {eulerRotation_.x * r2d,
                               eulerRotation_.y * r2d,
                               eulerRotation_.z * r2d};
                ImGui::SetNextItemWidth(-1);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.42f, 0.66f, 0.68f, 0.12f});
                if (ImGui::DragFloat3("##vpeuler", &deg.x, 1.f, -360.f, 360.f, "%.1f"))
                    eulerRotation_ = {deg.x * d2r, deg.y * d2r, deg.z * d2r};
                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::Text("  Quat (ref)  %.3f  %.3f  %.3f  %.3f",
                            quateRotation_.x, quateRotation_.y,
                            quateRotation_.z, quateRotation_.w);
                ImGui::PopStyleColor();
            }

            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        // ====================================================
        // [2] Camera Parameters (FOV / Near / Far)
        // ====================================================
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kHeaderOrange);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.82f, 0.58f, 0.36f, 0.35f});
        bool paramOpen = ImGui::CollapsingHeader("カメラパラメータ##vpp");
        ImGui::PopStyleColor(2);

        if (paramOpen) {
            ImGui::Indent(6.0f);
            SectionHeader("[ 投影 ]", DebugTheme::kAccentOrange);

            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgOrange);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, DebugTheme::kAccentOrange);
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, DebugTheme::kAccentOrange);

            // FOV -- ラベル上、スライダー全幅
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("視野角 (FOV)  [度]");
            ImGui::PopStyleColor();
            float fovDeg = fovAngleY_ * 180.f / std::numbers::pi_v<float>;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##vpfov", &fovDeg, 10.f, 170.f, "%.1f"))
                fovAngleY_ = fovDeg * std::numbers::pi_v<float> / 180.f;

            ImGui::Spacing();

            // Aspect Ratio -- ラベル上、スライダー全幅
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("アスペクト比 (横 / 縦)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##vpasp", &aspectRatio, 0.1f, 5.f, "%.3f");

            ImGui::PopStyleColor(3);

            ImGui::Spacing();
            SectionHeader("[ クリップ距離 ]", DebugTheme::kAccentOrange);

            if (ImGui::BeginTable("NearFar", 2,
                                  ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
                ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthFixed, 48.0f);
                ImGui::TableSetupColumn("V", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("ニア");
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgOrange);
                ImGui::DragFloat("##vpnear", &nearZ_, 0.01f, 0.001f, 10.f, "%.3f");
                ImGui::PopStyleColor();

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("ファー");
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgOrange);
                ImGui::DragFloat("##vpfar", &farZ_, 1.f, 1.f, 10000.f, "%.1f");
                ImGui::PopStyleColor();

                ImGui::EndTable();
            }

            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        // ====================================================
        // [3] Calculated Info
        // ====================================================
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kHeaderGreen);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.45f, 0.68f, 0.52f, 0.35f});
        bool calcOpen = ImGui::CollapsingHeader("計算情報##vpcalc",
                                                ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(2);

        if (calcOpen) {
            ImGui::Indent(6.0f);
            Vector3 wp = {matWorld_.m[3][0], matWorld_.m[3][1], matWorld_.m[3][2]};
            float dist = sqrtf(wp.x * wp.x + wp.y * wp.y + wp.z * wp.z);

            ReadOnlyRow("カメラ座標", "%.2f  %.2f  %.2f", wp.x, wp.y, wp.z);
            ReadOnlyRow("原点からの距離", "%.3f", dist);

            // 距離をカラープログレスバーで可視化
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                                  dist < 100.f ? DebugTheme::kAccentGreen : dist < 500.f ? DebugTheme::kAccentOrange
                                                                                         : DebugTheme::kAccentRed);
            ImGui::ProgressBar(std::min(dist / 1000.f, 1.f), ImVec2(-1, 4), "");
            ImGui::PopStyleColor();

            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        // ====================================================
        // [4] Matrix Info (collapsible)
        // ====================================================
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgPurple);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.62f, 0.50f, 0.74f, 0.20f});
        bool matOpen = ImGui::CollapsingHeader("行列情報##vpmat");
        ImGui::PopStyleColor(2);

        if (matOpen) {
            ImGui::Indent(6.0f);
            auto ShowMat = [](const char *lbl, const Matrix4x4 &m) {
                ImGui::PushStyleColor(ImGuiCol_Header, {0.15f, 0.15f, 0.18f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.20f, 0.20f, 0.25f, 1.0f});
                if (ImGui::TreeNodeEx(lbl, ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextReadOnly);
                    for (int i = 0; i < 4; ++i)
                        ImGui::Text("[%d] %7.3f %7.3f %7.3f %7.3f",
                                    i, m.m[i][0], m.m[i][1], m.m[i][2], m.m[i][3]);
                    ImGui::PopStyleColor();
                    ImGui::TreePop();
                }
                ImGui::PopStyleColor(2);
            };
            ShowMat("ワールド行列##wm", matWorld_);
            ShowMat("ビュー行列##vm", matView_);
            ShowMat("プロジェクション行列##pm", matProjection_);
            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        // ====================================================
        // [5] Save / Load
        // ====================================================
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgOrange);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.82f, 0.58f, 0.36f, 0.20f});
        bool ioOpen = ImGui::CollapsingHeader("保存 / 読み込み##vpio");
        ImGui::PopStyleColor(2);

        if (ioOpen) {
            ImGui::Indent(6.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("JSONファイル名 (拡張子なし)");
            ImGui::PopStyleColor();
            static char fname[256] = "MyCamera";
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##vpfname", fname, sizeof(fname));
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, {0.20f, 0.45f, 0.20f, 0.8f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.25f, 0.55f, 0.25f, 0.9f});
            if (ImGui::Button("現在のカメラを保存##vpsv", ImVec2(-1, 0)))
                Save(fname);
            ImGui::Spacing();
            if (ImGui::Button("ファイルから読み込み##vpld", ImVec2(-1, 0)))
                Load(fname);
            ImGui::PopStyleColor(2);
            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        // ====================================================
        // [6] Camera Easing
        // ====================================================
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kHeaderYellow);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.80f, 0.72f, 0.42f, 0.35f});
        bool easeOpen = ImGui::CollapsingHeader("カメライージング##vpease");
        ImGui::PopStyleColor(2);

        if (easeOpen) {
            ImGui::Indent(6.0f);

            // 状態バッジ
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("状態:");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            StatusBadge(isEasing_ ? "イージング中" : "待機",
                        isEasing_ ? DebugTheme::kAccentYellow : DebugTheme::kAccentGreen);

            if (isEasing_) {
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, DebugTheme::kAccentYellow);
                ImGui::ProgressBar(easingTime_ / easingDuration_, ImVec2(-1, 6), "");
                ImGui::PopStyleColor();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::Text("  %.2f / %.2f sec", easingTime_, easingDuration_);
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();

            // Easing type
            static int easeType = 0;
            static const char *easeNames[] = {
                "Linear",
                "InSine", "OutSine", "InOutSine",
                "InQuad", "OutQuad", "InOutQuad",
                "InCubic", "OutCubic", "InOutCubic",
                "InQuart", "OutQuart", "InOutQuart",
                "InQuint", "OutQuint", "InOutQuint",
                "InCirc", "OutCirc", "InOutCirc",
                "InExpo", "OutExpo", "InOutExpo",
                "InBack", "OutBack", "InOutBack",
                "InElastic", "OutElastic", "InOutElastic",
                "InBounce", "OutBounce", "InOutBounce"};
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("イージング種別");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##vpeasetype", &easeType, easeNames, IM_ARRAYSIZE(easeNames));

            ImGui::Spacing();

            // Duration -- ラベル上、スライダー全幅
            static float dur = 2.0f;
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("所要時間 [秒]");
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgYellow);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, DebugTheme::kAccentYellow);
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, DebugTheme::kAccentYellow);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##vpeasedur", &dur, 0.1f, 10.f, "%.1f sec");
            ImGui::PopStyleColor(3);

            ImGui::Spacing();

            // Target JSON
            static char jname[256] = "CameraTarget";
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("目標JSONファイル名 (拡張子なし)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##vpjname", jname, sizeof(jname));

            ImGui::Spacing();

            // Start / Stop buttons
            bool canStart = !isEasing_ && strlen(jname) > 0;
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  canStart ? ImVec4{0.50f, 0.40f, 0.10f, 0.85f}
                                           : ImVec4{0.25f, 0.25f, 0.28f, 0.6f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  canStart ? ImVec4{0.65f, 0.55f, 0.15f, 0.90f}
                                           : ImVec4{0.25f, 0.25f, 0.28f, 0.6f});
            if (ImGui::Button("イージング開始##vpstart", ImVec2(-1, 0)) && canStart)
                EaseCameraMove(static_cast<EasingType>(easeType), jname, dur);
            ImGui::PopStyleColor(2);

            ImGui::PushStyleColor(ImGuiCol_Button, {0.50f, 0.15f, 0.15f, 0.85f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.70f, 0.20f, 0.20f, 0.90f});
            if (ImGui::Button("イージング停止##vpstop", ImVec2(-1, 0)) && isEasing_) {
                isEasing_ = false;
                easingTime_ = 0.f;
            }
            ImGui::PopStyleColor(2);

            // 実行中の詳細
            if (isEasing_) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgYellow);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.80f, 0.72f, 0.42f, 0.20f});
                if (ImGui::CollapsingHeader("イージング詳細##vpdet")) {
                    ImGui::Indent(6.0f);
                    auto R3 = [](const char *lbl, ImVec4 col, float x, float y, float z) {
                        ImGui::PushStyleColor(ImGuiCol_Text, col);
                        ImGui::Text("%-8s", lbl);
                        ImGui::PopStyleColor();
                        ImGui::SameLine(72.f);
                        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextReadOnly);
                        ImGui::Text("%.2f  %.2f  %.2f", x, y, z);
                        ImGui::PopStyleColor();
                    };
                    R3("開始", DebugTheme::kAccentCyan,
                       startTranslation_.x, startTranslation_.y, startTranslation_.z);
                    R3("目標", DebugTheme::kAccentRed,
                       targetTranslation_.x, targetTranslation_.y, targetTranslation_.z);
                    R3("現在", DebugTheme::kAccentYellow,
                       translation_.x, translation_.y, translation_.z);
                    ImGui::Unindent(6.0f);
                }
                ImGui::PopStyleColor(2);
            }

            ImGui::Spacing();
            SectionHeader("[ クイック操作 ]", DebugTheme::kTextDim);
            if (ImGui::SmallButton("現在位置を保存##vpqsave"))
                Save("CurrentCamera");
            ImGui::SameLine();
            if (ImGui::SmallButton("原点に戻す##vpqorig") && !isEasing_) {
                auto tmp = std::make_unique<DataHandler>("Camera", "TempOrigin");
                tmp->Save("translation", Vector3{0.f, 0.f, -10.f});
                tmp->Save("eulerRotation", Vector3{0.f, 0.f, 0.f});
                tmp->Save("quateRotation", Quaternion::IdentityQuaternion());
                EaseCameraMove(static_cast<EasingType>(easeType), "TempOrigin", dur);
            }

            ImGui::Unindent(6.0f);
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(5);
#endif // USE_IMGUI
}

void ViewProjection::Save(std::string jsonFile) {
    if (jsonFile.empty()) {
        return;
    }
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("Camera", jsonFile);
    data->Save("matView", matView_);
    data->Save("matProjection", matProjection_);
    data->Save("matWorld", matWorld_);
    data->Save("translation", translation_);
    data->Save("eulerRotation", eulerRotation_);
    data->Save("isUseQuaternion", isUseQuaternion_);
    data->Save("fov", fovAngleY_);
    data->Save("nearZ", nearZ_);
    data->Save("farZ", farZ_);
    data->Save("aspectRatio", aspectRatio);
    data->Save("quateRotation", quateRotation_);
    ImGuiNotification::Post("カメラ設定を保存しました: " + jsonFile, {0.2f, 0.8f, 0.2f, 1.0f});
}

void ViewProjection::Load(std::string jsonFile) {
    if (jsonFile.empty()) {
        return;
    }
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("Camera", jsonFile);
    matView_ = data->Load("matView", MakeIdentity4x4());
    matProjection_ = data->Load("matProjection", MakeIdentity4x4());
    matWorld_ = data->Load("matWorld", MakeIdentity4x4());
    translation_ = data->Load<Vector3>("translation", {0.0f, 0.0f, -10.0f});
    eulerRotation_ = data->Load<Vector3>("eulerRotation", {0.0f, 0.0f, 0.0f});
    quateRotation_ = data->Load("quateRotation", Quaternion::IdentityQuaternion());
    isUseQuaternion_ = data->Load("isUseQuaternion", true);
    fovAngleY_ = data->Load("fov", fovAngleY_);
    nearZ_ = data->Load("nearZ", nearZ_);
    farZ_ = data->Load("farZ", farZ_);
    aspectRatio = data->Load("aspectRatio", aspectRatio);
    ImGuiNotification::Post("カメラ設定を読み込みました: " + jsonFile, {0.2f, 0.8f, 0.8f, 1.0f});
}
} // namespace Hagine
