#define NOMINMAX
#include "BehaviorTreeNode.h"
#include <Random.h>

namespace Hagine {

// ---------------------------------------------------------
// BTNode / CompositeNode 基底
// ---------------------------------------------------------
NodeStatus BTNode::Tick()
{
    // 初回実行時、または Running 以外から再開された時に OnEnter を呼ぶ
    if (status_ != NodeStatus::Running)
        OnEnter();

    // 更新処理を実行しステータスを更新
    status_ = OnUpdate();

    // 終了（Success または Failure）した時に OnExit を呼ぶ
    if (status_ != NodeStatus::Running)
        OnExit();

    return status_;
}
void BTNode::Abort()
{
    if (status_ == NodeStatus::Running)
        OnExit();
    Reset();
}

void BTNode::AddChild(std::shared_ptr<BTNode> /*child*/) {}
void BTNode::SetContext(BTContext * /*pContext*/) {}
void BTNode::OnEnter() {}
void BTNode::OnExit() {}

void CompositeNode::AddChild(std::shared_ptr<BTNode> child)
{
    children_.push_back(child);
}

void CompositeNode::SetContext(BTContext *pContext)
{
    BTNode::SetContext(pContext);
    // 全ての子ノードにコンテキストを伝播
    for (auto &child : children_)
        child->SetContext(pContext);
}

void CompositeNode::OnEnter()
{
    // 最初の子から実行するようにインデックスをリセット
    currentChildIndex_ = 0;
}

// ---------------------------------------------------------
// SequenceNode (Reactive)
// ---------------------------------------------------------
NodeStatus SequenceNode::OnUpdate()
{
    if (children_.empty())
    {
        return NodeStatus::Failure;
    }

    // 毎フレーム先頭から順に子ノードを評価する (Reactive)
    for (int i = 0; i < static_cast<int>(children_.size()); ++i)
    {
        NodeStatus childStatus = children_[i]->Tick();

        if (childStatus == NodeStatus::Running)
        {
            // Running 中のノードより後のノードはリセットしておく
            for (int j = i + 1; j < static_cast<int>(children_.size()); ++j)
            {
                children_[j]->Abort();
            }
            return NodeStatus::Running;
        }

        if (childStatus == NodeStatus::Failure)
        {
            // 一つでも失敗した時点で以降のノードをリセットして失敗を返す
            for (int j = i + 1; j < static_cast<int>(children_.size()); ++j)
            {
                children_[j]->Abort();
            }
            return NodeStatus::Failure;
        }
        // Success の場合は次のループで次の子ノードを評価
    }

    // 全ての子ノードが Success だった場合のみ Success を返す
    return NodeStatus::Success;
}

// ---------------------------------------------------------
// SequenceOnceNode (Non-Reactive)
// ---------------------------------------------------------
NodeStatus SequenceOnceNode::OnUpdate()
{
    if (children_.empty())
        return NodeStatus::Failure;

    // 前回の続きから子ノードを順次実行する
    while (currentChildIndex_ < static_cast<int>(children_.size()))
    {
        NodeStatus status = children_[currentChildIndex_]->Tick();

        // 実行中の場合はそのノードで止まる
        if (status == NodeStatus::Running)
            return NodeStatus::Running;

        // 失敗した場合は即座に終了
        if (status == NodeStatus::Failure)
            return NodeStatus::Failure;

        // 成功した場合は次の子ノードへ進む
        currentChildIndex_++;
    }

    return NodeStatus::Success;
}

// ---------------------------------------------------------
// SelectorNode (Reactive)
// ---------------------------------------------------------
NodeStatus SelectorNode::OnUpdate()
{
    if (children_.empty())
    {
        return NodeStatus::Failure;
    }

    // 毎フレーム先頭から順に子ノードを評価する (Reactive)
    for (int i = 0; i < static_cast<int>(children_.size()); ++i)
    {
        NodeStatus childStatus = children_[i]->Tick();

        if (childStatus == NodeStatus::Running)
        {
            // 実行中のノード以降はリセット
            for (int j = i + 1; j < static_cast<int>(children_.size()); ++j)
            {
                children_[j]->Abort();
            }
            return NodeStatus::Running;
        }

        if (childStatus == NodeStatus::Success)
        {
            // 一つでも成功した時点で以降のノードをリセットして成功を返す
            for (int j = i + 1; j < static_cast<int>(children_.size()); ++j)
            {
                children_[j]->Abort();
            }
            return NodeStatus::Success;
        }
        // Failure の場合は次のループで次の子ノードを試す
    }

    return NodeStatus::Failure;
}

// ---------------------------------------------------------
// RandomSelectorNode
// ---------------------------------------------------------
void RandomSelectorNode::OnEnter()
{
    selectedChildIndex_ = -1;
    if (children_.empty())
        return;

    // 重みの合計を算出
    float totalWeight = 0.0f;
    std::vector<float> weights;

    for (const auto &child : children_)
    {
        float w = 1.0f;
        auto weightNode = std::dynamic_pointer_cast<WeightDecoratorNode>(child);
        if (weightNode)
        {
            w = weightNode->GetWeight();
        }
        if (w < 0.0f)
            w = 0.0f;

        weights.push_back(w);
        totalWeight += w;
    }

    // 算出した重みに基づいてランダムに子ノードを選択
    float randomValue = Random::Range(0.0f, totalWeight);
    float currentSum = 0.0f;
    for (int i = 0; i < static_cast<int>(weights.size()); ++i)
    {
        currentSum += weights[i];
        if (randomValue <= currentSum)
        {
            selectedChildIndex_ = i;
            break;
        }
    }

    // 誤差等で決まらなかった場合は最後の要素を選択
    if (selectedChildIndex_ == -1 && !children_.empty())
    {
        selectedChildIndex_ = static_cast<int>(children_.size()) - 1;
    }
}

NodeStatus RandomSelectorNode::OnUpdate()
{
    if (children_.empty())
    {
        return NodeStatus::Failure;
    }

    if (selectedChildIndex_ < 0 || selectedChildIndex_ >= static_cast<int>(children_.size()))
    {
        return NodeStatus::Failure;
    }

    // 選択された子ノードのみを実行
    return children_[selectedChildIndex_]->Tick();
}

} // namespace Hagine
