#pragma once
#define NOMINMAX
#include "ColliderBase.h"
#include "type/AABBCollider.h"
#include "type/CylinderCollider.h"
#include "type/MeshCollider.h"
#include "type/OBBCollider.h"
#include "type/SphereCollider.h"
#include <camera/projection/ViewProjection.h>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

struct ImDrawList;
struct ImVec2;

namespace Hagine {

/// <summary>
/// レイキャストが当たった1点の情報
/// </summary>
struct RaycastHit
{
    ColliderBase *pCollider = nullptr; //!< 当たったコライダー
    Vector3 position{};                //!< 当たった点（ワールド空間）
    Vector3 normal{};                  //!< 当たった面の法線（レイと向かい合う側・ワールド空間）
    float distance = 0.0f;             //!< 始点からの距離
};

/// <summary>
/// 全コライダーの登録・更新・衝突判定を統括するシングルトン
/// タグごとにグループ化し、衝突マスクに基づいてペアの判定を行う
/// </summary>
class CollisionManager
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>CollisionManager*: シングルトンインスタンス</returns>
    static CollisionManager *GetInstance()
    {
        static CollisionManager instance;
        return &instance;
    }

    /// <summary>
    /// コライダーを登録
    /// </summary>
    /// <param name="pCollider">登録するコライダー</param>
    void Register(ColliderBase *pCollider);

    /// <summary>
    /// コライダーの登録を解除
    /// </summary>
    /// <param name="pCollider">解除するコライダー</param>
    void Unregister(ColliderBase *pCollider);

    /// <summary>
    /// 登録済みコライダーを全てクリア
    /// </summary>
    void Clear();

    /// <summary>
    /// コライダーのタグ変更に追従してグループを更新
    /// </summary>
    /// <param name="pCollider">対象のコライダー</param>
    /// <param name="oldTag">変更前のタグ</param>
    /// <param name="newTag">変更後のタグ</param>
    void UpdateColliderTag(ColliderBase *pCollider, const std::string &oldTag, const std::string &newTag);

    /// <summary>
    /// 更新処理（ワールド変換更新と衝突判定）
    /// </summary>
    void Update();

    /// <summary>
    /// 全コライダーのデバッグ描画
    /// </summary>
    /// <param name="viewProjection">ビュープロジェクション</param>
    void DebugDraw(const ViewProjection &viewProjection);

    /// <summary>
    /// 登録済みコライダーへレイを飛ばし、一番手前で当たった点を返す。
    ///
    /// 足IKの接地探しや、音の遮蔽判定のように「衝突ペア」ではなく
    /// 一方向の当たりだけが欲しい場面で使う。
    /// 判定が無効（IsEnabled が false）のコライダーは無視する。
    ///
    /// 円柱コライダーは押し出しと同じく「Y軸に直立した円柱」として当てる。
    /// </summary>
    /// <param name="origin">レイの始点（ワールド空間）</param>
    /// <param name="direction">レイの方向（正規化不要）</param>
    /// <param name="maxDistance">判定する最大距離</param>
    /// <param name="tagFilter">対象にするタグ（空なら全タグ）</param>
    /// <param name="outHit">当たった点の情報</param>
    /// <param name="ignoreOwnerName">このオブジェクト名を持つコライダーは無視する
    /// （足元へレイを撃つときに自分の体へ当たるのを防ぐ用。空なら無視しない）</param>
    /// <param name="pIgnore">無視するコライダー1つ（不要なら nullptr）</param>
    /// <returns>bool: 当たったら true</returns>
    bool RaycastClosest(const Vector3 &origin, const Vector3 &direction, float maxDistance,
                        const std::vector<std::string> &tagFilter, RaycastHit &outHit,
                        const std::string &ignoreOwnerName = {}, const ColliderBase *pIgnore = nullptr) const;

    /// <summary>
    /// OBB同士のめり込み解消ベクトル（MTV）を計算
    /// </summary>
    /// <param name="a">OBBコライダーA</param>
    /// <param name="b">OBBコライダーB</param>
    /// <param name="outMTV">出力されるめり込み解消ベクトル</param>
    /// <returns>bool: めり込みがあれば true</returns>
    bool CalculateDepenetration(OBBCollider *a, OBBCollider *b, Vector3 &outMTV);

