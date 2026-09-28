#pragma once
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// 使える音の並び（スケール）の種類
/// </summary>
enum class ScaleType
{
    Major,           // メジャー（明るい）
    NaturalMinor,    // ナチュラルマイナー（暗い）
    HarmonicMinor,   // ハーモニックマイナー（緊張感のある暗さ）
    Dorian,          // ドリアン（少し洒落た暗さ）
    Mixolydian,      // ミクソリディアン（ロック寄りの明るさ）
    PentatonicMajor, // メジャーペンタトニック（外れにくい明るさ）
    PentatonicMinor, // マイナーペンタトニック（ギターソロの定番）
    Blues,           // ブルース
    Chromatic,       // 全部の音（制限なし）
    Count,
};

/// <summary>
/// 音楽理論まわりの補助。
///
/// 「どの音を使えば外れないか」「和音をどう積むか」を機械的に出すためのもの。
/// 音楽に詳しくなくても、キーと進行を選ぶだけで形になるようにするのが目的。
/// </summary>
namespace MusicTheory {

/// <summary>スケールの数</summary>
int GetScaleCount();

/// <summary>スケールの表示名</summary>
/// <param name="scaleType">ScaleType の値</param>
/// <returns>const char*: 表示名</returns>
const char *GetScaleName(int scaleType);

/// <summary>主音の表示名（C, C#, D ...）</summary>
/// <param name="root">0〜11</param>
/// <returns>const char*: 表示名</returns>
const char *GetRootName(int root);

/// <summary>
/// その音がスケールに含まれるか
/// </summary>
/// <param name="note">ノート番号</param>
/// <param name="root">主音 (0〜11)</param>
/// <param name="scaleType">ScaleType の値</param>
/// <returns>bool: 含まれていれば true</returns>
bool IsInScale(int note, int root, int scaleType);

/// <summary>
/// スケール上で一番近い音へ寄せる
/// </summary>
/// <param name="note">ノート番号</param>
/// <param name="root">主音 (0〜11)</param>
/// <param name="scaleType">ScaleType の値</param>
/// <returns>int: 寄せた後のノート番号</returns>
int SnapToScale(int note, int root, int scaleType);

/// <summary>
/// スケールの何番目の音か（0 始まり）を返す
/// </summary>
/// <param name="note">ノート番号</param>
/// <param name="root">主音 (0〜11)</param>
/// <param name="scaleType">ScaleType の値</param>
/// <returns>int: 度数。スケール外なら -1</returns>
int GetScaleDegree(int note, int root, int scaleType);

/// <summary>
/// スケール上で1つ飛ばしに音を積んで和音を作る（ダイアトニックコード）。
/// スケールの中だけで積むので、どの度数から始めても音楽的に外れない
/// </summary>
/// <param name="bassNote">一番低い音のノート番号</param>
/// <param name="root">主音 (0〜11)</param>
/// <param name="scaleType">ScaleType の値</param>
/// <param name="noteCount">重ねる音の数（3 で三和音、4 でセブンス）</param>
/// <returns>std::vector&lt;int&gt;: 和音を構成するノート番号</returns>
std::vector<int> BuildDiatonicChord(int bassNote, int root, int scaleType, int noteCount);

/// <summary>
/// 度数を指定して和音を作る
/// </summary>
/// <param name="degree">スケールの何番目から積むか (0 始まり)</param>
/// <param name="octave">オクターブ (4 で中央あたり)</param>
/// <param name="root">主音 (0〜11)</param>
/// <param name="scaleType">ScaleType の値</param>
/// <param name="noteCount">重ねる音の数</param>
/// <returns>std::vector&lt;int&gt;: 和音を構成するノート番号</returns>
std::vector<int> BuildChordFromDegree(int degree, int octave, int root, int scaleType, int noteCount);

/// <summary>
/// 和音の呼び名を返す（Cm7 など）
/// </summary>
/// <param name="notes">和音を構成するノート番号</param>
/// <returns>std::string: 呼び名。判定できなければ最低音の音名だけ</returns>
std::string GetChordName(const std::vector<int> &notes);

/// <summary>よく使うコード進行の数</summary>
int GetProgressionCount();

/// <summary>コード進行の表示名</summary>
/// <param name="index">進行の番号</param>
/// <returns>const char*: 表示名</returns>
const char *GetProgressionName(int index);

/// <summary>
/// コード進行の中身（スケール上の度数の並び）
/// </summary>
/// <param name="index">進行の番号</param>
/// <returns>const std::vector&lt;int&gt;&amp;: 度数の並び</returns>
const std::vector<int> &GetProgressionDegrees(int index);

} // namespace MusicTheory
} // namespace Hagine
