#pragma once
#include "Instrument.h"
#include "WavFile.h"
#include <array>
#include <vector>

namespace Hagine {

/// <summary>
/// サンプラーの使い方
/// </summary>
enum class SamplerMode
{
    DrumKit, // ノート番号ごとに別の音を割り当てる（ドラムキット）
    Pitched, // 1つの音を鍵盤に合わせて伸び縮みさせる（生ピアノ・効果音など）
};

/// <summary>
/// サンプラーが持つ1つの音。読み込んだ .wav をエンジン内部の形式へそろえて保持する
/// </summary>
struct SamplerSlot
{
    std::string name = "Sample"; // 表示名
    std::string filePath;        // 読み込み元のパス（保存して次回復元する）
    AudioClip clip;              // 実データ（48kHz ステレオへ変換済み）

    int note = 36;        // DrumKit: 割り当てノート / Pitched: このサンプル本来の音程
    float gain = 1.0f;    // 音量
    float pan = 0.0f;     // 定位 (-1〜1)
    float release = 0.05f; // 鍵盤を離してから消えるまで[秒]
    bool loop = false;    // 鳴らし続けるか
    int chokeGroup = 0;   // 同じ番号同士は同時に鳴らない（0=なし）
};

/// <summary>
/// .wav を鳴らすサンプラー。
/// ドラムキットとしても、生ピアノのような音程つきの楽器としても使える。
/// 読み込んだ音はエディタ側で正規化・逆再生・トリム等の加工ができる。
/// </summary>
class SamplerInstrument : public Instrument
{
  public:
    /// ====================================
    /// public method
    /// ====================================

    SamplerInstrument() = default;
    ~SamplerInstrument() override = default;

    void NoteOn(int note, float velocity) override;
    void NoteOff(int note) override;
    void AllNotesOff() override;
    void Render(float *out, uint32_t frames) override;

    InstrumentKind GetKind() const override { return InstrumentKind::Sampler; }
    int GetActiveVoiceCount() const override;
    bool IsPitched() const override { return mode_ == SamplerMode::Pitched; }
    std::string GetNoteLabel(int note) const override;

    void SaveTo(nlohmann::json &outJson) const override;
    void LoadFrom(const nlohmann::json &json) override;

    /// <summary>
    /// .wav を読み込んで新しいスロットを作る
    /// </summary>
    /// <param name="path">読み込む .wav のパス</param>
    /// <param name="outError">失敗理由の格納先（不要なら nullptr）</param>
    /// <returns>int: 追加したスロット番号。失敗なら -1</returns>
    int LoadSample(const std::string &path, std::string *outError = nullptr);

    /// <summary>
    /// 既存スロットの中身を別の .wav で差し替える
    /// </summary>
    /// <param name="slotIndex">対象スロット</param>
    /// <param name="path">読み込む .wav のパス</param>
    /// <param name="outError">失敗理由の格納先（不要なら nullptr）</param>
    /// <returns>bool: 成功したら true</returns>
    bool ReplaceSample(int slotIndex, const std::string &path, std::string *outError = nullptr);

    /// <summary>
    /// スロットを削除する
    /// </summary>
    /// <param name="slotIndex">対象スロット</param>
    void RemoveSlot(int slotIndex);

    /// <summary>スロット一覧への参照（エディタが直接触る）</summary>
    /// <returns>std::vector&lt;SamplerSlot&gt;&amp;: スロット一覧</returns>
    std::vector<SamplerSlot> &GetSlots() { return slots_; }
    const std::vector<SamplerSlot> &GetSlots() const { return slots_; }

    /// <summary>使い方の設定</summary>
    void SetMode(SamplerMode mode) { mode_ = mode; }
    SamplerMode GetMode() const { return mode_; }

  private:
    /// ====================================
    /// private structures
    /// ====================================

    /// 1音ぶんの再生状態
    struct Voice
    {
        bool active = false;
        int slotIndex = -1;
        int note = 60;
        int chokeGroup = 0;
        double position = 0.0; // サンプル内の再生位置[フレーム]
        double step = 1.0;     // 1サンプル進むごとの位置の増分（音程）
        float gain = 1.0f;
        float panLeft = 0.7f;
        float panRight = 0.7f;
        bool loop = false;
        float amp = 1.0f;        // 離鍵後のフェード用
        float releaseCoef = 0.0f; // 0 のあいだは減衰しない
    };

    /// ====================================
    /// private method
    /// ====================================

    /// <summary>指定ノートで鳴らすスロットを決める</summary>
    /// <param name="note">ノート番号</param>
    /// <returns>int: スロット番号。見つからなければ -1</returns>
    int FindSlotForNote(int note) const;

    /// <summary>空いている声部を探す。無ければ一番古いものを奪う</summary>
    /// <returns>Voice&amp;: 使用する声部</returns>
    Voice &AllocateVoice();

  private:
    /// ====================================
    /// private variables
    /// ====================================

    SamplerMode mode_ = SamplerMode::DrumKit;
    std::vector<SamplerSlot> slots_;
    std::array<Voice, MusicConst::kMaxVoices> voices_;
    uint64_t voiceCounter_ = 0;
    std::array<uint64_t, MusicConst::kMaxVoices> voiceOrder_{};
};

} // namespace Hagine
