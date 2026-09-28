#include "BehaviorTreeAsset.h"
#include "utility/data/DataHandler.h"
#include <algorithm>
#include <iostream>

namespace Hagine {

namespace {
// 保存するときの数値のキー（param, param2〜param5）
const char *const kParamKeys[5] = {"param", "param2", "param3", "param4", "param5"};
// 繋がりが輪になっていたときに止める深さ
constexpr int kMaxBuildDepth = 256;
} // namespace

// ---------------------------------------------------------
// 読み込み・保存
// ---------------------------------------------------------
bool BehaviorTreeAsset::Load(const std::string &folder, const std::string &file)
{
    nodes.clear();
    links.clear();

    DataHandler handler(folder, file);
    if (!handler.Exists())
        return false;

    const json nodesJson = handler.Load("nodes", json::array());
    for (const auto &n : nodesJson)
    {
        BTNodeData node;
        node.id = n.value("id", 0);
        node.type = n.value("type", 0);
        node.title = n.value("title", std::string());
        node.x = n.value("x", 0.0f);
        node.y = n.value("y", 0.0f);
        for (int i = 0; i < 5; ++i)
            node.params[static_cast<size_t>(i)] = n.value(kParamKeys[i], 0.0f);
        // 文字の設定（旧形式は stateName という名前で持っていた）
        if (n.contains("text"))
            node.text = n["text"].get<std::string>();
        else if (n.contains("stateName"))
            node.text = n["stateName"].get<std::string>();

        const BTNodeTypeDesc *desc = BehaviorTreeRegistry::Get().Find(node.type);
        if (desc && node.text.empty())
            node.text = desc->defaultText;
        if (desc && desc->kind == BTNodeKind::WeightedRandom)
        {
            if (n.contains("weightedOutputs"))
            {
                for (const auto &w : n["weightedOutputs"])
                    node.weightedOutputs.push_back({w.value("pinId", 0), w.value("weight", 1.0f)});
            }
            else
            {
                node.weightedOutputs = BehaviorTreeRegistry::Get().MakeDefaultNode(node.type, node.id).weightedOutputs;
            }
        }
        nodes.push_back(std::move(node));
    }

    const json linksJson = handler.Load("links", json::array());
    for (const auto &l : linksJson)
        links.push_back({l.value("id", 0), l.value("start", 0), l.value("end", 0)});
    return true;
}

void BehaviorTreeAsset::Save(const std::string &folder, const std::string &file) const
{
    DataHandler handler(folder, file);
    json nodesJson = json::array();
    for (const auto &node : nodes)
    {
        json n;
        n["id"] = node.id;
        n["title"] = node.title;
        n["type"] = node.type;
        n["x"] = node.x;
        n["y"] = node.y;
        for (int i = 0; i < 5; ++i)
            n[kParamKeys[i]] = node.params[static_cast<size_t>(i)];

        const BTNodeTypeDesc *desc = BehaviorTreeRegistry::Get().Find(node.type);
        if (desc && !desc->textLabel.empty())
            n["text"] = node.text;
        if (!node.weightedOutputs.empty())
        {
            json weightsJson = json::array();
            for (const auto &output : node.weightedOutputs)
                weightsJson.push_back({{"pinId", output.pinId}, {"weight", output.weight}});
            n["weightedOutputs"] = weightsJson;
        }
        nodesJson.push_back(n);
    }
    handler.Save("nodes", nodesJson);

    json linksJson = json::array();
    for (const auto &link : links)
        linksJson.push_back({{"id", link.id}, {"start", link.start}, {"end", link.end}});
    handler.Save("links", linksJson);
}

// ---------------------------------------------------------
// 探索
// ---------------------------------------------------------
const BTNodeData *BehaviorTreeAsset::FindNode(int nodeId) const
{
    for (const auto &n : nodes)
    {
        if (n.id == nodeId)
            return &n;
    }
    return nullptr;
}

BTNodeData *BehaviorTreeAsset::FindNode(int nodeId)
{
    for (auto &n : nodes)
    {
        if (n.id == nodeId)
            return &n;
    }
    return nullptr;
}

int BehaviorTreeAsset::FindRootNodeId() const
{
    // どこからも入力されていない最初のノードを根とみなす
    for (const auto &node : nodes)
    {
        const int inputPin = BTPin::Input(node.id);
        const bool hasInput = std::any_of(links.begin(), links.end(), [inputPin](const BTLinkData &l) { return l.end == inputPin; });
        if (!hasInput)
            return node.id;
    }
    return -1;
}

std::vector<int> BehaviorTreeAsset::FindChildren(int outputPinId) const
{
    std::vector<int> children;
    for (const auto &link : links)
    {
        if (link.start == outputPinId)
            children.push_back(BTPin::NodeOfInput(link.end));
    }
    return children;
}

int BehaviorTreeAsset::NextNodeId() const
{
    int maxId = 0;
    for (const auto &n : nodes)
        maxId = std::max(maxId, n.id);
    return maxId + 1;
}

int BehaviorTreeAsset::NextLinkId() const
{
    int maxId = 0;
    for (const auto &l : links)
        maxId = std::max(maxId, l.id);
    return maxId + 1;
}

int BehaviorTreeAsset::NextExtraPinId() const
{
    // 追加の重み付き出力は決まった番号の範囲と重ならない所から払い出す
    int next = BTPin::kExtraStart;
    for (const auto &n : nodes)
    {
        for (const auto &w : n.weightedOutputs)
            next = std::max(next, w.pinId + 1);
    }
    return next;
}

// ---------------------------------------------------------
// 実行用ツリーの組み立て
// ---------------------------------------------------------
std::shared_ptr<BTNode> BehaviorTreeAsset::Build(int nodeId, std::map<int, std::shared_ptr<BTNode>> *outInstances) const
{
    if (nodeId < 0)
        nodeId = FindRootNodeId();
    if (nodeId < 0)
        return nullptr;
    return BuildRecursive(nodeId, outInstances, 0);
}

std::shared_ptr<BTNode> BehaviorTreeAsset::BuildRecursive(int nodeId, std::map<int, std::shared_ptr<BTNode>> *outInstances, int depth) const
{
    if (depth > kMaxBuildDepth)
        return nullptr;
    const BTNodeData *pNode = FindNode(nodeId);
    if (!pNode)
        return nullptr;
    const BTNodeData &nd = *pNode;
    const BTNodeTypeDesc *desc = BehaviorTreeRegistry::Get().Find(nd.type);
    if (!desc || !desc->create)
        return nullptr;

    std::shared_ptr<BTNode> runtimeNode = desc->create(nd);
    if (!runtimeNode)
        return nullptr;
    if (outInstances)
        (*outInstances)[nodeId] = runtimeNode;

    switch (desc->kind)
    {
    case BTNodeKind::Composite:
        for (int childId : FindChildren(BTPin::Output(nd.id)))
        {
            if (auto child = BuildRecursive(childId, outInstances, depth + 1))
                runtimeNode->AddChild(child);
        }
        return runtimeNode;

    case BTNodeKind::WeightedRandom:
        // 出力ごとの重みを持つ飾りで子を包み、ランダムセレクターへ入れる
        for (const auto &wp : nd.weightedOutputs)
        {
            for (int childId : FindChildren(wp.pinId))
            {
                if (auto child = BuildRecursive(childId, outInstances, depth + 1))
                {
                    auto decorator = std::make_shared<WeightDecoratorNode>(wp.weight);
                    decorator->AddChild(child);
                    runtimeNode->AddChild(decorator);
                }
            }
        }
        return runtimeNode;

    case BTNodeKind::Condition: {
        if (!desc->wrapChildren)
            return runtimeNode;
        const auto successIds = FindChildren(BTPin::Success(nd.id));
        const auto failureIds = FindChildren(BTPin::Failure(nd.id));
        if (successIds.empty() && failureIds.empty())
            return runtimeNode;

        // Selector{ Sequence{判定, 成功の先...}, 失敗の先... } に組み替える
        auto selectorWrapper = std::make_shared<SelectorNode>();
        if (!successIds.empty())
        {
            auto successSequence = std::make_shared<SequenceNode>();
            if (auto conditionCopy = desc->create(nd))
            {
                // 実際に判定を動かすのはこちらなので、状態の色付けもこちらを見る
                if (outInstances)
                    (*outInstances)[nodeId] = conditionCopy;
                successSequence->AddChild(conditionCopy);
                for (int childId : successIds)
                {
                    if (auto child = BuildRecursive(childId, outInstances, depth + 1))
                        successSequence->AddChild(child);
                }
                selectorWrapper->AddChild(successSequence);
            }
        }
        for (int childId : failureIds)
        {
            if (auto child = BuildRecursive(childId, outInstances, depth + 1))
                selectorWrapper->AddChild(child);
        }
        return selectorWrapper;
    }

    case BTNodeKind::Action:
    default:
        return runtimeNode;
    }
}

// ---------------------------------------------------------
// BehaviorTreeLoader
// ---------------------------------------------------------
std::shared_ptr<BTNode> BehaviorTreeLoader::LoadAndBuild(const std::string &folder, const std::string &file)
{
    BehaviorTreeAsset asset;
    if (!asset.Load(folder, file))
    {
        std::cerr << "[BehaviorTreeLoader] ファイルが見つかりません: " << folder << "/" << file << ".json" << std::endl;
        return nullptr;
    }
    if (asset.FindRootNodeId() == -1)
    {
        std::cerr << "[BehaviorTreeLoader] ルートノードが見つかりません" << std::endl;
        return nullptr;
    }
    auto root = asset.Build();
    if (!root)
        std::cerr << "[BehaviorTreeLoader] ツリーのビルドに失敗しました" << std::endl;
    return root;
}

} // namespace Hagine
