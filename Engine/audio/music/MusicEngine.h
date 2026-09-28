#pragma once
#include "MusicEffects.h"
#include "MusicTrack.h"
#include "MusicTypes.h"
#include "WavFile.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <xaudio2.h>

namespace Hagine {

/// <summary>
/// アルペジエータ（押さえた和音を1音ずつ順番に鳴らす）の設定。
/// 和音を押さえるだけで伴奏らしい動きが作れるので、
/// 鍵盤に慣れていなくても形になる
/// </summary>
struct ArpeggiatorParams
{
    bool enabled = false;   // 有効か
    int mode = 0;           // 0=上へ 1=下へ 2=上下 3=ばらばら
    int rateDivision = 3;   // 0=4分 1=8分 2=3連8分 3=16分 4=32分
    int octaveRange = 1;    // 何オクターブぶん繰り返すか (1〜3)
    float gateRatio = 0.7f; // 1音の長さの割合 (0.1〜1.0)
};

/// <summary>
/// 曲を組み立てて鳴らすためのサブシステム。
///
/// 既存の Audio が「できあがった .wav を再生する」役なのに対し、こちらは
/// 「音そのものをその場で作る」役。XAudio2 のソースボイスへ自前で生成した
/// 波形を流し込み、シンセ・サンプラー・シーケンサをまとめて動かす。
///
/// 音の生成は専用スレッド（ミキサースレッド）で行う。ゲームのフレームレートが
/// 落ちても音が途切れないようにするため、描画スレッドとは切り離してある。
/// トラックやノートを書き換えるときは必ず ScopedLock を取ること。
/// </summary>
class MusicEngine
{
  private:
    /// <summary>
    /// 再生待ち行列が1つ空いたことをミキサースレッドへ知らせるだけのコールバック。
    /// オーディオスレッド上で呼ばれるので、ここでは重い処理を一切しない
    /// </summary>
    class StreamCallback : public IXAudio2VoiceCallback
    {
      public:
        HANDLE bufferEndEvent = nullptr;

        void STDMETHODCALLTYPE OnStreamEnd() override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
        void STDMETHODCALLTYPE OnBufferStart(void *) override {}
        void STDMETHODCALLTYPE OnLoopEnd(void *) override {}
        void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT) override {}
        void STDMETHODCALLTYPE OnBufferEnd(void *) override
        {
            if (bufferEndEvent)
                SetEvent(bufferEndEvent);
        }
    };

    MusicEngine() = default;
    ~MusicEngine() = default;
    MusicEngine(const MusicEngine &) = delete;
    MusicEngine &operator=(const MusicEngine &) = delete;

  public:
    /// <summary>
    /// トラックやノートを書き換えるあいだ、ミキサーを待たせるためのスコープロック
    /// </summary>
    class ScopedLock
    {
      public:
        ScopedLock();
        ~ScopedLock();
        ScopedLock(const ScopedLock &) = delete;
        ScopedLock &operator=(const ScopedLock &) = delete;
    };

    /// ====================================
    /// public method
    /// ====================================

    /// <summary>シングルトンインスタンスの取得</summary>
    static MusicEngine *GetInstance();

    /// <summary>
    /// 初期化する。実際の音声出力は最初に音を鳴らすときまで始めない
    /// </summary>
    /// <param name="xAudio2">Audio が持っている XAudio2 本体を借りる</param>
    void Initialize(IXAudio2 *xAudio2);

    /// <summary>
    /// 音声出力を開始する（未開始なら）。再生・発音の入口から自動で呼ばれる
    /// </summary>
    void EnsurePlaybackStarted();

    /// <summary>終了処理。Audio::Finalize より前に呼ぶこと</summary>
    void Finalize();

    /// <summary>音声出力が動いているか</summary>
    /// <returns>bool: 動いていれば true</returns>
    bool IsPlaybackRunning() const { return threadRunning_.load(); }

    // --- トラック ---------------------------------------------------------

    /// <summary>
    /// トラックを追加する
    /// </summary>
    /// <param name="name">トラック名</param>
    /// <param name="kind">楽器の種類</param>
    /// <returns>int: 追加したトラックの番号</returns>
    int AddTrack(const std::string &name, InstrumentKind kind);

    /// <summary>
    /// トラックを削除する
    /// </summary>
    /// <param name="index">削除するトラック番号</param>
    void RemoveTrack(int index);

