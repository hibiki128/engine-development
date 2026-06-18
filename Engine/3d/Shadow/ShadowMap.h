#pragma once
#include "d3d12.h"
#include "wrl.h"
#include <cstdint>
#include <string>
#include <type/Matrix4x4.h>
#include <type/Vector3.h>

namespace Hagine {
class LightGroup;

class DirectXCommon;
class SrvManager;

/// <summary>
/// シャドウマップ管理クラス（シングルトン）
///
/// 平行光源からの深度テクスチャを生成し、影の描画に使用する。
/// ImGui でオン/オフ・パラメータ調整が可能。設定は JSON に保存/復元できる。
/// </summary>
class ShadowMap {
public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>ShadowMap*: シングルトンインスタンス</returns>
    static ShadowMap *GetInstance() {
        static ShadowMap instance;
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
    /// シャドウパスを開始
    /// </summary>
    void BeginShadowPass();

    /// <summary>
    /// シャドウパスを終了
    /// </summary>
    void EndShadowPass();

    /// <summary>
    /// ライトのView×Projection行列を再計算する（毎フレーム呼ぶ）
    /// </summary>
    void Update();

    /// ===================================================
    /// アクセサ
    /// ===================================================

    /// <summary>
    /// シャドウが有効かを取得
    /// </summary>
    /// <returns>bool: 有効なら true</returns>
    bool IsEnabled() const { return enabled_; }

    /// <summary>
    /// シャドウの有効/無効を設定
    /// </summary>
    /// <param name="v">有効にするなら true</param>
    void SetEnabled(bool v) { enabled_ = v; }

    /// <summary>
    /// シャドウパスが実行中かを取得
    /// </summary>
    /// <returns>bool: 実行中なら true</returns>
    bool IsShadowPassActive() const { return shadowPassActive_; }

    /// <summary>
    /// シャドウパスの実行状態を設定
    /// </summary>
    /// <param name="v">実行中にするなら true</param>
    void SetShadowPassActive(bool v) { shadowPassActive_ = v; }

    /// <summary>
    /// ライトのView×Projection行列を取得
    /// </summary>
    /// <returns>const Matrix4x4&: ライトのView×Projection行列</returns>
    const Matrix4x4 &GetLightViewProjection() const { return lightViewProj_; }

    /// <summary>
    /// シャドウマップのSRVインデックス（SrvManager管理）を取得
    /// </summary>
    /// <returns>uint32_t: SRVインデックス</returns>
    uint32_t GetShadowSrvIndex() const { return srvIndex_; }

    /// <summary>
    /// ShadowData定数バッファのGPUアドレスを取得
    /// </summary>
    /// <returns>D3D12_GPU_VIRTUAL_ADDRESS: GPU仮想アドレス</returns>
    D3D12_GPU_VIRTUAL_ADDRESS GetShadowDataGpuAddress() const;

    /// ===================================================
    /// ImGui / 保存
    /// ===================================================

    /// <summary>
    /// ImGuiでのシャドウ設定UIを更新
    /// </summary>
    /// <param name="open">ウィンドウの表示状態。閉じるボタン押下で false になる（nullptr で閉じるボタン非表示）</param>
    void UpdateImGui(bool *open = nullptr);

    /// <summary>
    /// 設定をJsonへ保存
    /// </summary>
    /// <param name="fileName">保存ファイル名</param>
    void SaveConfig(const std::string &fileName = "ShadowMap");

    /// <summary>
    /// 設定をJsonから読み込み
    /// </summary>
    /// <param name="fileName">読み込みファイル名</param>
    void LoadConfig(const std::string &fileName = "ShadowMap");

    /// ===================================================
    /// ライトパラメータ
    /// ===================================================

    /// <summary>
    /// ライト方向を取得
    /// </summary>
    /// <returns>const Vector3&: ライト方向（正規化済み）</returns>
    const Vector3 &GetLightDirection() const { return lightDir_; }

    /// <summary>
    /// ライト方向を設定（内部で正規化）
    /// </summary>
    /// <param name="dir">ライト方向</param>
    void SetLightDirection(const Vector3 &dir) { lightDir_ = dir.Normalize(); }

    /// <summary>
    /// シャドウ投影の中心となるターゲット位置を設定
    /// </summary>
    /// <param name="target">ターゲット位置</param>
    void SetLightTarget(const Vector3 &target) { lightTarget_ = target; }

private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    ShadowMap() = default;

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~ShadowMap() = default;
    ShadowMap(const ShadowMap &) = delete;
    ShadowMap &operator=(const ShadowMap &) = delete;

    /// <summary>
    /// ライトのビュー行列を生成
    /// </summary>
    /// <returns>Matrix4x4: ライトのビュー行列</returns>
    Matrix4x4 MakeLightViewMatrix() const;

    /// <summary>
    /// シャドウマップ用のSRVを生成
    /// </summary>
    void CreateShadowSRV();

    /// ===================================================
    /// private variants
    /// ===================================================

    // GPUリソース
    static constexpr UINT kShadowMapSize = 2048; // シャドウマップの解像度

    Microsoft::WRL::ComPtr<ID3D12Resource> depthResource_;     // 深度リソース
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;     // DSVヒープ
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};                  // DSVハンドル

    uint32_t srvIndex_ = UINT32_MAX; // シャドウマップのSRVインデックス

    /// <summary>
    /// シェーダーへ渡すシャドウ設定（定数バッファ）
    /// </summary>
    struct ShadowDataGPU {
        int32_t enabled; // 有効フラグ
        float bias;      // 深度バイアス
        float strength;  // 影の濃さ
        float padding;   // パディング
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowDataResource_; // ShadowData定数バッファ
    ShadowDataGPU *shadowDataPtr_ = nullptr;                    // 定数バッファのマップ先

    // パラメータ
    Matrix4x4 lightViewProj_{}; // ライトのView×Projection行列

    Vector3 lightDir_ = {0.0f, -1.0f, 0.5f};   // ライト方向（正規化）
    Vector3 lightTarget_ = {0.0f, 0.0f, 0.0f}; // シャドウ投影の中心
    float lightDistance_ = 80.0f;              // ライトまでの距離
    float orthoWidth_ = 40.0f;                 // 正射影の幅
    float orthoHeight_ = 40.0f;                // 正射影の高さ
    float nearZ_ = 0.1f;                       // ニアクリップ
    float farZ_ = 200.0f;                      // ファークリップ
    float bias_ = 0.001f;                      // 深度バイアス
    float strength_ = 0.7f;                    // 影の濃さ
    bool enabled_ = true;                      // シャドウ有効フラグ
    bool shadowPassActive_ = false;            // シャドウパス実行中フラグ
    bool syncWithDirectionalLight_ = true;     // DirectionalLightの方向を自動同期するか

    D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE; // 現在のリソース状態

    DirectXCommon *dxCommon_ = nullptr;   // DirectX共通処理
    SrvManager *srvManager_ = nullptr;    // SRVマネージャー
};
} // namespace Hagine
