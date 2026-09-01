#pragma once
#include "DirectXCommon.h"
#include <graphics/srv/SrvManager.h>

namespace Hagine {

/// <summary>
/// ポストエフェクト描画で使うバッファ群を管理するクラス
/// エフェクトを繰り返し適用するためのピンポンバッファと、最終結果テクスチャを保持する
/// </summary>
class RenderBuffer
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化（ピンポンバッファと最終結果テクスチャを生成）
    /// </summary>
    /// <param name="pDxCommon">DirectX共通処理</param>
    /// <param name="pSrvManager">SRVマネージャー</param>
    void Initialize(DirectXCommon *pDxCommon, SrvManager *pSrvManager);

    /// ===================================================
    /// ピンポンバッファ関連
    /// ===================================================

    /// <summary>
    /// 指定インデックスのピンポンバッファのRTVハンドルを取得
    /// </summary>
    /// <param name="index">バッファインデックス</param>
    /// <returns>D3D12_CPU_DESCRIPTOR_HANDLE: RTVハンドル</returns>
    D3D12_CPU_DESCRIPTOR_HANDLE GetPingPongRtvHandle(int index) const { return pingPongRtvHandles_[index]; }

    /// <summary>
    /// 指定インデックスのピンポンバッファのSRV(GPU)ハンドルを取得
    /// </summary>
    /// <param name="index">バッファインデックス</param>
    /// <returns>D3D12_GPU_DESCRIPTOR_HANDLE: SRVのGPUハンドル</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE GetPingPongSrvHandleGPU(int index) const { return pingPongSrvHandlesGPU_[index]; }

    /// <summary>
    /// 指定インデックスのピンポンバッファのリソースを取得
    /// </summary>
    /// <param name="index">バッファインデックス</param>
    /// <returns>ID3D12Resource: バッファリソース</returns>
    Microsoft::WRL::ComPtr<ID3D12Resource> GetPingPongResource(int index) const { return pingPongResources_[index]; }

    /// <summary>
    /// 指定インデックスのピンポンバッファのUAV(GPU)ハンドルを取得
    /// コンピュートシェーダー版エフェクトの書き込み先に使う
    /// </summary>
    /// <param name="index">バッファインデックス</param>
    /// <returns>D3D12_GPU_DESCRIPTOR_HANDLE: UAVのGPUハンドル</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE GetPingPongUavHandleGPU(int index) const { return pingPongUavHandlesGPU_[index]; }

    /// <summary>
    /// 指定インデックスのピンポンバッファのSRV(CPU)ハンドルを取得
    /// デスクリプタテーブルへコピーする用途で使う
    /// </summary>
    /// <param name="index">バッファインデックス</param>
    /// <returns>D3D12_CPU_DESCRIPTOR_HANDLE: SRVのCPUハンドル</returns>
    D3D12_CPU_DESCRIPTOR_HANDLE GetPingPongSrvHandleCPU(int index) const { return pingPongSrvHandlesCPU_[index]; }

    /// <summary>
    /// ピンポンバッファのフォーマット（リニア空間のFP16）
    /// </summary>
    static DXGI_FORMAT GetPingPongFormat() { return kPingPongFormat; }

    /// ===================================================
    /// コンピュートシェーダーの中間バッファ
    /// ===================================================

