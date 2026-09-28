#include "MusicTheory.h"
#include "MusicTypes.h"

#include <algorithm>
#include <array>
#include <iterator>

namespace Hagine {
namespace MusicTheory {
namespace {

/// スケールの中身。主音からの半音差の並び
struct ScaleEntry
{
    const char *name;
    std::vector<int> intervals;
};

const ScaleEntry kScales[] = {
    {"メジャー（明るい）", {0, 2, 4, 5, 7, 9, 11}},
    {"マイナー（暗い）", {0, 2, 3, 5, 7, 8, 10}},
    {"ハーモニックマイナー", {0, 2, 3, 5, 7, 8, 11}},
    {"ドリアン", {0, 2, 3, 5, 7, 9, 10}},
    {"ミクソリディアン", {0, 2, 4, 5, 7, 9, 10}},
    {"メジャーペンタトニック", {0, 2, 4, 7, 9}},
    {"マイナーペンタトニック", {0, 3, 5, 7, 10}},
    {"ブルース", {0, 3, 5, 6, 7, 10}},
    {"すべての音（制限なし）", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
};

/// コード進行のプリセット。並びはスケール上の度数（0 = 主和音）
struct ProgressionEntry
{
    const char *name;
    std::vector<int> degrees;
};

const ProgressionEntry kProgressions[] = {
    {"I-V-vi-IV（定番ポップ）", {0, 4, 5, 3}},
    {"vi-IV-I-V（小室進行）", {5, 3, 0, 4}},
    {"IV-V-iii-vi（王道進行）", {3, 4, 2, 5}},
    {"カノン進行", {0, 4, 5, 2, 3, 0, 3, 4}},
    {"ii-V-I（ジャズ）", {1, 4, 0}},
    {"I-vi-IV-V（50年代）", {0, 5, 3, 4}},
    {"12小節ブルース", {0, 0, 0, 0, 3, 3, 0, 0, 4, 3, 0, 0}},
};

/// 主音からの距離を 0〜11 に収める
int PitchClassOffset(int note, int root)
{
    return ((note - root) % 12 + 12) % 12;
}

/// 有効なスケール番号へ丸める
int ClampScale(int scaleType)
{
    return std::clamp(scaleType, 0, GetScaleCount() - 1);
}

} // namespace

int GetScaleCount()
{
    return static_cast<int>(std::size(kScales));
}

const char *GetScaleName(int scaleType)
{
    if (scaleType < 0 || scaleType >= GetScaleCount())
        return "";
    return kScales[scaleType].name;
}

const char *GetRootName(int root)
{
    static const char *kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return kNames[((root % 12) + 12) % 12];
}

bool IsInScale(int note, int root, int scaleType)
{
    const std::vector<int> &intervals = kScales[ClampScale(scaleType)].intervals;
    const int offset = PitchClassOffset(note, root);
    return std::find(intervals.begin(), intervals.end(), offset) != intervals.end();
}

int GetScaleDegree(int note, int root, int scaleType)
{
    const std::vector<int> &intervals = kScales[ClampScale(scaleType)].intervals;
    const int offset = PitchClassOffset(note, root);
    const auto it = std::find(intervals.begin(), intervals.end(), offset);
    if (it == intervals.end())
        return -1;
    return static_cast<int>(std::distance(intervals.begin(), it));
}

int SnapToScale(int note, int root, int scaleType)
{
    if (IsInScale(note, root, scaleType))
        return note;
    // 上下へ1半音ずつ広げながら、最初に見つかったスケール音を採用する
    for (int distance = 1; distance <= 6; ++distance)
    {
        if (IsInScale(note - distance, root, scaleType))
            return note - distance;
        if (IsInScale(note + distance, root, scaleType))
            return note + distance;
    }
    return note;
}

std::vector<int> BuildChordFromDegree(int degree, int octave, int root, int scaleType, int noteCount)
{
    const std::vector<int> &intervals = kScales[ClampScale(scaleType)].intervals;
    const int scaleSize = static_cast<int>(intervals.size());
    std::vector<int> chord;
    if (scaleSize == 0)
        return chord;

    noteCount = std::clamp(noteCount, 1, 5);
    const int baseNote = 12 * (octave + 1) + (((root % 12) + 12) % 12);

    // 1つ飛ばしに積む＝スケールの中で3度ずつ重ねることになる
    for (int i = 0; i < noteCount; ++i)
    {
        const int scaleIndex = degree + i * 2;
        const int octaveShift = scaleIndex / scaleSize;
        const int wrapped = ((scaleIndex % scaleSize) + scaleSize) % scaleSize;
        const int note = baseNote + intervals[wrapped] + 12 * octaveShift;
        if (note >= MusicConst::kLowestNote && note <= MusicConst::kHighestNote)
            chord.push_back(note);
    }
    return chord;
}

std::vector<int> BuildDiatonicChord(int bassNote, int root, int scaleType, int noteCount)
{
    const int snapped = SnapToScale(bassNote, root, scaleType);
    const int degree = GetScaleDegree(snapped, root, scaleType);
    if (degree < 0)
        return {snapped};

    // 押した高さをそのまま最低音にしたいので、そこから積み直す
    const std::vector<int> &intervals = kScales[ClampScale(scaleType)].intervals;
    const int scaleSize = static_cast<int>(intervals.size());
    std::vector<int> chord;
    noteCount = std::clamp(noteCount, 1, 5);
    for (int i = 0; i < noteCount; ++i)
    {
        const int scaleIndex = degree + i * 2;
        const int octaveShift = scaleIndex / scaleSize;
        const int wrapped = ((scaleIndex % scaleSize) + scaleSize) % scaleSize;
        const int note = snapped - intervals[degree] + intervals[wrapped] + 12 * octaveShift;
        if (note >= MusicConst::kLowestNote && note <= MusicConst::kHighestNote)
            chord.push_back(note);
    }
    return chord;
}

std::string GetChordName(const std::vector<int> &notes)
{
    if (notes.empty())
        return "";

    std::vector<int> sorted = notes;
    std::sort(sorted.begin(), sorted.end());
    const int bass = sorted.front();
    const std::string bassName = GetRootName(bass);
    if (sorted.size() == 1)
        return bassName;

    // 最低音からの半音差の組み合わせで和音の種類を見分ける
    std::vector<int> intervals;
    for (size_t i = 1; i < sorted.size(); ++i)
        intervals.push_back((sorted[i] - bass) % 12);
    std::sort(intervals.begin(), intervals.end());
    intervals.erase(std::unique(intervals.begin(), intervals.end()), intervals.end());

    struct ChordShape
    {
        std::vector<int> intervals;
        const char *suffix;
    };
    static const ChordShape kShapes[] = {
        {{4, 7}, ""},        {{3, 7}, "m"},       {{3, 6}, "dim"},     {{4, 8}, "aug"},
        {{5, 7}, "sus4"},    {{2, 7}, "sus2"},    {{4, 7, 11}, "maj7"}, {{4, 7, 10}, "7"},
        {{3, 7, 10}, "m7"},  {{3, 7, 11}, "mM7"}, {{3, 6, 10}, "m7b5"}, {{3, 6, 9}, "dim7"},
        {{4, 7, 9}, "6"},    {{3, 7, 9}, "m6"},
    };
    for (const ChordShape &shape : kShapes)
    {
        if (shape.intervals == intervals)
            return bassName + shape.suffix;
    }
    return bassName;
}

int GetProgressionCount()
{
    return static_cast<int>(std::size(kProgressions));
}

const char *GetProgressionName(int index)
{
    if (index < 0 || index >= GetProgressionCount())
        return "";
    return kProgressions[index].name;
}

const std::vector<int> &GetProgressionDegrees(int index)
{
    static const std::vector<int> kEmpty;
    if (index < 0 || index >= GetProgressionCount())
        return kEmpty;
    return kProgressions[index].degrees;
}

} // namespace MusicTheory
} // namespace Hagine
