#define NOMINMAX
#include "BaseObject.h"
#include "BaseObjectManager.h"
#include "browser/ShowFolder.h"
#include "collider/CollisionManager.h"
#include "debug/profiler/CpuProfiler.h"
#include "frame/Frame.h"
#include "model/material/Material.h"
#include "object/Object3dInstancing.h"
#include "scene/SceneManager.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#ifdef USE_IMGUI
#include "utility/debug/imgui/AssetDragDrop.h"
#include <asset/AssetPath.h>
#include <graphics/texture/TextureManager.h>
#include <imgui_internal.h>
#include <implot.h>
#endif // DEBUG

// コライダーの設定UI（インスペクタのコライダータブ）。
namespace Hagine {
void BaseObject::DebugCollider() {
#ifdef USE_IMGUI
    if (colliders_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("  コライダーなし");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    for (size_t i = 0; i < colliders_.size(); ++i) {
        auto *col = colliders_[i].get();
        if (!col)
            continue;

        ImGui::PushID(static_cast<int>(i));

        // ヘッダー色：衝突中は赤寄り、非衝突は通常
        bool colliding = col->IsCollidingInCurrentFrame();
        ImVec4 hdrColor = colliding
                              ? ImVec4{0.75f, 0.25f, 0.25f, 0.40f}
                              : ImVec4{0.25f, 0.40f, 0.65f, 0.35f};
        ImVec4 hdrHov = colliding
                            ? ImVec4{0.85f, 0.35f, 0.35f, 0.50f}
                            : ImVec4{0.35f, 0.50f, 0.75f, 0.45f};

        ImGui::PushStyleColor(ImGuiCol_Header, hdrColor);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hdrHov);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, hdrHov);

        bool open = ImGui::CollapsingHeader(col->GetName().c_str());
        ImGui::PopStyleColor(3);

        if (!open) {
            ImGui::PopID();
            continue;
        }

        ImGui::Indent(8.0f);

        // ---- 状態バッジ行 ----
        {
            bool ena = col->IsEnabled();
            bool vis = col->IsVisible();

            // 「当たり判定が生きているか」「線を出しているか」の入切なのでトグルにする。
            // 見ただけで今どちらかが分かり、チェックボックスより誤読しにくい
            InlineColumns badgeCols(3);
            if (ThemedToggle("有効##cena", &ena, DebugTheme::kAccentGreen))
                col->SetEnabled(ena);
            badgeCols.Next(1);
            if (ThemedToggle("表示##cvis", &vis, DebugTheme::kAccentBlue))
                col->SetVisible(vis);

            badgeCols.Next(2);
            StatusBadge(colliding ? "衝突中" : "待機",
                        colliding ? DebugTheme::kAccentRed : DebugTheme::kAccentGreen);
        }

        ImGui::Spacing();

        // ---- タグ設定 ----
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgBlue);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.55f, 1.0f, 0.20f));
        if (ImGui::TreeNodeEx("タグ設定##ctag", ImGuiTreeNodeFlags_SpanAvailWidth)) {
            col->ImGuiTagSettings();
            ImGui::TreePop();
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();

        // ---- コライダー形状パラメータ ----
        SectionHeader("[ 形状パラメータ ]", DebugTheme::kAccentCyan);

        if (auto *sphere = dynamic_cast<SphereCollider *>(col)) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentCyan);
            ImGui::TextUnformatted("種別: 球体");
            ImGui::PopStyleColor();

            float r = sphere->GetRadius();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("半径");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgBlue);
            if (ImGui::DragFloat("##srad", &r, 0.1f, 0.1f, 100.f, "%.2f"))
                sphere->SetRadius(r);
            ImGui::PopStyleColor();

            Vector3 off = sphere->GetOffset();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("オフセット (X / Y / Z)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgBlue);
            if (ImGui::DragFloat3("##soff", &off.x, 0.1f, -1000.f, 1000.f, "%.2f"))
                sphere->SetOffset(off);
            ImGui::PopStyleColor();
        } else if (auto *aabb = dynamic_cast<AABBCollider *>(col)) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentGreen);
            ImGui::TextUnformatted("種別: AABB");
            ImGui::PopStyleColor();

            Vector3 sz = aabb->GetSize();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("サイズ (X / Y / Z)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgGreen);
            if (ImGui::DragFloat3("##asz", &sz.x, 0.1f, 0.1f, 100.f, "%.2f"))
                aabb->SetSize(sz);
            ImGui::PopStyleColor();

            Vector3 off = aabb->GetOffset();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("オフセット (X / Y / Z)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgGreen);
            if (ImGui::DragFloat3("##aoff", &off.x, 0.1f, -1000.f, 1000.f, "%.2f"))
                aabb->SetOffset(off);
            ImGui::PopStyleColor();
        } else if (auto *obb = dynamic_cast<OBBCollider *>(col)) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentPurple);
            ImGui::TextUnformatted("種別: OBB");
            ImGui::PopStyleColor();

            Vector3 sz = obb->GetSize();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("サイズ (X / Y / Z)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgPurple);
            if (ImGui::DragFloat3("##osz", &sz.x, 0.1f, 0.1f, 100.f, "%.2f"))
                obb->SetSize(sz);
            ImGui::PopStyleColor();

            Vector3 rotOff = obb->GetRotationOffset();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("回転オフセット (X / Y / Z)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgPurple);
            if (ImGui::DragFloat3("##oroff", &rotOff.x, 0.1f, -1000.f, 1000.f, "%.2f"))
                obb->SetRotationOffset(rotOff);
            ImGui::PopStyleColor();

            Vector3 posOff = obb->GetPositionOffset();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("位置オフセット (X / Y / Z)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgPurple);
            if (ImGui::DragFloat3("##opoff", &posOff.x, 0.1f, -1000.f, 1000.f, "%.2f"))
                obb->SetPositionOffSet(posOff);
            ImGui::PopStyleColor();
        } else if (auto *cyl = dynamic_cast<CylinderCollider *>(col)) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentYellow);
            ImGui::TextUnformatted("種別: 円柱");
            ImGui::PopStyleColor();

            float r = cyl->GetRadius();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("半径");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgYellow);
            if (ImGui::DragFloat("##cyrad", &r, 0.1f, 0.1f, 1000.f, "%.2f"))
                cyl->SetRadius(r);
            ImGui::PopStyleColor();

            float h = cyl->GetHeight();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("高さ");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgYellow);
            if (ImGui::DragFloat("##cyhgt", &h, 0.1f, 0.1f, 1000.f, "%.2f"))
                cyl->SetHeight(h);
            ImGui::PopStyleColor();

            bool inward = cyl->IsInward();
            ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentYellow);
            if (ImGui::Checkbox("内側に閉じ込める##cyin", &inward))
                cyl->SetInward(inward);
            ImGui::PopStyleColor();
        } else if (auto *mesh = dynamic_cast<MeshCollider *>(col)) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
            ImGui::TextUnformatted("種別: メッシュ");
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::Text("三角形数: %d", static_cast<int>(mesh->GetTriangleCount()));
            if (!mesh->GetSourceModelPath().empty())
                ImGui::Text("ソース: %s", mesh->GetSourceModelPath().c_str());
            ImGui::PopStyleColor();

            bool wire = mesh->IsWireframeVisible();
            ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentOrange);
            if (ImGui::Checkbox("ワイヤーフレーム表示##mwire", &wire))
                mesh->SetWireframeVisible(wire);
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // ---- 保存 / 削除ボタン ----
        float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        if (ConfirmButton("保存##colsv", ImVec2(bw, 0.0f))) {
            col->SaveToJson();
            ImGuiNotification::Post("コライダーを保存しました: " + col->GetName(), {0.45f, 0.68f, 0.52f, 1.0f});
        }

        ImGui::SameLine();

        if (DangerButton("削除##coldel", ImVec2(bw, 0.0f))) {
            // colliders_ は unique_ptr 所有。erase で ~ColliderBase が走り
            // CollisionManager から自動的に Unregister される（delete は呼ばない）。
            std::string removedName = col->GetName();
            colliders_.erase(colliders_.begin() + i);
            ImGuiNotification::Post("コライダーを削除しました: " + removedName, {0.80f, 0.46f, 0.46f, 1.0f});
            ImGui::Unindent(8.0f);
            ImGui::PopID();
            break;
        }

        ImGui::Unindent(8.0f);
        ImGui::Spacing();
        ImGui::PopID();
    }

    ImGui::PopStyleVar(2);
#endif
}

} // namespace Hagine
