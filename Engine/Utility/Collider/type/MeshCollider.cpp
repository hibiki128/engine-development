#include "MeshCollider.h"
#include "Model/Model.h"
#include "line/DrawLine3D.h"
#include "myMath.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <unordered_set>

namespace Hagine {

namespace {
/// <summary>三角形の重心を求める</summary>
Vector3 TriangleCentroid(const Triangle &t) {
    return (t.v[0] + t.v[1] + t.v[2]) / 3.0f;
}

/// <summary>三角形を内包するAABBを求める</summary>
AABB TriangleBounds(const Triangle &t) {
    AABB b;
    b.min = {
        (std::min)({t.v[0].x, t.v[1].x, t.v[2].x}),
        (std::min)({t.v[0].y, t.v[1].y, t.v[2].y}),
        (std::min)({t.v[0].z, t.v[1].z, t.v[2].z})};
    b.max = {
        (std::max)({t.v[0].x, t.v[1].x, t.v[2].x}),
        (std::max)({t.v[0].y, t.v[1].y, t.v[2].y}),
        (std::max)({t.v[0].z, t.v[1].z, t.v[2].z})};
    return b;
}

/// <summary>2つのAABBを内包する最小のAABBを返す</summary>
AABB MergeAABB(const AABB &a, const AABB &b) {
    AABB r;
    r.min = {(std::min)(a.min.x, b.min.x), (std::min)(a.min.y, b.min.y), (std::min)(a.min.z, b.min.z)};
    r.max = {(std::max)(a.max.x, b.max.x), (std::max)(a.max.y, b.max.y), (std::max)(a.max.z, b.max.z)};
    return r;
}

/// <summary>AABB同士が重なっているか</summary>
bool AABBOverlap(const AABB &a, const AABB &b) {
    return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
           (a.min.y <= b.max.y && a.max.y >= b.min.y) &&
           (a.min.z <= b.max.z && a.max.z >= b.min.z);
}

/// <summary>三角形をワールド空間へ変換する</summary>
Triangle TransformTriangle(const Triangle &tri, const Matrix4x4 &m) {
    Triangle out;
    out.v[0] = Transformation(tri.v[0], m);
    out.v[1] = Transformation(tri.v[1], m);
    out.v[2] = Transformation(tri.v[2], m);
    out.normal = TransformNormal(tri.normal, m).Normalize();
    return out;
}

/// <summary>点から三角形上の最近点を求める（Ericson, Real-Time Collision Detection）</summary>
Vector3 ClosestPointOnTriangle(const Vector3 &p, const Vector3 &a, const Vector3 &b, const Vector3 &c) {
    Vector3 ab = b - a;
    Vector3 ac = c - a;
    Vector3 ap = p - a;
    float d1 = ab.Dot(ap);
    float d2 = ac.Dot(ap);
    if (d1 <= 0.0f && d2 <= 0.0f)
        return a;

    Vector3 bp = p - b;
    float d3 = ab.Dot(bp);
    float d4 = ac.Dot(bp);
    if (d3 >= 0.0f && d4 <= d3)
        return b;

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return a + ab * v;
    }

