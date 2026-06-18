#define NOMINMAX
#include "BaseObject.h"
#include "BaseObjectManager.h"
#include "Collider/CollisionManager.h"
#include "Engine/Frame/Frame.h"
#include "Engine/Utility/Debug/ImGui/Debugui_improved.h"
#include "Engine/Utility/Debug/ImGui/ImGuiNotification.h"
#include "Scene/SceneManager.h"
#include "ShowFolder/ShowFolder.h"
#ifdef _DEBUG
#include <imgui_internal.h>
#include <implot.h>
#endif // DEBUG

namespace Hagine {

#ifdef _DEBUG
namespace {
/// <summary>テーマのアクセント色から3状態の色を作り、折りたたみヘッダーを描く</summary>
/// <param name="label">ヘッダーラベル（## でID付与可）</param>
/// <param name="accent">基準となるアクセント色</param>
/// <param name="defaultOpen">初期状態で開くか</param>
/// <returns>bool: 開いていれば true</returns>
bool ThemedHeader(const char *label, ImVec4 accent, bool defaultOpen = false) {
    ImVec4 base = accent;   base.w = 0.22f;
    ImVec4 hov = accent;    hov.w = 0.38f;
    ImVec4 act = accent;    act.w = 0.50f;
    ImGui::PushStyleColor(ImGuiCol_Header, base);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hov);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, act);
    bool open = ImGui::CollapsingHeader(label, defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    ImGui::PopStyleColor(3);
    return open;
}

/// <summary>チェックマーク色だけアクセント色にしたチェックボックス</summary>
bool AccentCheckbox(const char *label, bool *v, ImVec4 accent) {
    ImGui::PushStyleColor(ImGuiCol_CheckMark, accent);
    bool changed = ImGui::Checkbox(label, v);
    ImGui::PopStyleColor();
    return changed;
}

/// <summary>地色を指定した横幅可変ボタン（width&lt;0 で全幅）</summary>
bool ColoredButton(const char *label, ImVec4 base, float width = -1.0f) {
    ImVec4 hov = {(std::min)(base.x + 0.08f, 1.0f), (std::min)(base.y + 0.08f, 1.0f),
                  (std::min)(base.z + 0.08f, 1.0f), (std::min)(base.w + 0.1f, 1.0f)};
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hov);
    bool hit = ImGui::Button(label, ImVec2(width, 0.0f));
    ImGui::PopStyleColor(2);
    return hit;
}
} // namespace
#endif // _DEBUG

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
        obj3d_->AnimationUpdate();
    }
    SetBlendMode(blendMode_);

    // リジッドボディの物理を更新（重力・速度積分）。
    // 衝突解消（押し出し）はこの後の CollisionManager::Update のコールバックで行う。
    UpdatePhysics(Frame::DeltaTime());
}

