#include "BehaviorTreeRegistry.h"
#include "BehaviorTreeAsset.h"
#include <algorithm>

namespace Hagine {

BehaviorTreeRegistry &BehaviorTreeRegistry::Get()
{
    static BehaviorTreeRegistry instance;
    return instance;
}

BehaviorTreeRegistry::BehaviorTreeRegistry()
{
    RegisterBuiltins();
}

void BehaviorTreeRegistry::Register(BTNodeTypeDesc desc)
{
    for (auto &t : types_)
    {
        if (t.typeId == desc.typeId)
        {
            t = std::move(desc);
            return;
        }
    }
    types_.push_back(std::move(desc));
}

const BTNodeTypeDesc *BehaviorTreeRegistry::Find(int typeId) const
{
    for (const auto &t : types_)
    {
        if (t.typeId == typeId)
            return &t;
    }
    return nullptr;
}

std::vector<std::string> BehaviorTreeRegistry::Categories() const
{
    std::vector<std::string> result;
    for (const auto &t : types_)
    {
        if (std::find(result.begin(), result.end(), t.category) == result.end())
            result.push_back(t.category);
    }
    return result;
}

BTNodeData BehaviorTreeRegistry::MakeDefaultNode(int typeId, int id) const
{
    BTNodeData node;
    node.id = id;
    node.type = typeId;
    if (const BTNodeTypeDesc *desc = Find(typeId))
    {
        node.title = desc->name;
        node.params = desc->defaults;
        node.text = desc->defaultText;
        if (desc->kind == BTNodeKind::WeightedRandom)
        {
            // 出力は最初から2本（重み 1:1）
            node.weightedOutputs = {{BTPin::FirstWeighted(id), 1.0f}, {BTPin::FirstWeighted(id) + 1, 1.0f}};
        }
    }
    return node;
}

void BehaviorTreeRegistry::RegisterBuiltins()
{
    const std::string kCategory = "コンポジット";

    BTNodeTypeDesc seq;
    seq.typeId = kTypeSequence;
    seq.name = "シーケンス";
    seq.category = kCategory;
    seq.kind = BTNodeKind::Composite;
    seq.description = "子ノードを順番に実行し、全て成功で成功を返す";
    seq.create = [](const BTNodeData &) { return std::make_shared<SequenceNode>(); };
    Register(seq);

    BTNodeTypeDesc once;
    once.typeId = kTypeSequenceOnce;
    once.name = "シーケンス(完走)";
    once.category = kCategory;
    once.kind = BTNodeKind::Composite;
    once.description = "子ノードを順番に最後まで実行する。実行中のアクションは条件が変わっても中断しない";
    once.create = [](const BTNodeData &) { return std::make_shared<SequenceOnceNode>(); };
    Register(once);

    BTNodeTypeDesc sel;
    sel.typeId = kTypeSelector;
    sel.name = "セレクター";
    sel.category = kCategory;
    sel.kind = BTNodeKind::Composite;
    sel.description = "子ノードを順番に試し、1つでも成功したら成功を返す";
    sel.create = [](const BTNodeData &) { return std::make_shared<SelectorNode>(); };
    Register(sel);

    BTNodeTypeDesc rnd;
    rnd.typeId = kTypeRandomSelector;
    rnd.name = "ランダムセレクター";
    rnd.category = kCategory;
    rnd.kind = BTNodeKind::Composite;
    rnd.description = "子ノードの中からランダムに1つを選択して実行する";
    rnd.create = [](const BTNodeData &) { return std::make_shared<RandomSelectorNode>(); };
    Register(rnd);

    BTNodeTypeDesc weight;
    weight.typeId = kTypeWeightedRandom;
    weight.name = "重み付けデコレータ";
    weight.category = kCategory;
    weight.kind = BTNodeKind::WeightedRandom;
    weight.description = "複数の行動に重み付けしてランダム選択する";
    weight.create = [](const BTNodeData &) { return std::make_shared<RandomSelectorNode>(); };
    Register(weight);
}

} // namespace Hagine
