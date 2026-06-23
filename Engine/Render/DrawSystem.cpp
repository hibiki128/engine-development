#include "DrawSystem.h"
#include "DirectXCommon.h"
#include "Collider/CollisionManager.h"
#include "Data/DataHandler.h"
#include "Utility/Debug/ImGui/ImGuiNotification.h"
#include "Graphics/Srv/SrvManager.h"
#include "Particle/ParticleEditor.h"
#include "Scene/SceneManager.h"
#include <Shadow/ShadowMap.h>
#include <algorithm>
#ifdef _DEBUG
#include "Particle/CSParticle/ParticleCSEditor.h"
#endif
#ifdef _DEBUG
#include "imgui.h"
#include "line/DrawLine3D.h"
#include "Utility/Debug/ImGui/Debugui_improved.h"
#include <set>
#include <vector>
#endif

namespace Hagine {
void DrawSystem::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                             OffScreen *offscreen, SceneManager *sceneManager,
                             CollisionManager *collision) {
    dxCommon_    = dxCommon;
    srvManager_  = srvManager;
    sceneManager_ = sceneManager;
    collision_   = collision;

    stageOffScreens_[0] = offscreen;
    nextStageIndex_ = 1;
}

// -------------------------------------------------------
// 描画エントリ登録
// -------------------------------------------------------

void DrawSystem::RegisterImpl(std::string name, int stageIndex,
                               std::function<void(const ViewProjection &)> drawFunc) {
    for (auto &e : entries_) {
        if (e.name == name) {
            e.stageIndex = stageIndex;
            e.draw = std::move(drawFunc);
            return;
        }
    }
    entries_.push_back({std::move(name), stageIndex, std::move(drawFunc), true});
}

void DrawSystem::Register(std::string name, DrawLayer layer,
                           std::function<void(const ViewProjection &)> drawFunc) {
    int stage = (layer == DrawLayer::kPostEffect) ? kUILayer : static_cast<int>(layer);
    RegisterImpl(std::move(name), stage, std::move(drawFunc));
}

void DrawSystem::Register(std::string name, int stageIndex,
                           std::function<void(const ViewProjection &)> drawFunc) {
    RegisterImpl(std::move(name), stageIndex, std::move(drawFunc));
}

void DrawSystem::Unregister(const std::string &name) {
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [&name](const DrawEntry &e) { return e.name == name; }),
        entries_.end());
}

void DrawSystem::Clear() {
    entries_.clear();
}

// -------------------------------------------------------
// マルチステージ管理
// -------------------------------------------------------

int DrawSystem::CreateStage() {
    int idx = nextStageIndex_++;
    auto owned = std::make_unique<OffScreen>();
    owned->Initialize();
    stageOffScreens_[idx] = owned.get();
    ownedOffScreens_.push_back(std::move(owned));
    return idx;
}

OffScreen *DrawSystem::GetStageOffScreen(int stageIndex) {
    auto it = stageOffScreens_.find(stageIndex);
    return (it != stageOffScreens_.end()) ? it->second : nullptr;
}

// -------------------------------------------------------
// メイン描画パイプライン
// -------------------------------------------------------