void BaseObject::Draw(const ViewProjection &viewProjection) {
    // 描画専用の位置オフセット・回転オフセットを一時的に適用する。
    // これらは描画時のみ反映し、ゲームプレイで参照する transform_ の値は描画後に元へ戻す
    Vector3 originalPosition = transform_->translation_;
    Quaternion originalRotation = transform_->quateRotation_;

    bool hasOffset = (offSet_.x != 0.0f || offSet_.y != 0.0f || offSet_.z != 0.0f);
    bool applyRenderTransform = hasOffset || applyRenderRotationOffset_;

    if (applyRenderTransform) {
        transform_->translation_ = originalPosition + offSet_;

        if (applyRenderRotationOffset_) {
            // ローカル空間の回転として現在の向きへ合成
            transform_->quateRotation_ = originalRotation * renderRotationOffset_;

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
        // オブジェクトの描画
        obj3d_->Draw(*transform_, viewProjection, reflect_, isLighting_, isModelDraw_);
    } else {
        obj3d_->DrawWireframe(*transform_, viewProjection, isRainbow_);
    }

    // 描画専用の変更を元へ戻す
    if (applyRenderTransform) {
        transform_->translation_ = originalPosition;
        transform_->quateRotation_ = originalRotation;
        transform_->UpdateMatrix();
    }
}

void BaseObject::UpdateWorldTransformHierarchy() {
    // まず自分のトランスフォームを更新
    if (transform_) {
        transform_->UpdateMatrix();
    }
    // 子を再帰的に更新
    for (auto it = children_.begin(); it != children_.end();) {
        BaseObject *child = *it;
        child->UpdateWorldTransformHierarchy();
        if (child->parent_ != this) {
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
        auto child = *it;
        // 再帰的に UpdateHierarchy
        child->UpdateHierarchy();

        // 子が「DetachParent()」した場合、parent_ == nullptr になる
        if (child->GetParent() != this) {
            // リストから削除
            it = children_.erase(it);
        } else {
            ++it;
        }
    }
}

void BaseObject::SetParent(BaseObject *parent) {
    if (parent_ == parent || parent == nullptr) {
        return; // 同じ親を持ってる場合何もしない
    }
    if (parent_) {
        DetachParent(); // もし現在の親がいるなら一旦デタッチ
    }

    assert(parent != nullptr && "SetParent to nullptr is not allowed.");

    parent_ = parent;
    // 親の子リストに追加
    parent_->children_.push_back(this);

    if (transform_) {
        transform_->parent_ = parent->GetWorldTransform();
    }
    parentName_ = parent_->GetName();
}

void BaseObject::AddChild(BaseObject *child) {
    assert(child != nullptr && "AddChild is nullptr");
    child->SetParent(this);
}

void BaseObject::DetachParent() {
    if (parent_) {
        parent_->children_.remove(this);
        parent_ = nullptr;
        if (transform_) {
            transform_->parent_ = nullptr;
        }
    }
}

void BaseObject::DetachChild(BaseObject *child) {
    if (!child) {
        return;
    }
    if (child->parent_ != this) {
        return;
    }
    child->parent_ = nullptr;
    if (child->transform_) {
        child->transform_->parent_ = nullptr;
    }
    children_.remove(child);
}

BaseObject *BaseObject::GetParent() {
    return parent_;
}

std::list<BaseObject *> *BaseObject::GetChildren() {
    return &children_;
}

BaseObject *BaseObject::GetChildByName(const std::string &name) {
    for (auto &child : children_) {
        if (child->objectName_ == name) {
            return child;
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
    auto allTexturePaths = obj3d_->GetAllTextruePath();
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
    if (ObjectDatas_) {
        for (int i = 0; i < int(obj3d_->GetMaterialCount()); i++) {
            SetColor(ObjectDatas_->Load<Vector4>("color_" + std::to_string(i), GetColor(i)), i);
        }
    }

    // テクスチャを設定
    for (int i = 0; i < texturePaths_.size(); i++) {
        obj3d_->SetTexture(texturePaths_[i], i);
    }

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

    SetColor(ObjectDatas_->Load<Vector4>("color_" + std::to_string(0), {1.0f, 1.0f, 1.0f, 1.0f}), 0);

    AnimaLoadFromJson();
}

void BaseObject::SaveParentChildRelationship() {
    if (!ObjectDatas_) {
        return;
    }

    // 親の名前を保存
    std::string parentName = parent_ ? parent_->GetName() : "";
    ObjectDatas_->Save<std::string>("parentName", parentName);

    // 子の名前リストを保存
    std::vector<std::string> childrenNames;
    for (const auto &child : children_) {
        if (child) {
            childrenNames.push_back(child->GetName());
        }
    }
    ObjectDatas_->Save<std::vector<std::string>>("childrenNames", childrenNames);
}

void BaseObject::LoadParentChildRelationship() {
    if (!ObjectDatas_) {
        return;
    }

    // 親の名前を読み込み（実際の親付けはBaseObjectManagerで行う）
    std::string parentName = ObjectDatas_->Load<std::string>("parentName", "");

    // 子の名前リストを読み込み（実際の子付けはBaseObjectManagerで行う）
    std::vector<std::string> childrenNames = ObjectDatas_->Load<std::vector<std::string>>("childrenNames", std::vector<std::string>());
}

std::string BaseObject::GetParentName() const {
    return parent_ ? parentName_ : "";
}

std::vector<std::string> BaseObject::GetChildrenNames() const {
    std::vector<std::string> names;
    for (const auto &child : children_) {
        if (child) {
            names.push_back(child->GetName());
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

void BaseObject::SaveToJson() {
    // JSONデータを扱うハンドラを作成
    modelPath_ = obj3d_->GetModelFilePath();
    ObjectDatas_ = std::make_unique<DataHandler>("ObjectDatas", objectName_);
    ObjectDatas_->Save<std::string>("modelName", modelPath_);
    ObjectDatas_->Save<std::string>("objectName", objectName_);
    ObjectDatas_->Save<Vector3>("translation", transform_->translation_);
    ObjectDatas_->Save<Quaternion>("rotation", transform_->quateRotation_);
    ObjectDatas_->Save<Vector3>("scale", transform_->scale_);
    ObjectDatas_->Save<bool>("Lighting", isLighting_);
    ObjectDatas_->Save<PrimitiveType>("PrimitiveType", type_);
    ObjectDatas_->Save<bool>("skeletonDraw", skeletonDraw_);
    ObjectDatas_->Save<bool>("isModelDraw", isModelDraw_);
    if (parent_) {
        ObjectDatas_->Save<std::string>("parentName", parent_->GetName());
    }
    for (int i = 0; i < int(obj3d_->GetMaterialCount()); i++) {
        texturePaths_.push_back(obj3d_->GetTextureFilePath(i));
        ObjectDatas_->Save<std::string>("textureName_" + std::to_string(i), texturePaths_[i]);
        ObjectDatas_->Save("color_" + std::to_string(i), GetColor(i));
    }

    ObjectDatas_->Save<bool>("isLighting", isLighting_);
    ObjectDatas_->Save<int>("blendMode", static_cast<int>(blendMode_));

    SaveParentChildRelationship();

    // 物理（リジッドボディ）情報を保存
    SavePhysics();

    // コライダー情報を保存
    SaveColliders();
    ObjectDatas_->Flush();
}

void BaseObject::SceneSaveToJson() {
    // JSONデータを扱うハンドラを作成
    ObjectDatas_ = std::make_unique<DataHandler>(foldarPath_, objectName_);
    modelPath_ = obj3d_->GetModelFilePath();
    ObjectDatas_->Save<std::string>("modelName", modelPath_);
    ObjectDatas_->Save<std::string>("objectName", objectName_);
    ObjectDatas_->Save<Vector3>("translation", transform_->translation_);
    ObjectDatas_->Save<Quaternion>("rotation", transform_->quateRotation_);
    ObjectDatas_->Save<Vector3>("scale", transform_->scale_);
    ObjectDatas_->Save<bool>("Lighting", isLighting_);
    ObjectDatas_->Save<PrimitiveType>("PrimitiveType", type_);
    ObjectDatas_->Save<bool>("skeletonDraw", skeletonDraw_);
    ObjectDatas_->Save<bool>("isModelDraw", isModelDraw_);
    if (parent_) {
        ObjectDatas_->Save<std::string>("parentName", parent_->GetName());
    }

    for (int i = 0; i < int(obj3d_->GetMaterialCount()); i++) {
        texturePaths_.push_back(obj3d_->GetTextureFilePath(i));
        ObjectDatas_->Save<std::string>("textureName_" + std::to_string(i), texturePaths_[i]);
        ObjectDatas_->Save("color_" + std::to_string(i), GetColor(i));
    }

    ObjectDatas_->Save<bool>("isLighting", isLighting_);
    ObjectDatas_->Save<int>("blendMode", static_cast<int>(blendMode_));

    SaveParentChildRelationship();

    // 物理（リジッドボディ）情報を保存
    SavePhysics();

    // コライダー情報を保存
    SaveColliders();
    ObjectDatas_->Flush();
}

void BaseObject::LoadFromJson() {
    // JSONデータを扱うハンドラを作成
    ObjectDatas_ = std::make_unique<DataHandler>(foldarPath_, objectName_);

    // 基本トランスフォームを読み込み
    transform_->translation_ = ObjectDatas_->Load<Vector3>("translation", {0.0f, 0.0f, 0.0f});
    transform_->quateRotation_ = ObjectDatas_->Load<Quaternion>("rotation", Quaternion::IdentityQuaternion());
    transform_->scale_ = ObjectDatas_->Load<Vector3>("scale", {1.0f, 1.0f, 1.0f});
    isLighting_ = ObjectDatas_->Load<bool>("Lighting", true);
    type_ = ObjectDatas_->Load<PrimitiveType>("PrimitiveType", PrimitiveType::kCount);
    skeletonDraw_ = ObjectDatas_->Load<bool>("skeletonDraw", false);
    isModelDraw_ = ObjectDatas_->Load<bool>("isModelDraw", true);
    parentName_ = ObjectDatas_->Load<std::string>("parentName", "");

    // モデルパスをJSONから読み込み（既に設定されている場合は上書きしない）
    std::string loadedModelPath = ObjectDatas_->Load<std::string>("modelName", "");
    if (!loadedModelPath.empty()) {
        modelPath_ = loadedModelPath;
    }

    // 現在のmodelPath_の状態でプリミティブかどうかを判断
    if (modelPath_.empty()) {
        // プリミティブの場合
        isPrimitive_ = true;
        if (texturePaths_.empty()) {
            texturePaths_.resize(1);
            texturePaths_[0] = ObjectDatas_->Load<std::string>("textureName_0", "debug/uvChecker.png");
        } else {
            texturePaths_[0] = ObjectDatas_->Load<std::string>("textureName_0", texturePaths_[0]);
        }
    } else {
        // 3Dモデルの場合
        isPrimitive_ = false;
        // obj3d_が既に作成されている場合のみテクスチャパスを読み込み
        if (obj3d_ && obj3d_->GetMaterialCount() > 0) {
            texturePaths_.resize(obj3d_->GetMaterialCount());
            for (int i = 0; i < texturePaths_.size(); i++) {
                texturePaths_[i] = ObjectDatas_->Load<std::string>("textureName_" + std::to_string(i), "debug/uvChecker.png");
            }
        }
    }

    isLighting_ = ObjectDatas_->Load<bool>("isLighting", true);
    blendMode_ = static_cast<BlendMode>(ObjectDatas_->Load<int>("blendMode", int(BlendMode::kNormal)));

    LoadParentChildRelationship();

    // 物理（リジッドボディ）情報を読み込み
    LoadPhysics();

    // コライダー情報を読み込み
    LoadColliders();

    // 押し出しが有効ならコライダーにコールバックを仕込む（コライダー生成後に行う）
    if (resolveCollision_) {
        InstallResolveCallbacks();
    }
}

void BaseObject::LoadFromJson(std::string folderPath, std::string jsonName) {
    // JSONデータを扱うハンドラを作成
    ObjectDatas_ = std::make_unique<DataHandler>(folderPath, jsonName);

    // 基本トランスフォームを読み込み
    transform_->translation_ = ObjectDatas_->Load<Vector3>("translation", {0.0f, 0.0f, 0.0f});
    transform_->quateRotation_ = ObjectDatas_->Load<Quaternion>("rotation", Quaternion::IdentityQuaternion());
    transform_->scale_ = ObjectDatas_->Load<Vector3>("scale", {1.0f, 1.0f, 1.0f});
    isLighting_ = ObjectDatas_->Load<bool>("Lighting", true);
    type_ = ObjectDatas_->Load<PrimitiveType>("PrimitiveType", type_);
    skeletonDraw_ = ObjectDatas_->Load<bool>("skeletonDraw", false);
    isModelDraw_ = ObjectDatas_->Load<bool>("isModelDraw", true);
    parentName_ = ObjectDatas_->Load<std::string>("parentName", "");

    // モデルパスをJSONから読み込み（既に設定されている場合は上書きしない）
    std::string loadedModelPath = ObjectDatas_->Load<std::string>("modelName", "");
    if (!loadedModelPath.empty()) {
        modelPath_ = loadedModelPath;
    }

    // 現在のmodelPath_の状態でプリミティブかどうかを判断
    if (modelPath_.empty()) {
        // プリミティブの場合
        isPrimitive_ = true;
        if (texturePaths_.empty()) {
            texturePaths_.resize(1);
            texturePaths_[0] = ObjectDatas_->Load<std::string>("textureName_0", "debug/uvChecker.png");
        } else {
            texturePaths_[0] = ObjectDatas_->Load<std::string>("textureName_0", texturePaths_[0]);
        }
    } else {
        // 3Dモデルの場合
        isPrimitive_ = false;
        // obj3d_が既に作成されている場合のみテクスチャパスを読み込み
        if (obj3d_ && obj3d_->GetMaterialCount() > 0) {
            texturePaths_.resize(obj3d_->GetMaterialCount());
            for (int i = 0; i < texturePaths_.size(); i++) {
                texturePaths_[i] = ObjectDatas_->Load<std::string>("textureName_" + std::to_string(i), texturePaths_[i]);
            }
        }
    }

    isLighting_ = ObjectDatas_->Load<bool>("isLighting", true);
    blendMode_ = static_cast<BlendMode>(ObjectDatas_->Load<int>("blendMode", int(BlendMode::kNormal)));

    LoadParentChildRelationship();

    // 物理（リジッドボディ）情報を読み込み
    LoadPhysics();

    // コライダー情報を読み込み
    LoadColliders();

    // 押し出しが有効ならコライダーにコールバックを仕込む（コライダー生成後に行う）
    if (resolveCollision_) {
        InstallResolveCallbacks();
    }
}

void BaseObject::SaveColliders() {
    if (!ObjectDatas_) {
        return;
    }

    // コライダー数を保存
    ObjectDatas_->Save<int>("colliderCount", static_cast<int>(colliders_.size()));

    // 各コライダーの情報を保存
    for (size_t i = 0; i < colliders_.size(); ++i) {
        auto *collider = colliders_[i].get();
        if (!collider)
            continue;

        std::string prefix = "collider_" + std::to_string(i) + "_";

        // 共通情報
        ObjectDatas_->Save<std::string>(prefix + "name", collider->GetName());
        ObjectDatas_->Save<int>(prefix + "type", static_cast<int>(collider->GetType()));
        ObjectDatas_->Save<std::string>(prefix + "tag", collider->GetTag());
        ObjectDatas_->Save<bool>(prefix + "isEnabled", collider->IsEnabled());
        ObjectDatas_->Save<bool>(prefix + "isVisible", collider->IsVisible());

        // 衝突マスクを保存
        const auto &mask = collider->GetCollisionMask();
        std::vector<std::string> maskList(mask.begin(), mask.end());
        ObjectDatas_->Save<std::vector<std::string>>(prefix + "collisionMask", maskList);

        // 型別の詳細情報を保存
        if (auto *sphere = dynamic_cast<SphereCollider *>(collider)) {
            ObjectDatas_->Save<float>(prefix + "radius", sphere->GetRadius());
            ObjectDatas_->Save<Vector3>(prefix + "offset", sphere->GetOffset());
        } else if (auto *aabb = dynamic_cast<AABBCollider *>(collider)) {
            ObjectDatas_->Save<Vector3>(prefix + "size", aabb->GetSize());
            ObjectDatas_->Save<Vector3>(prefix + "offset", aabb->GetOffset());
        } else if (auto *obb = dynamic_cast<OBBCollider *>(collider)) {
            ObjectDatas_->Save<Vector3>(prefix + "size", obb->GetSize());
            ObjectDatas_->Save<Vector3>(prefix + "rotationOffset", obb->GetRotationOffset());
            ObjectDatas_->Save<Vector3>(prefix + "scaleOffset", obb->GetPositionOffset());
        } else if (auto *cyl = dynamic_cast<CylinderCollider *>(collider)) {
            ObjectDatas_->Save<float>(prefix + "radius", cyl->GetRadius());
            ObjectDatas_->Save<float>(prefix + "height", cyl->GetHeight());
            ObjectDatas_->Save<bool>(prefix + "inward", cyl->IsInward());
        } else if (auto *mesh = dynamic_cast<MeshCollider *>(collider)) {
            // メッシュ形状（三角形データ）は保存せず、オブジェクトのモデルから再構築する。
            // ここでは復元に必要な最低限の情報のみ保存する。
            ObjectDatas_->Save<std::string>(prefix + "sourceModelPath", mesh->GetSourceModelPath());
            ObjectDatas_->Save<bool>(prefix + "wireframeVisible", mesh->IsWireframeVisible());
        }
    }
}

void BaseObject::LoadColliders() {
    if (!ObjectDatas_) {
        return;
    }

    // 既存のコライダーをクリア（登録解除後、unique_ptr が自動解放）
    for (auto &collider : colliders_) {
        if (collider) {
            CollisionManager::GetInstance()->Unregister(collider.get());
        }
    }
    colliders_.clear();

    // コライダー数を読み込み
    int colliderCount = ObjectDatas_->Load<int>("colliderCount", 0);

    // 各コライダーを読み込んで作成
    for (int i = 0; i < colliderCount; ++i) {
        std::string prefix = "collider_" + std::to_string(i) + "_";

        // 型を読み込み
        ColliderType type = static_cast<ColliderType>(
            ObjectDatas_->Load<int>(prefix + "type", 0));

        ColliderBase *collider = nullptr;
        std::unique_ptr<ColliderBase> colliderOwner;

        // 型に応じてコライダーを作成
        switch (type) {
        case ColliderType::Sphere: {
            auto sphere = std::make_unique<SphereCollider>();
            sphere->SetRadius(ObjectDatas_->Load<float>(prefix + "radius", 1.0f));
            sphere->SetOffset(ObjectDatas_->Load<Vector3>(prefix + "offset", {0.0f, 0.0f, 0.0f}));
            collider = sphere.get();
            colliderOwner = std::move(sphere);
            break;
        }
        case ColliderType::AABB: {
            auto aabb = std::make_unique<AABBCollider>();
            aabb->SetSize(ObjectDatas_->Load<Vector3>(prefix + "size", {1.0f, 1.0f, 1.0f}));
            aabb->SetOffset(ObjectDatas_->Load<Vector3>(prefix + "offset", {0.0f, 0.0f, 0.0f}));
            collider = aabb.get();
            colliderOwner = std::move(aabb);
            break;
        }
        case ColliderType::OBB: {
            auto obb = std::make_unique<OBBCollider>();
            obb->SetSize(ObjectDatas_->Load<Vector3>(prefix + "size", {1.0f, 1.0f, 1.0f}));
            obb->SetRotationOffset(ObjectDatas_->Load<Vector3>(prefix + "rotationOffset", {0.0f, 0.0f, 0.0f}));
            obb->SetPositionOffSet(ObjectDatas_->Load<Vector3>(prefix + "scaleOffset", {0.0f, 0.0f, 0.0f}));
            collider = obb.get();
            colliderOwner = std::move(obb);
            break;
        }
        case ColliderType::Cylinder: {
            auto cyl = std::make_unique<CylinderCollider>();
            cyl->SetRadius(ObjectDatas_->Load<float>(prefix + "radius", 30.0f));
            cyl->SetHeight(ObjectDatas_->Load<float>(prefix + "height", 100.0f));
            cyl->SetInward(ObjectDatas_->Load<bool>(prefix + "inward", true));
            collider = cyl.get();
            colliderOwner = std::move(cyl);
            break;
        }
        case ColliderType::Mesh: {
            auto mesh = std::make_unique<MeshCollider>();
            mesh->SetSourceModelPath(ObjectDatas_->Load<std::string>(prefix + "sourceModelPath", modelPath_));
            mesh->SetWireframeVisible(ObjectDatas_->Load<bool>(prefix + "wireframeVisible", true));
            // 三角形データは保存していないため、オブジェクトのモデルから再構築する
            if (obj3d_) {
                mesh->SetMatrixGetter([this]() { return this->GetWorldMatrix(); });
                mesh->BuildFromModel(obj3d_->GetModel());
            }
            collider = mesh.get();
            colliderOwner = std::move(mesh);
            break;
        }
        default:
            continue;
        }

        if (!collider)
            continue;

        // 共通情報を設定
        std::string name = ObjectDatas_->Load<std::string>(prefix + "name", objectName_ + "_Collider" + std::to_string(i));
        collider->SetName(name);
        collider->SetTag(ObjectDatas_->Load<std::string>(prefix + "tag", "None"));
        collider->SetEnabled(ObjectDatas_->Load<bool>(prefix + "isEnabled", true));
        collider->SetVisible(ObjectDatas_->Load<bool>(prefix + "isVisible", true));

        // 衝突マスクを読み込み
        auto maskList = ObjectDatas_->Load<std::vector<std::string>>(
            prefix + "collisionMask",
            std::vector<std::string>());
        for (const auto &maskTag : maskList) {
            collider->AddCollisionMask(maskTag);
        }

        // 位置と回転の取得関数を設定
        collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
        collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });

        // リストに追加して登録
        colliders_.push_back(std::move(colliderOwner));
        CollisionManager::GetInstance()->Register(collider);
    }
}

void BaseObject::AnimaSaveToJson() {
   /* if (!AnimaDatas_) {
        return;
    }
    AnimaDatas_->Save<bool>("Loop");*/
}

void BaseObject::AnimaLoadFromJson() {
   /* AnimaDatas_ = std::make_unique<DataHandler>("Animation", objectName_);
    isLoop_ = AnimaDatas_->Load<bool>("Loop", false);*/
}

void BaseObject::DebugCollider() {
#ifdef _DEBUG
    if (colliders_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted("  コライダーなし");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    for (size_t i = 0; i < colliders_.size(); ++i) {
        auto *col = colliders_[i].get();
        if (!col)
            continue;

        ImGui::PushID((int)i);

        // ヘッダー色：衝突中は赤寄り、非衝突は通常
        bool colliding = col->IsCollidingInCurrentFrame();
        ImVec4 hdrColor = colliding
                              ? ImVec4{0.75f, 0.25f, 0.25f, 0.40f}
                              : ImVec4{0.25f, 0.40f, 0.65f, 0.35f};
        ImVec4 hdrHov = colliding
                            ? ImVec4{0.85f, 0.35f, 0.35f, 0.50f}
                            : ImVec4{0.35f, 0.50f, 0.75f, 0.45f};

        ImGui::PushStyleColor(ImGuiCol_Header, hdrColor);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hdrHov);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, hdrHov);

        bool open = ImGui::CollapsingHeader(col->GetName().c_str());
        ImGui::PopStyleColor(3);

        if (!open) {
            ImGui::PopID();
            continue;
        }

        ImGui::Indent(8.0f);

        // ---- 状態バッジ行 ----
        {
            bool ena = col->IsEnabled();
            bool vis = col->IsVisible();

            ImGui::PushStyleColor(ImGuiCol_CheckMark,
                                  ena ? DebugTheme::kAccentGreen : DebugTheme::kAccentRed);
            if (ImGui::Checkbox("有効##cena", &ena))
                col->SetEnabled(ena);
            ImGui::PopStyleColor();
            ImGui::SameLine(120.f);
            ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentBlue);
            if (ImGui::Checkbox("表示##cvis", &vis))
                col->SetVisible(vis);
            ImGui::PopStyleColor();

            ImGui::SameLine(240.f);
            StatusBadge(colliding ? "衝突中" : "待機",
                        colliding ? DebugTheme::kAccentRed : DebugTheme::kAccentGreen);
        }

        ImGui::Spacing();

        // ---- タグ設定 ----
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgBlue);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.55f, 1.0f, 0.20f));
        if (ImGui::TreeNodeEx("タグ設定##ctag", ImGuiTreeNodeFlags_SpanAvailWidth)) {
            col->ImGuiTagSettings();
            ImGui::TreePop();
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();

        // ---- コライダー形状パラメータ ----
        SectionHeader("[ 形状パラメータ ]", DebugTheme::kAccentCyan);

        if (auto *sphere = dynamic_cast<SphereCollider *>(col)) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentCyan);
            ImGui::TextUnformatted("種別: 球体");
            ImGui::PopStyleColor();

            float r = sphere->GetRadius();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("半径");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgBlue);
            if (ImGui::DragFloat("##srad", &r, 0.1f, 0.1f, 100.f, "%.2f"))
                sphere->SetRadius(r);
            ImGui::PopStyleColor();

            Vector3 off = sphere->GetOffset();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("オフセット (X / Y / Z)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgBlue);
            if (ImGui::DragFloat3("##soff", &off.x, 0.1f, -1000.f, 1000.f, "%.2f"))
                sphere->SetOffset(off);
            ImGui::PopStyleColor();
        } else if (auto *aabb = dynamic_cast<AABBCollider *>(col)) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentGreen);
            ImGui::TextUnformatted("種別: AABB");
            ImGui::PopStyleColor();

            Vector3 sz = aabb->GetSize();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("サイズ (X / Y / Z)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgGreen);
            if (ImGui::DragFloat3("##asz", &sz.x, 0.1f, 0.1f, 100.f, "%.2f"))
                aabb->SetSize(sz);
            ImGui::PopStyleColor();

            Vector3 off = aabb->GetOffset();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("オフセット (X / Y / Z)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgGreen);
            if (ImGui::DragFloat3("##aoff", &off.x, 0.1f, -1000.f, 1000.f, "%.2f"))
                aabb->SetOffset(off);
            ImGui::PopStyleColor();
        } else if (auto *obb = dynamic_cast<OBBCollider *>(col)) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentPurple);
            ImGui::TextUnformatted("種別: OBB");
            ImGui::PopStyleColor();

            Vector3 sz = obb->GetSize();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("サイズ (X / Y / Z)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgPurple);
            if (ImGui::DragFloat3("##osz", &sz.x, 0.1f, 0.1f, 100.f, "%.2f"))
                obb->SetSize(sz);
            ImGui::PopStyleColor();

            Vector3 rotOff = obb->GetRotationOffset();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("回転オフセット (X / Y / Z)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgPurple);
            if (ImGui::DragFloat3("##oroff", &rotOff.x, 0.1f, -1000.f, 1000.f, "%.2f"))
                obb->SetRotationOffset(rotOff);
            ImGui::PopStyleColor();

            Vector3 posOff = obb->GetPositionOffset();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("位置オフセット (X / Y / Z)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgPurple);
            if (ImGui::DragFloat3("##opoff", &posOff.x, 0.1f, -1000.f, 1000.f, "%.2f"))
                obb->SetPositionOffSet(posOff);
            ImGui::PopStyleColor();
        } else if (auto *cyl = dynamic_cast<CylinderCollider *>(col)) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentYellow);
            ImGui::TextUnformatted("種別: 円柱");
            ImGui::PopStyleColor();

            float r = cyl->GetRadius();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("半径");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgYellow);
            if (ImGui::DragFloat("##cyrad", &r, 0.1f, 0.1f, 1000.f, "%.2f"))
                cyl->SetRadius(r);
            ImGui::PopStyleColor();

            float h = cyl->GetHeight();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("高さ");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgYellow);
            if (ImGui::DragFloat("##cyhgt", &h, 0.1f, 0.1f, 1000.f, "%.2f"))
                cyl->SetHeight(h);
            ImGui::PopStyleColor();

            bool inward = cyl->IsInward();
            ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentYellow);
            if (ImGui::Checkbox("内側に閉じ込める##cyin", &inward))
                cyl->SetInward(inward);
            ImGui::PopStyleColor();
        } else if (auto *mesh = dynamic_cast<MeshCollider *>(col)) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
            ImGui::TextUnformatted("種別: メッシュ");
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::Text("三角形数: %d", static_cast<int>(mesh->GetTriangleCount()));
            if (!mesh->GetSourceModelPath().empty())
                ImGui::Text("ソース: %s", mesh->GetSourceModelPath().c_str());
            ImGui::PopStyleColor();

            bool wire = mesh->IsWireframeVisible();
            ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentOrange);
            if (ImGui::Checkbox("ワイヤーフレーム表示##mwire", &wire))
                mesh->SetWireframeVisible(wire);
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // ---- 保存 / 削除ボタン ----
        float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        if (ColoredButton("保存##colsv", {0.20f, 0.45f, 0.20f, 0.80f}, bw)) {
            col->SaveToJson();
            ImGuiNotification::Post("コライダーを保存しました: " + col->GetName(), {0.45f, 0.68f, 0.52f, 1.0f});
        }

        ImGui::SameLine();

        if (ColoredButton("削除##coldel", {0.55f, 0.15f, 0.15f, 0.80f}, bw)) {
            // colliders_ は unique_ptr 所有。erase で ~ColliderBase が走り
            // CollisionManager から自動的に Unregister される（delete は呼ばない）。
            std::string removedName = col->GetName();
            colliders_.erase(colliders_.begin() + i);
            ImGuiNotification::Post("コライダーを削除しました: " + removedName, {0.80f, 0.46f, 0.46f, 1.0f});
            ImGui::Unindent(8.0f);
            ImGui::PopID();
            break;
        }

        ImGui::Unindent(8.0f);
        ImGui::Spacing();
        ImGui::PopID();
    }

    ImGui::PopStyleVar(2);