    /// <summary>
    /// AABB同士のめり込み解消ベクトル（MTV）を計算
    /// </summary>
    /// <param name="a">AABBコライダーA</param>
    /// <param name="b">AABBコライダーB</param>
    /// <param name="outMTV">出力されるめり込み解消ベクトル</param>
    /// <returns>bool: めり込みがあれば true</returns>
    bool CalculateDepenetration(AABBCollider *a, AABBCollider *b, Vector3 &outMTV);

    /// <summary>
    /// 任意の2コライダーのめり込み解消ベクトル（MTV）を計算する統一API。
    /// ヒット判定（TestCollision）とは独立した「押し戻し」専用の入口。
    /// 形状ペアに応じて適切な計算へディスパッチし、a を b から押し出す向きの MTV を返す。
    /// Sphere/AABB/OBB/Cylinder/Mesh の全組み合わせに対応する。
    /// ・Mesh×形状：メッシュ三角形からの正確な押し出し（円柱はAABB近似）。
    /// ・Mesh×Mesh：動かす側 a をワールド外接球で近似し、相手メッシュ b から押し出す
    ///   （球モデル等のコンパクトな凸形状なら近似誤差は小さい。巨大な静的メッシュを
    ///   動かす側にすると外接球が破綻するため、静的側は押し出しOFFで運用すること）。
    /// ・円柱が絡むペアはY軸直立円柱とみなしたXZ近似（inwardは内側へ、それ以外は外側へ）。
    /// </summary>
    /// <param name="a">押し出される側のコライダー</param>
    /// <param name="b">押し出す基準となるコライダー</param>
    /// <param name="outMTV">a に加算するとめり込みが解消される MTV</param>
    /// <returns>bool: めり込みがあれば true</returns>
    bool ComputeDepenetration(ColliderBase *a, ColliderBase *b, Vector3 &outMTV);

    /// <summary>
    /// OBBと円柱の衝突判定
    /// </summary>
    /// <param name="obb">OBBコライダー</param>
    /// <param name="cylinder">円柱コライダー</param>
    /// <returns>bool: 衝突していれば true</returns>
    bool IsCollisionOBBCylinder(OBBCollider *obb, CylinderCollider *cylinder);

    /// <summary>
    /// OBBと円柱のめり込み解消ベクトル（MTV）を計算
    /// </summary>
    /// <param name="obb">OBBコライダー</param>
    /// <param name="cylinder">円柱コライダー</param>
    /// <param name="outMTV">出力されるめり込み解消ベクトル</param>
    /// <returns>bool: めり込みがあれば true</returns>
    bool CalculateDepenetrationOBBCylinder(OBBCollider *obb, CylinderCollider *cylinder, Vector3 &outMTV);

#ifdef USE_IMGUI
    /// <summary>
    /// タグの追加・削除UI（タグ管理タブ）
    /// </summary>
    void ImGuiTagManager()
    {
        ColliderTagManager::GetInstance()->ImGuiTagManager();
    }

    /// <summary>
    /// コライダー窓（コライダー設定タブ）。一覧から選んで、形・タグ・当たる相手・押し出し・
    /// 保存・削除まで全部ここで編集できる。オブジェクトへのコライダーの追加もここから行える
    /// </summary>
    void ImGuiColliderInspector();

    /// <summary>
    /// コライダー1つの詳細の編集UI（コライダー窓の右側。インスペクタからも同じ物を呼ぶ）
    /// </summary>
    /// <param name="pCollider">編集するコライダー</param>
    void DrawColliderEditor(ColliderBase *pCollider);

    /// <summary>コライダー窓を開いて、このコライダーを選んだ状態にする</summary>
    void OpenInEditor(ColliderBase *pCollider);

    /// <summary>OpenInEditor の依頼を受け取る（窓を持つ側が毎フレーム呼ぶ。依頼があれば true）</summary>
    bool ConsumeEditorOpenRequest();

    /// <summary>
    /// エディタから頼まれた削除などを行う。UIを描いていない所（フレームの頭）で毎フレーム呼ぶ
    /// </summary>
    void ProcessEditorRequests();

