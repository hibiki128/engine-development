#pragma once
#include "Camera/ViewProjection/ViewProjection.h"
#include "Collider/ColliderBase.h"
#include "Collider/type/AABBCollider.h"
#include "Collider/type/CylinderCollider.h"
#include "Collider/type/MeshCollider.h"
#include "Collider/type/OBBCollider.h"
#include "Collider/type/SphereCollider.h"
#include "Data/DataHandler.h"
#include "Easing.h"
#include "Object/Object3d.h"
#include "Transform/ObjColor.h"
#include "Transform/WorldTransform.h"
#include "nlohmann/json.hpp"
#include <Graphics/PipeLine/PipeLineManager.h>
#include <string>

namespace Hagine {
class SkyBox;
class BaseObject {
  public:
    /// ===================================================
    /// public variaus
    /// ===================================================

    std::unique_ptr<DataHandler> ObjectDatas_{};
    std::unique_ptr<DataHandler> AnimaDatas_{};
    virtual ~BaseObject();

  protected:
    /// ===================================================
    /// protected variaus
    /// ===================================================

    // モデル配列データ
    std::unique_ptr<Object3d> obj3d_{};
    // ベースのワールド変換データ
    std::unique_ptr<WorldTransform> transform_{};

    Quaternion q_{};
    // ライティング
    bool isLighting_ = true;
    // isLoop_ は後方互換のために残しているが、ループ制御は
    // AddAnimation(path, loop) で登録したアニメーションごとのフラグが優先される
    bool skeletonDraw_ = false;
    bool isModelDraw_ = true;
    bool isWireframe_ = false;
    bool reflect_ = false;
    bool isPrimitive_ = false;
    bool isRainbow_ = false;
    bool isScene_ = false;
    bool isAlive_ = true;

    std::string objectName_{};
    std::string modelPath_{};
    std::vector<std::string> texturePaths_{};
    std::string texturePath_{};
    std::string foldarPath_ = "SceneData/Title/ObjectData";

    BaseObject *parent_ = nullptr;
    std::list<BaseObject *> children_{};

    PrimitiveType type_ = PrimitiveType::kCount;

  private:
    using json = nlohmann::json;

  public:
    /// ===================================================
    /// public method
    /// ===================================================

    // 初期化、更新、描画
    virtual void Init(const std::string className);
    virtual void Update();
    virtual void Draw(const ViewProjection &viewProjection);
    void UpdateWorldTransformHierarchy();
    void UpdateHierarchy();

    virtual void CreateModel(const std::string modelname);
    virtual void CreatePrimitiveModel(const PrimitiveType &type);

    virtual void ImGui();

    SphereCollider *AddSphereCollider(const std::string &name = "");
    AABBCollider *AddAABBCollider(const std::string &name = "");
    OBBCollider *AddOBBCollider(const std::string &name = "");
    CylinderCollider *AddCylinderCollider(const std::string &name = "");
    // 自身のモデル形状から三角形メッシュコライダーを生成する（静的な複雑形状向け）
    MeshCollider *AddMeshCollider(const std::string &name = "");

    // 中心座標取得
    WorldTransform *GetWorldTransform() { return transform_.get(); }
    ModelAnimation *GetModelAnimation() { return obj3d_->GetCurrentModelAnimation(); }

    /// =================================================
    /// 親子付け
    /// =================================================

    void SetParent(BaseObject *parent);

    void AddChild(BaseObject *child);

    void DetachParent();

    void DetachChild(BaseObject *child);

    BaseObject *GetParent();

    std::list<BaseObject *> *GetChildren();

    BaseObject *GetChildByName(const std::string &name);

    /// ===================================================
    /// セーブロード
    /// ===================================================

    void SceneSaveToJson();
    void SaveToJson();
    void LoadFromJson();
    void LoadFromJson(std::string folderPath, std::string jsonName);
    void SaveColliders();
    void LoadColliders();
    void AnimaSaveToJson();
    void AnimaLoadFromJson();
    void DebugCollider();
    void SaveParentChildRelationship();
    void LoadParentChildRelationship();
    void SetFolderPath(const std::string &folderPath) { foldarPath_ = folderPath; }

