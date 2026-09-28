#pragma once
#include "Instrument.h"
#include "MusicTypes.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// 曲の1パート。楽器1つと、その楽器が鳴らすノート列を持つ。
///
/// ノート列はミキサースレッドが再生中に読むため、書き換えは必ず
/// MusicEngine のロック下（MusicEngine::ScopedLock）で行うこと。
/// </summary>
class MusicTrack
{
  public:
    /// ====================================
    /// public method
    /// ====================================

    /// <param name="name">トラック名</param>
    /// <param name="kind">使用する楽器の種類</param>
    MusicTrack(const std::string &name, InstrumentKind kind)
        : name_(name), instrument_(Instrument::Create(kind))
    {
    }

    /// <summary>トラック名</summary>
    const std::string &GetName() const { return name_; }
    void SetName(const std::string &name) { name_ = name; }

    /// <summary>楽器本体</summary>
    /// <returns>Instrument*: 楽器。必ず非 nullptr</returns>
    Instrument *GetInstrument() const { return instrument_.get(); }

    /// <summary>
    /// 楽器を別の種類へ差し替える（音色は初期状態に戻る）
    /// </summary>
    /// <param name="kind">差し替え後の種類</param>
    void ReplaceInstrument(InstrumentKind kind)
    {
        instrument_->AllNotesOff();
        instrument_ = Instrument::Create(kind);
    }

    // --- ノート編集 -------------------------------------------------------

    /// <summary>ノート一覧（読み取り用）</summary>
    const std::vector<MusicNote> &GetNotes() const { return notes_; }

    /// <summary>ノート数</summary>
    int GetNoteCount() const { return static_cast<int>(notes_.size()); }

    /// <summary>
    /// ノートを追加する
    /// </summary>
    /// <param name="note">追加するノート（id は自動で振り直す）</param>
    /// <returns>uint32_t: 割り当てられた id</returns>
    uint32_t AddNote(MusicNote note)
    {
        note.id = ++noteIdCounter_;
        notes_.push_back(note);
        return note.id;
    }

    /// <summary>
    /// id を指定してノートを消す
    /// </summary>
    /// <param name="id">消すノートの id</param>
    void RemoveNote(uint32_t id)
    {
        for (size_t i = 0; i < notes_.size(); ++i)
        {
            if (notes_[i].id == id)
            {
                notes_.erase(notes_.begin() + i);
                return;
            }
        }
    }

    /// <summary>
    /// id からノートを探す
    /// </summary>
    /// <param name="id">探すノートの id</param>
    /// <returns>MusicNote*: 見つかったノート。無ければ nullptr</returns>
    MusicNote *FindNote(uint32_t id)
    {
        for (MusicNote &note : notes_)
        {
            if (note.id == id)
                return &note;
        }
        return nullptr;
    }

    /// <summary>ノートを全消しする</summary>
    void ClearNotes() { notes_.clear(); }

    /// <summary>
    /// このトラックのノートが終わる位置を返す
    /// </summary>
    /// <returns>int: 最後のノートの終端ティック</returns>
    int GetEndTick() const
    {
        int end = 0;
        for (const MusicNote &note : notes_)
            end = std::max(end, note.startTick + note.lengthTick);
        return end;
    }

    // --- ミキサー設定 -----------------------------------------------------

    float GetVolume() const { return volume_; }
    void SetVolume(float volume)
    {
        volume_ = volume;
        fadeTarget_ = volume;
        fadeSpeed_ = 0.0f;
    }

    /// <summary>
    /// 音量をなめらかに変える予約をする（インタラクティブBGMのパート増減用）
    /// </summary>
    /// <param name="targetVolume">行き先の音量</param>
    /// <param name="seconds">かける時間[秒]。0 以下なら即座に反映</param>
    void StartVolumeFade(float targetVolume, float seconds)
    {
        fadeTarget_ = std::max(0.0f, targetVolume);
        if (seconds <= 0.0f)
        {
            volume_ = fadeTarget_;
            fadeSpeed_ = 0.0f;
            return;
        }
        fadeSpeed_ = std::abs(fadeTarget_ - volume_) / seconds;
    }

    /// <summary>
    /// 予約された音量変化を進める。ミキサーが1ブロックごとに呼ぶ
    /// </summary>
    /// <param name="deltaSeconds">そのブロックの長さ[秒]</param>
    void UpdateVolumeFade(float deltaSeconds)
    {
        if (fadeSpeed_ <= 0.0f || volume_ == fadeTarget_)
            return;
        const float step = fadeSpeed_ * deltaSeconds;
        if (std::abs(fadeTarget_ - volume_) <= step)
        {
            volume_ = fadeTarget_;
            fadeSpeed_ = 0.0f;
            return;
        }
        volume_ += (fadeTarget_ > volume_) ? step : -step;
    }

    float GetPan() const { return pan_; }
    void SetPan(float pan) { pan_ = pan; }

    bool IsMuted() const { return muted_; }
    void SetMuted(bool muted) { muted_ = muted; }

    bool IsSoloed() const { return soloed_; }
    void SetSoloed(bool soloed) { soloed_ = soloed; }

    /// <summary>ピアノロールでの表示色（RGB）</summary>
    const float *GetColor() const { return color_; }
    void SetColor(float r, float g, float b)
    {
        color_[0] = r;
        color_[1] = g;
        color_[2] = b;
    }

  private:
    /// ====================================
    /// private variables
    /// ====================================

    std::string name_;
    std::unique_ptr<Instrument> instrument_;
    std::vector<MusicNote> notes_;
    uint32_t noteIdCounter_ = 0;

    float volume_ = 0.85f;
    float fadeTarget_ = 0.85f; // 音量フェードの行き先
    float fadeSpeed_ = 0.0f;   // 1秒あたりの音量変化量。0 ならフェードしない
    float pan_ = 0.0f;
    bool muted_ = false;
    bool soloed_ = false;
    float color_[3] = {0.45f, 0.60f, 0.78f};
};

} // namespace Hagine
