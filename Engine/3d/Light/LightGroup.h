#pragma once
#include "Data/DataHandler.h"
#include "d3d12.h"
#include "externals/nlohmann/json.hpp"
#include "wrl.h"
#include <Camera/ViewProjection/ViewProjection.h>
#include <string>
#include <type/Vector3.h>
#include <type/Vector4.h>

#define MAX_POINT_LIGHTS 5
#define MAX_SPOT_LIGHTS 5

/// <summary>
/// ライトタイプ種別
/// </summary>
namespace Hagine {
enum class LightType {
    Directional, // 平行光源
    Point,       // ポイントライト
    Spot         // スポットライト
};

class DirectXCommon;

/// <summary>
/// ライトグループクラス
/// シーン内の各種光源（平行・点・スポット）を一括管理し、GPUへ定数データとして転送する
/// </summary>
class LightGroup {
public:
    // ===================================================
    // 公開メソッド
    // ===================================================

    /// <summary>
    /// シングルトンインスタンスを取得
    /// </summary>
    static LightGroup *GetInstance() {
        static LightGroup instance;
        return &instance;
    }

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="viewProjection">カメラのビュープロジェクション</param>
    void Update(const ViewProjection &viewProjection);

    /// <summary>
    /// 描画設定（ルートシグネチャへのバインドなど）
    /// </summary>
    void Draw();

    /// <summary>
    /// ImGuiによるデバッグ表示
    /// </summary>
    void imgui();

    /// <summary>
    /// ライトデータをJSONへ保存
    /// </summary>
    /// <param name="fileName">ファイル名</param>
    void SaveLightData(const std::string &fileName);

    /// <summary>
    /// ライトデータをJSONから読み込み
    /// </summary>
    /// <param name="fileName">ファイル名</param>
    void LoadLightData(const std::string &fileName);

    /// <summary>
    /// 光源可視化フラグを設定
    /// </summary>
    void SetShowLightVisualization(bool show) { showLightVisualization_ = show; }

    Vector3 GetDirectionalLightDirection() const {
        if (directionalLightData_) return directionalLightData_->direction;
        return {0.f, -1.f, 0.f};
    }
    bool IsDirectionalLightActive() const {
        return directionalLightData_ && directionalLightData_->active != 0;
    }

private:
    // ===================================================
    // 非公開メソッド
    // ===================================================

    LightGroup() = default;
    ~LightGroup() = default;
    LightGroup(LightGroup &) = delete;
    LightGroup &operator=(LightGroup &) = delete;

    /// <summary>
    /// 平行光源用バッファの生成
    /// </summary>
    void CreateDirectionLight();

    /// <summary>
    /// ポイントライト用バッファの生成
    /// </summary>
    void CreatePointLights();

    /// <summary>
    /// スポットライト用バッファの生成
    /// </summary>
    void CreateSpotLights();

    /// <summary>
    /// カメラ情報用バッファの生成
    /// </summary>
    void CreateCamera();

    /// <summary>
    /// ポイントライトを追加
    /// </summary>
    void AddPointLight();

    /// <summary>
    /// ポイントライトを削除
    /// </summary>
    /// <param name="index">対象インデックス</param>
    void RemovePointLight(int index);

    /// <summary>
    /// スポットライトを追加
    /// </summary>
    void AddSpotLight();

    /// <summary>
    /// スポットライトを削除
    /// </summary>
    /// <param name="index">対象インデックス</param>
    void RemoveSpotLight(int index);

    /// <summary>
    /// ポイントライトバッファの同期
    /// </summary>
    void UpdatePointLightBuffer();

    /// <summary>
    /// スポットライトバッファの同期
    /// </summary>
    void UpdateSpotLightBuffer();

    /// <summary>
    /// デバッグ用の光源位置描画
    /// </summary>
    void DrawLightVisualization();

private:
    // ===================================================
    // 構造体定義
    // ===================================================

    /// <summary>
    /// 平行光源データ
    /// </summary>
    struct DirectionLight {
        Vector4 color;       // 色 (RGBA)
        Vector3 direction;   // 方向
        float intensity;     // 輝度
        int32_t active;      // 有効フラグ
        int32_t HalfLambert; // ハーフランバート適用
        int32_t BlinnPhong;  // Blinn-Phong適用
    };

    /// <summary>
    /// ポイントライトデータ
    /// </summary>
    struct PointLight {
        Vector4 color;       // 色 (RGBA)
        Vector3 position;    // 位置
        float intensity;     // 輝度
        int32_t active;      // 有効フラグ
        float radius;        // 影響半径
        float decay;         // 減衰率
        int32_t HalfLambert; // ハーフランバート適用
        int32_t BlinnPhong;  // Blinn-Phong適用
        float padding[3];
    };

    /// <summary>
    /// ポイントライト群
    /// </summary>
    struct PointLights {
        alignas(16) PointLight lights[MAX_POINT_LIGHTS]; 
        int32_t count;                                   // 有効数
        float padding[3];
    };

    /// <summary>
    /// スポットライトデータ
    /// </summary>
    struct SpotLight {
        Vector4 color;       // 色 (RGBA)
        Vector3 position;    // 位置
        float intensity;     // 輝度
        Vector3 direction;   // 方向
        float distance;      // 照射距離
        float decay;         // 減衰率
        float cosAngle;      // コーン角度の余弦
        int32_t active;      // 有効フラグ
        int32_t HalfLambert; // ハーフランバート適用
        int32_t BlinnPhong;  // Blinn-Phong適用
        float padding[3];
    };

    /// <summary>
    /// スポットライト群
    /// </summary>
    struct SpotLights {
        SpotLight lights[MAX_SPOT_LIGHTS]; 
        int32_t count;                     // 有効数
        float padding[3];
    };

    /// <summary>
    /// GPU転送用カメラデータ
    /// </summary>
    struct CameraForGPU {
        Vector3 worldPosition; // ワールド座標
    };

private:
    // ===================================================
    // メンバ変数
    // ===================================================

    DirectXCommon *dxCommon_ = nullptr; // DirectX基盤へのポインタ

    // リソース管理
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_; // 平行光源バッファ
    DirectionLight *directionalLightData_ = nullptr;                  

    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightsResource_;      // ポイントライトバッファ
    PointLights *pointLightsData_ = nullptr;                     

    Microsoft::WRL::ComPtr<ID3D12Resource> spotLightsResource_;       // スポットライトバッファ
    SpotLights *spotLightsData_ = nullptr;                      

    Microsoft::WRL::ComPtr<ID3D12Resource> cameraForGPUResource_;     // カメラデータバッファ
    CameraForGPU *cameraForGPUData_ = nullptr;                    

    // ライトリスト管理
    std::vector<PointLight> pointLights_; 
    std::vector<SpotLight> spotLights_;   

    // UI状態
    std::string saveMessage_;  
    int saveMessageTimer_ = 0; 
    bool isDirectionalLight_ = true;       // 平行光源の全体有効フラグ
    bool showLightVisualization_ = false; 

    std::unique_ptr<DataHandler> DLightData_ = nullptr; // JSONハンドラー
};
} // namespace Hagine
