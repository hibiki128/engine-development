#pragma once
#ifdef USE_IMGUI

#include "imgui.h"
#include "ImGuizmo.h"
#include <Input.h>
#include <object/base/BaseObject.h>
#include <transform/WorldTransform.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hagine {
class Sprite;

/// <summary>
/// ギズモ操作対象の大分類。
/// 型（Type）とは別に、ユーザーが「どの種類を動かすか」を選べるようにするための区分。
/// フィルタUI・シーン保存の仕分けなどで利用する。
/// </summary>
enum class GizmoCategory
{
    Object = 0,   // 3Dオブジェクト（BaseObject 等）
    Sprite = 1,   // 2Dスプライト
    Particle = 2, // パーティクルエミッター（CPU/GPU）
    Light = 3,    // 光源（LightGroup のポイント／スポットライト）
};
// フィルタ配列などのサイズに使う要素数
inline constexpr int kGizmoCategoryCount = 4;

/// <summary>
/// ギズモ操作対象を型に依存せず統一的に扱うためのラッパー構造体
/// BaseObject・WorldTransform・Vector3直接参照・Spriteの各種に対応する
/// </summary>
struct GizmoTarget
{
    enum class Type
    {
        BaseObject,     // BaseObject* を持つオブジェクト
        WorldTransform, // WorldTransform* のみを持つオブジェクト
        FreeTransform,  // Vector3* の直接参照（ParticleEmitter など）
        Sprite2D,       // Sprite の Vector2* 位置（ピクセル座標・XY のみ）
    };

    Type type = Type::BaseObject;
    GizmoCategory category = GizmoCategory::Object; // 操作対象フィルタ用の大分類
    std::string name;
    bool selectable = true;        // ギズモによる選択を許可するか
    bool isScreenSpace = false;    // スクリーン空間座標（ピクセル単位）かどうか（Sprite用）
    float screenHitRadius = 50.0f; // 2Dマウス選択の当たり判定半径（スプライト座標系ピクセル単位）

    // Type::BaseObject 用
    BaseObject *baseObject = nullptr;

    // Type::WorldTransform 用
    WorldTransform *worldTransform = nullptr;

    // Type::FreeTransform 用（各変換成分のメンバ変数への直接ポインタ）
    Vector3 *translate = nullptr; // 平行移動
    Vector3 *rotate = nullptr;    // 回転（オイラー角、ラジアン）
    Vector3 *scale = nullptr;     // スケール

    // Type::Sprite2D 用（Sprite::position_ への直接ポインタ）
    Vector2 *position2D = nullptr;

    // スクリーン空間の当たり判定をカスタマイズする関数。
    // 設定されている場合は screenHitRadius の円判定より優先される。
    // 引数はスプライト座標系（仮想解像度ピクセル）のマウス位置。
    // スプライトのように原点が矩形の角にある対象は、円判定だと本体をクリックしても
    // 当たらないため、実際の矩形で判定させる用途で使う。
    std::function<bool(const Vector2 &)> screenHitTest;

    // ImGui 詳細表示コールバック（nullptr の場合はデフォルト表示）
    std::function<void()> imguiCallback;

    /// <summary>
    /// ワールド行列を構築して返す
    /// </summary>
    /// <returns>Matrix4x4: ワールド行列</returns>
    Matrix4x4 GetWorldMatrix() const;

    /// <summary>
    /// ワールド座標（位置成分）を返す
    /// </summary>
    /// <returns>Vector3: ワールド座標</returns>
    Vector3 GetWorldPosition() const;

    /// <summary>
    /// 平行移動デルタを各型に応じて適用する
    /// </summary>
    /// <param name="delta">適用する平行移動量</param>
    void ApplyTranslationDelta(const Vector3 &delta);

    /// <summary>
    /// ギズモ操作後のワールド行列を各型に応じて反映する（移動・回転・拡縮すべて）
    /// 親を持つ対象は親のワールド行列を打ち消してローカル成分へ戻してから書き込む
    /// スクリーン空間（Sprite）は XY 平行移動のみ反映する
    /// </summary>
    /// <param name="worldMatrix">適用するワールド行列</param>
    void ApplyWorldMatrix(const Matrix4x4 &worldMatrix);