    Vector3 cp = p - c;
    float d5 = ab.Dot(cp);
    float d6 = ac.Dot(cp);
    if (d6 >= 0.0f && d5 <= d6)
        return c;

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return a + ac * w;
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + (c - b) * w;
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return a + ab * v + ac * w;
}

/// <summary>三角形と球の判定 + 押し戻しMTV（球を三角形から押し出す向き）</summary>
bool TriangleVsSphere(const Triangle &tri, const Vector3 &center, float radius, Vector3 &outMTV) {
    Vector3 closest = ClosestPointOnTriangle(center, tri.v[0], tri.v[1], tri.v[2]);
    Vector3 d = center - closest;
    float distSq = d.LengthSq();
    if (distSq > radius * radius)
        return false;

    float dist = std::sqrt(distSq);
    Vector3 dir = (dist > 1e-6f) ? (d / dist) : tri.normal; // 中心が面上なら法線方向へ逃がす
    outMTV = dir * (radius - dist);
    return true;
}

/// <summary>
/// 三角形とボックス（OBB/AABBを一般化）のSAT判定 + 押し戻しMTV
/// ボックスは中心C・単位軸u[3]・半径extent e[3]で表す。MTVはボックスを三角形から押し出す向き。
/// </summary>
bool TriangleVsBox(const Triangle &tri, const Vector3 &C, const Vector3 u[3], const float e[3], Vector3 &outMTV) {
    // ボックス中心を原点とした相対座標へ
    Vector3 v[3] = {tri.v[0] - C, tri.v[1] - C, tri.v[2] - C};
    Vector3 f[3] = {v[1] - v[0], v[2] - v[1], v[0] - v[2]};

    Vector3 axes[13];
    axes[0] = u[0];
    axes[1] = u[1];
    axes[2] = u[2];
    axes[3] = tri.normal;
    int idx = 4;
    for (int j = 0; j < 3; ++j)
        for (int k = 0; k < 3; ++k)
            axes[idx++] = f[j].Cross(u[k]);

    float minPen = FLT_MAX;
    Vector3 bestAxis;

    for (int i = 0; i < 13; ++i) {
        float len = axes[i].Length();
        if (len < 1e-6f)
            continue; // 退化軸（平行な辺など）はスキップ
        Vector3 L = axes[i] / len;

        float p0 = v[0].Dot(L);
        float p1 = v[1].Dot(L);
        float p2 = v[2].Dot(L);
        float triMin = (std::min)({p0, p1, p2});
        float triMax = (std::max)({p0, p1, p2});

        // ボックスの投影半径（中心は原点なので区間は[-r, r]）
        float r = e[0] * std::abs(u[0].Dot(L)) +
                  e[1] * std::abs(u[1].Dot(L)) +
                  e[2] * std::abs(u[2].Dot(L));

        float pen = (std::min)(triMax, r) - (std::max)(triMin, -r);
        if (pen < 0.0f)
            return false; // 分離軸 → 衝突なし

        if (pen < minPen) {
            minPen = pen;
            // ボックスを三角形から離す向きを選ぶ（ボックス中心0が三角形中心の反対側へ）
            float triCenter = (triMin + triMax) * 0.5f;
            float sign = (triCenter > 0.0f) ? -1.0f : 1.0f;
            bestAxis = L * sign;
        }
    }

    outMTV = bestAxis * minPen;
    return true;
}

/// <summary>線分と三角形の交差判定（Möller–Trumbore）</summary>
bool SegmentTriangle(const Vector3 &p, const Vector3 &q,
                     const Vector3 &a, const Vector3 &b, const Vector3 &c) {
    Vector3 dir = q - p;
    Vector3 e1 = b - a;
    Vector3 e2 = c - a;
    Vector3 pv = dir.Cross(e2);
    float det = e1.Dot(pv);
    if (std::abs(det) < 1e-8f)
        return false;

    float inv = 1.0f / det;
    Vector3 tv = p - a;
    float uu = tv.Dot(pv) * inv;
    if (uu < 0.0f || uu > 1.0f)
        return false;

    Vector3 qv = tv.Cross(e1);
    float vv = dir.Dot(qv) * inv;
    if (vv < 0.0f || uu + vv > 1.0f)
        return false;

    float t = e2.Dot(qv) * inv;
    return t >= 0.0f && t <= 1.0f;
}

/// <summary>三角形同士の交差判定（辺-面の交差で近似。共面ケースは非対応）</summary>
bool TriangleVsTriangle(const Triangle &A, const Triangle &B) {
    if (SegmentTriangle(A.v[0], A.v[1], B.v[0], B.v[1], B.v[2]))
        return true;
    if (SegmentTriangle(A.v[1], A.v[2], B.v[0], B.v[1], B.v[2]))
        return true;
    if (SegmentTriangle(A.v[2], A.v[0], B.v[0], B.v[1], B.v[2]))
        return true;
    if (SegmentTriangle(B.v[0], B.v[1], A.v[0], A.v[1], A.v[2]))
        return true;
    if (SegmentTriangle(B.v[1], B.v[2], A.v[0], A.v[1], A.v[2]))
        return true;
    if (SegmentTriangle(B.v[2], B.v[0], A.v[0], A.v[1], A.v[2]))
        return true;
    return false;
}

/// <summary>
/// 複数三角形のMTVを累積する。
/// 同一法線方向には最大量までしか押さず（平坦面で過剰補正しない）、
/// 直交方向は加算する（角での押し戻し）。
/// </summary>
void AccumulateMTV(Vector3 &sum, const Vector3 &mtv) {
    float pen = mtv.Length();
    if (pen < 1e-6f)
        return;
    Vector3 dir = mtv / pen;
    float already = sum.Dot(dir);
    if (already < pen)
        sum += dir * (pen - already);
}

/// <summary>球のワールドAABBを求める</summary>
AABB SphereWorldBounds(const Sphere &s) {
    Vector3 r = {s.radius, s.radius, s.radius};
    return {s.center - r, s.center + r};
}

/// <summary>OBBのワールドAABBを求める</summary>
AABB OBBWorldBounds(const OBB &obb) {
    Vector3 c = obb.scaleCenterRotated;
    Vector3 ext = {
        std::abs(obb.orientations[0].x) * obb.size.x + std::abs(obb.orientations[1].x) * obb.size.y + std::abs(obb.orientations[2].x) * obb.size.z,
        std::abs(obb.orientations[0].y) * obb.size.x + std::abs(obb.orientations[1].y) * obb.size.y + std::abs(obb.orientations[2].y) * obb.size.z,
        std::abs(obb.orientations[0].z) * obb.size.x + std::abs(obb.orientations[1].z) * obb.size.y + std::abs(obb.orientations[2].z) * obb.size.z};
    return {c - ext, c + ext};
}

/// <summary>16要素の行列が完全一致するか（静的メッシュのキャッシュ無効化判定用）</summary>
bool MatrixEqual(const Matrix4x4 &a, const Matrix4x4 &b) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (a.m[i][j] != b.m[i][j])
                return false;
    return true;
}