void DrawSystem::Draw(const ViewProjection &vp) {
    // ─── GPU パーティクル Compute フェーズ（全エミッターを一括実行して Direct Queue に Wait 挿入）───
    {
        for (auto &entry : entries_) {
            if (entry.enabled && entry.stageIndex == kGPUParticleCompute) {
                entry.draw(vp);
            }
        }
#ifdef _DEBUG
        // GPUパーティクルエディタのエミッターをシーン非依存でシミュレートする。
        // 描画はプレビュー窓(RenderPreview)の専用VPだけが行うためゲームシーンには漏れない。
        // （Compute=シミュレーションのみここで実行。Graphics はプレビューに隔離済み）
        ParticleCSEditor::GetInstance()->DrawAllCompute(vp);
#endif

        // 記録が無ければ ExecuteComputeCommands は自己ガードで no-op、Wait も signaled 済み値への待ちで無害。
        dxCommon_->ExecuteComputeCommands();
        dxCommon_->WaitForComputeOnDirectQueue();
    }

#ifdef _DEBUG
    // ─── GPUパーティクル プレビュー窓を描画（Compute 完了後・ステージ束ね前）───
    // Compute 済みの生存バッファを VS 読み取り可能な状態のままプレビューVPで再描画する。
    // 後段のステージループ(PreRenderTexture)がオフスクリーンRTと全画面ビューポートを束ね直すため復元不要。
    ParticleCSEditor::GetInstance()->RenderPreview();
#endif

    // ─── シャドウプレパス ───
    ShadowMap *shadowMap = ShadowMap::GetInstance();
    shadowMap->Update(); // 有効フラグをGPUバッファに反映（無効時もenabledを0にするため毎フレーム呼ぶ）
    if (shadowMap->IsEnabled()) {
        shadowMap->BeginShadowPass();
        srvManager_->SetDescriptorHeap();
        shadowMap->SetShadowPassActive(true);
        for (auto &entry : entries_) {
            if (entry.enabled && entry.stageIndex == 0) {
                entry.draw(vp);
            }
        }
        shadowMap->SetShadowPassActive(false);
        shadowMap->EndShadowPass();
    }

    // 登録済みステージ（kUILayer を除く）を昇順で処理
    std::vector<int> sortedStages;
    for (auto &[idx, _] : stageOffScreens_) {
        sortedStages.push_back(idx);
    }
    std::sort(sortedStages.begin(), sortedStages.end());

    OffScreen *lastOffScreen = nullptr;

    for (size_t si = 0; si < sortedStages.size(); ++si) {
        int stageIdx = sortedStages[si];
        OffScreen *stageOS = stageOffScreens_.at(stageIdx);

        // ─── オフスクリーンテクスチャへ描画 ───
        dxCommon_->PreRenderTexture();
        srvManager_->SetDescriptorHeap();

        // 前ステージの結果を背景として合成
        if (lastOffScreen) {
            stageOS->BlitToOffScreen(lastOffScreen->GetFinalResultSrvIndex());
        }

        for (auto &entry : entries_) {
            if (entry.enabled && entry.stageIndex == stageIdx) {
                entry.draw(vp);
            }
        }

        // 注: GPUパーティクルエディタのエミッターは「プレビュー窓のみ」で確認する。
        // 以前はここで DrawAllGraphics(vp) を呼び現在のシーン offscreen にも描画していたが、
        // 編集中のパーティクルがゲームシーンに漏れて見えてしまうため撤去。
        // シミュレーション（DrawAllCompute）は上のフェーズで実行済みで、
        // 描画は RenderPreview()（プレビュー専用VP）だけが行う。

#ifdef _DEBUG
        if (stageIdx == 0) {
            DrawLine3D::GetInstance()->Draw(vp);
            collision_->DebugDraw(vp);
        }
#endif

        // ─── ポストエフェクト適用 → finalResult（コピーなし）───
        if (si == 0) {
            dxCommon_->PreDraw(); // 初回: バックバッファも遷移
        } else {
            dxCommon_->PreDrawForEffects(); // 2回目以降: バックバッファ遷移なし
        }
        stageOS->SetProjection(vp.matProjection_);
        stageOS->DrawWithoutCopy();
        dxCommon_->TransitionDepthBarrier();

        lastOffScreen = stageOS;
    }

    if (!lastOffScreen) {
        ParticleEditor::GetInstance()->UpdateFrameStats();
        return;
    }

    // ─── UI・シーン遷移を finalResult に合成 ───
    lastOffScreen->BeginCompositePass();
    srvManager_->SetDescriptorHeap();

    for (auto &entry : entries_) {
        if (entry.enabled && entry.stageIndex == kUILayer) {
            entry.draw(vp);
        }
    }

    // シーン遷移は最前面（UIの上）
    sceneManager_->DrawTransition();

    lastOffScreen->EndCompositePass();

    // ─── finalResult（フルフレーム）をバックバッファへコピー ───
    lastOffScreen->CopyFinalResultToBackBuffer();

    ParticleEditor::GetInstance()->UpdateFrameStats();
}

