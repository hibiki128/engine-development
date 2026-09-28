#include "WavFile.h"
#include "MusicTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace Hagine {
namespace {

// WAVE のフォーマットタグ。mmreg.h の定義と同値だが、
// Windows ヘッダの取り込み状況に依存したくないのでここで持つ。
constexpr uint16_t kFormatPcm = 0x0001;
constexpr uint16_t kFormatFloat = 0x0003;
constexpr uint16_t kFormatExtensible = 0xFFFE;

/// ファイル先頭のチャンク見出し（"RIFF" や "fmt " など）
struct ChunkHead
{
    char id[4];
    uint32_t size;
};

/// 生バイト列の 1 サンプルを [-1,1] の float へ直す
float DecodeSample(const uint8_t *p, uint16_t bits, uint16_t formatTag)
{
    switch (bits)
    {
    case 8:
        // 8bit だけは符号なし（128 が無音）
        return (static_cast<float>(*p) - 128.0f) / 128.0f;
    case 16: {
        int16_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(v) / 32768.0f;
    }
    case 24: {
        // 下位から 3 バイト。最上位バイトの符号を 32bit へ引き伸ばす
        const int32_t v = (static_cast<int32_t>(p[0]) << 8) |
                          (static_cast<int32_t>(p[1]) << 16) |
                          (static_cast<int32_t>(p[2]) << 24);
        return static_cast<float>(v) / 2147483648.0f;
    }
    case 32: {
        if (formatTag == kFormatFloat)
        {
            float v = 0.0f;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }
        int32_t v = 0;
        std::memcpy(&v, p, sizeof(v));
        return static_cast<float>(v) / 2147483648.0f;
    }
    default:
        return 0.0f;
    }
}

/// [-1,1] の float を指定ビット深度の生バイト列へ書く
void EncodeSample(float value, uint8_t *p, int bits)
{
    // 書き出し時に歪ませないよう、範囲外は必ず切り詰める
    value = std::clamp(value, -1.0f, 1.0f);
    switch (bits)
    {
    case 16: {
        const int16_t v = static_cast<int16_t>(std::lround(value * 32767.0f));
        std::memcpy(p, &v, sizeof(v));
        break;
    }
    case 24: {
        const int32_t v = static_cast<int32_t>(std::lround(static_cast<double>(value) * 8388607.0));
        p[0] = static_cast<uint8_t>(v & 0xFF);
        p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        break;
    }
    case 32:
    default:
        std::memcpy(p, &value, sizeof(float));
        break;
    }
}

/// 失敗理由を書き込むだけの小物（outError が nullptr でも呼べるように）
bool Fail(std::string *outError, const std::string &message)
{
    if (outError)
        *outError = message;
    return false;
}

} // namespace

