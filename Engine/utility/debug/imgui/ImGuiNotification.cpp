#include "ImGuiNotification.h"
#include <algorithm>
#include <chrono>
#include <ctime>

namespace Hagine {
std::vector<ImGuiNotification::Notification> ImGuiNotification::notifications_;
std::vector<ImGuiNotification::Notification> ImGuiNotification::history_;
int ImGuiNotification::muteDepth_ = 0;

namespace {
/// <summary>今の時刻を HH:MM:SS で返す</summary>
std::string CurrentTimeText()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_s(&local, &now);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
    return buffer;
}

// 同時に画面へ出すトーストの上限（古いものから押し出す）
constexpr size_t kMaxToasts = 5;
// 出てくるときに横から滑り込むフレーム数
constexpr int kSlideInFrames = 10;
// 消えるときに薄くなるフレーム数
constexpr int kFadeOutFrames = 30;
} // namespace

void ImGuiNotification::Post(const std::string &message, const Vector4 &color, int durationFrames)
{
    Notification n = {message, color, durationFrames, durationFrames, CurrentTimeText()};

    // ミュート中でも履歴には積む。後から「何が起きたか」を追えるようにするため、
    // 消すのは画面へのトーストだけにしている
    if (muteDepth_ <= 0)
    {
        PushToast(n);
    }

    // 履歴に追加（最大200件）
    history_.push_back(n);
    if (history_.size() > 200)
    {
        history_.erase(history_.begin());
    }
}

void ImGuiNotification::PostError(const std::string &message, const Vector4 &color, int durationFrames)
{
    Notification n = {message, color, durationFrames, durationFrames, CurrentTimeText()};
    n.important = true;

    // シーンの読み直し中（ミュート中）に見つからないアセットこそ知りたいので、ミュートは無視する
    PushToast(n);

    history_.push_back(n);
    if (history_.size() > 200)
    {
        history_.erase(history_.begin());
    }
}

void ImGuiNotification::PushToast(Notification n)
{
    notifications_.push_back(std::move(n));
    while (notifications_.size() > kMaxToasts)
    {
        // 重要でないものから古い順に押し出す。起動時は「読み込みました」が大量に流れるので、
        // 古い順に消すだけだと警告がすぐ押し流されて見えなくなる
        auto victim = std::find_if(notifications_.begin(), notifications_.end(),
                                   [](const Notification &toast) { return !toast.important; });
        if (victim == notifications_.end())
        {
            victim = notifications_.begin();
        }
        notifications_.erase(victim);
    }
}

void ImGuiNotification::PostWithAction(const std::string &message, const Vector4 &color, const std::string &actionLabel,
                                       std::function<void()> action, int durationFrames)
{
    Notification n = {message, color, durationFrames, durationFrames, CurrentTimeText(), actionLabel, std::move(action)};
    if (muteDepth_ <= 0)
    {
        PushToast(n);
    }
    // 履歴にはボタン無しで残す（後から押しても意味が変わってしまうため）
    n.actionLabel.clear();
    n.action = nullptr;
    history_.push_back(n);
    if (history_.size() > 200)
    {
        history_.erase(history_.begin());
    }
}

