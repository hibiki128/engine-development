#define NOMINMAX
#include "BaseObject.h"
#include "BaseObjectManager.h"
#include "browser/ShowFolder.h"
#include "collider/CollisionManager.h"
#include "debug/profiler/CpuProfiler.h"
#include "frame/Frame.h"
#include "light/ToonSettings.h"
#include "model/material/Material.h"
#include "object/Object3dInstancing.h"
#include "scene/SceneManager.h"
#include "scene/SceneSerializer.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include <icon/IconsFontAwesome5.h>
#include "utility/debug/imgui/ImGuiNotification.h"
#ifdef USE_IMGUI
#include "utility/debug/imgui/AssetDragDrop.h"
#include <asset/AssetPath.h>
#include "utility/edit/animation/AnimationStateMachineEditor.h"
#include <graphics/texture/TextureManager.h>
#include <imgui_internal.h>
#include <implot.h>
#endif // DEBUG

// インスペクタUI本体（メインタブ・オブジェクト設定・スケールイージング・各種セレクタ）。
namespace Hagine {
// ============================================================
//  BaseObject::ImGui  (メインタブ)
// ============================================================
void BaseObject::DrawImGui() {
#ifdef USE_IMGUI
    if (!ImGui::BeginTabBar(objectName_.c_str()))
        return;

    if (ImGui::BeginTabItem(objectName_.c_str())) {
        // ---- 状態サマリー（一目で現在の挙動が分かる行）----
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(DebugTheme::kTextDim, "状態:");
        ImGui::SameLine();
        ImGui::TextColored(isAlive_ ? DebugTheme::kAccentGreen : DebugTheme::kAccentRed,
                           isAlive_ ? "生存" : "停止");
        ImGui::SameLine();
        ImGui::TextColored(DebugTheme::kTextDim, "|");
        ImGui::SameLine();
        ImGui::TextColored(rigidBody_.enabled ? DebugTheme::kAccentOrange : DebugTheme::kTextDim,
                           rigidBody_.enabled ? "物理ON" : "物理OFF");
        ImGui::SameLine();
        ImGui::TextColored(resolveCollision_ ? DebugTheme::kAccentOrange : DebugTheme::kTextDim,
                           resolveCollision_ ? "押出ON" : "押出OFF");
        ImGui::SameLine();
        ImGui::TextColored(DebugTheme::kTextDim, "|  コライダー %d 個", static_cast<int>(colliders_.size()));

        ImGui::Separator();

        // ---- 各セクション（枠の下端をドラッグして高さを自由に調整できる。ResizeYの高さはiniに保存される）----
        ImGui::BeginChild("BaseObjectBody", ImVec2(0, 420.0f),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
        DebugObject();
        ImGui::EndChild();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("（枠の下端をドラッグすると高さを変えられます）");
        ImGui::PopStyleColor();

        // ---- 保存バー（常に最下部に固定）----
        BaseObjectManager *manager = BaseObjectManager::GetInstance();
        if (manager->IsOwned(this)) {
            // エディタで置いた物の中身はシーンファイルが持つ。保存はシーン単位で行う
            ImGui::Checkbox("シーンに保存する##objSceneSave", &shouldSave_);
            ImGui::SetItemTooltip("外すと、この物と子はシーンファイルに書かれません（今の画面からは消えません）");
            if (shouldSave_ && !manager->IsSceneSaveTarget(this)) {
                ImGui::SameLine();
                ImGui::TextColored(DebugTheme::kAccentOrange, "親が保存しない設定なので書かれません");
            }
            if (ConfirmButton(ICON_FA_SAVE " シーンを保存##objsave")) {
                SceneSerializer::GetInstance()->SaveCurrentScene();
            }
            ImGui::SetItemTooltip("今のシーンのファイルへ、置いてある物をまとめて保存する（Ctrl+S）");
        } else {
            // ゲーム側がコードで作る物は、オブジェクト単体の保存（jsons/ObjectDatas/）で調整値を持つ
            if (ConfirmButton("この設定を全て保存##objsave")) {
                // SaveToJson の中で全コライダーも jsons/Collider/ へ保存される
                SaveToJson();
                AnimaSaveToJson();

                ImGuiNotification::Post(std::format("「{}」をセーブしました", objectName_),
                                        {0.45f, 0.68f, 0.52f, 1.0f});
            }
            ImGui::SetItemTooltip("ゲーム側で作る物なので、シーンではなく jsons/ObjectDatas/ へ保存する\n"
                                  "（オブジェクト設定・アニメ・全コライダーをまとめて保存）");
        }

        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
#endif // USE_IMGUI
}

void BaseObject::DebugObject() {
#ifdef USE_IMGUI
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);

    // セクションが増えて縦一列では探しづらくなったので、カテゴリごとのタブに分ける。
    // 選択中のタブの中身だけが描かれるので、スクロール量も毎フレームのUIコストも減る。
    // タブの中では従来どおり折りたたみヘッダーで畳める（開閉状態は imgui.ini に残る）。
    // 検索中は、この子窓のいちばん外側の見出しを絞り込みの対象にする
    if (InspectorSearch::IsActive()) {
        InspectorSearch::BeginBody();
    }
    const std::string categoryTabId = "BaseObjectCategories##" + objectName_;
    if (!InspectorSearch::BeginTabBar(categoryTabId.c_str(), ImGuiTabBarFlags_FittingPolicyScroll)) {
        ImGui::PopStyleVar(4);
        return;
    }

    // ====================================================
    // 基本（トランスフォーム）
    // ====================================================
    if (InspectorSearch::BeginTab("基本")) {

        // ====================================================
        // トランスフォーム
        // ====================================================
        if (ThemedHeader("トランスフォーム##hdr", DebugTheme::kAccentBlue, true)) {
            ImGui::Indent(6.0f);

            // ---- Local ----
            SectionHeader("[ ローカル ]", DebugTheme::kAccentBlue);

            // Table: Label | DragFloat3 | ResetBtn
            if (ImGui::BeginTable("LocalTF", 3,
                                  ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
                ImGui::TableSetupColumn("Lbl", ImGuiTableColumnFlags_WidthFixed, 62.0f);
                ImGui::TableSetupColumn("Drg", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Btn", ImGuiTableColumnFlags_WidthFixed, 30.0f);

                // -- Position --
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentBlue);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("位置");
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgBlue);
                ImGui::DragFloat3("##lpos", &transform_->translation_.x,
                                  0.1f, -1000.f, 1000.f, "%.2f");
                ImGui::PopStyleColor();
                {
                    // 右クリック: 位置のコピー・貼り付け・原点へ戻す
                    static const float kZero[3] = {0.0f, 0.0f, 0.0f};
                    FloatNContextMenu("##lposctx", &transform_->translation_.x, 3, kZero);
                }
                ImGui::TableNextColumn();
                if (SmallResetButton("[R]##rpos"))
                    transform_->translation_ = {};

                // -- Rotation (delta) --
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentCyan);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("回転(差分)");
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                static Vector3 deltaRot{};
                ImGui::SetNextItemWidth(-1);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.42f, 0.66f, 0.68f, 0.12f});
                if (ImGui::DragFloat3("##lrot", &deltaRot.x, 0.1f, -10.f, 10.f, "%.1fdeg")) {
                    float r = std::numbers::pi_v<float> / 180.f;
                    Quaternion cur = transform_->GetRotationQuaternion();
                    Quaternion dx = Quaternion::FromAxisAngle({1, 0, 0}, deltaRot.x * r);
                    Quaternion dy = Quaternion::FromAxisAngle({0, 1, 0}, deltaRot.y * r);
                    Quaternion dz = Quaternion::FromAxisAngle({0, 0, 1}, deltaRot.z * r);
                    transform_->SetRotationQuaternion((cur * (dy * dx * dz)).Normalize());
                    transform_->UpdateMatrix();
                    deltaRot = {};
                }
                ImGui::PopStyleColor();
                // 右クリック: 今の向き（オイラー角・度）のコピー・貼り付け
                {
                    const float toDeg = 180.0f / std::numbers::pi_v<float>;
                    Vector3 euler = transform_->GetRotationEuler();
                    float degrees[3] = {euler.x * toDeg, euler.y * toDeg, euler.z * toDeg};
                    static const float kZero[3] = {0.0f, 0.0f, 0.0f};
                    if (FloatNContextMenu("##lrotctx", degrees, 3, kZero)) {
                        transform_->SetRotationEuler({degrees[0] / toDeg, degrees[1] / toDeg, degrees[2] / toDeg});
                        transform_->UpdateMatrix();
                    }
                }
                ImGui::TableNextColumn();
                if (SmallResetButton("[R]##rrot")) {
                    transform_->SetRotationQuaternion(Quaternion::IdentityQuaternion());
                    transform_->UpdateMatrix();
                    deltaRot = {};
                }

                // -- Scale --
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentGreen);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("スケール");
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgGreen);
                ImGui::DragFloat3("##lscl", &transform_->scale_.x,
                                  0.01f, 0.01f, 10.f, "%.2f");
                ImGui::PopStyleColor();
                {
                    static const float kOne[3] = {1.0f, 1.0f, 1.0f};
                    FloatNContextMenu("##lsclctx", &transform_->scale_.x, 3, kOne);
                }
                ImGui::TableNextColumn();
                if (SmallResetButton("[R]##rscl"))
                    transform_->scale_ = {1, 1, 1};

