#include "MusicEngine.h"
#include "DrumMachineInstrument.h"
#include "SamplerInstrument.h"
#include "SynthInstrument.h"

#include <debug/log/Logger.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace Hagine {
namespace {

constexpr float kPi = 3.14159265358979323846f;

/// 定位から左右の音量を求める
void PanGains(float pan, float &outLeft, float &outRight)
{
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    outLeft = std::cos(angle);
    outRight = std::sin(angle);
}

/// <summary>
/// 音量が 1 を超えたぶんをなだらかに丸める。
/// そのまま切り落とすと「バリッ」という耳障りな歪みになるため
/// </summary>
/// <param name="value">入力</param>
/// <returns>float: -1〜1 に収めた値</returns>
float SoftClip(float value)
{
    const float t = value * (2.0f / 3.0f);
    if (t >= 1.0f)
        return 1.0f;
    if (t <= -1.0f)
        return -1.0f;
    return 1.5f * (t - t * t * t / 3.0f);
}

/// 指定の単位へ丸める（録音位置のそろえに使う）
int SnapTick(int tick, int unit)
{
    if (unit <= 0)
        return tick;
    return ((tick + unit / 2) / unit) * unit;
}

} // namespace

MusicEngine::ScopedLock::ScopedLock()
{
    MusicEngine::GetInstance()->GetMixMutex().lock();
}

MusicEngine::ScopedLock::~ScopedLock()
{
    MusicEngine::GetInstance()->GetMixMutex().unlock();
}

MusicEngine *MusicEngine::GetInstance()
{
    static MusicEngine instance;
    return &instance;
}

void MusicEngine::Initialize(IXAudio2 *xAudio2)
{
    pXAudio2_ = xAudio2;

    // ミキサーが毎ブロック使う作業領域は、ここで確保しておく。
    // 音を作っている最中にメモリ確保が走ると、その瞬間だけ音が途切れる
    blocks_.assign(MusicConst::kBlockCount,
                   std::vector<float>(MusicConst::kBlockFrames * MusicConst::kChannels, 0.0f));
    trackScratch_.assign(MusicConst::kBlockFrames * MusicConst::kChannels, 0.0f);
    delay_.Initialize();
    reverb_.Initialize();
    eventScratch_.reserve(256);
    activeSequencedNotes_.reserve(256);
    liveEvents_.reserve(64);
    pendingRecords_.reserve(32);

    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    if (tracks_.empty())
        BuildDefaultTracksLocked();
}

void MusicEngine::EnsurePlaybackStarted()
{
    if (threadRunning_.load() || pXAudio2_ == nullptr)
        return;

    // 32bit float のステレオで受け取る。整数へ丸めるのは最後の書き出しだけでよい
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = static_cast<WORD>(MusicConst::kChannels);
    format.nSamplesPerSec = MusicConst::kSampleRate;
    format.wBitsPerSample = 32;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;

    bufferEndEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    callback_.bufferEndEvent = bufferEndEvent_;

    const HRESULT hr = pXAudio2_->CreateSourceVoice(&sourceVoice_, &format, 0,
                                                    XAUDIO2_DEFAULT_FREQ_RATIO, &callback_);
    if (FAILED(hr) || sourceVoice_ == nullptr)
    {
        Logger::Error("音楽機能のソースボイスを作成できませんでした。");
        if (bufferEndEvent_)
        {
            CloseHandle(bufferEndEvent_);
            bufferEndEvent_ = nullptr;
        }
        return;
    }

    sourceVoice_->Start(0);
    threadRunning_.store(true);
    writeBlockIndex_ = 0;
    renderThread_ = std::thread(&MusicEngine::RenderThreadMain, this);
    Logger::Info("Music engine playback started.");
}

void MusicEngine::Finalize()
{
    if (threadRunning_.load())
    {
        threadRunning_.store(false);
        if (bufferEndEvent_)
            SetEvent(bufferEndEvent_); // 待ちっぱなしのスレッドを起こす
        if (renderThread_.joinable())
            renderThread_.join();
    }

    if (sourceVoice_)
    {
        sourceVoice_->Stop(0);
        sourceVoice_->FlushSourceBuffers();
        sourceVoice_->DestroyVoice();
        sourceVoice_ = nullptr;
    }
    if (bufferEndEvent_)
    {
        CloseHandle(bufferEndEvent_);
        bufferEndEvent_ = nullptr;
        callback_.bufferEndEvent = nullptr;
    }

    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    tracks_.clear();
    activeSequencedNotes_.clear();
    liveEvents_.clear();
    pendingRecords_.clear();
    pXAudio2_ = nullptr;
}

