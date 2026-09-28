#ifdef USE_IMGUI
#include "BehaviorTreeEditor.h"
#include "BehaviorTreeEditorStyle.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#include <algorithm>
#include <asset/AssetPath.h>
#include <edit/undo/UndoRedoManager.h>
#include <filesystem>
#include <set>

namespace ed = ax::NodeEditor;

// エディタの土台（ファイル・実行・ノードの追加と削除・繋ぎ方の判定・Undo）。
// 描画は BehaviorTreeEditorCanvas.cpp（ツールバーとグラフ）と BehaviorTreeEditorInspector.cpp（右の設定）にある。

namespace Hagine {

namespace {
// 警告の通知色（他のエディタの警告と同じ）
const Vector4 kNoticeWarn = {0.9f, 0.5f, 0.3f, 1.0f};
// 複製したノードをずらす量
constexpr float kDuplicateOffset = 40.0f;

// 今あるエディタ（作られた順）
std::vector<BehaviorTreeEditor *> &Instances()
{
    static std::vector<BehaviorTreeEditor *> instances;
    return instances;
}
} // namespace

const std::vector<BehaviorTreeEditor *> &BehaviorTreeEditor::GetInstances()
{
    return Instances();
}

BehaviorTreeEditor::BehaviorTreeEditor()
{
    ed::Config config;
    config.SettingsFile = nullptr;
    pContext_ = ed::CreateEditor(&config);
    alive_ = std::make_shared<BehaviorTreeEditor *>(this);
    Instances().push_back(this);
    // 空のツリーを基準にしておく（しないと最初のフレームで「編集した」扱いになり履歴に積まれる）
    ResetUndoBaseline();
}

BehaviorTreeEditor::~BehaviorTreeEditor()
{
    // Undo 履歴に残った操作が、消えた自分を触らないようにする
    *alive_ = nullptr;
    std::erase(Instances(), this);
    if (pContext_)
        ed::DestroyEditor(pContext_);
}

// ---------------------------------------------------------
// ファイル
// ---------------------------------------------------------
bool BehaviorTreeEditor::Open(const std::string &folder, const std::string &file)
{
    BehaviorTreeAsset loaded;
    if (!loaded.Load(folder, file))
    {
        ImGuiNotification::Post("ビヘイビアツリーが見つかりません: " + file, kNoticeWarn);
        return false;
    }
    StopRun();
    asset_ = std::move(loaded);
    folder_ = folder;
    fileName_ = file;
    nextNodeId_ = asset_.NextNodeId();
    nextLinkId_ = asset_.NextLinkId();
    nextPinId_ = asset_.NextExtraPinId();
    statusTimers_.clear();
    positionsPending_ = true;
    navigateCountdown_ = 3;
    modified_ = false;
    fileListDirty_ = true;
    ResetUndoBaseline();
    return true;
}

void BehaviorTreeEditor::Save()
{
    if (fileName_.empty())
        fileName_ = "NewBehavior";
    asset_.Save(folder_, fileName_);
    modified_ = false;
    fileListDirty_ = true;
    ImGuiNotification::Post("ビヘイビアツリーを保存しました: " + fileName_);
}

void BehaviorTreeEditor::NewTree()
{
    StopRun();
    asset_ = BehaviorTreeAsset();
    fileName_ = "NewBehavior";
    nextNodeId_ = 1;
    nextLinkId_ = 1;
    nextPinId_ = BTPin::kExtraStart;
    statusTimers_.clear();
    modified_ = false;
    ResetUndoBaseline();
}

void BehaviorTreeEditor::RefreshFileList()
{
    fileList_.clear();
    std::error_code error;
    const std::filesystem::path folder = std::filesystem::path(AssetPath::JsonRoot()) / folder_;
    for (const auto &entry : std::filesystem::directory_iterator(folder, error))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            fileList_.push_back(entry.path().stem().string());
    }
    std::sort(fileList_.begin(), fileList_.end());
    fileListDirty_ = false;
}

// ---------------------------------------------------------
// 実行
// ---------------------------------------------------------
void BehaviorTreeEditor::BuildAndRun()
{
    StopSingleTest();
    nodeInstanceMap_.clear();
    runtimeRoot_ = asset_.Build(-1, &nodeInstanceMap_);
    if (!runtimeRoot_)
    {
        isRunning_ = false;
        ImGuiNotification::Post("ツリーを組み立てられませんでした（根のノードがありません）", kNoticeWarn);
        return;
    }
    runtimeRoot_->SetContext(pDebugContext_);
    isRunning_ = true;
    if (onTreeBuilt_)
        onTreeBuilt_(runtimeRoot_);
}

