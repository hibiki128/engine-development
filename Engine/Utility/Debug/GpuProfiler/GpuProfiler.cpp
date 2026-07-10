#include "GpuProfiler.h"
#include <DirectXCommon.h>
#ifdef USE_IMGUI
#include <cstdio>
#include <imgui.h>
#include <implot.h>
// Debugui_improved.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include "Utility/Debug/ImGui/Debugui_improved.h"
#endif

namespace Hagine {

GpuProfiler *GpuProfiler::GetInstance() {
    static GpuProfiler instance;
    return &instance;
}

void GpuProfiler::EnsureInit() {
    if (initialized_)
        return;

    dxCommon_ = DirectXCommon::GetInstance();
    if (!dxCommon_ || !dxCommon_->GetDevice())
        return;

    ID3D12Device *device = dxCommon_->GetDevice().Get();

    // タイムスタンプ用 QueryHeap（kRing フレーム分）
    D3D12_QUERY_HEAP_DESC qhd{};
    qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhd.Count = kTotalSlots;
    qhd.NodeMask = 0;
    if (FAILED(device->CreateQueryHeap(&qhd, IID_PPV_ARGS(&queryHeap_))))
        return;

    // Readback バッファ（resolve 先・CPU 読み取り）
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = static_cast<UINT64>(kTotalSlots) * sizeof(uint64_t);
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    // Direct/Compute の両キューが同一 readback を書くとクロスキュー同時アクセスに
    // なるため、キューごとに 1 本ずつ用意する。
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&readbackGraphics_))))
        return;
    readbackGraphics_->SetName(L"GpuProfiler_Readback_Graphics");
    readbackGraphics_->Map(0, nullptr, reinterpret_cast<void **>(&mappedGraphics_));

    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&readbackCompute_))))
        return;
    readbackCompute_->SetName(L"GpuProfiler_Readback_Compute");
    readbackCompute_->Map(0, nullptr, reinterpret_cast<void **>(&mappedCompute_));

    // キューごとのタイムスタンプ周波数（tick/秒）。Direct と Compute で異なりうる。
    if (auto *gq = dxCommon_->GetCommandQueue())
        gq->GetTimestampFrequency(&freqGraphics_);
    if (auto *cq = dxCommon_->GetComputeCommandQueue())
        cq->GetTimestampFrequency(&freqCompute_);

    initialized_ = true;
}

void GpuProfiler::BeginFrame() {
    EnsureInit();
    if (!initialized_)
        return;

    // ring を進める。これから使う ring には kRing フレーム前の結果が入っており、
    // GPU は既に完了している（in-flight は最大2フレーム）ので安全に読み戻せる。
    ringIndex_ = (ringIndex_ + 1) % kRing;
    pairCursor_ = 0;
    RingFrame &rf = rings_[ringIndex_];

    if (rf.valid && mappedGraphics_ && mappedCompute_) {
        results_.clear();
        const uint32_t ringBase = ringIndex_ * kSlotsPerRing;
        for (const auto &e : rf.entries) {
            const uint64_t *src = e.isCompute ? mappedCompute_ : mappedGraphics_;
            uint64_t t0 = src[ringBase + e.pair * 2];
            uint64_t t1 = src[ringBase + e.pair * 2 + 1];
            uint64_t freq = e.isCompute ? freqCompute_ : freqGraphics_;
            double ms = (freq > 0 && t1 >= t0) ? static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(freq) : 0.0;

            // 同ラベル（複数エミッターの同名パス）は合算する
            bool merged = false;
            for (auto &r : results_) {
                if (r.isCompute == e.isCompute && r.label == e.label) {
                    r.ms += ms;
                    merged = true;
                    break;
                }
            }
            if (!merged)
                results_.push_back({e.label, ms, e.isCompute});
        }
    }

    // このフレームの記録用にクリア。今フレームの resolve 後、kRing フレーム後に読み戻される。
    rf.entries.clear();
    rf.valid = true;
}

void GpuProfiler::Finalize() {
    if (!initialized_)
        return;

    // 永続マップしている readback は Unmap してから解放する。
    if (mappedGraphics_) {
        readbackGraphics_->Unmap(0, nullptr);
        mappedGraphics_ = nullptr;
    }
    if (mappedCompute_) {
        readbackCompute_->Unmap(0, nullptr);
        mappedCompute_ = nullptr;
    }
    readbackGraphics_.Reset();
    readbackCompute_.Reset();
    queryHeap_.Reset();

    for (auto &rf : rings_) {
        rf.entries.clear();
        rf.valid = false;
    }
    results_.clear();
    dxCommon_ = nullptr;
    initialized_ = false;
}

int GpuProfiler::Open(ID3D12GraphicsCommandList *cl, const char *label, bool isCompute) {
    if (!enabled_ || !initialized_ || !cl)
        return -1;
    if (pairCursor_ >= kMaxPairsPerFrame)
        return -1;

    uint32_t pair = pairCursor_++;
    uint32_t slot = ringIndex_ * kSlotsPerRing + pair * 2;
    cl->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot);

    RingFrame &rf = rings_[ringIndex_];
    int handle = static_cast<int>(rf.entries.size());
    rf.entries.push_back({label ? label : "?", pair, isCompute});
    return handle;
}

int GpuProfiler::OpenCompute(ID3D12GraphicsCommandList *cl, const char *label) {
    return Open(cl, label, true);
}

int GpuProfiler::OpenGraphics(ID3D12GraphicsCommandList *cl, const char *label) {
    return Open(cl, label, false);
}

