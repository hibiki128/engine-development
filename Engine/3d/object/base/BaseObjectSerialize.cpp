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
#include <graphics/texture/TextureManager.h>
#include <imgui_internal.h>
#include <implot.h>
#endif // DEBUG

// オブジェクト・マテリアル・コライダー・アニメーションの保存／読み込み（JSON）。
namespace Hagine {
void BaseObject::SaveToJson() {
    // JSONデータを扱うハンドラを作成
    modelPath_ = obj3d_->GetModelFilePath();
    objectData_ = std::make_unique<DataHandler>("ObjectDatas", objectName_);
    objectData_->Save<std::string>("modelName", modelPath_);
    objectData_->Save<std::string>("objectName", objectName_);
    objectData_->Save<Vector3>("translation", transform_->translation_);
    objectData_->Save<Quaternion>("rotation", transform_->quaternionRotation_);
    objectData_->Save<Vector3>("scale", transform_->scale_);
    objectData_->Save<bool>("Lighting", isLighting_);
    objectData_->Save<PrimitiveType>("PrimitiveType", type_);
    objectData_->Save<bool>("skeletonDraw", skeletonDraw_);
    objectData_->Save<bool>("isModelDraw", isModelDraw_);
    objectData_->Save<bool>("isWireframe", isWireframe_);
    objectData_->Save<bool>("isRainbow", isRainbow_);
    objectData_->Save<bool>("isGizmoSelectable", isGizmoSelectable_);
    if (pParent_) {
        objectData_->Save<std::string>("parentName", pParent_->GetName());
    }
    for (int i = 0; i < int(obj3d_->GetMaterialCount()); i++) {
        // 保存のたびに push_back すると texturePaths_ が肥大化するので、サイズを合わせて代入する
        if (static_cast<int>(texturePaths_.size()) <= i)
            texturePaths_.resize(i + 1);
        texturePaths_[i] = obj3d_->GetTextureFilePath(i);
        objectData_->Save<std::string>("textureName_" + std::to_string(i), texturePaths_[i]);
        objectData_->Save("color_" + std::to_string(i), GetColor(i));
    }

    objectData_->Save<bool>("isLighting", isLighting_);
    objectData_->Save<int>("blendMode", static_cast<int>(blendMode_));

    SaveParentChildRelationship();

    // 物理（リジッドボディ）情報を保存
    SavePhysics();

    // マテリアル（ノーマルマップ関連）情報を保存
    SaveMaterials();

    // コライダー情報を保存
    SaveColliders();
    objectData_->Flush();
}

void BaseObject::SceneSaveToJson() {
    // JSONデータを扱うハンドラを作成
    objectData_ = std::make_unique<DataHandler>(folderPath_, objectName_);
    modelPath_ = obj3d_->GetModelFilePath();
    objectData_->Save<std::string>("modelName", modelPath_);
    objectData_->Save<std::string>("objectName", objectName_);
    objectData_->Save<Vector3>("translation", transform_->translation_);
    objectData_->Save<Quaternion>("rotation", transform_->quaternionRotation_);
    objectData_->Save<Vector3>("scale", transform_->scale_);
    objectData_->Save<bool>("Lighting", isLighting_);
    objectData_->Save<PrimitiveType>("PrimitiveType", type_);
    objectData_->Save<bool>("skeletonDraw", skeletonDraw_);
    objectData_->Save<bool>("isModelDraw", isModelDraw_);
    objectData_->Save<bool>("isWireframe", isWireframe_);
    objectData_->Save<bool>("isRainbow", isRainbow_);
    objectData_->Save<bool>("isGizmoSelectable", isGizmoSelectable_);
    if (pParent_) {
        objectData_->Save<std::string>("parentName", pParent_->GetName());
    }

    for (int i = 0; i < int(obj3d_->GetMaterialCount()); i++) {
        // 保存のたびに push_back すると texturePaths_ が肥大化するので、サイズを合わせて代入する
        if (static_cast<int>(texturePaths_.size()) <= i)
            texturePaths_.resize(i + 1);
        texturePaths_[i] = obj3d_->GetTextureFilePath(i);
        objectData_->Save<std::string>("textureName_" + std::to_string(i), texturePaths_[i]);
        objectData_->Save("color_" + std::to_string(i), GetColor(i));
    }

    objectData_->Save<bool>("isLighting", isLighting_);
    objectData_->Save<int>("blendMode", static_cast<int>(blendMode_));

    SaveParentChildRelationship();

    // 物理（リジッドボディ）情報を保存
    SavePhysics();

    // マテリアル（ノーマルマップ関連）情報を保存
    SaveMaterials();

    // コライダー情報を保存
    SaveColliders();
    objectData_->Flush();
}

void BaseObject::LoadFromJson() {
    // JSONデータを扱うハンドラを作成
    objectData_ = std::make_unique<DataHandler>(folderPath_, objectName_);

    // 基本トランスフォームを読み込み
    transform_->translation_ = objectData_->Load<Vector3>("translation", {0.0f, 0.0f, 0.0f});
    transform_->quaternionRotation_ = objectData_->Load<Quaternion>("rotation", Quaternion::IdentityQuaternion());
    transform_->scale_ = objectData_->Load<Vector3>("scale", {1.0f, 1.0f, 1.0f});

    // 読み込んだTRSをワールド行列へ即反映する。
    // 初回の全体更新前にコライダー構築（行列キャッシュ）や位置問い合わせが
    // 行われても、単位行列のままにならないようにする
    transform_->UpdateMatrix();

    isLighting_ = objectData_->Load<bool>("Lighting", true);
    type_ = objectData_->Load<PrimitiveType>("PrimitiveType", PrimitiveType::Count);
    skeletonDraw_ = objectData_->Load<bool>("skeletonDraw", false);
    isModelDraw_ = objectData_->Load<bool>("isModelDraw", true);
    isWireframe_ = objectData_->Load<bool>("isWireframe", isWireframe_);
    isRainbow_ = objectData_->Load<bool>("isRainbow", isRainbow_);
    isGizmoSelectable_ = objectData_->Load<bool>("isGizmoSelectable", isGizmoSelectable_);
    parentName_ = objectData_->Load<std::string>("parentName", "");

    // モデルパスをJSONから読み込み（既に設定されている場合は上書きしない）
    std::string loadedModelPath = objectData_->Load<std::string>("modelName", "");
    if (!loadedModelPath.empty()) {
        modelPath_ = loadedModelPath;
    }

    // 現在のmodelPath_の状態でプリミティブかどうかを判断
    if (modelPath_.empty()) {
        // プリミティブの場合
        isPrimitive_ = true;
        if (texturePaths_.empty()) {
            texturePaths_.resize(1);
            texturePaths_[0] = objectData_->Load<std::string>("textureName_0", "debug/uvChecker.png");
        } else {
            texturePaths_[0] = objectData_->Load<std::string>("textureName_0", texturePaths_[0]);
        }
    } else {
        // 3Dモデルの場合
        isPrimitive_ = false;
        // obj3d_が既に作成されている場合のみテクスチャパスを読み込み
        if (obj3d_ && obj3d_->GetMaterialCount() > 0) {
            texturePaths_.resize(obj3d_->GetMaterialCount());
            for (int i = 0; i < texturePaths_.size(); i++) {
                texturePaths_[i] = objectData_->Load<std::string>("textureName_" + std::to_string(i), "debug/uvChecker.png");
            }
        }
    }

    isLighting_ = objectData_->Load<bool>("isLighting", true);
    blendMode_ = static_cast<BlendMode>(objectData_->Load<int>("blendMode", int(BlendMode::Normal)));

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
    objectData_ = std::make_unique<DataHandler>(folderPath, jsonName);

    // 基本トランスフォームを読み込み
    transform_->translation_ = objectData_->Load<Vector3>("translation", {0.0f, 0.0f, 0.0f});
    transform_->quaternionRotation_ = objectData_->Load<Quaternion>("rotation", Quaternion::IdentityQuaternion());
    transform_->scale_ = objectData_->Load<Vector3>("scale", {1.0f, 1.0f, 1.0f});

    // 読み込んだTRSをワールド行列へ即反映する（LoadFromJson() と同じ理由）
    transform_->UpdateMatrix();

    isLighting_ = objectData_->Load<bool>("Lighting", true);
    type_ = objectData_->Load<PrimitiveType>("PrimitiveType", type_);
    skeletonDraw_ = objectData_->Load<bool>("skeletonDraw", false);
    isModelDraw_ = objectData_->Load<bool>("isModelDraw", true);
    isWireframe_ = objectData_->Load<bool>("isWireframe", isWireframe_);
    isRainbow_ = objectData_->Load<bool>("isRainbow", isRainbow_);
    isGizmoSelectable_ = objectData_->Load<bool>("isGizmoSelectable", isGizmoSelectable_);
    parentName_ = objectData_->Load<std::string>("parentName", "");

    // モデルパスをJSONから読み込み（既に設定されている場合は上書きしない）
    std::string loadedModelPath = objectData_->Load<std::string>("modelName", "");
    if (!loadedModelPath.empty()) {
        modelPath_ = loadedModelPath;
    }

    // 現在のmodelPath_の状態でプリミティブかどうかを判断
    if (modelPath_.empty()) {
        // プリミティブの場合
        isPrimitive_ = true;
        if (texturePaths_.empty()) {
            texturePaths_.resize(1);
            texturePaths_[0] = objectData_->Load<std::string>("textureName_0", "debug/uvChecker.png");
        } else {
            texturePaths_[0] = objectData_->Load<std::string>("textureName_0", texturePaths_[0]);
        }
    } else {
        // 3Dモデルの場合
        isPrimitive_ = false;
        // obj3d_が既に作成されている場合のみテクスチャパスを読み込み
        if (obj3d_ && obj3d_->GetMaterialCount() > 0) {
            texturePaths_.resize(obj3d_->GetMaterialCount());
            for (int i = 0; i < texturePaths_.size(); i++) {
                texturePaths_[i] = objectData_->Load<std::string>("textureName_" + std::to_string(i), texturePaths_[i]);
            }
        }
    }

    isLighting_ = objectData_->Load<bool>("isLighting", true);
    blendMode_ = static_cast<BlendMode>(objectData_->Load<int>("blendMode", int(BlendMode::Normal)));

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

void BaseObject::SaveMaterials() {
    if (!objectData_ || !obj3d_) {
        return;
    }

    // マテリアルごとのノーマルマップ関連設定を保存する
    for (int i = 0; i < static_cast<int>(obj3d_->GetMaterialCount()); ++i) {
        Material *mat = obj3d_->GetMaterial(i);
        if (!mat)
            continue;

        const MaterialData &md = mat->GetMaterialData();
        std::string prefix = "material_" + std::to_string(i) + "_";

        objectData_->Save<bool>(prefix + "enableNormalMap", md.enableNormalMap);
        objectData_->Save<std::string>(prefix + "normalMapPath", md.hasNormalMapTexture ? md.normalMapFilePath : "");
        objectData_->Save<bool>(prefix + "enableProceduralNormal", md.enableProceduralNormal);
        objectData_->Save<float>(prefix + "proceduralScale", md.proceduralScale);
        objectData_->Save<float>(prefix + "normalStrength", md.normalStrength);

        // UV（タイリング・オフセット・回転）
        objectData_->Save<Vector2>(prefix + "uvSize", md.uvSize);
        objectData_->Save<Vector2>(prefix + "uvPosition", md.uvPosition);
        objectData_->Save<float>(prefix + "uvRotate", md.uvRotate);
    }
}

void BaseObject::LoadMaterials() {
    if (!objectData_ || !obj3d_) {
        return;
    }

    // マテリアルごとのノーマルマップ関連設定を読み込む
    // （キーが存在しない場合は現在値を既定にして何も変えない）
    for (int i = 0; i < static_cast<int>(obj3d_->GetMaterialCount()); ++i) {
        Material *mat = obj3d_->GetMaterial(i);
        if (!mat)
            continue;

        MaterialData &md = mat->GetMaterialData();
        std::string prefix = "material_" + std::to_string(i) + "_";

        // 法線マップ画像が保存されていればテクスチャを読み込んで有効化する
        std::string nmPath = objectData_->Load<std::string>(prefix + "normalMapPath", "");
        if (!nmPath.empty()) {
            mat->SetNormalMap(nmPath);
        }

        // フラグ類は SetNormalMap の副作用より保存値を優先する
        md.enableNormalMap = objectData_->Load<bool>(prefix + "enableNormalMap", md.enableNormalMap);
        md.enableProceduralNormal = objectData_->Load<bool>(prefix + "enableProceduralNormal", md.enableProceduralNormal);
        md.proceduralScale = objectData_->Load<float>(prefix + "proceduralScale", md.proceduralScale);
        mat->SetNormalStrength(objectData_->Load<float>(prefix + "normalStrength", md.normalStrength));

        // UV（タイリング・オフセット・回転）。uvTransform は Draw で毎フレーム組み直される
        md.uvSize = objectData_->Load<Vector2>(prefix + "uvSize", md.uvSize);
        md.uvPosition = objectData_->Load<Vector2>(prefix + "uvPosition", md.uvPosition);
        md.uvRotate = objectData_->Load<float>(prefix + "uvRotate", md.uvRotate);
        // 画像未指定のまま有効な状態は albedo 流用として正規の設定なので落とさない
        // （Material::GetNormalMapIndex が albedo を t3 に束ねる）
    }
}

void BaseObject::SaveColliders() {
    if (!objectData_) {
        return;
    }

    // コライダー数を保存
    objectData_->Save<int>("colliderCount", static_cast<int>(colliders_.size()));

    // 各コライダーの情報を保存
    for (size_t i = 0; i < colliders_.size(); ++i) {
        auto *collider = colliders_[i].get();
        if (!collider)
            continue;

        std::string prefix = "collider_" + std::to_string(i) + "_";

        // 共通情報
        objectData_->Save<std::string>(prefix + "name", collider->GetName());
        objectData_->Save<int>(prefix + "type", static_cast<int>(collider->GetType()));
        objectData_->Save<std::string>(prefix + "tag", collider->GetTag());
        objectData_->Save<bool>(prefix + "isEnabled", collider->IsEnabled());
        objectData_->Save<bool>(prefix + "isVisible", collider->IsVisible());

        // 衝突マスクを保存
        const auto &mask = collider->GetCollisionMask();
        std::vector<std::string> maskList(mask.begin(), mask.end());
        objectData_->Save<std::vector<std::string>>(prefix + "collisionMask", maskList);

        // 型別の詳細情報を保存
        if (auto *sphere = dynamic_cast<SphereCollider *>(collider)) {
            objectData_->Save<float>(prefix + "radius", sphere->GetRadius());
            objectData_->Save<Vector3>(prefix + "offset", sphere->GetOffset());
        } else if (auto *aabb = dynamic_cast<AABBCollider *>(collider)) {
            objectData_->Save<Vector3>(prefix + "size", aabb->GetSize());
            objectData_->Save<Vector3>(prefix + "offset", aabb->GetOffset());
        } else if (auto *obb = dynamic_cast<OBBCollider *>(collider)) {
            objectData_->Save<Vector3>(prefix + "size", obb->GetSize());
            objectData_->Save<Vector3>(prefix + "rotationOffset", obb->GetRotationOffset());
            objectData_->Save<Vector3>(prefix + "scaleOffset", obb->GetPositionOffset());
        } else if (auto *cyl = dynamic_cast<CylinderCollider *>(collider)) {
            objectData_->Save<float>(prefix + "radius", cyl->GetRadius());
            objectData_->Save<float>(prefix + "height", cyl->GetHeight());
            objectData_->Save<bool>(prefix + "inward", cyl->IsInward());
        } else if (auto *mesh = dynamic_cast<MeshCollider *>(collider)) {
            // メッシュ形状（三角形データ）は保存せず、オブジェクトのモデルから再構築する。
            // ここでは復元に必要な最低限の情報のみ保存する。
            objectData_->Save<std::string>(prefix + "sourceModelPath", mesh->GetSourceModelPath());
            objectData_->Save<bool>(prefix + "wireframeVisible", mesh->IsWireframeVisible());
        }
    }
}

void BaseObject::LoadColliders() {
    if (!objectData_) {
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
    int colliderCount = objectData_->Load<int>("colliderCount", 0);

    // 各コライダーを読み込んで作成
    for (int i = 0; i < colliderCount; ++i) {
        std::string prefix = "collider_" + std::to_string(i) + "_";

        // 型を読み込み
        ColliderType type = static_cast<ColliderType>(
            objectData_->Load<int>(prefix + "type", 0));

        ColliderBase *collider = nullptr;
        std::unique_ptr<ColliderBase> colliderOwner;

        // 型に応じてコライダーを作成
        switch (type) {
        case ColliderType::Sphere: {
            auto sphere = std::make_unique<SphereCollider>();
            sphere->SetRadius(objectData_->Load<float>(prefix + "radius", 1.0f));
            sphere->SetOffset(objectData_->Load<Vector3>(prefix + "offset", {0.0f, 0.0f, 0.0f}));
            collider = sphere.get();
            colliderOwner = std::move(sphere);
            break;
        }
        case ColliderType::AABB: {
            auto aabb = std::make_unique<AABBCollider>();
            aabb->SetSize(objectData_->Load<Vector3>(prefix + "size", {1.0f, 1.0f, 1.0f}));
            aabb->SetOffset(objectData_->Load<Vector3>(prefix + "offset", {0.0f, 0.0f, 0.0f}));
            collider = aabb.get();
            colliderOwner = std::move(aabb);
            break;
        }
        case ColliderType::OBB: {
            auto obb = std::make_unique<OBBCollider>();
            obb->SetSize(objectData_->Load<Vector3>(prefix + "size", {1.0f, 1.0f, 1.0f}));
            obb->SetRotationOffset(objectData_->Load<Vector3>(prefix + "rotationOffset", {0.0f, 0.0f, 0.0f}));
            obb->SetPositionOffSet(objectData_->Load<Vector3>(prefix + "scaleOffset", {0.0f, 0.0f, 0.0f}));
            collider = obb.get();
            colliderOwner = std::move(obb);
            break;
        }
        case ColliderType::Cylinder: {
            auto cyl = std::make_unique<CylinderCollider>();
            cyl->SetRadius(objectData_->Load<float>(prefix + "radius", 30.0f));
            cyl->SetHeight(objectData_->Load<float>(prefix + "height", 100.0f));
            cyl->SetInward(objectData_->Load<bool>(prefix + "inward", true));
            collider = cyl.get();
            colliderOwner = std::move(cyl);
            break;
        }
        case ColliderType::Mesh: {
            auto mesh = std::make_unique<MeshCollider>();
            mesh->SetSourceModelPath(objectData_->Load<std::string>(prefix + "sourceModelPath", modelPath_));
            mesh->SetWireframeVisible(objectData_->Load<bool>(prefix + "wireframeVisible", true));
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
        std::string name = objectData_->Load<std::string>(prefix + "name", objectName_ + "_Collider" + std::to_string(i));
        collider->SetName(name);
        collider->SetTag(objectData_->Load<std::string>(prefix + "tag", "None"));
        collider->SetEnabled(objectData_->Load<bool>(prefix + "isEnabled", true));
        collider->SetVisible(objectData_->Load<bool>(prefix + "isVisible", true));

        // 衝突マスクを読み込み
        auto maskList = objectData_->Load<std::vector<std::string>>(
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

} // namespace Hagine
