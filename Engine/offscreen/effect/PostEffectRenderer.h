#pragma once
#include "PostEffectChain.h"
#include "RendererBuffer.h"

/// @brief ポストエフェクトの描画を担当するクラス
/// PostEffectChainのスロット順にエフェクトをピンポンバッファで適用し
/// 最終結果をfinalResultTextureに書き込む
namespace Hagine {
class PostEffectRenderer
{
  public:
    void Initialize(DirectXCommon *pDxCommon, SrvManager *pSrvManager, PipelineManager *psoManager);

    /// @brief エフェクトチェーンを適用して描画する
    void Draw(PostEffectChain &effectChain, float deltaTime);

    /// @brief エフェクト適用のみ（バックバッファへのコピーなし）
    void DrawWithoutCopy(PostEffectChain &effectChain, float deltaTime);

    /// @brief finalResultへのUI合成パス開始（GENERIC_READ→RENDER_TARGET）
    void BeginCompositePass();

    /// @brief UI合成パス終了（RENDER_TARGET→GENERIC_READ）
    void EndCompositePass();

    /// @brief 前ステージの最終結果をオフスクリーンテクスチャに描画（マルチステージ用）
    void BlitToOffScreen(D3D12_GPU_DESCRIPTOR_HANDLE srcSrv);

    uint32_t GetFinalResultSrvIndex() const { return renderBuffer_.GetFinalResultSrvIndex(); }
    void CopyFinalResultToBackBuffer();

  private:
    /// @brief エフェクトなしで最終結果テクスチャに直接コピー
    void DrawToFinalResult();

    /// @brief 有効なエフェクトをピンポンバッファ上で順に適用し、最後に最終結果へ書き戻す
    void ApplyEffectChain(PostEffectChain &effectChain, const std::vector<int> &enabledIndices);

    /// @brief チェーンの出口。ピンポン(リニアFP16) → 最終結果(sRGB) へ写す
    /// @param srcPingPong 最後に書き込んだピンポンバッファのインデックス
    void ResolveChainToFinalResult(int srcPingPong);

    /// @brief コンピュートシェーダー版エフェクトを実行する
    /// @return 実行できたら true。CS未対応・生成失敗なら false（呼び出し側がPS版へフォールバック）
    bool DispatchComputeEffect(const EffectSlot &slot,
                               bool isFirstInput,
                               int inputPingPong,
                               int outputPingPong);

    /// @brief CS用の連続したSRVデスクリプタ領域を確保する
    void InitializeComputeSrvTable();

    /// @brief CSの入力指定に従って、SRVテーブルへデスクリプタをコピーする
    /// @return テーブル先頭のGPUハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE BuildComputeSrvTable(const std::vector<ComputeInput> &inputs,
                                                     bool isFirstInput,
                                                     int inputPingPong,
                                                     bool readFromScratch);

    /// @brief 1つのエフェクトを適用して描画する
    /// @param slot             適用するエフェクトスロット
    /// @param isFirstInput     trueならオフスクリーンバッファ、falseならピンポンバッファを入力とする
    /// @param inputPingPong    入力ピンポンバッファのインデックス
    /// @param outputRtvIndex   出力先(-2=最終結果, 0/1=ピンポンバッファ)
    void DrawSingleEffect(const EffectSlot &slot,
                          bool isFirstInput,
                          int inputPingPong,
                          int outputRtvIndex);

    DirectXCommon *pDxCommon_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;
    PipelineManager *pPsoManager_ = nullptr;
    RenderBuffer renderBuffer_;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};
    D3D12_CPU_DESCRIPTOR_HANDLE finalResultRtvHandle_{};

    // コンピュートシェーダー用のSRVテーブル。
    // デスクリプタテーブルはヒープ上で連続している必要があるため、専用の連続領域を確保しておく。
    //
    // 1つのテーブルを使い回すことはできない。ディスパッチはコマンドリストに積まれるだけで
    // 実行はあとなので、同じフレーム内で次のエフェクトがテーブルを書き換えると、
    // 先に積んだディスパッチまで書き換え後のデスクリプタを読んでしまう。
    // そのためテーブルをリング状に複数持ち、ディスパッチごとに次のテーブルへ進める。
    static constexpr uint32_t kComputeSrvTableSize = 4;   // 1テーブルあたりのSRV数（t0..t3）
    static constexpr uint32_t kComputeSrvTableCount = 64; // リングの長さ（数フレーム分の余裕）
    uint32_t computeSrvTableBaseIndex_ = 0;
    uint32_t computeSrvTableCursor_ = 0;
    bool computeSrvTableReady_ = false;
};
} // namespace Hagine