    /// <summary>
    /// マウス選択・フォーカス用のローカル空間AABBを返す
    /// BaseObject はモデルの実形状、それ以外は単位サイズのボックスになる
    /// </summary>
    /// <returns>AABB: ローカル空間の境界ボックス</returns>
    AABB GetLocalBounds() const;

    /// <summary>
    /// ImGui で変換詳細を表示する
    /// </summary>
    void ShowImGui();
};

/// <summary>
/// ImGuizmoを用いたオブジェクトのギズモ操作（移動・回転・拡縮）を管理するシングルトン
/// 複数選択・コピー＆ペースト・マウス選択・デバッグ描画などを提供する
/// </summary>
class ImGuizmoManager
{
  private:
    /// <summary>
    /// コンストラクタ
    /// </summary>
    ImGuizmoManager() = default;

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~ImGuizmoManager() = default;
    ImGuizmoManager(const ImGuizmoManager &) = delete;
    ImGuizmoManager &operator=(const ImGuizmoManager &) = delete;

    // 操作対象一覧（名前付き、GizmoTarget で型を統一管理）
    std::unordered_map<std::string, GizmoTarget> transformMap_;
    // 選択されているオブジェクト名のセット
    std::unordered_set<std::string> selectedNames_;

    // コピー対象（BaseObject のみ）。
    // ポインタで持つとコピー後に元を削除された時点でぶら下がるので、名前で持って貼り付け時に引き直す。
    std::vector<std::string> copiedNames_;

    bool isMultiSelecting_ = false;
    bool isDrawDebug_ = true;

    // 操作対象フィルタ。GizmoCategory ごとに ON/OFF。
    // 無効な種類は選択・マウスピック・ギズモ表示・デバッグ描画の対象外になる。
    bool categoryEnabled_[kGizmoCategoryCount] = {true, true, true, true};

    // カメラのビュープロジェクション
    const ViewProjection *pViewProjection_ = nullptr;

    // 現在の操作モード・座標空間
    ImGuizmo::OPERATION currentOperation_ = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE currentMode_ = ImGuizmo::LOCAL;

    // ---- スナップ（グリッド吸着）----
    // 並べ物を作るときに座標を手打ちしなくて済むよう、操作量を一定刻みに丸める。
    // 常時ONにすると微調整ができないので、Shift 押下中だけ一時的に反転させられる。
    bool useSnap_ = false;
    float snapTranslate_ = 1.0f;      // 平行移動の刻み幅（ワールド単位）
    float snapRotateDegree_ = 15.0f;  // 回転の刻み幅（度）
    float snapScale_ = 0.1f;          // 拡縮の刻み幅

    // ---- 矩形（ラバーバンド）選択 ----
    // シーン上を空ドラッグしたら枠を出し、離した時点で枠に入っている対象をまとめて選ぶ。
    bool isBoxSelecting_ = false;
    ImVec2 boxSelectStart_ = {0.0f, 0.0f};
    // ドラッグ量がしきい値未満のままボタンを離した＝「クリック」だったことを示す。
    // 単体選択はこれを見て確定する（押した瞬間に選ぶと、矩形ドラッグの開始点にある物を
    // 一度掴んでしまい、選択が一瞬ちらつくため）。
    bool clickSelectRequested_ = false;
    // これ未満のドラッグは「クリック」として扱い、矩形選択にしない（ピクセル）
    static constexpr float kBoxSelectThreshold = 6.0f;

    // ---- 視点フォーカス要求 ----
    // F キーで選択オブジェクトへ寄る。実際にカメラを動かすのは DebugCamera 側なので、
    // ここでは要求だけ立てて ConsumeFocusRequest で受け渡す。
    bool focusRequested_ = false;
    Vector3 focusTarget_ = {0.0f, 0.0f, 0.0f};
    float focusRadius_ = 1.0f;

    bool showDebugRaycast_ = true;
    bool showDebugAABB_ = true;
    bool showDebugSphere_ = false;
    bool showDebugHitPoints_ = false;
    // 全オブジェクトの枠を出すと画面が線だらけになるので、既定は選択中のみ
    bool debugSelectedOnly_ = true;
    char searchBuffer_[256] = "";
    std::vector<std::string> filteredNames_;