    /// <summary>今このコライダーと当たっている相手の一覧</summary>
    std::vector<ColliderBase *> GetCollidingPartners(const ColliderBase *pCollider) const;

    /// <summary>
    /// 窓は開かずに、このコライダーを選んだ状態にする（インスペクタのコライダー欄から）
    /// </summary>
    void SelectInEditor(ColliderBase *pCollider)
    {
        pInspectorSelected_ = pCollider;
        MarkEditorShown();
    }

    /// <summary>
    /// 選んだコライダーを、シーン窓の上に重ねて描く（物の中に埋まっていても見えるように）。
    /// シーン窓の描画中に呼ぶ
    /// </summary>
    /// <param name="pDrawList">シーン窓の描画リスト</param>
    /// <param name="viewProjection">シーンのカメラ</param>
    /// <param name="sceneMin">シーンの絵の左上（画面座標）</param>
    /// <param name="sceneSize">シーンの絵の大きさ</param>
    void DrawSelectedOverlay(ImDrawList *pDrawList, const ViewProjection &viewProjection, const ImVec2 &sceneMin, const ImVec2 &sceneSize);

    /// <summary>全体の表示スイッチ（「デバッグ線」窓と結び付ける）</summary>
    bool *GetVisibleFlag() { return &isVisible_; }

    /// <summary>コライダーの詳細をこのフレームに出した（選んだコライダーの強調はこの間だけ出す）</summary>
    void MarkEditorShown();
#endif

    /// <summary>タグ名から決まる見分けやすい色（同じタグはいつも同じ色）</summary>
    static Vector4 TagColor(const std::string &tag);

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    CollisionManager() = default;

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~CollisionManager() = default;
    CollisionManager(const CollisionManager &) = delete;
    CollisionManager &operator=(const CollisionManager &) = delete;

    /// <summary>
    /// 全コライダーのワールド変換を更新
    /// </summary>
    void UpdateColliders();

    /// <summary>
    /// 1つのコライダーに対するレイ判定。形状ごとの計算へ振り分ける
    /// </summary>
    /// <param name="pCollider">対象のコライダー</param>
    /// <param name="origin">レイの始点</param>
    /// <param name="direction">レイの方向（正規化済み）</param>
    /// <param name="maxDistance">判定する最大距離</param>
    /// <param name="outDistance">始点からの距離</param>
    /// <param name="outNormal">当たった面の法線</param>
    /// <returns>bool: 当たったら true</returns>
    static bool RaycastCollider(const ColliderBase *pCollider, const Vector3 &origin, const Vector3 &direction,
                                float maxDistance, float &outDistance, Vector3 &outNormal);

    /// <summary>
    /// 衝突対象となるペアを総当たりで判定
    /// </summary>
    void CheckCollisions();

    /// <summary>
    /// コライダーペアの衝突状態を判定しコールバックを発火
    /// </summary>
    /// <param name="a">コライダーA</param>
    /// <param name="b">コライダーB</param>
    void CheckCollisionPair(ColliderBase *a, ColliderBase *b);

    /// <summary>
    /// 2つのコライダーの形状に応じた衝突判定を実行
    /// </summary>
    /// <param name="a">コライダーA</param>
    /// <param name="b">コライダーB</param>
    /// <returns>bool: 衝突していれば true</returns>
    bool TestCollision(ColliderBase *a, ColliderBase *b);

    /// 各種衝突判定関数
    bool IsCollision(const Sphere &s1, const Sphere &s2);
    bool IsCollision(const AABB &aabb1, const AABB &aabb2);
    bool IsCollision(const OBB &obb1, const OBB &obb2);
    bool IsCollision(const AABB &aabb, const Sphere &sphere);
    bool IsCollision(const OBB &obb, const Sphere &sphere);
    bool IsCollision(const AABB &aabb, const OBB &obb);

    /// 分離軸判定のヘルパー関数
    void ProjectOBB(const OBB &obb, const Vector3 &axis, float &min, float &max);
    void ProjectAABB(const Vector3 &axis, const AABB &aabb, float &outMin, float &outMax);
    bool TestAxis(const Vector3 &axis, const OBB &obb1, const OBB &obb2);
    bool TestAxis(const Vector3 &axis, const AABB &aabb, const OBB &obb);