/// <summary>エッジの重複排除キー（端点を量子化し順不同で正規化）</summary>
struct EdgeKey {
    int64_t a[3];
    int64_t b[3];
    bool operator==(const EdgeKey &o) const {
        return a[0] == o.a[0] && a[1] == o.a[1] && a[2] == o.a[2] &&
               b[0] == o.b[0] && b[1] == o.b[1] && b[2] == o.b[2];
    }
};

struct EdgeKeyHash {
    size_t operator()(const EdgeKey &k) const {
        size_t h = 1469598103934665603ull; // FNV-1a
        auto mix = [&](int64_t v) {
            h ^= static_cast<size_t>(v);
            h *= 1099511628211ull;
        };
        mix(k.a[0]);
        mix(k.a[1]);
        mix(k.a[2]);
        mix(k.b[0]);
        mix(k.b[1]);
        mix(k.b[2]);
        return h;
    }
};

/// <summary>2点から順不同・量子化済みのエッジキーを作る（共有エッジを同一視）</summary>
EdgeKey MakeEdgeKey(const Vector3 &p, const Vector3 &q) {
    const float inv = 1.0f / 1e-4f; // 0.1mm グリッドへ量子化
    int64_t pk[3] = {
        static_cast<int64_t>(std::llround(p.x * inv)),
        static_cast<int64_t>(std::llround(p.y * inv)),
        static_cast<int64_t>(std::llround(p.z * inv))};
    int64_t qk[3] = {
        static_cast<int64_t>(std::llround(q.x * inv)),
        static_cast<int64_t>(std::llround(q.y * inv)),
        static_cast<int64_t>(std::llround(q.z * inv))};

    // 辞書順で小さい方を a に揃え、順不同で一意化する
    bool pFirst = std::lexicographical_compare(pk, pk + 3, qk, qk + 3);
    EdgeKey k;
    const int64_t *lo = pFirst ? pk : qk;
    const int64_t *hi = pFirst ? qk : pk;
    for (int i = 0; i < 3; ++i) {
        k.a[i] = lo[i];
        k.b[i] = hi[i];
    }
    return k;
}
} // namespace