void MusicEngine::RenderThreadMain()
{
    while (threadRunning_.load())
    {
        // 取りこぼしても止まらないよう、待ちには短いタイムアウトを入れておく
        WaitForSingleObject(bufferEndEvent_, 20);
        if (!threadRunning_.load())
            break;
        if (exporting_.load())
            continue;

        XAUDIO2_VOICE_STATE state = {};
        sourceVoice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);

        // 確保したブロック数より1つ少ない数までしか積まない。
        // 全部積んでしまうと、次に書き込む先がまだ再生中のバッファになる
        while (state.BuffersQueued < MusicConst::kBlockCount - 1 && threadRunning_.load())
        {
            float *block = blocks_[writeBlockIndex_].data();
            RenderBlock(block, MusicConst::kBlockFrames);

            XAUDIO2_BUFFER buffer = {};
            buffer.AudioBytes = MusicConst::kBlockFrames * MusicConst::kChannels * sizeof(float);
            buffer.pAudioData = reinterpret_cast<const BYTE *>(block);
            sourceVoice_->SubmitSourceBuffer(&buffer);

            writeBlockIndex_ = (writeBlockIndex_ + 1) % MusicConst::kBlockCount;
            ++state.BuffersQueued;
        }
    }
}

void MusicEngine::RenderBlock(float *out, uint32_t frames)
{
    const auto renderStart = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::recursive_mutex> lock(mixMutex_);

        std::fill(out, out + static_cast<size_t>(frames) * MusicConst::kChannels, 0.0f);

        // 鍵盤を叩いた指示はブロックの先頭でまとめて反映する
        for (const LiveEvent &event : liveEvents_)
        {
            if (event.trackIndex < 0 || event.trackIndex >= static_cast<int>(tracks_.size()))
                continue;
            Instrument *instrument = tracks_[event.trackIndex]->GetInstrument();
            if (event.isNoteOn)
                instrument->NoteOn(event.note, event.velocity);
            else
                instrument->NoteOff(event.note);
        }
        liveEvents_.clear();

        // アルペジエータは再生中かどうかに関わらず動かす。
        // 止めているときでも和音を押さえれば刻んでくれたほうが、音色作りがしやすい
        if (arpParams_.enabled)
            AdvanceArpeggiatorLocked(frames);

        // トラック音量のフェード（インタラクティブBGMのパート増減）
        const float blockSeconds = static_cast<float>(frames) / MusicConst::kSampleRate;
        for (auto &track : tracks_)
            track->UpdateVolumeFade(blockSeconds);

        // ループ境界をまたがないようブロックを切り分けて作る
        const double ticksPerFrame = playing_.load()
                                         ? (static_cast<double>(tempo_) / 60.0 *
                                            MusicConst::kTicksPerBeat / MusicConst::kSampleRate)
                                         : 0.0;
        uint32_t done = 0;
        while (done < frames)
        {
            uint32_t segment = frames - done;
            if (playing_.load() && loopEnabled_ && loopEndTick_ > loopStartTick_ && ticksPerFrame > 0.0)
            {
                if (positionTicks_.load() >= static_cast<double>(loopEndTick_))
                    WrapLoopLocked();
                const double remain = static_cast<double>(loopEndTick_) - positionTicks_.load();
                const uint32_t framesToLoop =
                    static_cast<uint32_t>(std::max(1.0, std::ceil(remain / ticksPerFrame)));
                segment = std::min(segment, framesToLoop);
            }

            RenderSegment(out + static_cast<size_t>(done) * MusicConst::kChannels, segment);
            done += segment;

            if (playing_.load() && loopEnabled_ && loopEndTick_ > loopStartTick_ &&
                positionTicks_.load() >= static_cast<double>(loopEndTick_))
            {
                WrapLoopLocked();
            }
        }

        // エフェクトはミックスが出そろってから、全体音量を掛ける前に通す。
        // 先に音量を掛けると、音量を下げたときに残響だけ相対的に大きく残ってしまう
        delay_.Process(out, frames, delayParams_, tempo_);
        reverb_.Process(out, frames, reverbParams_);

        // 全体音量とソフトクリップ、メーター
        float peakLeft = 0.0f;
        float peakRight = 0.0f;
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float left = SoftClip(out[i * 2 + 0] * masterVolume_);
            const float right = SoftClip(out[i * 2 + 1] * masterVolume_);
            out[i * 2 + 0] = left;
            out[i * 2 + 1] = right;
            peakLeft = std::max(peakLeft, std::fabs(left));
            peakRight = std::max(peakRight, std::fabs(right));
        }
        meterLeft_.store(peakLeft);
        meterRight_.store(peakRight);
    }

    const auto renderEnd = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(renderEnd - renderStart).count();
    const double budget = static_cast<double>(frames) / MusicConst::kSampleRate;
    // 急な跳ねで読みづらくならないよう、表示用に少しならす
    const float load = static_cast<float>(elapsed / budget);
    cpuLoad_.store(cpuLoad_.load() * 0.9f + load * 0.1f);
}

