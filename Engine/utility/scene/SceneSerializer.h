#pragma once
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// シーンファイル（jsons/Scenes/シーン名.json）の保存・読み込み。
///
/// 1シーン＝1ファイルで、中身は次のとおり。
///   - objects     : エディタで置いたオブジェクト（コライダー・メタボールも含む。親が子より先に並ぶ）
///   - lights      : 平行光源・点光源・スポットライト（毎フレーム積み直す動的ライトは含めない）
///   - attachments : 光源・パーティクルをオブジェクトに付けた親子付け
///
/// 保存されるのは BaseObjectManager が所有するオブジェクトのうち「シーンに保存する」が付いた物だけ。
/// ゲーム側がコードで作るオブジェクト（RegisterExternal した物）は毎回コードが作り直すので書かない。
///
/// シーンが始まるとき（SceneManager がシーンの Initialize を呼ぶ直前）に、
/// そのシーン名のファイルがあれば自動で読み込む。Release でも同じように読み込まれる。
/// </summary>
class SceneSerializer
{
  public:
    /// <summary>ファイル形式のバージョン。キーの意味を変えたら上げる</summary>
    static constexpr int kVersion = 1;

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    static SceneSerializer *GetInstance();

    /// ===================================================
    /// ファイル
    /// ===================================================

    /// <summary>
    /// 今の配置をシーンファイルへ書き出す。
    /// 一時ファイルへ書いてから置き換えるので、途中で失敗しても元のファイルは壊れない。
    /// 置き換える前の内容は「シーン名.json.bak」に1世代だけ残す
    /// </summary>
    /// <param name="sceneName">シーン名（ファイル名に使えない文字は _ に置き換える）</param>
    /// <returns>bool: 書き出せたら true</returns>
    bool Save(const std::string &sceneName);

    /// <summary>
    /// シーンファイルを読み込み、今の所有オブジェクトを置き換える（ゲーム側のオブジェクトには触れない）
    /// </summary>
    /// <param name="sceneName">シーン名</param>
    /// <returns>bool: 読み込めたら true（ファイルが無い・壊れている場合は何も変えずに false）</returns>
    bool Load(const std::string &sceneName);

    /// <summary>
    /// ファイルがあるときだけ読み込む（シーン開始時の自動読み込み用。無ければ何もしない）
    /// </summary>
    /// <param name="sceneName">シーン名</param>
    /// <returns>bool: 読み込んだら true</returns>
    bool LoadIfExists(const std::string &sceneName);

    /// <summary>シーンファイルがあるか</summary>
    bool Exists(const std::string &sceneName) const;

    /// <summary>保存済みのシーン名の一覧（名前順）</summary>
    std::vector<std::string> ListScenes() const;

    /// <summary>シーンファイルの実パス（jsons/Scenes/シーン名.json）</summary>
    static std::string FilePath(const std::string &sceneName);

    /// <summary>今動いているシーンの名前（SceneManager のもの）</summary>
    static std::string CurrentSceneName();

    /// ===================================================
    /// エディタ
    /// ===================================================

    /// <summary>
    /// 今のシーンへ上書き保存する（Ctrl+S）。
    /// 保存ダイアログで選んだ「一緒に保存する物」（スプライト・UIアニメーション）も書く
    /// </summary>
    void SaveCurrentScene();

#ifdef USE_IMGUI
    /// <summary>保存ダイアログを開く（名前を付けて保存・保存する物の確認）</summary>
    void OpenSaveDialog();

    /// <summary>読み込みダイアログを開く</summary>
    void OpenLoadDialog();

    /// <summary>ダイアログを描く。毎フレーム、ウィンドウの外から呼ぶ</summary>
    void DrawImGui();
#endif // USE_IMGUI

  private:
    SceneSerializer() = default;
    ~SceneSerializer() = default;
    SceneSerializer(const SceneSerializer &) = delete;
    SceneSerializer &operator=(const SceneSerializer &) = delete;

    /// <summary>
    /// 読み込みの本体
    /// </summary>
    /// <param name="sceneName">シーン名</param>
    /// <param name="notify">結果をトーストで知らせるか（自動読み込みでは出さない）</param>
    bool LoadInternal(const std::string &sceneName, bool notify);

#ifdef USE_IMGUI
    /// <summary>保存ダイアログの中身</summary>
    void DrawSaveDialog();

    /// <summary>読み込みダイアログの中身</summary>
    void DrawLoadDialog();

    /// <summary>保存ダイアログの「保存するオブジェクト」一覧（所有オブジェクトを階層どおりに並べる）</summary>
    void DrawSaveTargetList();

    /// <summary>読み込みダイアログに出すファイル一覧を作り直す</summary>
    void RefreshLoadList();

    /// <summary>読み込みダイアログの1行ぶん</summary>
    struct SceneFileInfo
    {
        std::string sceneName;
        int objectCount = -1;  // 読めなければ -1
        std::string modified; // 更新日時（表示用）
    };

    bool requestOpenSave_ = false;
    bool requestOpenLoad_ = false;
    std::string saveNameBuffer_;
    std::vector<SceneFileInfo> loadList_;
    int loadSelected_ = -1;
    bool loadSprites_ = true;
    bool loadUIAnimation_ = true;
#endif // USE_IMGUI

    // シーンと一緒に保存する物（どちらも全シーン共通の保存先なので、外せるようにしてある）
    bool saveSprites_ = true;
    bool saveUIAnimation_ = true;
};
} // namespace Hagine
