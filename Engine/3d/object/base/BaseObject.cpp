#define NOMINMAX
#include "BaseObject.h"
#include "BaseObjectManager.h"
#include "browser/ShowFolder.h"
#include "collider/CollisionManager.h"
#include "debug/profiler/CpuProfiler.h"
#include "frame/Frame.h"
#include "model/material/Material.h"
#include "object/Object3dInstancing.h"
#include "render/CameraFade.h"
#include "render/RenderCulling.h"
#include "scene/SceneManager.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#ifdef USE_IMGUI
#include "utility/debug/imgui/AssetDragDrop.h"
#include <asset/AssetPath.h>
#include <edit/play/PlayModeManager.h>
#include <graphics/texture/TextureManager.h>
#include <imgui_internal.h>
#include <implot.h>
#endif // DEBUG

// BaseObject の本体（生成・更新・描画・親子関係・トランスフォーム）。保存/UI/コライダーは BaseObjectXxx.cpp に分けてある。
namespace Hagine {

BaseObject::~BaseObject() {
    // マネージャからの登録解除（未登録でも可）
    BaseObjectManager::GetInstance()->Unregister(this);
    colliders_.clear();
}

void BaseObject::Init(const std::string objectName) {
    transform_ = std::make_unique<WorldTransform>();
    obj3d_ = std::make_unique<Object3d>();
    obj3d_->Initialize();
    // 同名が既に居れば "_0", "_1" … を付けた名前になる（敵を何体も作るときも同じ名前で Init してよい）
    // 保存ファイルは連番を付ける前の名前で引くので、何体作っても同じ調整値を共有する
    objectName_ = BaseObjectManager::GetInstance()->AcquireObjectName(objectName, this);
    dataName_ = objectName;
    /// ワールドトランスフォームの初期化
    transform_->Initialize();
    // ライティングのセット
    isLighting_ = true;
    isAlive_ = true;
}

void BaseObject::Update() {
    if (obj3d_->GetHaveAnimation()) {
        // ステートマシンは「どのクリップを流すか」を決めるので、アニメーションを進める前に動かす
        if (animStateMachine_) {
#ifdef USE_IMGUI
            const bool running = PlayModeManager::GetInstance()->ShouldUpdateGame();
#else
            const bool running = true;
#endif // USE_IMGUI
            animStateMachine_->Update(running ? Frame::DeltaTime() : 0.0f);
        }
        // ループフラグはアニメーションごとに Object3d が内部管理する
        HAGINE_CPU_PROFILE("Update/Objects/Anim");
        obj3d_->AnimationUpdate();
    }
    SetBlendMode(blendMode_);

    // 物理の積分（衝突解消は CollisionManager::Update のコールバック。停止条件は衝突解消と同一）
#ifdef USE_IMGUI
    const bool updateGameWorld = PlayModeManager::GetInstance()->ShouldUpdateGame();
#else
    const bool updateGameWorld = true;
#endif // USE_IMGUI
    if (updateGameWorld)
    {
        HAGINE_CPU_PROFILE("Update/Objects/Phys");
        UpdatePhysics(Frame::DeltaTime());
    }
}

bool BaseObject::ApplyRenderTransform(Vector3 &outOriginalPosition, Quaternion &outOriginalRotation) {
    outOriginalPosition = transform_->translation_;
    outOriginalRotation = transform_->quaternionRotation_;

    const bool hasOffset = (offSet_.x != 0.0f || offSet_.y != 0.0f || offSet_.z != 0.0f);
    if (!hasOffset && !applyRenderRotationOffset_) {
        return false;
    }

    transform_->translation_ = outOriginalPosition + offSet_;

    if (applyRenderRotationOffset_) {
        // ローカル空間の回転として現在の向きへ合成
        transform_->quaternionRotation_ = outOriginalRotation * renderRotationOffset_;

        // モデル中心が原点にない場合、回転で位置がずれる。
        // ピボット（回転中心）が固定されるよう平行移動で補正する
        if (renderRotationPivot_.x != 0.0f || renderRotationPivot_.y != 0.0f || renderRotationPivot_.z != 0.0f) {
            Vector3 rotatedPivot = renderRotationOffset_.Rotate(renderRotationPivot_);
            Vector3 pivotShift = outOriginalRotation.Rotate(renderRotationPivot_ - rotatedPivot);
            transform_->translation_ += pivotShift;
        }
    }

    transform_->UpdateMatrix();
    return true;
}

void BaseObject::RestoreRenderTransform(const Vector3 &originalPosition, const Quaternion &originalRotation) {
    transform_->translation_ = originalPosition;
    transform_->quaternionRotation_ = originalRotation;
    transform_->UpdateMatrix();
}

Matrix4x4 BaseObject::GetRenderWorldMatrix() {
    Vector3 originalPosition;
    Quaternion originalRotation;
    if (!ApplyRenderTransform(originalPosition, originalRotation)) {
        return transform_->matWorld_;
    }
    const Matrix4x4 renderMatrix = transform_->matWorld_;
    RestoreRenderTransform(originalPosition, originalRotation);
    return renderMatrix;
}

void BaseObject::Draw(const ViewProjection &viewProjection) {
    // 描画専用の位置オフセット・回転オフセットを一時的に適用する。
    // これらは描画時のみ反映し、ゲームプレイで参照する transform_ の値は描画後に元へ戻す
    Vector3 originalPosition;
    Quaternion originalRotation;
    const bool applyRenderTransform = ApplyRenderTransform(originalPosition, originalRotation);

    // 画面に入らないものはここで捨てる。モデルを持たないものは広がりが分からないので判定しない
    const bool visible = !(obj3d_ && obj3d_->GetModel()) ||
                         RenderCulling::IsVisible(viewProjection, GetLocalBounds(), transform_->matWorld_);

    // カメラに近い物は透けさせる（影のパスでは常に 1）。完全に消えるなら描かない
    // （カメラビュー窓の描画中はマテリアルの値を書き換えない。メインの描画と共有しているため）
    float cameraFade = 1.0f;
    if (!RenderView::IsExtra()) {
        if (visible && obj3d_ && obj3d_->GetModel() && cameraFadeEnabled_) {
            cameraFade = CameraFade::Compute(viewProjection, GetLocalBounds(), transform_->matWorld_);
        }
        if (obj3d_) {
            obj3d_->SetCameraFade(cameraFade);
        }
    }

    if (visible && cameraFade > 0.0f) {
        // スケルトンの描画が必要な場合
        if (skeletonDraw_) {
            obj3d_->DrawSkeleton(*transform_, viewProjection);
        }
        if (!isWireframe_) {
            // 同じモデルを参照するオブジェクトをまとめて描くため、まずバッチャへ積んでみる。
            // 積めた場合は BaseObjectManager::Draw の Flush でまとめて描かれる。
            // （収集中でない・スキニング・半透明などで積めなければ従来どおり1体ずつ描く）
            const bool batched = isModelDraw_ &&
                                 Object3dInstancing::GetInstance()->TrySubmit(
                                     obj3d_.get(), *transform_, viewProjection, reflect_, isLighting_);
            if (!batched) {
                // オブジェクトの描画
                obj3d_->Draw(*transform_, viewProjection, reflect_, isLighting_, isModelDraw_);
            }
        } else {
            obj3d_->DrawWireframe(*transform_, viewProjection, isRainbow_);
        }
    }

    // 描画専用の変更を元へ戻す
    if (applyRenderTransform) {
        RestoreRenderTransform(originalPosition, originalRotation);
    }
}

void BaseObject::CopyPropertiesFrom(const BaseObject &source) {
    // シーンファイルへ保存する項目（トランスフォーム・見た目・マテリアル・物理・IK・揺れ物など）を写す。
    // 派生クラス固有の項目は派生側の CopyPropertiesFrom で写すため、基底の Serialize / Deserialize に限る
    nlohmann::json state = source.BaseObject::Serialize();
    // コライダーは名前を振り直して別途写す
    state.erase("colliders");
    BaseObject::Deserialize(state);

    // シーンファイルに含まれない描画設定
    reflect_ = source.reflect_;
    offSet_ = source.offSet_;

    // コライダー（形状・タグ・衝突マスク）。名前は複製先の命名で振り直す
    for (const std::unique_ptr<ColliderBase> &sourceCollider : source.colliders_) {
        if (!sourceCollider) {
            continue;
        }
        if (ColliderBase *collider = AddColliderForEditor(sourceCollider->GetType())) {
            collider->RestoreState(sourceCollider->CaptureState());
        }
    }
}

void BaseObject::UpdateWorldTransformHierarchy() {
    // まず自分のトランスフォームを更新
    if (transform_) {
        transform_->UpdateMatrix();
    }
    // 子を再帰的に更新
    for (auto it = children_.begin(); it != children_.end();) {
        BaseObject *pChild = *it;
        pChild->UpdateWorldTransformHierarchy();
        if (pChild->pParent_ != this) {
            it = children_.erase(it);
        } else {
            ++it;
        }
    }
}

void BaseObject::UpdateHierarchy() {
    // 自分自身の処理
    Update();

    // 子リストをイテレート
    for (auto it = children_.begin(); it != children_.end();) {
        auto pChild = *it;
        // 再帰的に UpdateHierarchy
        pChild->UpdateHierarchy();

        // 子が「DetachParent()」した場合、pParent_ == nullptr になる
        if (pChild->GetParent() != this) {
            // リストから削除
            it = children_.erase(it);
        } else {
            ++it;
        }
    }
}

void BaseObject::SetParent(BaseObject *parent) {
    if (pParent_ == parent || parent == nullptr) {
        return; // 同じ親を持ってる場合何もしない
    }
    if (pParent_) {
        DetachParent(); // もし現在の親がいるなら一旦デタッチ
    }

    assert(parent != nullptr && "SetParent to nullptr is not allowed.");

    pParent_ = parent;
    // 親の子リストに追加
    pParent_->children_.push_back(this);

    if (transform_) {
        transform_->pParent_ = parent->GetWorldTransform();
    }
    parentName_ = pParent_->GetName();
}

void BaseObject::AddChild(BaseObject *pChild) {
    assert(pChild != nullptr && "AddChild is nullptr");
    pChild->SetParent(this);
}

void BaseObject::DetachParent() {
    if (pParent_) {
        pParent_->children_.remove(this);
        pParent_ = nullptr;
        if (transform_) {
            transform_->pParent_ = nullptr;
        }
    }
}

void BaseObject::DetachChild(BaseObject *pChild) {
    if (!pChild) {
        return;
    }
    if (pChild->pParent_ != this) {
        return;
    }
    pChild->pParent_ = nullptr;
    if (pChild->transform_) {
        pChild->transform_->pParent_ = nullptr;
    }
    children_.remove(pChild);
}

BaseObject *BaseObject::GetParent() {
    return pParent_;
}

std::list<BaseObject *> *BaseObject::GetChildren() {
    return &children_;
}

BaseObject *BaseObject::GetChildByName(const std::string &name) {
    for (auto &pChild : children_) {
        if (pChild->objectName_ == name) {
            return pChild;
        }
    }
    return nullptr;
}

void BaseObject::CreateModel(const std::string modelname) {
    modelPath_ = modelname;
    isPrimitive_ = false;

    obj3d_->CreateModel(modelname);

    // テクスチャパスを3Dモデル用にリサイズ
    texturePaths_.resize(obj3d_->GetMaterialCount());

    // デフォルトのテクスチャパスを設定
    auto allTexturePaths = obj3d_->GetAllTexturePath();
    for (int i = 0; i < texturePaths_.size() && i < allTexturePaths.size(); i++) {
        texturePaths_[i] = allTexturePaths[i];
    }

    // オブジェクト単体の保存があれば読み込む（modelPath_は上書きされない）。
    // シーンファイルから作る物はシーン側が後から中身を流し込むので読まない
    if (loadObjectDataFile_) {
        LoadFromJson("ObjectDatas", dataName_);
    }

    // JSONから読み込んだカラー設定を適用
    if (objectData_) {
        for (int i = 0; i < int(obj3d_->GetMaterialCount()); i++) {
            SetColor(objectData_->Load<Vector4>("color_" + std::to_string(i), GetColor(i)), i);
        }
    }

    // テクスチャを設定
    for (int i = 0; i < texturePaths_.size(); i++) {
        obj3d_->SetTexture(texturePaths_[i], i);
    }

    // マテリアル（ノーマルマップ関連）情報を適用（マテリアル生成後に行う）
    LoadMaterials();

    AnimaLoadFromJson();
}

void BaseObject::CreatePrimitiveModel(const PrimitiveType &type) {
    modelPath_ = ""; // プリミティブの場合は空文字列
    isPrimitive_ = true;
    type_ = type;

    // プリミティブ用にテクスチャパスを設定（1枚のみ）
    texturePaths_.resize(1);
    texturePaths_[0] = "debug/uvChecker.png"; // デフォルト値

    // オブジェクト単体の保存があれば読み込む（シーンファイルから作る物は読まない）
    if (loadObjectDataFile_) {
        LoadFromJson("ObjectDatas", dataName_);
    }

    // プリミティブモデルを作成
    obj3d_->CreatePrimitiveModel(type_, texturePaths_[0]);

    if (objectData_) {
        SetColor(objectData_->Load<Vector4>("color_" + std::to_string(0), {1.0f, 1.0f, 1.0f, 1.0f}), 0);
    }

    // マテリアル（ノーマルマップ関連）情報を適用（マテリアル生成後に行う）
    LoadMaterials();

    AnimaLoadFromJson();
}

void BaseObject::SaveParentChildRelationship() {
    if (!objectData_) {
        return;
    }

    // 親の名前を保存
    std::string parentName = pParent_ ? pParent_->GetName() : "";
    objectData_->Save<std::string>("parentName", parentName);

    // 子の名前リストを保存
    std::vector<std::string> childrenNames;
    for (const auto &pChild : children_) {
        if (pChild) {
            childrenNames.push_back(pChild->GetName());
        }
    }
    objectData_->Save<std::vector<std::string>>("childrenNames", childrenNames);

    // 親のSRTをどの成分まで継承するか（親子付けの挙動）も保存する
    if (transform_) {
        objectData_->Save<bool>("inheritTranslation", transform_->inheritTranslation_);
        objectData_->Save<bool>("inheritRotation", transform_->inheritRotation_);
        objectData_->Save<bool>("inheritScale", transform_->inheritScale_);
    }
}

void BaseObject::LoadParentChildRelationship() {
    if (!objectData_) {
        return;
    }

    // 親の名前を読み込み（実際の親付けはBaseObjectManagerで行う）
    std::string parentName = objectData_->Load<std::string>("parentName", "");

    // 子の名前リストを読み込み（実際の子付けはBaseObjectManagerで行う）
    std::vector<std::string> childrenNames = objectData_->Load<std::vector<std::string>>("childrenNames", std::vector<std::string>());
}

std::string BaseObject::GetParentName() const {
    return pParent_ ? parentName_ : "";
}

std::vector<std::string> BaseObject::GetChildrenNames() const {
    std::vector<std::string> names;
    for (const auto &pChild : children_) {
        if (pChild) {
            names.push_back(pChild->GetName());
        }
    }
    return names;
}

Vector3 BaseObject::GetWorldPosition() {
    return transform_->GetWorldPosition();
}

// ワールド行列からクォータニオンを取得
Quaternion BaseObject::GetWorldRotation() {
    return transform_->GetWorldRotation();
}

// ワールドスケールを取得（回転を考慮）
Vector3 BaseObject::GetWorldScale() {
    return transform_->GetWorldScale();
}

// モデルのローカル空間AABBを取得（マウス選択やフォーカスで実サイズを使うため）
AABB BaseObject::GetLocalBounds() const {
    if (obj3d_ && obj3d_->GetModel()) {
        return obj3d_->GetModel()->GetLocalBounds();
    }
    return AABB{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
}

std::optional<Vector3> BaseObject::GetJointWorldPosition(const std::string &jointName) {
    if (!obj3d_) {
        return std::nullopt;
    }
    ModelAnimation *modelAnimation = obj3d_->GetCurrentModelAnimation();
    if (!modelAnimation) {
        return std::nullopt;
    }
    Bone *bone = modelAnimation->GetBone();
    if (!bone) {
        return std::nullopt;
    }

    // 描画時と同じ条件（描画オフセット込み）のワールド行列を組み立てて参照する
    Matrix4x4 worldMatrix = MakeAffineMatrix(
        transform_->scale_, transform_->quaternionRotation_, transform_->translation_ + offSet_);
    return bone->GetJointWorldPosition(jointName, worldMatrix);
}

} // namespace Hagine