void BehaviorTreeEditor::StopRun()
{
    const bool wasRunning = isRunning_ || runtimeRoot_;
    isRunning_ = false;
    runtimeRoot_ = nullptr;
    StopSingleTest();
    nodeInstanceMap_.clear();
    if (wasRunning && onTreeBuilt_)
        onTreeBuilt_(nullptr);
}

void BehaviorTreeEditor::StartSingleTest(int nodeId)
{
    // ツリー全体は止めて、選んだノード（とその下）だけを動かす
    StopRun();
    nodeInstanceMap_.clear();
    singleTestNode_ = asset_.Build(nodeId, &nodeInstanceMap_);
    if (!singleTestNode_)
        return;
    singleTestNode_->SetContext(pDebugContext_);
    singleTestNode_->Reset();
    singleTestNodeId_ = nodeId;
    singleTestResult_ = NodeStatus::Running;
    isSingleTesting_ = true;
}

void BehaviorTreeEditor::StopSingleTest()
{
    isSingleTesting_ = false;
    singleTestNode_ = nullptr;
    singleTestNodeId_ = -1;
    singleTestResult_ = NodeStatus::Idle;
}

void BehaviorTreeEditor::UpdateSingleTest()
{
    if (!isSingleTesting_ || !singleTestNode_)
        return;
    const NodeStatus s = singleTestNode_->Tick();
    singleTestResult_ = s;
    if (s == NodeStatus::Success || s == NodeStatus::Failure)
    {
        // 終わったら自動で止める（結果の光は残す）
        statusTimers_[singleTestNodeId_] = (s == NodeStatus::Success) ? BTEditorStyle::kStatusGlowTime : -BTEditorStyle::kStatusGlowTime;
        isSingleTesting_ = false;
    }
}

void BehaviorTreeEditor::UpdateStatusTimers(float dt)
{
    // 正 = 成功の光の残り時間、負 = 失敗の光の残り時間
    for (auto &[id, timer] : statusTimers_)
    {
        if (timer > 0.0f)
            timer = std::max(0.0f, timer - dt * BTEditorStyle::kStatusGlowDecay);
        else if (timer < 0.0f)
            timer = std::min(0.0f, timer + dt * BTEditorStyle::kStatusGlowDecay);
    }
    if (!isRunning_ && !isSingleTesting_)
        return;
    for (const auto &[nid, instance] : nodeInstanceMap_)
    {
        if (!instance)
            continue;
        const NodeStatus s = instance->GetStatus();
        if (s == NodeStatus::Success)
            statusTimers_[nid] = BTEditorStyle::kStatusGlowTime;
        else if (s == NodeStatus::Failure)
            statusTimers_[nid] = -BTEditorStyle::kStatusGlowTime;
    }
}

NodeStatus BehaviorTreeEditor::GetDisplayStatus(int nodeId) const
{
    if (!isRunning_ && !isSingleTesting_)
        return NodeStatus::Idle;
    auto it = nodeInstanceMap_.find(nodeId);
    return (it != nodeInstanceMap_.end() && it->second) ? it->second->GetStatus() : NodeStatus::Idle;
}

float BehaviorTreeEditor::GetStatusTimer(int nodeId) const
{
    auto it = statusTimers_.find(nodeId);
    return it != statusTimers_.end() ? it->second : 0.0f;
}

// ---------------------------------------------------------
// ノードの追加・複製・削除
// ---------------------------------------------------------
int BehaviorTreeEditor::CreateNode(int typeId, const ImVec2 &position)
{
    const int id = nextNodeId_++;
    BTNodeData node = BehaviorTreeRegistry::Get().MakeDefaultNode(typeId, id);
    node.x = position.x;
    node.y = position.y;
    asset_.nodes.push_back(node);
    ed::SetNodePosition(ed::NodeId(id), position);
    undoLabel_ = "ノードを追加";
    modified_ = true;
    return id;
}

