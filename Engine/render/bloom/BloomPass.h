#pragma once
#include "d3d12.h"
#include "wrl.h"
#include <cstdint>
#include <string>

namespace Hagine {
class DirectXCommon;
class SrvManager;

/// <summary>
/// ミップ列方式のブルーム（COD:AW / Jimenez の手法）。
///
/// 旧 `OffScreen/Bloom.CS.hlsl`（ポストエフェクト版）は全解像度のまま
/// ±2画素の5タップでぼかしていたので、実際には「光の縁がうっすら滲む」程度で、
/// ブルームというよりごく狭いグローだった。しかも輝度のハードカットだったため、
/// しきい値をまたぐ画素が点滅してチラついた。
///
/// こちらは
///   1. プリフィルタ  … ソフトニーで抽出しながら半解像度へ（Karis平均で蛍を抑制）
///   2. 縮小を数段    … 段を下るほど滲みの半径が倍々に広がる
///   3. 拡大して加算  … 下から順にテントフィルタで広げて積み上げる
///   4. 合成          … シーンへ加算（トーンマップの手前）
/// という構成で、近くは締まり遠くへなだらかに広がる本来のブルームになる。
///
/// **パーティクルだけを光らせるモード**を持つ。
/// パーティクル描画の直前にシーンの控えを取り、描画後との差を取ることで
/// 「そのフレームにパーティクルが足した色」だけを抽出する。
/// 加算ブレンドの粒子はそのまま足し算なので、差が寄与そのものになる。
///
/// さらに、その「パーティクルだけのミップ」をマスクに使って
/// **歪み（熱揺らぎ）**も掛けられる。素材が1枚も要らず、
/// ミップが縮小済みでぼけているぶんマスクが自然に外側へ広がる。
/// </summary>
class BloomPass
{
  private:
    BloomPass() = default;
    ~BloomPass() = default;
    BloomPass(const BloomPass &) = delete;
    BloomPass &operator=(const BloomPass &) = delete;

  public:
    /// ====================================
    /// public method
    /// ====================================

    /// <summary>シングルトンインスタンスを取得</summary>
    static BloomPass *GetInstance()
    {
        static BloomPass instance;
        return &instance;
    }

    /// <summary>初期化（ミップ列の確保と設定の読み込み）</summary>
    void Initialize();

    /// <summary>終了処理</summary>
    void Finalize();

    /// <summary>
    /// パーティクル描画の直前に呼ぶ。シーンの控えを取る。
    /// 「パーティクルのみ」モードで有効なときだけコピーが走る
    /// </summary>
    /// <param name="sceneResource">シーンのカラーターゲット（RENDER_TARGET 状態で渡すこと）</param>
    void CaptureBeforeParticles(ID3D12Resource *sceneResource);

    /// <summary>
    /// パーティクル描画の直後に呼ぶ。ブルームを計算してシーンへ加算する。
    /// 呼び出し前後でシーンは RENDER_TARGET 状態のまま（中で往復させる）
    /// </summary>
    /// <param name="sceneResource">シーンのカラーターゲット</param>
    void Render(ID3D12Resource *sceneResource);

    /// <summary>設定UI（統計ウィンドウから呼ばれる）</summary>
    void DrawImGui();

    /// <summary>設定をJSONへ保存</summary>
    void SaveData();

    /// ====================================
    /// Getter / Setter
    /// ====================================

    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool enable) { enabled_ = enable; }
    bool IsDistortionEnabled() const { return distortionEnabled_; }
    void SetDistortionEnabled(bool enable) { distortionEnabled_ = enable; }
    bool IsParticleOnly() const { return particleOnly_; }
    void SetParticleOnly(bool particleOnly) { particleOnly_ = particleOnly; }

  private:
    /// ====================================
    /// private method
    /// ====================================

    /// <summary>ミップ列と控えバッファを作り直す</summary>
    void CreateResources(uint32_t width, uint32_t height);

    /// <summary>仮想解像度が変わっていたら作り直す</summary>
    void EnsureResolution();

    /// <summary>設定JSONの読み込み</summary>
    void LoadData();

    /// <summary>
    /// SRVテーブルの指定スロットへその場でSRVを作る。
    /// シェーダー可視ヒープは CPU から読めずコピーできないので、作り直す形をとる
    /// </summary>
    void WriteSrv(uint32_t tableIndex, uint32_t slot, ID3D12Resource *resource);

    /// ====================================
    /// private variables
    /// ====================================

