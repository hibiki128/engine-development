#include "TimelineManager.h"

#include <Audio.h>
#include <asset/AssetPath.h>
#include <camera/Camera.h>
#include <camera/CameraManager.h>
#include <debug/log/Logger.h>
#include <object/base/BaseObject.h>
#include <object/base/BaseObjectManager.h>
#include <particle/gpu/ParticleCSEmitter.h>
#include <particle/gpu/ParticleCSSpawner.h>

#include <algorithm>
#include <filesystem>

namespace Hagine {
namespace {

/// 演出 JSON の置き場
std::string SequenceDirectory()
{
    return AssetPath::Json("timelines");
}

/// 名前から実際のパスを作る
std::string SequencePath(const std::string &name)
{
    return SequenceDirectory() + "/" + name + ".json";
}

} // namespace

TimelineManager *TimelineManager::GetInstance()
{
    static TimelineManager instance;
    return &instance;
}

void TimelineManager::Initialize()
{
    RescanSequences();
}

void TimelineManager::Finalize()
{
    playing_ = false;
    sequence_.Clear();
    sequenceNames_.clear();
    eventCallback_ = nullptr;
}

void TimelineManager::RescanSequences()
{
    sequenceNames_.clear();
    std::error_code ec;
    const std::string directory = SequenceDirectory();
    if (!std::filesystem::exists(directory, ec))
        return;
    for (const auto &entry : std::filesystem::directory_iterator(directory, ec))
    {
        if (ec)
            break;
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            sequenceNames_.push_back(entry.path().stem().string());
    }
    std::sort(sequenceNames_.begin(), sequenceNames_.end());
}

bool TimelineManager::LoadSequence(const std::string &name, std::string *outError)
{
    playing_ = false;
    if (!sequence_.Load(SequencePath(name), outError))
        return false;
    sequence_.SetName(name);
    time_ = 0.0f;
    previousTime_ = 0.0f;
    return true;
}

bool TimelineManager::SaveSequence(std::string *outError)
{
    if (!sequence_.Save(SequencePath(sequence_.GetName()), outError))
        return false;
    RescanSequences();
    return true;
}

bool TimelineManager::Play(const std::string &name)
{
    std::string error;
    if (!LoadSequence(name, &error))
    {
        Logger::Error("タイムラインを読み込めませんでした: " + error);
        return false;
    }
    PlayCurrent();
    return true;
}

void TimelineManager::PlayCurrent()
{
    time_ = 0.0f;
    previousTime_ = -0.0001f; // 0秒ちょうどのイベントも取りこぼさないよう、わずかに手前から始める
    playing_ = true;
    if (autoActivateCamera_)
        ActivateCameraTracks();
    Apply(previousTime_, time_, true);
    previousTime_ = time_;
}

void TimelineManager::Stop()
{
    playing_ = false;
    time_ = 0.0f;
    previousTime_ = 0.0f;
}

void TimelineManager::Pause()
{
    playing_ = false;
}

void TimelineManager::SetTime(float seconds, bool fireEvents)
{
    const float clamped = std::clamp(seconds, 0.0f, sequence_.GetDuration());
    Apply(previousTime_, clamped, fireEvents);
    time_ = clamped;
    previousTime_ = clamped;
}

void TimelineManager::Update(float deltaTime)
{
    if (!playing_)
        return;

    previousTime_ = time_;
    time_ += deltaTime;

    const float duration = sequence_.GetDuration();
    if (time_ >= duration)
    {
        if (sequence_.IsLooping())
        {
            // 終端までは普通に反映してから、先頭へ巻き戻す
            Apply(previousTime_, duration, true);
            time_ -= duration;
            previousTime_ = -0.0001f;
        }
        else
        {
            time_ = duration;
            Apply(previousTime_, time_, true);
            previousTime_ = time_;
            playing_ = false;
            return;
        }
    }

    Apply(previousTime_, time_, true);
    previousTime_ = time_;
}

void TimelineManager::ActivateCameraTracks()
{
    for (const TimelineTrack &track : sequence_.GetTracks())
    {
        if (!track.enabled || track.type != static_cast<int>(TimelineTrackType::Camera))
            continue;
        if (track.targetName.empty())
            continue;
        if (Camera *camera = CameraManager::GetInstance()->Find(track.targetName))
        {
            CameraManager::GetInstance()->SetActive(camera);
            break; // 複数あっても最初の1本だけを使う
        }
    }
}

void TimelineManager::Apply(float previousTime, float time, bool fireEvents)
{
    for (const TimelineTrack &track : sequence_.GetTracks())
    {
        if (!track.enabled)
            continue;

        switch (static_cast<TimelineTrackType>(track.type))
        {
        case TimelineTrackType::Transform:
            ApplyTransformTrack(track, time);
            break;
        case TimelineTrackType::Camera:
            ApplyCameraTrack(track, time);
            break;
        case TimelineTrackType::Sound:
        case TimelineTrackType::Particle:
        case TimelineTrackType::Event:
            if (fireEvents)
                FireEvents(track, previousTime, time);
            break;
        default:
            break;
        }
    }
}

void TimelineManager::ApplyTransformTrack(const TimelineTrack &track, float time)
{
    if (track.targetName.empty())
        return;
    BaseObject *object = BaseObjectManager::GetInstance()->GetObjectByName(track.targetName);
    if (object == nullptr)
        return;

    TimelineKey key;
    if (!TimelineSequence::EvaluateTrack(track, time, key))
        return;

    WorldTransform *transform = object->GetWorldTransform();
    if (transform == nullptr)
        return;

    // 回転はオイラー角で持っているので、クォータニオン運用の対象でもそちらへ切り替える
    transform->translation_ = key.position;
    transform->scale_ = key.scale;
    transform->isUseQuaternion_ = false;
    transform->eulerRotation_ = key.rotation;
}

void TimelineManager::ApplyCameraTrack(const TimelineTrack &track, float time)
{
    if (track.targetName.empty())
        return;
    Camera *camera = CameraManager::GetInstance()->Find(track.targetName);
    if (camera == nullptr)
        return;

    TimelineKey key;
    if (!TimelineSequence::EvaluateTrack(track, time, key))
        return;

    camera->SetPosition(key.position);
    camera->SetRotation(key.rotation);
    camera->SetFovYDegrees(key.fovDegrees);
}

void TimelineManager::FireEvents(const TimelineTrack &track, float previousTime, float time)
{
    // 巻き戻したときに同じイベントを何度も鳴らさないよう、前へ進んだときだけ見る
    if (time <= previousTime)
        return;

    for (const TimelineEvent &event : track.events)
    {
        if (event.time <= previousTime || event.time > time)
            continue;
        if (event.name.empty())
            continue;

        switch (static_cast<TimelineTrackType>(track.type))
        {
        case TimelineTrackType::Sound: {
            SoundPlayParams params;
            params.volume = event.value;
            params.bus = SoundBus::SE;
            Audio::GetInstance()->PlayOneShot(event.name, params);
            break;
        }
        case TimelineTrackType::Particle: {
            ParticleCSEmitter *emitter = ParticleCSSpawner::GetInstance()->Spawn(event.name);
            if (emitter == nullptr)
                break;
            Vector3 position = event.position;
            if (event.useTargetPosition && !track.targetName.empty())
            {
                if (BaseObject *object = BaseObjectManager::GetInstance()->GetObjectByName(track.targetName))
                {
                    if (const WorldTransform *transform = object->GetWorldTransform())
                        position = transform->translation_;
                }
            }
            emitter->SetTranslate(position);
            // 出しっぱなしにすると増え続けるので、鳴り終わったら自動で片付ける
            ParticleCSSpawner::GetInstance()->DespawnWhenFinished(emitter);
            break;
        }
        case TimelineTrackType::Event:
            if (eventCallback_)
                eventCallback_(event.name, event.value);
            break;
        default:
            break;
        }
    }
}

} // namespace Hagine
