#pragma once
#ifdef USE_IMGUI
#include <audio/AudioTypes.h>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// アセットブラウザ（画像・モデル・プレハブ・サウンド・JSON を1つの窓で探して使う）。
///
/// 左に種類ごとのフォルダツリーと「お気に入り」「最近使った」、右にサムネイルの一覧。
/// 一覧の項目はドラッグで各所へ渡せる（画像→テクスチャ欄、モデル・プレハブ→シーン）。
/// ダブルクリックは種類ごとの「いちばんよく使う操作」（モデル・プレハブは配置、サウンドは試聴）。
/// お気に入りと最近使ったもの・サムネイルの大きさは jsons/ImGuiSetting/AssetBrowser.json に残る。
/// </summary>
class EditorAssetBrowser
{
  public:
    /// <summary>アセットの種類（左のツリーの並び順）</summary>
    enum class Kind
    {
        Image,
        Model,
        Prefab,
        Sound,
        Json,
        Count,
    };

    /// <summary>一覧の1項目</summary>
    struct Entry
    {
        Kind kind = Kind::Image;
        std::string name;     // ファイル名（プレハブは拡張子なし）
        std::string relPath;  // 種類のルートからの相対パス（'/' 区切り。各所へ渡すのはこれ）
        std::string fullPath; // 作業ディレクトリからのパス
        std::string folder;   // relPath の親フォルダ（ルート直下は空）
        uintmax_t fileSize = 0;
    };

    /// <summary>設定を読み、アセットを走査する</summary>
    void Initialize();

    /// <summary>窓を描く</summary>
    /// <param name="open">表示フラグ（×ボタンと同期）</param>
    void Draw(bool *open);

    /// <summary>次に描くときに走査し直す（プレハブを保存した直後など）</summary>
    void RequestRescan() { rescanRequested_ = true; }

    /// <summary>見つけたアセットの一覧（コマンドパレットから引く）</summary>
    const std::vector<Entry> &GetEntries() const { return entries_; }

    /// <summary>ダブルクリックと同じ操作をする（モデル・プレハブは配置、サウンドは試聴、他はパスをコピー）</summary>
    void ActivateEntry(const Entry &entry) { Activate(entry); }

    /// <summary>
    /// 画像ファイルの書き換えを見張り、変わったものをその場で読み直す（ホットリロード）。
    /// 窓が閉じていても効くよう毎フレーム呼ぶ（実際に調べるのは1秒に1回）
    /// </summary>
    void PollFileChanges();

  private:
    /// <summary>今どこを見ているか</summary>
    enum class Location
    {
        Folder,    // 種類のフォルダ
        Favorites, // お気に入り
        Recent,    // 最近使った
    };

    void Scan();
    void LoadSettings();
    void SaveSettings() const;

    void DrawToolbar();
    void DrawSidebar();
    void DrawFolderNode(Kind kind, const std::string &folder);
    void DrawContent();
    void DrawGrid(const std::vector<std::string> &folders, const std::vector<const Entry *> &items);
    void DrawList(const std::vector<std::string> &folders, const std::vector<const Entry *> &items);
    void DrawItemTooltip(const Entry &entry);
    void DrawItemContextMenu(const Entry &entry);
    void DrawDeletePrefabModal();

    /// <summary>項目をドラッグ元にする（種類に応じたペイロード）</summary>
    void MakeDragSource(const Entry &entry, uint64_t textureId);
    /// <summary>ダブルクリック時の操作</summary>
    void Activate(const Entry &entry);
    /// <summary>「最近使った」に積む</summary>
    void Touch(const Entry &entry);
    void ToggleFavorite(const Entry &entry);
    bool IsFavorite(const Entry &entry) const;

    /// <summary>画像のサムネイル（読み込めない・表示できない画像は 0）</summary>
    uint64_t GetThumbnail(const Entry &entry);

    static std::string KeyOf(const Entry &entry);
    const Entry *FindByKey(const std::string &key) const;
    bool PassSearch(const Entry &entry) const;

    std::vector<Entry> entries_;
    // 種類ごとのフォルダ → 子フォルダ（フルパス、名前順）
    std::map<std::string, std::vector<std::string>> folderChildren_[static_cast<int>(Kind::Count)];
    int countByKind_[static_cast<int>(Kind::Count)] = {};

    Location location_ = Location::Folder;
    Kind currentKind_ = Kind::Image;
    std::string currentFolder_;
    std::string search_;
    std::string selectedKey_;
    float thumbSize_ = 72.0f;
    bool listView_ = false;
    float sidebarWidth_ = 190.0f;

    std::vector<std::string> favorites_;
    std::deque<std::string> recent_;
    static constexpr size_t kMaxRecent = 24;

    // ホットリロード（画像の更新日時を覚えておき、変わったら読み直す）
    bool autoReloadTextures_ = true;
    std::map<std::string, int64_t> imageWriteTimes_; // 画像の相対パス → 最後に見た更新日時
    double lastPollTime_ = 0.0;

    std::string pendingDeletePrefab_;
    bool openDeleteModal_ = false;
    SoundHandle previewSound_{};
    std::string previewSoundKey_;
    bool scanned_ = false;
    bool rescanRequested_ = false;
};

} // namespace Hagine
#endif // USE_IMGUI