#endif
}

// ============================================================
//  BaseObject::ImGui  (メインタブ)
// ============================================================
void BaseObject::ImGui() {
#ifdef _DEBUG
    if (!ImGui::BeginTabBar(objectName_.c_str()))
        return;

    if (ImGui::BeginTabItem(objectName_.c_str())) {
        // ---- 状態サマリー（一目で現在の挙動が分かる行）----
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(DebugTheme::kTextDim, "状態:");
        ImGui::SameLine();
        ImGui::TextColored(isAlive_ ? DebugTheme::kAccentGreen : DebugTheme::kAccentRed,
                           isAlive_ ? "生存" : "停止");
        ImGui::SameLine();
        ImGui::TextColored(DebugTheme::kTextDim, "|");
        ImGui::SameLine();
        ImGui::TextColored(rigidBody_.enabled ? DebugTheme::kAccentOrange : DebugTheme::kTextDim,
                           rigidBody_.enabled ? "物理ON" : "物理OFF");
        ImGui::SameLine();
        ImGui::TextColored(resolveCollision_ ? DebugTheme::kAccentOrange : DebugTheme::kTextDim,
                           resolveCollision_ ? "押出ON" : "押出OFF");
        ImGui::SameLine();
        ImGui::TextColored(DebugTheme::kTextDim, "|  コライダー %d 個", static_cast<int>(colliders_.size()));

        ImGui::Separator();

        // ---- 各セクション（保存バーの分だけ高さを残してスクロール領域に収める）----
        float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("BaseObjectBody", ImVec2(0, -footer), ImGuiChildFlags_Borders);
        DebugObject();
        ImGui::EndChild();

        // ---- 保存バー（常に最下部に固定）----
        if (ColoredButton("この設定を全て保存##objsave", {0.20f, 0.45f, 0.20f, 0.80f})) {
            SaveToJson();
            AnimaSaveToJson();
            for (auto &c : colliders_)
                c->SaveToJson();

            ImGuiNotification::Post(std::format("「{}」をセーブしました", objectName_),
                                    {0.45f, 0.68f, 0.52f, 1.0f});
        }
        ImGui::SetItemTooltip("オブジェクト設定・アニメ・全コライダーをまとめて保存する");

        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
#endif // _DEBUG
}

namespace {
/// <summary>
/// コライダー生成時、保存フォルダ(resources/jsons/Collider)に同名のJSONがあれば
/// その設定（サイズ・オフセット・色・表示/判定・タグ・マスク）を読み込む。
/// 無ければ何もしない（コードの既定値のまま）。
/// CollisionManager への登録より前に呼ぶことで、保存されたタグで正しく登録される。
/// </summary>
void LoadColliderIfSaved(ColliderBase *collider) {
    if (!collider)
        return;
    DataHandler probe("Collider", collider->GetName());
    if (probe.Exists()) {
        collider->LoadFromJson();
    }
}
} // namespace

SphereCollider *BaseObject::AddSphereCollider(const std::string &name) {
    auto collider = std::make_unique<SphereCollider>();

    std::string colliderName = name.empty() ? objectName_ + "_SphereCollider" : name;
    collider->SetName(colliderName);

    collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
    collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });

    SphereCollider *raw = collider.get();
    colliders_.push_back(std::move(collider));
    LoadColliderIfSaved(raw); // 保存済み設定があれば反映（登録より前）
    CollisionManager::GetInstance()->Register(raw);

    if (resolveCollision_) {
        raw->SetOnCollision([this, raw](ColliderBase *other) { this->ResolveCollisionWith(raw, other); });
    }
    return raw;
}

