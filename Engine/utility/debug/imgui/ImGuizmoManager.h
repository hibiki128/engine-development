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
class SceneProjector;

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
    // シーンにアイコンを出すか（パーティクルのエミッター用）。
    // フィールドのように専用のアイコンを別に描く物は false にして二重に出さない
    bool sceneIcon = true;

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
    // 補助表示（AABB・外接球・レイ）。選択の枠は DrawSelectionOverlay が出すので既定は切っておく
    bool isDrawDebug_ = false;

    // ---- 選択の表示（シーン窓に重ねる枠）----
    bool showSelectionOutline_ = true;                     // 選択中の物の枠
    bool showSelectionHiddenEdges_ = true;                 // 奥の辺を破線で出す
    bool showSelectionFill_ = true;                        // 枠の中をうっすら塗る
    bool showHoverOutline_ = true;                         // マウスを乗せている物の枠
    Vector4 selectionColor_ = {1.0f, 0.62f, 0.18f, 1.0f};  // 選択の色
    Vector4 hoverColor_ = {0.78f, 0.88f, 1.0f, 0.75f};     // マウスを乗せている物の色
    std::string hoveredName_;                              // マウスを乗せている物（このフレーム）

    // ---- オブジェクト選択の一覧 ----
    int browserCategoryFilter_ = -1;      // 種類の絞り込み（-1 = すべて / GizmoCategory）
    bool browserGroupNumbered_ = true;    // cube_1, cube_2 … を「cube」にまとめる
    bool browserScrollToSelection_ = false; // 選択が変わったら一覧をそこまで送る
    std::string browserLastSelection_;    // 前のフレームの選択（変化の検出用）
    std::string browserRangeAnchor_;      // Shift+クリックの範囲選択の起点
    std::string browserHoveredName_;      // 一覧でマウスを乗せている行（シーンでも枠を出す）

    // シーンのクリック対象フィルタ。GizmoCategory ごとに ON/OFF。
    // 効くのはシーン上のクリック・矩形選択だけで、階層やインスペクタからは OFF の種類も選べる。
    // スプライトは 3D を触っているときに画面上の UI を掴む事故が多いので既定で OFF。
    bool categoryEnabled_[kGizmoCategoryCount] = {true, false, true, true};

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
    // シーンのアイコンがこのフレームのクリックを使った（クリック選択を飛ばす）
    bool sceneClickConsumed_ = false;
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

    // ---- インスペクタ ----
    bool inspectorWindowOpen_ = false; // インスペクタ窓が開いているか（開いていればギズモ窓に詳細を出さない）
    std::string pinnedName_;           // ピン留め中の対象（空なら選択に追従）
    // インスペクタで見た物の履歴（ブラウザの戻る・進むと同じ。マウスのサイドボタンでも動く）
    std::vector<std::string> inspectorHistory_;
    int inspectorHistoryIndex_ = -1;
    static constexpr size_t kInspectorHistoryMax = 32;
    std::string inspectorFilter_; // 項目検索の文字（見出しの名前で絞る。空なら全部出す）

    /// <summary>トランスフォームのコピー（インスペクタの「…」メニューから貼れる）</summary>
    struct TransformClipboard
    {
        bool valid = false;
        Vector3 translation = {0.0f, 0.0f, 0.0f};
        Quaternion rotation = Quaternion::IdentityQuaternion();
        Vector3 scale = {1.0f, 1.0f, 1.0f};
    };
    TransformClipboard transformClipboard_;

    // プレハブ保存ダイアログ
    bool prefabDialogRequested_ = false;
    std::string prefabDialogRoot_;
    std::string prefabDialogName_;

    // 複数選択の一括拡縮（ドラッグ中の倍率。離したら 1 に戻す）
    float multiScaleFactor_ = 1.0f;

    // ---- 視点そろえ・スナップグリッド ----
    bool viewAlignRequested_ = false;
    float viewAlignPitch_ = 0.0f;
    float viewAlignYaw_ = 0.0f;
    bool showSnapGrid_ = true; // 移動スナップ中にグリッドを描く

    // ---- 配置ツール（ImGuizmoManagerPlacement.cpp）----
    struct PlacementSettings
    {
        int mode = 0; // 0=直線 1=格子 2=円 3=ばらまき
        int lineCount = 5;
        Vector3 lineStep = {2.0f, 0.0f, 0.0f};
        int gridColumns = 3;
        int gridRows = 3;
        float gridSpacingX = 2.0f;
        float gridSpacingZ = 2.0f;
        int ringCount = 8;
        float ringRadius = 6.0f;
        bool ringFaceOutward = true;
        int scatterCount = 12;
        float scatterRadius = 12.0f;
        float scatterMinDistance = 1.5f;
        uint32_t seed = 1;
        float randomYawDegrees = 0.0f;
        float scaleMin = 1.0f;
        float scaleMax = 1.0f;
        bool snapToGround = false;
        bool keepSource = true;
    };
    /// <summary>置く1か所</summary>
    struct PlacementPoint
    {
        Vector3 position;
        float yawDegrees = 0.0f;
        float scale = 1.0f;
    };
    PlacementSettings placement_;
    std::vector<PlacementPoint> BuildPlacementPoints(const Vector3 &origin) const;
    void ExecutePlacement(BaseObject *pSource);

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

    void UpdateFilteredNames();

    /// <summary>マウスを乗せている物の名前（無ければ空。シーン窓の描画中に決まる）</summary>
    const std::string &GetHoveredName() const { return hoveredName_; }

    /// <summary>ギズモの補助表示（AABB・外接球・レイ）のスイッチ（「デバッグ線」窓と結び付ける）</summary>
    bool *GetDrawDebugFlag() { return &isDrawDebug_; }

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

    // シーンにアイコンを出すかを設定する（AddTarget後に呼ぶ）
    void SetSceneIcon(const std::string &name, bool show)
    {
        auto it = transformMap_.find(name);
        if (it != transformMap_.end())
        {
            it->second.sceneIcon = show;
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

    // 指定分類がシーンのクリック対象になっているか
    bool IsCategoryEnabled(GizmoCategory category) const
    {
        return categoryEnabled_[static_cast<int>(category)];
    }

    /// <summary>
    /// シーンのクリック対象に含めるかを種類ごとに切り替える
    /// </summary>
    void SetCategoryEnabled(GizmoCategory category, bool enabled);

    /// <summary>
    /// 種類のクリック対象を反転する
    /// </summary>
    void ToggleCategory(GizmoCategory category);

    /// <summary>
    /// その種類だけをクリック対象にする。すでにその種類だけなら全部に戻す
    /// </summary>
    void SoloCategory(GizmoCategory category);

    /// <summary>
    /// 全種類をクリック対象にする
    /// </summary>
    void EnableAllCategories();

    /// <summary>
    /// クリック対象の ON/OFF をビットにまとめて返す（bit i = GizmoCategory i）。保存・ワークスペース用
    /// </summary>
    uint32_t GetCategoryMask() const;

    /// <summary>
    /// クリック対象の ON/OFF をビットでまとめて設定する
    /// </summary>
    void SetCategoryMask(uint32_t mask);

    /// <summary>
    /// 直前のクリックで重なっていた候補の数（Tab で順に選べる）。
    /// 選択がその候補から変わっていれば 0
    /// </summary>
    int GetOverlapCount() const;

    /// <summary>
    /// 重なり候補の中で今選んでいる番号（0 始まり）
    /// </summary>
    int GetOverlapIndex() const { return overlapCycleIndex_; }

    /// <summary>
    /// 重なり候補の次のものを選ぶ（Tab と同じ）
    /// </summary>
    void CycleOverlap() { CycleOverlapSelection(); }

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
    /// 指定した名前を選択に加える（Shift+クリックの範囲選択用）。未登録なら何もしない
    /// </summary>
    void AddToSelection(const std::string &name)
    {
        if (transformMap_.find(name) != transformMap_.end())
        {
            selectedNames_.insert(name);
        }
    }

    /// <summary>
    /// 指定した名前の選択を反転する（Ctrl+クリックでの追加・解除用）
    /// </summary>
    /// <param name="name">登録名。未登録なら何もしない</param>
    void ToggleSelect(const std::string &name)
    {
        if (transformMap_.find(name) == transformMap_.end())
        {
            return;
        }
        if (!selectedNames_.erase(name))
        {
            selectedNames_.insert(name);
        }
    }

    /// <summary>選択中の登録名の一覧</summary>
    const std::unordered_set<std::string> &GetSelectedNames() const { return selectedNames_; }

    /// <summary>
    /// 選択中の物へシーンカメラを寄せる（F キーと同じ）。一覧のダブルクリックなどから使う
    /// </summary>
    void FocusOnSelection() { RequestFocusOnSelection(); }

    /// ===================================================
    /// インスペクタ（ImGuizmoManagerInspector.cpp）
    /// ===================================================

    /// <summary>
    /// インスペクタ窓の中身を描く（窓の Begin/End は呼び出し元）。
    /// 1つ選択ならその詳細、複数選択なら共通項目のまとめて編集を出す
    /// </summary>
    void DrawInspector();

    /// <summary>
    /// インスペクタ窓が開いているかを伝える。開いている間はギズモ窓に詳細を重ねて出さない
    /// </summary>
    void SetInspectorWindowOpen(bool open) { inspectorWindowOpen_ = open; }

    /// <summary>
    /// プレハブ保存の名前入力ダイアログを開く（実際の描画は DrawEditorModals）
    /// </summary>
    /// <param name="rootName">根にするオブジェクト名</param>
    void OpenPrefabSaveDialog(const std::string &rootName);

    /// <summary>
    /// ギズモ側が持つダイアログ類を描く（どの窓が閉じていても出せるよう毎フレーム呼ぶ）
    /// </summary>
    void DrawEditorModals();

    /// <summary>
    /// プレハブを置いて選択状態にする（Undo 履歴にも積む）
    /// </summary>
    /// <param name="prefabName">プレハブ名</param>
    /// <param name="position">置くワールド座標</param>
    /// <returns>std::string: 置いた根の名前（失敗時は空）</returns>
    std::string PlacePrefab(const std::string &prefabName, const Vector3 &position);

    /// <summary>
    /// モデルからオブジェクトを作って選択状態にする（Undo 履歴にも積む）
    /// </summary>
    /// <param name="modelPath">models ルートからの相対パス</param>
    /// <param name="position">置くワールド座標</param>
    /// <returns>std::string: 作ったオブジェクト名（失敗時は空）</returns>
    std::string PlaceModel(const std::string &modelPath, const Vector3 &position);

    /// <summary>
    /// プレハブから置いた物の今の内容で、元のプレハブを上書きする
    /// </summary>
    void ApplyInstanceToPrefab(const std::string &instanceName);

    /// <summary>
    /// プレハブから置いた物を、プレハブの内容で置き直す（Undo 履歴にも積む）
    /// </summary>
    void RevertInstanceToPrefab(const std::string &instanceName);

    /// <summary>
    /// インスペクタ・階層・シーンの右クリックで共通に出す「プレハブ」メニューの中身（BeginMenu/Popup の内側で呼ぶ）
    /// </summary>
    void DrawPrefabLinkMenuItems(BaseObject *pObject);

    /// <summary>
    /// 選択中の物を、重心が指定の場所に来るようまとめて動かす（Undo 履歴にも積む）
    /// </summary>
    void MoveSelectionTo(const Vector3 &position);

    /// <summary>
    /// プリミティブを指定の場所に置いて選択状態にする（Undo 履歴にも積む）
    /// </summary>
    std::string PlacePrimitive(PrimitiveType type, const std::string &baseName, const Vector3 &position);

    /// <summary>ビュープロジェクション（シーンビューの軸表示用）</summary>
    const ViewProjection *GetViewProjection() const { return pViewProjection_; }

    /// <summary>シーンに名前を出す対象（3Dのもの）</summary>
    struct LabelTarget
    {
        std::string name;
        Vector3 position;
        GizmoCategory category = GizmoCategory::Object;
        bool selected = false;
    };

    /// <summary>
    /// シーンビューに名前ラベルを出す対象を集める（操作対象フィルタで外した種類は除く）
    /// </summary>
    /// <param name="selectedOnly">選択中のものだけにするか</param>
    std::vector<LabelTarget> CollectLabelTargets(bool selectedOnly) const;

    /// <summary>
    /// シーンにアイコンを出す対象（3D・選択可・アイコン有り）を種類で集める。クリック対象フィルタは見ない
    /// </summary>
    std::vector<LabelTarget> CollectIconTargets(GizmoCategory category) const;

    /// <summary>
    /// このフレームのシーンのクリックを使ったことにする（アイコンを押したときに、
    /// 奥の物まで一緒にクリック選択されないようにする）。ギズモの Update より先に呼ぶ
    /// </summary>
    void ConsumeSceneClick() { sceneClickConsumed_ = true; }

    /// <summary>
    /// 指定の場所へシーンカメラを寄せる要求を出す（ギズモ未登録のカメラのアイコン用）
    /// </summary>
    void RequestFocus(const Vector3 &target, float radius)
    {
        focusTarget_ = target;
        focusRadius_ = (std::max)(radius, 0.5f);
        focusRequested_ = true;
    }

    /// ===================================================
    /// シーンビューのオーバーレイ用
    /// ===================================================

    ImGuizmo::OPERATION GetOperation() const { return currentOperation_; }
    void SetOperation(ImGuizmo::OPERATION operation) { currentOperation_ = operation; }
    ImGuizmo::MODE GetMode() const { return currentMode_; }
    void SetMode(ImGuizmo::MODE mode) { currentMode_ = mode; }
    bool &UseSnap() { return useSnap_; }
    float &SnapTranslate() { return snapTranslate_; }
    float &SnapRotateDegree() { return snapRotateDegree_; }
    float &SnapScale() { return snapScale_; }
    bool &ShowSnapGrid() { return showSnapGrid_; }

    /// <summary>
    /// 視点を軸方向へそろえる要求を出す（シーンビュー右上の軸表示から）。
    /// 実際にカメラを回すのは DebugCamera 側で、ConsumeViewAlignRequest で受け取る
    /// </summary>
    /// <param name="pitch">X軸回りの角度（ラジアン）</param>
    /// <param name="yaw">Y軸回りの角度（ラジアン）</param>
    void RequestViewAlign(float pitch, float yaw)
    {
        viewAlignRequested_ = true;
        viewAlignPitch_ = pitch;
        viewAlignYaw_ = yaw;
    }

    /// <summary>視点そろえ要求を取り出す（無ければ false）。注視点は選択物の重心（無ければ前方）</summary>
    bool ConsumeViewAlignRequest(float &outPitch, float &outYaw, bool &outHasPivot, Vector3 &outPivot);

    /// <summary>
    /// 移動スナップ中に、選択物のまわりへ刻み幅のグリッドを描く（ギズモ操作中のみ）
    /// </summary>
    void DrawSnapGrid();

    /// <summary>
    /// 配置ツールの中身（選択中のオブジェクトを直線・格子・円・ばらまきで並べて複製する）。
    /// 窓の Begin/End は呼び出し元。窓が開いている間は置く場所をシーンに下描きする
    /// </summary>
    void DrawPlacementTool();

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
    // インスペクタの見出し（アイコン・名前・種類・親・表示/フォーカス/ピン留め/メニュー）
    void DrawInspectorHeader(GizmoTarget &target);
    // インスペクタの項目検索の欄（Ctrl+F で入る・Esc で消す）
    void DrawInspectorSearchBar();
    // 複数選択時の「まとめて編集」
    void DrawMultiSelectionInspector();
    // インスペクタの履歴（戻る・進む）
    void RecordInspectorHistory(const std::string &name);
    void NavigateInspectorHistory(int step);
    // トランスフォームのコピー・貼り付け
    void CopyTransformFrom(const GizmoTarget &target);
    void PasteTransformToSelection(bool translation, bool rotation, bool scale);
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

    // 選択中の物の枠・マウスを乗せている物の枠をシーン窓へ重ねて描く
    void DrawSelectionOverlay(const ImVec2 &scenePosition, const ImVec2 &sceneSize, bool sceneHovered);
    void DrawTargetOutline(ImDrawList *pDrawList, const SceneProjector &projector, const GizmoTarget &target, const Vector4 &color,
                           bool selected);
    // マウスの下にある物（クリック選択と同じ決め方）
    std::string PickTargetUnderMouse(const ImVec2 &scenePosition, const ImVec2 &sceneSize);
    // 「オブジェクト選択」の一覧（同じ名前の連番をまとめて出す）
    void DrawObjectBrowser();

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