// -------------------------------------------------------
// ImGui
// -------------------------------------------------------

void DrawSystem::UpdateImGui(bool *open) {
#ifdef _DEBUG
    // 表示名は日本語、ウィンドウIDは "DrawSystem" のまま（保存済みレイアウトとの互換維持）
    if (ImGui::Begin("描画システム###DrawSystem", open, ImGuiWindowFlags_NoFocusOnAppearing)) {
        SectionHeader("[ 描画エントリ ]", DebugTheme::kAccentBlue);
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::Text("登録: %zu 件", entries_.size());
        ImGui::PopStyleColor();

        if (ImGui::BeginTable("##DrawEntries", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("表示", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("ステージ", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthStretch);

            for (auto &entry : entries_) {
                ImGui::PushID(entry.name.c_str());
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
                ImGui::Checkbox("##en", &entry.enabled);
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("描画の ON / OFF");

                ImGui::TableNextColumn();
                if (entry.stageIndex == kUILayer) {
                    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgPurple);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f, 0.50f, 0.74f, 0.40f));
                    if (ImGui::SmallButton("UI Layer"))
                        entry.stageIndex = 0;
                    ImGui::PopStyleColor(2);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgBlue);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.60f, 0.78f, 0.40f));
                    char label[32];
                    snprintf(label, sizeof(label), "Stage %d", entry.stageIndex);
                    if (ImGui::SmallButton(label))
                        entry.stageIndex = kUILayer;
                    ImGui::PopStyleColor(2);
                }
                ImGui::SetItemTooltip("クリックで UI レイヤー / ステージ を切り替え");

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(entry.name.c_str());

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        // ───────────────────────────────────────────────
        // ステージ / レイヤー管理
        //   描画エントリ(コールバック)が属するステージ(描画パス)を確認・移動する。
        //   実際の描画順: GPU Compute → Stage0 → Stage1 → ... → UI Layer
        // ───────────────────────────────────────────────
        ImGui::Spacing();
        SectionHeader("[ ステージ / 描画順 ]", DebugTheme::kAccentCyan);

        // 利用可能なステージ(レイヤー)を描画順に集める
        std::vector<int> stages;
        stages.push_back(kGPUParticleCompute);
        {
            std::vector<int> render;
            for (auto &[idx, _] : stageOffScreens_) {
                render.push_back(idx);
            }
            std::sort(render.begin(), render.end());
            for (int idx : render) {
                stages.push_back(idx);
            }
        }
        stages.push_back(kUILayer);

        auto layerLabel = [&](int idx) -> std::string {
            if (idx == kGPUParticleCompute) return "GPU Compute";
            if (idx == kUILayer) return "UI Layer";
            return "Stage " + std::to_string(idx);
        };
        auto layerColor = [&](int idx) -> ImVec4 {
            if (idx == kGPUParticleCompute) return DebugTheme::kAccentOrange;
            if (idx == kUILayer) return DebugTheme::kAccentPurple;
            return DebugTheme::kAccentBlue;
        };

        // --- 描画順の表示（各ステージに属するエントリ） ---
        ImGui::BeginChild("##drawOrder", ImVec2(0, 150), true);
        for (size_t i = 0; i < stages.size(); ++i) {
            int idx = stages[i];
            int count = 0;
            for (auto &e : entries_) {
                if (e.stageIndex == idx) count++;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, layerColor(idx));
            ImGui::Text("%zu. %s", i + 1, layerLabel(idx).c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::Text("(%d)", count);
            ImGui::PopStyleColor();
            for (auto &e : entries_) {
                if (e.stageIndex != idx) continue;
                ImGui::PushStyleColor(ImGuiCol_Text, e.enabled ? DebugTheme::kTextReadOnly : DebugTheme::kTextDim);
                ImGui::Text("        %s%s", e.name.c_str(), e.enabled ? "" : "  (非表示)");
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndChild();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("上から順に描画（各Stageにポストエフェクト適用、UI Layerは最前面合成）");
        ImGui::PopStyleColor();

        // --- ステージ追加（現状未対応） ---
        // OffScreen/RendererBuffer が RTV ディスクリプタを固定スロット(3,4,5)に直書きしており、
        // 複数ステージを同時に持てない（RTVヒープも8スロット固定）。
        // 動的RTV確保に対応するまで CreateStage() はUIから呼ばない。
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("＊ステージ追加は現在のエンジン構成では未対応です（OffScreenがRTVスロットを共有しているため）");
        ImGui::PopStyleColor();

        // ───────────────────────────────────────────────
        // エントリのステージ移動（二画面 + ステージ選択ドロップダウン）
        // ───────────────────────────────────────────────
        ImGui::Spacing();
        SectionHeader("[ エントリのステージ移動 ]", DebugTheme::kAccentGreen);

        // 移動先候補は GPU Compute を除いた Stage群 + UI Layer
        std::vector<int> moveStages;
        for (int s : stages) {
            if (s != kGPUParticleCompute) {
                moveStages.push_back(s);
            }
        }

        static int leftStage = 0;         // 既定: Stage 0
        static int rightStage = kUILayer; // 既定: UI Layer
        auto clampStage = [&](int &s) {
            if (!moveStages.empty() && std::find(moveStages.begin(), moveStages.end(), s) == moveStages.end()) {
                s = moveStages.front();
            }
        };
        clampStage(leftStage);
        clampStage(rightStage);

        // 選択状態（エントリ名をキーに保持）
        static std::set<std::string> leftSel;
        static std::set<std::string> rightSel;

        float availWidth = ImGui::GetContentRegionAvail().x;
        float btnWidth = 48.0f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float listWidth = (availWidth - btnWidth - spacing * 2) * 0.5f;

        // ステージ選択ドロップダウン（変更時は選択をクリア）
        auto stageCombo = [&](const char *id, int &sel, std::set<std::string> &selItems) {
            ImGui::PushID(id);
            ImGui::SetNextItemWidth(listWidth);
            if (ImGui::BeginCombo("##stage", layerLabel(sel).c_str())) {
                for (int s : moveStages) {
                    bool selected = (s == sel);
                    if (ImGui::Selectable(layerLabel(s).c_str(), selected) && s != sel) {
                        sel = s;
                        selItems.clear();
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        };
        stageCombo("lc", leftStage, leftSel);
        ImGui::SameLine(listWidth + spacing + btnWidth + spacing);
        stageCombo("rc", rightStage, rightSel);

        // ステージ内のエントリをリスト表示（ダブルクリックで反対側ステージへ移動）
        auto entryList = [&](const char *id, int stage, std::set<std::string> &sel, int moveTo) {
            ImGui::BeginChild(id, ImVec2(listWidth, 200), true);
            bool any = false;
            for (auto &e : entries_) {
                if (e.stageIndex != stage) {
                    continue;
                }
                any = true;
                bool selected = sel.count(e.name) > 0;
                ImGui::PushStyleColor(ImGuiCol_Text, e.enabled ? DebugTheme::kTextReadOnly : DebugTheme::kTextDim);
                bool clicked = ImGui::Selectable(e.name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick);
                ImGui::PopStyleColor();
                if (clicked) {
                    if (!ImGui::GetIO().KeyCtrl) {
                        sel.clear();
                    }
                    if (selected) {
                        sel.erase(e.name);
                    } else {
                        sel.insert(e.name);
                    }
                    if (ImGui::IsMouseDoubleClicked(0) && stage != moveTo) {
                        e.stageIndex = moveTo;
                        sel.clear();
                    }
                }
            }
            if (!any) {
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
                ImGui::TextUnformatted("（エントリなし）");
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
        };

        // 選択中エントリをまとめて反対側ステージへ移す
        auto moveEntries = [&](int fromStage, int toStage, std::set<std::string> &sel) {
            for (auto &e : entries_) {
                if (e.stageIndex == fromStage && sel.count(e.name) > 0) {
                    e.stageIndex = toStage;
                }
            }
            sel.clear();
        };
        auto moveButton = [&](const char *label, bool enabled) -> bool {
            if (!enabled) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.30f, 0.30f, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.30f, 0.30f, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.30f, 0.30f, 0.40f));
            }
            bool pressed = ImGui::Button(label, ImVec2(btnWidth, 30)) && enabled;
            if (!enabled) {
                ImGui::PopStyleColor(3);
            }
            return pressed;
        };

        bool sameStage = (leftStage == rightStage);
        entryList("##leftEntries", leftStage, leftSel, rightStage);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, 60));
        if (moveButton("->##mvR", !leftSel.empty() && !sameStage)) {
            moveEntries(leftStage, rightStage, leftSel);
        }
        ImGui::SetItemTooltip("選択を右のステージへ移動");
        if (moveButton("<-##mvL", !rightSel.empty() && !sameStage)) {
            moveEntries(rightStage, leftStage, rightSel);
        }
        ImGui::SetItemTooltip("選択を左のステージへ移動");
        ImGui::EndGroup();
        ImGui::SameLine();
        entryList("##rightEntries", rightStage, rightSel, leftStage);

        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("操作: ドロップダウンで対象ステージを選択 / Ctrl+クリックで複数選択 / ダブルクリックで反対側へ移動");
        ImGui::PopStyleColor();

        ImGui::Spacing();
        SectionHeader("[ セーブ / ロード ]", DebugTheme::kAccentPurple);
        float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.58f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.52f, 0.70f, 0.95f));
        if (ImGui::Button("保存", ImVec2(bw, 0))) { SaveConfig(); }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.40f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.60f, 0.50f, 0.95f));
        if (ImGui::Button("読み込み", ImVec2(bw, 0))) { LoadConfig(); }
        ImGui::PopStyleColor(2);
    }
    ImGui::End();