    // Tab キーによる重複オブジェクトのサイクル選択用
    std::vector<std::pair<std::string, float>> overlapCandidates_; // (name, rayDistance)
    int overlapCycleIndex_ = 0;

  public:
    static ImGuizmoManager *GetInstance()
    {
        static ImGuizmoManager instance;
        return &instance;
    }

    void Finalize();
    void BeginFrame();
    void SetViewProjection(ViewProjection *pViewProjection);

    // ---- AddTarget オーバーロード群 ----

    /// BaseObject を登録する（既存の使い方）
    void AddTarget(const std::string &name, BaseObject *pObject, bool selectable = true);

    /// WorldTransform のみを持つオブジェクトを登録する
    /// imguiCallback を渡すと ImGui 表示をカスタマイズできる
    void AddTarget(const std::string &name, WorldTransform *worldTransform,
                   bool selectable = true,
                   std::function<void()> imguiCallback = nullptr);

    /// Vector3 ポインタを直接指定して登録する（ParticleEmitter など）
    /// translate は必須、rotate・scale は nullptr 可（ない場合は非表示）
    /// imguiCallback を渡すと ImGui 表示をカスタマイズできる
    void AddTarget(const std::string &name,
                   Vector3 *translate,
                   Vector3 *rotate = nullptr,
                   Vector3 *scale = nullptr,
                   bool selectable = true,
                   std::function<void()> imguiCallback = nullptr);

    /// Sprite を登録する（XY 移動のみ、スクリーン空間ピクセル座標）
    void AddTarget(const std::string &name, Sprite *pSprite, bool selectable = true);

    void DrawImGui();
    // sceneHovered: シーンウィンドウが他のImGuiウィンドウに覆われずホバーされているか。
    //               false のときはクリックによるオブジェクト選択を行わない（誤操作防止）。
    void Update(const ImVec2 &scenePosition, const ImVec2 &sceneSize, bool sceneHovered = true);

    /// 現在選択されている最初のオブジェクトを返す（BaseObject のみ対応）
    BaseObject *GetSelectedTarget();
    /// 選択中の全 BaseObject を返す（非 BaseObject エントリは除外）
    std::vector<BaseObject *> GetSelectedTargets();

    // 全操作対象を消す DeleteTarget() は廃止した。
    // 3Dオブジェクト側の一括削除から呼ばれていたため、スプライト・ライト・パーティクルの
    // 登録まで巻き添えで消える事故のもとになっていた。
    // 自分が登録したものだけを RemoveTarget / RemoveTargetIfOwnedBy で外すこと。

    void CopySelectedObjects();
    void PasteObjects();
    void DeleteSelectedObjects();

    /// <summary>
    /// 選択中の BaseObject をその場で複製し、複製後のオブジェクトを選択状態にする
    /// コピーバッファを経由しないので Ctrl+D の連打で並べていける
    /// </summary>
    void DuplicateSelectedObjects();

    /// ===================================================
    /// 整列・配置支援
    /// ===================================================

    /// <summary>
    /// 整列の基準。選択中の対象のどの位置に揃えるかを表す。
    /// </summary>
    enum class AlignMode
    {
        Min,    // 指定軸の最小側へ揃える
        Center, // 指定軸の中央へ揃える
        Max,    // 指定軸の最大側へ揃える
    };

    /// <summary>
    /// 選択中の対象を指定軸で揃える
    /// </summary>
    /// <param name="axis">対象の軸（0=X, 1=Y, 2=Z）</param>
    /// <param name="mode">揃える基準</param>
    void AlignSelected(int axis, AlignMode mode);

    /// <summary>
    /// 選択中の対象を指定軸に沿って等間隔に並べる
    /// 両端はそのままに、間のものだけを均等な位置へ動かす
    /// </summary>
    /// <param name="axis">対象の軸（0=X, 1=Y, 2=Z）</param>
    void DistributeSelected(int axis);

    /// <summary>
    /// 選択中の対象を真下の他オブジェクトの上面へ着地させる
    /// 地形や床の上に物を置くときに、Y座標を手で合わせなくて済むようにする
    /// </summary>
    void SnapSelectedToGround();

