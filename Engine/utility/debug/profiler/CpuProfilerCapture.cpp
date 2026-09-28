#include "CpuProfiler.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#ifdef USE_IMGUI
#include <imgui.h>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "utility/debug/imgui/DebugUIHelper.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#endif

// フレームの記録（重いフレームを後から見る）の表示と書き出し。
// 記録そのものは CpuProfiler.cpp（BeginFrame / RecordEvent / FinishFrameRecord）。

namespace Hagine {

namespace {
#ifdef USE_IMGUI
// ラベルごとに決まった色（同じ処理は毎回同じ色になる）
ImU32 LabelColor(const char *label, float alpha = 1.0f)
{
    size_t hash = std::hash<std::string>{}(label ? label : "");
    const float hue = static_cast<float>(hash % 360) / 360.0f;
    ImVec4 color = ImColor::HSV(hue, 0.45f, 0.72f);
    color.w = alpha;
    return ImGui::ColorConvertFloat4ToU32(color);
}
#endif // USE_IMGUI

constexpr float kBudgetMs = 1000.0f / 60.0f;
} // namespace

void CpuProfiler::SaveCaptureToFile() const
{
    // 重いフレームと直近のフレームを JSON に書き出す（あとで見比べる・人に渡す用）
    nlohmann::json root;
    auto toJson = [](const FrameRecord &r) {
        nlohmann::json frame;
        frame["index"] = r.index;
        frame["wallMs"] = r.wallMs;
        nlohmann::json events = nlohmann::json::array();
        for (const Event &e : r.events)
            events.push_back({{"label", e.label ? e.label : ""}, {"startMs", e.startMs}, {"ms", e.durationMs}, {"depth", e.depth}});
        frame["events"] = events;
        return frame;
    };
    root["spikes"] = nlohmann::json::array();
    for (const FrameRecord &r : spikes_)
        root["spikes"].push_back(toJson(r));
    root["recent"] = nlohmann::json::array();
    for (const FrameRecord &r : records_)
        root["recent"].push_back(toJson(r));

    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char name[64] = {};
    std::strftime(name, sizeof(name), "profile_%Y%m%d_%H%M%S.json", &local);
    std::error_code error;
    std::filesystem::create_directories("Captures", error);
    const std::string path = std::string("Captures/") + name;
    std::ofstream out(path);
    if (out.is_open())
        out << root.dump(2);
#ifdef USE_IMGUI
    ImGuiNotification::Post("プロファイルを書き出しました: " + path);
#endif
}

void CpuProfiler::DrawFrameCaptureImGui()
{
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("フレームの記録（重いフレームを後から見る）"))
        return;

