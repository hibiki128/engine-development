#pragma once
#include "d3d12.h"
#include "wrl.h"
#include <cstdint>
#include <type/Matrix4x4.h>
#include <type/Vector3.h>

namespace Hagine {
class DirectXCommon;
class SrvManager;
class ViewProjection;

/// <summary>
/// 視錐台を 3D の格子（フロクセル）に分けて霧と光の散乱を溜め、シーンへ合成するボリュメトリックフォグ
/// </summary>
class VolumetricFogPass
{
  private:
    VolumetricFogPass() = default;
    ~VolumetricFogPass() = default;
    VolumetricFogPass(const VolumetricFogPass &) = delete;
    VolumetricFogPass &operator=(const VolumetricFogPass &) = delete;

  public:
    /// ====================================
    /// public method
    /// ====================================

    /// <summary>
    /// シングルトンインスタンスを取得
    /// </summary>
    static VolumetricFogPass *GetInstance()
    {
        static VolumetricFogPass instance;
        return &instance;
    }

    /// <summary>
    /// 格子のテクスチャと定数バッファを作り、設定を読み込む
    /// </summary>
    void Initialize();

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 不透明物の描画後に呼び、霧と光の散乱をシーンへ合成する
    /// </summary>
    /// <param name="viewProjection">描画に使うカメラ</param>
    /// <param name="sceneResource">シーンのカラーターゲット（RENDER_TARGET 状態で渡す）</param>
    void Render(const ViewProjection &viewProjection, ID3D12Resource *sceneResource);

    /// <summary>
    /// 設定UIを描画
    /// </summary>
    void DrawImGui();

    /// <summary>
    /// 設定をJSONへ保存
    /// </summary>
    void SaveData();

    /// <summary>
    /// 前フレームの結果を捨てる（カメラの瞬間移動やシーン切り替えの後に呼ぶ）
    /// </summary>
    void ResetHistory() { historyValid_ = false; }

    /// ====================================
    /// Getter / Setter
    /// ====================================

    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool enable) { enabled_ = enable; }

  private:
    /// ====================================
    /// private method
    /// ====================================

    /// <summary>
    /// 格子のテクスチャ・光源バッファ・ディスクリプタを作る
    /// </summary>
    void CreateResources();

    /// <summary>
    /// 3D テクスチャを1枚作る
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateVolume(const wchar_t *name);

    /// <summary>
    /// 連続したディスクリプタを確保し、先頭の番号を返す（確保できなければ 0）
    /// </summary>
    uint32_t AllocateTable(uint32_t size);

    /// <summary>
    /// 3D テクスチャの SRV を指定番号へ作る
    /// </summary>
    void WriteVolumeSrv(uint32_t index, ID3D12Resource *resource);

    /// <summary>
    /// 3D テクスチャの UAV を指定番号へ作る
    /// </summary>
    void WriteVolumeUav(uint32_t index, ID3D12Resource *resource);

    /// <summary>
    /// 点光源・スポットライトを光源バッファへ詰め、詰めた数を返す
    /// </summary>
    uint32_t UploadLights();

    /// <summary>
    /// 設定JSONの読み込み
    /// </summary>
    void LoadData();

    /// ====================================
    /// private variables
    /// ====================================

    // 格子の分割数（横・縦・奥行き）。横縦は画面を 8 画素前後ずつに分ける
    static constexpr uint32_t kGridX = 160;
    static constexpr uint32_t kGridY = 90;
    static constexpr uint32_t kGridZ = 64;
    // 霧に当てる点光源・スポットライトの上限
    static constexpr uint32_t kMaxLights = 128;
    // 散乱（rgb）と減衰係数／透過率（a）
    static constexpr DXGI_FORMAT kVolumeFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    /// <summary>
    /// 霧に当てる光源1つぶん（点光源とスポットライト共通）
    /// </summary>
    struct VolumeLight
    {
        Vector3 position;  // ワールド座標
        float range;       // 届く距離
        Vector3 color;     // 色 × 強さ
        float decay;       // 減衰の鋭さ
        Vector3 direction; // スポットライトの向き（正規化済み）
        float cosAngle;    // スポットライトの開き角の余弦
        uint32_t type;     // 0: 点光源 / 1: スポットライト
        float pad[3];
    };
    static_assert(sizeof(VolumeLight) == 64, "HLSL の VolumeLight と大きさを揃える");