void MeshCollider::BuildFromModel(Model *model) {
    if (!model)
        return;

    std::vector<Triangle> tris;
    ModelData data = model->GetModelData();
    for (const auto &mesh : data.meshes) {
        const auto &verts = mesh.vertices;
        const auto &indices = mesh.indices;

        auto makeTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
            if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
                return;
            Triangle t;
            t.v[0] = {verts[i0].position.x, verts[i0].position.y, verts[i0].position.z};
            t.v[1] = {verts[i1].position.x, verts[i1].position.y, verts[i1].position.z};
            t.v[2] = {verts[i2].position.x, verts[i2].position.y, verts[i2].position.z};
            t.normal = (t.v[1] - t.v[0]).Cross(t.v[2] - t.v[0]).Normalize();
            tris.push_back(t);
        };

        if (!indices.empty()) {
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                makeTriangle(indices[i], indices[i + 1], indices[i + 2]);
            }
        } else {
            for (size_t i = 0; i + 2 < verts.size(); i += 3) {
                makeTriangle(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2));
            }
        }
    }

    BuildFromTriangles(tris);
}

void MeshCollider::BuildFromTriangles(const std::vector<Triangle> &localTriangles) {
    triangles_ = localTriangles;
    BuildBVH();
    BuildEdges();
    worldEdgesValid_ = false; // ワールドエッジキャッシュを作り直させる

    // Mesh×Mesh押し戻し用のローカル・バウンディング球を算出する。
    // 中心はローカルAABBの中点、半径は中心から最も遠い頂点までの距離。
    if (triangles_.empty()) {
        localBoundingCenter_ = {0.0f, 0.0f, 0.0f};
        localBoundingRadius_ = 0.0f;
    } else {
        AABB bounds = TriangleBounds(triangles_[0]);
        for (size_t i = 1; i < triangles_.size(); ++i)
            bounds = MergeAABB(bounds, TriangleBounds(triangles_[i]));
        localBoundingCenter_ = (bounds.min + bounds.max) * 0.5f;
        float maxDistSq = 0.0f;
        for (const auto &t : triangles_)
            for (int k = 0; k < 3; ++k) {
                float dSq = (t.v[k] - localBoundingCenter_).LengthSq();
                if (dSq > maxDistSq)
                    maxDistSq = dSq;
            }
        localBoundingRadius_ = std::sqrt(maxDistSq);
    }

    // 生成直後（最初の更新前）に描画・判定されても正しい行列を使えるよう、
    // ここでワールド行列キャッシュを一度確定させておく。
    UpdateWorldTransform();
}

void MeshCollider::BuildEdges() {
    localEdges_.clear();
    if (triangles_.empty())
        return;

    // 共有エッジ（隣接三角形が共有する辺）は1本にまとめ、描画本数を約半分にする
    std::unordered_set<EdgeKey, EdgeKeyHash> seen;
    seen.reserve(triangles_.size() * 3);
    localEdges_.reserve(triangles_.size() * 3);

    auto addEdge = [&](const Vector3 &p, const Vector3 &q) {
        if (seen.insert(MakeEdgeKey(p, q)).second)
            localEdges_.emplace_back(p, q);
    };

    for (const auto &t : triangles_) {
        addEdge(t.v[0], t.v[1]);
        addEdge(t.v[1], t.v[2]);
        addEdge(t.v[2], t.v[0]);
    }
}

void MeshCollider::BuildBVH() {
    nodes_.clear();
    rootNode_ = -1;
    if (triangles_.empty())
        return;

    nodes_.reserve(triangles_.size() * 2);
    rootNode_ = BuildNode(0, static_cast<int>(triangles_.size()), 0);
}

