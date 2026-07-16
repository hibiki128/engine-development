#include "CpuProfiler.h"
#ifdef USE_IMGUI
#include <cstdio>
#include <imgui.h>
#include <implot.h>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "utility/debug/imgui/DebugUIHelper.h"
#endif

namespace Hagine {

CpuProfiler *CpuProfiler::GetInstance()
{
    static CpuProfiler instance;
    return &instance;
}

void CpuProfiler::BeginFrame()
{
    using clock = std::chrono::high_resolution_clock;
    const clock::time_point now = clock::now();

    // ---- 実フレーム時間（BeginFrame 間隔・present 待ち込み）----
    if (hasLastBegin_)
    {
        const double wallMs = std::chrono::duration<double, std::milli>(now - lastBegin_).count();
        smoothedWallMs_ = (smoothedWallMs_ <= 0.0) ? wallMs : smoothedWallMs_ * 0.85 + wallMs * 0.15;
    }
    lastBegin_ = now;
    hasLastBegin_ = true;

    // ---- 前フレームの累積を表示用（EMA）へ確定 ----
    double measuredTotal = 0.0;
    std::vector<Result> next;
    next.reserve(current_.size());
    for (const auto &s : current_)
    {
        measuredTotal += s.ms;
        // 直前フレームの同ラベル値を引いて指数移動平均
        double prev = -1.0;
        for (const auto &r : results_)
        {
            if (r.label == s.label)
            {
                prev = r.ms;
                break;
            }
        }
        const double smoothed = (prev < 0.0) ? s.ms : prev * 0.85 + s.ms * 0.15;
        next.push_back({s.label, smoothed, s.order});
    }
    // order（フレーム内の実行順）で安定ソート
    for (size_t i = 1; i < next.size(); ++i)
    {
        Result key = next[i];
        size_t j = i;
        while (j > 0 && next[j - 1].order > key.order)
        {
            next[j] = next[j - 1];
            --j;
        }
        next[j] = key;
    }
    results_.swap(next);

    smoothedTotalMs_ = (smoothedTotalMs_ <= 0.0) ? measuredTotal : smoothedTotalMs_ * 0.85 + measuredTotal * 0.15;

    // ---- 当フレームの累積を空にする ----
    current_.clear();
    nextOrder_ = 0;
}

void CpuProfiler::Accumulate(const char *label, double ms)
{
    if (!enabled_ || !label)
        return;
    for (auto &s : current_)
    {
        if (s.label == label)
        {
            s.ms += ms;
            return;
        }
    }
    current_.push_back({label, ms, nextOrder_++});
}

CpuProfileScope::CpuProfileScope(const char *label)
    : pLabel_(label), t0_(std::chrono::high_resolution_clock::now()) {}

CpuProfileScope::~CpuProfileScope()
{
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::high_resolution_clock::now() - t0_)
                          .count();
    CpuProfiler::GetInstance()->Accumulate(pLabel_, ms);
}

void CpuProfiler::DrawImGui()
{
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("CPU プロファイラ (フェーズ別)"))
        return;

    // ---- 計測トグル ----
    bool en = enabled_;
    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
    if (ImGui::Checkbox("計測ON##cpuprof", &en))
        enabled_ = en;
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("(実時間・present待ち含む全スコープ)");

    // ---- サマリー: 予算(16.67ms) との比較 ----
    const double kBudgetMs = 1000.0 / 60.0; // 60fps 予算
    const double wall = smoothedWallMs_;
    const bool over = wall > kBudgetMs + 0.1;

    ImGui::Spacing();
    ImVec4 wallColor = over ? DebugTheme::kAccentRed : DebugTheme::kAccentGreen;
    ImGui::PushStyleColor(ImGuiCol_Text, wallColor);
    ImGui::Text("フレーム %.2f ms (%.0f fps)", wall, wall > 0.0 ? 1000.0 / wall : 0.0);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled(" / 予算 %.2f ms", kBudgetMs);
    ImGui::SameLine();
    ImGui::Text(" / 計測合計 %.2f ms", smoothedTotalMs_);

    // ---- 履歴グラフ（計測合計 ms の推移。予算ラインも引く）----
    static const int kHist = 240;
    static float hist[kHist] = {};
    static int histOff = 0;
    static float yMax = 20.0f;
    hist[histOff] = static_cast<float>(smoothedTotalMs_);
    histOff = (histOff + 1) % kHist;
    float curMax = static_cast<float>(smoothedTotalMs_) * 1.3f + 2.0f;
    yMax = (curMax > yMax) ? curMax : (yMax + (curMax - yMax) * 0.02f);

    ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.05f, 0.05f, 0.07f, 1.0f));
    if (ImPlot::BeginPlot("##cpuHist", ImVec2(-1, 90),
                          ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoInputs |
                              ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText))
    {
        ImPlot::SetupAxes(nullptr, "ms",
                          ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoGridLines,
                          ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, kHist, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, yMax, ImPlotCond_Always);
        // 60fps 予算ライン
        double budgetX[2] = {0, kHist};
        double budgetY[2] = {kBudgetMs, kBudgetMs};
        ImPlot::SetNextLineStyle(DebugTheme::kAccentRed, 1.0f);
        ImPlot::PlotLine("budget", budgetX, budgetY, 2);
        ImPlot::SetNextLineStyle(DebugTheme::kAccentGreen, 1.5f);
        ImPlot::PlotLine("cpu", hist, kHist, 1.0, 0.0, ImPlotLineFlags_None, histOff);
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleColor(2);

    // ---- フェーズ別テーブル（占有バー付き）----
    ImGui::Spacing();
    SectionHeader("[ フェーズ別 ]", DebugTheme::kAccentBlue);
    if (results_.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("（計測データなし — 計測ON かつ数フレーム経過で表示されます）");
        ImGui::PopStyleColor();
    }
    else
    {
        double maxMs = 1e-6;
        for (const auto &r : results_)
        {
            if (r.ms > maxMs)
                maxMs = r.ms;
        }

        if (ImGui::BeginTable("##cpuprofTable", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("フェーズ", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("占有", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableHeadersRow();

            for (const auto &r : results_)
            {
                ImGui::TableNextRow();

                // フェーズ名
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(r.label.c_str());

                // ms（右寄せ）
                ImGui::TableSetColumnIndex(1);
                char num[24];
                snprintf(num, sizeof(num), "%.3f", r.ms);
                float tw = ImGui::CalcTextSize(num).x;
                float avail = ImGui::GetContentRegionAvail().x;
                if (avail > tw)
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw));
                ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextReadOnly);
                ImGui::TextUnformatted(num);
                ImGui::PopStyleColor();

                // 占有バー（最大フェーズ基準）
                ImGui::TableSetColumnIndex(2);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, DebugTheme::kAccentBlue);
                ImGui::ProgressBar(static_cast<float>(r.ms / maxMs), ImVec2(-1.0f, 12.0f), "");
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextWrapped("※「待ち(VSync/GPU)」が大きくCPUフェーズ計が予算内なら描画(GPU)/VSync待ち。"
                           "CPUフェーズ計だけで予算超過ならCPUバウンド。");
        ImGui::PopStyleColor();
    }
#endif
}

} // namespace Hagine
