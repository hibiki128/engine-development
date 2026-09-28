#ifdef USE_IMGUI
#include "BehaviorTreeEditor.h"
#include "BehaviorTreeEditorStyle.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include <algorithm>
#include <icon/IconsFontAwesome5.h>
#include <set>

namespace ed = ax::NodeEditor;

// 右ペインのインスペクタ（選択ノードの情報と設定の編集）。
// 設定の項目は登録表（BTNodeTypeDesc::params）から作るので、種類ごとの分岐は持たない。

namespace Hagine {

using namespace BTEditorStyle;

void BehaviorTreeEditor::DrawInspector()
{
    CaptionText("インスペクター");
    ImGui::Separator();
    ImGui::Spacing();

    const std::vector<int> selected = GetSelectedNodeIds();
    BTNodeData *pTarget = selected.empty() ? nullptr : asset_.FindNode(selected.front());

    if (!pTarget)
    {
        DimText("ノードが選択されていません。");
        DimText("キャンバス上のノードをクリックすると、");
        DimText("ここでパラメータを編集できます。");

        // ツリー全体のようす
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        std::set<int> hasParent;
        for (const auto &link : asset_.links)
            hasParent.insert(BTPin::NodeOfInput(link.end));
        const int rootId = asset_.FindRootNodeId();
        int orphans = 0;
        for (const auto &n : asset_.nodes)
        {
            if (n.id != rootId && !hasParent.count(n.id))
                ++orphans;
        }
        const BTNodeData *root = asset_.FindNode(rootId);
        ImGui::Text("ノード %d 個 / 接続 %d 本", static_cast<int>(asset_.nodes.size()), static_cast<int>(asset_.links.size()));
        ImGui::Text("根: %s", root ? root->title.c_str() : "---");
        if (orphans > 0)
        {
            ImGui::TextColored(kBadgeOrphan, "未接続のノード: %d 個", orphans);
            DimText("根まで繋がっていないノードは実行されません。");
        }
        return;
    }

    if (selected.size() > 1)
    {
        ImGui::Text("%d 個選択中（先頭のノードを表示）", static_cast<int>(selected.size()));
        if (NeutralButton(ICON_FA_COPY " まとめて複製"))
            DuplicateNodes(selected);
        ImGui::SameLine();
        if (DangerButton(ICON_FA_TRASH " まとめて削除"))
        {
            for (int id : selected)
                DeleteNode(id);
            return;
        }
        ImGui::Separator();
    }

    // ---- ヘッダー（名前・種別・説明） ----
    const BTNodeTypeDesc *desc = BehaviorTreeRegistry::Get().Find(pTarget->type);
    ImGui::TextColored(AccentOf(desc), "%s", pTarget->title.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##btTitle", "表示名", &pTarget->title))
        undoLabel_ = "ノードの名前を変更";
    ImGui::TextDisabled("種別: %s%s%s  (ID:%d)", KindLabel(desc), desc ? " / " : "", desc ? desc->category.c_str() : "", pTarget->id);
    ImGui::Spacing();
    if (desc)
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", desc->description.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- 設定 ----
    if (desc)
    {
        DrawNodeParameters(*pTarget, *desc);
    }
    else
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(DebugTheme::kAccentOrange, "種類 %d は登録されていません。", pTarget->type);
        DimText("ゲーム側で BehaviorTreeRegistry に登録すると編集・実行できます（保存しても値は消えません）。");
        ImGui::PopTextWrapPos();
    }

    // ---- 操作 ----
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const int targetId = pTarget->id;
    ImGui::BeginDisabled(!pDebugContext_);
    if (NeutralButton(ICON_FA_VIAL " 単体テスト"))
        StartSingleTest(targetId);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_COPY " 複製"))
        DuplicateNodes({targetId});
    ImGui::SameLine();
    if (DangerButton(ICON_FA_TRASH " 削除"))
    {
        DeleteNode(targetId);
        return;
    }

    // ---- 実行中は編集内容を反映するための再ビルドを提供 ----
    if (isRunning_)
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ConfirmButton(ICON_FA_SYNC " 変更をツリーへ反映（再ビルド）", ImVec2(-1.0f, 0.0f)))
            BuildAndRun();
        DimText("※実行中の値の変更はこのボタンで反映されます");
    }
}

