#include "UndoRedoManager.h"

namespace Hagine {

void UndoRedoManager::Push(std::unique_ptr<IUndoCommand> command)
{
    // Undo/Redo適用中の変更を再登録しない（履歴の二重化防止）
    if (isApplying_ || !command)
    {
        return;
    }

    undoStack_.push_back(std::move(command));

    // 新しい操作が入ったらRedo履歴は無効になる
    redoStack_.clear();

    // 履歴上限を超えたら古いものから破棄
    while (undoStack_.size() > kMaxHistory)
    {
        undoStack_.pop_front();
    }
}

bool UndoRedoManager::Undo()
{
    if (undoStack_.empty())
    {
        return false;
    }

    std::unique_ptr<IUndoCommand> command = std::move(undoStack_.back());
    undoStack_.pop_back();

    isApplying_ = true;
    command->Undo();
    isApplying_ = false;

    redoStack_.push_back(std::move(command));
    return true;
}

bool UndoRedoManager::Redo()
{
    if (redoStack_.empty())
    {
        return false;
    }

    std::unique_ptr<IUndoCommand> command = std::move(redoStack_.back());
    redoStack_.pop_back();

    isApplying_ = true;
    command->Redo();
    isApplying_ = false;

    undoStack_.push_back(std::move(command));
    return true;
}

std::string UndoRedoManager::GetUndoLabel() const
{
    return undoStack_.empty() ? std::string() : undoStack_.back()->GetLabel();
}

std::string UndoRedoManager::GetRedoLabel() const
{
    return redoStack_.empty() ? std::string() : redoStack_.back()->GetLabel();
}

void UndoRedoManager::Clear()
{
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace Hagine