    /// <summary>
    /// 注入パスの定数
    /// </summary>
    struct InjectConstants
    {
        Matrix4x4 inverseView;            // ビュー → ワールド
        Matrix4x4 previousView;           // ワールド → 前フレームのビュー
        Matrix4x4 previousViewProjection; // ワールド → 前フレームのクリップ
        Matrix4x4 lightViewProjection;    // ワールド → 平行光源のクリップ
        Vector3 cameraPosition;
        float nearDistance;
        Vector3 albedo;
        float farDistance;
        Vector3 directionalColor;
        float density;
        Vector3 directionalDirection;
        float heightFalloff;
        Vector3 ambientColor;
        float baseHeight;
        Vector3 jitter;
        float historyBlend;
        float projectionScaleX;
        float projectionScaleY;
        float anisotropy;
        float shadowBias;
        uint32_t lightCount;
        uint32_t shadowEnabled;
        uint32_t historyValid;
        float localLightScale;
        int32_t gridSize[3];
        float pad0;
        uint32_t localLightShadow; // 点光源・スポットライトの影を調べるか
        float lightShadowOffset;   // 影のレイを光源の手前で止める距離
        float pad1[2];
    };
    static_assert(sizeof(InjectConstants) == 416, "HLSL の InjectParams と大きさを揃える");

    /// <summary>
    /// 積分パスの定数
    /// </summary>
    struct IntegrateConstants
    {
        float projectionScaleX;
        float projectionScaleY;
        float nearDistance;
        float farDistance;
        int32_t gridSize[3];
        float pad0;
    };
    static_assert(sizeof(IntegrateConstants) % 16 == 0, "定数バッファは16バイト単位");

    /// <summary>
    /// 合成パスの定数
    /// </summary>
    struct ApplyConstants
    {
        float depthScale;  // 射影行列の _43（深度 → ビュー空間の奥行きの復元用）
        float depthOffset; // 射影行列の _33
        float nearDistance;
        float farDistance;
        int32_t textureSize[2];
        int32_t gridDepth;
        float skyStrength;
    };
    static_assert(sizeof(ApplyConstants) % 16 == 0, "定数バッファは16バイト単位");

    DirectXCommon *pDxCommon_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;

    // 注入結果（散乱と減衰係数）。前フレームの結果と混ぜるので2枚を交互に使う
    Microsoft::WRL::ComPtr<ID3D12Resource> scatterVolumes_[2];
    uint32_t scatterSrvIndex_[2] = {};
    uint32_t scatterUavIndex_[2] = {};
    // 手前から積分した結果（散乱の累計と透過率）
    Microsoft::WRL::ComPtr<ID3D12Resource> integratedVolume_;
    uint32_t integratedUavIndex_ = 0;

    // 光源バッファ（アップロードヒープに置いて毎フレーム書き換える）
    Microsoft::WRL::ComPtr<ID3D12Resource> lightBuffer_;
    VolumeLight *pLights_ = nullptr;

    // 注入の入力テーブル（t0=前フレームの注入結果 / t1=シャドウマップ / t2=光源 / t3=加速構造）。書き込み先ごとに1本
    uint32_t injectTableIndex_[2] = {};
    // 合成の入力テーブル（t0=積分結果 / t1=深度）
    uint32_t applyTableIndex_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> injectCb_;
    InjectConstants *pInjectCb_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> integrateCb_;
    IntegrateConstants *pIntegrateCb_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> applyCb_;
    ApplyConstants *pApplyCb_ = nullptr;

    // 前フレームのカメラ（再投影用）
    Matrix4x4 previousView_{};
    Matrix4x4 previousViewProjection_{};
    Vector3 previousCameraPosition_{};
    bool historyValid_ = false;
    uint32_t writeIndex_ = 0; // 今フレームに書き込む注入結果の番号
    uint32_t frameIndex_ = 0; // ジッタの順番
    bool initialized_ = false;
    bool raytracedShadowActive_ = false; // 直前のフレームで点光源・スポットライトの影を調べたか（表示用）

    // ---- 設定 ----
    bool enabled_ = false;
    float density_ = 0.02f;                       // 基準の高さでの霧の濃さ
    float heightFalloff_ = 0.1f;                  // 高さ方向に薄くなる速さ（0で高さ無視）
    float baseHeight_ = 0.0f;                     // ここより下は濃さが一定
    Vector3 albedo_ = {1.0f, 1.0f, 1.0f};         // 霧の色（光を散らす割合）
    float anisotropy_ = 0.6f;                     // 散乱の偏り（0で全方位、1に近いほど光の進む向きへ集中）
    float farDistance_ = 64.0f;                   // 霧を計算する最大距離
    float directionalScale_ = 1.0f;               // 平行光源の散乱の強さ
    float localLightScale_ = 1.0f;                // 点光源・スポットライトの散乱の強さ
    Vector3 ambientColor_ = {0.02f, 0.025f, 0.03f}; // 光が当たっていない所の霧の明るさ
    float historyBlend_ = 0.9f;                   // 前フレームの結果を混ぜる割合
    float shadowBias_ = 0.002f;                   // シャドウ比較のバイアス
    float skyStrength_ = 1.0f;                    // 空に掛ける割合
    bool localLightShadow_ = true;                // 点光源・スポットライトの影をレイトレで調べるか
    float lightShadowOffset_ = 0.3f;              // 影のレイを光源の手前で止める距離（ランプ自身に当たらないように）
};
} // namespace Hagine
