#pragma once
#include "d3d12.h"
#include "wrl.h"
#include <cstdint>
#include <string>
#include <type/Matrix4x4.h>

namespace Hagine {
class DirectXCommon;
class SrvManager;

/// <summary>
/// SSAO（スクリーンスペースアンビエントオクルージョン）。
///
/// 深度と法線だけを見て「その点がどれだけ周りに囲まれているか」を測り、
/// 物と床の接地部分や、へこみ・隙間へ陰りを入れる。
/// 置いてあるものが地に足のついた見え方になるので、
/// ライトを増やすより手軽に立体感が出る。
///
/// G-Buffer を前提にしているので、ディファードが有効なときだけ働く。
/// </summary>
class SsaoRenderer
{
  private:
    SsaoRenderer() = default;
    ~SsaoRenderer() = default;
    SsaoRenderer(const SsaoRenderer &) = delete;
    SsaoRenderer &operator=(const SsaoRenderer &) = delete;

  public:
    /// ====================================
    /// public method
    /// ====================================

    /// <summary>シングルトンインスタンスを取得</summary>
    static SsaoRenderer *GetInstance()
    {
        static SsaoRenderer instance;
        return &instance;
    }

    /// <summary>初期化（バッファの確保と設定の読み込み）</summary>
    void Initialize();

    /// <summary>終了処理</summary>
    void Finalize();

    /// <summary>
    /// 遮蔽を計算する。ディファードのライティングパスの直前に呼ぶこと
    /// </summary>
    /// <param name="depthResource">深度バッファ</param>
    /// <param name="normalResource">G-Buffer の法線テクスチャ</param>
    /// <param name="invViewProjection">クリップ→ワールド復元用の行列</param>
    /// <param name="view">ビュー行列</param>
    /// <param name="projection">射影行列</param>
    void Render(ID3D12Resource *depthResource, ID3D12Resource *normalResource,
                const Matrix4x4 &invViewProjection, const Matrix4x4 &view, const Matrix4x4 &projection);

    /// <summary>ならし後の遮蔽テクスチャのSRV番号（ライティングパスが読む）</summary>
    /// <returns>uint32_t: SRV番号</returns>
    uint32_t GetResultSrvIndex() const { return blurredSrvIndex_; }

    /// <summary>有効かどうか</summary>
    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool enabled) { enabled_ = enabled; }

    /// <summary>ライティングへの効かせ具合 (0〜1)</summary>
    float GetStrength() const { return strength_; }

    /// <summary>ImGuiによる設定UI</summary>
    void DrawImGui();

    /// <summary>設定をJSONへ保存する</summary>
    /// <param name="fileName">ファイル名</param>
    void SaveData(const std::string &fileName);

    /// <summary>設定をJSONから読み込む</summary>
    /// <param name="fileName">ファイル名</param>
    void LoadData(const std::string &fileName);

  private:
    /// ====================================
    /// private structures
    /// ====================================

    /// SSAO 本体のパラメータ（Ssao.CS.hlsl と同じ並びにすること）
    struct SsaoConstants
    {
        Matrix4x4 invViewProjection;
        Matrix4x4 view;
        Matrix4x4 projection;

        uint32_t screenWidth = 0;
        uint32_t screenHeight = 0;
        float radius = 0.6f;
        float bias = 0.03f;

        float intensity = 1.0f;
        float power = 1.6f;
        int32_t sampleCount = 16;
        float maxDistance = 12.0f;
    };

    /// ならしのパラメータ（SsaoBlur.CS.hlsl と同じ並びにすること）
    struct SsaoBlurConstants
    {
        uint32_t screenWidth = 0;
        uint32_t screenHeight = 0;
        int32_t radius = 2;
        float depthThreshold = 0.0015f;
    };

    /// ====================================
    /// private method
    /// ====================================

    /// 仮想解像度が変わっていたらバッファを作り直す
    void EnsureResolution();

    /// 指定サイズで遮蔽テクスチャを作る
    void CreateResources(uint32_t width, uint32_t height);

    /// <summary>
    /// コンピュートへ渡す入力テーブル（連続した2枚）を、その場で作り直す。
    /// シェーダー可視ヒープはコピー元にできないので、毎回ここでSRVを「作る」
    /// </summary>
    /// <param name="tableIndex">テーブル先頭のディスクリプタ番号</param>
    /// <param name="first">t0 に差すリソース</param>
    /// <param name="firstFormat">t0 のフォーマット</param>
    /// <param name="second">t1 に差すリソース</param>
    /// <param name="secondFormat">t1 のフォーマット</param>
    void BuildInputTable(uint32_t tableIndex,
                         ID3D12Resource *first, DXGI_FORMAT firstFormat,
                         ID3D12Resource *second, DXGI_FORMAT secondFormat);

  private:
    /// ====================================
    /// private variables
    /// ====================================

    DirectXCommon *pDxCommon_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;

    bool initialized_ = false;
    bool enabled_ = true;
    float strength_ = 0.7f; // ライティングへの効かせ具合

    uint32_t width_ = 0;
    uint32_t height_ = 0;

    // 遮蔽の計算結果と、それをならしたもの
    Microsoft::WRL::ComPtr<ID3D12Resource> rawResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> blurredResource_;
    uint32_t rawSrvIndex_ = 0;
    uint32_t rawUavIndex_ = 0;
    uint32_t blurredSrvIndex_ = 0;
    uint32_t blurredUavIndex_ = 0;

    // コンピュートの入力テーブル（t0,t1 の2枚ぶん連続した領域）。
    // デスクリプタテーブルはヒープ上で連続している必要があるため、専用に押さえておく
    uint32_t ssaoTableIndex_ = 0;
    uint32_t blurTableIndex_ = 0;
    bool tablesReady_ = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    SsaoConstants *pConstants_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> blurConstantBuffer_;
    SsaoBlurConstants *pBlurConstants_ = nullptr;

    // UI で触るぶんの控え（定数バッファは毎フレーム作り直すのでここが正）
    SsaoConstants settings_;
    SsaoBlurConstants blurSettings_;
};

} // namespace Hagine
