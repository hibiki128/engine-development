#define NOMINMAX
#include "BaseObject.h"
#include "BaseObjectManager.h"
#include "browser/ShowFolder.h"
#include "collider/CollisionManager.h"
#include "debug/profiler/CpuProfiler.h"
#include "frame/Frame.h"
#include "model/material/Material.h"
#include "object/Object3dInstancing.h"
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
    // 非所有(RegisterExternal)でマネージャに登録されている場合、所有者(シーン等)が
    // 先に破棄されると BaseObjectManager::objects_ にダングリングポインタが残り、
    // アプリ終了時の RemoveAllObjects() でアクセス違反になる。破棄時に必ず自身を
    // 登録解除して防ぐ。未登録・所有オブジェクトでも erase は no-op なので安全
    BaseObjectManager::GetInstance()->UnregisterExternal(this);
    colliders_.clear();
}

void BaseObject::Init(const std::string objectName) {
    transform_ = std::make_unique<WorldTransform>();
    obj3d_ = std::make_unique<Object3d>();
    obj3d_->Initialize();
    objectName_ = objectName;
    /// ワールドトランスフォームの初期化
    transform_->Initialize();
    // ライティングのセット
    isLighting_ = true;
    isAlive_ = true;
}

void BaseObject::Update() {
    if (obj3d_->GetHaveAnimation()) {
        // ループフラグはアニメーションごとに Object3d が内部管理する
        HAGINE_CPU_PROFILE("Update/Objects/Anim");
        obj3d_->AnimationUpdate();
    }
    SetBlendMode(blendMode_);

    // リジッドボディの物理を更新（重力・速度積分）。
    // 衝突解消（押し出し）はこの後の CollisionManager::Update のコールバックで行う。
    //
    // 一時停止・停止中は積分しない。CollisionManager::Update は止まるのに重力だけ
    // 積分され続けると、押し戻す相手がいないままプレイヤーが床をすり抜けて落ちていく
    // （＝物理と衝突解消は必ず同じ条件で止める）。
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

void BaseObject::Draw(const ViewProjection &viewProjection) {
    // 描画専用の位置オフセット・回転オフセットを一時的に適用する。
    // これらは描画時のみ反映し、ゲームプレイで参照する transform_ の値は描画後に元へ戻す
    Vector3 originalPosition = transform_->translation_;
    Quaternion originalRotation = transform_->quaternionRotation_;

    bool hasOffset = (offSet_.x != 0.0f || offSet_.y != 0.0f || offSet_.z != 0.0f);
    bool applyRenderTransform = hasOffset || applyRenderRotationOffset_;

    if (applyRenderTransform) {
        transform_->translation_ = originalPosition + offSet_;

        if (applyRenderRotationOffset_) {
            // ローカル空間の回転として現在の向きへ合成
            transform_->quaternionRotation_ = originalRotation * renderRotationOffset_;

            // モデル中心が原点にない場合、回転で位置がずれる。
            // ピボット（回転中心）が固定されるよう平行移動で補正する
            if (renderRotationPivot_.x != 0.0f || renderRotationPivot_.y != 0.0f || renderRotationPivot_.z != 0.0f) {
                Vector3 rotatedPivot = renderRotationOffset_.Rotate(renderRotationPivot_);
                Vector3 pivotShift = originalRotation.Rotate(renderRotationPivot_ - rotatedPivot);
                transform_->translation_ += pivotShift;
            }
        }

        transform_->UpdateMatrix();
    }

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

    // 描画専用の変更を元へ戻す
    if (applyRenderTransform) {
        transform_->translation_ = originalPosition;
        transform_->quaternionRotation_ = originalRotation;
        transform_->UpdateMatrix();
    }
}

void BaseObject::CopyPropertiesFrom(const BaseObject &source) {
    // トランスフォーム
    transform_->translation_ = source.transform_->translation_;
    transform_->quaternionRotation_ = source.transform_->quaternionRotation_;
    transform_->scale_ = source.transform_->scale_;
    transform_->UpdateMatrix();

    // 見た目のフラグ
    isLighting_ = source.isLighting_;
    isModelDraw_ = source.isModelDraw_;
    isWireframe_ = source.isWireframe_;
    isRainbow_ = source.isRainbow_;
    reflect_ = source.reflect_;
    skeletonDraw_ = source.skeletonDraw_;
    offSet_ = source.offSet_;

    // マテリアル（テクスチャ・色）。複製先のマテリアル数に収まる範囲だけ写す
    const size_t materialCount =
        (std::min)(obj3d_->GetMaterialCount(), source.obj3d_->GetMaterialCount());
    for (size_t i = 0; i < materialCount; ++i)
    {
        const std::string texture = source.obj3d_->GetTextureFilePath(static_cast<uint32_t>(i));
        if (!texture.empty()) {
            SetTexture(texture, static_cast<uint32_t>(i));
        }
        obj3d_->SetColor(source.obj3d_->GetColor(static_cast<int>(i)), static_cast<int>(i));
    }

    // 物理
    rigidBody_ = source.rigidBody_;
    resolveCollision_ = source.resolveCollision_;
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

    // JSONファイルが存在する場合は読み込み（modelPath_は上書きされない）
    if (isScene_) {
        LoadFromJson();
    } else {
        LoadFromJson("ObjectDatas", objectName_);
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

    // JSONファイルが存在する場合は読み込み
    if (isScene_) {
        LoadFromJson();
    } else {
        LoadFromJson("ObjectDatas", objectName_);
    }

    // プリミティブモデルを作成
    obj3d_->CreatePrimitiveModel(type_, texturePaths_[0]);

    SetColor(objectData_->Load<Vector4>("color_" + std::to_string(0), {1.0f, 1.0f, 1.0f, 1.0f}), 0);

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
