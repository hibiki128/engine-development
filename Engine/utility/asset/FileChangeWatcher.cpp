#include "FileChangeWatcher.h"
#include <Windows.h>
#include <chrono>
#include <debug/log/Logger.h>
#include <string/stringUtility.h>
#include <cwctype>

namespace Hagine {
namespace {

/// 1回の通知で受け取れる量。64KB を超える通知はネットワーク越しのフォルダで失敗するのでここで止める
constexpr DWORD kNotifyBufferBytes = 64 * 1024;

/// 反応する変更の種類。ファイルの書き換え・作成・削除・名前の変更（保存時の差し替え）
constexpr DWORD kNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;

int64_t NowTicks()
{
    return std::chrono::steady_clock::now().time_since_epoch().count();
}
} // namespace

struct FileChangeWatcher::Watch
{
    HANDLE directory = INVALID_HANDLE_VALUE; // 見張るフォルダのハンドル
    HANDLE event = nullptr;                  // 通知が届いたら立つイベント
    OVERLAPPED overlapped{};                 // 非同期で待つための受け皿
    // ReadDirectoryChangesW は DWORD 境界に揃った領域を要求するので DWORD で取る
    std::vector<DWORD> buffer = std::vector<DWORD>(kNotifyBufferBytes / sizeof(DWORD));
    bool ioPending = false; // 通知待ちを出している最中か（止めるときに完了を待つ必要がある）
};

FileChangeWatcher::FileChangeWatcher() = default;

FileChangeWatcher::~FileChangeWatcher()
{
    Stop();
}

bool FileChangeWatcher::Start(const std::vector<std::string> &directories, const std::vector<std::string> &extensions)
{
    Stop();

    extensions_.clear();
    for (const std::string &extension : extensions)
    {
        std::wstring wide;
        for (char c : extension)
        {
            wide += static_cast<wchar_t>(std::towlower(static_cast<unsigned char>(c)));
        }
        extensions_.push_back(std::move(wide));
    }

    for (const std::string &directory : directories)
    {
        const std::wstring path = StringUtility::ConvertString(directory);
        const HANDLE handle = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            // アプリ側に images が無いなど、無いフォルダは見張らなくてよい
            continue;
        }
        auto watch = std::make_unique<Watch>();
        watch->directory = handle;
        watch->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!watch->event)
        {
            CloseHandle(handle);
            continue;
        }
        watch->overlapped.hEvent = watch->event;
        watches_.push_back(std::move(watch));
    }

    // WaitForMultipleObjects は止める用のイベントと合わせて 64 個まで
    if (watches_.empty() || watches_.size() + 1 > MAXIMUM_WAIT_OBJECTS)
    {
        Stop();
        return false;
    }

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_)
    {
        Stop();
        return false;
    }

    watching_ = true;
    thread_ = std::thread(&FileChangeWatcher::ThreadMain, this);
    return true;
}

void FileChangeWatcher::Stop()
{
    if (thread_.joinable())
    {
        SetEvent(static_cast<HANDLE>(stopEvent_));
        thread_.join();
    }
    if (stopEvent_)
    {
        CloseHandle(static_cast<HANDLE>(stopEvent_));
        stopEvent_ = nullptr;
    }
    for (const std::unique_ptr<Watch> &watch : watches_)
    {
        if (watch->event)
        {
            CloseHandle(watch->event);
        }
        if (watch->directory != INVALID_HANDLE_VALUE)
        {
            CloseHandle(watch->directory);
        }
    }
    watches_.clear();
    watching_ = false;
}

bool FileChangeWatcher::ConsumeChanges(double quietSeconds)
{
    if (!pending_.load())
    {
        return false;
    }
    const auto quiet = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(quietSeconds));
    if (NowTicks() - lastChangeTicks_.load() < quiet.count())
    {
        return false;
    }
    // 下ろした直後に届いた変更は、この後の呼び出し側の走査で拾われる（通知はファイルを書いた後に来るため）
    return pending_.exchange(false);
}

