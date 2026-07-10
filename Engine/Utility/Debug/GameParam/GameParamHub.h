#pragma once
#include <functional>
#include <string>
#include <type/Vector2.h>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Hagine {

/// <summary>
/// ゲームパラメータ登録・仕分けハブ
/// コード側は Register() でパラメータを事前登録するだけでよく、
/// 仕分け（どのウィンドウ/タブ/セクションに表示するか）は実行中にユーザーが
/// ハブUI上で行う。仕分けレイアウトはJSONへ保存され、次回起動時に復元される。
/// ※デバッグ専用のためシングルトンを許容する（どこからでも登録できる必要がある）
/// </summary>
class GameParamHub {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>登録可能なパラメータポインタの型</summary>
    using ParamPtr = std::variant<
        float *, int *, bool *,
        Vector2 *, Vector3 *, Vector4 *,
        const float *, const int *, const bool *>;

    /// <summary>
    /// 登録時のオプション（ドラッグ速度・範囲・色表示・変更時コールバック）
    /// </summary>
    struct Options {
        float speed = 0.1f;             ///< ドラッグ速度
        float min = 0.0f;               ///< 最小値（min==maxなら制限なし）
        float max = 0.0f;               ///< 最大値
        bool isColor = false;           ///< Vector4 を ColorEdit4 で表示する
        std::function<void()> onChange; ///< 値変更時コールバック（省略可）
    };

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>GameParamHub*: シングルトンインスタンス</returns>
    static GameParamHub *GetInstance();

    /// <summary>
    /// パラメータを登録する（同じ owner+name は上書き）
    /// </summary>
    /// <param name="owner">出所ラベル（例:"Player"）。未分類リストのグループ名になる</param>
    /// <param name="name">パラメータ表示名</param>
    /// <param name="ptr">対象変数のポインタ</param>
    /// <param name="opts">表示オプション</param>
    void Register(const std::string &owner, const std::string &name, ParamPtr ptr, const Options &opts = {});

    /// <summary>
    /// 指定 owner のパラメータをすべて解除する
    /// 登録元オブジェクトの破棄前（デストラクタ等）に必ず呼びポインタ失効を防ぐ
    /// </summary>
    /// <param name="owner">解除する出所ラベル</param>
    void Unregister(const std::string &owner);

    /// <summary>
    /// ハブウィンドウとユーザー作成ウィンドウ群を描画する
    /// </summary>
    /// <param name="open">ハブウィンドウの表示フラグ（Xボタンと同期）</param>
    void DrawImGui(bool *open);

    /// <summary>
    /// 仕分けレイアウトをJSONへ保存する
    /// </summary>
    void SaveLayout();

    /// <summary>
    /// 仕分けレイアウトをJSONから読み込む（初回描画時に自動で呼ばれる）
    /// </summary>
    void LoadLayout();

  private:
    GameParamHub() = default;
    ~GameParamHub() = default;
    GameParamHub(const GameParamHub &) = delete;
    GameParamHub &operator=(const GameParamHub &) = delete;

    /// ===================================================
    /// private 型定義
    /// ===================================================

    /// <summary>登録された1パラメータ</summary>
    struct Entry {
        std::string owner; ///< 出所ラベル
        std::string name;  ///< 表示名
        ParamPtr ptr;      ///< 対象変数
        Options opts;      ///< 表示オプション
    };

    /// <summary>ユーザーが作るセクション（CollapsingHeader相当）</summary>
    struct SectionDef {
        std::string name;
    };

    /// <summary>ユーザーが作るタブ</summary>
    struct TabDef {
        std::string name;
        std::vector<SectionDef> sections;
    };

    /// <summary>ユーザーが作るウィンドウ</summary>
    struct WindowDef {
        std::string name;
        std::vector<TabDef> tabs;
    };

    /// <summary>パラメータの仕分け先（空文字は「直下」を表す）</summary>
    struct Assignment {
        std::string window;
        std::string tab;
        std::string section;
    };

    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>owner/name 形式の一意キーを作る</summary>
    static std::string MakeKey(const std::string &owner, const std::string &name);

    /// <summary>パラメータ1件のウィジェットを描画する（変更時は onChange を呼ぶ）</summary>
    void DrawParamWidget(Entry &entry);

    /// <summary>ドラッグ元（掴んで仕分けるためのハンドル）を描画する</summary>
    void DrawDragSource(const std::string &key);

    /// <summary>ドロップ先を設定する（直前のアイテムに対して）</summary>
    void HandleDropTarget(const std::string &window, const std::string &tab, const std::string &section);

    /// <summary>右クリックメニューで仕分け先を選ばせる（直前のアイテムに対して）</summary>
    void DrawMoveContextMenu(const std::string &key);

    /// <summary>ハブウィンドウ本体（レイアウト編集＋未分類リスト）を描画する</summary>
    void DrawHubWindow(bool *open);

    /// <summary>ユーザー作成ウィンドウ群を描画する</summary>
    void DrawUserWindows();

    /// <summary>指定の仕分け先に割り当てられたパラメータキー一覧を返す</summary>
    std::vector<std::string> CollectAssigned(const std::string &window, const std::string &tab, const std::string &section);

    /// <summary>仕分け先一覧を描画（レイアウトツリー）</summary>
    void DrawLayoutTree();

    /// <summary>キー一覧のパラメータを行として描画する</summary>
    void DrawParamRows(const std::vector<std::string> &keys);

    /// <summary>ウィンドウ定義を名前で探す（FindWindow は Windows API マクロと衝突するため別名）</summary>
    WindowDef *FindWindowDef(const std::string &name);

    /// ===================================================
    /// private variants
    /// ===================================================

    std::vector<Entry> entries_;                              ///< 登録済みパラメータ（登録順を保持）
    std::vector<WindowDef> windows_;                          ///< ユーザー作成ウィンドウ定義
    std::unordered_map<std::string, Assignment> assignments_; ///< key -> 仕分け先
    bool layoutLoaded_ = false;                               ///< レイアウト読み込み済みフラグ
    bool layoutDirty_ = false;                                ///< 未保存の仕分け変更があるか
};

} // namespace Hagine
