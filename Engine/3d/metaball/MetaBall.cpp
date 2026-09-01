#define NOMINMAX
#include "MetaBall.h"
#include "MarchingCubesTable.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace Hagine {
namespace {

/// 解像度の上限。上げすぎるとセル数が 3 乗で効いて即座に固まるので蓋をしておく
constexpr uint32_t kMinResolution = 4;
constexpr uint32_t kMaxResolution = 192;

/// <summary>
/// 辺 i を「どのサンプル点が持つどの軸の辺か」に読み替える表。
/// dx,dy,dz がセル原点からの格子オフセット、axis が 0=X,1=Y,2=Z。
/// 隣り合うセルが同じ辺を参照したとき、同じ頂点を共有するために使う。
/// </summary>
constexpr int kEdgeOwner[12][4] = {
    {0, 0, 0, 0}, {1, 0, 0, 2}, {0, 0, 1, 0}, {0, 0, 0, 2},
    {0, 1, 0, 0}, {1, 1, 0, 2}, {0, 1, 1, 0}, {0, 1, 0, 2},
    {0, 0, 0, 1}, {1, 0, 0, 1}, {1, 0, 1, 1}, {0, 0, 1, 1},
};

/// <summary>
/// Wyvill の falloff。t = 距離 / 影響半径。
/// t=0 で 1、t=1 でちょうど 0 になり、t>=1 では完全に 0。
/// 有限半径で 0 になるので scatter で足し込める（無限に裾を引く指数型では出来ない）。
/// </summary>
inline float WyvillFalloff(float t2)
{
    // 1 - 22/9 t^2 + 17/9 t^4 - 4/9 t^6
    constexpr float a = 22.0f / 9.0f;
    constexpr float b = 17.0f / 9.0f;
    constexpr float c = 4.0f / 9.0f;
    return 1.0f + t2 * (-a + t2 * (b - c * t2));
}

/// <summary>点から線分までの距離の 2 乗</summary>
inline float DistanceSqToSegment(const Vector3 &point, const Vector3 &a, const Vector3 &b)
{
    const Vector3 ab = b - a;
    const float lenSq = ab.LengthSq();
    if (lenSq <= 1e-12f)
    {
        return (point - a).LengthSq();
    }
    float t = (point - a).Dot(ab) / lenSq;
    t = std::clamp(t, 0.0f, 1.0f);
    return (point - (a + ab * t)).LengthSq();
}

/// <summary>要素が影響を及ぼす範囲の半サイズ</summary>
inline Vector3 ElementHalfExtent(const MetaBallElement &element)
{
    if (element.shape == MetaBallShape::Capsule)
    {
        return {std::fabs(element.axis.x) + element.radius,
                std::fabs(element.axis.y) + element.radius,
                std::fabs(element.axis.z) + element.radius};
    }
    return {element.radius, element.radius, element.radius};
}

/// <summary>要素の集合の AABB。有効な要素が無ければ false</summary>
bool ComputeBounds(const std::vector<MetaBallElement> &elements,
                   const std::vector<uint32_t> &members,
                   Vector3 &outMin, Vector3 &outMax)
{
    bool hasAny = false;
    for (uint32_t index : members)
    {
        const MetaBallElement &element = elements[index];
        if (!element.enabled || element.radius <= 0.0f)
        {
            continue;
        }
        const Vector3 half = ElementHalfExtent(element);
        const Vector3 lo = element.position - half;
        const Vector3 hi = element.position + half;
        if (!hasAny)
        {
            outMin = lo;
            outMax = hi;
            hasAny = true;
        }
        else
        {
            outMin = {std::min(outMin.x, lo.x), std::min(outMin.y, lo.y), std::min(outMin.z, lo.z)};
            outMax = {std::max(outMax.x, hi.x), std::max(outMax.y, hi.y), std::max(outMax.z, hi.z)};
        }
    }
    return hasAny;
}

} // namespace

float MetaBallBuilder::EvaluateElement(const MetaBallElement &element, const Vector3 &point)
{
    if (!element.enabled || element.radius <= 0.0f)
    {
        return 0.0f;
    }

    float distSq = 0.0f;
    if (element.shape == MetaBallShape::Capsule)
    {
        distSq = DistanceSqToSegment(point, element.position - element.axis, element.position + element.axis);
    }
    else
    {
        distSq = (point - element.position).LengthSq();
    }

    const float radiusSq = element.radius * element.radius;
    if (distSq >= radiusSq)
    {
        return 0.0f;
    }

    const float value = element.stiffness * WyvillFalloff(distSq / radiusSq);
    return element.negative ? -value : value;
}

