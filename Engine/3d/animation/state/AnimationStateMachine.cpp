#include "AnimationStateMachine.h"
#include <algorithm>
#include <asset/AssetPath.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

namespace Hagine {

namespace {
using json = nlohmann::json;

std::filesystem::path MakePath(const std::string &file)
{
    return std::filesystem::path(AssetPath::JsonRoot()) / AnimationStateMachineAsset::kFolder / (file + ".json");
}

std::map<std::string, std::shared_ptr<AnimationStateMachineAsset>> &Cache()
{
    static std::map<std::string, std::shared_ptr<AnimationStateMachineAsset>> cache;
    return cache;
}
} // namespace

bool AnimationStateMachineAsset::Load(const std::string &file)
{
    std::ifstream in(MakePath(file));
    if (!in.is_open())
    {
        return false;
    }
    json root;
    try
    {
        in >> root;
    }
    catch (const json::exception &)
    {
        return false;
    }
    return FromJson(root);
}

bool AnimationStateMachineAsset::FromJson(const json &root)
{
    if (!root.is_object())
    {
        return false;
    }

    params.clear();
    states.clear();
    transitions.clear();
    entryState = root.value("entryState", 0);
    anyStateX = root.value("anyStateX", -300.0f);
    anyStateY = root.value("anyStateY", 0.0f);

    for (const json &p : root.value("params", json::array()))
    {
        AnimParamDef def;
        def.name = p.value("name", std::string());
        def.type = static_cast<AnimParamType>(p.value("type", 0));
        def.defaultValue = p.value("default", 0.0f);
        params.push_back(def);
    }
    for (const json &s : root.value("states", json::array()))
    {
        AnimStateData state;
        state.id = s.value("id", 0);
        state.name = s.value("name", std::string("State"));
        state.kind = static_cast<AnimStateKind>(s.value("kind", 0));
        state.file = s.value("file", std::string());
        state.loop = s.value("loop", true);
        state.speed = s.value("speed", 1.0f);
        state.blendMode = s.value("blendMode", 0);
        state.paramX = s.value("paramX", std::string());
        state.paramY = s.value("paramY", std::string());
        state.x = s.value("x", 0.0f);
        state.y = s.value("y", 0.0f);
        for (const json &bp : s.value("points", json::array()))
        {
            state.points.push_back({bp.value("file", std::string()), bp.value("x", 0.0f), bp.value("y", 0.0f), bp.value("speed", 1.0f)});
        }
        states.push_back(state);
    }
    for (const json &t : root.value("transitions", json::array()))
    {
        AnimTransitionData transition;
        transition.id = t.value("id", 0);
        transition.from = t.value("from", 0);
        transition.to = t.value("to", 0);
        transition.hasExitTime = t.value("hasExitTime", false);
        transition.exitTime = t.value("exitTime", 1.0f);
        transition.duration = t.value("duration", 0.25f);
        for (const json &c : t.value("conditions", json::array()))
        {
            transition.conditions.push_back({c.value("param", std::string()), static_cast<AnimConditionOp>(c.value("op", 0)), c.value("value", 0.0f)});
        }
        transitions.push_back(transition);
    }
    Touch();
    return true;
}

void AnimationStateMachineAsset::Save(const std::string &file) const
{
    const json root = ToJson();
    const std::filesystem::path path = MakePath(file);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path);
    if (out.is_open())
    {
        out << root.dump(4);
    }
}

json AnimationStateMachineAsset::ToJson() const
{
    json root;
    root["entryState"] = entryState;
    root["anyStateX"] = anyStateX;
    root["anyStateY"] = anyStateY;
    json paramsJson = json::array();
    for (const AnimParamDef &p : params)
    {
        paramsJson.push_back({{"name", p.name}, {"type", static_cast<int>(p.type)}, {"default", p.defaultValue}});
    }
    root["params"] = paramsJson;

    json statesJson = json::array();
    for (const AnimStateData &s : states)
    {
        json state = {{"id", s.id}, {"name", s.name}, {"kind", static_cast<int>(s.kind)}, {"file", s.file},
                      {"loop", s.loop}, {"speed", s.speed}, {"x", s.x}, {"y", s.y}};
        if (s.kind == AnimStateKind::BlendSpace)
        {
            state["blendMode"] = s.blendMode;
            state["paramX"] = s.paramX;
            state["paramY"] = s.paramY;
            json points = json::array();
            for (const AnimBlendPoint &p : s.points)
            {
                points.push_back({{"file", p.file}, {"x", p.x}, {"y", p.y}, {"speed", p.speed}});
            }
            state["points"] = points;
        }
        statesJson.push_back(state);
    }
    root["states"] = statesJson;

    json transitionsJson = json::array();
    for (const AnimTransitionData &t : transitions)
    {
        json conditions = json::array();
        for (const AnimCondition &c : t.conditions)
        {
            conditions.push_back({{"param", c.param}, {"op", static_cast<int>(c.op)}, {"value", c.value}});
        }
        transitionsJson.push_back({{"id", t.id}, {"from", t.from}, {"to", t.to}, {"hasExitTime", t.hasExitTime},
                                   {"exitTime", t.exitTime}, {"duration", t.duration}, {"conditions", conditions}});
    }
    root["transitions"] = transitionsJson;
    return root;
}

AnimStateData *AnimationStateMachineAsset::FindState(int id)
{
    for (AnimStateData &s : states)
    {
        if (s.id == id)
        {
            return &s;
        }
    }
    return nullptr;
}

const AnimStateData *AnimationStateMachineAsset::FindState(int id) const
{
    for (const AnimStateData &s : states)
    {
        if (s.id == id)
        {
            return &s;
        }
    }
    return nullptr;
}

const AnimParamDef *AnimationStateMachineAsset::FindParam(const std::string &name) const
{
    for (const AnimParamDef &p : params)
    {
        if (p.name == name)
        {
            return &p;
        }
    }
    return nullptr;
}

int AnimationStateMachineAsset::NextStateId() const
{
    int next = 1;
    for (const AnimStateData &s : states)
    {
        next = (std::max)(next, s.id + 1);
    }
    return next;
}

int AnimationStateMachineAsset::NextTransitionId() const
{
    int next = 1;
    for (const AnimTransitionData &t : transitions)
    {
        next = (std::max)(next, t.id + 1);
    }
    return next;
}

std::shared_ptr<AnimationStateMachineAsset> AnimationStateMachineLibrary::Get(const std::string &file)
{
    if (file.empty())
    {
        return nullptr;
    }
    auto it = Cache().find(file);
    if (it != Cache().end())
    {
        return it->second;
    }
    auto asset = std::make_shared<AnimationStateMachineAsset>();
    if (!asset->Load(file))
    {
        return nullptr;
    }
    Cache()[file] = asset;
    return asset;
}

void AnimationStateMachineLibrary::Put(const std::string &file, const std::shared_ptr<AnimationStateMachineAsset> &asset)
{
    Cache()[file] = asset;
}

std::vector<std::string> AnimationStateMachineLibrary::ListFiles()
{
    std::vector<std::string> files;
    std::error_code error;
    const std::filesystem::path folder = std::filesystem::path(AssetPath::JsonRoot()) / AnimationStateMachineAsset::kFolder;
    for (const auto &entry : std::filesystem::directory_iterator(folder, error))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            files.push_back(entry.path().stem().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace Hagine