void MusicEngine::RenderSegment(float *out, uint32_t frames)
{
    const bool playing = playing_.load();
    const double startTick = positionTicks_.load();
    const double ticksPerFrame =
        playing ? (static_cast<double>(tempo_) / 60.0 * MusicConst::kTicksPerBeat / MusicConst::kSampleRate)
                : 0.0;
    const double endTick = startTick + ticksPerFrame * static_cast<double>(frames);

    eventScratch_.clear();

    if (playing && ticksPerFrame > 0.0)
    {
        auto toOffset = [&](double tick) {
            const double offset = (tick - startTick) / ticksPerFrame;
            return static_cast<uint32_t>(std::clamp(offset, 0.0, static_cast<double>(frames - 1)));
        };

        // 鳴り終わるノートを止める
        for (size_t i = 0; i < activeSequencedNotes_.size();)
        {
            const ActiveSequencedNote &active = activeSequencedNotes_[i];
            if (static_cast<double>(active.endTick) < endTick)
            {
                ScheduledEvent event;
                event.frameOffset = toOffset(static_cast<double>(active.endTick));
                event.type = 1;
                event.trackIndex = active.trackIndex;
                event.note = active.note;
                eventScratch_.push_back(event);
                activeSequencedNotes_.erase(activeSequencedNotes_.begin() + i);
                continue;
            }
            ++i;
        }

        // この区間で始まるノートを鳴らす
        for (size_t trackIndex = 0; trackIndex < tracks_.size(); ++trackIndex)
        {
            const MusicTrack &track = *tracks_[trackIndex];
            for (const MusicNote &note : track.GetNotes())
            {
                const double noteStart = static_cast<double>(note.startTick);
                if (noteStart < startTick || noteStart >= endTick)
                    continue;

                ScheduledEvent event;
                event.frameOffset = toOffset(noteStart);
                event.type = 0;
                event.trackIndex = static_cast<int>(trackIndex);
                event.note = note.note;
                event.velocity = note.velocity;
                eventScratch_.push_back(event);

                ActiveSequencedNote active;
                active.trackIndex = static_cast<int>(trackIndex);
                active.note = note.note;
                active.endTick = note.startTick + std::max(1, note.lengthTick);
                activeSequencedNotes_.push_back(active);
            }
        }

        // メトロノーム（拍のあたま。小節あたまだけ高い音にする）
        if (metronomeEnabled_)
        {
            const double beatTicks = static_cast<double>(MusicConst::kTicksPerBeat);
            long long beat = static_cast<long long>(std::ceil(startTick / beatTicks));
            while (static_cast<double>(beat) * beatTicks < endTick)
            {
                const double beatTick = static_cast<double>(beat) * beatTicks;
                if (beatTick >= startTick)
                {
                    ScheduledEvent event;
                    event.frameOffset = toOffset(beatTick);
                    event.type = 2;
                    event.note = (beatsPerBar_ > 0 && (beat % beatsPerBar_) == 0) ? 1 : 0;
                    eventScratch_.push_back(event);
                }
                ++beat;
            }
        }

        std::stable_sort(eventScratch_.begin(), eventScratch_.end(),
                         [](const ScheduledEvent &a, const ScheduledEvent &b) {
                             return a.frameOffset < b.frameOffset;
                         });
    }

    // 出来事のたびに区切って音を作る。こうすると発音位置がサンプル単位でそろう
    uint32_t cursor = 0;
    for (const ScheduledEvent &event : eventScratch_)
    {
        if (event.frameOffset > cursor)
        {
            MixInstruments(out + static_cast<size_t>(cursor) * MusicConst::kChannels,
                           event.frameOffset - cursor);
            cursor = event.frameOffset;
        }

        if (event.type == 2)
        {
            metronomeAmp_ = 1.0f;
            metronomePhase_ = 0.0f;
            metronomeFrequency_ = (event.note == 1) ? 2400.0f : 1600.0f;
            continue;
        }
        if (event.trackIndex < 0 || event.trackIndex >= static_cast<int>(tracks_.size()))
            continue;
        Instrument *instrument = tracks_[event.trackIndex]->GetInstrument();
        if (event.type == 0)
            instrument->NoteOn(event.note, event.velocity);
        else
            instrument->NoteOff(event.note);
    }
    if (cursor < frames)
    {
        MixInstruments(out + static_cast<size_t>(cursor) * MusicConst::kChannels, frames - cursor);
    }

    positionTicks_.store(endTick);
}