float MetaBallBuilder::EvaluateDensity(const std::vector<MetaBallElement> &elements, const Vector3 &point)
{
    float sum = 0.0f;
    for (const MetaBallElement &element : elements)
    {
        sum += EvaluateElement(element, point);
    }
    return sum;
}

namespace {

/// <summary>
/// かたまり 1 つ分を格子に切って三角形化し、meshData に連結する。
/// members に入っている要素だけを見る。
/// </summary>
/// <returns>格子のサンプル点数（x,y,z）。作れなかった場合は全て 0</returns>
struct GridDims
{
    int nx = 0, ny = 0, nz = 0;
};

GridDims AppendClusterMesh(const std::vector<MetaBallElement> &elements,
                           const std::vector<uint32_t> &members,
                           float cellSize, float threshold, float uvScale,
                           uint64_t maxSamples, MeshData &meshData)
{
    Vector3 boundsMin{};
    Vector3 boundsMax{};
    if (!ComputeBounds(elements, members, boundsMin, boundsMax) || cellSize <= 0.0f)
    {
        return {};
    }

    // 外周 2 セル分を余白にする。一番外のサンプル点が必ず密度 0 になり、表面が閉じる
    constexpr int kPadCells = 2;
    const Vector3 extent = boundsMax - boundsMin;
    const Vector3 origin = boundsMin - Vector3{cellSize, cellSize, cellSize} * static_cast<float>(kPadCells);

    auto axisSampleCount = [&](float length) {
        return static_cast<int>(std::ceil(length / cellSize)) + 1 + kPadCells * 2;
    };
    const int nx = axisSampleCount(extent.x);
    const int ny = axisSampleCount(extent.y);
    const int nz = axisSampleCount(extent.z);
    const uint64_t sampleCount = static_cast<uint64_t>(nx) * ny * nz;

    // セルを細かくしすぎたときの保険。ここで諦めないとメモリを食い潰す
    if (sampleCount == 0 || sampleCount > maxSamples)
    {
        return {};
    }

    std::vector<float> density(static_cast<size_t>(sampleCount), 0.0f);

    auto sampleIndex = [nx, ny](int x, int y, int z) {
        return (static_cast<size_t>(z) * ny + y) * nx + x;
    };
    auto samplePosition = [&](int x, int y, int z) {
        return origin + Vector3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)} * cellSize;
    };

    // ---- scatter: 各要素が自分の影響範囲だけに密度を足し込む ----------------
    // 触られなかったサンプル点は 0 のまま残り、評価コストも発生しない
    int touchedMin[3] = {nx, ny, nz};
    int touchedMax[3] = {-1, -1, -1};

    for (uint32_t memberIndex : members)
    {
        const MetaBallElement &element = elements[memberIndex];
        if (!element.enabled || element.radius <= 0.0f)
        {
            continue;
        }

        const Vector3 half = ElementHalfExtent(element);
        const Vector3 lo = element.position - half - origin;
        const Vector3 hi = element.position + half - origin;

        const int i0 = std::max(0, static_cast<int>(std::floor(lo.x / cellSize)));
        const int j0 = std::max(0, static_cast<int>(std::floor(lo.y / cellSize)));
        const int k0 = std::max(0, static_cast<int>(std::floor(lo.z / cellSize)));
        const int i1 = std::min(nx - 1, static_cast<int>(std::ceil(hi.x / cellSize)));
        const int j1 = std::min(ny - 1, static_cast<int>(std::ceil(hi.y / cellSize)));
        const int k1 = std::min(nz - 1, static_cast<int>(std::ceil(hi.z / cellSize)));
        if (i0 > i1 || j0 > j1 || k0 > k1)
        {
            continue;
        }

        touchedMin[0] = std::min(touchedMin[0], i0);
        touchedMin[1] = std::min(touchedMin[1], j0);
        touchedMin[2] = std::min(touchedMin[2], k0);
        touchedMax[0] = std::max(touchedMax[0], i1);
        touchedMax[1] = std::max(touchedMax[1], j1);
        touchedMax[2] = std::max(touchedMax[2], k1);

        for (int z = k0; z <= k1; ++z)
        {
            for (int y = j0; y <= j1; ++y)
            {
                const size_t rowBase = (static_cast<size_t>(z) * ny + y) * nx;
                for (int x = i0; x <= i1; ++x)
                {
                    density[rowBase + x] += MetaBallBuilder::EvaluateElement(element, samplePosition(x, y, z));
                }
            }
        }
    }

    if (touchedMax[0] < 0)
    {
        return {};
    }

    // ---- Marching Cubes ----------------------------------------------------
    auto densityAt = [&](int x, int y, int z) -> float {
        x = std::clamp(x, 0, nx - 1);
        y = std::clamp(y, 0, ny - 1);
        z = std::clamp(z, 0, nz - 1);
        return density[sampleIndex(x, y, z)];
    };

    // 密度場の勾配。密度は内側ほど高いので、外向き法線は勾配の逆向きになる
    auto gradientAt = [&](int x, int y, int z) {
        return Vector3{densityAt(x + 1, y, z) - densityAt(x - 1, y, z),
                       densityAt(x, y + 1, z) - densityAt(x, y - 1, z),
                       densityAt(x, y, z + 1) - densityAt(x, y, z - 1)} *
               (0.5f / cellSize);
    };

    // サンプル点ごとに X/Y/Z 方向の辺を 1 本ずつ持たせ、隣接セル間で頂点を共有する
    std::vector<int32_t> edgeCache(static_cast<size_t>(sampleCount) * 3, -1);

    // 触られた範囲の外は密度が 0 のままなので、その内側だけ歩けばよい
    const int cx0 = std::max(0, touchedMin[0] - 1);
    const int cy0 = std::max(0, touchedMin[1] - 1);
    const int cz0 = std::max(0, touchedMin[2] - 1);
    const int cx1 = std::min(nx - 2, touchedMax[0]);
    const int cy1 = std::min(ny - 2, touchedMax[1]);
    const int cz1 = std::min(nz - 2, touchedMax[2]);

    for (int z = cz0; z <= cz1; ++z)
    {
        for (int y = cy0; y <= cy1; ++y)
        {
            for (int x = cx0; x <= cx1; ++x)
            {
                // セルの 8 隅の密度を取り、しきい値以上の隅のビットを立てる
                float corner[8];
                int cubeIndex = 0;
                for (int i = 0; i < 8; ++i)
                {
                    const int *o = kMarchingCubesCornerOffset[i];
                    corner[i] = density[sampleIndex(x + o[0], y + o[1], z + o[2])];
                    if (corner[i] >= threshold)
                    {
                        cubeIndex |= (1 << i);
                    }
                }
                // 全部内側／全部外側なら表面は通らない
                if (cubeIndex == 0 || cubeIndex == 255)
                {
                    continue;
                }

                // 辺 1 本につき頂点 1 個を作る（既にあれば使い回す）
                auto vertexOnEdge = [&](int edge) -> uint32_t {
                    const int *owner = kEdgeOwner[edge];
                    const int ox = x + owner[0];
                    const int oy = y + owner[1];
                    const int oz = z + owner[2];
                    const size_t cacheSlot = sampleIndex(ox, oy, oz) * 3 + owner[3];
                    if (edgeCache[cacheSlot] >= 0)
                    {
                        return static_cast<uint32_t>(edgeCache[cacheSlot]);
                    }

                    const int c0 = kMarchingCubesEdgeCorner[edge][0];
                    const int c1 = kMarchingCubesEdgeCorner[edge][1];
                    const int *o0 = kMarchingCubesCornerOffset[c0];
                    const int *o1 = kMarchingCubesCornerOffset[c1];

                    const float d0 = corner[c0];
                    const float d1 = corner[c1];
                    const float diff = d1 - d0;
                    // しきい値をまたぐ位置を線形補間で求める
                    float t = (std::fabs(diff) > 1e-8f) ? (threshold - d0) / diff : 0.5f;
                    t = std::clamp(t, 0.0f, 1.0f);

                    const Vector3 p0 = samplePosition(x + o0[0], y + o0[1], z + o0[2]);
                    const Vector3 p1 = samplePosition(x + o1[0], y + o1[1], z + o1[2]);
                    const Vector3 position = p0 + (p1 - p0) * t;

                    const Vector3 g0 = gradientAt(x + o0[0], y + o0[1], z + o0[2]);
                    const Vector3 g1 = gradientAt(x + o1[0], y + o1[1], z + o1[2]);
                    Vector3 normal = (g0 + (g1 - g0) * t) * -1.0f; // 外向き
                    if (normal.LengthSq() > 1e-12f)
                    {
                        normal = normal.Normalize();
                    }
                    else
                    {
                        normal = {0.0f, 1.0f, 0.0f};
                    }

                    // Marching Cubes は UV を作らないので、法線の主軸に平面投影しておく。
                    // 継ぎ目は出るがテクスチャを貼れる程度にはなる
                    const float ax = std::fabs(normal.x);
                    const float ay = std::fabs(normal.y);
                    const float az = std::fabs(normal.z);
                    Vector2 uv{};
                    if (ax >= ay && ax >= az)
                    {
                        uv = {position.z * uvScale, position.y * uvScale};
                    }
                    else if (ay >= az)
                    {
                        uv = {position.x * uvScale, position.z * uvScale};
                    }
                    else
                    {
                        uv = {position.x * uvScale, position.y * uvScale};
                    }

                    VertexData vertex{};
                    vertex.position = {position.x, position.y, position.z, 1.0f};
                    vertex.normal = normal;
                    vertex.texcoord = uv;

                    const uint32_t newIndex = static_cast<uint32_t>(meshData.vertices.size());
                    meshData.vertices.push_back(vertex);
                    edgeCache[cacheSlot] = static_cast<int32_t>(newIndex);
                    return newIndex;
                };

                const int8_t *tri = kMarchingCubesTriTable[cubeIndex];
                for (int t = 0; t < 15 && tri[t] >= 0; t += 3)
                {
                    const uint32_t i0 = vertexOnEdge(tri[t + 0]);
                    const uint32_t i1 = vertexOnEdge(tri[t + 1]);
                    const uint32_t i2 = vertexOnEdge(tri[t + 2]);
                    // 縮退三角形は捨てる
                    if (i0 == i1 || i1 == i2 || i2 == i0)
                    {
                        continue;
                    }
                    // このエンジンは Cross(v1-v0, v2-v0) が外向きになる巻き方が表
                    // （Sphere プリミティブと同じ向き）。テーブルの並びがそのままこれになる
                    meshData.indices.push_back(i0);
                    meshData.indices.push_back(i1);
                    meshData.indices.push_back(i2);
                }
            }
        }
    }

    // edgeCache はかたまりごとに作り直すので、頂点番号の付け替えは不要。
    // meshData.vertices の末尾に積んだ番号をそのまま使っている
    return GridDims{nx, ny, nz};
}

} // namespace

