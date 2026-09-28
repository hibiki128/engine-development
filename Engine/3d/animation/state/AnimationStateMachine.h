#pragma once
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// ステートマシンのパラメータの種類
/// </summary>
enum class AnimParamType
{
    Float,   ///< 数値（速さなど）
    Bool,    ///< オン/オフ（地上にいるか など）
    Trigger, ///< 一度だけの合図（撃った など。遷移に使われると消える）
};

/// <summary>
/// パラメータの定義（名前と種類と初期値）
/// </summary>
struct AnimParamDef
{
    std::string name;
    AnimParamType type = AnimParamType::Float;
    float defaultValue = 0.0f; ///< Bool は 0/1
};

/// <summary>
/// 遷移の条件の比べ方
/// </summary>
enum class AnimConditionOp
{
    Greater,   ///< 数値が value より大きい
    Less,      ///< 数値が value より小さい
    IsTrue,    ///< オン
    IsFalse,   ///< オフ
    Triggered, ///< 合図が来ている
};

/// <summary>
/// 遷移の条件1つ（全部満たしたら遷移する）
/// </summary>
struct AnimCondition
{
    std::string param;
    AnimConditionOp op = AnimConditionOp::Greater;
    float value = 0.0f;
};

/// <summary>
/// ステートが再生するもの
/// </summary>
enum class AnimStateKind
{
    Clip,       ///< アニメーションファイル1つ
    BlendSpace, ///< 複数のファイルをパラメータで混ぜる
};

/// <summary>
/// ブレンドスペースの点（ステートの中に持つ）
/// </summary>
struct AnimBlendPoint
{
    std::string file;  ///< アニメーションファイル
    float x = 0.0f;    ///< 位置X
    float y = 0.0f;    ///< 位置Y
    float speed = 1.0f;
};

/// <summary>
/// ステート（ノード）
/// </summary>
struct AnimStateData
{
    int id = 0;
    std::string name = "State";
    AnimStateKind kind = AnimStateKind::Clip;
    std::string file;   ///< Clip のときのアニメーションファイル（"animation/Player/Idle.gltf" など）
    bool loop = true;   ///< Clip のときループするか
    float speed = 1.0f; ///< 再生速度

    // ---- BlendSpace のとき ----
    int blendMode = 0;                  ///< BlendSpaceMode の値
    std::vector<AnimBlendPoint> points; ///< 点
    std::string paramX;                 ///< X に使うパラメータ名
    std::string paramY;                 ///< Y に使うパラメータ名

    // ---- エディタ ----
    float x = 0.0f;
    float y = 0.0f;
};

/// <summary>
/// 遷移（線）。from が kAnyState なら「どのステートからでも」
/// </summary>
struct AnimTransitionData
{
    int id = 0;
    int from = 0;
    int to = 0;
    std::vector<AnimCondition> conditions;
    bool hasExitTime = false; ///< 再生が exitTime（1で1周）まで進むのを待つ
    float exitTime = 1.0f;
    float duration = 0.25f; ///< 切り替えにかける時間（秒）
};

/// <summary>
/// アニメーションのステートマシン（データ）。
/// ステート・遷移・パラメータを持ち、JSON（jsons/AnimationStateMachine/名前.json）に保存する。
/// 同じ名前のものは Library で1つにまとめて共有し、エディタで直すと動いているキャラへすぐ効く
/// </summary>
class AnimationStateMachineAsset
{
  public:
    static constexpr int kAnyState = -1;                          ///< 「どのステートからでも」
    static constexpr const char *kFolder = "AnimationStateMachine"; ///< 保存フォルダ

    std::vector<AnimParamDef> params;
    std::vector<AnimStateData> states;
    std::vector<AnimTransitionData> transitions;
    int entryState = 0;     ///< 最初に入るステート
    float anyStateX = -300.0f; ///< エディタでの「どこからでも」の位置
    float anyStateY = 0.0f;

    /// <summary>
    /// 読み込む（見つからなければ false）
    /// </summary>
    bool Load(const std::string &file);

    /// <summary>
    /// 保存する
    /// </summary>
    void Save(const std::string &file) const;

    /// <summary>
    /// 中身を JSON にする / JSON から戻す（保存と「元に戻す」で使う）
    /// </summary>
    nlohmann::json ToJson() const;
    bool FromJson(const nlohmann::json &root);

    AnimStateData *FindState(int id);
    const AnimStateData *FindState(int id) const;
    const AnimParamDef *FindParam(const std::string &name) const;
    int NextStateId() const;
    int NextTransitionId() const;

    /// <summary>
    /// 中身を変えたら呼ぶ（動いているキャラが登録し直す目印）
    /// </summary>
    void Touch() { ++revision_; }
    int GetRevision() const { return revision_; }

  private:
    int revision_ = 0;
};

/// <summary>
/// 名前ごとに1つだけ読み込んで共有する置き場
/// </summary>
class AnimationStateMachineLibrary
{
  public:
    /// <summary>
    /// 名前で取得する（まだなら読み込む。ファイルが無ければ nullptr）
    /// </summary>
    static std::shared_ptr<AnimationStateMachineAsset> Get(const std::string &file);

    /// <summary>
    /// 読み込み済みのものを差し替える（エディタで新規作成・読み直したとき）
    /// </summary>
    static void Put(const std::string &file, const std::shared_ptr<AnimationStateMachineAsset> &asset);

    /// <summary>
    /// 保存済みのファイル名の一覧
    /// </summary>
    static std::vector<std::string> ListFiles();
};

} // namespace Hagine
