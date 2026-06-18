#pragma once
#include "wrl.h"
#include <Model/ModelStructs.h>
#include <Primitive/PrimitiveModel.h>
#include <d3d12.h>

namespace Hagine {
class DirectXCommon;

/// <summary>
/// マテリアルクラス
/// テクスチャ、色、ライティング設定を管理する
/// </summary>
class Material {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// テクスチャ読み込み
    /// </summary>
    void LoadTexture();

    /// <summary>
    /// プリミティブ初期化
    /// </summary>
    /// <param name="type">プリミティブタイプ</param>
    void PrimitiveInitialize(const PrimitiveType &type);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="color">色</param>
    /// <param name="lighting">ライティング有効フラグ</param>
    void Draw(const Vector4 color, bool lighting);

    /// <summary>
    /// Getter
    /// </summary>
    MaterialData &GetMaterialData() { return materialData_; }
    const MaterialData &GetMaterialData() const { return materialData_; }
    MaterialDataGPU *GetMaterialDataGPU() { return materialDataGPU_; }

    /// <summary>
    /// Setter
    /// </summary>
    void SetTexture(const std::string &texturePath);
    void SetEnvironmentCoefficients(float environmentCoefficients);
    void SetUVPosition(const Vector2 &pos) { materialData_.uvPosition = pos; }
    void SetUVSize(const Vector2 &size) { materialData_.uvSize = size; }
    void SetUVRotate(const float &rotate) { materialData_.uvRotate = rotate; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// マテリアルテンプレートファイル読み込み
    /// </summary>
    /// <param name="directoryPath">ディレクトリパス</param>
    /// <param name="filename">ファイル名</param>
    /// <returns>MaterialData: 読み込んだマテリアルデータ</returns>
    MaterialData LoadMaterialTemplateFile(const std::string &directoryPath, const std::string &filename);

    /// <summary>
    /// マテリアル作成
    /// </summary>
    void CreateMaterial();

    /// <summary>
    /// GPUデータ更新
    /// </summary>
    void UpdateGPUData();

  private:
    /// ===================================================
    /// private varians
    /// ===================================================

    DirectXCommon *dxCommon_ = nullptr;                       // DirectX共通クラス
    MaterialData materialData_;                               // CPU側マテリアルデータ
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_; // GPUバッファリソース
    MaterialDataGPU *materialDataGPU_ = nullptr;              // GPUバッファデータポインタ
};
} // namespace Hagine