    /// ===================================================
    /// getter
    /// ===================================================
    const WorldTransform &GetTransform() { return *transform_; }
    std::string &GetName() { return objectName_; }
    std::string &GetModelPath() { return modelPath_; }
    bool &GetIsModelDraw() { return isModelDraw_; }
    std::string &GetTexturePath(int index = 0) { return texturePaths_[index]; }
    std::string GetParentName() const;
    std::vector<std::string> GetChildrenNames() const;
    Object3d *GetObject3d() { return obj3d_.get(); }
    PrimitiveType GetPrimitiveType() { return type_; }
    Vector3 &GetLocalPosition() { return transform_->translation_; }
    Quaternion &GetLocalRotation() { return transform_->quateRotation_; }
    Vector3 &GetLocalScale() { return transform_->scale_; }
    Vector3 GetWorldPosition();
    Quaternion GetWorldRotation();
    Vector3 GetWorldScale();
    Matrix4x4 GetWorldMatrix() { return transform_->matWorld_; }
    bool AnimaIsFinish() { return obj3d_->IsFinish(); }
    bool &GetLighting() { return isLighting_; }
    bool GetShouldSave() const { return shouldSave_; }
    bool IsPrimitive() const { return isPrimitive_; }
    const Vector4 GetColor(int index = 0) { return obj3d_->GetColor(index); }
    bool IsGizmoSelectable() const { return isGizmoSelectable_; }
    bool GetIsAlive() const { return isAlive_; }
    Material *GetMaterial(uint32_t index = 0) {
        obj3d_->GetMaterial(index);
    }
    std::vector<std::unique_ptr<ColliderBase>> &GetColliders() { return colliders_; }

    /// ===================================================
    /// setter
    /// ===================================================
    void SetTexture(const std::string &filePath, uint32_t index = 0) {
        if (filePath.empty()) {
            return; // ファイルパスが空なら何もしない
        }
        obj3d_->SetTexture(filePath, index);
        texturePaths_[index] = filePath;
    }
    void SetModel(std::unique_ptr<Object3d> obj) {
        obj3d_ = std::move(obj);
    }
    void SetModel(const std::string &filePath) { obj3d_->SetModel(filePath); }
    void SetAnima(const std::string &filePath) { obj3d_->SetAnimation(filePath); }
    void SetBlendMode(BlendMode blendMode) { obj3d_->SetBlendMode(blendMode); }
    void SetReflect(bool reflect) { reflect_ = reflect; }
    void SetColor(const Vector4 &color, int index = 0) { obj3d_->SetColor(color, index); }
    void SetAlpha(const float &alpha, int index = 0) {
        Vector4 color;
        color.x = obj3d_->GetColor().x;
        color.y = obj3d_->GetColor().y;
        color.z = obj3d_->GetColor().z;
        color.w = alpha;
        obj3d_->SetColor(color, index);
    }
    void SetShouldSave(bool shouldSave) { shouldSave_ = shouldSave; }
    void SetPrimitive(bool isPrimitive) { isPrimitive_ = isPrimitive; }
    void SetIsScene(bool isScene) { isScene_ = isScene; }
    void SetGizmoSelectable(bool selectable) { isGizmoSelectable_ = selectable; }
    void SetIsAlive(bool flag) { isAlive_ = flag; }
    void SetIsModelDraw(bool isModelDraw) { isModelDraw_ = isModelDraw; }
    void SetOffset(const Vector3 &offset) { offSet_ = offset; }

    /// <summary>
    /// 描画専用の回転オフセットを設定する
    /// ゲームプレイ用の向き（transform_ の回転）は変えず、描画時のみモデルを傾ける用途。
    /// ローカル空間の回転として現在の向きへ合成される（例：進行方向への体の傾け表現）
    /// </summary>
    /// <param name="offset">ローカル空間の回転オフセット</param>
    /// <param name="pivot">回転中心（ローカル軸・ワールド単位、原点からのオフセット）。
    /// モデルの中心が原点にない場合に、回転で位置がずれないよう補正するために使う</param>
    void SetRenderRotationOffset(const Quaternion &offset, const Vector3 &pivot = {0.0f, 0.0f, 0.0f}) {
        renderRotationOffset_ = offset;
        renderRotationPivot_ = pivot;
        applyRenderRotationOffset_ = true;
    }

    /// <summary>
    /// 描画専用の回転オフセットを解除する
    /// </summary>
    void ClearRenderRotationOffset() {
        renderRotationOffset_ = Quaternion::IdentityQuaternion();
        renderRotationPivot_ = {0.0f, 0.0f, 0.0f};
        applyRenderRotationOffset_ = false;
    }

    void SetAnimationSpeed(float speed) { obj3d_->SetAnimationSpeed(speed); }
    void SetAnimationBlendDuration(float duration) { obj3d_->SetAnimationBlendDuration(duration); }
  
    /// <summary>
    /// アニメーションを追加登録する
    /// </summary>
    /// <param name="filePath">アニメーションファイルパス</param>
    /// <param name="loop">ループ再生するか（デフォルト true）。攻撃系は false を渡すこと</param>
    void AddAnimation(const std::string &filePath, bool loop = true) { obj3d_->AddAnimation(filePath, loop); }

    /// ===================================================
    /// 物理（リジッドボディ）
    /// ===================================================