    // ミップの段数。半解像度から始めて1段ずつ半分にする。
    // 1280x720 なら 640x360 → 320x180 → 160x90 → 80x45 → 40x22 → 20x11。
    // 段を増やすほど滲みが広がるが、小さくなりすぎると形が崩れる
    static constexpr uint32_t kMipCount = 6;
    // シーンと同じ HDR フォーマット（1.0 を超える明るさを保つ）
    static constexpr DXGI_FORMAT kBloomFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    DirectXCommon *pDxCommon_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;

    // パーティクル描画前のシーンの控え（パーティクルのみモードで使う）
    Microsoft::WRL::ComPtr<ID3D12Resource> beforeParticleResource_;
    uint32_t beforeParticleSrvIndex_ = 0;
    bool beforeParticleCaptured_ = false; // 今フレーム控えを取ったか

    // ミップ列
    Microsoft::WRL::ComPtr<ID3D12Resource> mipResources_[kMipCount];
    uint32_t mipSrvIndex_[kMipCount] = {};
    uint32_t mipUavIndex_[kMipCount] = {};
    uint32_t mipWidth_[kMipCount] = {};
    uint32_t mipHeight_[kMipCount] = {};

    // 入力テーブル（t0,t1 の2枠）。プリフィルタが2枚使うので連続領域が要る
    uint32_t prefilterTableIndex_ = 0;
    // 歪みの入力テーブル（t0=控え / t1=マスクに使うミップ）
    uint32_t distortTableIndex_ = 0;
    bool tablesReady_ = false;

    // 定数バッファ（パスごとに別。1フレーム内で複数回ディスパッチするため
    // 1本を書き換えると後のディスパッチが後勝ちの値を読んでしまう）
    struct PrefilterConstants
    {
        float threshold;
        float knee;
        uint32_t particleOnly;
        uint32_t pad0;
        int32_t dstSize[2];
        int32_t srcSize[2];
    };
    struct DownsampleConstants
    {
        int32_t dstSize[2];
        int32_t srcSize[2];
    };
    struct UpsampleConstants
    {
        int32_t dstSize[2];
        int32_t srcSize[2];
        float filterRadius;
        float blend;
        float pad0[2];
    };
    struct DistortionConstants
    {
        int32_t dstSize[2];
        float strength;
        float frequency;
        float speed;
        float time;
        float maskGain;
        float pad0;
    };
    struct CompositeConstants
    {
        int32_t dstSize[2];
        float intensity;
        float pad0;
    };

    Microsoft::WRL::ComPtr<ID3D12Resource> prefilterCb_;
    PrefilterConstants *pPrefilterCb_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> downsampleCb_[kMipCount];
    DownsampleConstants *pDownsampleCb_[kMipCount] = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> upsampleCb_[kMipCount];
    UpsampleConstants *pUpsampleCb_[kMipCount] = {};
    Microsoft::WRL::ComPtr<ID3D12Resource> compositeCb_;
    CompositeConstants *pCompositeCb_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> distortionCb_;
    DistortionConstants *pDistortionCb_ = nullptr;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool initialized_ = false;

    // ---- 設定 ----
    bool enabled_ = true;
    bool particleOnly_ = true; // 既定はパーティクルだけを光らせる
    float threshold_ = 1.0f;   // ここから光り始める明るさ（HDRなので 1.0 超も普通にある）
    float knee_ = 0.5f;        // しきい値まわりのなめらかさ
    float intensity_ = 0.6f;   // 加算する強さ
    float filterRadius_ = 0.005f; // アップサンプルのテント幅（UV空間）
    uint32_t activeMipCount_ = kMipCount; // 実際に使う段数（減らすと滲みが狭くなる）

    // ---- 歪み（熱揺らぎ）の設定 ----
    // パーティクルだけのミップをマスクに使うので「パーティクルだけを光らせる」が前提
    bool distortionEnabled_ = false;
    float distortionStrength_ = 0.015f;  // ずらす量（UV空間）
    float distortionFrequency_ = 26.0f;  // ゆらぎの細かさ
    float distortionSpeed_ = 0.35f;      // ゆらぎが動く速さ
    float distortionMaskGain_ = 20.0f;   // 明るさ→揺れ量の倍率（ミップは縮小で暗くなるので大きめ）
    uint32_t distortionMaskMip_ = 2;     // マスクに使う段（大きいほど外へ広がる）
    float distortionTime_ = 0.0f;        // 経過時間
};
} // namespace Hagine