namespace WavFile {

bool Load(const std::string &path, AudioClip &outClip, std::string *outError)
{
    outClip.Clear();

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return Fail(outError, "ファイルを開けませんでした: " + path);

    ChunkHead riff{};
    file.read(reinterpret_cast<char *>(&riff), sizeof(riff));
    if (!file || std::strncmp(riff.id, "RIFF", 4) != 0)
        return Fail(outError, "RIFF ヘッダがありません: " + path);

    char waveTag[4] = {};
    file.read(waveTag, 4);
    if (!file || std::strncmp(waveTag, "WAVE", 4) != 0)
        return Fail(outError, "WAVE 形式ではありません: " + path);

    uint16_t formatTag = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    std::vector<uint8_t> rawData;
    bool hasFormat = false;

    // fmt と data はどちらが先に来ても、間に別のチャンクが挟まってもよい作りにする
    ChunkHead chunk{};
    while (file.read(reinterpret_cast<char *>(&chunk), sizeof(chunk)))
    {
        const std::streampos next = file.tellg() + static_cast<std::streamoff>(chunk.size + (chunk.size & 1));

        if (std::strncmp(chunk.id, "fmt ", 4) == 0)
        {
            std::vector<uint8_t> fmt(chunk.size);
            file.read(reinterpret_cast<char *>(fmt.data()), chunk.size);
            if (chunk.size < 16)
                return Fail(outError, "fmt チャンクが壊れています: " + path);

            std::memcpy(&formatTag, fmt.data() + 0, 2);
            std::memcpy(&channels, fmt.data() + 2, 2);
            std::memcpy(&sampleRate, fmt.data() + 4, 4);
            std::memcpy(&bitsPerSample, fmt.data() + 14, 2);

            // 拡張形式は本当の種別が末尾の GUID 先頭 2 バイトに入っている
            if (formatTag == kFormatExtensible && chunk.size >= 40)
            {
                uint16_t subFormat = 0;
                std::memcpy(&subFormat, fmt.data() + 24, 2);
                formatTag = subFormat;
            }
            hasFormat = true;
        }
        else if (std::strncmp(chunk.id, "data", 4) == 0)
        {
            rawData.resize(chunk.size);
            file.read(reinterpret_cast<char *>(rawData.data()), chunk.size);
        }

        file.clear();
        file.seekg(next);
        if (file.eof() || !file)
            break;
    }

    if (!hasFormat)
        return Fail(outError, "fmt チャンクが見つかりません: " + path);
    if (rawData.empty())
        return Fail(outError, "data チャンクが見つかりません: " + path);
    if (channels == 0 || sampleRate == 0)
        return Fail(outError, "チャンネル数またはレートが不正です: " + path);
    if (formatTag != kFormatPcm && formatTag != kFormatFloat)
        return Fail(outError, "対応していない圧縮形式です（PCM/float のみ対応）: " + path);
    if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)
        return Fail(outError, "対応していないビット深度です: " + path);

    const uint32_t bytesPerSample = bitsPerSample / 8u;
    const size_t totalSamples = rawData.size() / bytesPerSample;

    outClip.channels = channels;
    outClip.sampleRate = sampleRate;
    outClip.samples.resize(totalSamples);
    for (size_t i = 0; i < totalSamples; ++i)
    {
        outClip.samples[i] = DecodeSample(rawData.data() + i * bytesPerSample, bitsPerSample, formatTag);
    }
    return true;
}

bool Save(const std::string &path, const AudioClip &clip, int bitsPerSample, std::string *outError)
{
    if (clip.Empty())
        return Fail(outError, "書き出す音声がありません");
    if (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)
        return Fail(outError, "書き出しは 16 / 24 / 32bit のみ対応しています");

    // 出力先のフォルダが無ければ作る（jsons や sounds の下に新フォルダを掘れるように）
    std::error_code ec;
    const std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path())
        std::filesystem::create_directories(fsPath.parent_path(), ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return Fail(outError, "ファイルを作成できませんでした: " + path);

    const uint16_t formatTag = (bitsPerSample == 32) ? kFormatFloat : kFormatPcm;
    const uint16_t channels = static_cast<uint16_t>(clip.channels);
    const uint32_t sampleRate = clip.sampleRate;
    const uint16_t bytesPerSample = static_cast<uint16_t>(bitsPerSample / 8);
    const uint16_t blockAlign = static_cast<uint16_t>(channels * bytesPerSample);
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataBytes = static_cast<uint32_t>(clip.samples.size()) * bytesPerSample;
    const uint32_t riffSize = 36u + dataBytes;

    auto writeU32 = [&file](uint32_t v) { file.write(reinterpret_cast<const char *>(&v), 4); };
    auto writeU16 = [&file](uint16_t v) { file.write(reinterpret_cast<const char *>(&v), 2); };

    file.write("RIFF", 4);
    writeU32(riffSize);
    file.write("WAVE", 4);

    file.write("fmt ", 4);
    writeU32(16);
    writeU16(formatTag);
    writeU16(channels);
    writeU32(sampleRate);
    writeU32(byteRate);
    writeU16(blockAlign);
    writeU16(static_cast<uint16_t>(bitsPerSample));

    file.write("data", 4);
    writeU32(dataBytes);

    // 1 サンプルずつ書くと遅いので、いったんバイト列へ詰めてから一括で流す
    std::vector<uint8_t> bytes(dataBytes);
    for (size_t i = 0; i < clip.samples.size(); ++i)
    {
        EncodeSample(clip.samples[i], bytes.data() + i * bytesPerSample, bitsPerSample);
    }
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    if (!file)
        return Fail(outError, "書き込みに失敗しました: " + path);
    return true;
}