void BehaviorTreeEditor::DuplicateNodes(const std::vector<int> &nodeIds)
{
    if (nodeIds.empty())
        return;
    // 元のピン → 複製のピン（選んだノード同士の繋がりも写す）
    std::map<int, int> pinMap;
    std::vector<int> newIds;
    for (int srcId : nodeIds)
    {
        const BTNodeData *src = asset_.FindNode(srcId);
        if (!src)
            continue;
        BTNodeData copy = *src;
        copy.id = nextNodeId_++;
        copy.x += kDuplicateOffset;
        copy.y += kDuplicateOffset;
        pinMap[BTPin::Input(srcId)] = BTPin::Input(copy.id);
        pinMap[BTPin::Output(srcId)] = BTPin::Output(copy.id);
        pinMap[BTPin::Success(srcId)] = BTPin::Success(copy.id);
        pinMap[BTPin::Failure(srcId)] = BTPin::Failure(copy.id);
        for (size_t i = 0; i < copy.weightedOutputs.size(); ++i)
        {
            const int newPin = (i < 2) ? BTPin::FirstWeighted(copy.id) + static_cast<int>(i) : nextPinId_++;
            pinMap[copy.weightedOutputs[i].pinId] = newPin;
            copy.weightedOutputs[i].pinId = newPin;
        }
        ed::SetNodePosition(ed::NodeId(copy.id), ImVec2(copy.x, copy.y));
        newIds.push_back(copy.id);
        asset_.nodes.push_back(std::move(copy));
    }
    const size_t linkCount = asset_.links.size();
    for (size_t i = 0; i < linkCount; ++i)
    {
        const BTLinkData link = asset_.links[i];
        auto s = pinMap.find(link.start);
        auto e = pinMap.find(link.end);
        if (s != pinMap.end() && e != pinMap.end())
            asset_.links.push_back({nextLinkId_++, s->second, e->second});
    }
    ed::ClearSelection();
    for (int id : newIds)
        ed::SelectNode(ed::NodeId(id), true);
    undoLabel_ = "ノードを複製";
    modified_ = true;
}

void BehaviorTreeEditor::DeleteNode(int nodeId)
{
    const BTNodeData *node = asset_.FindNode(nodeId);
    if (!node)
        return;
    // このノードのピンに触れている繋がりも消す
    std::set<int> pins = {BTPin::Input(nodeId), BTPin::Output(nodeId), BTPin::Success(nodeId), BTPin::Failure(nodeId)};
    for (const auto &w : node->weightedOutputs)
        pins.insert(w.pinId);
    std::erase_if(asset_.links, [&pins](const BTLinkData &l) { return pins.count(l.start) || pins.count(l.end); });
    std::erase_if(asset_.nodes, [nodeId](const BTNodeData &n) { return n.id == nodeId; });
    statusTimers_.erase(nodeId);
    undoLabel_ = "ノードを削除";
    modified_ = true;
}

void BehaviorTreeEditor::RemoveWeightedOutput(BTNodeData &node, int index)
{
    if (index < 0 || index >= static_cast<int>(node.weightedOutputs.size()))
        return;
    // 消す出力ピンに繋がっているリンクも一緒に除去する
    const int removedPin = node.weightedOutputs[static_cast<size_t>(index)].pinId;
    std::erase_if(asset_.links, [removedPin](const BTLinkData &l) { return l.start == removedPin; });
    node.weightedOutputs.erase(node.weightedOutputs.begin() + index);
    undoLabel_ = "重みの出力を削除";
    modified_ = true;
}

// ---------------------------------------------------------
// ピンと繋がり
// ---------------------------------------------------------
BehaviorTreeEditor::PinRole BehaviorTreeEditor::GetPinRole(int pinId, int *outNodeId) const
{
    const int slot = BTPin::Slot(pinId);
    if (slot >= 1 && slot <= 4)
    {
        const int nodeId = (pinId - BTPin::kOffset) / 10;
        const BTNodeData *node = asset_.FindNode(nodeId);
        if (node)
        {
            const BTNodeTypeDesc *desc = BehaviorTreeRegistry::Get().Find(node->type);
            if (outNodeId)
                *outNodeId = nodeId;
            if (slot == 1)
                return PinRole::Input;
            const BTNodeKind kind = desc ? desc->kind : BTNodeKind::Action;
            if (slot == 2 && kind == BTNodeKind::Composite)
                return PinRole::Output;
            if (slot == 3 && kind == BTNodeKind::Condition)
                return PinRole::Success;
            if (slot == 4 && kind == BTNodeKind::Condition)
                return PinRole::Failure;
            return PinRole::None;
        }
    }
    for (const auto &node : asset_.nodes)
    {
        for (const auto &w : node.weightedOutputs)
        {
            if (w.pinId == pinId)
            {
                if (outNodeId)
                    *outNodeId = node.id;
                return PinRole::Weighted;
            }
        }
    }
    return PinRole::None;
}