    /// <summary>トラック数</summary>
    /// <returns>int: トラック数</returns>
    int GetTrackCount() const { return static_cast<int>(tracks_.size()); }

    /// <summary>
    /// トラックを取得する
    /// </summary>
    /// <param name="index">トラック番号</param>
    /// <returns>MusicTrack*: 見つからなければ nullptr</returns>
    MusicTrack *GetTrack(int index);

    // --- 演奏（鍵盤やパッドを直接叩く） ------------------------------------

    /// <summary>
    /// 音を鳴らし始める。録音中なら同時にノートの記録も始まる
    /// </summary>
    /// <param name="trackIndex">対象トラック</param>
    /// <param name="note">ノート番号</param>
    /// <param name="velocity">強さ (0〜1)</param>
    void NoteOn(int trackIndex, int note, float velocity);

    /// <summary>
    /// 音を止める
    /// </summary>
    /// <param name="trackIndex">対象トラック</param>
    /// <param name="note">ノート番号</param>
    void NoteOff(int trackIndex, int note);

    /// <summary>鳴っている音をすべて止める</summary>
    void AllNotesOff();

    // --- シーケンサ -------------------------------------------------------

    /// <summary>現在位置から再生を始める</summary>
    void Play();

    /// <summary>再生を止めて先頭（ループ開始位置）へ戻す</summary>
    void Stop();

    /// <summary>その場で再生を止める</summary>
    void Pause();

    /// <summary>再生中かどうか</summary>
    bool IsPlaying() const { return playing_.load(); }

    /// <summary>テンポを設定する</summary>
    /// <param name="bpm">1分あたりの拍数 (20〜300)</param>
    void SetTempo(float bpm);
    float GetTempo() const { return tempo_; }

    /// <summary>1小節あたりの拍数</summary>
    void SetBeatsPerBar(int beats);
    int GetBeatsPerBar() const { return beatsPerBar_; }

    /// <summary>再生位置（ティック）</summary>
    double GetPositionTicks() const { return positionTicks_.load(); }
    void SetPositionTicks(double ticks);

    /// <summary>ループの有効・無効</summary>
    void SetLoopEnabled(bool enabled);
    bool IsLoopEnabled() const { return loopEnabled_; }

    /// <summary>
    /// ループ範囲を設定する
    /// </summary>
    /// <param name="startTick">開始位置</param>
    /// <param name="endTick">終了位置</param>
    void SetLoopRange(int startTick, int endTick);
    int GetLoopStartTick() const { return loopStartTick_; }
    int GetLoopEndTick() const { return loopEndTick_; }

    /// <summary>曲の終端（一番後ろのノートが終わる位置）</summary>
    /// <returns>int: ティック</returns>
    int GetSongEndTick() const;

    /// <summary>メトロノームの有効・無効</summary>
    void SetMetronomeEnabled(bool enabled) { metronomeEnabled_ = enabled; }
    bool IsMetronomeEnabled() const { return metronomeEnabled_; }

    /// <summary>録音（弾いた音をノートとして書き込む）の有効・無効</summary>
    void SetRecording(bool recording);
    bool IsRecording() const { return recording_; }

    /// <summary>録音時に位置をそろえる単位[ティック]。0 ならそろえない</summary>
    void SetRecordQuantize(int ticks) { recordQuantize_ = ticks; }
    int GetRecordQuantize() const { return recordQuantize_; }

    /// <summary>録音先のトラック</summary>
    void SetRecordTrack(int trackIndex) { recordTrack_ = trackIndex; }
    int GetRecordTrack() const { return recordTrack_; }

    // --- ミキサー ---------------------------------------------------------

    /// <summary>全体音量</summary>
    void SetMasterVolume(float volume);
    float GetMasterVolume() const { return masterVolume_; }

    /// <summary>
    /// 直近ブロックの出力レベル（メーター表示用）
    /// </summary>
    /// <param name="channel">0=左, 1=右</param>
    /// <returns>float: 振幅 (0〜1)</returns>
    float GetMeterLevel(int channel) const;

    /// <summary>ミキサーの負荷（1.0 で音が途切れ始める）</summary>
    /// <returns>float: 負荷率</returns>
    float GetCpuLoad() const { return cpuLoad_.load(); }

