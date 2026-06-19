#pragma once
#include <string>
#include <vector>
#ifdef _DEBUG
#include <imgui.h>
#endif // _DEBUG
#include"type/Vector4.h"


/// @brief ImGuiによる通知システム
namespace Hagine {
class ImGuiNotification {
  public:
    struct Notification {
        std::string message;
        Vector4 color;
        int remainingFrames;
        int totalFrames;
    };

    /// @brief 通知を投稿する
    /// @param message メッセージ
    /// @param color テキスト色（デフォルトは緑系）
    /// @param durationFrames 表示フレーム数（デフォルト180フレーム = 約3秒）
    static void Post(const std::string &message,
                     const Vector4 &color = {0.2f, 0.8f, 0.2f, 1.0f},
                     int durationFrames = 180);

    /// @brief 通知を描画する（毎フレームメインUIのどこかで呼ぶ）
    static void Draw();

    /// @brief ログ履歴を取得する
    static const std::vector<Notification> &GetHistory() { return history_; }

    /// @brief ログ履歴をクリアする
    static void ClearHistory() { history_.clear(); }

  private:
    static std::vector<Notification> notifications_;
    static std::vector<Notification> history_;
};
} // namespace Hagine