const char *BehaviorTreeEditor::ValidateLink(int &startPin, int &endPin) const
{
    int startNode = -1, endNode = -1;
    PinRole a = GetPinRole(startPin, &startNode);
    PinRole b = GetPinRole(endPin, &endNode);
    // 入力側からつまんだときは向きを直す
    if (a == PinRole::Input && b != PinRole::Input)
    {
        std::swap(startPin, endPin);
        std::swap(a, b);
        std::swap(startNode, endNode);
    }
    if (a == PinRole::None || b == PinRole::None)
        return "このピンは繋げません";
    if (a == PinRole::Input && b == PinRole::Input)
        return "入力どうしは繋げません";
    if (b != PinRole::Input)
        return "出力どうしは繋げません";
    if (startNode == endNode)
        return "自分自身には繋げません";
    for (const auto &l : asset_.links)
    {
        if (l.start == startPin && l.end == endPin)
            return "もう繋がっています";
    }
    return nullptr;
}

int BehaviorTreeEditor::FirstOutputPin(const BTNodeData &node) const
{
    const BTNodeTypeDesc *desc = BehaviorTreeRegistry::Get().Find(node.type);
    if (!desc)
        return 0;
    switch (desc->kind)
    {
    case BTNodeKind::Composite:
        return BTPin::Output(node.id);
    case BTNodeKind::Condition:
        return BTPin::Success(node.id);
    case BTNodeKind::WeightedRandom:
        return node.weightedOutputs.empty() ? 0 : node.weightedOutputs.front().pinId;
    case BTNodeKind::Action:
        break;
    }
    return 0;
}

void BehaviorTreeEditor::ConnectToPendingPin(int newNodeId)
{
    const int pin = pendingLinkPin_;
    pendingLinkPin_ = 0;
    if (pin == 0)
        return;
    const BTNodeData *node = asset_.FindNode(newNodeId);
    if (!node)
        return;
    // 出力から引っ張った → 新しいノードの入力へ / 入力から引っ張った → 新しいノードの最初の出力から
    int start = pin;
    int end = BTPin::Input(newNodeId);
    if (GetPinRole(pin) == PinRole::Input)
    {
        start = FirstOutputPin(*node);
        end = pin;
        if (start == 0)
            return;
    }
    if (!ValidateLink(start, end))
        asset_.links.push_back({nextLinkId_++, start, end});
}

void BehaviorTreeEditor::HandleCreateAction()
{
    if (ed::BeginCreate(BTEditorStyle::kLinkAccept, 2.0f))
    {
        ed::PinId startId, endId;
        if (ed::QueryNewLink(&startId, &endId))
        {
            int start = static_cast<int>(startId.Get());
            int end = static_cast<int>(endId.Get());
            if (const char *reason = ValidateLink(start, end))
            {
                ed::RejectNewItem(BTEditorStyle::kLinkReject, 2.0f);
                ed::Suspend();
                ImGui::SetTooltip("%s", reason);
                ed::Resume();
            }
            else if (ed::AcceptNewItem())
            {
                asset_.links.push_back({nextLinkId_++, start, end});
                undoLabel_ = "ノードを接続";
                modified_ = true;
            }
        }

        // ピンから何も無い所へ離したら、その場にノードを足すメニューを出す
        ed::PinId pinId;
        if (ed::QueryNewNode(&pinId))
        {
            if (ed::AcceptNewItem())
            {
                pendingLinkPin_ = static_cast<int>(pinId.Get());
                createPos_ = ed::ScreenToCanvas(ImGui::GetMousePos());
                openCreateMenu_ = true;
            }
        }
    }
    ed::EndCreate();
}

void BehaviorTreeEditor::HandleDeleteAction()
{
    if (ed::BeginDelete())
    {
        ed::NodeId nodeId;
        while (ed::QueryDeletedNode(&nodeId))
        {
            if (ed::AcceptDeletedItem())
                DeleteNode(static_cast<int>(nodeId.Get()));
        }
        ed::LinkId linkId;
        while (ed::QueryDeletedLink(&linkId))
        {
            if (ed::AcceptDeletedItem())
            {
                const int id = static_cast<int>(linkId.Get());
                std::erase_if(asset_.links, [id](const BTLinkData &l) { return l.id == id; });
                if (undoLabel_.empty())
                    undoLabel_ = "接続を削除";
                modified_ = true;
            }
        }
    }
    ed::EndDelete();
}

