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
/// ワールド空間でまとめて三角形化するときのパラメータ。
///
/// 弾のように要素が世界中に散る用途では、全体の AABB に解像度を割り当てると
/// セルが粗くなりすぎて要素が消えてしまう。こちらはセルの大きさを
/// ワールド単位で直接指定し、影響球が重なる要素だけをかたまりにして
/// それぞれ別の格子で切る。
/// </summary>
struct MetaBallWorldParams
{
    float voxelSize = 0.15f; // セル 1 辺の長さ（ワールド単位）。小さいほど滑らかで重い
    float threshold = 0.5f;  // 等値面のしきい値
    float uvScale = 1.0f;    // 平面投影 UV のスケール
    // 1 かたまりあたりのサンプル点数の上限。voxelSize を小さくしすぎたときの保険
    uint32_t maxSamplesPerCluster = 4u * 1024 * 1024;
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
    uint32_t clusterCount = 0;      // 分かれたかたまりの数
    uint32_t elementCount = 0;      // 寄与した要素の数
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
    /// 影響球が重なる要素同士をかたまりにまとめ、かたまりごとに別々の格子で三角形化する。
    /// 結果は 1 つの MeshData に連結されるので、描画は 1 ドローで済む。
    ///
    /// 離れたところにある要素が 1 つの巨大な格子を張らせないので、
    /// 弾のように世界中に散らばる使い方でも解像度が落ちない。
    /// </summary>
    /// <param name="elements">ワールド空間に置かれた要素</param>
    /// <param name="params">セルの大きさなどのパラメータ</param>
    /// <param name="outStats">統計の受け取り先（不要なら nullptr）</param>
    /// <returns>MeshData: 全かたまりを連結したメッシュ</returns>
    static MeshData BuildClustered(const std::vector<MetaBallElement> &elements,
                                   const MetaBallWorldParams &params,
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
