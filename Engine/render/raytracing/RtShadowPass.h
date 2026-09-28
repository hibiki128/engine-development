#pragma once
#include "d3d12.h"
#include "wrl.h"
#include <cstdint>
#include <type/Matrix4x4.h>
#include <type/Vector3.h>

namespace Hagine {
class DirectXCommon;
class SrvManager;

/// <summary>
/// レイトレーシングで影のマスクを作るパス（インラインRT / RayQuery）。
///
/// 深度バッファからワールド座標を戻し、そこから光源へ遮蔽レイを1本飛ばして
/// 「日向なら1／影なら0」を画面いっぱいのテクスチャへ書き出す。
/// 出来たマスクはディファードのライティングがシャドウマップの代わりに使える。
///
/// シャドウマップと比べたときの利点:
///   ・解像度に起因するギザギザが出ない
///   ・接地の影が浮いたりめり込んだりしない（バイアス調整地獄から解放される）
///   ・投影範囲(ortho)という概念が無いので、どこまで離れても影が落ちる
///
/// 実行には加速構造（RaytracingScene）が要る。
/// 非対応環境や加速構造が無いときは何もしない
/// </summary>
class RtShadowPass
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    static RtShadowPass *GetInstance()
    {
        static RtShadowPass instance;
        return &instance;
    }

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="pDxCommon">DirectX共通処理</param>
    /// <param name="pSrvManager">SRVマネージャー</param>
    void Initialize(DirectXCommon *pDxCommon, SrvManager *pSrvManager);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 影のマスクを作る。G-Buffer パスの後（深度が確定した後）に呼ぶ
    /// </summary>
    /// <param name="depthResource">シーンの深度バッファ</param>
    /// <param name="normalResource">G-Bufferの法線（無ければ nullptr）</param>
    /// <param name="inverseViewProjection">ビュー射影行列の逆行列</param>
    /// <param name="lightDirection">平行光源が進む向き</param>
    void Render(ID3D12Resource *depthResource, ID3D12Resource *normalResource,
                const Matrix4x4 &inverseViewProjection, const Vector3 &lightDirection);

    /// <summary>
    /// このパスを使うか
    /// </summary>
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

    /// <summary>
    /// このフレームに影マスクを作れたか。
    /// ライティング側はこれを見て、シャドウマップと使い分ける
    /// </summary>
    /// <returns>bool: 作れていれば true</returns>
    bool HasValidMask() const { return maskReady_; }

    /// <summary>
    /// 影マスクのSRV番号
    /// </summary>
    /// <returns>uint32_t: SRV番号。未作成なら UINT32_MAX</returns>
    uint32_t GetShadowMaskSrvIndex() const { return maskSrvIndex_; }

    /// <summary>
    /// 影マスクのリソース
    /// </summary>
    ID3D12Resource *GetShadowMaskResource() const { return maskResource_.Get(); }

    /// <summary>
    /// ImGuiでの設定UI
    /// </summary>
    void DrawImGui();

  private:
    RtShadowPass() = default;
    ~RtShadowPass() = default;
    RtShadowPass(const RtShadowPass &) = delete;
    RtShadowPass &operator=(const RtShadowPass &) = delete;

    /// <summary>
    /// 解像度ぶんのマスクテクスチャを作り直す
    /// </summary>
    void CreateResources(uint32_t width, uint32_t height);

    /// <summary>
    /// 解像度が変わっていたら作り直す
    /// </summary>
    void EnsureResolution();

    /// <summary>RtShadow.CS.hlsl の RtShadowConstants と同じ並びにすること</summary>
    struct Constants
    {
        Matrix4x4 inverseViewProjection{}; // NDC → ワールド
        Vector3 lightDirection = {0.0f, -1.0f, 0.0f}; // 平行光源が進む向き
        float normalBias = 0.02f;          // 始点を法線方向へ押し出す量
        float maxDistance = 500.0f;        // 遮蔽を探す最大距離
        float softness = 0.0f;             // 影の柔らかさ
        int textureSize[2] = {0, 0};       // 出力の解像度
    };
    static_assert(sizeof(Constants) == 96, "RtShadow.CS.hlsl の RtShadowConstants と並びが合っていません");

    DirectXCommon *pDxCommon_ = nullptr; // DirectX共通処理
    SrvManager *pSrvManager_ = nullptr;  // SRVマネージャー
    bool initialized_ = false;           // 初期化済みか
    bool enabled_ = false;               // 使うかどうか（既定はOFF）
    bool maskReady_ = false;             // このフレームにマスクを作れたか

    uint32_t width_ = 0;  // マスクの幅
    uint32_t height_ = 0; // マスクの高さ

    Microsoft::WRL::ComPtr<ID3D12Resource> maskResource_; // 影マスク
    uint32_t maskSrvIndex_ = UINT32_MAX;                  // マスクのSRV
    uint32_t maskUavIndex_ = UINT32_MAX;                  // マスクのUAV

    // t0=TLAS, t1=深度, t2=法線 を並べる連続領域の先頭
    uint32_t inputTableIndex_ = UINT32_MAX;

    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_; // 定数バッファ
    Constants *pConstants_ = nullptr;                       // そのマップ先
    Constants settings_{};                                  // 編集中の設定
};
} // namespace Hagine
