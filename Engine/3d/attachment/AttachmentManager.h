#pragma once
#include <string>
#include <type/Quaternion.h>
#include <type/Vector3.h>
#include <unordered_map>
#include <vector>

namespace Hagine {
class WorldTransform;
class DataHandler;

/// <summary>
/// 親子付けできる対象の種類。UIのアイコン代わりの分類と、まとめて登録解除するのに使う。
/// </summary>
enum class AttachKind
{
    Object,   // BaseObject（3Dオブジェクト）
    Light,    // 光源（点光源・スポットライト）
    Particle, // パーティクルエミッター
};

/// <summary>
/// 親子付けできる対象1件への参照。
///
/// 種類ごとに「トランスフォームの持ち方」が違うので、ここで吸収する:
///   ・BaseObject / CPUパーティクル … WorldTransform を持っている
///   ・光源 / GPUエミッター        … 生の Vector3 しか持っていない
///
/// 登録側はポインタを渡すだけでよい。std::vector の再確保などでポインタが変わる場合は、
/// 変わるたびに登録し直すこと（LightGroup のギズモ登録と同じ扱い）。
/// </summary>
struct AttachTarget
{
    AttachKind kind = AttachKind::Object;
    std::string name; // 一意な登録名。保存キーにもなる

    // SRT をまとめて持っている場合はこちら（優先される）
    WorldTransform *worldTransform = nullptr;

    // 生のメンバへの直接ポインタ（worldTransform が nullptr のとき使う）
    Vector3 *position = nullptr;      // 位置（必須）
    Vector3 *rotationEuler = nullptr; // 回転（任意・ラジアン）
    Vector3 *direction = nullptr;     // 向きベクトル（任意。スポットライト用）

    /// <summary>
    /// 位置・回転を扱えるだけの情報が揃っているか
    /// </summary>
    bool IsValid() const { return worldTransform != nullptr || position != nullptr; }
};

/// <summary>
/// 種類をまたいだ親子付け（アタッチ）を一手に引き受けるクラス。
///
/// 3Dオブジェクト同士の親子付けは WorldTransform のポインタを繋ぐ既存の仕組みが
/// そのまま使われる（描画がその行列を直接読むため）。こちらが面倒を見るのは
/// 「光源をプレイヤーに付ける」「パーティクルをボスに付ける」のように、
/// 片方が BaseObject でない組み合わせ。毎フレーム親のワールド行列から子の値を作り直す。
///
/// リンクは名前だけで持っているので、読み込みの順番を気にしなくてよい。
/// 相手がまだ居なければそのフレームは何もせず、両方そろった時点で効き始める。
/// </summary>
class AttachmentManager
{
  private:
    AttachmentManager() = default;
    ~AttachmentManager() = default;
    AttachmentManager(const AttachmentManager &) = delete;
    AttachmentManager &operator=(const AttachmentManager &) = delete;

  public:
    /// <summary>
    /// シングルトンインスタンスを取得
    /// </summary>
    static AttachmentManager *GetInstance()
    {
        static AttachmentManager instance;
        return &instance;
    }

    /// <summary>
    /// 終了処理（登録とリンクを全て破棄する）
    /// </summary>
    void Finalize();

    // ===================================================
    //  対象の登録
    // ===================================================

    /// <summary>
    /// 親子付けの対象として登録する。同名があれば上書きする
    /// </summary>
    /// <param name="target">登録する対象</param>
    void Register(const AttachTarget &target);

    /// <summary>
    /// 登録を解除する。リンクは消さないので、登録し直せばまた効く
    /// </summary>
    /// <param name="name">対象の名前</param>
    void Unregister(const std::string &name);

    /// <summary>
    /// 指定の種類の登録をまとめて解除する（ライトの一覧を作り直すときなどに使う）
    /// </summary>
    /// <param name="kind">対象の種類</param>
    void UnregisterKind(AttachKind kind);

    /// <summary>
    /// 名前から登録済みの対象を引く
    /// </summary>
    /// <param name="name">対象の名前</param>
    /// <returns>const AttachTarget*: 未登録なら nullptr</returns>
    const AttachTarget *FindTarget(const std::string &name) const;

    /// <summary>
    /// 登録済みの名前を種類で絞って取得する（名前順）
    /// </summary>
    /// <param name="kind">対象の種類</param>
    /// <returns>std::vector&lt;std::string&gt;: 名前の一覧</returns>
    std::vector<std::string> GetTargetNames(AttachKind kind) const;

    // ===================================================
    //  親子付け
    // ===================================================

    /// <summary>
    /// 子を親へ付ける。今の見た目の位置関係を保ったままアタッチする
    /// </summary>
    /// <param name="childName">子の名前</param>
    /// <param name="parentName">親の名前</param>
    /// <returns>bool: 付けられたら true（自分自身・循環・未登録なら false）</returns>
    bool Attach(const std::string &childName, const std::string &parentName);

    /// <summary>
    /// 親子付けを解除する。解除後もその場に留まる
    /// </summary>
    /// <param name="childName">子の名前</param>
    void Detach(const std::string &childName);