AABBCollider *BaseObject::AddAABBCollider(const std::string &name) {
    auto collider = std::make_unique<AABBCollider>();

    std::string colliderName = name.empty() ? objectName_ + "_AABBCollider" : name;
    collider->SetName(colliderName);

    collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
    collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });

    AABBCollider *raw = collider.get();
    colliders_.push_back(std::move(collider));
    LoadColliderIfSaved(raw); // 保存済み設定があれば反映（登録より前）
    CollisionManager::GetInstance()->Register(raw);

    if (resolveCollision_) {
        raw->SetOnCollision([this, raw](ColliderBase *other) { this->ResolveCollisionWith(raw, other); });
    }
    return raw;
}

OBBCollider *BaseObject::AddOBBCollider(const std::string &name) {
    auto collider = std::make_unique<OBBCollider>();

    std::string colliderName = name.empty() ? objectName_ + "_OBBCollider" : name;
    collider->SetName(colliderName);

    collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
    collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });

    OBBCollider *raw = collider.get();
    colliders_.push_back(std::move(collider));
    LoadColliderIfSaved(raw); // 保存済み設定があれば反映（登録より前）
    CollisionManager::GetInstance()->Register(raw);

    if (resolveCollision_) {
        raw->SetOnCollision([this, raw](ColliderBase *other) { this->ResolveCollisionWith(raw, other); });
    }
    return raw;
}

