#pragma once
#include <functional>
#include <string>
#include <vector>
#ifdef USE_IMGUI
#include <imgui.h>
#endif // USE_IMGUI
#include "type/Vector4.h"

/// @brief ImGuiによる通知システム
namespace Hagine {
class ImGuiNotification
{
  public:
    struct Notification
    {
        std::string message;
        Vector4 color;
        int remainingFrames;
        int totalFrames;
        std::string time; // 投稿した時刻（HH:MM:SS）。履歴に出す
        std::string actionLabel;      // カードに出すボタン（空ならボタン無し）
        std::function<void()> action; // ボタンを押したときの処理
        bool important = false;       // 通常の通知に押し出されない（アセットが見つからない等の警告用）
    };

    /// @brief 通知を投稿する
    /// @param message メッセージ
    /// @param color テキスト色（デフォルトは緑系）
    /// @param durationFrames 表示フレーム数（デフォルト180フレーム = 約3秒）
    static void Post(const std::string &message,
                     const Vector4 &color = {0.2f, 0.8f, 0.2f, 1.0f},
                     int durationFrames = 180);

    /// @brief ボタン付きの通知を投稿する（「元に戻す」など、その場で取り消したい操作用）。
    ///        マウスを重ねている間は消えない。押すと action を呼んでカードを閉じる
    /// @param message メッセージ
    /// @param color 色
    /// @param actionLabel ボタンの文字
    /// @param action 押したときの処理
    /// @param durationFrames 表示フレーム数（ボタンを押す時間を見て既定は約5秒）
    static void PostWithAction(const std::string &message, const Vector4 &color, const std::string &actionLabel,
                               std::function<void()> action, int durationFrames = 300);

    /// @brief 見落とされると困る警告を投稿する（アセットが見つからない等）。
    ///        起動時のように「読み込みました」が大量に流れても押し出されず、ScopedMute 中も表示する
    /// @param message メッセージ
    /// @param color 色
    /// @param durationFrames 表示フレーム数（既定は約10秒）
    static void PostError(const std::string &message, const Vector4 &color = {0.95f, 0.45f, 0.35f, 1.0f},
                          int durationFrames = 600);

    /// @brief 通知を描画する（毎フレームメインUIのどこかで呼ぶ）
    static void Draw();

    /// @brief ログ履歴を取得する
    static const std::vector<Notification> &GetHistory() { return history_; }

    /// @brief ログ履歴をクリアする
    static void ClearHistory() { history_.clear(); }

    /// @brief 画面へのトースト表示を止めるスコープ（履歴には残る）
    ///
    /// シーンの作り直しやUndoの復元は、内部で「追加しました」「削除しました」を
    /// 何件も呼ぶ。人が押した操作ではないので画面に流す意味が無く、
    /// 本当に見せたい通知を押し流してしまう。その間だけ黙らせるために使う。
    /// 入れ子にしてよい（数えているので内側が抜けても外側は効いたまま）。
    class ScopedMute
    {
      public:
        ScopedMute() { ++muteDepth_; }
        ~ScopedMute() { --muteDepth_; }
        ScopedMute(const ScopedMute &) = delete;
        ScopedMute &operator=(const ScopedMute &) = delete;
    };

  private:
    /// @brief トーストを積む。上限を超えたら、重要でないものから古い順に押し出す
    static void PushToast(Notification n);

    static std::vector<Notification> notifications_;
    static std::vector<Notification> history_;
    static int muteDepth_; // 0より大きい間はトーストを出さない
};
} // namespace Hagine