                ImGui::EndTable();
            }

            // 現在のオイラー角（参考）
            {
                Vector3 e = transform_->GetRotationEuler();
                float d = 180.f / std::numbers::pi_v<float>;
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::Text(" Current Rot  X:%.1f  Y:%.1f  Z:%.1f (deg)",
                            e.x * d, e.y * d, e.z * d);
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();

            // ---- World (read-only) ----
            SectionHeader("[ ワールド (読み取り専用) ]", DebugTheme::kAccentGreen);

            Vector3 wPos = GetWorldPosition();
            Quaternion wRot = GetWorldRotation();
            Vector3 wScale = GetWorldScale();
            float toDeg = 180.f / std::numbers::pi_v<float>;

            // ReadOnlyRow を使って ## が画面に出ないようにする
            // InputFloat3 の ID は ## 始まりで非表示
            auto WorldVec3Row = [](const char *rowLabel,
                                   const char *dragId,
                                   float x, float y, float z) {
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("  %-10s", rowLabel);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                float v[3] = {x, y, z};
                ImGui::SetNextItemWidth(-1);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.12f, 0.12f, 0.14f, 0.8f});
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextReadOnly);
                ImGui::InputFloat3(dragId, v, "%.2f", ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor(2);
            };

            WorldVec3Row("位置", "##wpos", wPos.x, wPos.y, wPos.z);
            WorldVec3Row("Rotation", "##wrot",
                         wRot.x * toDeg, wRot.y * toDeg, wRot.z * toDeg);
            WorldVec3Row("スケール", "##wscl", wScale.x, wScale.y, wScale.z);

            ImGui::Spacing();

            // ---- ImPlot: Scale history ----
            ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgGreen);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.45f, 0.68f, 0.52f, 0.20f});
            bool plotOpen = ImGui::CollapsingHeader("スケール履歴 (グラフ)##scgr");
            ImGui::PopStyleColor(2);

            if (plotOpen) {
                constexpr int kN = 120;
                static float hx[kN]{}, hy[kN]{}, hz[kN]{};
                static int head = 0, cnt = 0;
                hx[head] = transform_->scale_.x;
                hy[head] = transform_->scale_.y;
                hz[head] = transform_->scale_.z;
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
                if (ImPlot::BeginPlot("##scplot", ImVec2(-1, 75),
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
                // 凡例
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
                ImGui::TextUnformatted(" X");
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentGreen);
                ImGui::TextUnformatted(" Y");
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentBlue);
                ImGui::TextUnformatted(" Z");
                ImGui::PopStyleColor();
            }

            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        InspectorSearch::EndTab();
    }

    // ====================================================
    // 派生クラス固有のタブ（メタボールなど）
    // 基本の隣に固定で出すことで、そのオブジェクト固有の設定を見つけやすくする
    // ====================================================
    if (const char *extensionName = GetImGuiExtensionName()) {
        if (InspectorSearch::BeginTab(extensionName)) {
            if (InspectorSearch::ShowLooseContent()) {
                DrawImGuiExtension();
            }
            InspectorSearch::EndTab();
        }
    }

    // ====================================================
    // 見た目（表示・マテリアル・アニメーション）
    // ====================================================
    if (InspectorSearch::BeginTab("見た目")) {

        // ====================================================
        // 表示（描画モード・ライティング・ギズモ）
        // ====================================================
        if (ThemedHeader("表示##hdr", DebugTheme::kAccentCyan)) {
            ImGui::Indent(6.0f);

            // ---- 描画モード（モデル / ワイヤーフレームは排他）----
            SectionHeader("[ 描画モード ]", DebugTheme::kAccentBlue);
            {
                // px 直書きの SameLine(170/330) をやめ、窓幅を3等分した列に置く。
                // 直書きだと窓を狭めたときにラベルへ重なり、広げると間延びしていた。
                InlineColumns modeCols(3);
                if (AccentCheckbox("モデル描画##mdraw", &isModelDraw_, DebugTheme::kAccentBlue) && isModelDraw_)
                    isWireframe_ = false;
                modeCols.Next(1);
                if (AccentCheckbox("ワイヤーフレーム##wf", &isWireframe_, DebugTheme::kAccentBlue) && isWireframe_)
                    isModelDraw_ = false;
                if (isWireframe_) {
                    modeCols.Next(2);
                    AccentCheckbox("レインボー##rb", &isRainbow_, DebugTheme::kAccentYellow);
                } else {
                    isRainbow_ = false;
                }
            }

            ImGui::Spacing();

            // ---- ライティング ----
            SectionHeader("[ ライティング ]", DebugTheme::kAccentOrange);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(DebugTheme::kTextDim, "状態:");
            ImGui::SameLine();
            StatusBadge(isLighting_ ? "オン" : "オフ",
                        isLighting_ ? DebugTheme::kAccentGreen : DebugTheme::kAccentRed);
            ImGui::SameLine();
            if (isLighting_ ? DangerButton("無効にする##lt") : ConfirmButton("有効にする##lt"))
                isLighting_ = !isLighting_;

            ImGui::Spacing();

            // ---- カメラに近いと透ける ----
            SectionHeader("[ カメラに近いと透ける ]", DebugTheme::kAccentCyan);
            AccentCheckbox("透けさせる##camFade", &cameraFadeEnabled_, DebugTheme::kAccentCyan);
            ImGui::SetItemTooltip("カメラが近づくと網目状に透けていき、最後は見えなくなります（影は残ります）。\n"
                                  "地形や床など、透けてほしくない物はオフにします。\n"
                                  "距離などの設定は「描画システム」窓の「カメラに近い物を透けさせる」");

            ImGui::Spacing();

            // ---- ギズモ ----
            SectionHeader("[ ギズモ ]", DebugTheme::kAccentRed);
            AccentCheckbox("ギズモ選択可##gsel", &isGizmoSelectable_, DebugTheme::kAccentRed);
            ImGui::SetItemTooltip("オフ: マウスクリック / ギズモ操作の対象外になる");

            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        // ====================================================
        // マテリアル（スロット・カラー・テクスチャ・ブレンド）
        // メタボールのように実体のモデルを持たないオブジェクトでは、
        // ここを触っても何も変わらないので出さない（専用タブ側に用意する）
        // ====================================================
        if (!HasInspectorMaterial()) {
            if (InspectorSearch::ShowLooseContent()) {
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                const char *extensionName = GetImGuiExtensionName();
                ImGui::TextWrapped("マテリアルは「%s」タブで設定します", extensionName ? extensionName : "専用");
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }
        } else if (ThemedHeader("マテリアル##hdr", DebugTheme::kAccentPurple)) {
            ImGui::Indent(6.0f);
            static int selMat = 0;
            size_t matCount = obj3d_->GetMaterialCount();
            if (obj3d_->GetHaveAnimation() && matCount > 1)
                --matCount;

            SectionHeader("[ マテリアルスロット ]", DebugTheme::kAccentPurple);

            if (matCount > 1) {
                std::vector<std::string> items;
                std::vector<const char *> cstrs;
                for (int i = 0; i < static_cast<int>(matCount); ++i)
                    items.push_back("Slot " + std::to_string(i + 1));
                for (auto &s : items)
                    cstrs.push_back(s.c_str());
                ImGui::SetNextItemWidth(-1);
                ImGui::Combo("##matslot", &selMat, cstrs.data(), static_cast<int>(cstrs.size()));
                selMat = std::clamp(selMat, 0, static_cast<int>(matCount) - 1);
            } else {
                selMat = 0;
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("  シングルマテリアル");
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();

            // Color
            ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgPurple);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.62f, 0.50f, 0.74f, 0.20f});
            if (ImGui::TreeNodeEx("カラー##mc", ImGuiTreeNodeFlags_SpanAvailWidth)) {
                Vector4 cur = GetColor(selMat);
                float c[4] = {cur.x, cur.y, cur.z, cur.w};
                ImGui::SetNextItemWidth(-1);
                if (ImGui::ColorEdit4("##colpicker", c))
                    SetColor({c[0], c[1], c[2], c[3]}, selMat);
                if (ImGui::SmallButton("カラーリセット##cr"))
                    SetColor({1, 1, 1, 1}, selMat);
                ImGui::TreePop();
            }

            // Texture（サムネ＋D&D＋フォルダ選択。フォルダ一覧はポップアップに入れてスクロールを奪わない）
            if (ImGui::TreeNodeEx("テクスチャ##tx", ImGuiTreeNodeFlags_SpanAvailWidth)) {
                auto *tm = TextureManager::GetInstance();
                const std::string curTex =
                    (selMat < static_cast<int>(texturePaths_.size())) ? texturePaths_[selMat] : std::string();

                // 現在のテクスチャのサムネイル（ここへドラッグ&ドロップでも設定できる）
                D3D12_GPU_DESCRIPTOR_HANDLE texHandle{};
                if (!curTex.empty()) {
                    tm->LoadTexture(curTex); // ロード済みなら即return
                    texHandle = tm->GetSrvHandleGPU(AssetPath::Image(curTex));
                }
                if (texHandle.ptr != 0)
                    ImGui::Image(static_cast<ImTextureID>(texHandle.ptr), ImVec2(56.0f, 56.0f));
                else
                    ImGui::Button("ここへ\nドロップ", ImVec2(56.0f, 56.0f));
                std::string dropped;
                if (AssetDragDrop::TextureTarget(dropped)) {
                    SetTexture(dropped, selMat);
                    if (selMat < static_cast<int>(texturePaths_.size()))
                        texturePaths_[selMat] = dropped;
                    texturePath_ = dropped;
                }
                ImGui::SetItemTooltip("アセットブラウザの画像をD&Dで設定できます");

                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::Text("現在: %s", curTex.empty() ? "(なし)" : curTex.c_str());
                ImGui::PopStyleColor();
                if (ImGui::SmallButton("フォルダから選択...##topen"))
                    ImGui::OpenPopup("テクスチャ選択##texpop");
                ImGui::SameLine();
                if (ImGui::SmallButton("クリア##tc"))
                    texturePath_.clear();
                ImGui::EndGroup();

                // フォルダブラウザは別ウィンドウ（ポップアップ）に置く＝インスペクタ側のスクロールを奪わない
                ImGui::SetNextWindowSize(ImVec2(540, 480), ImGuiCond_Appearing);
                if (ImGui::BeginPopup("テクスチャ選択##texpop")) {
                    ShowTextureFile(texturePath_);
                    ImGui::Separator();
                    if (ImGui::Button("適用##ta")) {
                        SetTexture(texturePath_, selMat);
                        if (selMat < static_cast<int>(texturePaths_.size()))
                            texturePaths_[selMat] = texturePath_;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("閉じる##tclose"))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::TreePop();
            }

            // UV（タイリング / オフセット / 回転）
            if (ImGui::TreeNodeEx("UV##uv", ImGuiTreeNodeFlags_SpanAvailWidth)) {
                if (Material *mat = GetMaterial(static_cast<uint32_t>(selMat))) {
                    MaterialData &md = mat->GetMaterialData();
                    ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat2("##uvsize", &md.uvSize.x, 0.05f, 0.01f, 200.0f, "タイリング %.2f");
                    ImGui::SetItemTooltip("大きくするとテクスチャが繰り返される（広い地面の法線マップ等で使う）");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat2("##uvpos", &md.uvPosition.x, 0.005f, -100.0f, 100.0f, "オフセット %.3f");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat("##uvrot", &md.uvRotate, 0.01f, -6.28f, 6.28f, "回転 %.2f rad");
                    if (ImGui::SmallButton("UVリセット##uvr")) {
                        md.uvSize = {1.0f, 1.0f};
                        md.uvPosition = {0.0f, 0.0f};
                        md.uvRotate = 0.0f;
                    }
                }
                ImGui::TreePop();
            }

            // Blend mode
            if (ImGui::TreeNodeEx("ブレンドモード##bm", ImGuiTreeNodeFlags_SpanAvailWidth)) {
                ShowBlendModeCombo(blendMode_);
                ImGui::TreePop();
            }

            // トゥーンシェーディング（全体設定がONのときだけ効く）
            if (ImGui::TreeNodeEx("トゥーン##toonmat", ImGuiTreeNodeFlags_SpanAvailWidth)) {
                if (Material *mat = GetMaterial(static_cast<uint32_t>(selMat))) {
                    MaterialData &md = mat->GetMaterialData();
                    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentPurple);
                    ImGui::Checkbox("トゥーンを適用##toonmatchk", &md.enableToon);
                    ImGui::PopStyleColor();
                    ImGui::SetItemTooltip("このマテリアルをセル画風の陰影で描くかどうか。\n"
                                          "地面やエフェクトだけ従来の陰影で残したいときに外します");

                    const bool globalOn = ToonSettings::GetInstance()->IsEnabled();
                    ImGui::PushStyleColor(ImGuiCol_Text, globalOn ? DebugTheme::kTextDim : DebugTheme::kAccentOrange);
                    ImGui::TextWrapped(globalOn
                                           ? "全体設定はONです"
                                           : "全体設定がOFFなので、ここをONにしても効きません（ライトの設定画面で切り替えられます）");
                    ImGui::PopStyleColor();
                }
                ImGui::TreePop();
            }

            // 自己発光（エミッシブ）
            if (ImGui::TreeNodeEx("自己発光##emissivemat", ImGuiTreeNodeFlags_SpanAvailWidth)) {
                if (Material *mat = GetMaterial(static_cast<uint32_t>(selMat))) {
                    MaterialData &md = mat->GetMaterialData();
                    ImGui::DragFloat("発光の強さ##emissivestr", &md.emissiveStrength, 0.02f, 0.0f, 8.0f);
                    ImGui::SetItemTooltip("ライトに当たっていなくても光る分です。0で発光なし。\n"
                                          "発光色はこのマテリアルの色×テクスチャ（アルベド）を使います。\n"
                                          "1を超えるとブルームが拾って滲みます");
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::TextWrapped("滲ませたいときは、ポストエフェクトにブルームを足してください");
                    ImGui::PopStyleColor();
                }
                ImGui::TreePop();
            }

            // ノーマルマップ / 手続き的法線
            if (ImGui::TreeNodeEx("ノーマルマップ##nm", ImGuiTreeNodeFlags_SpanAvailWidth)) {
                if (Material *mat = GetMaterial(static_cast<uint32_t>(selMat))) {
                    MaterialData &md = mat->GetMaterialData();

                    // テクスチャ法線マップ
                    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
                    ImGui::Checkbox("テクスチャ法線マップ##nmtex", &md.enableNormalMap);
                    ImGui::PopStyleColor();
                    if (md.enableNormalMap) {
                        // ---- 現在の法線マップ（サムネがそのままD&Dのドロップ先）----
                        auto *tm = TextureManager::GetInstance();
                        D3D12_GPU_DESCRIPTOR_HANDLE nmHandle{};
                        if (md.hasNormalMapTexture && !md.normalMapFilePath.empty()) {
                            tm->LoadTexture(md.normalMapFilePath); // ロード済みなら即return
                            nmHandle = tm->GetSrvHandleGPU(AssetPath::Image(md.normalMapFilePath));
                        }
                        if (nmHandle.ptr != 0)
                            ImGui::Image(static_cast<ImTextureID>(nmHandle.ptr), ImVec2(56.0f, 56.0f));
                        else
                            ImGui::Button("ここへ\nドロップ", ImVec2(56.0f, 56.0f));

                        std::string dropped;
                        if (AssetDragDrop::TextureTarget(dropped)) {
                            mat->SetNormalMap(dropped);
                            normalMapPath_ = dropped;
                        }
                        ImGui::SetItemTooltip("アセットブラウザの画像をドラッグ&ドロップで設定できます");

                        ImGui::SameLine();
                        ImGui::BeginGroup();
                        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                        ImGui::Text("map: %s", md.hasNormalMapTexture ? md.normalMapFilePath.c_str() : "(未設定=albedo流用)");
                        ImGui::PopStyleColor();
                        ImGui::TextDisabled("D&D または下のブラウザで設定");
                        if (ImGui::SmallButton("クリア##nmclear")) {
                            mat->ClearNormalMap();
                            normalMapPath_.clear();
                        }
                        ImGui::EndGroup();

                        // ---- フォルダから選択（ポップアップ＝スクロールを奪わない）----
                        if (ImGui::SmallButton("フォルダから選択...##nmopen"))
                            ImGui::OpenPopup("法線マップ選択##nmpop");
                        ImGui::SameLine();
                        ImGui::TextDisabled("選択中: %s", normalMapPath_.empty() ? "(なし)" : normalMapPath_.c_str());

                        ImGui::SetNextWindowSize(ImVec2(540, 480), ImGuiCond_Appearing);
                        if (ImGui::BeginPopup("法線マップ選択##nmpop")) {
                            ShowTextureFile(normalMapPath_, "normalmap");
                            ImGui::Separator();
                            ImGui::BeginDisabled(normalMapPath_.empty());
                            if (ImGui::SmallButton("適用##nmapply")) {
                                mat->SetNormalMap(normalMapPath_);
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::EndDisabled();
                            ImGui::SameLine();
                            if (ImGui::SmallButton("選択解除##nmdesel"))
                                normalMapPath_.clear();
                            ImGui::SameLine();
                            if (ImGui::SmallButton("閉じる##nmclose"))
                                ImGui::CloseCurrentPopup();
                            ImGui::EndPopup();
                        }
                    }

                    // 手続き的法線（両方ONなら PS は手続き的を優先）
                    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
                    ImGui::Checkbox("手続き的法線##pn", &md.enableProceduralNormal);
                    ImGui::PopStyleColor();
                    ImGui::SetItemTooltip("テクスチャ不要。worldXZの高さ場から法線を摂動。両方ONなら手続き的が優先");
                    if (md.enableProceduralNormal) {
                        ImGui::SetNextItemWidth(-1);
                        ImGui::DragFloat("スケール##pns", &md.proceduralScale, 0.05f, 0.01f, 50.0f, "スケール %.2f");
                    }

                    // 法線の強さ（テクスチャ・手続き共通）
                    ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat("強さ##pnst", &md.normalStrength, 0.01f, 0.0f, 8.0f, "強さ %.2f");
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                    ImGui::TextUnformatted("マテリアルがありません");
                    ImGui::PopStyleColor();
                }
                ImGui::TreePop();
            }
            ImGui::PopStyleColor(2);

            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        // ====================================================
        // アニメーション
        // ====================================================
        if (obj3d_->GetHaveAnimation()) {
            if (ThemedHeader("アニメーション##hdr", DebugTheme::kAccentYellow)) {
                ImGui::Indent(6.0f);
                SectionHeader("[ 制御 ]", DebugTheme::kAccentYellow);

                ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentYellow);
                // 現在のアニメーションのループ設定を取得・変更
                std::string currentModelPath = obj3d_->GetModelFilePath();
                bool loop = obj3d_->GetAnimationLoop(currentModelPath);
                InlineColumns animCols(2);
                if (ImGui::Checkbox("ループ##lp", &loop)) {
                    obj3d_->SetAnimationLoop(currentModelPath, loop);
                }

                animCols.Next(1);
                ImGui::Checkbox("スケルトン表示##sk", &skeletonDraw_);
                ImGui::PopStyleColor();
                ImGui::Spacing();

                // アニメーション速度設定
                float speed = obj3d_->GetAnimationSpeed();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentYellow);
                ImGui::TextUnformatted("再生速度");
                ImGui::PopStyleColor();
                ImGui::SetNextItemWidth(-1);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgYellow);
                if (ImGui::DragFloat("##aspeed", &speed, 0.01f, 0.0f, 10.0f, "%.2f")) {
                    obj3d_->SetAnimationSpeed(speed);
                }
                ImGui::PopStyleColor();

                // ブレンド時間設定
                float blendDuration = obj3d_->GetAnimationBlendDuration();
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentCyan);
                ImGui::TextUnformatted("ブレンド時間 (秒)");
                ImGui::PopStyleColor();
                ImGui::SetNextItemWidth(-1);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.42f, 0.66f, 0.68f, 0.12f});
                if (ImGui::DragFloat("##ablend", &blendDuration, 0.01f, 0.0f, 5.0f, "%.2f")) {
                    obj3d_->SetAnimationBlendDuration(blendDuration);
                }
                ImGui::PopStyleColor();

                ImGui::Spacing();

                if (PrimaryButton("再生##aplay", ImVec2(-1, 0)))
                    obj3d_->PlayAnimation();

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgYellow);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.80f, 0.72f, 0.42f, 0.20f});
                if (ImGui::TreeNodeEx("アニメーション設定##as", ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    ShowFileSelector();
                    ImGui::TreePop();
                }
                ImGui::PopStyleColor(2);

                ImGui::Spacing();
                DrawAnimStateMachineImGui();
                ImGui::Unindent(6.0f);
            }
        }

        InspectorSearch::EndTab();
    }

    // ====================================================
    // 物理（コライダー・押し出し・リジッドボディ）
    // ====================================================
    if (InspectorSearch::BeginTab("物理")) {

        // ====================================================
        // コライダー
        // ====================================================
        if (ThemedHeader("コライダー##hdr", DebugTheme::kAccentCyan)) {
            ImGui::Indent(6.0f);

            // コライダー追加ボタン
            if (PrimaryButton("+ コライダー追加##addcol"))
                ImGui::OpenPopup("AddColliderPopup##acp");
            ImGui::SetItemTooltip("形状を選んで追加。当たって押し返す挙動は『物理』セクションで設定する");

            if (ImGui::BeginPopup("AddColliderPopup##acp")) {
                const std::pair<const char *, ColliderType> kShapes[] = {{"Sphere", ColliderType::Sphere},
                                                                         {"AABB", ColliderType::AABB},
                                                                         {"OBB", ColliderType::OBB},
                                                                         {"Cylinder", ColliderType::Cylinder},
                                                                         {"Mesh", ColliderType::Mesh}};
                for (const auto &[shapeName, shapeType] : kShapes) {
                    if (ImGui::MenuItem(shapeName)) {
                        AddColliderForEditor(shapeType);
                    }
                }
                ImGui::EndPopup();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            DebugCollider();

            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        // ====================================================
        // 物理（押し出し・リジッドボディ）
        // ====================================================
        if (ThemedHeader("物理 (押し出し / リジッドボディ)##hdr", DebugTheme::kAccentOrange)) {
            ImGui::Indent(6.0f);

            // ---- クイック設定（用途別プリセット）----
            SectionHeader("[ クイック設定 ]", DebugTheme::kAccentGreen);
            ImGui::TextColored(DebugTheme::kTextDim, "用途を選ぶとまとめて設定されます");
            if (AccentButton("跳ねる物##presetDyn", DebugTheme::kAccentGreen, 0.0f)) {
                SetResolveCollision(true);
                rigidBody_.enabled = true;
                rigidBody_.useGravity = true;
                rigidBody_.restitution = 0.5f;
                rigidBody_.velocity = {0.0f, 0.0f, 0.0f};
                ImGuiNotification::Post("プリセット: 跳ねる物（重力＋押し出し＋反発）", {0.45f, 0.68f, 0.52f, 1.0f});
            }
            ImGui::SetItemTooltip("重力で落下し、地面に当たって跳ね返る動的オブジェクト");
            ImGui::SameLine();
            if (AccentButton("静的な地面##presetStatic", DebugTheme::kAccentBlue, 0.0f)) {
                SetResolveCollision(false);
                rigidBody_.enabled = false;
                rigidBody_.useGravity = false;
                rigidBody_.velocity = {0.0f, 0.0f, 0.0f};
                ImGuiNotification::Post("プリセット: 静的な地面（動かない受け止め側）", {0.42f, 0.66f, 0.68f, 1.0f});
            }
            ImGui::SetItemTooltip("動かない床・壁。相手を受け止める側はこちら（押し出しはOFF）");

            ImGui::Spacing();

            // ---- 押し出し（衝突解消）----
            SectionHeader("[ 押し出し（衝突解消）]", DebugTheme::kAccentOrange);
            bool resolve = resolveCollision_;
            if (AccentCheckbox("めり込んだら押し出す##resolve", &resolve, DebugTheme::kAccentOrange))
                SetResolveCollision(resolve);
            ImGui::SetItemTooltip("衝突した相手から自分を押し出す。動かしたい側だけONにする\n"
                                  "（床など受け止める側はOFF。独自の衝突処理を持つオブジェクトでも使わない）");

            ImGui::Spacing();

            // ---- リジッドボディ ----
            SectionHeader("[ リジッドボディ ]", DebugTheme::kAccentOrange);
            AccentCheckbox("リジッドボディとして扱う##rbenable", &rigidBody_.enabled, DebugTheme::kAccentGreen);
            ImGui::SetItemTooltip("重力で落下し速度を持つ。押し出しと併用すると坂を滑り落ちる");

            // リジッドボディOFFのときは物理パラメータを淡色＝無効表示にする
            ImGui::BeginDisabled(!rigidBody_.enabled);

            AccentCheckbox("重力を受ける##rbgrav", &rigidBody_.useGravity, DebugTheme::kAccentBlue);

            ImGui::DragFloat("質量##rbmass", &rigidBody_.mass, 0.05f, 0.01f, 1000.0f, "%.2f");
            ImGui::SetItemTooltip("外力 F=ma に効く。大きいほど力で動きにくい");
            ImGui::DragFloat3("重力加速度##rbg", &rigidBody_.gravity.x, 0.1f, -100.0f, 100.0f, "%.2f");
            ImGui::DragFloat("減衰 (空気抵抗)##rbdamp", &rigidBody_.linearDamping, 0.005f, 0.0f, 10.0f, "%.3f");
            ImGui::SetItemTooltip("毎フレーム速度を減らす。大きいほどすぐ止まる");
            ImGui::DragFloat("反発係数##rbrest", &rigidBody_.restitution, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("0=跳ねない / 1=完全反発。押し出しONのとき跳ね返りに効く");
            ImGui::DragFloat("摩擦##rbfric", &rigidBody_.friction, 0.01f, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("接触面に沿う速度の減衰。坂の滑り方に効く");

            ImGui::Spacing();
            Vector3 v = rigidBody_.velocity;
            ReadOnlyRow("速度", "%.2f, %.2f, %.2f", v.x, v.y, v.z);
            if (NeutralButton("速度をリセット##rbresetv"))
                rigidBody_.velocity = {0.0f, 0.0f, 0.0f};

            ImGui::EndDisabled();

            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        // ====================================================
        // 足IK（接地）
        // ====================================================
        // 脚を持たないモデルには出さない（スキンの入った gltf だけが対象）
        if (obj3d_ && obj3d_->GetHaveAnimation() && ThemedHeader("足IK（接地）##ikhdr", DebugTheme::kAccentPurple)) {
            ImGui::Indent(6.0f);
            DrawFootIkImGui();
            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        // ====================================================
        // 注視IK（頭を見る先へ向ける）
        // ====================================================
        if (obj3d_ && obj3d_->GetHaveAnimation() && ThemedHeader("注視（頭を向ける）##lookhdr", DebugTheme::kAccentCyan)) {
            ImGui::Indent(6.0f);
            DrawLookAtImGui();
            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        // ====================================================
        // 手のIK（手首を目標へ伸ばす）
        // ====================================================
        if (obj3d_ && obj3d_->GetHaveAnimation() && ThemedHeader("手のIK（目標へ手を伸ばす）##handikhdr", DebugTheme::kAccentGreen)) {
            ImGui::Indent(6.0f);
            DrawHandIkImGui();
            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        // ====================================================
        // 揺れ物（髪・布・しっぽ）
        // ====================================================
        if (obj3d_ && obj3d_->GetHaveAnimation() && ThemedHeader("揺れ物（髪・布・しっぽ）##springhdr", DebugTheme::kAccentOrange)) {
            ImGui::Indent(6.0f);
            DrawSpringBoneImGui();
            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        InspectorSearch::EndTab();
    }

    // ====================================================
    // ツール（複製・スケールイージング検証）
    // ====================================================
    if (InspectorSearch::BeginTab("ツール")) {

        // ====================================================
        // ツール（スケールイージング検証）
        // ====================================================
        if (ThemedHeader("ツール##hdr", DebugTheme::kAccentGreen)) {
            ImGui::Indent(6.0f);

            SectionHeader("[ 複製 ]", DebugTheme::kAccentGreen);
            if (ImGui::Button("このオブジェクトを複製", ImVec2(-1, 0))) {
                // 複製はオブジェクトの生成・登録を伴うので、インスペクタを描いている
                // 最中に objects_ を触らないよう、マネージャ側で次フレームに実行させる
                BaseObjectManager::GetInstance()->RequestDuplicate(objectName_);
            }
            ImGui::SetItemTooltip("トランスフォーム・マテリアル・メタボールの要素ごと複製し、少しずらして置く。\n"
                                  "選択中のオブジェクトが対象のショートカットとは違い、\n"
                                  "こちらは今インスペクタに出ているこのオブジェクトを複製する");
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextWrapped("選択中のオブジェクトには Ctrl+D（複製）/ Ctrl+C・Ctrl+V（コピー＆ペースト）も使えます");
            ImGui::PopStyleColor();
            ImGui::Spacing();

            SectionHeader("[ スケールイージング検証 ]", DebugTheme::kAccentGreen);
            DrawScaleEaseImGui();
            ImGui::Unindent(6.0f);
            ImGui::Spacing();
        }

        InspectorSearch::EndTab();
    }

    InspectorSearch::EndTabBar();

    ImGui::PopStyleVar(4);
#endif // USE_IMGUI
}

void BaseObject::DrawAnimStateMachineImGui() {
#ifdef USE_IMGUI
    SectionHeader("[ ステートマシン ]", DebugTheme::kAccentYellow);
    const std::string current = animStateMachine_ ? animStateMachine_->GetAssetName() : std::string();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##asmSelect", current.empty() ? "(使わない)" : current.c_str())) {
        if (ImGui::Selectable("(使わない)", current.empty())) {
            SetAnimationStateMachine("");
        }
        for (const std::string &name : AnimationStateMachineLibrary::ListFiles()) {
            if (ImGui::Selectable(name.c_str(), name == current) && !SetAnimationStateMachine(name)) {
                ImGuiNotification::Post("ステートマシンを付けられませんでした: " + name, {0.82f, 0.58f, 0.36f, 1.0f});
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("待機→走り→ジャンプのような切り替えを、ステートマシンのファイルに任せる。\n"
                          "ファイルはアニメーションステートマシン窓で作る（シーンを保存すると付けたことも保存される）");

    if (!animStateMachine_) {
        if (NeutralButton("ステートマシン窓を開く##asmOpenEmpty", ImVec2(-1, 0))) {
            AnimationStateMachineEditor::RequestOpen(std::string());
        }
        return;
    }

    // 今のステートとパラメータ（その場で値を変えて試せる）
    const std::shared_ptr<AnimationStateMachineAsset> &asset = animStateMachine_->GetAsset();
    const AnimStateData *pState = asset ? asset->FindState(animStateMachine_->GetCurrentStateId()) : nullptr;
    ImGui::TextUnformatted("今のステート:");
    ImGui::SameLine();
    StatusBadge(pState ? pState->name.c_str() : "(なし)", DebugTheme::kAccentYellow);
    ImGui::SameLine();
    ImGui::TextDisabled("%.1f 周", animStateMachine_->GetNormalizedTime());
    if (asset) {
        for (const AnimParamDef &p : asset->params) {
            ImGui::PushID(p.name.c_str());
            if (p.type == AnimParamType::Float) {
                float v = animStateMachine_->GetValue(p.name);
                if (ImGui::DragFloat(p.name.c_str(), &v, 0.02f)) {
                    animStateMachine_->SetFloat(p.name, v);
                }
            } else if (p.type == AnimParamType::Bool) {
                bool v = animStateMachine_->GetValue(p.name) >= 0.5f;
                if (ImGui::Checkbox(p.name.c_str(), &v)) {
                    animStateMachine_->SetBool(p.name, v);
                }
            } else if (NeutralButton((std::string("合図: ") + p.name).c_str())) {
                animStateMachine_->SetTrigger(p.name);
            }
            ImGui::PopID();
        }
    }
    if (PrimaryButton("エディタで開く##asmOpen", ImVec2(-1, 0))) {
        AnimationStateMachineEditor::RequestOpen(current);
    }
#endif // USE_IMGUI
}

void BaseObject::DrawLookAtImGui() {
#ifdef USE_IMGUI
    LookAtSolver *pSolver = GetLookAt();
    if (!pSolver) {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("背骨・首・頭を少しずつ回して、見る先へ顔を向けます。\n"
                           "見る先はゲーム側のコード（SetLookAtTarget）が毎フレーム渡します");
        ImGui::PopStyleColor();
        if (PrimaryButton("首と頭のジョイントを自動で拾う##lookdetect", ImVec2(-1, 0))) {
            pSolver = AcquireLookAt();
            ModelAnimation *pAnimation = obj3d_ ? obj3d_->GetCurrentModelAnimation() : nullptr;
            Bone *pBone = pAnimation ? pAnimation->GetBone() : nullptr;
            if (pSolver && pBone && pSolver->AutoDetect(pBone->GetSkeletonRef())) {
                pSolver->GetSettings().enabled = true;
                ImGuiNotification::Post(std::format("注視: ジョイントを{}個拾いました", pSolver->GetSettings().joints.size()),
                                        {0.45f, 0.68f, 0.52f, 1.0f});
            } else {
                ImGuiNotification::Post("注視: 頭のジョイントが見つかりませんでした", {0.82f, 0.58f, 0.36f, 1.0f});
            }
        }
        return;
    }

    LookAtSettings &settings = pSolver->GetSettings();
    AccentCheckbox("注視を効かせる##lookenable", &settings.enabled, DebugTheme::kAccentCyan);

    // 今の状態（見る先があるか・どれだけ向けているか）
    ImGui::SameLine();
    if (pSolver->HasTarget()) {
        StatusBadge("見る先あり", DebugTheme::kAccentGreen);
    } else {
        StatusBadge("見る先なし", DebugTheme::kTextDim);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::Text("左右 %+.0f°  上下 %+.0f°  効き %.0f%%", pSolver->GetYawDegrees(), pSolver->GetPitchDegrees(),
                pSolver->GetBlend() * 100.0f);
    ImGui::PopStyleColor();

    ImGui::BeginDisabled(!settings.enabled);
    ImGui::DragFloat("効き具合##lookweight", &settings.weight, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("0で素のアニメーションのまま。見比べるときに使う");

    ImGui::Spacing();
    SectionHeader("[ 向けられる範囲 ]", DebugTheme::kAccentCyan);
    ImGui::DragFloat("左右の上限[度]##lookyaw", &settings.maxYawDegrees, 0.5f, 0.0f, 120.0f, "%.0f");
    ImGui::DragFloat("上下の上限[度]##lookpitch", &settings.maxPitchDegrees, 0.5f, 0.0f, 80.0f, "%.0f");
    ImGui::DragFloat("あきらめる角度[度]##lookgiveup", &settings.giveUpYawDegrees, 0.5f, 0.0f, 180.0f, "%.0f");
    ImGui::SetItemTooltip("見る先がこれより後ろへ回ったら、首をひねり切らずに正面へ戻す");

    ImGui::Spacing();
    SectionHeader("[ 追従の速さ ]", DebugTheme::kAccentCyan);
    ImGui::DragFloat("向きの追従[1/秒]##lookfollow", &settings.followSpeed, 0.1f, 0.0f, 40.0f, "%.1f");
    ImGui::SetItemTooltip("大きいほど素早く見る先へ向く。小さいとゆったり目で追う");
    ImGui::DragFloat("出入りの速さ[1/秒]##lookblend", &settings.blendSpeed, 0.1f, 0.0f, 40.0f, "%.1f");
    ImGui::SetItemTooltip("見る先ができた / 外れたときに、注視を効かせ始める / 抜く速さ");

    ImGui::Spacing();
    SectionHeader("[ 回すジョイント（根元から順）]", DebugTheme::kAccentCyan);
    int removeIndex = -1;
    for (size_t i = 0; i < settings.joints.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextUnformatted(settings.joints[i].name.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
        ImGui::DragFloat("##share", &settings.joints[i].share, 0.01f, 0.0f, 1.0f, "取り分 %.2f");
        ImGui::SameLine();
        if (DangerButton("x", ImVec2(22.0f, 0.0f))) {
            removeIndex = static_cast<int>(i);
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        settings.joints.erase(settings.joints.begin() + removeIndex);
    }
    if (NeutralButton("自動で拾い直す##lookredetect", ImVec2(-1, 0))) {
        ModelAnimation *pAnimation = obj3d_ ? obj3d_->GetCurrentModelAnimation() : nullptr;
        Bone *pBone = pAnimation ? pAnimation->GetBone() : nullptr;
        if (pBone) {
            pSolver->AutoDetect(pBone->GetSkeletonRef());
        }
    }
    ImGui::EndDisabled();
#endif // USE_IMGUI
}

#ifdef USE_IMGUI
namespace {
/// <summary>ジョイントを名前で選ぶコンボ（骨が多いので絞り込み欄つき）</summary>
/// <returns>bool: 選び直したら true</returns>
bool JointCombo(const char *id, std::string &jointName, const Skeleton *pSkeleton) {
    static std::string filter;
    ImGui::SetNextItemWidth(-1);
    if (!ImGui::BeginCombo(id, jointName.empty() ? "(未選択)" : jointName.c_str(), ImGuiComboFlags_HeightLarge)) {
        return false;
    }
    bool changed = false;
    if (ImGui::IsWindowAppearing()) {
        filter.clear();
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##jointFilter", ICON_FA_SEARCH " 骨の名前で絞る", &filter);
    if (pSkeleton) {
        for (const Joint &joint : pSkeleton->joints) {
            if (!filter.empty() && joint.name.find(filter) == std::string::npos) {
                continue;
            }
            if (ImGui::Selectable(joint.name.c_str(), joint.name == jointName)) {
                jointName = joint.name;
                changed = true;
            }
        }
    }
    ImGui::EndCombo();
    return changed;
}
} // namespace
#endif // USE_IMGUI

void BaseObject::DrawHandIkImGui() {
#ifdef USE_IMGUI
    ModelAnimation *pAnimation = obj3d_ ? obj3d_->GetCurrentModelAnimation() : nullptr;
    Bone *pBone = pAnimation ? pAnimation->GetBone() : nullptr;
    const Skeleton *pSkeleton = pBone ? &pBone->GetSkeletonRef() : nullptr;
    HandIkSolver *pSolver = GetHandIk();

    // まだ使っていないオブジェクトには、腕のジョイントを拾うところから始めてもらう
    if (!pSolver) {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("上腕と前腕を曲げ直して、手首を目標の位置へ運びます（武器を握る・壁に手をつく など）。\n"
                           "目標はゲーム側のコード（SetHandIkTarget）が毎フレーム渡します。ここで試しの目標を置いて確かめられます");
        ImGui::PopStyleColor();
        if (PrimaryButton("腕のジョイントを自動で拾う##handikdetect", ImVec2(-1, 0))) {
            pSolver = AcquireHandIk();
            const size_t found = (pSolver && pSkeleton) ? pSolver->AutoDetect(*pSkeleton) : 0;
            if (found > 0) {
                pSolver->GetSettings().enabled = true;
                ImGuiNotification::Post(std::format("手のIK: 腕を{}本拾いました", found), {0.45f, 0.68f, 0.52f, 1.0f});
            } else {
                ImGuiNotification::Post("手のIK: 手首のジョイントが見つかりませんでした", {0.82f, 0.58f, 0.36f, 1.0f});
            }
        }
        return;
    }

    HandIkSettings &settings = pSolver->GetSettings();
    std::vector<HandIkSolver::LimbState> &states = pSolver->GetLimbStates();
    if (states.size() != settings.limbs.size()) {
        states.resize(settings.limbs.size());
    }

    AccentCheckbox("手のIKを効かせる##handikenable", &settings.enabled, DebugTheme::kAccentGreen);
    ImGui::BeginDisabled(!settings.enabled);
    ImGui::DragFloat("効き具合##handikweight", &settings.weight, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("0で素のアニメーションのまま。見比べるときに使う");
    ImGui::DragFloat("出入りの速さ[1/秒]##handikblend", &settings.blendSpeed, 0.1f, 0.0f, 40.0f, "%.1f");
    ImGui::SetItemTooltip("目標ができた / 外れたときに、IK を効かせ始める / 抜く速さ");
    AccentCheckbox("腕と目標を線で描く##handikdebug", &settings.drawDebug, DebugTheme::kAccentGreen);

    ImGui::Spacing();
    SectionHeader("[ 腕 ]", DebugTheme::kAccentGreen);
    int removeLimb = -1;
    for (size_t i = 0; i < settings.limbs.size(); ++i) {
        HandIkLimb &limb = settings.limbs[i];
        HandIkSolver::LimbState &state = states[i];
        ImGui::PushID(static_cast<int>(i));
        const std::string label = std::format("{} {}###limb", ICON_FA_HAND_PAPER, limb.label.empty() ? std::string("(名前なし)") : limb.label);
        if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
            // 今の状態（何を目標にしているか・届いているか）
            if (state.useTestTarget) {
                StatusBadge("試しの目標", DebugTheme::kAccentYellow);
            } else if (state.hasTarget) {
                StatusBadge("ゲームの目標", DebugTheme::kAccentGreen);
            } else {
                StatusBadge("目標なし", DebugTheme::kTextDim);
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, state.reachRate > 1.0f && (state.useTestTarget || state.hasTarget) ? DebugTheme::kAccentOrange : DebugTheme::kTextDim);
            ImGui::Text("効き %.0f%%  距離/腕の長さ %.2f%s", state.blend * 100.0f, state.reachRate,
                        state.reachRate > 1.0f ? "（届かない）" : "");
            ImGui::PopStyleColor();

            ImGui::Checkbox("解く##limbEnabled", &limb.enabled);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##limbLabel", "呼び名（SetHandIkTarget で使う）", &limb.label);
            ImGui::TextDisabled("上腕");
            JointCombo("##limbUpper", limb.upperJoint, pSkeleton);
            ImGui::TextDisabled("前腕（肘）");
            JointCombo("##limbLower", limb.lowerJoint, pSkeleton);
            ImGui::TextDisabled("手首");
            JointCombo("##limbHand", limb.handJoint, pSkeleton);

            // エディタで試す: 手の今の位置から目標を動かして、腕の曲がり方を見る
            ImGui::Spacing();
            if (ImGui::Checkbox("試しの目標で確かめる##limbTest", &state.useTestTarget) && state.useTestTarget) {
                state.testTarget = state.animatedHandWorld;
            }
            ImGui::SetItemTooltip("ゲームからの目標より優先して、ここで置いた位置へ手を運ぶ（保存しない）");
            if (state.useTestTarget) {
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.65f);
                ImGui::DragFloat3("目標（ワールド）##limbTestTarget", &state.testTarget.x, 0.01f, 0.0f, 0.0f, "%.2f");
                if (NeutralButton("手の今の位置へ戻す##limbTestReset")) {
                    state.testTarget = state.animatedHandWorld;
                }
            }
            if (DangerButton(ICON_FA_TRASH_ALT " この腕を外す##limbRemove")) {
                removeLimb = static_cast<int>(i);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeLimb >= 0) {
        settings.limbs.erase(settings.limbs.begin() + removeLimb);
        states.erase(states.begin() + removeLimb);
    }
    if (ConfirmButton("+ 腕を追加##limbAdd")) {
        settings.limbs.push_back({});
        states.push_back({});
    }
    ImGui::SameLine();
    if (NeutralButton("左右の腕を自動で拾い直す##handikredetect") && pSkeleton) {
        const size_t found = pSolver->AutoDetect(*pSkeleton);
        ImGuiNotification::Post(std::format("手のIK: 腕を{}本拾い直しました", found), {0.45f, 0.68f, 0.52f, 1.0f});
    }
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextWrapped("ゲームから: SetHandIkTarget(\"左手\", 位置) を毎フレーム / 外すときは ClearHandIkTargets()");
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
#endif // USE_IMGUI
}

void BaseObject::DrawSpringBoneImGui() {
#ifdef USE_IMGUI
    ModelAnimation *pAnimation = obj3d_ ? obj3d_->GetCurrentModelAnimation() : nullptr;
    Bone *pBone = pAnimation ? pAnimation->GetBone() : nullptr;
    SpringBoneSolver *pSolver = GetSpringBone();

    // まだ使っていないオブジェクトには、揺らす骨を拾うところから始めてもらう
    if (!pSolver) {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("髪・スカート・しっぽなどの骨を、体の動きに遅れてついてくるように揺らします。\n"
                           "アニメーションを付け直さなくても、動きに揺れが乗ります");
        ImGui::PopStyleColor();
        if (PrimaryButton("揺らす骨を名前から自動で拾う##springdetect", ImVec2(-1, 0))) {
            pSolver = AcquireSpringBone();
            const size_t found = (pSolver && pBone) ? pSolver->AutoDetect(pBone->GetSkeletonRef()) : 0;
            if (found > 0) {
                pSolver->GetSettings().enabled = true;
                ImGuiNotification::Post(std::format("揺れ物: {}か所を拾いました", found), {0.45f, 0.68f, 0.52f, 1.0f});
            } else {
                ImGuiNotification::Post("揺れ物: hair / skirt / tail などの名前の骨が見つかりませんでした。根元を手で選んでください",
                                        {0.82f, 0.58f, 0.36f, 1.0f});
            }
        }
        if (NeutralButton("空で始める（根元の骨を手で選ぶ）##springempty", ImVec2(-1, 0))) {
            AcquireSpringBone();
        }
        return;
    }

    SpringBoneSettings &settings = pSolver->GetSettings();
    const Skeleton *pSkeleton = pBone ? &pBone->GetSkeletonRef() : nullptr;

    auto jointCombo = [&](const char *id, std::string &jointName) { return JointCombo(id, jointName, pSkeleton); };

    AccentCheckbox("揺れ物を効かせる##springenable", &settings.enabled, DebugTheme::kAccentOrange);
    ImGui::SameLine();
    StatusBadge(std::format("揺らしている骨 {} 本", pSolver->GetSimulatedJointCount()).c_str(),
                pSolver->GetSimulatedJointCount() > 0 ? DebugTheme::kAccentGreen : DebugTheme::kTextDim);

    ImGui::BeginDisabled(!settings.enabled);
    ImGui::DragFloat("効き具合##springweight", &settings.weight, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("0で素のアニメーションのまま。見比べるときに使う");
    ImGui::DragFloat("末端の骨の長さ##springtip", &settings.tipLengthRate, 0.01f, 0.0f, 2.0f, "親の骨の %.2f 倍");
    ImGui::SetItemTooltip("いちばん先の骨は子が無いので、親の骨を伸ばした仮の先端で向きを決める");
    AccentCheckbox("揺れと当たり球を線で描く##springdebug", &settings.drawDebug, DebugTheme::kAccentOrange);
    ImGui::SameLine();
    if (NeutralButton("揺れをリセット##springreset")) {
        pSolver->Reset();
    }
    ImGui::SetItemTooltip("今のアニメーションのポーズから揺れをやり直す");

    // ---- 揺らすまとまり ----
    ImGui::Spacing();
    SectionHeader("[ 揺らすまとまり（根元の骨ごと）]", DebugTheme::kAccentOrange);
    int removeChain = -1;
    for (size_t i = 0; i < settings.chains.size(); ++i) {
        SpringBoneChain &chain = settings.chains[i];
        ImGui::PushID(static_cast<int>(i));
        const std::string label = std::format("{} {}###chain", chain.enabled ? ICON_FA_WIND : ICON_FA_PAUSE,
                                              chain.rootJoint.empty() ? std::string("(根元が未選択)") : chain.rootJoint);
        if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
            jointCombo("##chainRoot", chain.rootJoint);
            ImGui::Checkbox("揺らす##chainEnabled", &chain.enabled);

            // よく使う組み合わせ（そのあと数値で詰める）
            ImGui::SameLine();
            ImGui::TextDisabled("プリセット:");
            struct Preset {
                const char *name;
                float stiffness;
                float drag;
                float gravity;
            };
            static constexpr Preset kPresets[] = {
                {"髪", 1.0f, 0.4f, 0.0f}, {"布", 0.5f, 0.2f, 0.6f}, {"しっぽ", 1.6f, 0.3f, 0.1f}, {"ぷるん", 2.5f, 0.15f, 0.0f}};
            for (const Preset &preset : kPresets) {
                ImGui::SameLine();
                if (ImGui::SmallButton(preset.name)) {
                    chain.stiffness = preset.stiffness;
                    chain.drag = preset.drag;
                    chain.gravityPower = preset.gravity;
                }
            }

            ImGui::DragFloat("戻る強さ##stiff", &chain.stiffness, 0.01f, 0.0f, 8.0f, "%.2f");
            ImGui::SetItemTooltip("元のポーズへ戻ろうとする力。大きいほど硬く、小さいほどだらんとする");
            ImGui::DragFloat("減衰##drag", &chain.drag, 0.005f, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("0でいつまでも揺れ続け、1で揺れずにすぐ止まる");
            ImGui::DragFloat("重力##gravity", &chain.gravityPower, 0.01f, 0.0f, 8.0f, "%.2f");
            ImGui::DragFloat3("重力の向き##gravityDir", &chain.gravityDirection.x, 0.01f, -1.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("ワールドの向き。横にすると風に吹かれているように見える");
            ImGui::DragFloat("当たりの太さ##hitRadius", &chain.hitRadius, 0.001f, 0.0f, 10.0f, "%.3f");
            ImGui::SetItemTooltip("下の「めり込み防止の球」との当たりに使う骨の太さ（モデルの長さの単位）");
            if (DangerButton(ICON_FA_TRASH_ALT " このまとまりを外す##chainRemove")) {
                removeChain = static_cast<int>(i);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeChain >= 0) {
        settings.chains.erase(settings.chains.begin() + removeChain);
        pSolver->Reset();
    }
    if (ConfirmButton("+ まとまりを追加##chainAdd")) {
        settings.chains.push_back({});
    }

    // ---- めり込み防止の球 ----
    ImGui::Spacing();
    SectionHeader("[ めり込み防止の球 ]", DebugTheme::kAccentOrange);
    int removeCollider = -1;
    for (size_t i = 0; i < settings.colliders.size(); ++i) {
        SpringBoneCollider &collider = settings.colliders[i];
        ImGui::PushID(static_cast<int>(1000 + i));
        const std::string label = std::format("{} {}###collider", ICON_FA_CIRCLE,
                                              collider.joint.empty() ? std::string("(骨が未選択)") : collider.joint);
        if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
            jointCombo("##colliderJoint", collider.joint);
            ImGui::DragFloat3("ずれ##colliderOffset", &collider.offset.x, 0.001f, 0.0f, 0.0f, "%.3f");
            ImGui::SetItemTooltip("骨の位置からのずれ（骨の向きに沿った空間）");
            ImGui::DragFloat("半径##colliderRadius", &collider.radius, 0.001f, 0.0f, 100.0f, "%.3f");
            if (DangerButton(ICON_FA_TRASH_ALT " この球を外す##colliderRemove")) {
                removeCollider = static_cast<int>(i);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeCollider >= 0) {
        settings.colliders.erase(settings.colliders.begin() + removeCollider);
    }
    if (ConfirmButton("+ 球を追加##colliderAdd")) {
        settings.colliders.push_back({});
    }

    ImGui::Spacing();
    if (NeutralButton("名前から自動で拾い直す##springredetect", ImVec2(-1, 0)) && pSkeleton) {
        const size_t found = pSolver->AutoDetect(*pSkeleton);
        ImGuiNotification::Post(std::format("揺れ物: {}か所を拾い直しました", found), {0.45f, 0.68f, 0.52f, 1.0f});
    }
    ImGui::SetItemTooltip("hair / skirt / tail / cape などを含む名前の骨を根元として拾い、頭に当たり球を置き直す（今の設定は消える）");
    ImGui::EndDisabled();
#endif // USE_IMGUI
}

void BaseObject::DrawFootIkImGui() {
#ifdef USE_IMGUI
    FootIkSolver *pSolver = GetFootIk();

    // まだ使っていないオブジェクトには、ジョイントを拾うところから始めてもらう
    if (!pSolver) {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("坂やでこぼこの地面で、足が地面にめり込んだり浮いたりしないように\n"
                           "腿・すね・足首を曲げ直します。まずは脚のジョイントを拾ってください");
        ImGui::PopStyleColor();
        if (PrimaryButton("脚のジョイントを自動で拾う##ikdetect", ImVec2(-1, 0))) {
            pSolver = AcquireFootIk();
            ModelAnimation *pAnimation = obj3d_ ? obj3d_->GetCurrentModelAnimation() : nullptr;
            Bone *pBone = pAnimation ? pAnimation->GetBone() : nullptr;
            if (pSolver && pBone && pSolver->AutoDetect(pBone->GetSkeletonRef())) {
                ImGuiNotification::Post(
                    std::format("足IK: 脚を{}本拾いました", pSolver->GetSettings().legs.size()),
                    {0.45f, 0.68f, 0.52f, 1.0f});
            } else {
                ImGuiNotification::Post("足IK: それらしい脚のジョイントが見つかりませんでした",
                                        {0.82f, 0.58f, 0.36f, 1.0f});
            }
        }
        ImGui::SetItemTooltip("mixamorig:LeftUpLeg / LeftLeg / LeftFoot のような名前を手がかりに探します");
        return;
    }

    FootIkSettings &settings = pSolver->GetSettings();

    AccentCheckbox("足IKを効かせる##ikenable", &settings.enabled, DebugTheme::kAccentPurple);
    ImGui::SetItemTooltip("地面へレイを飛ばして足の高さを合わせます。\n"
                          "対象は当たり判定が有効なコライダー（地形のメッシュコライダーなど）です");

    if (settings.legs.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
        ImGui::TextWrapped("脚が登録されていません。下の「自動で拾い直す」を押してください");
        ImGui::PopStyleColor();
    }

    ImGui::BeginDisabled(!settings.enabled);

    ImGui::DragFloat("効き具合##ikweight", &settings.weight, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("0で素のアニメーションのまま。見比べるときに使う");

    ImGui::Spacing();
    SectionHeader("[ 接地 ]", DebugTheme::kAccentPurple);
    ImGui::DragFloat("足の高さ##ikfooth", &settings.footHeight, 0.005f, 0.0f, 1.0f, "%.3f");
    ImGui::SetItemTooltip("足首を地面からどれだけ浮かせるか。靴の厚みぶん");
    ImGui::DragFloat("腰を下げる上限##ikhipdrop", &settings.maxHipDrop, 0.01f, 0.0f, 2.0f, "%.2f");
    ImGui::SetItemTooltip("段差で片足が届かないとき、ここまでなら腰を沈める。\n"
                          "大きくし過ぎると常にしゃがんで見える");
    ImGui::DragFloat("レイの開始高さ##ikrayup", &settings.rayUpOffset, 0.05f, 0.0f, 5.0f, "%.2f");
    ImGui::SetItemTooltip("足首より上のここから下向きにレイを撃つ。\n"
                          "足がすでに地面へめり込んでいる場合に拾い直すための遡り");
    ImGui::DragFloat("レイの長さ##ikraylen", &settings.rayLength, 0.05f, 0.1f, 20.0f, "%.2f");
    ImGui::SetItemTooltip("これより下に地面が無ければ空中とみなし、効きを抜きます");

    ImGui::Spacing();
    SectionHeader("[ 地面とみなすタグ ]", DebugTheme::kAccentPurple);
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextWrapped(settings.groundTags.empty()
                           ? "1つも選んでいないので、当たり判定が有効な全コライダーが対象です"
                           : "選んだタグのコライダーだけを地面として扱います");
    ImGui::PopStyleColor();
    {
        // 自分の体は常に対象外なので、ここに出てくるのは他のオブジェクトのタグだけ
        std::vector<std::string> tags(ColliderTagManager::GetInstance()->GetAllTags().begin(),
                                      ColliderTagManager::GetInstance()->GetAllTags().end());
        std::sort(tags.begin(), tags.end());
        for (const std::string &tag : tags) {
            auto it = std::find(settings.groundTags.begin(), settings.groundTags.end(), tag);
            bool selected = (it != settings.groundTags.end());
            if (AccentCheckbox((tag + "##iktag").c_str(), &selected, DebugTheme::kAccentCyan)) {
                if (selected) {
                    settings.groundTags.push_back(tag);
                } else {
                    settings.groundTags.erase(it);
                }
            }
        }
    }

    ImGui::Spacing();
    SectionHeader("[ 足の向き ]", DebugTheme::kAccentPurple);
    AccentCheckbox("地面の傾きに合わせる##ikalign", &settings.alignFootToGround, DebugTheme::kAccentBlue);
    ImGui::BeginDisabled(!settings.alignFootToGround);
    ImGui::DragFloat("傾けられる上限[度]##ikmaxang", &settings.maxFootAngleDegrees, 0.5f, 0.0f, 90.0f, "%.0f");
    ImGui::EndDisabled();

    ImGui::Spacing();
    SectionHeader("[ 追従の速さ ]", DebugTheme::kAccentPurple);
    ImGui::DragFloat("位置##ikposspeed", &settings.positionLerpSpeed, 0.1f, 0.0f, 60.0f, "%.1f");
    ImGui::SetItemTooltip("大きいほど地面に食いつく。小さいほど滑らかだが遅れる（0で即座）");
    ImGui::DragFloat("傾き##ikrotspeed", &settings.rotationLerpSpeed, 0.1f, 0.0f, 60.0f, "%.1f");

    ImGui::Spacing();
    SectionHeader("[ 状態 ]", DebugTheme::kAccentPurple);
    ReadOnlyRow("腰の沈み", "%.3f m", pSolver->GetHipOffset());
    for (size_t i = 0; i < settings.legs.size(); ++i) {
        ReadOnlyRow(settings.legs[i].footJoint.c_str(), "%s",
                    pSolver->IsLegGrounded(i) ? "接地" : "空中");
    }
    AccentCheckbox("レイと接地点を線で描く##ikdebug", &settings.drawDebug, DebugTheme::kAccentCyan);

    ImGui::EndDisabled();

    ImGui::Spacing();
    SectionHeader("[ 脚のジョイント ]", DebugTheme::kAccentPurple);
    for (const FootIkLeg &leg : settings.legs) {
        ReadOnlyRow("腿 / すね / 足首", "%s / %s / %s", leg.upperJoint.c_str(), leg.lowerJoint.c_str(),
                    leg.footJoint.c_str());
    }
    if (NeutralButton("自動で拾い直す##ikredetect", ImVec2(-1, 0))) {
        ModelAnimation *pAnimation = obj3d_ ? obj3d_->GetCurrentModelAnimation() : nullptr;
        Bone *pBone = pAnimation ? pAnimation->GetBone() : nullptr;
        // AutoDetect は SetSettings 経由で設定を作り直すので、有効フラグを引き継ぐ
        const bool wasEnabled = settings.enabled;
        if (pBone && pSolver->AutoDetect(pBone->GetSkeletonRef())) {
            pSolver->GetSettings().enabled = wasEnabled;
            ImGuiNotification::Post(std::format("足IK: 脚を{}本拾いました", pSolver->GetSettings().legs.size()),
                                    {0.45f, 0.68f, 0.52f, 1.0f});
        } else {
            ImGuiNotification::Post("足IK: それらしい脚のジョイントが見つかりませんでした",
                                    {0.82f, 0.58f, 0.36f, 1.0f});
        }
    }
    ReadOnlyRow("腰", "%s", settings.hipJoint.empty() ? "（未設定）" : settings.hipJoint.c_str());
#endif // USE_IMGUI
}

void BaseObject::DrawScaleEaseImGui() {
#ifdef USE_IMGUI
    // EasingType enum（0〜30）＋ Vector3 Amplitude 拡張（31〜33）の表示名
    // 31 = InElasticAmplitude, 32 = OutElasticAmplitude, 33 = InOutElasticAmplitude
    static const char *kModeNames[] = {
        "Linear",
        "InSine",
        "OutSine",
        "InOutSine",
        "InQuad",
        "OutQuad",
        "InOutQuad",
        "InCubic",
        "OutCubic",
        "InOutCubic",
        "InQuart",
        "OutQuart",
        "InOutQuart",
        "InQuint",
        "OutQuint",
        "InOutQuint",
        "InCirc",
        "OutCirc",
        "InOutCirc",
        "InExpo",
        "OutExpo",
        "InOutExpo",
        "InBack",
        "OutBack",
        "InOutBack",
        "InElastic",
        "OutElastic",
        "InOutElastic",
        "InBounce",
        "OutBounce",
        "InOutBounce",
        // Vector3 振幅による加算オフセット系（EasingType 外の拡張）
        "InElastic  [Amplitude]",
        "OutElastic [Amplitude]",
        "InOutElastic [Amplitude]",
    };
    constexpr int kModeCount = IM_ARRAYSIZE(kModeNames);

    // Amplitude 拡張モード（selectedMode >= 31）かどうかを判定する
    auto IsAmplitudeMode = [](int mode) { return mode >= 31; };

    // ----- イージングタイプ選択 -----
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##setype", &scaleEase_.selectedMode, kModeNames, kModeCount);

    ImGui::Spacing();

    // ----- 所要時間：ラベルを別行に表示して幅いっぱいにドラッグフィールドを配置 -----
    ImGui::TextUnformatted("所要時間");
    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat("##seT", &scaleEase_.totalTime, 0.05f, 0.05f, 10.0f, "%.2f s");

    ImGui::Spacing();

    if (IsAmplitudeMode(scaleEase_.selectedMode)) {
        // ----- Elastic Amplitude モード：軸ごとに独立した振幅でスケールを加算する -----
        SectionHeader("[ Elastic Amplitude ]", DebugTheme::kAccentGreen);

        // ラベルを別行に出すことで SetNextItemWidth(-1) でも名称が確認できる
        ImGui::TextUnformatted("振幅 (X / Y / Z)");
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat3("##seAmp", &scaleEase_.amplitude.x,
                          0.01f, -5.0f, 5.0f, "%.2f");

        ImGui::TextUnformatted("周期");
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##sePer", &scaleEase_.period,
                         0.01f, 0.01f, 2.0f, "%.2f");

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted(" スタート時の現在スケールを基準に振幅を加算");
        ImGui::PopStyleColor();
    } else {
        // ----- 通常イージングモード（start -> end 補間） -----
        SectionHeader("[ スタート / エンド スケール ]", DebugTheme::kAccentBlue);

        // ラベルをウィジェット上行に表示し、ボタン分の幅を残して配置
        ImGui::TextUnformatted("スタートスケール");
        ImGui::SetNextItemWidth(-80.0f);
        ImGui::DragFloat3("##seSS", &scaleEase_.startScale.x,
                          0.01f, 0.01f, 20.0f, "%.2f");
        ImGui::SameLine();
        // 現在のスケール値をスタート値としてキャプチャする
        if (ImGui::SmallButton("現在##cpSS")) {
            scaleEase_.startScale = transform_->scale_;
        }

        ImGui::TextUnformatted("エンドスケール");
        ImGui::SetNextItemWidth(-80.0f);
        ImGui::DragFloat3("##seES", &scaleEase_.endScale.x,
                          0.01f, 0.01f, 20.0f, "%.2f");
        ImGui::SameLine();
        // 現在のスケール値をエンド値としてキャプチャする
        if (ImGui::SmallButton("現在##cpES")) {
            scaleEase_.endScale = transform_->scale_;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ----- 再生中：プログレスバーとスケール更新 -----
    if (scaleEase_.isActive) {
        float progress = scaleEase_.currentTime / scaleEase_.totalTime;
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
        ImGui::Spacing();

        // DeltaTime でフレーム経過時間を加算して再生を進める
        float dt = ImGui::GetIO().DeltaTime;
        scaleEase_.currentTime += dt;

        if (scaleEase_.currentTime >= scaleEase_.totalTime) {
            // 再生完了：停止してスケールを終端値に確定する
            scaleEase_.currentTime = scaleEase_.totalTime;
            scaleEase_.isActive = false;
            if (IsAmplitudeMode(scaleEase_.selectedMode)) {
                transform_->scale_ = scaleEase_.baseScale;
            } else {
                transform_->scale_ = scaleEase_.endScale;
            }
        } else {
            // 再生中のスケール更新
            if (IsAmplitudeMode(scaleEase_.selectedMode)) {
                // Vector3 振幅をオフセットとしてベーススケールに加算する
                Vector3 offset;
                if (scaleEase_.selectedMode == 31) {
                    offset = EaseInElasticAmplitude(
                        scaleEase_.currentTime, scaleEase_.totalTime,
                        scaleEase_.amplitude, scaleEase_.period);
                } else if (scaleEase_.selectedMode == 32) {
                    offset = EaseOutElasticAmplitude(
                        scaleEase_.currentTime, scaleEase_.totalTime,
                        scaleEase_.amplitude, scaleEase_.period);
                } else {
                    offset = EaseInOutElasticAmplitude(
                        scaleEase_.currentTime, scaleEase_.totalTime,
                        scaleEase_.amplitude, scaleEase_.period);
                }
                transform_->scale_ = scaleEase_.baseScale + offset;
            } else {
                // EasingType 範囲（0〜30）は start -> end 補間
                transform_->scale_ = ApplyEasing(
                    static_cast<EasingType>(scaleEase_.selectedMode),
                    scaleEase_.startScale, scaleEase_.endScale,
                    scaleEase_.currentTime, scaleEase_.totalTime);
            }
        }
    }

    // ----- スタート / ストップボタン -----
    if (!scaleEase_.isActive) {
        if (PrimaryButton("スタート##sePlay", ImVec2(-1.0f, 0.0f))) {
            scaleEase_.currentTime = 0.0f;
            scaleEase_.isActive = true;
            // スタート時点のスケールをベースとして記録する
            scaleEase_.baseScale = transform_->scale_;
        }
    } else {
        if (NeutralButton("ストップ##seStop", ImVec2(-1.0f, 0.0f))) {
            scaleEase_.isActive = false;
            // ストップ時はスケールをスタート時点の値に戻す
            transform_->scale_ = scaleEase_.baseScale;
        }
    }
#endif
}

void BaseObject::ShowFileSelector() {
#ifdef USE_IMGUI
    // ShowFolder の GLTF ブラウザで選択パスを保持する
    // 選択確定後に「適用」ボタンで SetAnimation を呼び出す
    static std::string selectedGltfPath;

    ShowGltfFile(selectedGltfPath);

    ImGui::Spacing();

    if (!selectedGltfPath.empty()) {
        if (PrimaryButton("アニメーション適用##applyAnima", ImVec2(-1.0f, 0.0f))) {
            obj3d_->SetAnimation(selectedGltfPath);
        }
    }
#endif // USE_IMGUI
}

void BaseObject::ShowBlendModeCombo(BlendMode &currentMode) {
#ifdef USE_IMGUI

    // コンボボックスに表示する項目（日本語）
    static const char *blendModeItems[] = {
        "なし",      // kNone
        "通常",      // kNormal
        "加算",      // kAdd
        "減算",      // kSubtract
        "乗算",      // kMultiply
        "スクリーン" // kScreen
    };

    // 現在の選択状態（enumをintにキャスト）
    int currentIndex = static_cast<int>(currentMode);

    // コンボボックス表示
    if (ImGui::Combo("ブレンドモード", &currentIndex, blendModeItems, IM_ARRAYSIZE(blendModeItems))) {
        // ユーザーが選択を変更したときに反映
        currentMode = static_cast<BlendMode>(currentIndex);
    }
#endif // USE_IMGUI
}
} // namespace Hagine
