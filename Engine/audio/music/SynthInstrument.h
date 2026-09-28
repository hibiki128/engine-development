#pragma once
#include "Instrument.h"
#include <array>

namespace Hagine {

/// <summary>
/// オシレータの波形。Piano / Organ は倍音を重ねた合成波形
/// </summary>
enum class SynthWaveform
{
    Sine,     // サイン波
    Triangle, // 三角波
    Saw,      // のこぎり波
    Square,   // 矩形波
    Pulse,    // パルス波（幅を変えられる矩形波）
    Piano,    // ピアノ寄りの倍音構成
    Organ,    // オルガン寄りの倍音構成
    Noise,    // ノイズ
    Count,
};

/// <summary>
/// シンセの音色パラメータ。まとめて保存・プリセット適用できるよう1つの構造体にしてある
/// </summary>
struct SynthParams
{
    int waveform = static_cast<int>(SynthWaveform::Piano); // SynthWaveform
    float pulseWidth = 0.5f;                               // パルス波の幅 (0.05〜0.95)

    int osc2Semitone = 0;    // 第2オシレータの音程差[半音]
    float osc2Detune = 0.0f; // 第2オシレータのずらし[セント]
    float osc2Mix = 0.0f;    // 第2オシレータの混ぜ具合 (0〜1)

    float attack = 0.003f;  // 立ち上がり[秒]
    float decay = 1.6f;     // 減衰[秒]
    float sustain = 0.0f;   // 保持レベル (0〜1)
    float release = 0.35f;  // 離したあとの余韻[秒]

    float cutoff = 6000.0f;      // ローパスの遮断周波数[Hz]
    float resonance = 0.15f;     // 共振 (0〜0.95)
    float filterEnvAmount = 0.7f; // 発音直後にカットオフを持ち上げる量 (0〜1)
    float filterDecay = 0.9f;     // その持ち上がりが戻るまでの時間[秒]

    float gain = 0.75f;             // 音量
    float pan = 0.0f;               // 定位 (-1=左, 1=右)
    bool velocityToVolume = true;   // 強さを音量へ反映するか
};

/// <summary>
/// 波形を合成して鳴らすポリフォニックシンセ。
/// ピアノ・ベース・リードなど、音程を持つ楽器はすべてこれで作る。
/// </summary>
class SynthInstrument : public Instrument
{
  public:
    /// ====================================
    /// public method
    /// ====================================

    SynthInstrument();
    ~SynthInstrument() override = default;

    void NoteOn(int note, float velocity) override;
    void NoteOff(int note) override;
    void AllNotesOff() override;
    void Render(float *out, uint32_t frames) override;

    InstrumentKind GetKind() const override { return InstrumentKind::Synth; }
    int GetActiveVoiceCount() const override;

    void SaveTo(nlohmann::json &outJson) const override;
    void LoadFrom(const nlohmann::json &json) override;

    /// <summary>音色パラメータへの参照（エディタが直接触る）</summary>
    /// <returns>SynthParams&amp;: パラメータ</returns>
    SynthParams &GetParams() { return params_; }
    const SynthParams &GetParams() const { return params_; }

    /// <summary>
    /// 用意されたプリセットを適用する
    /// </summary>
    /// <param name="presetIndex">プリセット番号 (0 〜 GetPresetCount()-1)</param>
    void ApplyPreset(int presetIndex);

    /// <summary>プリセットの数</summary>
    /// <returns>int: 個数</returns>
    static int GetPresetCount();

    /// <summary>プリセットの表示名</summary>
    /// <param name="presetIndex">プリセット番号</param>
    /// <returns>const char*: 表示名</returns>
    static const char *GetPresetName(int presetIndex);

    /// <summary>波形の表示名</summary>
    /// <param name="waveform">SynthWaveform の値</param>
    /// <returns>const char*: 表示名</returns>
    static const char *GetWaveformName(int waveform);

  private:
    /// ====================================
    /// private structures
    /// ====================================

    /// エンベロープの段階
    enum class EnvStage
    {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release,
    };

    /// 1音ぶんの発音状態
    struct Voice
    {
        bool active = false;
        int note = 60;
        float velocity = 1.0f;
        float frequency = 440.0f;

        float phase1 = 0.0f; // 第1オシレータの位相 (0〜1)
        float phase2 = 0.0f; // 第2オシレータの位相 (0〜1)

        EnvStage stage = EnvStage::Idle;
        float ampLevel = 0.0f;   // 音量エンベロープの現在値
        float filterEnv = 0.0f;  // フィルタエンベロープの現在値

        // ゼロディレイフィードバック型ステートバリアブルフィルタの内部状態
        float ic1 = 0.0f;
        float ic2 = 0.0f;

        uint64_t startOrder = 0; // 発音順。声部を奪うときの判断に使う
    };

    /// ====================================
    /// private method
    /// ====================================

    /// <summary>空いている声部を探す。無ければ一番古い音を奪う</summary>
    /// <returns>Voice&amp;: 使用する声部</returns>
    Voice &AllocateVoice();

    /// <summary>1つのオシレータの値を返す</summary>
    /// <param name="waveform">波形</param>
    /// <param name="phase">位相 (0〜1)</param>
    /// <param name="phaseStep">1サンプルあたりの位相の進み（折り返し歪みの補正に使う）</param>
    /// <returns>float: -1〜1 の値</returns>
    float Oscillator(int waveform, float phase, float phaseStep) const;

  private:
    /// ====================================
    /// private variables
    /// ====================================

    SynthParams params_;
    std::array<Voice, MusicConst::kMaxVoices> voices_;
    uint64_t noteCounter_ = 0; // 発音のたびに増える通し番号
};

} // namespace Hagine