    /// ===================================================
    /// private variables
    /// ===================================================

    // タグごとにコライダーをグループ化（文字列キー対応）
    std::unordered_map<std::string, std::vector<ColliderBase *>> collidersByTag_;

    /// <summary>
    /// 衝突状態を管理するためのコライダーペア
    /// </summary>
    struct CollisionPair
    {
        ColliderBase *a;
        ColliderBase *b;

        bool operator==(const CollisionPair &other) const
        {
            return (a == other.a && b == other.b) || (a == other.b && b == other.a);
        }
    };

    /// <summary>
    /// コライダーペアのハッシュ関数（順序非依存）
    /// </summary>
    struct CollisionPairHash
    {
        std::size_t operator()(const CollisionPair &pair) const
        {
            auto ptrA = reinterpret_cast<std::uintptr_t>(pair.a);
            auto ptrB = reinterpret_cast<std::uintptr_t>(pair.b);
            return std::hash<std::uintptr_t>{}((std::min)(ptrA, ptrB)) ^
                   (std::hash<std::uintptr_t>{}((std::max)(ptrA, ptrB)) << 1);
        }
    };

    std::unordered_map<CollisionPair, bool, CollisionPairHash> collisionStates_; // ペアごとの衝突状態

    bool isVisible_ = false; // コライダーのデバッグ表示フラグ（全体）
    Vector4 hitColor_ = {1.0f, 0.25f, 0.25f, 1.0f}; // 当たっている間の線の色

#ifdef USE_IMGUI
    ColliderBase *pInspectorSelected_ = nullptr; // インスペクタで選択中のコライダー
    std::string inspectorSearch_;                // 一覧の絞り込み（名前・タグ）
    int inspectorTypeFilter_ = -1;               // 種類の絞り込み（-1 = すべて / ColliderType）
    bool inspectorEnabledOnly_ = false;          // 当たり判定が有効な物だけ
    bool highlightSelected_ = true;              // 選んだコライダーをシーンで点滅させる
    bool highlightXRay_ = true;                  // 点滅に加えて、物に隠れても見えるようシーン窓に重ねて描く
    bool colorByTag_ = false;                    // タグの色で塗り分けている（設定の色は書き換えない）
    Vector4 highlightColor_ = {1.0f, 0.82f, 0.15f, 1.0f}; // 選んだコライダーの点滅の色
    int editorShownFrame_ = -100;                // コライダーの詳細を最後に出したフレーム（強調はその間だけ）

    /// <summary>コライダーがまだ登録されているか（ポインタの比較だけで確かめる）</summary>
    bool IsRegistered(const ColliderBase *pCollider) const;

    /// <summary>タグの色で塗り分けるか（塗り分けは描くときに決めるので、外せば設定の色に戻る）</summary>
    void SetColorByTag(bool enable) { colorByTag_ = enable; }

    /// <summary>強調して見せるコライダー（無ければ nullptr）</summary>
    ColliderBase *GetHighlightedCollider() const;

    /// <summary>シーンの選択が変わったら、その物のコライダーを選ぶ（窓が閉じていても追従する）</summary>
    void FollowSceneSelection();

    /// <summary>「＋追加」のポップアップ（付ける物と形を選ぶ）</summary>
    void DrawAddColliderPopup();

    bool inspectorGroupByOwner_ = true;      // 一覧を持ち主ごとにまとめる（false ならタグごと）
    bool inspectorSelectedOnly_ = false;     // シーンで選んでいる物のコライダーだけ出す
    bool inspectorFollowSelection_ = true;   // シーンで選んだ物のコライダーを一覧でも選ぶ
    std::string lastSceneSelection_;         // 前のフレームにシーンで選んでいた物
    std::string addOwnerName_;               // 「＋追加」で付ける先
    std::string addOwnerFilter_;             // 付ける先を選ぶときの絞り込み
    ColliderBase *pPendingDelete_ = nullptr; // 次のフレームの頭で消すコライダー
    bool openEditorRequested_ = false;       // コライダー窓を開いてほしいという依頼
#endif
};
} // namespace Hagine
