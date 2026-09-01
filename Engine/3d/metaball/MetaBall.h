#pragma once
#include "model/ModelStructs.h"
#include <cstdint>
#include <vector>

namespace Hagine {

/// <summary>
/// メタボール要素の形状
/// </summary>
enum class MetaBallShape
{
    Ball = 0, // 球
    Capsule,  // カプセル（線分の周り）
    Count,
};

/// <summary>
/// メタボール 1 要素。Blender のメタボール要素に相当する。
/// </summary>
struct MetaBallElement
{
    Vector3 position{};                       // ローカル空間での中心
    MetaBallShape shape = MetaBallShape::Ball; // 形状
    float radius = 1.0f;                      // 影響半径（この距離で密度がちょうど 0 になる）
    float stiffness = 1.0f;                   // 中心での密度の高さ
    bool negative = false;                    // 負の要素（他をへこませる）
    Vector3 axis{};                           // Capsule の半長ベクトル（中心から端まで）
    bool enabled = true;                      // 無効化フラグ
};

/// <summary>
/// メッシュ生成のパラメータ
/// </summary>
struct MetaBallBuildParams
{
    uint32_t resolution = 32; // 一番長い辺の分割数。小さいほど粗く速い
    float threshold = 0.5f;   // 等値面のしきい値。大きいほど痩せる
    float uvScale = 1.0f;     // 平面投影 UV のスケール
};

/// <summary>
/// 生成結果の統計。ImGui 表示とプロファイル用。
/// </summary>
struct MetaBallBuildStats
{
    uint32_t vertexCount = 0;    // 頂点数
    uint32_t triangleCount = 0;  // 三角形数
    uint32_t gridX = 0;          // サンプル点の数（X）
    uint32_t gridY = 0;          // サンプル点の数（Y）
    uint32_t gridZ = 0;          // サンプル点の数（Z）
    float cellSize = 0.0f;       // 1 セルの辺の長さ
    float buildMilliseconds = 0.0f; // 生成にかかった時間
};

/// <summary>
/// メタボールの密度場を Marching Cubes で三角形メッシュに変換する。
///
/// 密度場の評価は gather（各セルで全要素をループ）ではなく scatter で行う。
/// 各要素が自分の影響球に入るサンプル点だけに加算して回るので、
/// コストが「セル数 × 要素数」ではなく「要素ごとの影響球の体積の合計」になる。
/// falloff が有限半径でちょうど 0 になること（Wyvill）が前提。
/// </summary>
class MetaBallBuilder
{
  public:
    /// <summary>
    /// 要素列からメッシュを生成する。要素が無い/表面が無い場合は空の MeshData を返す。
    /// </summary>
    /// <param name="elements">メタボール要素</param>
    /// <param name="params">生成パラメータ</param>
    /// <param name="outStats">統計の受け取り先（不要なら nullptr）</param>
    /// <returns>MeshData: 生成された頂点とインデックス</returns>
    static MeshData Build(const std::vector<MetaBallElement> &elements,
                          const MetaBallBuildParams &params,
                          MetaBallBuildStats *outStats = nullptr);

    /// <summary>
    /// 指定座標での密度を求める（テストとデバッグ用。生成本体は scatter なのでこれを使わない）
    /// </summary>
    static float EvaluateDensity(const std::vector<MetaBallElement> &elements, const Vector3 &point);

    /// <summary>
    /// 1 要素が指定座標に与える密度を求める
    /// </summary>
    static float EvaluateElement(const MetaBallElement &element, const Vector3 &point);
};

} // namespace Hagine
