#pragma once
#ifdef USE_IMGUI
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// コマンドパレットに並べる1項目
/// </summary>
struct EditorCommand
{
    /// <summary>項目の種類。一覧の色分けと、先頭記号での絞り込みに使う</summary>
    enum class Kind
    {
        Action,    // 操作（保存・Undo・再生など）          先頭記号 >
        Window,    // ウィンドウの表示切り替え              先頭記号 #
        Workspace, // ワークスペース（窓の組み合わせ）の切り替え
        Object,    // オブジェクトを選んでカメラを寄せる    先頭記号 @
        Scene,     // シーンの切り替え                      先頭記号 !
        Setting,   // 外観などの設定
        Asset,     // アセット（モデルを置く・音を試聴する・画像のパスをコピー） 先頭記号 $
    };

    Kind kind = Kind::Action;
    std::string icon;     // 先頭のアイコン（Font Awesome の文字列）
    std::string label;    // 表示名（検索対象・一致した文字を強調する）
    std::string hint;     // 補足（検索対象。表示は薄く）。別名やキーワードを入れておくと引っかかりやすい
    std::string shortcut; // 右端に出すショートカット表記
    bool checked = false; // 右端にチェックを出す（表示中のウィンドウなど）
    std::function<void()> action;
};

/// <summary>
/// コマンドパレット（Ctrl+K）。
/// ウィンドウ・操作・オブジェクト・シーンを1つの検索窓からあいまい検索して実行する。
/// 窓が20を超えてメニューから探すのが手間になったので、名前の一部を打てば届くようにする。
///
/// 項目は開いている間だけ呼び出し側が毎フレーム組み立てて渡す（閉じている間は何も作らない）。
/// </summary>
class EditorCommandPalette
{
  public:
    /// <summary>開く（次の Draw で入力欄にフォーカスが入る）</summary>
    void Open();

    /// <summary>開閉を切り替える</summary>
    void Toggle();

    /// <summary>項目を渡す必要があるか（開いている／開こうとしている）</summary>
    bool WantsCommands() const { return isOpen_ || openRequested_; }

    /// <summary>
    /// 描画と入力処理。WantsCommands() が true のフレームだけ呼べばよい
    /// </summary>
    /// <param name="commands">並べる項目</param>
    void Draw(const std::vector<EditorCommand> &commands);

  private:
    /// <summary>一致の結果（一覧の1行）</summary>
    struct Match
    {
        int index = -1;           // commands の添字
        int score = 0;            // 大きいほど上に並ぶ
        std::vector<int> matched; // label の中で一致した文字（コードポイント単位の位置）
    };

    /// <summary>
    /// あいまい一致。query の文字が target に順番どおり現れれば一致とみなし、点数を付ける
    /// </summary>
    /// <param name="query">検索語（小文字化済みのコードポイント列）</param>
    /// <param name="target">対象文字列</param>
    /// <param name="outMatched">一致した位置（不要なら nullptr）</param>
    /// <returns>int: 点数。一致しなければ -1</returns>
    static int FuzzyScore(const std::vector<uint32_t> &query, const std::string &target, std::vector<int> *outMatched);

    /// <summary>UTF-8 をコードポイント列へ（ASCII は小文字へそろえる）</summary>
    static std::vector<uint32_t> ToLowerCodepoints(const std::string &text);

    /// <summary>項目を一意に表す鍵（最近使った項目の記録用）</summary>
    static std::string MakeKey(const EditorCommand &command);

    /// <summary>絞り込みと並べ替え</summary>
    void Filter(const std::vector<EditorCommand> &commands);

    /// <summary>1行を描く</summary>
    /// <returns>bool: クリックされたら true</returns>
    bool DrawRow(const EditorCommand &command, const Match &match, bool selected);

    /// <summary>項目を実行して閉じる</summary>
    void Execute(const EditorCommand &command);

    bool isOpen_ = false;
    bool openRequested_ = false;
    bool closeRequested_ = false;
    bool focusInput_ = false;
    bool scrollToSelection_ = false;
    char query_[256] = {};
    int selected_ = 0;
    std::vector<Match> matches_;
    std::deque<std::string> recent_; // 最近実行した項目（新しい順）

    static constexpr size_t kMaxRecent = 8;
    static constexpr int kVisibleRows = 12;
};

} // namespace Hagine
#endif // USE_IMGUI
