#pragma once
#include <cmath>
#include <cstdint>
#include <string>

namespace Hagine {

/// <summary>
/// 音楽機能（シンセ・シーケンサ・エディタ）全体で共有する定数
/// </summary>
namespace MusicConst {

/// 内部処理のサンプリングレート。読み込んだ .wav はすべてここへ変換して扱う
inline constexpr uint32_t kSampleRate = 48000;

/// 内部処理のチャンネル数（ステレオ固定）
inline constexpr uint32_t kChannels = 2;

/// 4分音符あたりのティック数。16分音符 = 24 ティック、1小節(4/4) = 384 ティック
inline constexpr int kTicksPerBeat = 96;

/// 扱う音域の下限（A0。88鍵ピアノの一番左）
inline constexpr int kLowestNote = 21;

/// 扱う音域の上限（C8。88鍵ピアノの一番右）
inline constexpr int kHighestNote = 108;

/// 1つの楽器が同時に鳴らせる音の数
inline constexpr int kMaxVoices = 32;

/// ミキサーが一度に生成するフレーム数。小さいほど発音の遅れが減るがCPU負荷は上がる
inline constexpr uint32_t kBlockFrames = 256;

/// ストリーミング用に確保するブロック数（うち kBlockCount-1 個までを再生待ち行列へ積む）
inline constexpr uint32_t kBlockCount = 4;

} // namespace MusicConst

/// <summary>
/// ノート番号を周波数[Hz]へ変換する（69 = A4 = 440Hz）
/// </summary>
/// <param name="noteNumber">ノート番号。小数を渡せばセント単位のずらしになる</param>
/// <returns>float: 周波数[Hz]</returns>
inline float NoteToFrequency(float noteNumber)
{
    return 440.0f * std::pow(2.0f, (noteNumber - 69.0f) / 12.0f);
}

/// <summary>
/// そのノート番号が黒鍵かどうかを返す
/// </summary>
/// <param name="noteNumber">ノート番号</param>
/// <returns>bool: 黒鍵なら true</returns>
inline bool IsBlackKey(int noteNumber)
{
    static const bool kTable[12] = {false, true, false, true, false, false, true, false, true, false, true, false};
    const int pitchClass = ((noteNumber % 12) + 12) % 12;
    return kTable[pitchClass];
}

/// <summary>
/// ノート番号を音名の文字列へ変換する（例: 60 → "C4"）
/// </summary>
/// <param name="noteNumber">ノート番号</param>
/// <returns>std::string: 音名</returns>
inline std::string NoteName(int noteNumber)
{
    static const char *kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int pitchClass = ((noteNumber % 12) + 12) % 12;
    const int octave = (noteNumber / 12) - 1;
    return std::string(kNames[pitchClass]) + std::to_string(octave);
}

/// <summary>
/// ティック数を秒へ変換する
/// </summary>
/// <param name="ticks">ティック数</param>
/// <param name="bpm">テンポ</param>
/// <returns>double: 秒</returns>
inline double TicksToSeconds(double ticks, double bpm)
{
    return (ticks / static_cast<double>(MusicConst::kTicksPerBeat)) * (60.0 / bpm);
}

/// <summary>
/// 秒をティック数へ変換する
/// </summary>
/// <param name="seconds">秒</param>
/// <param name="bpm">テンポ</param>
/// <returns>double: ティック数</returns>
inline double SecondsToTicks(double seconds, double bpm)
{
    return seconds * (bpm / 60.0) * static_cast<double>(MusicConst::kTicksPerBeat);
}

/// <summary>
/// シーケンサ上の1音。ピアノロールの1つの長方形に対応する
/// </summary>
struct MusicNote
{
    uint32_t id = 0;        // 編集中の同一性を保つためのID（トラック内で一意）
    int note = 60;          // ノート番号
    int startTick = 0;      // 発音開始位置
    int lengthTick = 96;    // 長さ
    float velocity = 0.8f;  // 強さ (0〜1)
};

} // namespace Hagine