void MusicEngine::MixInstruments(float *out, uint32_t frames)
{
    if (frames == 0)
        return;

    bool anySolo = false;
    for (const auto &track : tracks_)
    {
        if (track->IsSoloed())
        {
            anySolo = true;
            break;
        }
    }

    for (const auto &track : tracks_)
    {
        Instrument *instrument = track->GetInstrument();
        if (instrument == nullptr)
            continue;

        std::fill(trackScratch_.begin(),
                  trackScratch_.begin() + static_cast<ptrdiff_t>(frames) * MusicConst::kChannels, 0.0f);
        instrument->Render(trackScratch_.data(), frames);

        // ミュート中でも楽器自体は動かしておく。そうしないと解除した瞬間に
        // 止まっていた音が復活して不自然になる
        const bool audible = !track->IsMuted() && (!anySolo || track->IsSoloed());
        if (!audible)
            continue;

        float panLeft = 0.0f;
        float panRight = 0.0f;
        PanGains(track->GetPan(), panLeft, panRight);
        const float volume = track->GetVolume();

        for (uint32_t i = 0; i < frames; ++i)
        {
            out[i * 2 + 0] += trackScratch_[i * 2 + 0] * volume * panLeft;
            out[i * 2 + 1] += trackScratch_[i * 2 + 1] * volume * panRight;
        }
    }

    if (metronomeAmp_ > 0.0005f)
    {
        const float decay = std::exp(-1.0f / (0.025f * static_cast<float>(MusicConst::kSampleRate)));
        for (uint32_t i = 0; i < frames; ++i)
        {
            const float value = std::sin(2.0f * kPi * metronomePhase_) * metronomeAmp_ * 0.35f;
            out[i * 2 + 0] += value;
            out[i * 2 + 1] += value;
            metronomePhase_ += metronomeFrequency_ / static_cast<float>(MusicConst::kSampleRate);
            if (metronomePhase_ >= 1.0f)
                metronomePhase_ -= 1.0f;
            metronomeAmp_ *= decay;
        }
    }
}

double MusicEngine::GetArpeggiatorStepTicks() const
{
    const double beat = static_cast<double>(MusicConst::kTicksPerBeat);
    switch (arpParams_.rateDivision)
    {
    case 0:
        return beat;              // 4分
    case 1:
        return beat * 0.5;        // 8分
    case 2:
        return beat / 3.0;        // 3連8分
    case 4:
        return beat * 0.125;      // 32分
    case 3:
    default:
        return beat * 0.25;       // 16分
    }
}

int MusicEngine::GetArpeggiatorHeldCount() const
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    return static_cast<int>(arpHeldNotes_.size());
}

void MusicEngine::StopArpeggiatorNoteLocked()
{
    if (arpSoundingNote_ < 0)
        return;
    if (arpTrack_ >= 0 && arpTrack_ < static_cast<int>(tracks_.size()))
        tracks_[arpTrack_]->GetInstrument()->NoteOff(arpSoundingNote_);
    arpSoundingNote_ = -1;
}

void MusicEngine::AdvanceArpeggiatorLocked(uint32_t frames)
{
    if (arpHeldNotes_.empty())
    {
        StopArpeggiatorNoteLocked();
        arpPhaseTicks_ = 0.0;
        arpStep_ = 0;
        return;
    }

    const double ticksPerFrame =
        static_cast<double>(tempo_) / 60.0 * MusicConst::kTicksPerBeat / MusicConst::kSampleRate;
    const double stepTicks = GetArpeggiatorStepTicks();

    arpPhaseTicks_ += ticksPerFrame * static_cast<double>(frames);

    // 区切りより手前で音を切ることで、粒立ちのある刻みになる
    const double gateTicks = stepTicks * std::clamp(arpParams_.gateRatio, 0.1f, 1.0f);
    if (arpSoundingNote_ >= 0 && arpPhaseTicks_ >= gateTicks)
        StopArpeggiatorNoteLocked();

    while (arpPhaseTicks_ >= stepTicks)
    {
        arpPhaseTicks_ -= stepTicks;
        StepArpeggiatorLocked();
    }
}

