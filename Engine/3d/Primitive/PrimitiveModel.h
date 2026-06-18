#pragma once
#include "Model/ModelStructs.h"
#include <unordered_map>

namespace Hagine {

/// <summary>
/// プリミティブ形状の種類
/// </summary>
enum class PrimitiveType {
    None = 0,
    Plane,
    Sphere,
    Cube,
    Cylinder,
    Ring,
    Triangle,
    Cone,
    Pyramid,
    ClosedCylinder,
    kCount,
};

/// <summary>
/// 各種プリミティブ形状の頂点データを生成・保持するシングルトン
/// </summary>
class PrimitiveModel {
  private:
    /// ====================================================
    /// private method
    /// ====================================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    PrimitiveModel() = default;

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~PrimitiveModel() = default;
    PrimitiveModel(PrimitiveModel &) = delete;
    PrimitiveModel &operator=(PrimitiveModel &) = delete;

    /// <summary>
    /// プリミティブ1種類分の頂点・インデックス・UV・色データ
    /// </summary>
    struct PrimitiveData {
        std::vector<VertexData> vertices; // 頂点データ
        std::vector<uint32_t> indices;    // インデックスデータ
        Matrix4x4 uvMatrix;               // UV変換行列
        Vector4 color;                    // 色
    };

  public:
    /// =============================================================
    /// public method
    /// =============================================================

    /// <summary>
    /// 初期化（全プリミティブの頂点データを生成）
    /// </summary>
    void Initialize();

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>PrimitiveModel*: シングルトンインスタンス</returns>
    static PrimitiveModel* GetInstance() {
        static PrimitiveModel instance;
        return &instance;
    }

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 指定種類のプリミティブデータを取得
    /// </summary>
    /// <param name="type">プリミティブの種類</param>
    /// <returns>PrimitiveData: 該当する頂点データ（なければ空）</returns>
    PrimitiveData GetPrimitiveData(const PrimitiveType &type) {
        auto it = primitiveDataMap_.find(type);
        if (it != primitiveDataMap_.end()) {
            return it->second;
        }
        return {};
    }

  private:
    /// ===================================================
    /// private method（各プリミティブの頂点生成）
    /// ===================================================

    /// <summary>
    /// 球の頂点データを生成
    /// </summary>
    void CreateSphere();

    /// <summary>
    /// 平面の頂点データを生成
    /// </summary>
    void CreatePlane();

    /// <summary>
    /// 立方体の頂点データを生成
    /// </summary>
    void CreateCube();

    /// <summary>
    /// 円柱の頂点データを生成
    /// </summary>
    void CreateCylinder();

    /// <summary>
    /// リングの頂点データを生成
    /// </summary>
    void CreateRing();

    /// <summary>
    /// 三角形の頂点データを生成
    /// </summary>
    void CreateTriangle();

    /// <summary>
    /// 円錐の頂点データを生成
    /// </summary>
    void CreateCone();

    /// <summary>
    /// 角錐の頂点データを生成
    /// </summary>
    void CreatePyramid();

    /// <summary>
    /// 上下に蓋のある円柱の頂点データを生成
    /// </summary>
    void CreateClosedCylinder();

  private:
    /// ===================================================
    /// private variants
    /// ===================================================

    std::unordered_map<PrimitiveType, PrimitiveData> primitiveDataMap_; // 種類ごとのプリミティブデータ
};
} // namespace Hagine
