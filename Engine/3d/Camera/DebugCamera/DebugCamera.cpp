#include "DebugCamera.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "Mymath.h"
#ifdef _DEBUG
#include "imgui.h"
#include <implot.h>
#endif 
#include "Utility/Debug/ImGui/Debugui_improved.h"
#include "algorithm"

namespace Hagine {
void DebugCamera::Initialize(ViewProjection *viewProjection) {
    viewProjection_ = viewProjection;
    translation_ = viewProjection->translation_;
    isUseQuaternion_ = viewProjection->isUseQuaternion_;
    eulerRotation_ = viewProjection->eulerRotation_;
    quateRotation_ = viewProjection->quateRotation_;
    matRot_ = MakeIdentity4x4();
    isActive_ = false;
    lockCamera_ = false;
    mouseSensitivity_ = 0.003f;
    moveZspeed_ = 0.005f;
    mouse_ = {0.0f, 0.0f};
}

void DebugCamera::Update() {
    // アクティブ時のみデバッグ操作を適用
    if (isActive_) {
        // カメラ操作がロックされていない場合のみ移動計算
        if (!lockCamera_) {
            CameraMove(eulerRotation_, translation_, mouse_);
        }

        Matrix4x4 cameraMatrix;

        // 回転の定義形式に応じて行列を生成
        if (isUseQuaternion_) {
            cameraMatrix = MakeAffineMatrix(
                {1.0f, 1.0f, 1.0f},
                quateRotation_,
                translation_);
        } else {
            cameraMatrix = MakeAffineMatrix(
                {1.0f, 1.0f, 1.0f},
                eulerRotation_,
                translation_);
        }

        // ビュープロジェクションの状態を更新
        viewProjection_->matWorld_ = cameraMatrix;
        viewProjection_->matView_ = Inverse(cameraMatrix);
        viewProjection_->translation_ = translation_;
        viewProjection_->eulerRotation_ = eulerRotation_;
        viewProjection_->quateRotation_ = quateRotation_;
        viewProjection_->isUseQuaternion_ = isUseQuaternion_;
        
        // 投影行列の再計算
        viewProjection_->matProjection_ = MakePerspectiveFovMatrix(
            45.0f * std::numbers::pi_v<float> / 180.0f,
            float(WinApp::kClientWidth) / float(WinApp::kClientHeight),
            0.1f, 1000.0f);
    } else {
        // 非アクティブ時は通常更新
        viewProjection_->UpdateMatrix();
    }
}

void DebugCamera::CameraMove(Vector3 &cameraRotate, Vector3 &cameraTranslate, Vector2 &clickPosition) {
    // 現在の回転状態から各軸の向きを算出
    Matrix4x4 matRot;
    if (isUseQuaternion_) {
        matRot = QuaternionToMatrix4x4(quateRotation_);
    } else {
        matRot = MakeRotateXMatrix(eulerRotation_.x) * MakeRotateYMatrix(eulerRotation_.y);
    }

    // カメラのローカル軸(前後左右上下)の算出
    Vector3 forward = TransformNormal({0.0f, 0.0f, -2.0f}, matRot);
    Vector3 right = TransformNormal({2.0f, 0.0f, 0.0f}, matRot);
    Vector3 up = {0.0f, 2.0f, 0.0f};

    // キーボード操作による移動処理
    if (useKey_) {
        // コントロールキーで加速
        bool isDashing = Input::GetInstance()->PushKey(DIK_LCONTROL);
        float speed = moveZspeed_ * 10.0f * (isDashing ? 5.0f : 1.0f);

        // 各キー入力に基づく移動ベクトルの計算
        Vector3 move = {0, 0, 0};
        if (Input::GetInstance()->PushKey(DIK_W))
            move -= forward;
        if (Input::GetInstance()->PushKey(DIK_S))
            move += forward;
        if (Input::GetInstance()->PushKey(DIK_D))
            move += right;
        if (Input::GetInstance()->PushKey(DIK_A))
            move -= right;
        if (Input::GetInstance()->PushKey(DIK_SPACE))
            move += up;
        if (Input::GetInstance()->PushKey(DIK_LSHIFT))
            move -= up;

        // 算出された移動量を位置に加算
        translation_ += move * speed;
    }

    // ---------- マウスによるカメラ移動 ----------
    if (useMouse_) {
        // ホイールクリックによるXY移動
        if (Input::GetInstance()->IsPressMouse(2)) {
            Vector2 currentMousePos = Input::GetInstance()->GetMousePos();
            float deltaX = static_cast<float>(currentMousePos.x - clickPosition.x);
            float deltaY = static_cast<float>(currentMousePos.y - clickPosition.y);

            // X方向（右）とY方向（上）にカメラを平行移動
            translation_ -= right * deltaX * mouseSensitivity_;
            translation_ += up * deltaY * mouseSensitivity_;

            // マウス位置更新
            clickPosition = currentMousePos;
        }

        // ホイール回転でカメラの前後移動（Z軸）
        int wheel = Input::GetInstance()->GetWheel();
        if (wheel != 0) {
            translation_ -= forward * static_cast<float>(wheel) * mouseSensitivity_;
        }
    }

    // ---------- マウス右クリックによる視点回転 ----------
    if (Input::GetInstance()->IsPressMouse(1)) {
        Vector2 currentMousePos = Input::GetInstance()->GetMousePos();
        float deltaX = static_cast<float>(currentMousePos.x - clickPosition.x);
        float deltaY = static_cast<float>(currentMousePos.y - clickPosition.y);

        if (isUseQuaternion_) {
            // クォータニオンでの回転処理
            Quaternion yawRotation = Quaternion::FromAxisAngle({0, 1, 0}, deltaX * mouseSensitivity_);
            Quaternion pitchRotation = Quaternion::FromAxisAngle({1, 0, 0}, deltaY * mouseSensitivity_);

            quateRotation_ = yawRotation * pitchRotation * quateRotation_;
            quateRotation_ = quateRotation_.Normalize();

            // 参考用にオイラー角も更新
            eulerRotation_ = quateRotation_.ToEulerAngles();
        } else {
            // オイラー角での回転処理
            cameraRotate.y += deltaX * mouseSensitivity_;
            cameraRotate.x += deltaY * mouseSensitivity_;

            // 上下反転制限
            const float pi_2 = std::numbers::pi_v<float> / 2.0f - 0.01f;
            cameraRotate.x = std::clamp(cameraRotate.x, -pi_2, pi_2);

            // 参考用にクォータニオンも更新
            quateRotation_ = Quaternion::FromEulerAngles(eulerRotation_);
        }

        clickPosition = currentMousePos;
    } else if (!Input::GetInstance()->IsPressMouse(2)) {
        clickPosition = Input::GetInstance()->GetMousePos();
    }
}

void DebugCamera::imgui() {
#ifdef _DEBUG
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    // ---- Active toggle (always visible) ----
    {
        ImGui::PushStyleColor(ImGuiCol_CheckMark,
                              isActive_ ? DebugTheme::kAccentGreen : DebugTheme::kAccentRed);
        ImGui::Checkbox("デバッグカメラ使用##dbc", &isActive_);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 8);
        StatusBadge(isActive_ ? "使用中" : "未使用",
                    isActive_ ? DebugTheme::kAccentGreen : DebugTheme::kTextDim);
    }