void MusicEngine::StepArpeggiatorLocked()
{
    StopArpeggiatorNoteLocked();
    if (arpHeldNotes_.empty())
        return;
    if (arpTrack_ < 0 || arpTrack_ >= static_cast<int>(tracks_.size()))
        return;

    // 押さえている音をオクターブぶん積み上げて、1本の並びにする
    const int octaves = std::clamp(arpParams_.octaveRange, 1, 3);
    const int heldCount = static_cast<int>(arpHeldNotes_.size());
    const int total = heldCount * octaves;
    if (total <= 0)
        return;

    int index = 0;
    switch (arpParams_.mode)
    {
    case 1: // 下へ
        index = (total - 1) - (arpStep_ % total);
        ++arpStep_;
        break;
    case 2: { // 上下（端で折り返す）
        index = std::clamp(arpStep_, 0, total - 1);
        arpStep_ += arpDirection_;
        if (arpStep_ >= total)
        {
            arpStep_ = std::max(0, total - 2);
            arpDirection_ = -1;
        }
        else if (arpStep_ < 0)
        {
            arpStep_ = std::min(1, total - 1);
            arpDirection_ = 1;
        }
        break;
    }
    case 3: { // ばらばら
        static thread_local uint32_t randomState = 0x7F4A7C15u;
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        index = static_cast<int>(randomState % static_cast<uint32_t>(total));
        break;
    }
    case 0:
    default: // 上へ
        index = arpStep_ % total;
        ++arpStep_;
        break;
    }

    const int note = arpHeldNotes_[index % heldCount] + 12 * (index / heldCount);
    if (note < MusicConst::kLowestNote || note > MusicConst::kHighestNote)
        return;

    tracks_[arpTrack_]->GetInstrument()->NoteOn(note, arpHeldVelocity_);
    arpSoundingNote_ = note;
}

void MusicEngine::FadeTrackVolume(int trackIndex, float targetVolume, float seconds)
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks_.size()))
        return;
    tracks_[trackIndex]->StartVolumeFade(targetVolume, seconds);
}

void MusicEngine::StopAllVoicesLocked()
{
    for (auto &track : tracks_)
    {
        if (track->GetInstrument())
            track->GetInstrument()->AllNotesOff();
    }
    activeSequencedNotes_.clear();
    liveEvents_.clear();

    // アルペジエータの押さえも解除しないと、止めたのに刻み続ける
    arpHeldNotes_.clear();
    arpSoundingNote_ = -1;
    arpPhaseTicks_ = 0.0;
    arpStep_ = 0;

    // エフェクトの中に残っている響きも消す（止めた後に尾を引かないように）
    delay_.Reset();
    reverb_.Reset();
}

void MusicEngine::WrapLoopLocked()
{
    // ループの継ぎ目でノートが鳴りっぱなしにならないよう、一度すべて止める
    for (const ActiveSequencedNote &active : activeSequencedNotes_)
    {
        if (active.trackIndex >= 0 && active.trackIndex < static_cast<int>(tracks_.size()))
            tracks_[active.trackIndex]->GetInstrument()->NoteOff(active.note);
    }
    activeSequencedNotes_.clear();
    positionTicks_.store(static_cast<double>(loopStartTick_));
}

void MusicEngine::BuildDefaultTracksLocked()
{
    tracks_.clear();

    auto piano = std::make_unique<MusicTrack>("ピアノ", InstrumentKind::Synth);
    static_cast<SynthInstrument *>(piano->GetInstrument())->ApplyPreset(0);
    piano->SetColor(0.45f, 0.60f, 0.78f);
    tracks_.push_back(std::move(piano));

    auto bass = std::make_unique<MusicTrack>("ベース", InstrumentKind::Synth);
    static_cast<SynthInstrument *>(bass->GetInstrument())->ApplyPreset(2);
    bass->SetColor(0.62f, 0.50f, 0.74f);
    tracks_.push_back(std::move(bass));

    auto drum = std::make_unique<MusicTrack>("ドラム", InstrumentKind::DrumMachine);
    drum->SetColor(0.82f, 0.58f, 0.36f);
    tracks_.push_back(std::move(drum));
}

int MusicEngine::AddTrack(const std::string &name, InstrumentKind kind)
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    tracks_.push_back(std::make_unique<MusicTrack>(name, kind));

    // 追加順に色を変えて、ピアノロールで見分けられるようにする
    static const float kPalette[][3] = {
        {0.45f, 0.60f, 0.78f}, {0.62f, 0.50f, 0.74f}, {0.82f, 0.58f, 0.36f},
        {0.45f, 0.68f, 0.52f}, {0.42f, 0.66f, 0.68f}, {0.80f, 0.72f, 0.42f},
        {0.80f, 0.46f, 0.46f}};
    const size_t index = (tracks_.size() - 1) % std::size(kPalette);
    tracks_.back()->SetColor(kPalette[index][0], kPalette[index][1], kPalette[index][2]);

    return static_cast<int>(tracks_.size()) - 1;
}