void ImGuiNotification::Draw()
{
#ifdef USE_IMGUI
    if (notifications_.empty())
        return;

    // 画面の右下に、下から積み上げるカードとして出す
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 workPos = viewport->WorkPos;
    const ImVec2 workSize = viewport->WorkSize;
    const ImVec2 windowPos(workPos.x + workSize.x - 12.0f, workPos.y + workSize.y - 12.0f);

    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowViewport(viewport->ID);
    // ボタン付きのカードがあるときだけ入力を受け取る（無いときは下の窓のクリックを邪魔しない）
    const bool anyAction = std::any_of(notifications_.begin(), notifications_.end(),
                                       [](const Notification &n) { return static_cast<bool>(n.action); });
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;
    if (!anyAction)
    {
        flags |= ImGuiWindowFlags_NoInputs;
    }

    // ボタンの処理は一覧を回し終えてから呼ぶ（処理の中で通知を出すと notifications_ が伸びて走査が壊れる）
    std::function<void()> pendingAction;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 6.0f));
    if (ImGui::Begin("##Notifications", nullptr, flags))
    {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const float padX = 12.0f;
        const float padY = 8.0f;
        const float barWidth = 3.0f;
        const float maxTextWidth = 420.0f;

        // 全カードの幅をそろえる（右端がそろっている方が読みやすい）。ボタン付きはボタンのぶん広げる
        float cardWidth = 0.0f;
        for (const Notification &n : notifications_)
        {
            const ImVec2 size = ImGui::CalcTextSize(n.message.c_str(), nullptr, false, maxTextWidth);
            float width = size.x;
            if (n.action)
            {
                width += ImGui::CalcTextSize(n.actionLabel.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f + 12.0f;
            }
            cardWidth = std::max(cardWidth, width);
        }
        cardWidth += padX * 2.0f + barWidth;

        for (auto it = notifications_.begin(); it != notifications_.end();)
        {
            const int age = it->totalFrames - it->remainingFrames;
            float alpha = 1.0f;
            if (it->remainingFrames < kFadeOutFrames)
            {
                alpha = static_cast<float>(it->remainingFrames) / static_cast<float>(kFadeOutFrames);
            }
            // 出てきた直後は右から滑り込む
            const float slide = (age < kSlideInFrames) ? (1.0f - static_cast<float>(age) / kSlideInFrames) : 0.0f;
            const float offsetX = slide * slide * 40.0f;
            alpha *= 1.0f - slide * 0.6f;

            const ImVec2 textSize = ImGui::CalcTextSize(it->message.c_str(), nullptr, false, maxTextWidth);
            const float contentHeight = it->action ? std::max(textSize.y, ImGui::GetFrameHeight()) : textSize.y;
            const ImVec2 cardSize(cardWidth, contentHeight + padY * 2.0f);
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const ImVec2 min(cursor.x + offsetX, cursor.y);
            const ImVec2 max(min.x + cardSize.x, min.y + cardSize.y);

            ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
            bg.w = 0.92f * alpha;
            ImVec4 border = ImGui::GetStyleColorVec4(ImGuiCol_Border);
            border.w *= alpha;
            ImVec4 accent(it->color.x, it->color.y, it->color.z, alpha);
            ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            text.w *= alpha;

            drawList->AddRectFilled(min, max, ImGui::GetColorU32(bg), 6.0f);
            drawList->AddRect(min, max, ImGui::GetColorU32(border), 6.0f);
            drawList->AddRectFilled(ImVec2(min.x + 4.0f, min.y + 6.0f), ImVec2(min.x + 4.0f + barWidth, max.y - 6.0f),
                                    ImGui::GetColorU32(accent), 1.5f);
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                              ImVec2(min.x + padX + barWidth, min.y + padY + (contentHeight - textSize.y) * 0.5f),
                              ImGui::GetColorU32(text), it->message.c_str(), nullptr, maxTextWidth);

            bool closeNow = false;
            bool hovered = false;
            if (it->action)
            {
                // カードの右端にボタン
                const float buttonWidth = ImGui::CalcTextSize(it->actionLabel.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                const ImVec2 buttonPos(max.x - padX - buttonWidth, min.y + padY);
                const ImVec2 after = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(buttonPos);
                ImGui::PushID(&*it);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x * 0.45f, accent.y * 0.45f, accent.z * 0.45f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x * 0.65f, accent.y * 0.65f, accent.z * 0.65f, 1.0f));
                if (ImGui::Button(it->actionLabel.c_str()))
                {
                    pendingAction = it->action;
                    closeNow = true;
                }
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar();
                ImGui::PopID();
                ImGui::SetCursorScreenPos(after);
                hovered = ImGui::IsMouseHoveringRect(min, max);
            }
            ImGui::Dummy(cardSize);

            // ボタン付きのカードは、マウスを重ねている間は消さない（押そうとしている途中で消えないように）
            if (!hovered)
            {
                it->remainingFrames--;
            }
            else if (it->remainingFrames < kFadeOutFrames)
            {
                it->remainingFrames = kFadeOutFrames;
            }
            if (closeNow || it->remainingFrames <= 0)
            {
                it = notifications_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);

    if (pendingAction)
    {
        pendingAction();
    }
#endif
}
} // namespace Hagine