void FileChangeWatcher::MarkChanged()
{
    lastChangeTicks_.store(NowTicks());
    pending_.store(true);
}

bool FileChangeWatcher::ContainsWatchedFile(const uint8_t *buffer, size_t size) const
{
    if (extensions_.empty())
    {
        return true;
    }
    size_t offset = 0;
    while (offset + sizeof(FILE_NOTIFY_INFORMATION) <= size)
    {
        const auto *info = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(buffer + offset);
        const std::wstring_view name(info->FileName, info->FileNameLength / sizeof(wchar_t));

        // 拡張子はファイル名の最後の '.' から。フォルダ名の '.' は拾わない
        const size_t slash = name.find_last_of(L"\\/");
        const size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring_view::npos && (slash == std::wstring_view::npos || dot > slash))
        {
            std::wstring extension(name.substr(dot));
            for (wchar_t &c : extension)
            {
                c = static_cast<wchar_t>(std::towlower(c));
            }
            for (const std::wstring &watched : extensions_)
            {
                if (extension == watched)
                {
                    return true;
                }
            }
        }

        if (info->NextEntryOffset == 0)
        {
            break;
        }
        offset += info->NextEntryOffset;
    }
    return false;
}

void FileChangeWatcher::ThreadMain()
{
    // 次の通知待ちを出す。失敗したら見張りを続けられない
    auto issue = [](Watch &watch) -> bool {
        ResetEvent(watch.event);
        watch.ioPending = ReadDirectoryChangesW(watch.directory, watch.buffer.data(), kNotifyBufferBytes, TRUE, kNotifyFilter,
                                                nullptr, &watch.overlapped, nullptr) != FALSE;
        return watch.ioPending;
    };

    std::vector<HANDLE> waitHandles;
    waitHandles.push_back(static_cast<HANDLE>(stopEvent_));
    bool failed = false;
    for (const std::unique_ptr<Watch> &watch : watches_)
    {
        if (!issue(*watch))
        {
            failed = true;
            break;
        }
        waitHandles.push_back(watch->event);
    }

    while (!failed)
    {
        const DWORD result = WaitForMultipleObjects(static_cast<DWORD>(waitHandles.size()), waitHandles.data(), FALSE, INFINITE);
        if (result == WAIT_OBJECT_0)
        {
            break; // Stop が呼ばれた
        }
        const DWORD index = result - WAIT_OBJECT_0 - 1;
        if (index >= watches_.size())
        {
            failed = true;
            break;
        }

        Watch &watch = *watches_[index];
        watch.ioPending = false;
        DWORD bytes = 0;
        if (!GetOverlappedResult(watch.directory, &watch.overlapped, &bytes, FALSE))
        {
            if (GetLastError() != ERROR_NOTIFY_ENUM_DIR)
            {
                failed = true;
                break;
            }
            // 変更が多すぎて中身を受け取れなかった。何が変わったか分からないので調べ直させる
            MarkChanged();
        }
        else if (bytes == 0)
        {
            // 同上（受け取り領域があふれた）
            MarkChanged();
        }
        else if (ContainsWatchedFile(reinterpret_cast<const uint8_t *>(watch.buffer.data()), bytes))
        {
            MarkChanged();
        }

        if (!issue(watch))
        {
            failed = true;
        }
    }

    // 出したままの通知待ちを取り消し、OS が受け皿を触り終わるまで待つ（この後 Stop が領域を捨てるため）
    for (const std::unique_ptr<Watch> &watch : watches_)
    {
        if (watch->ioPending)
        {
            CancelIoEx(watch->directory, &watch->overlapped);
            DWORD bytes = 0;
            GetOverlappedResult(watch->directory, &watch->overlapped, &bytes, TRUE);
            watch->ioPending = false;
        }
    }

    if (failed)
    {
        Logger::Warn("フォルダの見張りが止まりました。一定間隔の走査に切り替えます");
    }
    watching_ = false;
}

} // namespace Hagine
