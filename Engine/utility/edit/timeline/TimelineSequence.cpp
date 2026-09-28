#include "TimelineSequence.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace Hagine {
namespace {

/// Vector3 を JSON へ
nlohmann::json ToJson(const Vector3 &value)
{
    return {value.x, value.y, value.z};
}

/// JSON を Vector3 へ（配列でなければ既定値のまま）
Vector3 ToVector3(const nlohmann::json &json, const Vector3 &fallback)
{
    if (!json.is_array() || json.size() < 3)
        return fallback;
    return Vector3(json[0].get<float>(), json[1].get<float>(), json[2].get<float>());
}

/// キーを時間順に並べ直す
void SortKeys(TimelineTrack &track)
{
    std::sort(track.keys.begin(), track.keys.end(),
              [](const TimelineKey &a, const TimelineKey &b) { return a.time < b.time; });
    std::sort(track.events.begin(), track.events.end(),
              [](const TimelineEvent &a, const TimelineEvent &b) { return a.time < b.time; });
}

} // namespace

int TimelineSequence::AddTrack(TimelineTrackType type, const std::string &name)
{
    TimelineTrack track;
    track.type = static_cast<int>(type);
    track.name = name;
    tracks_.push_back(track);
    return static_cast<int>(tracks_.size()) - 1;
}

void TimelineSequence::RemoveTrack(int index)
{
    if (index < 0 || index >= static_cast<int>(tracks_.size()))
        return;
    tracks_.erase(tracks_.begin() + index);
}

void TimelineSequence::Clear()
{
    tracks_.clear();
    duration_ = 5.0f;
    loop_ = false;
}

float TimelineSequence::FindLastTime() const
{
    float last = 0.0f;
    for (const TimelineTrack &track : tracks_)
    {
        for (const TimelineKey &key : track.keys)
            last = std::max(last, key.time);
        for (const TimelineEvent &event : track.events)
            last = std::max(last, event.time);
    }
    return last;
}

bool TimelineSequence::EvaluateTrack(const TimelineTrack &track, float time, TimelineKey &outKey)
{
    if (track.keys.empty())
        return false;

    // 先頭より前・末尾より後ろは、端のキーをそのまま使う
    if (time <= track.keys.front().time)
    {
        outKey = track.keys.front();
        return true;
    }
    if (time >= track.keys.back().time)
    {
        outKey = track.keys.back();
        return true;
    }

    // 挟んでいる2つのキーを探して、手前のキーが指定した繋ぎ方で補間する
    for (size_t i = 0; i + 1 < track.keys.size(); ++i)
    {
        const TimelineKey &from = track.keys[i];
        const TimelineKey &to = track.keys[i + 1];
        if (time < from.time || time > to.time)
            continue;

        const float span = to.time - from.time;
        const float elapsed = std::clamp(time - from.time, 0.0f, std::max(0.0001f, span));
        const EasingType easing = static_cast<EasingType>(from.easing);
        const float total = std::max(0.0001f, span);

        outKey = from;
        outKey.time = time;
        outKey.position = ApplyEasing(easing, from.position, to.position, elapsed, total);
        outKey.rotation = ApplyEasing(easing, from.rotation, to.rotation, elapsed, total);
        outKey.scale = ApplyEasing(easing, from.scale, to.scale, elapsed, total);
        outKey.fovDegrees = ApplyEasing(easing, from.fovDegrees, to.fovDegrees, elapsed, total);
        return true;
    }

    outKey = track.keys.back();
    return true;
}

