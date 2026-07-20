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
    int OpenCompute(ID3D12GraphicsCommandList *cl, const char *label);
    /// Graphics キュー用スパン開始。
    int OpenGraphics(ID3D12GraphicsCommandList *cl, const char *label);
    /// スパン終了。Open の戻り値ハンドルを渡す。
    void Close(ID3D12GraphicsCommandList *cl, int handle);

    /// Compute リストが閉じる前（ExecuteComputeCommands 前）に呼ぶ。
    void ResolveCompute(ID3D12GraphicsCommandList *cl);
    /// Graphics リストが閉じる前（PostDraw 前）に呼ぶ。
    void ResolveGraphics(ID3D12GraphicsCommandList *cl);

    /// ImGui 表示（ラベル別 ms とキュー合計）。
    void DrawImGui();

    void SetEnabled(bool e) { enabled_ = e; }
    bool IsEnabled() const { return enabled_; }

  private:
    GpuProfiler() = default;
    ~GpuProfiler() = default;
    GpuProfiler(const GpuProfiler &) = delete;
    GpuProfiler &operator=(const GpuProfiler &) = delete;

    void EnsureInit();
    int Open(ID3D12GraphicsCommandList *cl, const char *label, bool isCompute);
    void Resolve(ID3D12GraphicsCommandList *cl, bool isCompute);

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

    DirectXCommon *pDxCommon_ = nullptr;
    bool enabled_ = true;
    bool initialized_ = false;
};
} // namespace Hagine
