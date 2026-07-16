#pragma once
#include <deque>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace Hagine {

/// <summary>
/// Undo/Redo可能な操作1件を表すインターフェース
/// </summary>
class IUndoCommand
{
  public:
    virtual ~IUndoCommand() = default;

    /// <summary>
    /// 操作を取り消す
    /// </summary>
    virtual void Undo() = 0;

    /// <summary>
    /// 取り消した操作をやり直す
    /// </summary>
    virtual void Redo() = 0;

    /// <summary>
    /// 履歴表示用のラベルを取得する
    /// </summary>
    /// <returns>const std::string&: 操作名</returns>
    virtual const std::string &GetLabel() const = 0;
};

/// <summary>
/// 任意の処理をUndo/Redoとして登録できる汎用コマンド
/// アプリケーション側からも自由に使える最小の構成要素
/// </summary>
class LambdaCommand : public IUndoCommand
{
  public:
    /// <summary>
    /// コンストラクタ
    /// </summary>
    /// <param name="label">操作名</param>
    /// <param name="undo">取り消し処理</param>
    /// <param name="redo">やり直し処理</param>
    LambdaCommand(std::string label, std::function<void()> undo, std::function<void()> redo)
        : label_(std::move(label)), undo_(std::move(undo)), redo_(std::move(redo)) {}

    void Undo() override
    {
        if (undo_)
        {
            undo_();
        }
    }
    void Redo() override
    {
        if (redo_)
        {
            redo_();
        }
    }
    const std::string &GetLabel() const override { return label_; }

  private:
    std::string label_;           // 操作名
    std::function<void()> undo_;  // 取り消し処理
    std::function<void()> redo_;  // やり直し処理
};

/// <summary>
/// 対象の状態JSON（編集前／編集後）を適用関数へ渡すことでUndo/Redoする汎用コマンド
/// 対象を丸ごとJSON化できるものなら何にでも使える
/// </summary>
class JsonStateCommand : public IUndoCommand
{
  public:
    /// <summary>
    /// コンストラクタ
    /// </summary>
    /// <param name="label">操作名</param>
    /// <param name="before">編集前の状態JSON</param>
    /// <param name="after">編集後の状態JSON</param>
    /// <param name="apply">状態JSONを対象へ適用する関数</param>
    JsonStateCommand(std::string label, nlohmann::json before, nlohmann::json after,
                     std::function<void(const nlohmann::json &)> apply)
        : label_(std::move(label)), before_(std::move(before)), after_(std::move(after)), apply_(std::move(apply)) {}

    void Undo() override
    {
        if (apply_)
        {
            apply_(before_);
        }
    }
    void Redo() override
    {
        if (apply_)
        {
            apply_(after_);
        }
    }
    const std::string &GetLabel() const override { return label_; }

  private:
    std::string label_;                                 // 操作名
    nlohmann::json before_;                             // 編集前の状態
    nlohmann::json after_;                              // 編集後の状態
    std::function<void(const nlohmann::json &)> apply_; // 状態の適用関数
};

/// <summary>
/// エディタ全体のUndo/Redo履歴を管理するシングルトン
/// 実行済みの操作を Push で積み、Undo/Redo で巻き戻し・やり直しを行う
/// </summary>
class UndoRedoManager
{
  public:
    // 履歴の最大保持数（超えた分は古いものから破棄）
    static constexpr size_t kMaxHistory = 64;

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns>UndoRedoManager*: インスタンス</returns>
    static UndoRedoManager *GetInstance()
    {
        static UndoRedoManager instance;
        return &instance;
    }

    /// <summary>
    /// 実行済みの操作を履歴に積む（Redo履歴はクリアされる）
    /// </summary>
    /// <param name="command">積むコマンド</param>
    void Push(std::unique_ptr<IUndoCommand> command);

    /// <summary>
    /// 直前の操作を取り消す
    /// </summary>
    /// <returns>bool: 取り消しを実行したら true</returns>
    bool Undo();

    /// <summary>
    /// 取り消した操作をやり直す
    /// </summary>
    /// <returns>bool: やり直しを実行したら true</returns>
    bool Redo();

    bool CanUndo() const { return !undoStack_.empty(); }              // Undo可能か
    bool CanRedo() const { return !redoStack_.empty(); }              // Redo可能か
    std::string GetUndoLabel() const;                                 // 次にUndoされる操作名
    std::string GetRedoLabel() const;                                 // 次にRedoされる操作名
    bool IsApplying() const { return isApplying_; }                   // Undo/Redo適用中か
    size_t GetUndoCount() const { return undoStack_.size(); }         // Undo履歴数
    size_t GetRedoCount() const { return redoStack_.size(); }         // Redo履歴数

    /// <summary>
    /// 全履歴を破棄する（シーン切替時など）
    /// </summary>
    void Clear();

  private:
    UndoRedoManager() = default;
    ~UndoRedoManager() = default;
    UndoRedoManager(UndoRedoManager &) = delete;
    UndoRedoManager &operator=(UndoRedoManager &) = delete;

    std::deque<std::unique_ptr<IUndoCommand>> undoStack_; // Undo履歴（末尾が最新）
    std::deque<std::unique_ptr<IUndoCommand>> redoStack_; // Redo履歴（末尾が次のRedo）
    bool isApplying_ = false;                             // Undo/Redo適用中の再Pushを防ぐフラグ
};

/// <summary>
/// トップレベルが「名前 → 状態」のマップ形式であるJSON同士の差分を作るヘルパー
/// 変更されたキーだけを含む before/after を返す（片方にしか無いキーは null で表現）
/// </summary>
/// <param name="before">編集前の状態</param>
/// <param name="after">編集後の状態</param>
/// <returns>std::pair: {差分のみのbefore, 差分のみのafter}</returns>
inline std::pair<nlohmann::json, nlohmann::json> MakeTopLevelJsonDiff(const nlohmann::json &before, const nlohmann::json &after)
{
    // マップ形式でなければ丸ごと差し替え扱い
    if (!before.is_object() || !after.is_object())
    {
        return {before, after};
    }

    nlohmann::json diffBefore = nlohmann::json::object();
    nlohmann::json diffAfter = nlohmann::json::object();

    // before側を走査：変更・削除されたキー
    for (auto it = before.begin(); it != before.end(); ++it)
    {
        auto found = after.find(it.key());
        if (found == after.end())
        {
            diffBefore[it.key()] = it.value();
            diffAfter[it.key()] = nullptr; // afterには存在しない＝削除された
        }
        else if (*found != it.value())
        {
            diffBefore[it.key()] = it.value();
            diffAfter[it.key()] = *found;
        }
    }
    // after側だけに存在するキー：追加された
    for (auto it = after.begin(); it != after.end(); ++it)
    {
        if (before.find(it.key()) == before.end())
        {
            diffBefore[it.key()] = nullptr; // beforeには存在しない＝追加された
            diffAfter[it.key()] = it.value();
        }
    }
    return {diffBefore, diffAfter};
}

} // namespace Hagine