CylinderCollider *BaseObject::AddCylinderCollider(const std::string &name) {
    auto collider = std::make_unique<CylinderCollider>();

    std::string colliderName = name.empty() ? objectName_ + "_CylinderCollider" : name;
    collider->SetName(colliderName);

    collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
    collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });

    CylinderCollider *raw = collider.get();
    colliders_.push_back(std::move(collider));
    LoadColliderIfSaved(raw); // 保存済み設定があれば反映（登録より前）
    CollisionManager::GetInstance()->Register(raw);

    if (resolveCollision_) {
        raw->SetOnCollision([this, raw](ColliderBase *other) { this->ResolveCollisionWith(raw, other); });
    }
    return raw;
}

MeshCollider *BaseObject::AddMeshCollider(const std::string &name) {
    auto collider = std::make_unique<MeshCollider>();

    std::string colliderName = name.empty() ? objectName_ + "_MeshCollider" : name;
    collider->SetName(colliderName);

    // 位置・回転に加え、スケールを含むワールド行列も取得できるよう配線する
    collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
    collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });
    collider->SetMatrixGetter([this]() { return this->GetWorldMatrix(); });

    // 自身のモデル形状（ローカル空間の頂点）から三角形群とBVHを構築する
    if (obj3d_) {
        collider->SetSourceModelPath(modelPath_);
        collider->BuildFromModel(obj3d_->GetModel());
    }

    MeshCollider *raw = collider.get();
    colliders_.push_back(std::move(collider));
    LoadColliderIfSaved(raw); // 保存済み設定があれば反映（登録より前）
    CollisionManager::GetInstance()->Register(raw);

    // 押し出しが有効なら、追加したコライダーにも押し出しコールバックを仕込む
    if (resolveCollision_) {
        raw->SetOnCollision([this, raw](ColliderBase *other) {
            this->ResolveCollisionWith(raw, other);
        });
    }
    return raw;
}

void BaseObject::UpdatePhysics(float deltaTime) {
    if (!rigidBody_.enabled || deltaTime <= 0.0f || !transform_) {
        return;
    }

    // 重力（加速度）を速度へ積分
    if (rigidBody_.useGravity) {
        rigidBody_.velocity += rigidBody_.gravity * deltaTime;
    }

    // 外力を加速度（a = F / m）として速度へ積分
    if (rigidBody_.mass > 1e-4f) {
        rigidBody_.velocity += (accumulatedForce_ / rigidBody_.mass) * deltaTime;
    }
    accumulatedForce_ = {0.0f, 0.0f, 0.0f};

    // 速度の減衰（空気抵抗）
    float damp = 1.0f - rigidBody_.linearDamping * deltaTime;
    if (damp < 0.0f) {
        damp = 0.0f;
    }
    rigidBody_.velocity *= damp;

    // 速度を位置へ積分（ローカル座標。物理は親なしルートオブジェクト向け）
    transform_->translation_ += rigidBody_.velocity * deltaTime;
}

void BaseObject::ResolveCollisionWith(ColliderBase *self, ColliderBase *other) {
    if (!resolveCollision_ || !transform_) {
        return;
    }

    // self を other から押し出す MTV を統一APIで取得する
    Vector3 mtv;
    if (!CollisionManager::GetInstance()->ComputeDepenetration(self, other, mtv)) {
        return;
    }
    if (mtv.LengthSq() < 1e-10f) {
        return;
    }

    // めり込み解消（押し出し）
    transform_->translation_ += mtv;

    // リジッドボディなら、接触面に沿うよう速度を補正する
    if (rigidBody_.enabled) {
        Vector3 n = mtv.Normalize();
        float vn = rigidBody_.velocity.Dot(n);
        if (vn < 0.0f) {
            // 法線方向の侵入成分を除去（反発係数で跳ね返り）
            rigidBody_.velocity -= n * (vn * (1.0f + rigidBody_.restitution));
        }
        // 接線方向に摩擦をかける（坂を滑り落ちる挙動になる）
        Vector3 vTangent = rigidBody_.velocity - n * rigidBody_.velocity.Dot(n);
        rigidBody_.velocity -= vTangent * rigidBody_.friction;
    }
}

void BaseObject::InstallResolveCallbacks() {
    for (auto &c : colliders_) {
        if (!c) {
            continue;
        }
        ColliderBase *self = c.get();
        self->SetOnCollision([this, self](ColliderBase *other) {
            this->ResolveCollisionWith(self, other);
        });
    }
}

void BaseObject::ClearResolveCallbacks() {
    for (auto &c : colliders_) {
        if (c) {
            c->SetOnCollision(nullptr);
        }
    }
}

void BaseObject::SetResolveCollision(bool enable) {
    resolveCollision_ = enable;
    if (enable) {
        InstallResolveCallbacks();
    } else {
        ClearResolveCallbacks();
    }
}

void BaseObject::SavePhysics() {
    if (!ObjectDatas_) {
        return;
    }
    ObjectDatas_->Save<bool>("rb_enabled", rigidBody_.enabled);
    ObjectDatas_->Save<bool>("rb_useGravity", rigidBody_.useGravity);
    ObjectDatas_->Save<float>("rb_mass", rigidBody_.mass);
    ObjectDatas_->Save<Vector3>("rb_gravity", rigidBody_.gravity);
    ObjectDatas_->Save<float>("rb_linearDamping", rigidBody_.linearDamping);
    ObjectDatas_->Save<float>("rb_restitution", rigidBody_.restitution);
    ObjectDatas_->Save<float>("rb_friction", rigidBody_.friction);
    ObjectDatas_->Save<bool>("resolveCollision", resolveCollision_);
}

void BaseObject::LoadPhysics() {
    if (!ObjectDatas_) {
        return;
    }
    rigidBody_.enabled = ObjectDatas_->Load<bool>("rb_enabled", false);
    rigidBody_.useGravity = ObjectDatas_->Load<bool>("rb_useGravity", true);
    rigidBody_.mass = ObjectDatas_->Load<float>("rb_mass", 1.0f);
    rigidBody_.gravity = ObjectDatas_->Load<Vector3>("rb_gravity", {0.0f, -9.8f, 0.0f});
    rigidBody_.linearDamping = ObjectDatas_->Load<float>("rb_linearDamping", 0.05f);
    rigidBody_.restitution = ObjectDatas_->Load<float>("rb_restitution", 0.0f);
    rigidBody_.friction = ObjectDatas_->Load<float>("rb_friction", 0.3f);
    resolveCollision_ = ObjectDatas_->Load<bool>("resolveCollision", false);
    rigidBody_.velocity = {0.0f, 0.0f, 0.0f}; // 速度はランタイム状態なのでリセット
}

