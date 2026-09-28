#include "UndoRedoManager.h"
#include <ctime>

namespace Hagine {

namespace {
/// <summary>今の時刻を HH:MM:SS で返す（履歴窓に出す）</summary>
std::string CurrentTimeText()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
    return buffer;
}
} // namespace

void UndoRedoManager::Push(std::unique_ptr<IUndoCommand> command)
{
    // Undo/Redo適用中の変更を再登録しない（履歴の二重化防止）
    if (isApplying_ || !command)
    {
        return;
    }

    undoStack_.push_back({std::move(command), CurrentTimeText()});

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

    Entry entry = std::move(undoStack_.back());
    undoStack_.pop_back();

    isApplying_ = true;
    entry.command->Undo();
    isApplying_ = false;

    redoStack_.push_back(std::move(entry));
    return true;
}

bool UndoRedoManager::Redo()
{
    if (redoStack_.empty())
    {
        return false;
    }

    Entry entry = std::move(redoStack_.back());
    redoStack_.pop_back();

    isApplying_ = true;
    entry.command->Redo();
    isApplying_ = false;

    undoStack_.push_back(std::move(entry));
    return true;
}

std::string UndoRedoManager::GetUndoLabel() const
{
    return undoStack_.empty() ? std::string() : undoStack_.back().command->GetLabel();
}

std::string UndoRedoManager::GetRedoLabel() const
{
    return redoStack_.empty() ? std::string() : redoStack_.back().command->GetLabel();
}

UndoRedoManager::HistoryItem UndoRedoManager::GetUndoItem(size_t index) const
{
    if (index >= undoStack_.size())
    {
        return {};
    }
    const Entry &entry = undoStack_[index];
    return {entry.command->GetLabel(), entry.time};
}

UndoRedoManager::HistoryItem UndoRedoManager::GetRedoItem(size_t index) const
{
    if (index >= redoStack_.size())
    {
        return {};
    }
    // redoStack_ は末尾が次のRedoなので、後ろから数える
    const Entry &entry = redoStack_[redoStack_.size() - 1 - index];
    return {entry.command->GetLabel(), entry.time};
}

int UndoRedoManager::JumpTo(size_t targetUndoCount)
{
    int steps = 0;
    while (undoStack_.size() > targetUndoCount && Undo())
    {
        --steps;
    }
    while (undoStack_.size() < targetUndoCount && Redo())
    {
        ++steps;
    }
    return steps;
}

void UndoRedoManager::Clear()
{
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace Hagine