MeshData MetaBallBuilder::Build(const std::vector<MetaBallElement> &elements,
                                const MetaBallBuildParams &params,
                                MetaBallBuildStats *outStats)
{
    const auto startTime = std::chrono::high_resolution_clock::now();

    MeshData meshData{};
    if (outStats)
    {
        *outStats = MetaBallBuildStats{};
    }

    // 全要素をひとまとめに扱う
    std::vector<uint32_t> members(elements.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(elements.size()); ++i)
    {
        members[i] = i;
    }

    Vector3 boundsMin{};
    Vector3 boundsMax{};
    if (!ComputeBounds(elements, members, boundsMin, boundsMax))
    {
        return meshData;
    }

    const uint32_t resolution = std::clamp(params.resolution, kMinResolution, kMaxResolution);
    const Vector3 extent = boundsMax - boundsMin;
    const float longestSide = std::max({extent.x, extent.y, extent.z, 1e-4f});
    const float cellSize = longestSide / static_cast<float>(resolution);

    const GridDims dims = AppendClusterMesh(elements, members, cellSize, params.threshold,
                                            params.uvScale, 64ull * 1024 * 1024, meshData);

    meshData.materialIndex = 0;

    if (outStats)
    {
        const auto endTime = std::chrono::high_resolution_clock::now();
        outStats->vertexCount = static_cast<uint32_t>(meshData.vertices.size());
        outStats->triangleCount = static_cast<uint32_t>(meshData.indices.size() / 3);
        outStats->gridX = static_cast<uint32_t>(dims.nx);
        outStats->gridY = static_cast<uint32_t>(dims.ny);
        outStats->gridZ = static_cast<uint32_t>(dims.nz);
        outStats->cellSize = cellSize;
        outStats->clusterCount = meshData.indices.empty() ? 0u : 1u;
        outStats->elementCount = static_cast<uint32_t>(elements.size());
        outStats->buildMilliseconds =
            std::chrono::duration<float, std::milli>(endTime - startTime).count();
    }

    return meshData;
}