    /// <summary>
    /// トラックの音量をなめらかに変える。
    /// 戦況に応じてパートを増減させる「インタラクティブBGM」を作るための入口
    /// </summary>
    /// <param name="trackIndex">対象トラック</param>
    /// <param name="targetVolume">行き先の音量</param>
    /// <param name="seconds">かける時間[秒]。0 なら即座に反映</param>
    void FadeTrackVolume(int trackIndex, float targetVolume, float seconds);

    // --- エフェクト -------------------------------------------------------

    /// <summary>やまびこ（ディレイ）の設定への参照</summary>
    DelayParams &GetDelayParams() { return delayParams_; }
    const DelayParams &GetDelayParams() const { return delayParams_; }

    /// <summary>残響（リバーブ）の設定への参照</summary>
    ReverbParams &GetReverbParams() { return reverbParams_; }
    const ReverbParams &GetReverbParams() const { return reverbParams_; }

    /// <summary>アルペジエータの設定への参照</summary>
    ArpeggiatorParams &GetArpeggiatorParams() { return arpParams_; }
    const ArpeggiatorParams &GetArpeggiatorParams() const { return arpParams_; }

    /// <summary>アルペジエータで今押さえている音の数（表示用）</summary>
    /// <returns>int: 押さえている数</returns>
    int GetArpeggiatorHeldCount() const;

    // --- プロジェクト -----------------------------------------------------

    /// <summary>作りかけの内容をすべて捨て、初期構成（ピアノ/ベース/ドラム）に戻す</summary>
    void NewProject();

    /// <summary>
    /// プロジェクトを JSON へ保存する
    /// </summary>
    /// <param name="path">保存先パス</param>
    /// <param name="outError">失敗理由の格納先（不要なら nullptr）</param>
    /// <returns>bool: 成功したら true</returns>
    bool SaveProject(const std::string &path, std::string *outError = nullptr);

    /// <summary>
    /// プロジェクトを JSON から読み込む
    /// </summary>
    /// <param name="path">読み込むパス</param>
    /// <param name="outError">失敗理由の格納先（不要なら nullptr）</param>
    /// <returns>bool: 成功したら true</returns>
    bool LoadProject(const std::string &path, std::string *outError = nullptr);

    /// <summary>
    /// 曲を丸ごと .wav へ書き出す（実時間を待たずに一気に生成する）
    /// </summary>
    /// <param name="path">出力先パス</param>
    /// <param name="tailSeconds">最後のノートのあとに残す余韻の長さ[秒]</param>
    /// <param name="outError">失敗理由の格納先（不要なら nullptr）</param>
    /// <returns>bool: 成功したら true</returns>
    bool ExportWav(const std::string &path, float tailSeconds = 2.0f, std::string *outError = nullptr);

    /// <summary>プロジェクト名</summary>
    const std::string &GetProjectName() const { return projectName_; }
    void SetProjectName(const std::string &name) { projectName_ = name; }

    /// <summary>編集用のミューテックス（ScopedLock が使う）</summary>
    /// <returns>std::recursive_mutex&amp;: ミキサーと共有するロック</returns>
    std::recursive_mutex &GetMixMutex() { return mixMutex_; }

  private:
    /// ====================================
    /// private structures
    /// ====================================

    /// ミキサースレッドへ渡す発音指示
    struct LiveEvent
    {
        int trackIndex = 0;
        int note = 60;
        float velocity = 1.0f;
        bool isNoteOn = true;
    };

    /// 1ブロック内で処理する予定の出来事
    struct ScheduledEvent
    {
        uint32_t frameOffset = 0;
        int type = 0; // 0=ノートオン, 1=ノートオフ, 2=メトロノーム
        int trackIndex = 0;
        int note = 60;
        float velocity = 1.0f;
    };

    /// シーケンサが鳴らし中のノート（終端で止めるために覚えておく）
    struct ActiveSequencedNote
    {
        int trackIndex = 0;
        int note = 60;
        int endTick = 0;
    };

    /// 録音中、まだ離されていない打鍵
    struct PendingRecord
    {
        int trackIndex = 0;
        int note = 60;
        float velocity = 1.0f;
        int startTick = 0;
    };

    /// ====================================
    /// private method
    /// ====================================

    /// ミキサースレッドの本体
    void RenderThreadMain();

    /// 1ブロックぶんの音を作る（ロックを取る）
    void RenderBlock(float *out, uint32_t frames);

    /// ループ境界を越えないひとかたまりを作る（ロック済みで呼ぶ）
    void RenderSegment(float *out, uint32_t frames);