    if (!isActive_) {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("  カメラは無効です。");
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
        return; // ここで終了 → 以降の重複セクションは表示されない
    }

    ImGui::Separator();
    ImGui::BeginChild("CamBody", ImVec2(0, 0), false);

    // ====================================================
    // [1] Position / Rotation
    // ====================================================
    ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kHeaderBlue);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.45f, 0.60f, 0.78f, 0.35f});
    bool posOpen = ImGui::CollapsingHeader("位置 / 回転##campr",
                                           ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(2);

    if (posOpen) {
        ImGui::Indent(6.0f);

        // ---- Position ----
        SectionHeader("[ 位置 ]", DebugTheme::kAccentBlue);

        // ラベルを上に、スライダーは全幅
        LabeledDrag3("移動 (X / Y / Z)", "##camtrans",
                     &translation_.x, 0.01f, -1000.f, 1000.f, "%.2f",
                     DebugTheme::kBgBlue);

        // 位置履歴グラフ
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgBlue);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.45f, 0.60f, 0.78f, 0.20f});
        if (ImGui::CollapsingHeader("位置履歴 (グラフ)##camhist")) {
            constexpr int kN = 100;
            static float hx[kN]{}, hy[kN]{}, hz[kN]{};
            static int head = 0, cnt = 0;
            hx[head] = translation_.x;
            hy[head] = translation_.y;
            hz[head] = translation_.z;
            head = (head + 1) % kN;
            if (cnt < kN)
                ++cnt;

            static float dx[kN], dy[kN], dz[kN];
            int s = (head - cnt + kN) % kN;
            for (int i = 0; i < cnt; ++i) {
                int id = (s + i) % kN;
                dx[i] = hx[id];
                dy[i] = hy[id];
                dz[i] = hz[id];
            }

            ImPlot::PushStyleColor(ImPlotCol_PlotBg, {0.08f, 0.08f, 0.10f, 1.0f});
            if (ImPlot::BeginPlot("##camposhist", ImVec2(-1, 65),
                                  ImPlotFlags_NoTitle | ImPlotFlags_NoLegend |
                                      ImPlotFlags_NoInputs | ImPlotFlags_NoFrame)) {
                ImPlot::SetupAxes(nullptr, nullptr,
                                  ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_AutoFit);
                ImPlot::SetupAxisLimits(ImAxis_X1, 0, kN, ImGuiCond_Always);
                ImPlot::PushStyleColor(ImPlotCol_Line, DebugTheme::kAccentRed);
                ImPlot::PlotLine("X", dx, cnt);
                ImPlot::PopStyleColor();
                ImPlot::PushStyleColor(ImPlotCol_Line, DebugTheme::kAccentGreen);
                ImPlot::PlotLine("Y", dy, cnt);
                ImPlot::PopStyleColor();
                ImPlot::PushStyleColor(ImPlotCol_Line, DebugTheme::kAccentBlue);
                ImPlot::PlotLine("Z", dz, cnt);
                ImPlot::PopStyleColor();
                ImPlot::EndPlot();
            }
            ImPlot::PopStyleColor();
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();

        // ---- Rotation ----
        SectionHeader("[ 回転 ]", DebugTheme::kAccentCyan);

        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);
        ImGui::Checkbox("クォータニオン使用##camquatchk", &isUseQuaternion_);
        ImGui::PopStyleColor();
        ImGui::Spacing();

        if (isUseQuaternion_) {
            // ラベルを上に置いてから全幅 DragFloat4
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("クォータニオン (X / Y / Z / W)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.42f, 0.66f, 0.68f, 0.12f});
            ImGui::DragFloat4("##camquat", &quateRotation_.x, 0.01f, -1.f, 1.f, "%.3f");
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            float d = 180.f / std::numbers::pi_v<float>;
            ImGui::Text("  Euler (ref)  X:%.1f  Y:%.1f  Z:%.1f (deg)",
                        eulerRotation_.x * d, eulerRotation_.y * d, eulerRotation_.z * d);
            ImGui::PopStyleColor();
        } else {
            // ラベルを上に置いてから全幅 DragFloat3
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
            if (ImGui::DragFloat3("##cameuler", &deg.x, 1.f, -360.f, 360.f, "%.1f"))
                eulerRotation_ = {deg.x * d2r, deg.y * d2r, deg.z * d2r};
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::Text("  Quat (ref)  %.3f  %.3f  %.3f  %.3f",
                        quateRotation_.x, quateRotation_.y,
                        quateRotation_.z, quateRotation_.w);
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        if (ImGui::SmallButton("位置リセット##cprst"))
            translation_ = {0.f, 0.f, -50.f};
        ImGui::SameLine();
        if (ImGui::SmallButton("回転リセット##crrst")) {
            eulerRotation_ = {};
            quateRotation_ = Quaternion::IdentityQuaternion();
        }

        ImGui::Unindent(6.0f);
        ImGui::Spacing();
    }

    // ====================================================
    // [2] Move Speed
    // ====================================================
    ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kHeaderOrange);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.82f, 0.58f, 0.36f, 0.35f});
    bool moveOpen = ImGui::CollapsingHeader("移動速度##cammv",
                                            ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(2);

    if (moveOpen) {
        ImGui::Indent(6.0f);

        // ラベルを上に → スライダーが全幅を使えて見切れない
        ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgOrange);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, DebugTheme::kAccentOrange);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, DebugTheme::kAccentOrange);

        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("カメラ移動速度 (Z軸)");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##camspd", &moveZspeed_, 0.001f, 1.0f, "%.3f");

        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("マウス感度 (回転ドラッグ)");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##camsen", &mouseSensitivity_, 0.001f, 0.1f, "%.3f");

        ImGui::PopStyleColor(3);

        if (ImGui::SmallButton("速度リセット##csrst")) {
            mouseSensitivity_ = 0.003f;
            moveZspeed_ = 0.005f;
        }
        ImGui::Unindent(6.0f);
        ImGui::Spacing();
    }

    // ====================================================
    // [3] Input / Control
    // ====================================================
    ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kHeaderPurple);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.62f, 0.50f, 0.74f, 0.35f});
    bool ctrlOpen = ImGui::CollapsingHeader("入力 / 操作##camctl",
                                            ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor(2);

    if (ctrlOpen) {
        ImGui::Indent(6.0f);
        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentPurple);
        ImGui::Checkbox("カメラロック (入力無効)##camlk", &lockCamera_);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("入力モード:");
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);
        if (ImGui::RadioButton("キーボード##rk", useKey_ && !useMouse_)) {
            useKey_ = true;
            useMouse_ = false;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("マウス##rm", useMouse_ && !useKey_)) {
            useMouse_ = true;
            useKey_ = false;
        }
        ImGui::PopStyleColor();

        if (!useKey_ && !useMouse_) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
            ImGui::TextUnformatted("  [!] 入力モード未選択");
            ImGui::PopStyleColor();
        }

        // 操作説明（1行）
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        if (useMouse_)
            ImGui::TextUnformatted("  ホイール:Z移動  |  中ドラッグ:XY移動  |  右ドラッグ:回転");
        else if (useKey_)
            ImGui::TextUnformatted("  WASD:移動  |  Space/Shift:上下  |  右ドラッグ:回転");
        ImGui::PopStyleColor();

        ImGui::Unindent(6.0f);
        ImGui::Spacing();
    }

    // ====================================================
    // [4] Status (compact read-only table)
    // ====================================================
    ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgGreen);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.45f, 0.68f, 0.52f, 0.20f});
    bool stOpen = ImGui::CollapsingHeader("ステータス##camst");
    ImGui::PopStyleColor(2);

    if (stOpen) {
        ImGui::Indent(6.0f);
        ReadOnlyRow("状態", "%s", isActive_ ? "使用中" : "未使用");
        ReadOnlyRow("座標", "%.2f  %.2f  %.2f",
                    translation_.x, translation_.y, translation_.z);
        ReadOnlyRow("入力", "%s",
                    useKey_ ? "キーボード" : useMouse_ ? "Mouse"
                                                       : "なし");
        ImGui::Unindent(6.0f);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
#endif // _DEBUG
}
} // namespace Hagine
