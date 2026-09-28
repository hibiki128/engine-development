#pragma once
#include "TimelineSequence.h"
#include <functional>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// タイムライン演出の再生を受け持つ。
///
/// カメラ・オブジェクト・音・パーティクルを1本の時間軸に並べて、
/// 名前を指定するだけで演出を流せるようにするためのもの。
/// エディタ（TimelineEditor）と再生対象のシーケンスは共有しているので、
/// 編集しながらその場で確認できる。
/// </summary>
class TimelineManager
{
  private:
    TimelineManager() = default;
    ~TimelineManager() = default;
    TimelineManager(const TimelineManager &) = delete;
    TimelineManager &operator=(const TimelineManager &) = delete;

  public:
    /// 合図（イベントトラック）を受け取る関数の形
    using EventCallback = std::function<void(const std::string &name, float value)>;

    /// ====================================
    /// public method
    /// ====================================

    /// <summary>シングルトンインスタンスの取得</summary>
    static TimelineManager *GetInstance();

    /// <summary>保存済み演出の一覧を作る</summary>
    void Initialize();

    /// <summary>再生を止めて中身を捨てる</summary>
    void Finalize();

    /// <summary>
    /// 毎フレームの更新。再生中なら時間を進めて対象へ反映する
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過時間[秒]</param>
    void Update(float deltaTime);

    // --- 再生 -------------------------------------------------------------

    /// <summary>
    /// 名前を指定して演出を再生する（ゲームから使う一番の入口）
    /// </summary>
    /// <param name="name">jsons/timelines 配下の拡張子なしファイル名</param>
    /// <returns>bool: 読み込めて再生を始めたら true</returns>
    bool Play(const std::string &name);

    /// <summary>今エディタで開いている演出を先頭から再生する</summary>
    void PlayCurrent();

    /// <summary>再生を止めて先頭へ戻す</summary>
    void Stop();

    /// <summary>その場で止める</summary>
    void Pause();

    /// <summary>再生中かどうか</summary>
    bool IsPlaying() const { return playing_; }

    /// <summary>現在の再生位置[秒]</summary>
    float GetTime() const { return time_; }

    /// <summary>
    /// 再生位置を動かす
    /// </summary>
    /// <param name="seconds">移動先[秒]</param>
    /// <param name="fireEvents">音やパーティクルの合図も出すか。エディタのつまみ操作では false</param>
    void SetTime(float seconds, bool fireEvents = false);

    // --- 編集 -------------------------------------------------------------

    /// <summary>エディタが編集している演出</summary>
    TimelineSequence &GetSequence() { return sequence_; }
    const TimelineSequence &GetSequence() const { return sequence_; }

    /// <summary>保存済み演出の名前一覧</summary>
    const std::vector<std::string> &GetSequenceNames() const { return sequenceNames_; }

    /// <summary>保存先フォルダを走査して一覧を作り直す</summary>
    void RescanSequences();

    /// <summary>
    /// 演出を読み込んで編集対象にする
    /// </summary>
    /// <param name="name">拡張子なしのファイル名</param>
    /// <param name="outError">失敗理由の格納先（不要なら nullptr）</param>
    /// <returns>bool: 成功したら true</returns>
    bool LoadSequence(const std::string &name, std::string *outError = nullptr);

    /// <summary>
    /// 編集中の演出を保存する
    /// </summary>
    /// <param name="outError">失敗理由の格納先（不要なら nullptr）</param>
    /// <returns>bool: 成功したら true</returns>
    bool SaveSequence(std::string *outError = nullptr);

    /// <summary>
    /// イベントトラックの合図を受け取る関数を設定する。
    /// ゲーム側で「ここでHPを減らす」といった処理を挟むための入口
    /// </summary>
    /// <param name="callback">受け取る関数</param>
    void SetEventCallback(EventCallback callback) { eventCallback_ = std::move(callback); }

    /// <summary>
    /// 再生時にカメラトラックのカメラへ自動で切り替えるか
    /// </summary>
    void SetAutoActivateCamera(bool enabled) { autoActivateCamera_ = enabled; }
    bool IsAutoActivateCamera() const { return autoActivateCamera_; }

  private:
    /// ====================================
    /// private method
    /// ====================================

    /// 指定時刻の状態をすべてのトラックへ反映する
    void Apply(float previousTime, float time, bool fireEvents);

    /// オブジェクトのトランスフォームへ反映する
    void ApplyTransformTrack(const TimelineTrack &track, float time);

    /// カメラへ反映する
    void ApplyCameraTrack(const TimelineTrack &track, float time);

    /// 区間 (previousTime, time] を跨いだイベントを発火する
    void FireEvents(const TimelineTrack &track, float previousTime, float time);

    /// カメラトラックのカメラを表示用に切り替える
    void ActivateCameraTracks();

  private:
    /// ====================================
    /// private variables
    /// ====================================

    TimelineSequence sequence_;
    std::vector<std::string> sequenceNames_;

    bool playing_ = false;
    float time_ = 0.0f;
    float previousTime_ = 0.0f;
    bool autoActivateCamera_ = true;

    EventCallback eventCallback_;
};

} // namespace Hagine