void MusicEngine::RemoveTrack(int index)
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    if (index < 0 || index >= static_cast<int>(tracks_.size()))
        return;

    // トラック番号を覚えている側は、ずれると別のトラックを指してしまう。
    // 消すときは鳴っている音も予約も一度まっさらにする
    StopAllVoicesLocked();
    pendingRecords_.clear();
    tracks_.erase(tracks_.begin() + index);
    if (recordTrack_ >= static_cast<int>(tracks_.size()))
        recordTrack_ = std::max(0, static_cast<int>(tracks_.size()) - 1);
}

MusicTrack *MusicEngine::GetTrack(int index)
{
    if (index < 0 || index >= static_cast<int>(tracks_.size()))
        return nullptr;
    return tracks_[index].get();
}

void MusicEngine::NoteOn(int trackIndex, int note, float velocity)
{
    EnsurePlaybackStarted();

    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks_.size()))
        return;

    if (arpParams_.enabled)
    {
        // アルペジエータが有効なときは、押さえた音をそのまま鳴らさず並びへ加える。
        // 実際の発音はミキサースレッド側が拍に合わせて行う
        arpTrack_ = trackIndex;
        arpHeldVelocity_ = velocity;
        if (std::find(arpHeldNotes_.begin(), arpHeldNotes_.end(), note) == arpHeldNotes_.end())
        {
            arpHeldNotes_.push_back(note);
            std::sort(arpHeldNotes_.begin(), arpHeldNotes_.end());
        }
    }
    else
    {
        LiveEvent event;
        event.trackIndex = trackIndex;
        event.note = note;
        event.velocity = velocity;
        event.isNoteOn = true;
        liveEvents_.push_back(event);
    }

    if (recording_ && playing_.load())
    {
        PendingRecord record;
        record.trackIndex = trackIndex;
        record.note = note;
        record.velocity = velocity;
        record.startTick = std::max(0, SnapTick(static_cast<int>(std::llround(positionTicks_.load())),
                                                recordQuantize_));
        pendingRecords_.push_back(record);
    }
}

void MusicEngine::NoteOff(int trackIndex, int note)
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks_.size()))
        return;

    // アルペジエータで押さえていた音なら、並びから外すだけでよい
    const auto held = std::find(arpHeldNotes_.begin(), arpHeldNotes_.end(), note);
    if (held != arpHeldNotes_.end())
    {
        arpHeldNotes_.erase(held);
    }
    else
    {
        LiveEvent event;
        event.trackIndex = trackIndex;
        event.note = note;
        event.velocity = 0.0f;
        event.isNoteOn = false;
        liveEvents_.push_back(event);
    }

    CommitPendingRecord(trackIndex, note);
}

void MusicEngine::CommitPendingRecord(int trackIndex, int note)
{
    for (size_t i = 0; i < pendingRecords_.size(); ++i)
    {
        const PendingRecord &record = pendingRecords_[i];
        if (record.trackIndex != trackIndex || record.note != note)
            continue;

        const int endTick = SnapTick(static_cast<int>(std::llround(positionTicks_.load())), recordQuantize_);
        const int minimum = (recordQuantize_ > 0) ? recordQuantize_ : (MusicConst::kTicksPerBeat / 4);
        MusicNote musicNote;
        musicNote.note = note;
        musicNote.startTick = record.startTick;
        musicNote.lengthTick = std::max(minimum, endTick - record.startTick);
        musicNote.velocity = record.velocity;

        if (trackIndex >= 0 && trackIndex < static_cast<int>(tracks_.size()))
            tracks_[trackIndex]->AddNote(musicNote);

        pendingRecords_.erase(pendingRecords_.begin() + i);
        return;
    }
}

void MusicEngine::AllNotesOff()
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    StopAllVoicesLocked();
    pendingRecords_.clear();
}

void MusicEngine::Play()
{
    EnsurePlaybackStarted();
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    activeSequencedNotes_.clear();
    playing_.store(true);
}

void MusicEngine::Stop()
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    playing_.store(false);
    StopAllVoicesLocked();
    pendingRecords_.clear();
    positionTicks_.store(loopEnabled_ ? static_cast<double>(loopStartTick_) : 0.0);
}

void MusicEngine::Pause()
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    playing_.store(false);
    StopAllVoicesLocked();
    pendingRecords_.clear();
}

void MusicEngine::SetTempo(float bpm)
{
    tempo_ = std::clamp(bpm, 20.0f, 300.0f);
}

void MusicEngine::SetBeatsPerBar(int beats)
{
    beatsPerBar_ = std::clamp(beats, 1, 16);
}