int MeshCollider::BuildNode(int start, int count, int depth) {
    BVHNode node;

    // この範囲の三角形を内包する境界を計算
    AABB bounds = TriangleBounds(triangles_[start]);
    for (int i = 1; i < count; ++i)
        bounds = MergeAABB(bounds, TriangleBounds(triangles_[start + i]));
    node.bounds = bounds;

    const int kLeafSize = 4;
    const int kMaxDepth = 32;

    // 葉ノード化
    auto makeLeaf = [&]() -> int {
        node.left = -1;
        node.right = -1;
        node.start = start;
        node.count = count;
        int idx = static_cast<int>(nodes_.size());
        nodes_.push_back(node);
        return idx;
    };

    if (count <= kLeafSize || depth >= kMaxDepth)
        return makeLeaf();

    // 重心の広がりから分割軸を選ぶ
    Vector3 cmin = TriangleCentroid(triangles_[start]);
    Vector3 cmax = cmin;
    for (int i = 1; i < count; ++i) {
        Vector3 c = TriangleCentroid(triangles_[start + i]);
        cmin = {(std::min)(cmin.x, c.x), (std::min)(cmin.y, c.y), (std::min)(cmin.z, c.z)};
        cmax = {(std::max)(cmax.x, c.x), (std::max)(cmax.y, c.y), (std::max)(cmax.z, c.z)};
    }
    Vector3 ext = cmax - cmin;
    int axis = (ext.x >= ext.y && ext.x >= ext.z) ? 0 : ((ext.y >= ext.z) ? 1 : 2);

    // 重心の中央値で分割
    int mid = start + count / 2;
    std::nth_element(
        triangles_.begin() + start,
        triangles_.begin() + mid,
        triangles_.begin() + start + count,
        [axis](const Triangle &a, const Triangle &b) {
            Vector3 ca = TriangleCentroid(a);
            Vector3 cb = TriangleCentroid(b);
            float va = (axis == 0) ? ca.x : (axis == 1) ? ca.y : ca.z;
            float vb = (axis == 0) ? cb.x : (axis == 1) ? cb.y : cb.z;
            return va < vb;
        });

    int leftCount = mid - start;
    if (leftCount == 0 || leftCount == count)
        return makeLeaf(); // 分割が退化した場合は葉にする

    // 再帰前にノードスロットを確保（vector再確保があってもインデックスは安定）
    int idx = static_cast<int>(nodes_.size());
    nodes_.push_back(node);
    int l = BuildNode(start, leftCount, depth + 1);
    int r = BuildNode(mid, count - leftCount, depth + 1);
    nodes_[idx].left = l;
    nodes_[idx].right = r;
    nodes_[idx].start = 0;
    nodes_[idx].count = 0;
    return idx;
}

void MeshCollider::QueryCandidates(const AABB &localBounds, std::vector<int> &outIndices) const {
    if (rootNode_ < 0)
        return;

    int stack[64];
    int sp = 0;
    stack[sp++] = rootNode_;

    while (sp > 0) {
        const BVHNode &n = nodes_[stack[--sp]];
        if (!AABBOverlap(n.bounds, localBounds))
            continue;

        if (n.IsLeaf()) {
            for (int i = 0; i < n.count; ++i)
                outIndices.push_back(n.start + i);
        } else if (sp <= 62) {
            stack[sp++] = n.left;
            stack[sp++] = n.right;
        }
    }
}

AABB MeshCollider::WorldBoundsToLocal(const AABB &w) const {
    // ワールド境界の8隅を逆行列でローカルへ変換し、AABBを取り直す（保守的）
    std::array<Vector3, 8> corners = {
        Vector3{w.min.x, w.min.y, w.min.z},
        Vector3{w.max.x, w.min.y, w.min.z},
        Vector3{w.min.x, w.max.y, w.min.z},
        Vector3{w.max.x, w.max.y, w.min.z},
        Vector3{w.min.x, w.min.y, w.max.z},
        Vector3{w.max.x, w.min.y, w.max.z},
        Vector3{w.min.x, w.max.y, w.max.z},
        Vector3{w.max.x, w.max.y, w.max.z}};

    AABB local;
    Vector3 first = Transformation(corners[0], cachedInverse_);
    local.min = first;
    local.max = first;
    for (int i = 1; i < 8; ++i) {
        Vector3 p = Transformation(corners[i], cachedInverse_);
        local.min = {(std::min)(local.min.x, p.x), (std::min)(local.min.y, p.y), (std::min)(local.min.z, p.z)};
        local.max = {(std::max)(local.max.x, p.x), (std::max)(local.max.y, p.y), (std::max)(local.max.z, p.z)};
    }
    return local;
}