    /// <summary>
    /// 子に付いている親の名前を返す
    /// </summary>
    /// <param name="childName">子の名前</param>
    /// <returns>std::string: 親が無ければ空文字</returns>
    std::string GetParentName(const std::string &childName) const;

    /// <summary>
    /// この親に付いている子の名前を返す
    /// </summary>
    /// <param name="parentName">親の名前</param>
    /// <returns>std::vector&lt;std::string&gt;: 子の名前（名前順）</returns>
    std::vector<std::string> GetChildNames(const std::string &parentName) const;

    /// <summary>
    /// この名前を親に持つリンクが1つでもあるか
    /// </summary>
    bool HasChildren(const std::string &parentName) const;

    /// <summary>
    /// 対象が名前を変えたとき、リンクの参照を追従させる
    /// </summary>
    /// <param name="oldName">変更前の名前</param>
    /// <param name="newName">変更後の名前</param>
    void RenameTarget(const std::string &oldName, const std::string &newName);

    // ===================================================
    //  更新
    // ===================================================

    /// <summary>
    /// 全リンクを解決して子のトランスフォームを更新する。
    /// 親のワールド行列が確定した後（オブジェクトの更新後・描画の前）に毎フレーム呼ぶこと。
    /// </summary>
    void Update();

    // ===================================================
    //  UI・保存
    // ===================================================

    /// <summary>
    /// ImGuiによる一覧・親設定UI
    /// </summary>
    void DrawImGui();

    /// <summary>
    /// リンクをJSONへ保存する
    /// </summary>
    /// <param name="folderPath">jsons ルートからのフォルダパス</param>
    /// <param name="fileName">ファイル名</param>
    void Save(const std::string &folderPath, const std::string &fileName) const;

    /// <summary>
    /// リンクをJSONから読み込む（既存のリンクは置き換える）
    /// </summary>
    /// <param name="folderPath">jsons ルートからのフォルダパス</param>
    /// <param name="fileName">ファイル名</param>
    void Load(const std::string &folderPath, const std::string &fileName);

    /// <summary>
    /// 全リンクを破棄する（シーン切り替え時など）
    /// </summary>
    void ClearLinks() { links_.clear(); }

    /// <summary>
    /// 親子付けの数（統計表示用）
    /// </summary>
    size_t GetLinkCount() const { return links_.size(); }

  private:
    /// <summary>
    /// 1件の親子付け。トランスフォームは名前で解決するのでポインタは持たない。
    /// </summary>
    struct Link
    {
        std::string parentName; // 親の名前

        // 親のローカル空間での相対トランスフォーム。
        // アタッチ時に「今の見た目」から逆算し、以降は子を動かすたびに取り直す。
        Vector3 localPosition{};
        Quaternion localRotation = Quaternion::IdentityQuaternion();
        Vector3 localDirection = {0.0f, -1.0f, 0.0f};

        // 親のどの成分に追従するか
        bool inheritTranslation = true;
        bool inheritRotation = true;

        // 相対値を計算済みか（読み込み直後は false で、初回の解決時に確定させる）
        bool hasLocal = false;

        // 前フレームに書き込んだ値。これと違っていれば「子が外から動かされた」と判断し、
        // 相対値を取り直す。こうしておくと、アタッチ中でも子をギズモやUIで自由に動かせる。
        Vector3 lastWrittenPosition{};
        Quaternion lastWrittenRotation = Quaternion::IdentityQuaternion();
        Vector3 lastWrittenDirection{};
        bool hasWritten = false;
    };

    /// <summary>
    /// 対象の現在のワールド位置を読む
    /// </summary>
    static Vector3 ReadPosition(const AttachTarget &target);

    /// <summary>
    /// 対象の現在のワールド回転を読む
    /// </summary>
    static Quaternion ReadRotation(const AttachTarget &target);

    /// <summary>
    /// 対象へワールド位置を書く
    /// </summary>
    static void WritePosition(const AttachTarget &target, const Vector3 &position);

    /// <summary>
    /// 対象へワールド回転を書く（回転を持たない対象では何もしない）
    /// </summary>
    static void WriteRotation(const AttachTarget &target, const Quaternion &rotation);

    /// <summary>
    /// 循環参照になるか調べる
    /// </summary>
    /// <param name="childName">子の名前</param>
    /// <param name="parentName">親にしようとしている名前</param>
    /// <returns>bool: 循環するなら true</returns>
    bool WouldCreateCycle(const std::string &childName, const std::string &parentName) const;

    /// <summary>
    /// 親のワールド値から子の相対値を計算し直す
    /// </summary>
    void RebuildLocal(Link &link, const AttachTarget &child, const AttachTarget &parent) const;

    std::unordered_map<std::string, AttachTarget> targets_; // 登録済みの対象
    std::unordered_map<std::string, Link> links_;          // 子の名前 → 親と相対値

#ifdef USE_IMGUI
    std::string uiSelectedChild_; // UIで選択中の子
#endif
};
} // namespace Hagine