std::vector<int> BehaviorTreeEditor::GetSelectedNodeIds() const
{
    std::vector<int> result;
    const int count = ed::GetSelectedObjectCount();
    if (count <= 0)
        return result;
    std::vector<ed::NodeId> selected(static_cast<size_t>(count));
    const int n = ed::GetSelectedNodes(selected.data(), count);
    for (int i = 0; i < n; ++i)
        result.push_back(static_cast<int>(selected[static_cast<size_t>(i)].Get()));
    return result;
}

// ---------------------------------------------------------
// Undo
// ---------------------------------------------------------
nlohmann::json BehaviorTreeEditor::CaptureUndoState() const
{
    // トップレベルを「ノード1個 = 1キー」にして、変わったノードだけが差分に載るようにする
    nlohmann::json state = nlohmann::json::object();
    for (const auto &n : asset_.nodes)
    {
        nlohmann::json j;
        j["id"] = n.id;
        j["type"] = n.type;
        j["title"] = n.title;
        j["x"] = n.x;
        j["y"] = n.y;
        j["params"] = n.params;
        j["text"] = n.text;
        nlohmann::json weights = nlohmann::json::array();
        for (const auto &w : n.weightedOutputs)
            weights.push_back({w.pinId, w.weight});
        j["weights"] = weights;
        state["n" + std::to_string(n.id)] = j;
    }
    nlohmann::json links = nlohmann::json::array();
    for (const auto &l : asset_.links)
        links.push_back({l.id, l.start, l.end});
    state["links"] = links;
    return state;
}

void BehaviorTreeEditor::RestoreUndoState(const nlohmann::json &state)
{
    for (auto it = state.begin(); it != state.end(); ++it)
    {
        const std::string &key = it.key();
        if (key == "links")
        {
            asset_.links.clear();
            if (it->is_array())
            {
                for (const auto &l : *it)
                    asset_.links.push_back({l[0].get<int>(), l[1].get<int>(), l[2].get<int>()});
            }
            continue;
        }
        if (key.empty() || key[0] != 'n')
            continue;
        const int id = std::stoi(key.substr(1));
        if (it->is_null())
        {
            std::erase_if(asset_.nodes, [id](const BTNodeData &n) { return n.id == id; });
            continue;
        }
        BTNodeData node;
        node.id = id;
        node.type = (*it)["type"].get<int>();
        node.title = (*it)["title"].get<std::string>();
        node.x = (*it)["x"].get<float>();
        node.y = (*it)["y"].get<float>();
        node.params = (*it)["params"].get<std::array<float, 5>>();
        node.text = (*it)["text"].get<std::string>();
        for (const auto &w : (*it)["weights"])
            node.weightedOutputs.push_back({w[0].get<int>(), w[1].get<float>()});
        if (BTNodeData *existing = asset_.FindNode(id))
            *existing = std::move(node);
        else
            asset_.nodes.push_back(std::move(node));
    }
    // 番号は戻さない（消したノードのIDを使い回さない）
    nextNodeId_ = std::max(nextNodeId_, asset_.NextNodeId());
    nextLinkId_ = std::max(nextLinkId_, asset_.NextLinkId());
    nextPinId_ = std::max(nextPinId_, asset_.NextExtraPinId());
    positionsPending_ = true;
    modified_ = true;
    ResetUndoBaseline();
}

void BehaviorTreeEditor::CommitUndo()
{
    // つまんでいる・打っている最中は待ち、手を離したフレームで1回分として積む
    if (ImGui::IsAnyItemActive())
        return;
    nlohmann::json now = CaptureUndoState();
    if (now == undoBaseline_)
    {
        undoLabel_.clear();
        return;
    }
    auto [before, after] = MakeTopLevelJsonDiff(undoBaseline_, now);
    const std::string label = undoLabel_.empty() ? "ビヘイビアツリーを編集" : undoLabel_;
    std::weak_ptr<BehaviorTreeEditor *> alive = alive_;
    UndoRedoManager::GetInstance()->Push(std::make_unique<JsonStateCommand>(
        "BT: " + label, std::move(before), std::move(after), [alive](const nlohmann::json &s) {
            if (auto self = alive.lock(); self && *self)
                (*self)->RestoreUndoState(s);
        }));
    undoBaseline_ = std::move(now);
    undoLabel_.clear();
    modified_ = true;
}

void BehaviorTreeEditor::ResetUndoBaseline()
{
    undoBaseline_ = CaptureUndoState();
    undoLabel_.clear();
}

} // namespace Hagine

#endif // USE_IMGUI