void GpuProfiler::Close(ID3D12GraphicsCommandList *cl, int handle) {
    if (!enabled_ || !initialized_ || !cl || handle < 0)
        return;
    RingFrame &rf = rings_[ringIndex_];
    if (handle >= static_cast<int>(rf.entries.size()))
        return;
    uint32_t pair = rf.entries[handle].pair;
    uint32_t slot = ringIndex_ * kSlotsPerRing + pair * 2 + 1;
    cl->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot);
}

void GpuProfiler::Resolve(ID3D12GraphicsCommandList *cl, bool isCompute) {
    if (!enabled_ || !initialized_ || !cl)
        return;
    RingFrame &rf = rings_[ringIndex_];
    const uint32_t ringBase = ringIndex_ * kSlotsPerRing;
    for (const auto &e : rf.entries) {
        if (e.isCompute != isCompute)
            continue;
        uint32_t startSlot = ringBase + e.pair * 2;
        ID3D12Resource *dst = isCompute ? readbackCompute_.Get() : readbackGraphics_.Get();
        cl->ResolveQueryData(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                             startSlot, 2, dst,
                             static_cast<UINT64>(startSlot) * sizeof(uint64_t));
    }
}

void GpuProfiler::ResolveCompute(ID3D12GraphicsCommandList *cl) { Resolve(cl, true); }
void GpuProfiler::ResolveGraphics(ID3D12GraphicsCommandList *cl) { Resolve(cl, false); }

void GpuProfiler::DrawImGui() {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("GPU プロファイラ (パス別)"))
        return;

    const ImVec4 kCompute = DebugTheme::kAccentCyan;
    const ImVec4 kGraphics = DebugTheme::kAccentOrange;

    // ---- 計測トグル ----
    bool en = enabled_;
    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentGreen);
    if (ImGui::Checkbox("計測ON##gpuprof", &en))
        enabled_ = en;
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("(タイムスタンプ・3フレーム遅延)");

    // ---- 集計 ----
    double computeTotal = 0.0, graphicsTotal = 0.0, maxMs = 1e-6;
    for (const auto &r : results_) {
        (r.isCompute ? computeTotal : graphicsTotal) += r.ms;
        if (r.ms > maxMs)
            maxMs = r.ms;
    }
    double gpuTotal = computeTotal + graphicsTotal;

    // ---- 合計サマリー（色分け）----
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kGraphics);
    ImGui::Text("Graphics %.3f ms", graphicsTotal);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, kCompute);
    ImGui::Text(" / Compute %.3f ms", computeTotal);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text(" / GPU合計 %.3f ms", gpuTotal);

    // ---- 履歴グラフ（ImPlot: Graphics / Compute の ms 推移）----
    static const int kHist = 240;
    static float histCompute[kHist] = {};
    static float histGraphics[kHist] = {};
    static int histOff = 0;
    static float yMax = 4.0f;
    histGraphics[histOff] = static_cast<float>(graphicsTotal);
    histCompute[histOff] = static_cast<float>(computeTotal);
    histOff = (histOff + 1) % kHist;
    float curMax = static_cast<float>(gpuTotal) * 1.3f + 0.5f;
    yMax = (curMax > yMax) ? curMax : (yMax + (curMax - yMax) * 0.02f); // 緩やかに追従

    ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.05f, 0.05f, 0.07f, 1.0f));
    if (ImPlot::BeginPlot("##gpuHist", ImVec2(-1, 90),
                          ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoInputs |
                              ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes(nullptr, "ms",
                          ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoGridLines,
                          ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, kHist, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, yMax, ImPlotCond_Always);
        ImPlot::SetNextLineStyle(kGraphics, 1.5f);
        ImPlot::PlotLine("Graphics", histGraphics, kHist, 1.0, 0.0, ImPlotLineFlags_None, histOff);
        ImPlot::SetNextLineStyle(kCompute, 1.5f);
        ImPlot::PlotLine("Compute", histCompute, kHist, 1.0, 0.0, ImPlotLineFlags_None, histOff);
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleColor(2);

    // ---- パス別テーブル（占有バー付き）----
    ImGui::Spacing();
    SectionHeader("[ パス別 ]", DebugTheme::kAccentBlue);
    if (results_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("（計測データなし — 計測ON かつ数フレーム経過で表示されます）");
        ImGui::PopStyleColor();
    } else if (ImGui::BeginTable("##gpuprofTable", 4,
                                 ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("キュー", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("パス", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("占有", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableHeadersRow();

        for (const auto &r : results_) {
            const ImVec4 qc = r.isCompute ? kCompute : kGraphics;
            ImGui::TableNextRow();

            // キュー（色付きラベル）
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, qc);
            ImGui::TextUnformatted(r.isCompute ? "Compute" : "Graphics");
            ImGui::PopStyleColor();

            // パス名
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(r.label.c_str());

            // ms（数値そのものを右寄せの明るいテキストで＝小さい値も読みやすく）
            ImGui::TableSetColumnIndex(2);
            char num[24];
            snprintf(num, sizeof(num), "%.3f", r.ms);
            float tw = ImGui::CalcTextSize(num).x;
            float avail = ImGui::GetContentRegionAvail().x;
            if (avail > tw)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw));
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextReadOnly);
            ImGui::TextUnformatted(num);
            ImGui::PopStyleColor();

            // 占有バー（最大パス基準・キュー色。オーバーレイ文字は載せない）
            ImGui::TableSetColumnIndex(3);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, qc);
            ImGui::ProgressBar(static_cast<float>(r.ms / maxMs), ImVec2(-1.0f, 12.0f), "");
            ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
#endif
}
} // namespace Hagine