void Resample(AudioClip &clip, uint32_t newSampleRate)
{
    if (clip.Empty() || newSampleRate == 0 || clip.sampleRate == newSampleRate)
        return;

    const uint32_t channels = clip.channels;
    const uint32_t srcFrames = clip.FrameCount();
    const double ratio = static_cast<double>(newSampleRate) / static_cast<double>(clip.sampleRate);
    const uint32_t dstFrames = std::max(1u, static_cast<uint32_t>(std::llround(srcFrames * ratio)));

    std::vector<float> dst(static_cast<size_t>(dstFrames) * channels);
    for (uint32_t f = 0; f < dstFrames; ++f)
    {
        const double srcPos = static_cast<double>(f) / ratio;
        const uint32_t i0 = static_cast<uint32_t>(srcPos);
        const uint32_t i1 = std::min(i0 + 1u, srcFrames - 1u);
        const float t = static_cast<float>(srcPos - static_cast<double>(i0));
        for (uint32_t c = 0; c < channels; ++c)
        {
            const float a = clip.samples[static_cast<size_t>(i0) * channels + c];
            const float b = clip.samples[static_cast<size_t>(i1) * channels + c];
            dst[static_cast<size_t>(f) * channels + c] = a + (b - a) * t;
        }
    }
    clip.samples.swap(dst);
    clip.sampleRate = newSampleRate;
}

void ConvertChannels(AudioClip &clip, uint32_t newChannels)
{
    if (clip.Empty() || newChannels == 0 || clip.channels == newChannels)
        return;

    const uint32_t srcChannels = clip.channels;
    const uint32_t frames = clip.FrameCount();
    std::vector<float> dst(static_cast<size_t>(frames) * newChannels);

    for (uint32_t f = 0; f < frames; ++f)
    {
        const float *src = clip.samples.data() + static_cast<size_t>(f) * srcChannels;
        float *out = dst.data() + static_cast<size_t>(f) * newChannels;

        if (srcChannels == 1)
        {
            // モノラルは全チャンネルへ同じ音を配る
            for (uint32_t c = 0; c < newChannels; ++c)
                out[c] = src[0];
        }
        else if (newChannels == 1)
        {
            // まとめるときは平均。単純加算だと音量が跳ね上がる
            float sum = 0.0f;
            for (uint32_t c = 0; c < srcChannels; ++c)
                sum += src[c];
            out[0] = sum / static_cast<float>(srcChannels);
        }
        else
        {
            for (uint32_t c = 0; c < newChannels; ++c)
                out[c] = src[std::min(c, srcChannels - 1u)];
        }
    }
    clip.samples.swap(dst);
    clip.channels = newChannels;
}

void ConvertToEngineFormat(AudioClip &clip)
{
    if (clip.Empty())
        return;
    Resample(clip, MusicConst::kSampleRate);
    ConvertChannels(clip, MusicConst::kChannels);
}

float FindPeak(const AudioClip &clip)
{
    float peak = 0.0f;
    for (float v : clip.samples)
        peak = std::max(peak, std::fabs(v));
    return peak;
}

void Normalize(AudioClip &clip, float peak)
{
    const float current = FindPeak(clip);
    if (current <= 1e-6f)
        return;
    ApplyGain(clip, peak / current);
}

void ApplyGain(AudioClip &clip, float gain)
{
    for (float &v : clip.samples)
        v *= gain;
}

void Reverse(AudioClip &clip)
{
    if (clip.Empty())
        return;
    const uint32_t channels = clip.channels;
    const uint32_t frames = clip.FrameCount();
    // チャンネルの並びは保ったままフレーム単位で入れ替える
    for (uint32_t f = 0; f < frames / 2; ++f)
    {
        float *a = clip.samples.data() + static_cast<size_t>(f) * channels;
        float *b = clip.samples.data() + static_cast<size_t>(frames - 1 - f) * channels;
        for (uint32_t c = 0; c < channels; ++c)
            std::swap(a[c], b[c]);
    }
}

