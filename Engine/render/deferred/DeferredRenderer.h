#pragma once
#include "camera/projection/ViewProjection.h"
#include <cstdint>
#include <d3d12.h>
#include <type/Matrix4x4.h>
#include <type/Vector3.h>
#include <wrl/client.h>

namespace Hagine {
class DirectXCommon;
class SrvManager;
class PipelineManager;

/// <summary>
/// ディファードレンダリング
///
/// 【流れ】
///   1. BeginGBufferPass()  … G-Buffer 3枚＋深度へ不透明オブジェクトを描く
///   2. EndGBufferPass()    … G-Buffer と深度を読み取り可能な状態へ遷移
///   3. CullLights()        … 16x16タイルごとに届くポイントライトを絞り込む
///   4. RenderLighting()    … 全画面パスでライティングし、オフスクリーンRTへ書く
///   5. BeginForwardPass()  … 深度を書き込み可能へ戻し、以降は前方描画（空・半透明・パーティクル・UI）
///
/// 【前方描画との関係】
///   ライティングの数式は Object3d.PS と一致させてあるため、
///   ディファードのON/OFFで見た目は変わらない。半透明・パーティクル・線は
///   従来どおり前方描画のままなので、G-Buffer には載らない。
/// </summary>
class DeferredRenderer
{
  public:
    /// ===================================================
    /// public constant
    /// ===================================================

    // ライトカリングのタイルサイズ（Deferred.hlsli の DEFERRED_TILE_SIZE と一致させること）
    static constexpr uint32_t kTileSize = 16;
    // 1タイルが保持できるライト数（Deferred.hlsli の DEFERRED_MAX_LIGHTS_PER_TILE と一致させること）
    static constexpr uint32_t kMaxLightsPerTile = 256;
    // タイルあたりの要素数（先頭1個が件数）
    static constexpr uint32_t kTileStride = kMaxLightsPerTile + 1;

    // G-Buffer が使うRTVスロット（0,1=バックバッファ / 2=オフスクリーン / 3,4=ピンポン / 5=最終結果 / 6=プレビュー）
    static constexpr uint32_t kGBufferRtvSlot = 8;
    static constexpr uint32_t kGBufferCount = 3;

    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static DeferredRenderer *GetInstance()
    {
        static DeferredRenderer instance;
        return &instance;
    }

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// ディファードを使うかどうか。false のときは従来の前方描画のみになる
    /// </summary>
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_ && initialized_; }

    /// <summary>
    /// 現在 G-Buffer パスを記録中か。
    /// Object3d はこれを見て「G-Buffer用PSOで描く／前方描画では描かない」を切り替える。
    /// （ShadowMap::IsShadowPassActive と同じ考え方）
    /// </summary>
    bool IsGBufferPassActive() const { return gBufferPassActive_; }

    /// <summary>
    /// G-Buffer パスを開始する（3枚のRTVと既存の深度をバインドしクリアする）
    /// </summary>
    /// <param name="viewProjection">このフレームのビュープロジェクション</param>
    void BeginGBufferPass(const ViewProjection &viewProjection);

    /// <summary>
    /// G-Buffer パスを終了し、G-Buffer と深度をシェーダーリソースへ遷移させる
    /// </summary>
    void EndGBufferPass();

    /// <summary>
    /// タイルごとのライトカリングを実行する（EndGBufferPass の後に呼ぶ）
    /// </summary>
    void CullLights();

    /// <summary>
    /// ライティングパス。オフスクリーンRTへ陰影を書き込む
    /// </summary>
    void RenderLighting();

    /// <summary>
    /// 前方描画へ戻す。深度を書き込み可能へ戻し、オフスクリーンRT＋深度をバインドする
    /// </summary>
    void BeginForwardPass();

    /// <summary>
    /// ImGuiでの設定・統計表示
    /// </summary>
    void DrawImGui();