void BaseObject::DebugObject() {
#ifdef _DEBUG
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);

    // ====================================================
    // トランスフォーム
    // ====================================================
    if (ThemedHeader("トランスフォーム##hdr", DebugTheme::kAccentBlue, true)) {
        ImGui::Indent(6.0f);

        // ---- Local ----
        SectionHeader("[ ローカル ]", DebugTheme::kAccentBlue);

        // Table: Label | DragFloat3 | ResetBtn
        if (ImGui::BeginTable("LocalTF", 3,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableSetupColumn("Lbl", ImGuiTableColumnFlags_WidthFixed, 62.0f);
            ImGui::TableSetupColumn("Drg", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Btn", ImGuiTableColumnFlags_WidthFixed, 30.0f);

            // -- Position --
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentBlue);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("位置");
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgBlue);
            ImGui::DragFloat3("##lpos", &transform_->translation_.x,
                              0.1f, -1000.f, 1000.f, "%.2f");
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            if (SmallResetButton("[R]##rpos"))
                transform_->translation_ = {};

            // -- Rotation (delta) --
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentCyan);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("回転(差分)");
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            static Vector3 deltaRot{};
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.42f, 0.66f, 0.68f, 0.12f});
            if (ImGui::DragFloat3("##lrot", &deltaRot.x, 0.1f, -10.f, 10.f, "%.1fdeg")) {
                float r = std::numbers::pi_v<float> / 180.f;
                Quaternion cur = transform_->GetRotationQuaternion();
                Quaternion dx = Quaternion::FromAxisAngle({1, 0, 0}, deltaRot.x * r);
                Quaternion dy = Quaternion::FromAxisAngle({0, 1, 0}, deltaRot.y * r);
                Quaternion dz = Quaternion::FromAxisAngle({0, 0, 1}, deltaRot.z * r);
                transform_->SetRotationQuaternion((cur * (dy * dx * dz)).Normalize());
                transform_->UpdateMatrix();
                deltaRot = {};
            }
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            if (SmallResetButton("[R]##rrot")) {
                transform_->SetRotationQuaternion(Quaternion::IdentityQuaternion());
                transform_->UpdateMatrix();
                deltaRot = {};
            }

            // -- Scale --
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentGreen);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("スケール");
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgGreen);
            ImGui::DragFloat3("##lscl", &transform_->scale_.x,
                              0.01f, 0.01f, 10.f, "%.2f");
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            if (SmallResetButton("[R]##rscl"))
                transform_->scale_ = {1, 1, 1};

            ImGui::EndTable();
        }

        // 現在のオイラー角（参考）
        {
            Vector3 e = transform_->GetRotationEuler();
            float d = 180.f / std::numbers::pi_v<float>;
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::Text(" Current Rot  X:%.1f  Y:%.1f  Z:%.1f (deg)",
                        e.x * d, e.y * d, e.z * d);
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // ---- World (read-only) ----
        SectionHeader("[ ワールド (読み取り専用) ]", DebugTheme::kAccentGreen);

        Vector3 wPos = GetWorldPosition();
        Quaternion wRot = GetWorldRotation();
        Vector3 wScale = GetWorldScale();
        float toDeg = 180.f / std::numbers::pi_v<float>;

        // ReadOnlyRow を使って ## が画面に出ないようにする
        // InputFloat3 の ID は ## 始まりで非表示
        auto WorldVec3Row = [](const char *rowLabel,
                               const char *dragId,
                               float x, float y, float z) {
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("  %-10s", rowLabel);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            float v[3] = {x, y, z};
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.12f, 0.12f, 0.14f, 0.8f});
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextReadOnly);
            ImGui::InputFloat3(dragId, v, "%.2f", ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor(2);
        };

        WorldVec3Row("位置", "##wpos", wPos.x, wPos.y, wPos.z);
        WorldVec3Row("Rotation", "##wrot",
                     wRot.x * toDeg, wRot.y * toDeg, wRot.z * toDeg);
        WorldVec3Row("スケール", "##wscl", wScale.x, wScale.y, wScale.z);

        ImGui::Spacing();

        // ---- ImPlot: Scale history ----
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgGreen);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.45f, 0.68f, 0.52f, 0.20f});
        bool plotOpen = ImGui::CollapsingHeader("スケール履歴 (グラフ)##scgr");
        ImGui::PopStyleColor(2);

        if (plotOpen) {
            constexpr int kN = 120;
            static float hx[kN]{}, hy[kN]{}, hz[kN]{};
            static int head = 0, cnt = 0;
            hx[head] = transform_->scale_.x;
            hy[head] = transform_->scale_.y;
            hz[head] = transform_->scale_.z;
            head = (head + 1) % kN;
            if (cnt < kN)
                ++cnt;

            static float dx[kN], dy[kN], dz[kN];
            int s = (head - cnt + kN) % kN;
            for (int i = 0; i < cnt; ++i) {
                int id = (s + i) % kN;
                dx[i] = hx[id];
                dy[i] = hy[id];
                dz[i] = hz[id];
            }

            ImPlot::PushStyleColor(ImPlotCol_PlotBg, {0.08f, 0.08f, 0.10f, 1.0f});
            if (ImPlot::BeginPlot("##scplot", ImVec2(-1, 75),
                                  ImPlotFlags_NoTitle | ImPlotFlags_NoLegend |
                                      ImPlotFlags_NoInputs | ImPlotFlags_NoFrame)) {
                ImPlot::SetupAxes(nullptr, nullptr,
                                  ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_AutoFit);
                ImPlot::SetupAxisLimits(ImAxis_X1, 0, kN, ImGuiCond_Always);
                ImPlot::PushStyleColor(ImPlotCol_Line, DebugTheme::kAccentRed);
                ImPlot::PlotLine("X", dx, cnt);
                ImPlot::PopStyleColor();
                ImPlot::PushStyleColor(ImPlotCol_Line, DebugTheme::kAccentGreen);
                ImPlot::PlotLine("Y", dy, cnt);
                ImPlot::PopStyleColor();
                ImPlot::PushStyleColor(ImPlotCol_Line, DebugTheme::kAccentBlue);
                ImPlot::PlotLine("Z", dz, cnt);
                ImPlot::PopStyleColor();
                ImPlot::EndPlot();
            }
            ImPlot::PopStyleColor();
            // 凡例
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
            ImGui::TextUnformatted(" X");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentGreen);
            ImGui::TextUnformatted(" Y");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentBlue);
            ImGui::TextUnformatted(" Z");
            ImGui::PopStyleColor();
        }

        ImGui::Unindent(6.0f);
        ImGui::Spacing();
    }

    // ====================================================
    // 表示（描画モード・ライティング・ギズモ）
    // ====================================================
    if (ThemedHeader("表示##hdr", DebugTheme::kAccentCyan)) {
        ImGui::Indent(6.0f);

        // ---- 描画モード（モデル / ワイヤーフレームは排他）----
        SectionHeader("[ 描画モード ]", DebugTheme::kAccentBlue);
        if (AccentCheckbox("モデル描画##mdraw", &isModelDraw_, DebugTheme::kAccentBlue) && isModelDraw_)
            isWireframe_ = false;
        ImGui::SameLine(170.0f);
        if (AccentCheckbox("ワイヤーフレーム##wf", &isWireframe_, DebugTheme::kAccentBlue) && isWireframe_)
            isModelDraw_ = false;
        if (isWireframe_) {
            ImGui::SameLine(330.0f);
            AccentCheckbox("レインボー##rb", &isRainbow_, DebugTheme::kAccentYellow);
        } else {
            isRainbow_ = false;
        }

        ImGui::Spacing();

        // ---- ライティング ----
        SectionHeader("[ ライティング ]", DebugTheme::kAccentOrange);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(DebugTheme::kTextDim, "状態:");
        ImGui::SameLine();
        StatusBadge(isLighting_ ? "オン" : "オフ",
                    isLighting_ ? DebugTheme::kAccentGreen : DebugTheme::kAccentRed);
        ImGui::SameLine();
        if (ColoredButton(isLighting_ ? "無効にする##lt" : "有効にする##lt",
                          isLighting_ ? ImVec4{0.55f, 0.25f, 0.25f, 0.70f} : ImVec4{0.25f, 0.45f, 0.25f, 0.70f}, 0.0f))
            isLighting_ = !isLighting_;

        ImGui::Spacing();

        // ---- ギズモ ----
        SectionHeader("[ ギズモ ]", DebugTheme::kAccentRed);
        AccentCheckbox("ギズモ選択可##gsel", &isGizmoSelectable_, DebugTheme::kAccentRed);
        ImGui::SetItemTooltip("オフ: マウスクリック / ギズモ操作の対象外になる");

        ImGui::Unindent(6.0f);
        ImGui::Spacing();
    }

    // ====================================================
    // マテリアル（スロット・カラー・テクスチャ・ブレンド）
    // ====================================================
    if (ThemedHeader("マテリアル##hdr", DebugTheme::kAccentPurple)) {
        ImGui::Indent(6.0f);
        static int selMat = 0;
        size_t matCount = obj3d_->GetMaterialCount();
        if (obj3d_->GetHaveAnimation() && matCount > 1)
            --matCount;

        SectionHeader("[ マテリアルスロット ]", DebugTheme::kAccentPurple);

        if (matCount > 1) {
            std::vector<std::string> items;
            std::vector<const char *> cstrs;
            for (int i = 0; i < (int)matCount; ++i)
                items.push_back("Slot " + std::to_string(i + 1));
            for (auto &s : items)
                cstrs.push_back(s.c_str());
            ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##matslot", &selMat, cstrs.data(), (int)cstrs.size());
            selMat = std::clamp(selMat, 0, (int)matCount - 1);
        } else {
            selMat = 0;
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted("  シングルマテリアル");
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // Color
        ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgPurple);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.62f, 0.50f, 0.74f, 0.20f});
        if (ImGui::TreeNodeEx("カラー##mc", ImGuiTreeNodeFlags_SpanAvailWidth)) {
            Vector4 cur = GetColor(selMat);
            float c[4] = {cur.x, cur.y, cur.z, cur.w};
            ImGui::SetNextItemWidth(-1);
            if (ImGui::ColorEdit4("##colpicker", c))
                SetColor({c[0], c[1], c[2], c[3]}, selMat);
            if (ImGui::SmallButton("カラーリセット##cr"))
                SetColor({1, 1, 1, 1}, selMat);
            ImGui::TreePop();
        }

        // Texture
        if (ImGui::TreeNodeEx("テクスチャ##tx", ImGuiTreeNodeFlags_SpanAvailWidth)) {
            ShowTextureFile(texturePath_);
            ImGui::Spacing();
            if (ImGui::SmallButton("適用##ta")) {
                SetTexture(texturePath_, selMat);
                texturePaths_[selMat] = texturePath_;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("クリア##tc"))
                texturePath_.clear();
            ImGui::TreePop();
        }

        // Blend mode
        if (ImGui::TreeNodeEx("ブレンドモード##bm", ImGuiTreeNodeFlags_SpanAvailWidth)) {
            ShowBlendModeCombo(blendMode_);
            ImGui::TreePop();
        }
        ImGui::PopStyleColor(2);

        ImGui::Unindent(6.0f);
        ImGui::Spacing();
    }

    // ====================================================
    // アニメーション
    // ====================================================
    if (obj3d_->GetHaveAnimation()) {
        if (ThemedHeader("アニメーション##hdr", DebugTheme::kAccentYellow)) {
            ImGui::Indent(6.0f);
            SectionHeader("[ 制御 ]", DebugTheme::kAccentYellow);

            ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentYellow);
            // 現在のアニメーションのループ設定を取得・変更
            std::string currentModelPath = obj3d_->GetModelFilePath();
            bool loop = obj3d_->GetAnimationLoop(currentModelPath);
            if (ImGui::Checkbox("ループ##lp", &loop)) {
                obj3d_->SetAnimationLoop(currentModelPath, loop);
            }

            ImGui::SameLine(130.0f);
            ImGui::Checkbox("スケルトン表示##sk", &skeletonDraw_);
            ImGui::PopStyleColor();
            ImGui::Spacing();

            // アニメーション速度設定
            float speed = obj3d_->GetAnimationSpeed();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentYellow);
            ImGui::TextUnformatted("再生速度");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, DebugTheme::kBgYellow);
            if (ImGui::DragFloat("##aspeed", &speed, 0.01f, 0.0f, 10.0f, "%.2f")) {
                obj3d_->SetAnimationSpeed(speed);
            }
            ImGui::PopStyleColor();

            // ブレンド時間設定
            float blendDuration = obj3d_->GetAnimationBlendDuration();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentCyan);
            ImGui::TextUnformatted("ブレンド時間 (秒)");
            ImGui::PopStyleColor();
            ImGui::SetNextItemWidth(-1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.42f, 0.66f, 0.68f, 0.12f});
            if (ImGui::DragFloat("##ablend", &blendDuration, 0.01f, 0.0f, 5.0f, "%.2f")) {
                obj3d_->SetAnimationBlendDuration(blendDuration);
            }
            ImGui::PopStyleColor();

            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, {0.25f, 0.55f, 0.20f, 0.8f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.30f, 0.70f, 0.25f, 0.9f});
            if (ImGui::Button("再生##aplay", ImVec2(-1, 0)))
                obj3d_->PlayAnimation();
            ImGui::PopStyleColor(2);

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Header, DebugTheme::kBgYellow);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, {0.80f, 0.72f, 0.42f, 0.20f});
            if (ImGui::TreeNodeEx("アニメーション設定##as", ImGuiTreeNodeFlags_SpanAvailWidth)) {
                ShowFileSelector();
                ImGui::TreePop();
            }
            ImGui::PopStyleColor(2);
            ImGui::Unindent(6.0f);
        }
    }

    // ====================================================
    // コライダー
    // ====================================================
    if (ThemedHeader("コライダー##hdr", DebugTheme::kAccentCyan)) {
        ImGui::Indent(6.0f);

        // コライダー追加ボタン
        if (ColoredButton("+ コライダー追加##addcol", {0.20f, 0.40f, 0.60f, 0.80f}, 0.0f))
            ImGui::OpenPopup("AddColliderPopup##acp");
        ImGui::SetItemTooltip("形状を選んで追加。当たって押し返す挙動は『物理』セクションで設定する");

        if (ImGui::BeginPopup("AddColliderPopup##acp")) {
            auto makeDefault = [](auto *c) {
                c->SetTag("Environment");
                c->AddCollisionMask("Player");
            };
            if (ImGui::MenuItem("Sphere")) {
                auto *c = AddSphereCollider();
                makeDefault(c);
                c->SetRadius(1.0f);
            }
            if (ImGui::MenuItem("AABB")) {
                auto *c = AddAABBCollider();
                makeDefault(c);
                c->SetSize({2.0f, 2.0f, 2.0f});
            }
            if (ImGui::MenuItem("OBB")) {
                auto *c = AddOBBCollider();
                makeDefault(c);
                c->SetSize({2.0f, 2.0f, 2.0f});
            }
            if (ImGui::MenuItem("Cylinder")) {
                auto *c = AddCylinderCollider();
                makeDefault(c);
                c->SetRadius(2.0f);
                c->SetHeight(4.0f);
                c->SetInward(false); // 障害物として外側に押し出す
            }
            if (ImGui::MenuItem("Mesh")) {
                // 自身のモデル形状から三角形メッシュコライダーを生成する
                auto *c = AddMeshCollider();
                makeDefault(c);
            }
            ImGui::EndPopup();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        DebugCollider();

        ImGui::Unindent(6.0f);
        ImGui::Spacing();
    }

    // ====================================================
    // 物理（押し出し・リジッドボディ）
    // ====================================================
    if (ThemedHeader("物理 (押し出し / リジッドボディ)##hdr", DebugTheme::kAccentOrange)) {
        ImGui::Indent(6.0f);

        // ---- クイック設定（用途別プリセット）----
        SectionHeader("[ クイック設定 ]", DebugTheme::kAccentGreen);
        ImGui::TextColored(DebugTheme::kTextDim, "用途を選ぶとまとめて設定されます");
        if (ColoredButton("跳ねる物##presetDyn", DebugTheme::kBgGreen, 0.0f)) {
            SetResolveCollision(true);
            rigidBody_.enabled = true;
            rigidBody_.useGravity = true;
            rigidBody_.restitution = 0.5f;
            rigidBody_.velocity = {0.0f, 0.0f, 0.0f};
            ImGuiNotification::Post("プリセット: 跳ねる物（重力＋押し出し＋反発）", {0.45f, 0.68f, 0.52f, 1.0f});
        }
        ImGui::SetItemTooltip("重力で落下し、地面に当たって跳ね返る動的オブジェクト");
        ImGui::SameLine();
        if (ColoredButton("静的な地面##presetStatic", DebugTheme::kBgBlue, 0.0f)) {
            SetResolveCollision(false);
            rigidBody_.enabled = false;
            rigidBody_.useGravity = false;
            rigidBody_.velocity = {0.0f, 0.0f, 0.0f};
            ImGuiNotification::Post("プリセット: 静的な地面（動かない受け止め側）", {0.42f, 0.66f, 0.68f, 1.0f});
        }
        ImGui::SetItemTooltip("動かない床・壁。相手を受け止める側はこちら（押し出しはOFF）");

        ImGui::Spacing();

        // ---- 押し出し（衝突解消）----
        SectionHeader("[ 押し出し（衝突解消）]", DebugTheme::kAccentOrange);
        bool resolve = resolveCollision_;
        if (AccentCheckbox("めり込んだら押し出す##resolve", &resolve, DebugTheme::kAccentOrange))
            SetResolveCollision(resolve);
        ImGui::SetItemTooltip("衝突した相手から自分を押し出す。動かしたい側だけONにする\n"
                              "（床など受け止める側はOFF。独自の衝突処理を持つオブジェクトでも使わない）");

        ImGui::Spacing();

        // ---- リジッドボディ ----
        SectionHeader("[ リジッドボディ ]", DebugTheme::kAccentOrange);
        AccentCheckbox("リジッドボディとして扱う##rbenable", &rigidBody_.enabled, DebugTheme::kAccentGreen);
        ImGui::SetItemTooltip("重力で落下し速度を持つ。押し出しと併用すると坂を滑り落ちる");

        // リジッドボディOFFのときは物理パラメータを淡色＝無効表示にする
        ImGui::BeginDisabled(!rigidBody_.enabled);

        AccentCheckbox("重力を受ける##rbgrav", &rigidBody_.useGravity, DebugTheme::kAccentBlue);

        ImGui::DragFloat("質量##rbmass", &rigidBody_.mass, 0.05f, 0.01f, 1000.0f, "%.2f");
        ImGui::SetItemTooltip("外力 F=ma に効く。大きいほど力で動きにくい");
        ImGui::DragFloat3("重力加速度##rbg", &rigidBody_.gravity.x, 0.1f, -100.0f, 100.0f, "%.2f");
        ImGui::DragFloat("減衰 (空気抵抗)##rbdamp", &rigidBody_.linearDamping, 0.005f, 0.0f, 10.0f, "%.3f");
        ImGui::SetItemTooltip("毎フレーム速度を減らす。大きいほどすぐ止まる");
        ImGui::DragFloat("反発係数##rbrest", &rigidBody_.restitution, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("0=跳ねない / 1=完全反発。押し出しONのとき跳ね返りに効く");
        ImGui::DragFloat("摩擦##rbfric", &rigidBody_.friction, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("接触面に沿う速度の減衰。坂の滑り方に効く");

        ImGui::Spacing();
        Vector3 v = rigidBody_.velocity;
        ReadOnlyRow("速度", "%.2f, %.2f, %.2f", v.x, v.y, v.z);
        if (ColoredButton("速度をリセット##rbresetv", {0.30f, 0.30f, 0.36f, 1.0f}))
            rigidBody_.velocity = {0.0f, 0.0f, 0.0f};

        ImGui::EndDisabled();

        ImGui::Unindent(6.0f);
        ImGui::Spacing();
    }

    // ====================================================
    // ツール（スケールイージング検証）
    // ====================================================
    if (ThemedHeader("ツール##hdr", DebugTheme::kAccentGreen)) {
        ImGui::Indent(6.0f);
        SectionHeader("[ スケールイージング検証 ]", DebugTheme::kAccentGreen);
        DrawScaleEaseImGui();
        ImGui::Unindent(6.0f);
        ImGui::Spacing();
    }

    ImGui::PopStyleVar(4);
#endif // _DEBUG
}

void BaseObject::DrawScaleEaseImGui() {
#ifdef _DEBUG
    // EasingType enum（0〜30）＋ Vector3 Amplitude 拡張（31〜33）の表示名
    // 31 = InElasticAmplitude, 32 = OutElasticAmplitude, 33 = InOutElasticAmplitude
    static const char *kModeNames[] = {
        "Linear",
        "InSine",
        "OutSine",
        "InOutSine",
        "InQuad",
        "OutQuad",
        "InOutQuad",
        "InCubic",
        "OutCubic",
        "InOutCubic",
        "InQuart",
        "OutQuart",
        "InOutQuart",
        "InQuint",
        "OutQuint",
        "InOutQuint",
        "InCirc",
        "OutCirc",
        "InOutCirc",
        "InExpo",
        "OutExpo",
        "InOutExpo",
        "InBack",
        "OutBack",
        "InOutBack",
        "InElastic",
        "OutElastic",
        "InOutElastic",
        "InBounce",
        "OutBounce",
        "InOutBounce",
        // Vector3 振幅による加算オフセット系（EasingType 外の拡張）
        "InElastic  [Amplitude]",
        "OutElastic [Amplitude]",
        "InOutElastic [Amplitude]",
    };
    constexpr int kModeCount = IM_ARRAYSIZE(kModeNames);

    // Amplitude 拡張モード（selectedMode >= 31）かどうかを判定する
    auto IsAmplitudeMode = [](int mode) { return mode >= 31; };

    // ----- イージングタイプ選択 -----
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##setype", &scaleEase_.selectedMode, kModeNames, kModeCount);

    ImGui::Spacing();

    // ----- 所要時間：ラベルを別行に表示して幅いっぱいにドラッグフィールドを配置 -----
    ImGui::TextUnformatted("所要時間");
    ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat("##seT", &scaleEase_.totalTime, 0.05f, 0.05f, 10.0f, "%.2f s");

    ImGui::Spacing();

    if (IsAmplitudeMode(scaleEase_.selectedMode)) {
        // ----- Elastic Amplitude モード：軸ごとに独立した振幅でスケールを加算する -----
        SectionHeader("[ Elastic Amplitude ]", DebugTheme::kAccentGreen);

        // ラベルを別行に出すことで SetNextItemWidth(-1) でも名称が確認できる
        ImGui::TextUnformatted("振幅 (X / Y / Z)");
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat3("##seAmp", &scaleEase_.amplitude.x,
                          0.01f, -5.0f, 5.0f, "%.2f");

        ImGui::TextUnformatted("周期");
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##sePer", &scaleEase_.period,
                         0.01f, 0.01f, 2.0f, "%.2f");

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
        ImGui::TextUnformatted(" スタート時の現在スケールを基準に振幅を加算");
        ImGui::PopStyleColor();
    } else {
        // ----- 通常イージングモード（start -> end 補間） -----
        SectionHeader("[ スタート / エンド スケール ]", DebugTheme::kAccentBlue);

        // ラベルをウィジェット上行に表示し、ボタン分の幅を残して配置
        ImGui::TextUnformatted("スタートスケール");
        ImGui::SetNextItemWidth(-80.0f);
        ImGui::DragFloat3("##seSS", &scaleEase_.startScale.x,
                          0.01f, 0.01f, 20.0f, "%.2f");
        ImGui::SameLine();
        // 現在のスケール値をスタート値としてキャプチャする
        if (ImGui::SmallButton("現在##cpSS")) {
            scaleEase_.startScale = transform_->scale_;
        }

        ImGui::TextUnformatted("エンドスケール");
        ImGui::SetNextItemWidth(-80.0f);
        ImGui::DragFloat3("##seES", &scaleEase_.endScale.x,
                          0.01f, 0.01f, 20.0f, "%.2f");
        ImGui::SameLine();
        // 現在のスケール値をエンド値としてキャプチャする
        if (ImGui::SmallButton("現在##cpES")) {
            scaleEase_.endScale = transform_->scale_;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ----- 再生中：プログレスバーとスケール更新 -----
    if (scaleEase_.isActive) {
        float progress = scaleEase_.currentTime / scaleEase_.totalTime;
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
        ImGui::Spacing();

        // DeltaTime でフレーム経過時間を加算して再生を進める
        float dt = ImGui::GetIO().DeltaTime;
        scaleEase_.currentTime += dt;

        if (scaleEase_.currentTime >= scaleEase_.totalTime) {
            // 再生完了：停止してスケールを終端値に確定する
            scaleEase_.currentTime = scaleEase_.totalTime;
            scaleEase_.isActive = false;
            if (IsAmplitudeMode(scaleEase_.selectedMode)) {
                transform_->scale_ = scaleEase_.baseScale;
            } else {
                transform_->scale_ = scaleEase_.endScale;
            }
        } else {
            // 再生中のスケール更新
            if (IsAmplitudeMode(scaleEase_.selectedMode)) {
                // Vector3 振幅をオフセットとしてベーススケールに加算する
                Vector3 offset;
                if (scaleEase_.selectedMode == 31) {
                    offset = EaseInElasticAmplitude(
                        scaleEase_.currentTime, scaleEase_.totalTime,
                        scaleEase_.amplitude, scaleEase_.period);
                } else if (scaleEase_.selectedMode == 32) {
                    offset = EaseOutElasticAmplitude(
                        scaleEase_.currentTime, scaleEase_.totalTime,
                        scaleEase_.amplitude, scaleEase_.period);
                } else {
                    offset = EaseInOutElasticAmplitude(
                        scaleEase_.currentTime, scaleEase_.totalTime,
                        scaleEase_.amplitude, scaleEase_.period);
                }
                transform_->scale_ = scaleEase_.baseScale + offset;
            } else {
                // EasingType 範囲（0〜30）は start -> end 補間
                transform_->scale_ = ApplyEasing(
                    static_cast<EasingType>(scaleEase_.selectedMode),
                    scaleEase_.startScale, scaleEase_.endScale,
                    scaleEase_.currentTime, scaleEase_.totalTime);
            }
        }
    }

    // ----- スタート / ストップボタン -----
    if (!scaleEase_.isActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.15f, 0.55f, 0.25f, 0.9f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.20f, 0.70f, 0.30f, 1.0f});
        if (ImGui::Button("スタート##sePlay", ImVec2(-1.0f, 0.0f))) {
            scaleEase_.currentTime = 0.0f;
            scaleEase_.isActive = true;
            // スタート時点のスケールをベースとして記録する
            scaleEase_.baseScale = transform_->scale_;
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.55f, 0.15f, 0.15f, 0.9f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.70f, 0.20f, 0.20f, 1.0f});
        if (ImGui::Button("ストップ##seStop", ImVec2(-1.0f, 0.0f))) {
            scaleEase_.isActive = false;
            // ストップ時はスケールをスタート時点の値に戻す
            transform_->scale_ = scaleEase_.baseScale;
        }
        ImGui::PopStyleColor(2);
    }
#endif
}

