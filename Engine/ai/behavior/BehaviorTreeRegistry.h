#pragma once
#include "BehaviorTreeNode.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// ファイルに保存されるノード1個分の情報（エディタと読み込みで共通）
/// </summary>
struct BTNodeData
{
    /// <summary>
    /// 重み付きランダムの出力1本分
    /// </summary>
    struct WeightPin
    {
        int pinId = 0;
        float weight = 1.0f;
    };

    int id = 0;                          // ノードID（ピンIDはここから決まる）
    int type = 0;                        // ノードの種類（登録表の typeId）
    std::string title;                   // 表示名
    float x = 0.0f;                      // キャンバス上の位置
    float y = 0.0f;                      //
    std::array<float, 5> params{};       // 数値の設定（param〜param5）
    std::string text;                    // 文字の設定（選択肢から選ぶ物など）
    std::vector<WeightPin> weightedOutputs; // 重み付きランダムの出力

    float Param(int index) const { return params[static_cast<size_t>(index)]; }
};

/// <summary>
/// ノードの種類の大分類（ピンの形と組み立て方が決まる）
/// </summary>
enum class BTNodeKind
{
    Composite,      // 子を順に扱う（出力ピン1本、繋いだ順が子の順）
    WeightedRandom, // 出力ごとに重みを持ち、重みに従って1本を選ぶ
    Condition,      // 成功/失敗の2本の出力を持つ判定
    Action,         // 出力なしの末端
};

/// <summary>
/// 数値の設定の見せ方
/// </summary>
enum class BTParamKind
{
    Float, // ドラッグで小数
    Int,   // ドラッグで整数（保存は小数のまま）
    Ratio, // 0〜1 のスライダー
    Bool,  // チェックボックス（1 以上で ON）
};

/// <summary>
/// 数値の設定1個分の説明（インスペクタの表示に使う）
/// </summary>
struct BTParamDesc
{
    int index = 0;               // params の何番目か（0〜4）
    std::string label;           // 表示名
    BTParamKind kind = BTParamKind::Float;
    float speed = 0.05f;         // ドラッグの細かさ
    float min = 0.0f;            //
    float max = 100.0f;          //
    const char *format = "%.2f"; // 表示の書式
};

/// <summary>
/// ノードの種類1個分の登録情報。ゲーム側はこれを埋めて BehaviorTreeRegistry へ登録する。
/// 生成・説明・設定項目をここにまとめておけば、エディタも読み込みも種類ごとの分岐を持たずに済む。
/// </summary>
struct BTNodeTypeDesc
{
    int typeId = 0;               // 保存される種類の番号（一度決めたら変えない）
    std::string name;             // 追加メニューと新しいノードの表示名
    std::string category;         // 追加メニューの分類（最初に出てきた順に並ぶ）
    BTNodeKind kind = BTNodeKind::Action;
    std::string description;      // ツールチップとインスペクタの説明
    std::vector<BTParamDesc> params; // 数値の設定
    std::array<float, 5> defaults{}; // 新しく置いたときの数値

    std::string textLabel;                // 文字の設定の表示名（空なら文字の設定なし）
    std::vector<std::string> textChoices; // 文字の設定の選択肢
    std::string defaultText;              // 新しく置いたときの文字

    std::string hint;                     // インスペクタの下に出す補足
    bool wrapChildren = true;             // 判定ノード: 成功/失敗の先に繋いだ物を分岐として組み立てる

    /// <summary>実行用のノードを作る（必須）</summary>
    std::function<std::shared_ptr<BTNode>(const BTNodeData &)> create;

    /// <summary>ノードの中に出す短い要約（省略時は数値の設定から自動で作る）</summary>
    std::function<std::string(const BTNodeData &)> summary;
};

/// <summary>
/// ノードの種類の登録表。エンジンは汎用のコンポジット（シーケンス・セレクター・ランダム・重み付け）を
/// 最初から持っていて、ゲーム側は自分のアクション・判定を起動時に Register する。
/// </summary>
class BehaviorTreeRegistry
{
  public:
    /// <summary>
    /// 登録表を取得（初回に汎用のコンポジットを登録する）
    /// </summary>
    static BehaviorTreeRegistry &Get();

    /// <summary>
    /// 種類を登録する（同じ typeId は上書き）
    /// </summary>
    void Register(BTNodeTypeDesc desc);

    /// <summary>
    /// 種類を探す（無ければ nullptr）
    /// </summary>
    const BTNodeTypeDesc *Find(int typeId) const;

    /// <summary>
    /// 登録順の一覧
    /// </summary>
    const std::vector<BTNodeTypeDesc> &All() const { return types_; }

    /// <summary>
    /// 分類の一覧（最初に出てきた順）
    /// </summary>
    std::vector<std::string> Categories() const;

    /// <summary>
    /// 指定の種類で、新しく置くノードの初期値を作る（ピンIDは id から決まる）
    /// </summary>
    BTNodeData MakeDefaultNode(int typeId, int id) const;

    /// <summary>
    /// エンジンが持つ汎用ノードの typeId（ゲーム側はこれと重ならない番号を使う）
    /// </summary>
    static constexpr int kTypeSequence = 0;
    static constexpr int kTypeSelector = 1;
    static constexpr int kTypeRandomSelector = 2;
    static constexpr int kTypeWeightedRandom = 3;
    static constexpr int kTypeSequenceOnce = 27;

  private:
    BehaviorTreeRegistry();
    void RegisterBuiltins();

    std::vector<BTNodeTypeDesc> types_;
};

} // namespace Hagine
