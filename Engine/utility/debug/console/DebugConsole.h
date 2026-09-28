#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <type/Vector4.h>

namespace Hagine {

/// <summary>
/// 実行中にコマンドを打ち込んで、値の確認や書き換え・シーン切り替えなどを行うコンソール。
///
/// ゲーム側からも RegisterCommand / RegisterFloat で自由に増やせる。
/// スライダーを1つ増やすほどでもない調整や、「今の状態を一行で見たい」ときに使う。
/// </summary>
class DebugConsole
{
  public:
    /// コマンドの中身。args[0] はコマンド名そのもの
    using CommandFunc = std::function<void(const std::vector<std::string> &args)>;

  private:
    DebugConsole() = default;
    ~DebugConsole() = default;
    DebugConsole(const DebugConsole &) = delete;
    DebugConsole &operator=(const DebugConsole &) = delete;

  public:
    /// ====================================
    /// public method
    /// ====================================

    /// <summary>シングルトンインスタンスの取得</summary>
    static DebugConsole *GetInstance();

    /// <summary>組み込みコマンドを登録する</summary>
    void Initialize();

    /// <summary>登録内容を捨てる</summary>
    void Finalize();

    /// <summary>
    /// コマンドを登録する
    /// </summary>
    /// <param name="name">コマンド名（空白を含めないこと）</param>
    /// <param name="help">help で表示される説明</param>
    /// <param name="func">実行される中身</param>
    void RegisterCommand(const std::string &name, const std::string &help, CommandFunc func);

    /// <summary>
    /// float 変数を set / get で触れるように登録する
    /// </summary>
    /// <param name="name">変数名</param>
    /// <param name="target">対象の変数。寿命は登録側が保証すること</param>
    /// <param name="help">説明</param>
    void RegisterFloat(const std::string &name, float *target, const std::string &help = "");

    /// <summary>int 変数を set / get で触れるように登録する</summary>
    void RegisterInt(const std::string &name, int *target, const std::string &help = "");

    /// <summary>bool 変数を set / get で触れるように登録する</summary>
    void RegisterBool(const std::string &name, bool *target, const std::string &help = "");

    /// <summary>
    /// 登録した変数を解除する。対象がいなくなる前に必ず呼ぶこと
    /// </summary>
    /// <param name="name">変数名</param>
    void UnregisterVariable(const std::string &name);

    /// <summary>
    /// 1行を実行する
    /// </summary>
    /// <param name="line">"scene Title" のような入力</param>
    void Execute(const std::string &line);

    /// <summary>
    /// コンソールへ1行出力する
    /// </summary>
    /// <param name="text">出す文字</param>
    /// <param name="color">文字色</param>
    void Print(const std::string &text, const Vector4 &color = {0.85f, 0.87f, 0.90f, 1.0f});

    /// <summary>
    /// ウィンドウを描く
    /// </summary>
    /// <param name="open">表示フラグ。閉じるボタンで false になる</param>
    void DrawImGui(bool *open);

  private:
    /// ====================================
    /// private structures
    /// ====================================

    /// 登録されたコマンド
    struct Command
    {
        std::string help;
        CommandFunc func;
    };

    /// set / get で触れる変数
    struct Variable
    {
        enum class Type
        {
            Float,
            Int,
            Bool,
        };
        Type type = Type::Float;
        void *target = nullptr;
        std::string help;
    };

    /// 表示履歴の1行
    struct Line
    {
        std::string text;
        Vector4 color;
    };

    /// ====================================
    /// private method
    /// ====================================

    /// 入力行を空白で区切る
    static std::vector<std::string> Tokenize(const std::string &line);

    /// 変数の現在値を文字列にする
    std::string FormatVariable(const Variable &variable) const;

    /// 補完候補を集める
    std::vector<std::string> CollectCandidates(const std::string &prefix) const;

    /// 組み込みコマンドの登録本体
    void RegisterBuiltInCommands();

  private:
    /// ====================================
    /// private variables
    /// ====================================

    std::map<std::string, Command> commands_;
    std::map<std::string, Variable> variables_;
    std::vector<Line> lines_;
    std::vector<std::string> history_;  // 過去の入力（上下キーで呼び出す）
    int historyCursor_ = -1;
    std::string inputBuffer_;
    bool scrollToBottom_ = false;
    bool initialized_ = false;
};

} // namespace Hagine