    /// <summary>
    /// F キーによる視点フォーカス要求を取り出す（要求が無ければ false）
    /// DebugCamera が毎フレーム呼び、要求があればその位置へ寄る
    /// </summary>
    /// <param name="outTarget">注視点（ワールド座標）</param>
    /// <param name="outRadius">対象のおおよその半径（寄る距離の算出に使う）</param>
    /// <returns>bool: 要求があったか</returns>
    bool ConsumeFocusRequest(Vector3 &outTarget, float &outRadius);

    /// <summary>
    /// 新規オブジェクトを置く既定位置（カメラ前方の少し先）を返す
    /// 原点固定だと生成のたびに探しに行く手間がかかるため、視界の中に出す
    /// スナップが有効なときは刻み幅に丸める
    /// </summary>
    /// <param name="distance">カメラからの距離</param>
    /// <returns>Vector3: 配置位置（カメラ未設定なら原点）</returns>
    Vector3 GetSpawnPosition(float distance = 12.0f) const;

    /// <summary>
    /// マウスカーソルの指す先の配置位置を返す（アセットのドロップ配置用）
    /// 地面（Y=0平面）と交わればその交点、交わらなければカメラ前方の既定位置
    /// スナップが有効なときは刻み幅に丸める
    /// </summary>
    /// <param name="fallbackDistance">地面と交わらなかった場合のカメラからの距離</param>
    /// <returns>Vector3: 配置位置</returns>
    Vector3 GetSpawnPositionUnderCursor(float fallbackDistance = 12.0f) const;

    void DrawSelectedObjectHighlight();
    void DrawSelectionMarker(const Vector3 &worldPosition);
    void UpdateFilteredNames();

    // ギズモの選択状態をセット
    // selectable が false になった場合は、現在の選択状態からも除外する
    void SetSelectable(const std::string &name, bool selectable)
    {
        auto it = transformMap_.find(name);
        if (it != transformMap_.end())
        {
            it->second.selectable = selectable;
            if (!selectable)
            {
                selectedNames_.erase(name);
            }
        }
    }

    // スクリーン空間フラグと2Dヒット半径を設定する（Sprite登録後に呼ぶ）
    void SetScreenSpace(const std::string &name, bool isScreenSpace, float hitRadius = 50.0f)
    {
        auto it = transformMap_.find(name);
        if (it != transformMap_.end())
        {
            it->second.isScreenSpace = isScreenSpace;
            it->second.screenHitRadius = hitRadius;
        }
    }

    // スクリーン空間の当たり判定関数を設定する（設定時は半径判定より優先。AddTarget後に呼ぶ）
    void SetScreenHitTest(const std::string &name, std::function<bool(const Vector2 &)> hitTest)
    {
        auto it = transformMap_.find(name);
        if (it != transformMap_.end())
        {
            it->second.screenHitTest = std::move(hitTest);
        }
    }

    // 操作対象の大分類を設定する（フィルタUIの分類に反映。AddTarget後に呼ぶ）
    void SetCategory(const std::string &name, GizmoCategory category)
    {
        auto it = transformMap_.find(name);
        if (it != transformMap_.end())
        {
            it->second.category = category;
        }
    }

    // 指定分類が操作対象フィルタで有効か
    bool IsCategoryEnabled(GizmoCategory category) const
    {
        return categoryEnabled_[static_cast<int>(category)];
    }

    /// <summary>
    /// 指定した名前だけを選択状態にする（他ウィンドウの一覧とギズモ選択を同期させる用途）
    /// </summary>
    /// <param name="name">選択する登録名。未登録なら選択を解除する</param>
    void SelectOnly(const std::string &name)
    {
        selectedNames_.clear();
        if (transformMap_.find(name) != transformMap_.end())
        {
            selectedNames_.insert(name);
        }
    }

    /// <summary>
    /// 指定した名前が選択されているか
    /// </summary>
    bool IsSelected(const std::string &name) const
    {
        return selectedNames_.find(name) != selectedNames_.end();
    }

    /// <summary>
    /// 指定した名前が登録済みか
    /// </summary>
    bool HasTarget(const std::string &name) const
    {
        return transformMap_.find(name) != transformMap_.end();
    }

    // ギズモの選択状態を取得
    bool GetSelectable(const std::string &name)
    {
        if (transformMap_.find(name) != transformMap_.end())
        {
            return transformMap_[name].selectable;
        }
        return false;
    }