void BehaviorTreeEditor::DrawNodeParameters(BTNodeData &node, const BTNodeTypeDesc &desc)
{
    ImGui::PushID(node.id);
    ImGui::PushItemWidth(150.0f);

    // ---- 重み付きランダムの出力 ----
    if (desc.kind == BTNodeKind::WeightedRandom)
    {
        ImGui::TextUnformatted("各出力の重み:");
        float total = 0.0f;
        for (const auto &w : node.weightedOutputs)
            total += std::max(0.0f, w.weight);
        int removeIndex = -1;
        for (int i = 0; i < static_cast<int>(node.weightedOutputs.size()); ++i)
        {
            ImGui::PushID(i);
            auto &w = node.weightedOutputs[static_cast<size_t>(i)];
            const std::string label = "出力" + std::to_string(i + 1);
            ImGui::DragFloat(label.c_str(), &w.weight, 0.05f, 0.0f, 100.0f, "%.2f");
            ImGui::SameLine();
            ImGui::TextDisabled("%.0f%%", total > 0.0f ? std::max(0.0f, w.weight) / total * 100.0f : 0.0f);
            if (node.weightedOutputs.size() > 1)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_TRASH))
                    removeIndex = i;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("この出力と、その先への接続を消す");
            }
            ImGui::PopID();
        }
        if (removeIndex >= 0)
            RemoveWeightedOutput(node, removeIndex);
        ImGui::Spacing();
        if (NeutralButton(ICON_FA_PLUS " 出力を追加"))
        {
            node.weightedOutputs.push_back({nextPinId_++, 1.0f});
            undoLabel_ = "重みの出力を追加";
            modified_ = true;
        }
        DimText("％は合計に対する割合（選ばれる確率）");
    }
    else if (desc.params.empty() && desc.textLabel.empty())
    {
        DimText("編集可能なパラメータはありません。");
    }

    // ---- 数値の設定 ----
    for (const auto &p : desc.params)
    {
        float &value = node.params[static_cast<size_t>(p.index)];
        ImGui::PushID(p.index);
        switch (p.kind)
        {
        case BTParamKind::Float:
            ImGui::DragFloat(p.label.c_str(), &value, p.speed, p.min, p.max, p.format);
            FloatNContextMenu("##paramMenu", &value, 1, &desc.defaults[static_cast<size_t>(p.index)]);
            break;
        case BTParamKind::Ratio:
            ImGui::SliderFloat(p.label.c_str(), &value, p.min, p.max, p.format);
            FloatNContextMenu("##paramMenu", &value, 1, &desc.defaults[static_cast<size_t>(p.index)]);
            break;
        case BTParamKind::Int: {
            int v = static_cast<int>(value);
            if (ImGui::DragInt(p.label.c_str(), &v, p.speed, static_cast<int>(p.min), static_cast<int>(p.max)))
                value = static_cast<float>(v);
            break;
        }
        case BTParamKind::Bool: {
            bool on = value >= 1.0f;
            if (ImGui::Checkbox(p.label.c_str(), &on))
                value = on ? 1.0f : 0.0f;
            break;
        }
        }
        ImGui::PopID();
    }

    // ---- 文字の設定（選択肢から選ぶ）----
    if (!desc.textLabel.empty())
    {
        ImGui::SetNextItemWidth(180.0f);
        if (desc.textChoices.empty())
        {
            ImGui::InputText(desc.textLabel.c_str(), &node.text);
        }
        else if (ImGui::BeginCombo(desc.textLabel.c_str(), node.text.c_str()))
        {
            for (const auto &choice : desc.textChoices)
            {
                const bool selected = choice == node.text;
                if (ImGui::Selectable(choice.c_str(), selected))
                    node.text = choice;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    if (!desc.hint.empty())
    {
        ImGui::PushTextWrapPos(0.0f);
        DimText(desc.hint.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::PopItemWidth();
    ImGui::PopID();
}

} // namespace Hagine

#endif // USE_IMGUI