#endif
}

// -------------------------------------------------------
// JSON 保存 / 読み込み
// -------------------------------------------------------

void DrawSystem::SaveConfig(const std::string &fileName) {
    auto data = std::make_unique<DataHandler>("DrawSystem", fileName);
    int count = static_cast<int>(entries_.size());
    data->Save("count", count);
    for (int i = 0; i < count; ++i) {
        const auto &e = entries_[i];
        std::string prefix = "entry_" + std::to_string(i) + "_";
        data->Save(prefix + "name", e.name);
        data->Save(prefix + "stage", e.stageIndex);
        data->Save(prefix + "enabled", static_cast<int>(e.enabled));
    }
    ImGuiNotification::Post("描画設定を保存しました: " + fileName, {0.2f, 0.8f, 0.2f, 1.0f});
}

void DrawSystem::LoadConfig(const std::string &fileName) {
    auto data = std::make_unique<DataHandler>("DrawSystem", fileName);
    int count = data->Load<int>("count", 0);
    for (int i = 0; i < count; ++i) {
        std::string prefix = "entry_" + std::to_string(i) + "_";
        std::string name    = data->Load<std::string>(prefix + "name", "");
        int stage   = data->Load<int>(prefix + "stage", 0);
        int enabled = data->Load<int>(prefix + "enabled", 1);

        for (auto &e : entries_) {
            if (e.name == name) {
                e.stageIndex = stage;
                e.enabled    = static_cast<bool>(enabled);
                break;
            }
        }
    }
    ImGuiNotification::Post("描画設定を読み込みました: " + fileName, {0.2f, 0.8f, 0.8f, 1.0f});
}
} // namespace Hagine