    /// <summary>
    /// リジッドボディの物理パラメータ
    /// </summary>
    struct RigidBodyParams {
        bool enabled = false;                  // リジッドボディとして物理挙動させるか
        bool useGravity = true;                // 重力を適用するか
        float mass = 1.0f;                     // 質量（外力 F=ma に使用）
        Vector3 gravity = {0.0f, -9.8f, 0.0f}; // 重力加速度
        float linearDamping = 0.05f;           // 速度の減衰（空気抵抗）
        float restitution = 0.0f;              // 反発係数（0=跳ねない, 1=完全反発）
        float friction = 0.3f;                 // 接触摩擦（接線速度の減衰, 坂の滑り方に影響）
        Vector3 velocity = {0.0f, 0.0f, 0.0f}; // 速度（ランタイム状態。保存しない）
    };

    /// <summary>重力・速度を積分して位置を更新する（Update から毎フレーム呼ばれる）</summary>
    /// <param name="deltaTime">前フレームからの経過秒</param>
    void UpdatePhysics(float deltaTime);

    /// <summary>外力を加える（次の UpdatePhysics で a=F/m として速度に反映）</summary>
    void AddForce(const Vector3 &force) { accumulatedForce_ += force; }

    /// <summary>リジッドボディ（物理挙動）の有効/無効を設定</summary>
    void SetRigidBody(bool enable) { rigidBody_.enabled = enable; }
    bool IsRigidBody() const { return rigidBody_.enabled; }
    RigidBodyParams &GetRigidBody() { return rigidBody_; }

    /// <summary>
    /// 衝突時の押し出し（めり込み解消）の有効/無効を設定する。
    /// 有効化すると、このオブジェクトの各コライダーの OnCollision に押し出し処理を仕込む。
    /// ※ 独自の衝突コールバックを持つオブジェクト（Player 等）では使わないこと（上書きされる）。
    /// </summary>
    void SetResolveCollision(bool enable);
    bool IsResolveCollision() const { return resolveCollision_; }

  private:
    void DebugObject();
    void ShowFileSelector();
    // ブレンドモードの選択UI
    void ShowBlendModeCombo(BlendMode &currentMode);

    // --- 物理 ---
    /// <summary>衝突相手に対して押し出し（＋リジッドボディなら速度補正）を適用する</summary>
    void ResolveCollisionWith(ColliderBase *self, ColliderBase *other);
    /// <summary>全コライダーの OnCollision に押し出しコールバックを仕込む</summary>
    void InstallResolveCallbacks();
    /// <summary>全コライダーの OnCollision をクリアする</summary>
    void ClearResolveCallbacks();
    /// <summary>物理パラメータを ObjectDatas_ へ保存</summary>
    void SavePhysics();
    /// <summary>物理パラメータを ObjectDatas_ から読み込み</summary>
    void LoadPhysics();

    bool shouldSave_ = true;
    bool isGizmoSelectable_ = true;
    BlendMode blendMode_ = BlendMode::kNormal;
    std::string parentName_{};

    std::vector<std::unique_ptr<ColliderBase>> colliders_;

    // --- 物理（リジッドボディ）状態 ---
    RigidBodyParams rigidBody_;                       // 物理パラメータ
    Vector3 accumulatedForce_ = {0.0f, 0.0f, 0.0f};   // 1フレーム分の外力
    bool resolveCollision_ = false;                   // 衝突時に押し出すか

    // スケールにイージングを適用してモーションを確認するためのデバッグ用状態
    struct ScaleEaseState {
        // EasingType enumの範囲（0〜30）＋Amplitude拡張（31〜33）をまとめて管理するインデックス
        // 31 = InElasticAmplitude, 32 = OutElasticAmplitude, 33 = InOutElasticAmplitude
        int selectedMode = 32; // デフォルト: OutElasticAmplitude
        float totalTime = 1.0f;
        float currentTime = 0.0f;
        bool isActive = false;

        // 通常イージング（start → end 補間）用スケール値
        Vector3 startScale = {1.0f, 1.0f, 1.0f};
        Vector3 endScale = {2.0f, 2.0f, 2.0f};

        // ElasticAmplitude系（ぷよぷよ）用パラメータ
        Vector3 amplitude = {0.5f, 0.5f, 0.5f};
        float period = 0.3f;

        // スタートボタン押下時のスケールを記録（Amplitude系の基準点）
        Vector3 baseScale = {1.0f, 1.0f, 1.0f};
    };

    Vector3 offSet_ = {0.0f, 0.0f, 0.0f};

    // 描画専用の回転オフセット（ゲームプレイ用の向きには影響しない。描画時のみ適用）
    Quaternion renderRotationOffset_ = Quaternion::IdentityQuaternion(); // ローカル空間の追加回転
    Vector3 renderRotationPivot_ = {0.0f, 0.0f, 0.0f};                   // 回転中心（原点からのオフセット）
    bool applyRenderRotationOffset_ = false;                            // 描画回転オフセットを適用するか

    ScaleEaseState scaleEase_{};

    // スケールイージングテストのImGuiウィジェットを描画し再生状態を更新する
    void DrawScaleEaseImGui();
};
} // namespace Hagine
