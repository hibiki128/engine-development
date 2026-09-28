#include "ParticleEditor.h"
#include <asset/AssetPath.h>
#include "DirectXCommon.h"
#ifdef USE_IMGUI
#include "utility/debug/imgui/ImGuizmoManager.h"
// DebugUIHelper.h は ImGui:: を使うので imgui.h（ImGuizmoManager.h 経由）の後に include する
#include "utility/debug/imgui/DebugUIHelper.h"
#include <icon/IconsFontAwesome5.h>
#endif // USE_IMGUI
#include <utility/debug/imgui/ImGuiNotification.h>
#include <algorithm>
#include <format>

// CPUパーティクルエディタの「作成」「削除」タブ（GPU 側の ParticleCSEditorCreate.cpp と同じ並び）。
// CPU の粒は GPU のようなプロシージャル形状を持たないので、プリセットは置かず
// 「空のエミッター・複製・保存済みの読み込み」をそろえる。

namespace Hagine {

void ParticleEditor::DrawQuickCreate()
{
#ifdef USE_IMGUI
    const std::string jsonDir = AssetPath::Json("Particle");
    const auto isLoaded = [this](const std::string &n) { return emitters_.contains(n); };

    // ---- 名前 ----
    ImGui::SeparatorText(ICON_FA_TAG " 名前");
    const ParticleEditorUI::NameCheck nameCheck =
        ParticleEditorUI::DrawNameField("##cpuQuickName", quickName_, "新しいエミッターの名前", jsonDir, isLoaded);

    // ---- 空のエミッター ----
    ImGui::BeginDisabled(!nameCheck.ok);
    if (PrimaryButton(ICON_FA_PLUS " 空のエミッターを作る", ImVec2(-1.0f, 0.0f)))
    {
        AddParticleEmitter(quickName_);
        selectedEmitterName_ = quickName_;
        quickName_.clear();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("作ったら「エミッター」タブでグループ（粒の形）を足してください");

    // ---- 複製 ----
    ImGui::SeparatorText(ICON_FA_COPY " 今あるエミッターを複製");
    if (emitters_.empty())
    {
        DimText("エミッターがありません");
    }
    else
    {
        if (!emitters_.contains(duplicateSource_))
            duplicateSource_.clear();
        if (duplicateSource_.empty() && emitters_.contains(selectedEmitterName_))
            duplicateSource_ = selectedEmitterName_;
        std::vector<std::string> names = GetEmitterNames();
        std::sort(names.begin(), names.end());
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize("  複製  ").x - spacing * 2.0f);
        if (ImGui::BeginCombo("##cpuDupSource", duplicateSource_.empty() ? "元にするエミッター" : duplicateSource_.c_str()))
        {
            for (const std::string &n : names)
            {
                if (ImGui::Selectable(n.c_str(), n == duplicateSource_))
                    duplicateSource_ = n;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        const bool nameUsable = quickName_.empty() || nameCheck.ok;
        ImGui::BeginDisabled(duplicateSource_.empty() || !nameUsable);
        if (PrimaryButton(" 複製 "))
        {
            const std::string name =
                quickName_.empty() ? ParticleEditorUI::MakeUniqueEmitterName(duplicateSource_ + "_copy", jsonDir, isLoaded) : quickName_;
            if (ParticleEmitter *src = GetEmitterByName(duplicateSource_))
            {
                // 今の設定を新しい名前の保存ファイルへ書き、それを読み込んで作る
                {
                    ImGuiNotification::ScopedMute mute;
                    src->SaveToJsonAs(name);
                }
                AddParticleEmitter(name);
                selectedEmitterName_ = name;
                ImGuiNotification::Post(std::format("「{}」を複製しました: {}", duplicateSource_, name), {0.45f, 0.68f, 0.52f, 1.0f});
                quickName_.clear();
            }
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("今の設定（保存していない調整も含む）をそのまま写して、新しい名前で保存します");
    }

    // ---- 保存済みから読み込む ----
    ImGui::SeparatorText(ICON_FA_FOLDER_OPEN " 保存済みから読み込む");
    ParticleEditorUI::DrawSavedFileLoader("cpuLoad", loadSearch_, jsonDir, isLoaded, [this](const std::string &n) {
        AddParticleEmitter(n);
        selectedEmitterName_ = n;
    });

    ImGui::Spacing();
    if (NeutralButton(ICON_FA_STOP " すべての自動発生を止める"))
    {
        for (auto &emitter : emitters_)
        {
            emitter.second->SetIsAuto(false);
        }
    }
#endif // USE_IMGUI
}

void ParticleEditor::DrawDeleteTab()
{
#ifdef USE_IMGUI
    ParticleEditorUI::DrawEmitterDeleteList(
        "cpuEmitterDelete", deleteState_, GetEmitterNames(), AssetPath::Json("Particle"),
        [this](const std::string &n) {
            // 描画中のフレームがこのエミッターのバッファを使っているかもしれないので、終わるのを待ってから消す
            DirectXCommon::GetInstance()->WaitForGPU();
            emitters_.erase(n);
            if (selectedEmitterName_ == n)
            {
                selectedEmitterName_.clear();
                selectedEmitterIndex_ = 0;
            }
        },
        [this](const std::string &n) {
            if (!emitters_.contains(n))
            {
                AddParticleEmitter(n);
                selectedEmitterName_ = n;
            }
        });
#endif // USE_IMGUI
}

} // namespace Hagine