    /// 実際に楽器を鳴らして混ぜる（ロック済みで呼ぶ）
    void MixInstruments(float *out, uint32_t frames);

    /// 鳴っている音をすべて止める（ロック済みで呼ぶ）
    void StopAllVoicesLocked();

    /// ループ終端に達したときの巻き戻し（ロック済みで呼ぶ）
    void WrapLoopLocked();

    /// 録音中の打鍵をノートとして確定する
    void CommitPendingRecord(int trackIndex, int note);

    /// 既定のトラック構成を作る（ロック済みで呼ぶ）
    void BuildDefaultTracksLocked();

    /// アルペジエータの時計を進め、区切りが来たら次の音へ移る（ロック済みで呼ぶ）
    void AdvanceArpeggiatorLocked(uint32_t frames);

    /// アルペジエータの次の1音を鳴らす（ロック済みで呼ぶ）
    void StepArpeggiatorLocked();

    /// アルペジエータが鳴らしている音を止める（ロック済みで呼ぶ）
    void StopArpeggiatorNoteLocked();

    /// アルペジエータの1音ぶんのティック数を返す
    double GetArpeggiatorStepTicks() const;

  private:
    /// ====================================
    /// private variables
    /// ====================================

    // --- 音声出力 ---
    IXAudio2 *pXAudio2_ = nullptr; // Audio から借りている。解放はしない
    IXAudio2SourceVoice *sourceVoice_ = nullptr;
    StreamCallback callback_;
    HANDLE bufferEndEvent_ = nullptr;
    std::thread renderThread_;
    std::atomic<bool> threadRunning_{false};
    std::atomic<bool> exporting_{false};
    std::vector<std::vector<float>> blocks_; // 再生待ち行列へ積むバッファ
    uint32_t writeBlockIndex_ = 0;

    // --- 曲データ ---
    mutable std::recursive_mutex mixMutex_;
    std::vector<std::unique_ptr<MusicTrack>> tracks_;
    std::string projectName_ = "NewSong";

    // --- 進行状態 ---
    std::atomic<bool> playing_{false};
    std::atomic<double> positionTicks_{0.0};
    float tempo_ = 120.0f;
    int beatsPerBar_ = 4;
    bool loopEnabled_ = true;
    int loopStartTick_ = 0;
    int loopEndTick_ = MusicConst::kTicksPerBeat * 16; // 4小節
    bool metronomeEnabled_ = false;
    float masterVolume_ = 0.8f;

    // --- 録音 ---
    bool recording_ = false;
    int recordQuantize_ = MusicConst::kTicksPerBeat / 4; // 16分音符
    int recordTrack_ = 0;
    std::vector<PendingRecord> pendingRecords_;

    // --- ミキサー内部 ---
    std::vector<LiveEvent> liveEvents_;
    std::vector<ScheduledEvent> eventScratch_;
    std::vector<ActiveSequencedNote> activeSequencedNotes_;
    std::vector<float> trackScratch_;
    float metronomeAmp_ = 0.0f;
    float metronomePhase_ = 0.0f;
    float metronomeFrequency_ = 1600.0f;
    double lastBeatBoundary_ = -1.0;

    // --- エフェクト（ミックス全体に掛ける） ---
    MusicDelay delay_;
    MusicReverb reverb_;
    DelayParams delayParams_;
    ReverbParams reverbParams_;

    // --- アルペジエータ ---
    // 対象は「今の録音先トラック」1本だけに絞ってある。
    // 全トラックぶん持たせても操作が複雑になるだけで、実際に使うのは弾いているトラックなので
    ArpeggiatorParams arpParams_;
    std::vector<int> arpHeldNotes_;   // 押さえている音（昇順）
    float arpHeldVelocity_ = 0.8f;    // 最後に押さえた強さ
    int arpTrack_ = 0;                // 鳴らす先のトラック
    int arpStep_ = 0;                 // 何番目まで進んだか
    int arpDirection_ = 1;            // 上下モードでの向き
    int arpSoundingNote_ = -1;        // 今鳴らしている音（-1 = なし）
    double arpPhaseTicks_ = 0.0;      // 1音ぶんの区切りまでの進み

    std::atomic<float> meterLeft_{0.0f};
    std::atomic<float> meterRight_{0.0f};
    std::atomic<float> cpuLoad_{0.0f};
};

} // namespace Hagine