void BaseObject::ShowFileSelector() {
#ifdef _DEBUG
    // ShowFolder の GLTF ブラウザで選択パスを保持する
    // 選択確定後に「適用」ボタンで SetAnimation を呼び出す
    static std::string selectedGltfPath;

    ShowGltfFile(selectedGltfPath);

    ImGui::Spacing();

    if (!selectedGltfPath.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.25f, 0.45f, 0.70f, 0.80f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.35f, 0.55f, 0.85f, 0.90f});
        if (ImGui::Button("アニメーション適用##applyAnima", ImVec2(-1.0f, 0.0f))) {
            obj3d_->SetAnimation(selectedGltfPath);
        }
        ImGui::PopStyleColor(2);
    }
#endif // _DEBUG
}

void BaseObject::ShowBlendModeCombo(BlendMode &currentMode) {
#ifdef _DEBUG

    // コンボボックスに表示する項目（日本語）
    static const char *blendModeItems[] = {
        "なし",      // kNone
        "通常",      // kNormal
        "加算",      // kAdd
        "減算",      // kSubtract
        "乗算",      // kMultiply
        "スクリーン" // kScreen
    };

    // 現在の選択状態（enumをintにキャスト）
    int currentIndex = static_cast<int>(currentMode);

    // コンボボックス表示
    if (ImGui::Combo("ブレンドモード", &currentIndex, blendModeItems, IM_ARRAYSIZE(blendModeItems))) {
        // ユーザーが選択を変更したときに反映
        currentMode = static_cast<BlendMode>(currentIndex);
    }
#endif // _DEBUG
}
} // namespace Hagine