bool TimelineSequence::Save(const std::string &path, std::string *outError) const
{
    nlohmann::json root;
    root["name"] = name_;
    root["duration"] = duration_;
    root["loop"] = loop_;

    nlohmann::json trackArray = nlohmann::json::array();
    for (const TimelineTrack &track : tracks_)
    {
        nlohmann::json item;
        item["type"] = track.type;
        item["name"] = track.name;
        item["targetName"] = track.targetName;
        item["enabled"] = track.enabled;

        nlohmann::json keyArray = nlohmann::json::array();
        for (const TimelineKey &key : track.keys)
        {
            keyArray.push_back({{"time", key.time},
                                {"position", ToJson(key.position)},
                                {"rotation", ToJson(key.rotation)},
                                {"scale", ToJson(key.scale)},
                                {"fov", key.fovDegrees},
                                {"easing", key.easing}});
        }
        item["keys"] = keyArray;

        nlohmann::json eventArray = nlohmann::json::array();
        for (const TimelineEvent &event : track.events)
        {
            eventArray.push_back({{"time", event.time},
                                  {"name", event.name},
                                  {"value", event.value},
                                  {"position", ToJson(event.position)},
                                  {"useTargetPosition", event.useTargetPosition}});
        }
        item["events"] = eventArray;

        trackArray.push_back(item);
    }
    root["tracks"] = trackArray;

    std::error_code ec;
    const std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path())
        std::filesystem::create_directories(fsPath.parent_path(), ec);

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open())
    {
        if (outError)
            *outError = "ファイルを作成できませんでした: " + path;
        return false;
    }
    file << root.dump(2);
    return true;
}

bool TimelineSequence::Load(const std::string &path, std::string *outError)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        if (outError)
            *outError = "ファイルを開けませんでした: " + path;
        return false;
    }

    nlohmann::json root;
    try
    {
        file >> root;
    }
    catch (const nlohmann::json::exception &e)
    {
        if (outError)
            *outError = std::string("JSON の読み込みに失敗しました: ") + e.what();
        return false;
    }

    Clear();
    if (root.contains("name"))
        name_ = root.at("name").get<std::string>();
    if (root.contains("duration"))
        duration_ = root.at("duration").get<float>();
    if (root.contains("loop"))
        loop_ = root.at("loop").get<bool>();

    if (!root.contains("tracks") || !root.at("tracks").is_array())
        return true;

    for (const auto &item : root.at("tracks"))
    {
        TimelineTrack track;
        if (item.contains("type"))
            track.type = item.at("type").get<int>();
        if (item.contains("name"))
            track.name = item.at("name").get<std::string>();
        if (item.contains("targetName"))
            track.targetName = item.at("targetName").get<std::string>();
        if (item.contains("enabled"))
            track.enabled = item.at("enabled").get<bool>();

        if (item.contains("keys") && item.at("keys").is_array())
        {
            for (const auto &keyJson : item.at("keys"))
            {
                TimelineKey key;
                if (keyJson.contains("time"))
                    key.time = keyJson.at("time").get<float>();
                if (keyJson.contains("position"))
                    key.position = ToVector3(keyJson.at("position"), key.position);
                if (keyJson.contains("rotation"))
                    key.rotation = ToVector3(keyJson.at("rotation"), key.rotation);
                if (keyJson.contains("scale"))
                    key.scale = ToVector3(keyJson.at("scale"), key.scale);
                if (keyJson.contains("fov"))
                    key.fovDegrees = keyJson.at("fov").get<float>();
                if (keyJson.contains("easing"))
                    key.easing = keyJson.at("easing").get<int>();
                track.keys.push_back(key);
            }
        }

        if (item.contains("events") && item.at("events").is_array())
        {
            for (const auto &eventJson : item.at("events"))
            {
                TimelineEvent event;
                if (eventJson.contains("time"))
                    event.time = eventJson.at("time").get<float>();
                if (eventJson.contains("name"))
                    event.name = eventJson.at("name").get<std::string>();
                if (eventJson.contains("value"))
                    event.value = eventJson.at("value").get<float>();
                if (eventJson.contains("position"))
                    event.position = ToVector3(eventJson.at("position"), event.position);
                if (eventJson.contains("useTargetPosition"))
                    event.useTargetPosition = eventJson.at("useTargetPosition").get<bool>();
                track.events.push_back(event);
            }
        }

        SortKeys(track);
        tracks_.push_back(std::move(track));
    }
    return true;
}

} // namespace Hagine
