#pragma once
#include <chrono>
#include <string>
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

    std::vector<Sample> current_; // 当フレームの累積
    std::vector<Result> results_; // 表示用（EMA）
    int nextOrder_ = 0;

    bool enabled_ = true;

    double smoothedTotalMs_ = 0.0;
    double smoothedWallMs_ = 0.0;
    std::chrono::high_resolution_clock::time_point lastBegin_{};
    bool hasLastBegin_ = false;
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
