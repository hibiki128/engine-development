#pragma once
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <type/Vector2.h>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Hagine {

class DataHandler;

/// <summary>
/// ゲームパラメータ登録・仕分けハブ
/// コード側は Register() でパラメータを事前登録するだけでよく、
/// 仕分け（どのウィンドウ/タブ/セクションに表示するか）は実行中にユーザーが
/// ハブUI上で行う。仕分けレイアウトはJSONへ保存され、次回起動時に復元される。
/// ※デバッグ専用のためシングルトンを許容する（どこからでも登録できる必要がある）
/// </summary>
class GameParamHub
{
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
    struct Options
    {
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
    ~GameParamHub();
    GameParamHub(const GameParamHub &) = delete;
    GameParamHub &operator=(const GameParamHub &) = delete;

    /// ===================================================
    /// private 型定義
    /// ===================================================

    /// <summary>パラメータの「値」を保持する型（リセット用のコード既定値スナップショット）</summary>
    using ParamValue = std::variant<std::monostate, float, int, bool, Vector2, Vector3, Vector4>;

    /// <summary>登録された1パラメータ</summary>
    struct Entry
    {
        std::string owner;         ///< 出所ラベル
        std::string name;          ///< 表示名
        ParamPtr ptr;              ///< 対象変数
        Options opts;              ///< 表示オプション
        ParamValue original;       ///< 初回登録時のコード既定値（元の値にリセットする用）
        bool hasOriginal = false;  ///< original を捕捉済みか
    };

    /// <summary>ユーザーが作るセクション（CollapsingHeader相当）</summary>
    struct SectionDef
    {
        std::string name;
    };

    /// <summary>ユーザーが作るタブ</summary>
    struct TabDef
    {
        std::string name;
        std::vector<SectionDef> sections;
    };

    /// <summary>ユーザーが作るウィンドウ</summary>
    struct WindowDef
    {
        std::string name;
        std::vector<TabDef> tabs;
    };

    /// <summary>パラメータの仕分け先（空文字は「直下」を表す）</summary>
    struct Assignment
    {
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
    /// private method（値の永続化・複数選択移動・ドロップゾーン）
    /// ===================================================

    /// <summary>値の保存先 DataHandler を必要になったら生成する</summary>
    void EnsureValueStore();

    /// <summary>1エントリの現在値をJSONキャッシュへ書く（Flushは呼び出し側）</summary>
    void SaveEntryValue(const Entry &e);

    /// <summary>指定ラベル(owner)の全パラメータの「現在値」をJSONへ保存する</summary>
    void SaveOwnerValues(const std::string &owner);

    /// <summary>
    /// 指定の仕分け先（window / tab / section）に置かれた全パラメータの現在値を保存する。
    /// tab や section が空なら、その1つ上の階層すべてをまとめて保存する
    /// （例: section=""→そのタブ配下すべて、tab=""→そのウィンドウ配下すべて）。
    /// </summary>
    /// <returns>保存した項目数</returns>
    int SaveAssignedValues(const std::string &window, const std::string &tab, const std::string &section);

    /// <summary>指定ラベル(owner)の保存済みの値を現在のパラメータへ読み戻す</summary>
    void LoadOwnerValues(const std::string &owner);

    /// <summary>1エントリに保存済みの値があれば適用する（登録時に呼ぶ＝起動時復元）</summary>
    void ApplySavedValue(Entry &e);

    /// <summary>登録時に現在値（コード既定値）を original として捕捉する</summary>
    void CaptureOriginal(Entry &e);

    /// <summary>1エントリを元の値（コード既定値）へ戻し、保存済みの値も消す</summary>
    void ResetEntryToOriginal(Entry &e);

    /// <summary>指定ラベルの全項目を元の値へ戻す</summary>
    void ResetOwnerToOriginal(const std::string &owner);

    /// <summary>指定の仕分け先（window/tab/section）配下の全項目を元の値へ戻す。戻した数を返す</summary>
    int ResetAssignedToOriginal(const std::string &window, const std::string &tab, const std::string &section);

    /// <summary>BeginMultiSelect/EndMultiSelect のリクエストを selectedKeys_ へ反映する（引数は ImGuiMultiSelectIO*）</summary>
    void ApplyMultiSelectRequests(void *multiSelectIo, const std::vector<std::string> &keys);

    /// <summary>確認ダイアログを要求する（次フレームでモーダルを開き、OKで action を実行）</summary>
    void RequestConfirm(const std::string &message, std::function<void()> action);

    /// <summary>確認モーダルを描画する（DrawImGuiの最後で1回呼ぶ）</summary>
    void DrawConfirmPopup();

    /// <summary>
    /// ドロップ/メニュー操作の移動を適用する。droppedKey が複数選択に含まれていれば
    /// 選択全体を、含まれなければその1件だけを移動する。target.window が空なら未分類へ戻す。
    /// </summary>
    void MoveKeys(const std::string &droppedKey, const Assignment &target);

    /// <summary>フル幅のドロップ受け皿を描く（ここに落とすと window/tab/section へ移動）</summary>
    void DrawDropZone(const char *label, const std::string &window, const std::string &tab, const std::string &section);

    /// ===================================================
    /// private variables
    /// ===================================================

    std::vector<Entry> entries_;                              ///< 登録済みパラメータ（登録順を保持）
    std::vector<WindowDef> windows_;                          ///< ユーザー作成ウィンドウ定義
    std::unordered_map<std::string, Assignment> assignments_; ///< key -> 仕分け先
    std::set<std::string> selectedKeys_;                      ///< 複数選択中のパラメータキー（まとめて移動用）
    std::unique_ptr<DataHandler> valueStore_;                 ///< パラメータ値の保存先（遅延生成）
    bool layoutLoaded_ = false;                               ///< レイアウト読み込み済みフラグ
    bool layoutDirty_ = false;                                ///< 未保存の仕分け変更があるか

    // ─── 確認ダイアログ（保存/リセット前の確認） ───
    bool wantOpenConfirm_ = false;          ///< 次フレームで確認モーダルを開く
    std::string confirmMessage_;            ///< 確認モーダルに出す文言
    std::function<void()> confirmAction_;   ///< OK時に実行する処理
};

} // namespace Hagine
