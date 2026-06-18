#pragma once
#include "PostEffectChain.h"
#include "RendererBuffer.h"

/// @brief ポストエフェクトの描画を担当するクラス
/// PostEffectChainのスロット順にエフェクトをピンポンバッファで適用し
/// 最終結果をfinalResultTextureに書き込む
namespace Hagine {
class PostEffectRenderer {
  public:
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager, PipeLineManager *psoManager);

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

    /// @brief 1つのエフェクトを適用して描画する
    /// @param slot             適用するエフェクトスロット
    /// @param isFirstInput     trueならオフスクリーンバッファ、falseならピンポンバッファを入力とする
    /// @param inputPingPong    入力ピンポンバッファのインデックス
    /// @param outputRtvIndex   出力先(-2=最終結果, 0/1=ピンポンバッファ)
    void DrawSingleEffect(const EffectSlot &slot,
                          bool isFirstInput,
                          int inputPingPong,
                          int outputRtvIndex);

    DirectXCommon *dxCommon_   = nullptr;
    SrvManager *srvManager_    = nullptr;
    PipeLineManager *psoManager_ = nullptr;
    RenderBuffer renderBuffer_;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};
    D3D12_CPU_DESCRIPTOR_HANDLE finalResultRtvHandle_{};
};
} // namespace Hagine
