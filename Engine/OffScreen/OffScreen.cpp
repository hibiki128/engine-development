#include "OffScreen.h"
#include "DirectXCommon.h"
#include <Frame.h>
#include <format>
#ifdef _DEBUG
#include "imgui.h"
#include "Utility/Debug/ImGui/ImGuiNotification.h"
#include "Utility/Debug/ImGui/Debugui_improved.h"
#endif

namespace Hagine {
void OffScreen::Initialize() {
    dxCommon_ = DirectXCommon::GetInstance();
    SrvManager *srvManager = SrvManager::GetInstance();
    PipeLineManager *psoManager = PipeLineManager::GetInstance();

    renderer_.Initialize(dxCommon_, srvManager, psoManager);

    // エフェクトチェーンとDirectXCommonのポインタを渡して初期化
    dataManager_.Initialize(&effectChain_, dxCommon_);
}

void OffScreen::Draw() {
    renderer_.Draw(effectChain_, Frame::DeltaTime());
}

void OffScreen::DrawWithoutCopy() {
    renderer_.DrawWithoutCopy(effectChain_, Frame::DeltaTime());
}

void OffScreen::BeginCompositePass() {
    renderer_.BeginCompositePass();
}

void OffScreen::EndCompositePass() {
    renderer_.EndCompositePass();
}

void OffScreen::BlitToOffScreen(uint32_t prevFinalResultSrvIndex) {
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu =
        SrvManager::GetInstance()->GetGPUDescriptorHandle(prevFinalResultSrvIndex);
    renderer_.BlitToOffScreen(srvGpu);
}

void OffScreen::SetProjection(Matrix4x4 projectionMatrix) {
    projectionMatrix_ = projectionMatrix;

    // 深度ベースアウトライン等、射影逆行列が必要なエフェクトに反映
    const auto &slots = effectChain_.GetSlots();
    for (int i = 0; i < PostEffectChain::kMaxSlots; ++i) {
        if (!slots[i].occupied) {
            continue;
        }
        if (auto *p = effectChain_.GetParams<OutlineDepthParams>(i)) {
            p->SetProjectionInverse(projectionMatrix);
        }
    }
}

uint32_t OffScreen::GetFinalResultSrvIndex() const {
    return renderer_.GetFinalResultSrvIndex();
}

void OffScreen::CopyFinalResultToBackBuffer() {
    renderer_.CopyFinalResultToBackBuffer();
}

// -------------------------------------------------------
//  エフェクト管理API（OffScreenのラッパー）
// -------------------------------------------------------

int OffScreen::AddEffect(ShaderMode mode, const std::string &name, int slotIndex) {
    int result = effectChain_.AddEffect(mode, name, dxCommon_, slotIndex);

    // 射影逆行列が必要なエフェクトへの即時反映
    if (result != -1) {
        if (auto *p = effectChain_.GetParams<OutlineDepthParams>(result)) {
            p->SetProjectionInverse(projectionMatrix_);
        }
    }
    return result;
}

bool OffScreen::RemoveEffect(int slotIndex) {
    return effectChain_.RemoveEffect(slotIndex);
}

int OffScreen::RemoveEffectByName(const std::string &name) {
    return effectChain_.RemoveEffectByName(name);
}

int OffScreen::RemoveAllEffectsByName(const std::string &name) {
    return effectChain_.RemoveAllEffectsByName(name);
}

bool OffScreen::SetEffectEnabled(int slotIndex, bool enabled) {
    return effectChain_.SetEnabled(slotIndex, enabled);
}

bool OffScreen::MoveEffectUp(int slotIndex) {
    return effectChain_.MoveUp(slotIndex);
}

bool OffScreen::MoveEffectDown(int slotIndex) {
    return effectChain_.MoveDown(slotIndex);
}

void OffScreen::LoadData(const std::string &fileName) {
    dataManager_.LoadData(fileName);
}

// -------------------------------------------------------
//  ImGui
// -------------------------------------------------------

void OffScreen::Setting() {
#ifdef _DEBUG
    const char *shaderModeItems[] = {
        "なし", "グレイ", "ビネット", "スムース", "ガウス",
        "アウトライン(エッジ検出)", "アウトライン(深度ベース)",
        "ブラー", "シネマティック", "ディゾルブ", "ランダム", "集中線", "ピクセル化", "ブルーム", "レトロ", "衝撃波"};

    // 削除用の控えめな赤ボタン色をまとめて適用するヘルパー
    auto pushDangerButton = [] {
        ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgRed);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.46f, 0.46f, 0.40f));
    };

    // ── ステータス ──
    SectionHeader("[ ポストエフェクト ]", DebugTheme::kAccentBlue);
    const int freeSlots = effectChain_.GetFreeSlotCount();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted("空きスロット");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    StatusBadge(std::format("{} / {}", freeSlots, PostEffectChain::kMaxSlots).c_str(),
                freeSlots > 0 ? DebugTheme::kAccentGreen : DebugTheme::kAccentOrange);

    ImGui::Spacing();

    // ── クイック切替：チェックでON/OFF（未追加なら自動追加）──
    ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgGreen);
    bool quickOpen = ImGui::CollapsingHeader("クイック切替", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (quickOpen) {
        const auto &quickSlots = effectChain_.GetSlots();
        for (int m = 1; m < static_cast<int>(ShaderMode::kCount); ++m) {
            ShaderMode mode = static_cast<ShaderMode>(m);

            // このモードを持つスロットを探す
            int foundSlot = -1;
            for (int i = 0; i < PostEffectChain::kMaxSlots; ++i) {
                if (quickSlots[i].occupied && quickSlots[i].params &&
                    quickSlots[i].params->GetMode() == mode) {
                    foundSlot = i;
                    break;
                }
            }

            bool active = (foundSlot >= 0) && quickSlots[foundSlot].enabled;
            ImGui::PushID(1000 + m);
            if (ImGui::Checkbox(shaderModeItems[m], &active)) {
                if (active) {
                    if (foundSlot < 0) {
                        std::string defaultName = std::string("quick_") + shaderModeItems[m];
                        AddEffect(mode, defaultName, -1);
                    } else {
                        effectChain_.SetEnabled(foundSlot, true);
                    }
                } else {
                    if (foundSlot >= 0) {
                        effectChain_.SetEnabled(foundSlot, false);
                    }
                }
            }

            // 有効時はパラメータも展開
            if (active && foundSlot >= 0) {
                ImGui::SameLine();
                pushDangerButton();
                bool del = ImGui::SmallButton("削除");
                ImGui::PopStyleColor(2);
                if (del) {
                    effectChain_.RemoveEffect(foundSlot);
                    ImGuiNotification::Post(std::format("エフェクトを削除しました: {}", shaderModeItems[m]), {0.82f, 0.58f, 0.36f, 1.0f});
                } else {
                    ImGui::Indent();
                    quickSlots[foundSlot].params->DrawUI();
                    ImGui::Unindent();
                }
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        if (ImGui::Button("全部OFF")) {
            const auto &s = effectChain_.GetSlots();
            for (int i = 0; i < PostEffectChain::kMaxSlots; ++i) {
                if (s[i].occupied)
                    effectChain_.SetEnabled(i, false);
            }
            ImGuiNotification::Post("全エフェクトを無効化しました", {0.82f, 0.58f, 0.36f, 1.0f});
        }
        ImGui::SameLine();
        pushDangerButton();
        bool clearAll = ImGui::Button("全部削除");
        ImGui::PopStyleColor(2);
        if (clearAll) {
            for (int i = PostEffectChain::kMaxSlots - 1; i >= 0; --i) {
                effectChain_.RemoveEffect(i);
            }
            ImGuiNotification::Post("全エフェクトを削除しました", {0.82f, 0.58f, 0.36f, 1.0f});
        }
    }

    ImGui::Spacing();

    // ── エフェクト追加 ──
    SectionHeader("[ エフェクト追加 ]", DebugTheme::kAccentGreen);

    static int selectedMode = 0;
    static char effectName[64] = "";
    static int targetSlot = -1; // -1で自動

    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted("種類 / 名前 / スロット");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##addmode", &selectedMode, shaderModeItems, IM_ARRAYSIZE(shaderModeItems));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##addname", "エフェクト名（省略可）", effectName, sizeof(effectName));
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##addslot", &targetSlot, -1, PostEffectChain::kMaxSlots - 1, "スロット: %d");
    ImGui::SetItemTooltip("-1 で空きスロットへ自動配置");

    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgGreen);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.68f, 0.52f, 0.40f));
    bool addClicked = ImGui::Button("エフェクトを追加", ImVec2(-1, 0));
    ImGui::PopStyleColor(2);
    if (addClicked) {
        int result = AddEffect(static_cast<ShaderMode>(selectedMode), effectName, targetSlot);
        if (result == -1) {
            ImGuiNotification::Post("追加に失敗しました（スロット満杯か指定スロット使用中）", {0.85f, 0.42f, 0.42f, 1.0f});
        } else {
            ImGuiNotification::Post(std::format("エフェクトを追加しました: {}", shaderModeItems[selectedMode]), {0.45f, 0.68f, 0.52f, 1.0f});
        }
    }

    ImGui::Spacing();

    // ── スロット一覧（行をドラッグ＆ドロップで並べ替え）──
    SectionHeader("[ スロット一覧 ]", DebugTheme::kAccentOrange);
    ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
    ImGui::TextUnformatted("行をドラッグして別の行に重ねると順序を入れ替えます");
    ImGui::PopStyleColor();

    const auto &slots = effectChain_.GetSlots();
    int dragFrom = -1; // ドラッグ元スロット
    int dragTo = -1;   // ドロップ先スロット（ループ後にまとめて入れ替える）
    for (int i = 0; i < PostEffectChain::kMaxSlots; ++i) {
        ImGui::PushID(i);

        if (!slots[i].occupied) {
            // 空きスロットは控えめに表示
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::Text("[%d]  空き", i);
            ImGui::PopStyleColor();
            ImGui::PopID();
            continue;
        }

        const auto &slot = slots[i];
        ShaderMode mode = slot.params->GetMode();

        // 有効トグル
        bool enabled = slot.enabled;
        if (ImGui::Checkbox("##en", &enabled))
            effectChain_.SetEnabled(i, enabled);
        ImGui::SetItemTooltip("このエフェクトの有効 / 無効");
        ImGui::SameLine();

        // ドラッグ可能な行ラベル（末尾の操作ボタンぶんの幅を空ける）
        std::string rowLabel = std::format("[{}] {}  ({})", i, slot.name,
                                           shaderModeItems[static_cast<int>(mode)]);
        float reserve = 150.0f;
        float selW = ImGui::GetContentRegionAvail().x - reserve;
        if (selW < 80.0f)
            selW = 80.0f;
        ImGui::Selectable(rowLabel.c_str(), false, ImGuiSelectableFlags_AllowOverlap, ImVec2(selW, 0.0f));
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("FX_SLOT", &i, sizeof(int));
            ImGui::Text("移動: %s", slot.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("FX_SLOT")) {
                dragFrom = *static_cast<const int *>(payload->Data);
                dragTo = i;
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SetItemTooltip("ドラッグで並べ替え");

        // 並べ替え / 削除
        ImGui::SameLine();
        if (ImGui::SmallButton("上") && i > 0)
            effectChain_.MoveUp(i);
        ImGui::SetItemTooltip("ひとつ上へ");
        ImGui::SameLine();
        if (ImGui::SmallButton("下") && i < PostEffectChain::kMaxSlots - 1)
            effectChain_.MoveDown(i);
        ImGui::SetItemTooltip("ひとつ下へ");
        ImGui::SameLine();
        pushDangerButton();
        bool delSlot = ImGui::SmallButton("削除");
        ImGui::PopStyleColor(2);
        if (delSlot) {
            effectChain_.RemoveEffect(i);
            ImGuiNotification::Post(std::format("スロット[{}]を削除しました", i), {0.82f, 0.58f, 0.36f, 1.0f});
            ImGui::PopID();
            continue;
        }

        // 有効時のみパラメータUI表示
        if (slot.enabled) {
            ImGui::Indent();
            slot.params->DrawUI();
            ImGui::Unindent();
        }

        ImGui::PopID();
        ImGui::Separator();
    }
    // ドロップ確定後にまとめて入れ替える（ループ中のスロット変更を避ける）
    if (dragFrom >= 0 && dragTo >= 0 && dragFrom != dragTo) {
        if (effectChain_.SwapSlots(dragFrom, dragTo)) {
            ImGuiNotification::Post(std::format("スロット {} と {} を入れ替えました", dragFrom, dragTo),
                                    {0.42f, 0.66f, 0.68f, 1.0f});
        }
    }

    ImGui::Spacing();

    // ── セーブ / ロード ──
    SectionHeader("[ セーブ / ロード ]", DebugTheme::kAccentPurple);
    static char saveFileName[256] = "OffScreenData";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##ofsfile", "ファイル名", saveFileName, sizeof(saveFileName));
    ImGui::Spacing();

    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.58f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.52f, 0.70f, 0.95f));
    if (ImGui::Button("セーブ", ImVec2(bw, 0))) {
        dataManager_.SaveData(std::string(saveFileName));
        ImGuiNotification::Post(std::format("オフスクリーン設定を保存しました: {}", saveFileName), {0.45f, 0.68f, 0.52f, 1.0f});
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.40f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.60f, 0.50f, 0.95f));
    if (ImGui::Button("ロード", ImVec2(bw, 0))) {
        dataManager_.LoadData(std::string(saveFileName));
        ImGuiNotification::Post(std::format("オフスクリーン設定を読み込みました: {}", saveFileName), {0.42f, 0.66f, 0.68f, 1.0f});
    }
    ImGui::PopStyleColor(2);
#endif
}
} // namespace Hagine
