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
#include <format>
#include <icon/IconsFontAwesome5.h>
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

    // 中身の編集はコライダー窓と同じ物（CollisionManager::DrawColliderEditor）を使う。
    // ここでは一覧と、窓で開くボタンだけを持つ（2か所で別々の画面を直さなくて済むように）
    CollisionManager *pCollision = CollisionManager::GetInstance();
    for (size_t i = 0; i < colliders_.size(); ++i) {
        ColliderBase *col = colliders_[i].get();
        if (!col) {
            continue;
        }
        ImGui::PushID(static_cast<int>(i));

        const bool colliding = col->IsCollidingInCurrentFrame();
        const std::string label = std::format("{}{}###collider", colliding ? ICON_FA_BOLT " " : "",
                                              col->GetName().empty() ? std::string("(名前なし)") : col->GetName());
        ImGui::PushStyleColor(ImGuiCol_Text, colliding ? DebugTheme::kAccentRed : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap);
        ImGui::PopStyleColor();
        // 押した・開いたコライダーを選んで、シーンで点滅させる（どれの話をしているか見えるように）
        if (ImGui::IsItemClicked() || ImGui::IsItemToggledOpen()) {
            pCollision->SelectInEditor(col);
        }
        if (open) {
            // 開いている間は、選んでいるコライダーの強調を出し続ける
            pCollision->MarkEditorShown();
        }

        // 行の右端に「コライダー窓で開く」
        const float buttonWidth = ImGui::GetFrameHeight();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonWidth);
        {
            ScopedButtonColors colors(DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
            if (ImGui::Button(ICON_FA_EXTERNAL_LINK_ALT "##openCollider", ImVec2(buttonWidth, 0.0f))) {
                pCollision->OpenInEditor(col);
            }
        }
        ImGui::SetItemTooltip("コライダー窓で開く（一覧・追加・削除もそこでできる）");

        if (open) {
            pCollision->DrawColliderEditor(col);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
#endif
}

} // namespace Hagine
