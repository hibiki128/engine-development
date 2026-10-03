#pragma once
#include <chrono>
#include <deque>
#include <string>
#include <thread>
#include <vector>

namespace Hagine {

/// <summary>
/// CPU フレーム内訳プロファイラ（軽量スコープ計測）。
///
/// GpuProfiler が GPU タイムスタンプでパス別 ms を測るのに対し、こちらは
/// CPU 側の各フェーズ（更新・アニメ・衝突・ImGui 構築・描画コマンド記録・
/// present 待ち 等）の実時間を std::chrono で測る。
///
/// Debug ビルドの重さは大抵 CPU バウンド（MSVC デバッグ STL / 非インライン /
/// イテレータデバッグ）なので、まずどのフェーズが 16.6ms 予算を食っているかを
/// ここで特定する。
///
/// 使い方（1フレームの流れ）:
///   CpuProfiler::GetInstance()->BeginFrame();   // フレーム先頭で1回
///   {
///     HAGINE_CPU_PROFILE("Update/Objects");     // このスコープの実時間を計測
///     ... 処理 ...
///   }
///   CpuProfiler::GetInstance()->DrawImGui();     // 任意のImGui窓で表示
///
/// 同名ラベルは1フレーム内で合算される。表示値はフレーム間で指数移動平均を掛けて
/// ちらつきを抑える。Release ビルドでは HAGINE_CPU_PROFILE は何もしない。
/// </summary>
class CpuProfiler
{
  public:
    static CpuProfiler *GetInstance();

    /// フレーム先頭で呼ぶ。前フレームの累積を表示用へ確定し、当フレームの累積を空にする。
    void BeginFrame();

    /// スコープ計測の結果を加算（CpuProfileScope のデストラクタから呼ばれる）。
    void Accumulate(const char *label, double ms);

    /// ImGui 表示（フェーズ別 ms・占有バー・履歴グラフ）。
    void DrawImGui();

    void SetEnabled(bool e) { enabled_ = e; }
    bool IsEnabled() const { return enabled_; }

    /// 表示用: 計測フェーズの合計 ms（present 待ちを含む全スコープの和）。
    double GetMeasuredTotalMs() const { return smoothedTotalMs_; }
    /// 表示用: BeginFrame 間の実フレーム時間 ms（present 待ち込み）。
    double GetFrameWallMs() const { return smoothedWallMs_; }

    /// 区間ごとの ms（均した値）を「名前 ms」の1行ずつにまとめる（ログへ書き出して比べる用）。
    std::string FormatSummary() const;

    /// スコープの開始・終了を記録する（CpuProfileScope から呼ばれる。フレームの記録用）
    void RecordEvent(const char *label, std::chrono::high_resolution_clock::time_point begin,
                     std::chrono::high_resolution_clock::time_point end, int depth);

    /// ImGui 表示: フレームの記録（直近のフレームの棒グラフ・重いフレームの一覧・1フレームの中身の帯グラフ）
    void DrawFrameCaptureImGui();

  private:
    CpuProfiler() = default;
    ~CpuProfiler() = default;
    CpuProfiler(const CpuProfiler &) = delete;
    CpuProfiler &operator=(const CpuProfiler &) = delete;

    struct Sample
    {
        std::string label;
        double ms = 0.0; // 当フレーム累積
        int order = 0;   // フレーム内で初めて現れた順
    };
    struct Result
    {
        std::string label;
        double ms = 0.0; // 指数移動平均済み
        int order = 0;
    };

    /// <summary>1フレームの中の1区間（HAGINE_CPU_PROFILE 1回ぶん）</summary>
    struct Event
    {
        const char *label = nullptr; // マクロに渡した文字列リテラル
        float startMs = 0.0f;        // フレームの頭からの開始時刻
        float durationMs = 0.0f;     // かかった時間
        int depth = 0;               // 入れ子の深さ（0が一番外）
    };
    /// <summary>記録した1フレーム</summary>
    struct FrameRecord
    {
        uint64_t index = 0;        // 通し番号
        float wallMs = 0.0f;       // フレーム全体の実時間（present 待ち込み）
        std::vector<Event> events; // 中の区間
    };

    void FinishFrameRecord(float wallMs);
    const FrameRecord *FindRecord(uint64_t index) const;
    float GetSpikeThreshold() const;
    void SaveCaptureToFile() const;

    std::vector<Sample> current_; // 当フレームの累積
    std::vector<Result> results_; // 表示用（EMA）
    int nextOrder_ = 0;

    bool enabled_ = true;

    double smoothedTotalMs_ = 0.0;
    double smoothedWallMs_ = 0.0;
    std::chrono::high_resolution_clock::time_point lastBegin_{};
    bool hasLastBegin_ = false;

    // ---- フレームの記録 ----
    static constexpr size_t kMaxRecords = 300; // 直近これだけのフレームを持つ
    static constexpr size_t kMaxSpikes = 30;   // 重いフレームはこれだけ別に取っておく
    std::thread::id mainThread_{};             // 記録するのはフレームを回しているスレッドだけ
    std::vector<Event> currentEvents_;         // 今のフレームの区間
    std::deque<FrameRecord> records_;          // 直近のフレーム
    std::deque<FrameRecord> spikes_;           // 重かったフレーム（古い順）
    uint64_t frameCounter_ = 0;
    bool recording_ = true;           // 記録するか
    bool frozen_ = false;             // 一時停止（今の記録を見るために新しいフレームを入れない）
    bool autoThreshold_ = true;       // しきい値を直近の中央値から自動で決める
    float manualThresholdMs_ = 25.0f; // 手動のしきい値
    uint64_t selectedFrame_ = 0;      // 詳しく見るフレームの通し番号（0 は未選択）
    bool selectedIsSpike_ = false;    // 選んだのが重いフレームの一覧からか
};

/// <summary>
/// RAII スコープ計測。ctor で開始、dtor で経過を CpuProfiler へ加算する。
/// 直接使わず HAGINE_CPU_PROFILE マクロ経由で使う。
/// </summary>
struct CpuProfileScope
{
    const char *pLabel_;
    std::chrono::high_resolution_clock::time_point t0_;
    explicit CpuProfileScope(const char *label);
    ~CpuProfileScope();
    CpuProfileScope(const CpuProfileScope &) = delete;
    CpuProfileScope &operator=(const CpuProfileScope &) = delete;
};

} // namespace Hagine

// ---- スコープ計測マクロ（Release では無効化して完全ノーコスト）----
#define HAGINE_CPU_CONCAT_INNER(a, b) a##b
#define HAGINE_CPU_CONCAT(a, b) HAGINE_CPU_CONCAT_INNER(a, b)
#ifdef USE_IMGUI
#define HAGINE_CPU_PROFILE(name) ::Hagine::CpuProfileScope HAGINE_CPU_CONCAT(hagineCpuScope_, __LINE__)(name)
#else
#define HAGINE_CPU_PROFILE(name) ((void)0)
#endif
