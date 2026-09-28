#pragma once
#include "Instrument.h"
#include <array>
#include <vector>

namespace Hagine {

/// <summary>
/// 合成で作る打楽器の種類。パッドごとにこのどれかを選ぶ
/// </summary>
enum class DrumVoiceType
{
    Kick,    // バスドラム
    Snare,   // スネア
    HiHat,   // ハイハット
    Tom,     // タム
    Clap,    // ハンドクラップ
    Cymbal,  // シンバル
    Rim,     // リムショット
    Cowbell, // カウベル
    Count,
};

/// <summary>
/// ドラムマシンの1パッド。ノート番号1つに対して音色1つが対応する
/// </summary>
struct DrumPad
{
    std::string name = "Pad";                          // 表示名
    int type = static_cast<int>(DrumVoiceType::Kick);  // DrumVoiceType
    int note = 36;                                     // 割り当てるノート番号
    float tune = 55.0f;                                // 基本となる音の高さ[Hz]
    float decay = 0.35f;                               // 減衰[秒]
    float tone = 0.5f;                                 // 明るさ / ノイズの量 (0〜1)
    float gain = 0.9f;                                 // 音量
    float pan = 0.0f;                                  // 定位 (-1〜1)
    int chokeGroup = 0;                                // 同じ番号のパッド同士は同時に鳴らない（0=なし）
};

/// <summary>
/// 打楽器を合成で鳴らすドラムマシン。
/// サンプル（.wav）を用意しなくてもドラムパートを組めるようにするための楽器。
/// 生の音を使いたくなったら SamplerInstrument へ差し替える。
/// </summary>
class DrumMachineInstrument : public Instrument
{
  public:
    /// ====================================
    /// public method
    /// ====================================

    DrumMachineInstrument();
    ~DrumMachineInstrument() override = default;

    void NoteOn(int note, float velocity) override;
    void NoteOff(int note) override;
    void AllNotesOff() override;
    void Render(float *out, uint32_t frames) override;

    InstrumentKind GetKind() const override { return InstrumentKind::DrumMachine; }
    int GetActiveVoiceCount() const override;
    bool IsPitched() const override { return false; }
    std::string GetNoteLabel(int note) const override;

    void SaveTo(nlohmann::json &outJson) const override;
    void LoadFrom(const nlohmann::json &json) override;

    /// <summary>パッド一覧への参照（エディタが直接触る）</summary>
    /// <returns>std::vector&lt;DrumPad&gt;&amp;: パッド一覧</returns>
    std::vector<DrumPad> &GetPads() { return pads_; }
    const std::vector<DrumPad> &GetPads() const { return pads_; }

    /// <summary>
    /// パッドを追加する
    /// </summary>
    /// <param name="pad">追加する内容</param>
    void AddPad(const DrumPad &pad);

    /// <summary>
    /// パッドを削除する
    /// </summary>
    /// <param name="index">削除するパッドの番号</param>
    void RemovePad(int index);

    /// <summary>
    /// 標準的な8パッド構成（キック・スネア・ハット等）へ戻す
    /// </summary>
    void ResetToDefaultKit();

    /// <summary>打楽器種類の表示名</summary>
    /// <param name="type">DrumVoiceType の値</param>
    /// <returns>const char*: 表示名</returns>
    static const char *GetVoiceTypeName(int type);

  private:
    /// ====================================
    /// private structures
    /// ====================================

    /// 1打ぶんの発音状態
    struct Voice
    {
        bool active = false;
        int padIndex = -1;
        int chokeGroup = 0;
        float velocity = 1.0f;

        float phase = 0.0f;      // 主オシレータの位相
        float phase2 = 0.0f;     // 副オシレータの位相
        float frequency = 55.0f; // 現在の周波数（キック等はここを下げていく）
        float targetFrequency = 55.0f;
        float sweepCoef = 0.0f;

        float amp = 1.0f;      // 音量エンベロープ
        float ampCoef = 0.99f;
        float noiseAmp = 1.0f; // ノイズ側のエンベロープ
        float noiseCoef = 0.99f;

        float lowpassState = 0.0f; // フィルタの内部状態
        float panLeft = 0.7f;
        float panRight = 0.7f;
        float gain = 1.0f;
        int type = 0;
        float tone = 0.5f;
        double elapsed = 0.0; // 発音からの経過秒（クラップの連打などに使う）
    };

    /// ====================================
    /// private method
    /// ====================================

    /// <summary>空いている声部を探す。無ければ一番音量の小さいものを奪う</summary>
    /// <returns>Voice&amp;: 使用する声部</returns>
    Voice &AllocateVoice();

  private:
    /// ====================================
    /// private variables
    /// ====================================

    std::vector<DrumPad> pads_;
    std::array<Voice, 24> voices_;
};

} // namespace Hagine