void MeshCollider::UpdateWorldTransform() {
    if (getMatrixFunc_) {
        cachedWorld_ = getMatrixFunc_();
    } else {
        // 行列取得関数が無い場合は位置・回転から構築（スケールは単位）
        cachedWorld_ = MakeAffineMatrix({1.0f, 1.0f, 1.0f}, GetCenterRotation(), GetCenterPosition());
    }
    cachedInverse_ = Inverse(cachedWorld_);
    cachedScale_ = ExtractScale(cachedWorld_).x; // 一様スケール前提
}

void MeshCollider::DebugDraw(const ViewProjection &viewProjection) {
    if (!isVisible_ || !isEnabled_ || !isWireframeVisible_ || localEdges_.empty())
        return;

    // 静的メッシュではワールド行列が変わらない限り、エッジのワールド変換を再計算しない。
    // （色 color_ は毎フレーム変わり得るが SetPoints 時に渡すので再変換は不要）
    if (!worldEdgesValid_ || !MatrixEqual(cachedWorld_, lastDrawMatrix_)) {
        worldEdges_.resize(localEdges_.size());
        for (size_t i = 0; i < localEdges_.size(); ++i) {
            worldEdges_[i].first = Transformation(localEdges_[i].first, cachedWorld_);
            worldEdges_[i].second = Transformation(localEdges_[i].second, cachedWorld_);
        }
        lastDrawMatrix_ = cachedWorld_;
        worldEdgesValid_ = true;
    }

    auto *line = DrawLine3D::GetInstance();
    for (const auto &e : worldEdges_)
        line->SetPoints(e.first, e.second, color_);
}

void MeshCollider::SaveToJson() {
    ColliderBase::SaveToJson();

    dataHandler_->Save("sourceModelPath", sourceModelPath_);
    dataHandler_->Save("wireframeVisible", isWireframeVisible_);
}

void MeshCollider::LoadFromJson() {
    ColliderBase::LoadFromJson();

    sourceModelPath_ = dataHandler_->Load<std::string>("sourceModelPath", sourceModelPath_);
    isWireframeVisible_ = dataHandler_->Load<bool>("wireframeVisible", true);
}

bool MeshCollider::Intersect(const Sphere &sphere) const {
    if (triangles_.empty())
        return false;

    std::vector<int> candidates;
    QueryCandidates(WorldBoundsToLocal(SphereWorldBounds(sphere)), candidates);

    for (int i : candidates) {
        Triangle wt = TransformTriangle(triangles_[i], cachedWorld_);
        Vector3 mtv;
        if (TriangleVsSphere(wt, sphere.center, sphere.radius, mtv))
            return true;
    }
    return false;
}

bool MeshCollider::Intersect(const OBB &obb) const {
    if (triangles_.empty())
        return false;

    std::vector<int> candidates;
    QueryCandidates(WorldBoundsToLocal(OBBWorldBounds(obb)), candidates);

    const Vector3 u[3] = {obb.orientations[0], obb.orientations[1], obb.orientations[2]};
    const float e[3] = {obb.size.x, obb.size.y, obb.size.z};

    for (int i : candidates) {
        Triangle wt = TransformTriangle(triangles_[i], cachedWorld_);
        Vector3 mtv;
        if (TriangleVsBox(wt, obb.scaleCenterRotated, u, e, mtv))
            return true;
    }
    return false;
}