    // オブジェクト削除時にギズモからも消すためのメソッド
    void RemoveTarget(const std::string &name)
    {
        transformMap_.erase(name);
        selectedNames_.erase(name);
    }

    /// <summary>
    /// 指定名の登録が pOwner のものである場合にのみ登録解除する。
    /// 同名で登録し直された（＝別の実体に横取りされた）場合に、他人の登録を消さないための版。
    /// 破棄されるオブジェクトのデストラクタから呼ぶことを想定している。
    /// </summary>
    /// <param name="name">登録名</param>
    /// <param name="pOwner">AddTarget に渡したポインタ（BaseObject* / WorldTransform* / Vector3* など）</param>
    void RemoveTargetIfOwnedBy(const std::string &name, const void *pOwner)
    {
        auto it = transformMap_.find(name);
        if (it == transformMap_.end() || !pOwner)
        {
            return;
        }
        const GizmoTarget &target = it->second;
        const bool isOwner =
            (target.baseObject == pOwner) ||
            (target.worldTransform == pOwner) ||
            (target.translate == pOwner) ||
            (target.position2D == pOwner);
        if (isOwner)
        {
            RemoveTarget(name);
        }
    }

  private:
    void ShowSelectedObjectImGui();
    // 操作対象フィルタで無効化された分類の選択を解除する
    void PruneSelectionByFilter();
    void HandleMouseSelection(const ImVec2 &scenePosition, const ImVec2 &sceneSize, bool sceneHovered);
    void CycleOverlapSelection();
    // シーンウィンドウ上でのみ効くギズモ操作のホットキーを処理する
    void HandleHotkeys(bool sceneHovered);
    // 矩形（ラバーバンド）選択の開始・枠の描画・確定を行う
    void HandleBoxSelection(const ImVec2 &scenePosition, const ImVec2 &sceneSize, bool sceneHovered);
    // 指定スクリーン矩形に中心が入っている対象を選択する
    void SelectInsideScreenRect(const ImVec2 &rectMin, const ImVec2 &rectMax,
                                const ImVec2 &scenePosition, const ImVec2 &sceneSize, bool additive);
    // 整列・等間隔配置で共通に使う「動かせる3D対象」を集める
    std::vector<GizmoTarget *> CollectMovableSelection();
    // ワールド座標を指定して対象を移動する（親を持つ場合も正しくローカルへ戻す）
    void SetTargetWorldPosition(GizmoTarget &target, const Vector3 &worldPosition);
    // F キーのフォーカス要求を立てる（選択中ターゲットの重心と大きさを見る）
    void RequestFocusOnSelection();
    void DecomposeMatrix(const Matrix4x4 &matrix, Vector3 &position, Quaternion &rotation, Vector3 &scale);
    bool WorldToScreen(const Vector3 &worldPos, Vector3 &screenPos, const ImVec2 &scenePosition, const ImVec2 &sceneSize);

    std::string GenerateUniqueName(const std::string &baseName);

    /// <summary>
    /// BaseObject を複製して BaseObjectManager へ追加する。
    /// 中身は BaseObjectManager::CloneObject が写す（元と同じ派生クラスで作り直され、
    /// マテリアルごとのテクスチャ・色やメタボールの要素まで引き継がれる）。
    /// </summary>
    /// <param name="pSource">複製元</param>
    /// <param name="offset">複製先に加える位置のずらし量</param>
    /// <returns>std::string: 追加されたオブジェクト名（失敗時は空文字）</returns>
    std::string CloneObject(BaseObject *pSource, const Vector3 &offset);

    void DrawDebugRaycast();
    void DrawAABBWireframe(const Matrix4x4 &worldMatrix, const AABB &localBounds, const Vector4 &color);
    void DrawSphereWireframe(const Matrix4x4 &worldMatrix, const AABB &localBounds, const Vector4 &color);
    // 行列を直接受け取るレイヒット描画（GizmoTarget が BaseObject 以外の場合にも対応）
    void TestAndDrawRayHit(const Ray &ray, const GizmoTarget &target);
    // sceneSize を追加（スクリーン空間ギズモの描画に必要）
    void DisplayGizmo(const ImVec2 &scenePosition, const ImVec2 &sceneSize);

    RayHitInfo hitInfo_;
};

} // namespace Hagine
#endif // USE_IMGUI
