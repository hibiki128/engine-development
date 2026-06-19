#include "ImGuiNotification.h"
#include <algorithm>

namespace Hagine {
std::vector<ImGuiNotification::Notification> ImGuiNotification::notifications_;
std::vector<ImGuiNotification::Notification> ImGuiNotification::history_;

void ImGuiNotification::Post(const std::string &message, const Vector4 &color, int durationFrames) {
    Notification n = {message, color, durationFrames, durationFrames};
    notifications_.push_back(n);
    
    // 履歴に追加（最大100件）
    history_.push_back(n);
    if (history_.size() > 100) {
        history_.erase(history_.begin());
    }
}

void ImGuiNotification::Draw() {
#ifdef USE_IMGUI
    if (notifications_.empty()) return;

    // 通知ウィンドウの設定（画面の右下あたりに表示）
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 work_pos = viewport->WorkPos;
    ImVec2 work_size = viewport->WorkSize;
    ImVec2 window_pos = ImVec2(work_pos.x + work_size.x - 10.0f, work_pos.y + work_size.y - 10.0f);
    ImVec2 window_pos_pivot = ImVec2(1.0f, 1.0f);

    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
    ImGui::SetNextWindowBgAlpha(0.35f); // 背景を少し透明に

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##Notifications", nullptr, window_flags)) {
        for (auto it = notifications_.begin(); it != notifications_.end(); ) {
            // フェードアウト効果（最後の30フレームで透明度を下げる）
            float alpha = 1.0f;
            if (it->remainingFrames < 30) {
                alpha = static_cast<float>(it->remainingFrames) / 30.0f;
            }
            
            ImVec4 textColor = it->color;
            textColor.w *= alpha;

            ImGui::PushStyleColor(ImGuiCol_Text, textColor);
            ImGui::TextUnformatted(it->message.c_str());
            ImGui::PopStyleColor();

            it->remainingFrames--;
            if (it->remainingFrames <= 0) {
                it = notifications_.erase(it);
            } else {
                ++it;
            }
        }
    }
    ImGui::End();
#endif
}
} // namespace Hagine
