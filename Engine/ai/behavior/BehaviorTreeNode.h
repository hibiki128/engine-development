#pragma once
#include <memory>
#include <vector>

namespace Hagine {

/// <summary>
/// ノードの実行結果
/// </summary>
enum class NodeStatus
{
    Success,
    Failure,
    Running,
    Idle
};

/// <summary>
/// ビヘイビアツリーが動かす対象の情報（誰が・誰に対して、など）。
/// エンジンは中身を知らないので、ゲーム側で派生させて必要なものを持たせる
/// （例: 敵と、その相手のプレイヤー）。ノードは SetContext で受け取り、自分の型に戻して使う。
/// </summary>
struct BTContext
{
    virtual ~BTContext() = default;
};

/// <summary>
/// ビヘイビアツリーのノード基底クラス
/// </summary>
class BTNode
{
  public:
    virtual ~BTNode() = default;

    /// <summary>
    /// ノードの実行（Running 以外から始まるときは OnEnter、終わったら OnExit を呼ぶ）
    /// </summary>
    NodeStatus Tick();

    /// <summary>
    /// 子ノードの追加（子を持たないノードは何もしない）
    /// </summary>
    virtual void AddChild(std::shared_ptr<BTNode> child);

    /// <summary>
    /// 現在の状態を取得
    /// </summary>
    NodeStatus GetStatus() const { return status_; }

    /// <summary>
    /// 状態をリセット
    /// </summary>
    virtual void Reset() { status_ = NodeStatus::Idle; }

    /// <summary>
    /// 打ち切る。実行中なら OnExit を呼んでから状態を戻す
    /// （別の枝へ切り替わったとき、ガードや溜めを付けっぱなしにしないため）
    /// </summary>
    void Abort();

    /// <summary>
    /// 動かす対象の情報を渡す（子を持つノードは子へも伝える）
    /// </summary>
    virtual void SetContext(BTContext *pContext);

  protected:
    /// <summary>ノード開始時の処理</summary>
    virtual void OnEnter();

    /// <summary>更新処理（派生クラスで実装）</summary>
    virtual NodeStatus OnUpdate() = 0;

    /// <summary>ノード終了時の処理</summary>
    virtual void OnExit();

    NodeStatus status_ = NodeStatus::Idle; // ノードの現在の状態
};

/// <summary>
/// 複数の子ノードを持つコンポジットノードの基底クラス
/// </summary>
class CompositeNode : public BTNode
{
  public:
    void AddChild(std::shared_ptr<BTNode> child) override;
    void SetContext(BTContext *pContext) override;

    void Reset() override
    {
        BTNode::Reset();
        currentChildIndex_ = 0;
        for (auto &child : children_)
        {
            child->Abort();
        }
    }

  protected:
    /// <summary>ノード開始時の処理（最初の子から実行する）</summary>
    void OnEnter() override;

    std::vector<std::shared_ptr<BTNode>> children_; // 子ノードのリスト
    int currentChildIndex_ = 0;                     // 現在実行中の子ノードのインデックス
};

/// <summary>
/// 重み付きデコレーターノード（ランダムセレクターが選ぶ確率の重みを持つ）
/// </summary>
class WeightDecoratorNode : public CompositeNode
{
  public:
    WeightDecoratorNode(float weight) : weight_(weight) {}

    /// <summary>更新処理（子ノードにパススルー）</summary>
    NodeStatus OnUpdate() override
    {
        if (children_.empty())
            return NodeStatus::Failure;
        return children_[0]->Tick();
    }

    /// <summary>重みを取得</summary>
    float GetWeight() const { return weight_; }

  private:
    float weight_ = 1.0f; // 重みの値
};

/// <summary>
/// 子ノードからランダムに一つを選択して実行するノード（子が重み付きデコレーターなら重みに従う）
/// </summary>
class RandomSelectorNode : public CompositeNode
{
  protected:
    void OnEnter() override;
    NodeStatus OnUpdate() override;

  private:
    int selectedChildIndex_ = -1; // 選択された子のインデックス
};

/// <summary>
/// 子ノードを順番に実行するノード (Reactive)。毎フレーム先頭から条件を再評価する
/// </summary>
class SequenceNode : public CompositeNode
{
  protected:
    NodeStatus OnUpdate() override;
};

/// <summary>
/// 子ノードを順番に最後まで実行するノード (Non-Reactive)。Running 中のアクションは条件変化で中断されない
/// </summary>
class SequenceOnceNode : public CompositeNode
{
  protected:
    NodeStatus OnUpdate() override;
};

/// <summary>
/// 子ノードを順番に試し、一つでも成功すれば終了するノード (Reactive)
/// </summary>
class SelectorNode : public CompositeNode
{
  protected:
    NodeStatus OnUpdate() override;
};

} // namespace Hagine
