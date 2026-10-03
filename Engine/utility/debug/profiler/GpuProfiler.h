#pragma once
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>

namespace Hagine {
class DirectXCommon;

/// <summary>
/// GPU タイムスタンプによる軽量プロファイラ。
///
/// Direct(graphics) キューと Compute キューは別タイムライン・別周波数のため、
/// スパンごとにどちらのキューかを保持し、対応する GetTimestampFrequency で ms 換算する。
///
/// 使い方（1フレームの流れ）:
///   BeginFrame()                                   // 先頭で1回（ringを進めて3F前の結果を取り込む）
///   int h = OpenCompute(computeCl, "Update");      // computeリストに開始タイムスタンプ
///   ... dispatch ...
///   Close(computeCl, h);                           // 終了タイムスタンプ
///   ResolveCompute(computeCl);                     // Execute 前に compute スパンを resolve
///   ResolveGraphics(graphicsCl);                   // graphics リスト Close 前に resolve
///
/// QueryHeap は kRing(3) フレーム分を確保し、書き込んだフレームの結果は
/// kRing フレーム後（GPU 完了保証後）に読み戻すため CPU/GPU を待たせない。
/// </summary>
class GpuProfiler
{
  public:
    static GpuProfiler *GetInstance();

    /// フレーム先頭で呼ぶ。ring を進め、3F前に記録したスパンの結果を取り込む。
    void BeginFrame();

    /// Compute キュー用スパン開始。戻り値はハンドル（Close に渡す）。-1=無効/上限超過。
    int OpenCompute(ID3D12GraphicsCommandList *pCommandList, const char *label);
    /// Graphics キュー用スパン開始。
    int OpenGraphics(ID3D12GraphicsCommandList *pCommandList, const char *label);
    /// スパン終了。Open の戻り値ハンドルを渡す。
    void Close(ID3D12GraphicsCommandList *pCommandList, int handle);

    /// Compute リストが閉じる前（ExecuteComputeCommands 前）に呼ぶ。
    void ResolveCompute(ID3D12GraphicsCommandList *pCommandList);
    /// Graphics リストが閉じる前（PostDraw 前）に呼ぶ。
    void ResolveGraphics(ID3D12GraphicsCommandList *pCommandList);

    /// ImGui 表示（ラベル別 ms とキュー合計）。
    void DrawImGui();

    /// ラベル別 ms を「名前 ms」の1行ずつにまとめる（ログへ書き出して比べる用）。
    std::string FormatSummary() const;

    /// <summary>
    /// 1フレームぶんの描画統計（GPU が数えた値。Direct キューの分だけ）
    /// </summary>
    struct FrameStats
    {
        uint64_t vertices = 0;          ///< 読み込んだ頂点の数
        uint64_t primitives = 0;        ///< 組み立てた三角形（線・点も含む）の数
        uint64_t drawnPrimitives = 0;   ///< 裁ち落とし・裏面カリングを通って実際に描いた数
        uint64_t pixels = 0;            ///< ピクセルシェーダーを走らせた回数（塗った画素の延べ数）
        uint64_t computeThreads = 0;    ///< コンピュートシェーダーのスレッド数（Direct キュー分）
        bool valid = false;             ///< 読み戻せたか
    };

    /// <summary>描画の始め（Direct リストへ記録を始めた直後）に呼ぶ</summary>
    void BeginPipelineStats(ID3D12GraphicsCommandList *pCommandList);

    /// <summary>描画の終わり（Direct リストを閉じる前）に呼ぶ</summary>
    void EndPipelineStats(ID3D12GraphicsCommandList *pCommandList);

    /// <summary>直近に読み戻せたフレームの描画統計（3フレーム遅れ）</summary>
    const FrameStats &GetFrameStats() const { return frameStats_; }

    /// <summary>描画統計の表示（統計の窓などから呼ぶ）</summary>
    void DrawFrameStatsImGui();

    void SetEnabled(bool e) { enabled_ = e; }
    bool IsEnabled() const { return enabled_; }

    /// <summary>
    /// 持っているGPUリソースを解放する（アプリの終了処理から呼ぶ）。
    /// シングルトンは静的領域にあるので、ここで手放さないと
    /// リークチェックが走る時点までクエリヒープと読み戻しバッファが残り続ける
    /// </summary>
    void Finalize();

  private:
    GpuProfiler() = default;
    ~GpuProfiler() = default;
    GpuProfiler(const GpuProfiler &) = delete;
    GpuProfiler &operator=(const GpuProfiler &) = delete;

    void EnsureInit();
    int Open(ID3D12GraphicsCommandList *pCommandList, const char *label, bool isCompute);
    void Resolve(ID3D12GraphicsCommandList *pCommandList, bool isCompute);

    static constexpr uint32_t kRing = 3;                             // リングバッファ段数（in-flight 2F + 余裕1）
    static constexpr uint32_t kMaxPairsPerFrame = 64;                // 1フレームに記録できるスパン上限
    static constexpr uint32_t kSlotsPerRing = kMaxPairsPerFrame * 2; // begin/end で2スロット
    static constexpr uint32_t kTotalSlots = kRing * kSlotsPerRing;

    struct Entry
    {
        std::string label;
        uint32_t pair;  // ring 内ペアindex
        bool isCompute; // どちらのキューで記録したか
    };
    struct RingFrame
    {
        std::vector<Entry> entries;
        bool valid = false; // 一度でも記録・resolve したか（最初の数フレームの読み戻し抑止）
    };

    struct Result
    {
        std::string label;
        double ms;
        bool isCompute;
    };

    // ラベル別 ms（表示用、読み戻しのたび再構築）
    std::vector<Result> results_;

    RingFrame rings_[kRing];
    uint32_t ringIndex_ = 0;
    uint32_t pairCursor_ = 0; // 当該フレームの次の空きペア

    uint64_t freqGraphics_ = 0;
    uint64_t freqCompute_ = 0;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
    // readback はキューごとに分ける。1本を Direct/Compute 両キューから書くと
    // クロスキュー同時アクセス扱いになりデバッグレイヤーが停止する（実際の競合）。
    Microsoft::WRL::ComPtr<ID3D12Resource> readbackGraphics_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readbackCompute_;
    uint64_t *pMappedGraphics_ = nullptr;
    uint64_t *pMappedCompute_ = nullptr;

    // ---- 描画統計（パイプライン統計クエリ。1フレーム1つを kRing 段）----
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> statsHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> statsReadback_;
    D3D12_QUERY_DATA_PIPELINE_STATISTICS *pMappedStats_ = nullptr;
    bool statsWritten_[kRing] = {}; // その段に結果を書いたか
    bool statsOpen_ = false;        // このフレームの計測中か
    FrameStats frameStats_;

    DirectXCommon *pDxCommon_ = nullptr;
    bool enabled_ = true;
    bool initialized_ = false;
};
} // namespace Hagine
