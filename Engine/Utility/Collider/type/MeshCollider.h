#pragma once
#include "../ColliderBase.h"
#include "type/Matrix4x4.h"
#include <functional>
#include <vector>

namespace Hagine {

class Model;

/// <summary>
/// 三角形メッシュのコライダー
/// モデルの頂点データから三角形群を構築し、BVHで高速化した判定を行う。
/// 静的な複雑形状（地形・壁・障害物）の正確な衝突に用いる。
/// 判定・押し戻しの相手形状はワールド空間で受け取り、各候補三角形を
/// ワールド空間へ変換して評価する（BVHはローカル空間のまま保持し再構築不要）。
/// ※ 非一様スケールには未対応（一様スケール前提）。
/// </summary>
class MeshCollider : public ColliderBase {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    MeshCollider() = default;
    ~MeshCollider() override = default;

    /// <summary>
    /// モデルの全メッシュからローカル空間の三角形群を構築しBVHを作る
    /// </summary>
    /// <param name="model">元となるモデル</param>
    void BuildFromModel(Model *model);

    /// <summary>
    /// ローカル空間の三角形群を直接与えてBVHを作る
    /// </summary>
    /// <param name="localTriangles">ローカル空間の三角形配列</param>
    void BuildFromTriangles(const std::vector<Triangle> &localTriangles);

    /// <summary>
    /// ワールド行列の取得関数を設定（スケールを含む変換に使用）
    /// </summary>
    /// <param name="func">ワールド行列を返す関数</param>
    void SetMatrixGetter(std::function<Matrix4x4()> func) { getMatrixFunc_ = func; }

    /// <summary>
    /// コライダーの形状種別を取得
    /// </summary>
    /// <returns>ColliderType: Mesh</returns>
    ColliderType GetType() const override { return ColliderType::Mesh; }

    /// <summary>
    /// ワールド変換（行列とその逆行列）を更新
    /// </summary>
    void UpdateWorldTransform() override;

    /// <summary>
    /// デバッグ描画（三角形ワイヤーフレーム）
    /// </summary>
    /// <param name="viewProjection">ビュープロジェクション</param>
    void DebugDraw(const ViewProjection &viewProjection) override;

    /// <summary>
    /// 設定をJsonへ保存
    /// </summary>
    void SaveToJson() override;

    /// <summary>
    /// 設定をJsonから読み込み
    /// </summary>
    void LoadFromJson() override;

    /// ===================================================
    /// ヒット判定（相手形状はワールド空間）
    /// ===================================================

    bool Intersect(const Sphere &sphere) const;
    bool Intersect(const OBB &obb) const;
    bool Intersect(const AABB &aabb) const;
    bool Intersect(const MeshCollider &other) const;

    /// ===================================================
    /// 押し戻し（相手形状をこのメッシュから押し出すMTVを返す）
    /// outMTV は「相手形状」に加算するとめり込みが解消される向き・量
    /// ===================================================

    bool Depenetrate(const Sphere &sphere, Vector3 &outMTV) const;
    bool Depenetrate(const OBB &obb, Vector3 &outMTV) const;
    bool Depenetrate(const AABB &aabb, Vector3 &outMTV) const;

    /// <summary>
    /// 全三角形を内包するワールド空間のバウンディング球を返す。
    /// Mesh×Meshの押し戻しで、動かす側を球近似して相手メッシュから押し出す用途に使う。
    /// （球モデルのような凸でコンパクトな形状なら近似誤差は小さい）
    /// </summary>
    /// <returns>Sphere: ワールド空間の中心と半径（未構築なら半径0）</returns>
    Sphere GetWorldBoundingSphere() const;

    /// ===================================================
    /// getter / setter
    /// ===================================================

    void SetSourceModelPath(const std::string &path) { sourceModelPath_ = path; }
    const std::string &GetSourceModelPath() const { return sourceModelPath_; }
    size_t GetTriangleCount() const { return triangles_.size(); }
    void SetWireframeVisible(bool visible) { isWireframeVisible_ = visible; }
    bool IsWireframeVisible() const { return isWireframeVisible_; }
    bool IsBuilt() const { return !triangles_.empty(); }

  private:
    /// ===================================================
    /// private struct
    /// ===================================================

    /// <summary>
    /// BVHノード（フラット配列で保持）
    /// 内部ノードは left/right に子インデックスを持ち、葉ノードは start/count に
    /// triangles_ 配列内の三角形範囲を持つ
    /// </summary>
    struct BVHNode {
        AABB bounds{};   // ノードの境界ボックス
        int left = -1;   // 左子ノードインデックス（葉なら-1）
        int right = -1;  // 右子ノードインデックス
        int start = 0;   // 葉: 三角形開始インデックス
        int count = 0;   // 葉: 三角形数
        bool IsLeaf() const { return left < 0; }
    };

    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>BVH全体を構築</summary>
    void BuildBVH();

    /// <summary>BVHノードを再帰構築（triangles_ をその場で並べ替える）</summary>
    /// <returns>生成したノードのインデックス</returns>
    int BuildNode(int start, int count, int depth);

    /// <summary>デバッグ描画用に、重複を除いたローカル空間のエッジ群を構築する</summary>
    void BuildEdges();

    /// <summary>ローカル空間の問い合わせ境界に重なる三角形インデックスをBVHから収集</summary>
    void QueryCandidates(const AABB &localBounds, std::vector<int> &outIndices) const;

    /// <summary>ワールド空間の境界ボックスをローカル空間の境界ボックスへ変換（保守的）</summary>
    AABB WorldBoundsToLocal(const AABB &worldBounds) const;

    /// ===================================================
    /// private variants
    /// ===================================================

    std::vector<Triangle> triangles_; // ローカル空間の三角形群（BVH構築で並べ替えられる）
    std::vector<BVHNode> nodes_;      // BVHノード配列
    int rootNode_ = -1;               // ルートノードインデックス

    // デバッグ描画の高速化用：重複を除いたローカルエッジと、そのワールド変換キャッシュ
    std::vector<std::pair<Vector3, Vector3>> localEdges_; // 重複排除済みローカルエッジ
    std::vector<std::pair<Vector3, Vector3>> worldEdges_; // ワールド変換済みエッジ（キャッシュ）
    Matrix4x4 lastDrawMatrix_ = MakeIdentity4x4();        // 前回ワールド変換に使った行列
    bool worldEdgesValid_ = false;                        // ワールドエッジキャッシュが有効か

    std::function<Matrix4x4()> getMatrixFunc_;       // ワールド行列取得関数
    // 初回 UpdateWorldTransform 前に描画・判定されても不正な行列にならないよう単位行列で初期化する
    Matrix4x4 cachedWorld_ = MakeIdentity4x4();      // キャッシュしたワールド行列
    Matrix4x4 cachedInverse_ = MakeIdentity4x4();    // その逆行列
    float cachedScale_ = 1.0f;                       // 一様スケール（半径・距離補正用）

    std::string sourceModelPath_;       // 構築元モデルのパス（保存用）
    bool isWireframeVisible_ = true;    // 三角形ワイヤー表示フラグ

    // Mesh×Mesh押し戻し用：全三角形を内包するローカル空間のバウンディング球（構築時に算出）
    Vector3 localBoundingCenter_ = {0.0f, 0.0f, 0.0f};
    float localBoundingRadius_ = 0.0f;
};
} // namespace Hagine
