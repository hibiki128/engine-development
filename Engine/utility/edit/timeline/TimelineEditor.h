#pragma once
#ifdef USE_IMGUI
#include "TimelineManager.h"
#include <imgui.h>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// タイムライン演出のエディタ。
/// カメラ・オブジェクト・音・パーティクルを1本の時間軸に並べて演出を組む。
///
/// 数値を直接打たなくても作れるよう、
/// 「今のオブジェクトの位置をキーにする」ボタンを中心に据えてある。
/// ギズモで置いてボタンを押す、を繰り返せば動きができあがる。
/// </summary>
class TimelineEditor
{
  private:
    TimelineEditor() = default;
    ~TimelineEditor() = default;
    TimelineEditor(const TimelineEditor &) = delete;
    TimelineEditor &operator=(const TimelineEditor &) = delete;

  public:
    /// ====================================
    /// public method
    /// ====================================

    /// <summary>シングルトンインスタンスの取得</summary>
    static TimelineEditor *GetInstance();

    /// <summary>
    /// ウィンドウを描画する
    /// </summary>
    /// <param name="open">表示フラグ。閉じるボタンで false になる</param>
    void Draw(bool *open);

  private:
    /// ====================================
    /// private structures
    /// ====================================

    /// 今つまんでいるもの
    enum class DragTarget
    {
        None,
        Key,
        Event,
        Playhead,
    };

    /// ====================================
    /// private method
    /// ====================================

    /// メニューバー（新規・保存・読み込み）
    void DrawMenuBar();

    /// 再生と全体設定の列
    void DrawTransport();

    /// 左側のトラック一覧
    void DrawTrackList();

    /// 右側の時間軸グリッド
    void DrawTimelineGrid();

    /// 選択中のキー／イベントの詳細
    void DrawInspector();

    /// 対象オブジェクト・カメラを選ばせる
    void DrawTargetSelector(TimelineTrack &track);

    /// <summary>
    /// 対象の今の姿勢を、指定時刻のキーとして取り込む
    /// </summary>
    /// <param name="trackIndex">対象トラック</param>
    /// <param name="time">キーを置く時刻</param>
    void CaptureKeyFromTarget(int trackIndex, float time);

    /// 選択中のキー／イベントを消す
    void DeleteSelection();

    /// スナップ（Shift を押している間は反転）を掛けた時刻
    float SnapTime(float time) const;

    /// タイムライン上のショートカット（Space / Home / End / ← →）
    void HandleShortcuts();

  private:
    /// ====================================
    /// private variables
    /// ====================================

    int selectedTrack_ = -1;
    int selectedKey_ = -1;
    int selectedEvent_ = -1;

    float pixelsPerSecond_ = 120.0f;
    bool snapEnabled_ = true; // キー・再生位置を刻みに吸着させる
    float snapStep_ = 0.1f;   // 刻み（秒）
    // キーの右クリックメニューの対象
    int menuTrack_ = -1;
    int menuKey_ = -1;
    int menuEvent_ = -1;
    float trackRowHeight_ = 26.0f;

    DragTarget dragTarget_ = DragTarget::None;
    int dragTrack_ = -1;
    int dragIndex_ = -1;

    std::string sequenceNameBuffer_ = "NewTimeline";
    std::string statusMessage_;
    float statusTimer_ = 0.0f;

    bool initialized_ = false;
};

} // namespace Hagine
#endif // USE_IMGUI