    /// <summary>
    /// 分離フィルタ（横→縦）のような複数パスCSの中間結果を置くバッファ。
    /// ピンポンは入力用と出力用で2枚とも埋まっているため、
    /// パス数が偶数のときに入力と出力が同じバッファになるのを避ける目的で用意している。
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12Resource> GetComputeScratchResource() const { return computeScratchResource_; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetComputeScratchSrvHandleCPU() const { return computeScratchSrvHandleCPU_; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetComputeScratchUavHandleGPU() const { return computeScratchUavHandleGPU_; }

    /// ===================================================
    /// 最終結果バッファ関連
    /// ===================================================

    /// <summary>
    /// 最終結果テクスチャのRTVハンドルを取得
    /// </summary>
    /// <returns>D3D12_CPU_DESCRIPTOR_HANDLE: RTVハンドル</returns>
    D3D12_CPU_DESCRIPTOR_HANDLE GetFinalResultRtvHandle() const { return finalResultRtvHandle_; }

    /// <summary>
    /// 最終結果テクスチャのSRV(GPU)ハンドルを取得
    /// </summary>
    /// <returns>D3D12_GPU_DESCRIPTOR_HANDLE: SRVのGPUハンドル</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE GetFinalResultSrvHandleGPU() const { return finalResultSrvHandleGPU_; }

    /// <summary>
    /// 最終結果テクスチャのリソースを取得
    /// </summary>
    /// <returns>ID3D12Resource: 最終結果リソース</returns>
    Microsoft::WRL::ComPtr<ID3D12Resource> GetFinalResultResource() const { return finalResultResource_; }

    /// <summary>
    /// 最終結果テクスチャのSRVインデックスを取得
    /// </summary>
    /// <returns>uint32_t: SRVインデックス</returns>
    uint32_t GetFinalResultSrvIndex() const { return finalResultSrvIndex_; }

    /// <summary>
    /// ピンポンバッファの数を取得
    /// </summary>
    /// <returns>const int: バッファ数</returns>
    const int GetPingPongBufferCount() const
    {
        return kPingPongBufferCount;
    }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// ピンポンバッファを生成
    /// </summary>
    void CreatePingPongBuffers();

    /// <summary>
    /// 最終結果テクスチャを生成
    /// </summary>
    void CreateFinalResultTexture();

    /// <summary>
    /// コンピュートシェーダーの中間バッファを生成
    /// </summary>
    void CreateComputeScratchTexture();

    /// ===================================================
    /// private variables
    /// ===================================================

    int currentPingPongBuffer_ = 0; // 現在の書き込み対象ピンポンバッファ

    DirectXCommon *pDxCommon_ = nullptr; // DirectX共通処理
    SrvManager *pSrvManager_ = nullptr;  // SRVマネージャー

    // ピンポンバッファのフォーマット。
    // リニア空間のFP16。sRGBだとUAVが作れず、8bitだとエフェクトを重ねたときに階調が落ちる。
    static constexpr DXGI_FORMAT kPingPongFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    static const int kPingPongBufferCount = 2; // ピンポンバッファの数
    Microsoft::WRL::ComPtr<ID3D12Resource> pingPongResources_[kPingPongBufferCount];
    uint32_t pingPongSrvIndices_[kPingPongBufferCount];
    uint32_t pingPongUavIndices_[kPingPongBufferCount];
    D3D12_CPU_DESCRIPTOR_HANDLE pingPongRtvHandles_[kPingPongBufferCount];
    D3D12_CPU_DESCRIPTOR_HANDLE pingPongSrvHandlesCPU_[kPingPongBufferCount];
    D3D12_GPU_DESCRIPTOR_HANDLE pingPongSrvHandlesGPU_[kPingPongBufferCount];
    D3D12_GPU_DESCRIPTOR_HANDLE pingPongUavHandlesGPU_[kPingPongBufferCount];

    Microsoft::WRL::ComPtr<ID3D12Resource> finalResultResource_; // 最終結果リソース
    uint32_t finalResultSrvIndex_;                               // 最終結果のSRVインデックス
    D3D12_CPU_DESCRIPTOR_HANDLE finalResultRtvHandle_;           // 最終結果のRTVハンドル
    D3D12_CPU_DESCRIPTOR_HANDLE finalResultSrvHandleCPU_;        // 最終結果のSRV(CPU)ハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE finalResultSrvHandleGPU_;        // 最終結果のSRV(GPU)ハンドル

    // 複数パスCSの中間バッファ（RTVは不要。CSが書いてCSが読むだけ）
    Microsoft::WRL::ComPtr<ID3D12Resource> computeScratchResource_;
    D3D12_CPU_DESCRIPTOR_HANDLE computeScratchSrvHandleCPU_{};
    D3D12_GPU_DESCRIPTOR_HANDLE computeScratchUavHandleGPU_{};
};
} // namespace Hagine