    // ---- 操作 ----
    ImGui::Checkbox("記録する##fcRec", &recording_);
    ImGui::SameLine();
    ImGui::Checkbox("一時停止##fcFreeze", &frozen_);
    ImGui::SetItemTooltip("新しいフレームを入れず、今の記録をじっくり見る");
    ImGui::SameLine();
    ImGui::Checkbox("しきい値を自動##fcAuto", &autoThreshold_);
    ImGui::SetItemTooltip("直近のフレームの中央値の1.5倍より重いフレームを「重いフレーム」として取っておく");
    if (!autoThreshold_)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::DragFloat("##fcThreshold", &manualThresholdMs_, 0.1f, 1.0f, 500.0f, "%.1f ms");
    }
    const float threshold = GetSpikeThreshold();
    char thresholdText[32] = "(集計中)";
    if (threshold < 1.0e8f)
        std::snprintf(thresholdText, sizeof(thresholdText), "%.1f ms", threshold);
    ImGui::TextDisabled("しきい値 %s  /  重いフレーム %d 件  /  記録 %d フレーム", thresholdText,
                        static_cast<int>(spikes_.size()), static_cast<int>(records_.size()));

    // ---- 直近のフレームの棒グラフ（クリックで中身を見る）----
    {
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = 90.0f;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList *pDraw = ImGui::GetWindowDrawList();
        pDraw->AddRectFilled(origin, {origin.x + width, origin.y + height}, IM_COL32(18, 19, 24, 255), 3.0f);

        float maxMs = kBudgetMs * 1.5f;
        for (const FrameRecord &r : records_)
            maxMs = (std::max)(maxMs, r.wallMs);
        const float barW = width / static_cast<float>(kMaxRecords);
        const float scale = (height - 4.0f) / maxMs;

        // 予算（60fps）としきい値の線
        const float budgetY = origin.y + height - kBudgetMs * scale;
        pDraw->AddLine({origin.x, budgetY}, {origin.x + width, budgetY}, IM_COL32(200, 110, 110, 160));
        if (threshold < maxMs)
        {
            const float thY = origin.y + height - threshold * scale;
            pDraw->AddLine({origin.x, thY}, {origin.x + width, thY}, IM_COL32(220, 190, 90, 140));
        }

        const size_t offset = kMaxRecords - records_.size(); // 右詰め（新しいほど右）
        for (size_t i = 0; i < records_.size(); ++i)
        {
            const FrameRecord &r = records_[i];
            const float x0 = origin.x + (offset + i) * barW;
            const float y0 = origin.y + height - r.wallMs * scale;
            ImU32 color = IM_COL32(95, 150, 115, 255);
            if (r.wallMs > threshold)
                color = IM_COL32(210, 95, 95, 255);
            if (r.index == selectedFrame_)
                color = IM_COL32(240, 205, 90, 255);
            pDraw->AddRectFilled({x0, y0}, {x0 + (std::max)(barW - 1.0f, 1.0f), origin.y + height}, color);
        }

        ImGui::InvisibleButton("##fcBars", {width, height});
        if (ImGui::IsItemHovered() && !records_.empty())
        {
            const float mx = ImGui::GetIO().MousePos.x - origin.x;
            const int slot = static_cast<int>(mx / barW) - static_cast<int>(offset);
            if (slot >= 0 && slot < static_cast<int>(records_.size()))
            {
                const FrameRecord &r = records_[static_cast<size_t>(slot)];
                ImGui::SetTooltip("フレーム #%llu  %.2f ms\nクリックで中身を見る", static_cast<unsigned long long>(r.index), r.wallMs);
                if (ImGui::IsItemClicked())
                {
                    selectedFrame_ = r.index;
                    selectedIsSpike_ = false;
                    frozen_ = true; // 見ている間に流れていかないよう止める
                }
            }
        }
    }

    // ---- 重いフレームの一覧 ----
    ImGui::Spacing();
    SectionHeader("[ 重かったフレーム（新しい順）]", DebugTheme::kAccentRed);
    if (spikes_.empty())
    {
        DimText("まだありません（しきい値より重いフレームがあると、ここに残ります）");
    }
    else if (ImGui::BeginChild("##fcSpikes", {0.0f, 110.0f}, ImGuiChildFlags_Borders))
    {
        for (auto it = spikes_.rbegin(); it != spikes_.rend(); ++it)
        {
            // 一番外側（更新・描画などの大きな区切り）の中で、一番時間のかかった処理
            const Event *pHeaviest = nullptr;
            for (const Event &e : it->events)
            {
                if (e.depth > 0 && (!pHeaviest || e.durationMs > pHeaviest->durationMs))
                    pHeaviest = &e;
            }
            if (!pHeaviest)
            {
                for (const Event &e : it->events)
                {
                    if (!pHeaviest || e.durationMs > pHeaviest->durationMs)
                        pHeaviest = &e;
                }
            }
            char line[256] = {};
            std::snprintf(line, sizeof(line), "#%llu   %.2f ms   一番重い処理: %s (%.2f ms)##sp%llu",
                          static_cast<unsigned long long>(it->index), it->wallMs, pHeaviest ? pHeaviest->label : "-",
                          pHeaviest ? pHeaviest->durationMs : 0.0f, static_cast<unsigned long long>(it->index));
            if (ImGui::Selectable(line, it->index == selectedFrame_))
            {
                selectedFrame_ = it->index;
                selectedIsSpike_ = true;
            }
        }
    }
    if (!spikes_.empty())
        ImGui::EndChild();
    if (NeutralButton("一覧を消す##fcClear"))
    {
        spikes_.clear();
        selectedFrame_ = 0;
    }
    ImGui::SameLine();
    if (PrimaryButton("ファイルに書き出す##fcSave"))
        SaveCaptureToFile();
    ImGui::SetItemTooltip("重いフレームと直近のフレームを Captures/profile_日時.json へ書き出す");

    // ---- 選んだフレームの中身（帯グラフ）----
    const FrameRecord *pRecord = FindRecord(selectedFrame_);
    if (!pRecord)
    {
        DimText("棒グラフか一覧からフレームを選ぶと、中身を時間の帯で表示します");
        return;
    }
    ImGui::Spacing();
    SectionHeader("[ フレームの中身 ]", DebugTheme::kAccentYellow);
    ImGui::Text("フレーム #%llu   %.2f ms", static_cast<unsigned long long>(pRecord->index), pRecord->wallMs);
    ImGui::SameLine();
    ImGui::TextDisabled("（横=時間、下へ行くほど内側の処理。重なっていない所は計測していない処理か待ち）");

    int maxDepth = 0;
    float endMs = pRecord->wallMs;
    for (const Event &e : pRecord->events)
    {
        maxDepth = (std::max)(maxDepth, e.depth);
        endMs = (std::max)(endMs, e.startMs + e.durationMs);
    }
    const float rowH = 20.0f;
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = rowH * static_cast<float>(maxDepth + 1) + 18.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList *pDraw = ImGui::GetWindowDrawList();
    pDraw->AddRectFilled(origin, {origin.x + width, origin.y + height}, IM_COL32(18, 19, 24, 255), 3.0f);
    const float scale = width / (std::max)(endMs, 0.001f);

    // 目盛り（5ms ごと）
    for (float t = 0.0f; t <= endMs; t += 5.0f)
    {
        const float x = origin.x + t * scale;
        pDraw->AddLine({x, origin.y}, {x, origin.y + height - 16.0f}, IM_COL32(40, 42, 50, 255));
        char tick[16] = {};
        std::snprintf(tick, sizeof(tick), "%.0f", t);
        pDraw->AddText({x + 2.0f, origin.y + height - 15.0f}, IM_COL32(120, 125, 135, 255), tick);
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const Event *pHovered = nullptr;
    for (const Event &e : pRecord->events)
    {
        const float x0 = origin.x + e.startMs * scale;
        const float x1 = (std::max)(x0 + 1.0f, origin.x + (e.startMs + e.durationMs) * scale);
        const float y0 = origin.y + e.depth * rowH;
        const float y1 = y0 + rowH - 2.0f;
        pDraw->AddRectFilled({x0, y0}, {x1, y1}, LabelColor(e.label), 2.0f);
        if (x1 - x0 > 40.0f)
        {
            // 帯に入るぶんだけ名前を書く
            char text[128] = {};
            std::snprintf(text, sizeof(text), "%s %.2f", e.label, e.durationMs);
            pDraw->PushClipRect({x0 + 2.0f, y0}, {x1 - 2.0f, y1}, true);
            pDraw->AddText({x0 + 4.0f, y0 + 2.0f}, IM_COL32(15, 15, 20, 255), text);
            pDraw->PopClipRect();
        }
        if (mouse.x >= x0 && mouse.x <= x1 && mouse.y >= y0 && mouse.y <= y1)
            pHovered = &e;
    }
    ImGui::InvisibleButton("##fcFlame", {width, height});
    if (pHovered && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s\n%.3f ms（%.2f ms から）", pHovered->label, pHovered->durationMs, pHovered->startMs);
    }

    // この中で時間のかかった処理（同じ名前は合算）
    std::map<std::string, float> totals;
    for (const Event &e : pRecord->events)
        totals[e.label] += e.durationMs;
    std::vector<std::pair<std::string, float>> sorted(totals.begin(), totals.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    if (ImGui::BeginTable("##fcTop", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("時間のかかった処理（外側の処理は内側の時間も含む）");
        ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < sorted.size() && i < 10; ++i)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(sorted[i].first.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", sorted[i].second);
        }
        ImGui::EndTable();
    }
#endif // USE_IMGUI
}

} // namespace Hagine