MeshData MetaBallBuilder::BuildClustered(const std::vector<MetaBallElement> &elements,
                                         const MetaBallWorldParams &params,
                                         MetaBallBuildStats *outStats)
{
    const auto startTime = std::chrono::high_resolution_clock::now();

    MeshData meshData{};
    if (outStats)
    {
        *outStats = MetaBallBuildStats{};
    }

    // 有効な要素だけを拾う
    std::vector<uint32_t> active;
    active.reserve(elements.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(elements.size()); ++i)
    {
        if (elements[i].enabled && elements[i].radius > 0.0f)
        {
            active.push_back(i);
        }
    }
    if (active.empty() || params.voxelSize <= 0.0f)
    {
        return meshData;
    }

    // ---- 影響範囲が重なる要素をつなげてかたまりに分ける（Union-Find）--------
    // 判定は AABB の重なりで多めに拾う。多めに繋がっても格子が少し大きくなるだけで
    // 結果は変わらないが、取りこぼすと表面が切れてしまう
    std::vector<uint32_t> parent(active.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(active.size()); ++i)
    {
        parent[i] = i;
    }
    auto find = [&parent](uint32_t x) {
        while (parent[x] != x)
        {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    auto unite = [&](uint32_t a, uint32_t b) {
        a = find(a);
        b = find(b);
        if (a != b)
        {
            parent[a] = b;
        }
    };

    for (size_t i = 0; i < active.size(); ++i)
    {
        const MetaBallElement &ei = elements[active[i]];
        const Vector3 hi = ElementHalfExtent(ei);
        for (size_t j = i + 1; j < active.size(); ++j)
        {
            const MetaBallElement &ej = elements[active[j]];
            const Vector3 hj = ElementHalfExtent(ej);
            const Vector3 d = ei.position - ej.position;
            if (std::fabs(d.x) <= hi.x + hj.x && std::fabs(d.y) <= hi.y + hj.y &&
                std::fabs(d.z) <= hi.z + hj.z)
            {
                unite(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
            }
        }
    }

    // 代表ごとにメンバーを集める
    std::vector<std::vector<uint32_t>> clusters;
    std::vector<int> clusterOfRoot(active.size(), -1);
    for (uint32_t i = 0; i < static_cast<uint32_t>(active.size()); ++i)
    {
        const uint32_t root = find(i);
        if (clusterOfRoot[root] < 0)
        {
            clusterOfRoot[root] = static_cast<int>(clusters.size());
            clusters.emplace_back();
        }
        clusters[static_cast<size_t>(clusterOfRoot[root])].push_back(active[i]);
    }

    // ---- かたまりごとに切って 1 つのメッシュに連結する ----------------------
    uint32_t builtClusters = 0;
    GridDims lastDims{};
    for (const std::vector<uint32_t> &cluster : clusters)
    {
        const GridDims dims = AppendClusterMesh(elements, cluster, params.voxelSize, params.threshold,
                                                params.uvScale, params.maxSamplesPerCluster, meshData);
        if (dims.nx > 0)
        {
            ++builtClusters;
            lastDims = dims;
        }
    }

    meshData.materialIndex = 0;

    if (outStats)
    {
        const auto endTime = std::chrono::high_resolution_clock::now();
        outStats->vertexCount = static_cast<uint32_t>(meshData.vertices.size());
        outStats->triangleCount = static_cast<uint32_t>(meshData.indices.size() / 3);
        outStats->gridX = static_cast<uint32_t>(lastDims.nx);
        outStats->gridY = static_cast<uint32_t>(lastDims.ny);
        outStats->gridZ = static_cast<uint32_t>(lastDims.nz);
        outStats->cellSize = params.voxelSize;
        outStats->clusterCount = builtClusters;
        outStats->elementCount = static_cast<uint32_t>(active.size());
        outStats->buildMilliseconds =
            std::chrono::duration<float, std::milli>(endTime - startTime).count();
    }

    return meshData;
}

} // namespace Hagine