void MusicEngine::SetPositionTicks(double ticks)
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    StopAllVoicesLocked();
    positionTicks_.store(std::max(0.0, ticks));
}

void MusicEngine::SetLoopEnabled(bool enabled)
{
    loopEnabled_ = enabled;
}

void MusicEngine::SetLoopRange(int startTick, int endTick)
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    loopStartTick_ = std::max(0, startTick);
    loopEndTick_ = std::max(loopStartTick_ + MusicConst::kTicksPerBeat, endTick);
}

int MusicEngine::GetSongEndTick() const
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    int end = 0;
    for (const auto &track : tracks_)
        end = std::max(end, track->GetEndTick());
    return end;
}

void MusicEngine::SetRecording(bool recording)
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    recording_ = recording;
    if (!recording)
        pendingRecords_.clear();
}

void MusicEngine::SetMasterVolume(float volume)
{
    masterVolume_ = std::clamp(volume, 0.0f, 2.0f);
}

float MusicEngine::GetMeterLevel(int channel) const
{
    return (channel == 0) ? meterLeft_.load() : meterRight_.load();
}

void MusicEngine::NewProject()
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    playing_.store(false);
    StopAllVoicesLocked();
    pendingRecords_.clear();
    positionTicks_.store(0.0);
    projectName_ = "NewSong";
    tempo_ = 120.0f;
    beatsPerBar_ = 4;
    loopEnabled_ = true;
    loopStartTick_ = 0;
    loopEndTick_ = MusicConst::kTicksPerBeat * 16;
    delayParams_ = DelayParams{};
    reverbParams_ = ReverbParams{};
    arpParams_ = ArpeggiatorParams{};
    BuildDefaultTracksLocked();
}

