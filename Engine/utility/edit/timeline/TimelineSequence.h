#pragma once
#include <Easing.h>
#include <nlohmann/json.hpp>
#include <string>
#include <type/Vector3.h>
#include <vector>

namespace Hagine {

/// <summary>
/// タイムラインのトラックの種類
/// </summary>
enum class TimelineTrackType
{
    Transform, // オブジェクトの位置・回転・大きさを動かす
    Camera,    // カメラの位置・向き・画角を動かす
    Sound,     // 決めた時間に音を鳴らす
    Particle,  // 決めた時間にパーティクルを出す
    Event,     // 決めた時間にゲーム側へ合図を送る
    Count,
};

/// <summary>
/// 補間して動かすトラックの1点。次のキーまでを easing で繋ぐ
/// </summary>
struct TimelineKey
{
    float time = 0.0f;                                    // 位置[秒]
    Vector3 position = {};                                // 座標
    Vector3 rotation = {};                                // 回転（オイラー角・ラジアン）
    Vector3 scale = {1.0f, 1.0f, 1.0f};                   // 大きさ
    float fovDegrees = 45.0f;                             // 画角（カメラのみ）
    int easing = static_cast<int>(EasingType::InOutQuad); // 次のキーまでの繋ぎ方
};

/// <summary>
/// 一瞬だけ起きる出来事（音を鳴らす・パーティクルを出す・合図を送る）
/// </summary>
struct TimelineEvent
{
    float time = 0.0f;         // 起きる時間[秒]
    std::string name;          // 音のパス / パーティクルのテンプレート名 / 合図の名前
    float value = 1.0f;        // 音量など、種類ごとの補助的な数値
    Vector3 position = {};     // パーティクルを出す位置
    bool useTargetPosition = true; // 対象オブジェクトの位置に出すか（false なら上の position）
};

/// <summary>
/// タイムラインの1本の行。1つの対象に対して、時間軸上の変化を持つ
/// </summary>
struct TimelineTrack
{
    int type = static_cast<int>(TimelineTrackType::Transform); // TimelineTrackType
    std::string name = "トラック";
    std::string targetName;       // 対象のオブジェクト名 / カメラ名
    bool enabled = true;          // 再生時に反映するか
    std::vector<TimelineKey> keys;
    std::vector<TimelineEvent> events;
};

/// <summary>
/// 1本の演出。複数のトラックをまとめて、JSON へ保存・読み込みできる
/// </summary>
class TimelineSequence
{
  public:
    /// ====================================
    /// public method
    /// ====================================

    /// <summary>演出の名前（保存ファイル名にもなる）</summary>
    const std::string &GetName() const { return name_; }
    void SetName(const std::string &name) { name_ = name; }

    /// <summary>全体の長さ[秒]</summary>
    float GetDuration() const { return duration_; }
    void SetDuration(float seconds) { duration_ = (seconds < 0.1f) ? 0.1f : seconds; }

    /// <summary>終端まで行ったら先頭へ戻るか</summary>
    bool IsLooping() const { return loop_; }
    void SetLooping(bool loop) { loop_ = loop; }

    /// <summary>トラック一覧</summary>
    std::vector<TimelineTrack> &GetTracks() { return tracks_; }
    const std::vector<TimelineTrack> &GetTracks() const { return tracks_; }

    /// <summary>
    /// トラックを追加する
    /// </summary>
    /// <param name="type">種類</param>
    /// <param name="name">表示名</param>
    /// <returns>int: 追加したトラックの番号</returns>
    int AddTrack(TimelineTrackType type, const std::string &name);

    /// <summary>
    /// トラックを削除する
    /// </summary>
    /// <param name="index">トラック番号</param>
    void RemoveTrack(int index);

    /// <summary>中身をすべて捨てる</summary>
    void Clear();

    /// <summary>
    /// 一番後ろのキー・イベントの時刻を返す（長さの目安に使う）
    /// </summary>
    /// <returns>float: 秒</returns>
    float FindLastTime() const;

    /// <summary>
    /// 指定した時刻のトラック状態を求める
    /// </summary>
    /// <param name="track">対象トラック</param>
    /// <param name="time">時刻[秒]</param>
    /// <param name="outKey">補間結果の格納先</param>
    /// <returns>bool: キーが1つ以上あり、結果が得られたら true</returns>
    static bool EvaluateTrack(const TimelineTrack &track, float time, TimelineKey &outKey);

    /// <summary>
    /// JSON へ保存する
    /// </summary>
    /// <param name="path">保存先パス</param>
    /// <param name="outError">失敗理由の格納先（不要なら nullptr）</param>
    /// <returns>bool: 成功したら true</returns>
    bool Save(const std::string &path, std::string *outError = nullptr) const;

    /// <summary>
    /// JSON から読み込む
    /// </summary>
    /// <param name="path">読み込み元パス</param>
    /// <param name="outError">失敗理由の格納先（不要なら nullptr）</param>
    /// <returns>bool: 成功したら true</returns>
    bool Load(const std::string &path, std::string *outError = nullptr);

  private:
    /// ====================================
    /// private variables
    /// ====================================

    std::string name_ = "NewTimeline";
    float duration_ = 5.0f;
    bool loop_ = false;
    std::vector<TimelineTrack> tracks_;
};

} // namespace Hagine