bool MeshCollider::Intersect(const AABB &aabb) const {
    if (triangles_.empty())
        return false;

    std::vector<int> candidates;
    QueryCandidates(WorldBoundsToLocal(aabb), candidates);

    Vector3 center = (aabb.min + aabb.max) * 0.5f;
    const Vector3 u[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    const float e[3] = {(aabb.max.x - aabb.min.x) * 0.5f, (aabb.max.y - aabb.min.y) * 0.5f, (aabb.max.z - aabb.min.z) * 0.5f};

    for (int i : candidates) {
        Triangle wt = TransformTriangle(triangles_[i], cachedWorld_);
        Vector3 mtv;
        if (TriangleVsBox(wt, center, u, e, mtv))
            return true;
    }
    return false;
}

bool MeshCollider::Intersect(const MeshCollider &other) const {
    if (triangles_.empty() || other.triangles_.empty())
        return false;

    // 自分の各三角形（ワールド）を相手BVHで絞り込み、三角形同士で判定
    for (const auto &localTri : triangles_) {
        Triangle wt = TransformTriangle(localTri, cachedWorld_);
        AABB worldBounds = TriangleBounds(wt);

        std::vector<int> candidates;
        other.QueryCandidates(other.WorldBoundsToLocal(worldBounds), candidates);

        for (int j : candidates) {
            Triangle ot = TransformTriangle(other.triangles_[j], other.cachedWorld_);
            if (TriangleVsTriangle(wt, ot))
                return true;
        }
    }
    return false;
}

bool MeshCollider::Depenetrate(const Sphere &sphere, Vector3 &outMTV) const {
    if (triangles_.empty())
        return false;

    std::vector<int> candidates;
    QueryCandidates(WorldBoundsToLocal(SphereWorldBounds(sphere)), candidates);

    Vector3 sum = {0.0f, 0.0f, 0.0f};
    bool hit = false;
    for (int i : candidates) {
        Triangle wt = TransformTriangle(triangles_[i], cachedWorld_);
        Vector3 mtv;
        if (TriangleVsSphere(wt, sphere.center, sphere.radius, mtv)) {
            AccumulateMTV(sum, mtv);
            hit = true;
        }
    }
    outMTV = sum;
    return hit;
}

bool MeshCollider::Depenetrate(const OBB &obb, Vector3 &outMTV) const {
    if (triangles_.empty())
        return false;

    std::vector<int> candidates;
    QueryCandidates(WorldBoundsToLocal(OBBWorldBounds(obb)), candidates);

    const Vector3 u[3] = {obb.orientations[0], obb.orientations[1], obb.orientations[2]};
    const float e[3] = {obb.size.x, obb.size.y, obb.size.z};

    Vector3 sum = {0.0f, 0.0f, 0.0f};
    bool hit = false;
    for (int i : candidates) {
        Triangle wt = TransformTriangle(triangles_[i], cachedWorld_);
        Vector3 mtv;
        if (TriangleVsBox(wt, obb.scaleCenterRotated, u, e, mtv)) {
            AccumulateMTV(sum, mtv);
            hit = true;
        }
    }
    outMTV = sum;
    return hit;
}

bool MeshCollider::Depenetrate(const AABB &aabb, Vector3 &outMTV) const {
    if (triangles_.empty())
        return false;

    std::vector<int> candidates;
    QueryCandidates(WorldBoundsToLocal(aabb), candidates);

    Vector3 center = (aabb.min + aabb.max) * 0.5f;
    const Vector3 u[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    const float e[3] = {(aabb.max.x - aabb.min.x) * 0.5f, (aabb.max.y - aabb.min.y) * 0.5f, (aabb.max.z - aabb.min.z) * 0.5f};

    Vector3 sum = {0.0f, 0.0f, 0.0f};
    bool hit = false;
    for (int i : candidates) {
        Triangle wt = TransformTriangle(triangles_[i], cachedWorld_);
        Vector3 mtv;
        if (TriangleVsBox(wt, center, u, e, mtv)) {
            AccumulateMTV(sum, mtv);
            hit = true;
        }
    }
    outMTV = sum;
    return hit;
}

Sphere MeshCollider::GetWorldBoundingSphere() const {
    Sphere s;
    s.center = Transformation(localBoundingCenter_, cachedWorld_);
    s.radius = localBoundingRadius_ * cachedScale_; // 一様スケール前提
    return s;
}
} // namespace Hagine