bool MusicEngine::SaveProject(const std::string &path, std::string *outError)
{
    std::lock_guard<std::recursive_mutex> lock(mixMutex_);

    nlohmann::json root;
    root["name"] = projectName_;
    root["tempo"] = tempo_;
    root["beatsPerBar"] = beatsPerBar_;
    root["loopEnabled"] = loopEnabled_;
    root["loopStart"] = loopStartTick_;
    root["loopEnd"] = loopEndTick_;
    root["masterVolume"] = masterVolume_;

    nlohmann::json effects;
    SaveEffectsTo(effects, delayParams_, reverbParams_);
    root["effects"] = effects;

    root["arpeggiator"] = {{"enabled", arpParams_.enabled},
                           {"mode", arpParams_.mode},
                           {"rateDivision", arpParams_.rateDivision},
                           {"octaveRange", arpParams_.octaveRange},
                           {"gateRatio", arpParams_.gateRatio}};

    nlohmann::json trackArray = nlohmann::json::array();
    for (const auto &track : tracks_)
    {
        nlohmann::json item;
        item["name"] = track->GetName();
        item["kind"] = static_cast<int>(track->GetInstrument()->GetKind());
        item["volume"] = track->GetVolume();
        item["pan"] = track->GetPan();
        item["muted"] = track->IsMuted();
        item["soloed"] = track->IsSoloed();
        item["color"] = {track->GetColor()[0], track->GetColor()[1], track->GetColor()[2]};

        nlohmann::json instrumentJson;
        track->GetInstrument()->SaveTo(instrumentJson);
        item["instrument"] = instrumentJson;

        // ノートは数が多くなるので、4つ組の配列にして行数を抑える
        nlohmann::json noteArray = nlohmann::json::array();
        for (const MusicNote &note : track->GetNotes())
            noteArray.push_back({note.note, note.startTick, note.lengthTick, note.velocity});
        item["notes"] = noteArray;

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

bool MusicEngine::LoadProject(const std::string &path, std::string *outError)
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

    std::lock_guard<std::recursive_mutex> lock(mixMutex_);
    playing_.store(false);
    StopAllVoicesLocked();
    pendingRecords_.clear();
    positionTicks_.store(0.0);
    tracks_.clear();

    auto read = [&root](const char *key, auto &target) {
        if (root.contains(key))
            target = root.at(key).get<std::decay_t<decltype(target)>>();
    };
    read("name", projectName_);
    read("tempo", tempo_);
    read("beatsPerBar", beatsPerBar_);
    read("loopEnabled", loopEnabled_);
    read("loopStart", loopStartTick_);
    read("loopEnd", loopEndTick_);
    read("masterVolume", masterVolume_);

    if (root.contains("effects"))
        LoadEffectsFrom(root.at("effects"), delayParams_, reverbParams_);

    if (root.contains("arpeggiator"))
    {
        const auto &arp = root.at("arpeggiator");
        auto readArp = [&arp](const char *key, auto &target) {
            if (arp.contains(key))
                target = arp.at(key).get<std::decay_t<decltype(target)>>();
        };
        readArp("enabled", arpParams_.enabled);
        readArp("mode", arpParams_.mode);
        readArp("rateDivision", arpParams_.rateDivision);
        readArp("octaveRange", arpParams_.octaveRange);
        readArp("gateRatio", arpParams_.gateRatio);
    }

    if (root.contains("tracks") && root.at("tracks").is_array())
    {
        for (const auto &item : root.at("tracks"))
        {
            const std::string name = item.contains("name") ? item.at("name").get<std::string>() : "Track";
            const InstrumentKind kind = item.contains("kind")
                                            ? static_cast<InstrumentKind>(item.at("kind").get<int>())
                                            : InstrumentKind::Synth;
            auto track = std::make_unique<MusicTrack>(name, kind);

            if (item.contains("volume"))
                track->SetVolume(item.at("volume").get<float>());
            if (item.contains("pan"))
                track->SetPan(item.at("pan").get<float>());
            if (item.contains("muted"))
                track->SetMuted(item.at("muted").get<bool>());
            if (item.contains("soloed"))
                track->SetSoloed(item.at("soloed").get<bool>());
            if (item.contains("color") && item.at("color").is_array() && item.at("color").size() >= 3)
            {
                track->SetColor(item.at("color")[0].get<float>(),
                                item.at("color")[1].get<float>(),
                                item.at("color")[2].get<float>());
            }
            if (item.contains("instrument"))
                track->GetInstrument()->LoadFrom(item.at("instrument"));

            if (item.contains("notes") && item.at("notes").is_array())
            {
                for (const auto &noteJson : item.at("notes"))
                {
                    if (!noteJson.is_array() || noteJson.size() < 4)
                        continue;
                    MusicNote note;
                    note.note = noteJson[0].get<int>();
                    note.startTick = noteJson[1].get<int>();
                    note.lengthTick = noteJson[2].get<int>();
                    note.velocity = noteJson[3].get<float>();
                    track->AddNote(note);
                }
            }
            tracks_.push_back(std::move(track));
        }
    }

    if (tracks_.empty())
        BuildDefaultTracksLocked();
    return true;
}

bool MusicEngine::ExportWav(const std::string &path, float tailSeconds, std::string *outError)
{
    // 書き出しのあいだミキサースレッドを手前で止める。
    // ロック待ちにするとバッファを積めずに耳障りな音が出るため
    exporting_.store(true);
    if (sourceVoice_)
    {
        sourceVoice_->Stop(0);
        sourceVoice_->FlushSourceBuffers();
    }

    bool result = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mixMutex_);

        const bool savedPlaying = playing_.load();
        const bool savedLoop = loopEnabled_;
        const bool savedMetronome = metronomeEnabled_;
        const double savedPosition = positionTicks_.load();

        int endTick = 0;
        for (const auto &track : tracks_)
            endTick = std::max(endTick, track->GetEndTick());

        if (endTick <= 0)
        {
            if (outError)
                *outError = "書き出す内容がありません（ノートが1つもありません）";
        }
        else
        {
            // 曲の長さ + 余韻ぶんを一気に生成する
            loopEnabled_ = false;
            metronomeEnabled_ = false;
            StopAllVoicesLocked();
            positionTicks_.store(0.0);
            playing_.store(true);

            const double seconds = TicksToSeconds(static_cast<double>(endTick), tempo_) +
                                   static_cast<double>(std::max(0.0f, tailSeconds));
            const uint32_t totalFrames =
                static_cast<uint32_t>(seconds * static_cast<double>(MusicConst::kSampleRate));

            AudioClip clip;
            clip.channels = MusicConst::kChannels;
            clip.sampleRate = MusicConst::kSampleRate;
            clip.samples.assign(static_cast<size_t>(totalFrames) * MusicConst::kChannels, 0.0f);

            uint32_t done = 0;
            while (done < totalFrames)
            {
                const uint32_t count = std::min(MusicConst::kBlockFrames, totalFrames - done);
                RenderBlock(clip.samples.data() + static_cast<size_t>(done) * MusicConst::kChannels, count);
                done += count;
            }

            playing_.store(false);
            StopAllVoicesLocked();
            result = WavFile::Save(path, clip, 16, outError);
        }

        loopEnabled_ = savedLoop;
        metronomeEnabled_ = savedMetronome;
        positionTicks_.store(savedPosition);
        playing_.store(savedPlaying);
    }

    if (sourceVoice_)
        sourceVoice_->Start(0);
    exporting_.store(false);
    return result;
}

} // namespace Hagine
