#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace Hagine {

/// <summary>
/// フォルダの中のファイルが書き換わったことを OS から知らせてもらう仕組み（ReadDirectoryChangesW）。
///
/// ホットリロードはこれまで一定間隔でフォルダを走査し、全ファイルの更新日時を調べていた。
/// ファイルごとに OS への問い合わせが何十回も走るので、その1フレームだけ 20ms 以上止まっていた。
/// これを使うと、見張りは別スレッドで OS の通知を待つだけになり、
/// メインスレッドは「変わったか」のフラグを見るだけで済む。
///
/// どのファイルが変わったかは渡さない。呼び出し側は、フラグが立ったときだけ
/// これまでどおり更新日時を見比べて、変わった物を特定する。
/// </summary>
class FileChangeWatcher
{
  public:
    // Watch は cpp の中でだけ定義するので、作る・捨てるも cpp 側で行う
    FileChangeWatcher();
    ~FileChangeWatcher();
    FileChangeWatcher(const FileChangeWatcher &) = delete;
    FileChangeWatcher &operator=(const FileChangeWatcher &) = delete;

    /// <summary>
    /// 見張りを始める。サブフォルダも含む。すでに始めていたら一度止めてからやり直す
    /// </summary>
    /// <param name="directories">見張るフォルダ（UTF-8。存在しないものは飛ばす）</param>
    /// <param name="extensions">反応する拡張子（小文字・ドット付き。例: ".hlsl"）。空なら全てのファイル</param>
    /// <returns>bool: 1つでも見張れたら true。false のときは呼び出し側で走査に切り替えること</returns>
    bool Start(const std::vector<std::string> &directories, const std::vector<std::string> &extensions);

    /// <summary>
    /// 見張りを止めてスレッドを畳む
    /// </summary>
    void Stop();

    /// <summary>
    /// 見張りが動いているか。途中で OS 側の失敗により止まったら false になる
    /// </summary>
    /// <returns>bool: 動いていれば true</returns>
    bool IsWatching() const { return watching_.load(); }

    /// <summary>
    /// 変更が届いていて、最後の通知から quietSeconds 以上経っていれば true を返し、届いた印を下ろす。
    /// エディタの保存は「一時ファイルに書いて差し替える」などで通知が数回続くので、
    /// 落ち着くのを待ってから1回だけ反応させるため
    /// </summary>
    /// <param name="quietSeconds">最後の通知から待つ秒数</param>
    /// <returns>bool: 調べ直すべきなら true</returns>
    bool ConsumeChanges(double quietSeconds);

  private:
    /// <summary>
    /// 1フォルダぶんの見張り（ハンドルと受け取り用の領域）
    /// </summary>
    struct Watch;

    /// <summary>
    /// 見張りスレッドの本体
    /// </summary>
    void ThreadMain();

    /// <summary>
    /// 通知の中身（変わったファイル名の並び）に、反応する拡張子の物が含まれているか
    /// </summary>
    /// <param name="buffer">ReadDirectoryChangesW が書いた中身</param>
    /// <param name="size">書かれたバイト数</param>
    /// <returns>bool: 含まれていれば true</returns>
    bool ContainsWatchedFile(const uint8_t *buffer, size_t size) const;

    /// <summary>
    /// 変更が届いたことを記録する
    /// </summary>
    void MarkChanged();

    std::vector<std::unique_ptr<Watch>> watches_; // 見張っているフォルダ
    std::vector<std::wstring> extensions_;        // 反応する拡張子（小文字）
    std::thread thread_;                          // 見張りスレッド
    void *stopEvent_ = nullptr;                   // 止めるときに立てるイベント（HANDLE）
    std::atomic<bool> watching_ = false;          // 見張りが動いているか
    std::atomic<bool> pending_ = false;           // まだ反応していない変更があるか
    std::atomic<int64_t> lastChangeTicks_ = 0;    // 最後に通知が届いた時刻（steady_clock の刻み）
};

} // namespace Hagine