void Fade(AudioClip &clip, float fadeInSeconds, float fadeOutSeconds)
{
    if (clip.Empty())
        return;
    const uint32_t channels = clip.channels;
    const uint32_t frames = clip.FrameCount();

    const uint32_t inFrames = std::min(frames, static_cast<uint32_t>(std::max(0.0f, fadeInSeconds) * clip.sampleRate));
    for (uint32_t f = 0; f < inFrames; ++f)
    {
        const float gain = static_cast<float>(f) / static_cast<float>(inFrames);
        for (uint32_t c = 0; c < channels; ++c)
            clip.samples[static_cast<size_t>(f) * channels + c] *= gain;
    }

    const uint32_t outFrames = std::min(frames, static_cast<uint32_t>(std::max(0.0f, fadeOutSeconds) * clip.sampleRate));
    for (uint32_t f = 0; f < outFrames; ++f)
    {
        const float gain = static_cast<float>(f) / static_cast<float>(outFrames);
        const uint32_t index = frames - 1 - f;
        for (uint32_t c = 0; c < channels; ++c)
            clip.samples[static_cast<size_t>(index) * channels + c] *= gain;
    }
}

void Trim(AudioClip &clip, uint32_t startFrame, uint32_t endFrame)
{
    if (clip.Empty())
        return;
    const uint32_t frames = clip.FrameCount();
    startFrame = std::min(startFrame, frames);
    endFrame = std::clamp(endFrame, startFrame, frames);
    if (startFrame == 0 && endFrame == frames)
        return;

    const size_t channels = clip.channels;
    std::vector<float> dst(clip.samples.begin() + static_cast<ptrdiff_t>(startFrame * channels),
                           clip.samples.begin() + static_cast<ptrdiff_t>(endFrame * channels));
    clip.samples.swap(dst);
}

void TrimSilence(AudioClip &clip, float threshold)
{
    if (clip.Empty())
        return;
    const uint32_t channels = clip.channels;
    const uint32_t frames = clip.FrameCount();

    // 全チャンネルのどれかがしきい値を超えていれば「音がある」とみなす
    auto isLoud = [&](uint32_t frame) {
        for (uint32_t c = 0; c < channels; ++c)
        {
            if (std::fabs(clip.samples[static_cast<size_t>(frame) * channels + c]) > threshold)
                return true;
        }
        return false;
    };

    uint32_t first = 0;
    while (first < frames && !isLoud(first))
        ++first;
    if (first >= frames)
        return; // 全部無音。何もしない方が事故が少ない

    uint32_t last = frames;
    while (last > first && !isLoud(last - 1))
        --last;

    Trim(clip, first, last);
}

void BuildEnvelope(const AudioClip &clip, int bucketCount, std::vector<float> &outMin, std::vector<float> &outMax)
{
    outMin.clear();
    outMax.clear();
    if (clip.Empty() || bucketCount <= 0)
        return;

    outMin.resize(bucketCount, 0.0f);
    outMax.resize(bucketCount, 0.0f);

    const uint32_t channels = clip.channels;
    const uint32_t frames = clip.FrameCount();
    const double framesPerBucket = static_cast<double>(frames) / static_cast<double>(bucketCount);

    for (int b = 0; b < bucketCount; ++b)
    {
        const uint32_t begin = static_cast<uint32_t>(b * framesPerBucket);
        const uint32_t end = std::min(frames, static_cast<uint32_t>((b + 1) * framesPerBucket));
        float lo = 0.0f;
        float hi = 0.0f;
        for (uint32_t f = begin; f < end; ++f)
        {
            // 表示はモノラルでよいので、フレーム内は平均してしまう
            float sum = 0.0f;
            for (uint32_t c = 0; c < channels; ++c)
                sum += clip.samples[static_cast<size_t>(f) * channels + c];
            const float v = sum / static_cast<float>(channels);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        outMin[b] = lo;
        outMax[b] = hi;
    }
}

} // namespace WavFile
} // namespace Hagine