    /// <summary>
    /// このフレームのタイル数を取得（統計表示用）
    /// </summary>
    uint32_t GetTileCountX() const { return tileCountX_; }
    uint32_t GetTileCountY() const { return tileCountY_; }

  private:
    /// ===================================================
    /// private struct
    /// ===================================================

    /// <summary>
    /// ディファードの各パスが共有する定数（Deferred.hlsli の DeferredConstants と一致させること）
    /// </summary>
    struct DeferredConstants
    {
        Matrix4x4 invViewProjection;   // クリップ→ワールド復元用
        Matrix4x4 view;                // ビュー行列
        Matrix4x4 projection;          // 射影行列
        Matrix4x4 lightViewProjection; // シャドウマップ用
        Vector3 cameraPosition;        // カメラのワールド座標
        uint32_t pointLightCount;      // CPUが積んだポイントライト数（粒子光源は含まない）
        uint32_t screenWidth;          // 描画幅
        uint32_t screenHeight;         // 描画高
        uint32_t tileCountX;           // タイル数X
        uint32_t tileCountY;           // タイル数Y
        float nearZ;                   // nearクリップ
        float farZ;                    // farクリップ
        uint32_t pointLightCapacity;   // ライトバッファの容量（GPU生成分の溢れを切り捨てる上限）
        float padding;
    };

    /// <summary>
    /// G-Buffer 1枚ぶん
    /// </summary>
    struct GBufferTarget
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;    // テクスチャ
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};            // RTV
        uint32_t srvIndex = 0;                              // SRV番号
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_GENERIC_READ; // 現在の状態
    };

    /// ===================================================
    /// private method
    /// ===================================================

    DeferredRenderer() = default;
    ~DeferredRenderer() = default;
    DeferredRenderer(const DeferredRenderer &) = delete;
    DeferredRenderer &operator=(const DeferredRenderer &) = delete;

    /// <summary>
    /// 指定サイズでG-Bufferとタイルバッファを作り直す
    /// </summary>
    /// <param name="width">幅</param>
    /// <param name="height">高さ</param>
    void CreateResources(uint32_t width, uint32_t height);

    /// <summary>
    /// 仮想解像度が変わっていたらリソースを作り直す
    /// </summary>
    void EnsureResolution();

    /// <summary>
    /// 定数バッファを今フレームの値で更新する
    /// </summary>
    /// <param name="viewProjection">ビュープロジェクション</param>
    void UpdateConstants(const ViewProjection &viewProjection);

    /// <summary>
    /// G-Buffer 1枚の状態を遷移させる
    /// </summary>
    /// <param name="target">対象</param>
    /// <param name="after">遷移先</param>
    void TransitionGBuffer(GBufferTarget &target, D3D12_RESOURCE_STATES after);

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    DirectXCommon *pDxCommon_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;
    PipelineManager *pPsoManager_ = nullptr;

    bool initialized_ = false;
    bool enabled_ = true;           // ディファードを使うか
    bool gBufferPassActive_ = false; // G-Buffer パス記録中か

    uint32_t width_ = 0;      // G-Bufferの幅
    uint32_t height_ = 0;     // G-Bufferの高さ
    uint32_t tileCountX_ = 0; // タイル数X
    uint32_t tileCountY_ = 0; // タイル数Y

    GBufferTarget gBuffers_[kGBufferCount]; // アルベド / 法線＋光沢 / マテリアル

    // タイルごとのライトインデックス（先頭1要素が件数）
    Microsoft::WRL::ComPtr<ID3D12Resource> tileLightBuffer_;
    uint32_t tileLightSrvIndex_ = 0;
    uint32_t tileLightUavIndex_ = 0;
    D3D12_RESOURCE_STATES tileLightState_ = D3D12_RESOURCE_STATE_COMMON;

    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
    DeferredConstants *pConstants_ = nullptr;

    // 直近フレームの統計（ImGui表示用）
    uint32_t lastPointLightCount_ = 0;
};
} // namespace Hagine
