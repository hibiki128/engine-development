#pragma once
#include "Model/ModelStructs.h"
#include <unordered_map>
#include <utility>
#include <vector>

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
/// 円形プリミティブ（Ring/Sphere/Cylinder/Cone）の生成パラメータ。
/// 既定値は従来の固定形状と同一なので、未指定時は見た目が変わらない。
/// </summary>
struct PrimitiveParams {
    uint32_t divide = 32;         // 円周方向の分割数（多いほど滑らか）
    uint32_t heightDivide = 1;    // Cylinder の高さ方向の分割数（格子の横リング本数 = heightDivide+1）
    float ringOuterRadius = 1.0f; // Ring の外半径
    float ringInnerRadius = 0.5f; // Ring の内半径（円の幅 = 外半径 - 内半径）
};

/// <summary>指定の PrimitiveType がパラメータ調整に対応するか</summary>
bool IsParametricPrimitive(PrimitiveType type);

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

    /// <summary>
    /// パラメータ指定でプリミティブ頂点データを生成する（キャッシュしない）。
    /// 円形系(Ring/Sphere/Cylinder/Cone)は params を反映、それ以外は既定の固定データを返す。
    /// </summary>
    PrimitiveData BuildParametricData(const PrimitiveType &type, const PrimitiveParams &params);

    /// <summary>
    /// Cylinder の格子線（縦線＋横リング）をローカル空間の線分列として生成する。
    /// 三角形メッシュから導出したエッジと違い対角線を含まないため、
    /// パーティクルのエッジ発生モードで綺麗な格子（籠状）になる。
    /// 頂点座標は BuildCylinder と同じローカル空間（半径1・高さ2）で揃えてある。
    /// </summary>
    /// <returns>各要素 {始点, 終点} の線分リスト</returns>
    std::vector<std::pair<Vector3, Vector3>> BuildCylinderGridLines(uint32_t divide, uint32_t heightDivide);

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

    /// ===================================================
    /// パラメータ版ビルダー（Create* は既定値でこれらを呼ぶ）
    /// ===================================================
    PrimitiveData BuildSphere(uint32_t divide);
    PrimitiveData BuildCylinder(uint32_t divide, uint32_t heightDivide);
    PrimitiveData BuildRing(uint32_t divide, float outerRadius, float innerRadius);
    PrimitiveData BuildCone(uint32_t divide);

  private:
    /// ===================================================
    /// private variants
    /// ===================================================

    std::unordered_map<PrimitiveType, PrimitiveData> primitiveDataMap_; // 種類ごとのプリミティブデータ
};
} // namespace Hagine
