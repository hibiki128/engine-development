#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// メモリ上に展開した音声データ。
/// サンプルは [-1,1] の float でインターリーブ（L,R,L,R,...）して持つ。
/// エンジン内の音声加工はすべてこの形を経由する。
/// </summary>
struct AudioClip
{
    std::vector<float> samples;  // インターリーブされたサンプル列
    uint32_t channels = 0;       // チャンネル数
    uint32_t sampleRate = 0;     // サンプリングレート[Hz]

    /// <summary>フレーム数（1フレーム = 全チャンネル1サンプルぶん）</summary>
    /// <returns>uint32_t: フレーム数</returns>
    uint32_t FrameCount() const
    {
        return (channels == 0) ? 0u : static_cast<uint32_t>(samples.size() / channels);
    }

    /// <summary>再生時間[秒]</summary>
    /// <returns>float: 秒数</returns>
    float DurationSeconds() const
    {
        return (sampleRate == 0) ? 0.0f : static_cast<float>(FrameCount()) / static_cast<float>(sampleRate);
    }

    /// <summary>中身が空かどうか</summary>
    bool Empty() const { return samples.empty() || channels == 0; }

    /// <summary>中身を捨てる</summary>
    void Clear()
    {
        samples.clear();
        channels = 0;
        sampleRate = 0;
    }
};

/// <summary>
/// .wav ファイルの読み書きと、読み込んだ音声への基本的な加工。
///
/// 読み込みは PCM 8/16/24/32bit と IEEE float 32bit、WAVE_FORMAT_EXTENSIBLE に対応する。
/// 加工系はすべて AudioClip をその場で書き換える（元に戻したいときは呼び出し側で複製しておく）。
/// </summary>
namespace WavFile {

/// <summary>
/// .wav を読み込んで AudioClip へ展開する
/// </summary>
/// <param name="path">ファイルパス（作業ディレクトリからの相対 or 絶対）</param>
/// <param name="outClip">読み込み結果の格納先</param>
/// <param name="outError">失敗理由の格納先（不要なら nullptr）</param>
/// <returns>bool: 成功したら true</returns>
bool Load(const std::string &path, AudioClip &outClip, std::string *outError = nullptr);

/// <summary>
/// AudioClip を .wav として書き出す
/// </summary>
/// <param name="path">出力先パス。途中のフォルダが無ければ作成する</param>
/// <param name="clip">書き出す音声</param>
/// <param name="bitsPerSample">ビット深度。16 / 24 / 32(float) のいずれか</param>
/// <param name="outError">失敗理由の格納先（不要なら nullptr）</param>
/// <returns>bool: 成功したら true</returns>
bool Save(const std::string &path, const AudioClip &clip, int bitsPerSample = 16, std::string *outError = nullptr);

/// <summary>
/// サンプリングレートを変換する（線形補間）
/// </summary>
/// <param name="clip">対象</param>
/// <param name="newSampleRate">変換後のレート[Hz]</param>
void Resample(AudioClip &clip, uint32_t newSampleRate);

/// <summary>
/// チャンネル数を変換する。モノラル→ステレオは複製、多チャンネル→少は平均でまとめる
/// </summary>
/// <param name="clip">対象</param>
/// <param name="newChannels">変換後のチャンネル数</param>
void ConvertChannels(AudioClip &clip, uint32_t newChannels);

/// <summary>
/// エンジン内部の形式（MusicConst::kSampleRate / kChannels）へそろえる
/// </summary>
/// <param name="clip">対象</param>
void ConvertToEngineFormat(AudioClip &clip);

// --- 加工 ---------------------------------------------------------------

/// <summary>最大振幅が peak になるよう全体を一律に増減する</summary>
/// <param name="clip">対象</param>
/// <param name="peak">そろえたいピーク値 (0〜1)</param>
void Normalize(AudioClip &clip, float peak = 0.98f);

/// <summary>全体へ倍率を掛ける</summary>
/// <param name="clip">対象</param>
/// <param name="gain">倍率</param>
void ApplyGain(AudioClip &clip, float gain);

/// <summary>逆再生にする</summary>
/// <param name="clip">対象</param>
void Reverse(AudioClip &clip);

/// <summary>先頭と末尾にフェードを掛ける</summary>
/// <param name="clip">対象</param>
/// <param name="fadeInSeconds">フェードインの長さ[秒]</param>
/// <param name="fadeOutSeconds">フェードアウトの長さ[秒]</param>
void Fade(AudioClip &clip, float fadeInSeconds, float fadeOutSeconds);

/// <summary>指定した範囲だけを残す</summary>
/// <param name="clip">対象</param>
/// <param name="startFrame">残す範囲の先頭フレーム</param>
/// <param name="endFrame">残す範囲の終端フレーム（この位置は含まない）</param>
void Trim(AudioClip &clip, uint32_t startFrame, uint32_t endFrame);

/// <summary>前後の無音部分を削る</summary>
/// <param name="clip">対象</param>
/// <param name="threshold">無音とみなす振幅のしきい値</param>
void TrimSilence(AudioClip &clip, float threshold = 0.002f);

/// <summary>ピーク（最大振幅）を調べる</summary>
/// <param name="clip">対象</param>
/// <returns>float: 最大振幅 (0〜)</returns>
float FindPeak(const AudioClip &clip);

/// <summary>
/// 波形表示用に、区間ごとの最小値・最大値の組を作る
/// </summary>
/// <param name="clip">対象</param>
/// <param name="bucketCount">分割数（＝グラフの横方向の点数）</param>
/// <param name="outMin">各区間の最小値</param>
/// <param name="outMax">各区間の最大値</param>
void BuildEnvelope(const AudioClip &clip, int bucketCount, std::vector<float> &outMin, std::vector<float> &outMax);

} // namespace WavFile
} // namespace Hagine
