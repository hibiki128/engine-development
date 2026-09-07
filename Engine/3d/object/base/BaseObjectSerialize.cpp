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
#include <algorithm>
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
        objectData_->Save<bool>(prefix + "enableToon", md.enableToon);

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
        // 後から足した項目なので、キーが無い既存データでは現在値（既定は適用する）のままにする
        md.enableToon = objectData_->Load<bool>(prefix + "enableToon", md.enableToon);

        // UV（タイリング・オフセット・回転）。uvTransform は Draw で毎フレーム組み直される
        md.uvSize = objectData_->Load<Vector2>(prefix + "uvSize", md.uvSize);
        md.uvPosition = objectData_->Load<Vector2>(prefix + "uvPosition", md.uvPosition);
        md.uvRotate = objectData_->Load<float>(prefix + "uvRotate", md.uvRotate);
        // 画像未指定のまま有効な状態は albedo 流用として正規の設定なので落とさない
        // （Material::GetNormalMapIndex が albedo を t3 に束ねる）
    }
}

namespace {
/// <summary>
/// コライダー保存JSONのファイル名を分解する。
/// 形式は「&lt;オブジェクト名&gt;_&lt;種別&gt;Collider_&lt;連番&gt;」
/// </summary>
/// <param name="stem">拡張子を除いたファイル名</param>
/// <param name="objectName">持ち主のオブジェクト名</param>
/// <param name="outType">形状種別の出力先</param>
/// <param name="outIndex">連番の出力先</param>
/// <returns>bool: このオブジェクトのコライダーとして解釈できたら true</returns>
bool ParseColliderFileName(const std::string &stem, const std::string &objectName,
                           ColliderType &outType, int &outIndex) {
    const std::string prefix = objectName + "_";
    if (stem.size() <= prefix.size() || stem.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }

    // 残りは "<種別>Collider_<連番>" になっているはず
    const std::string rest = stem.substr(prefix.size());
    const std::string separator = "Collider_";
    const size_t sep = rest.rfind(separator);
    if (sep == std::string::npos) {
        return false;
    }

    // 連番以外が続くもの（名前の頭がたまたま一致した別オブジェクトのファイル）は弾く
    const std::string indexText = rest.substr(sep + separator.size());
    if (indexText.empty() || indexText.size() > 9 ||
        indexText.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }

    const std::string typeName = rest.substr(0, sep);
    for (ColliderType type : {ColliderType::Sphere, ColliderType::AABB, ColliderType::OBB,
                              ColliderType::Cylinder, ColliderType::Mesh}) {
        if (typeName == ColliderTypeName(type)) {
            outType = type;
            outIndex = std::stoi(indexText);
            return true;
        }
    }
    return false;
}

/// <summary>
/// 保存済みコライダーJSONの情報
/// </summary>
struct SavedColliderFile {
    std::string name;  // コライダー名（＝拡張子を除いたファイル名）
    ColliderType type; // 形状種別
    int index;         // 連番
};

/// <summary>
/// jsons/Collider/ から、指定オブジェクトのコライダーJSONを連番順に列挙する
/// </summary>
/// <param name="objectName">持ち主のオブジェクト名</param>
/// <returns>std::vector&lt;SavedColliderFile&gt;: 見つかったJSONの一覧</returns>
std::vector<SavedColliderFile> ListSavedColliderFiles(const std::string &objectName) {
    std::vector<SavedColliderFile> found;
    if (objectName.empty()) {
        return found;
    }

    const std::string folder = AssetPath::JsonRoot() + "/Collider";
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return found;
    }

    for (const auto &entry : fs::directory_iterator(folder, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }

        // オブジェクト名に日本語が含まれても壊れないよう UTF-8 で取り出す
        const std::u8string stemUtf8 = entry.path().stem().u8string();
        const std::string stem(stemUtf8.begin(), stemUtf8.end());

        SavedColliderFile file{};
        if (!ParseColliderFileName(stem, objectName, file.type, file.index)) {
            continue;
        }
        file.name = stem;
        found.push_back(file);
    }

    std::sort(found.begin(), found.end(), [](const SavedColliderFile &a, const SavedColliderFile &b) {
        return a.index != b.index ? a.index < b.index : a.name < b.name;
    });
    return found;
}

/// <summary>
/// 形状種別からコライダーの実体を作る
/// </summary>
/// <param name="type">形状種別</param>
/// <returns>std::unique_ptr&lt;ColliderBase&gt;: 生成したコライダー（未知の種別なら nullptr）</returns>
std::unique_ptr<ColliderBase> CreateColliderOfType(ColliderType type) {
    switch (type) {
    case ColliderType::Sphere:
        return std::make_unique<SphereCollider>();
    case ColliderType::AABB:
        return std::make_unique<AABBCollider>();
    case ColliderType::OBB:
        return std::make_unique<OBBCollider>();
    case ColliderType::Cylinder:
        return std::make_unique<CylinderCollider>();
    case ColliderType::Mesh:
        return std::make_unique<MeshCollider>();
    }
    return nullptr;
}
} // namespace

// コライダーの設定は jsons/Collider/<オブジェクト名>_<種別>Collider_<連番>.json だけに置く。
// 以前はオブジェクトJSONにも同じ設定を書いていて、両者が食い違うとどちらが効くのか
// 分からなくなっていたため、保存場所を個別JSONへ一本化した
void BaseObject::SaveColliders() {
    // 旧形式のキーが残っていたら消す（保存場所が二重になるのを防ぐ）
    if (objectData_) {
        objectData_->Remove("colliderCount");
        objectData_->RemoveByPrefix("collider_");
    }

    std::vector<std::string> savedNames;
    savedNames.reserve(colliders_.size());
    for (auto &collider : colliders_) {
        if (!collider) {
            continue;
        }
        collider->SetOwnerName(objectName_);
        collider->SaveToJson();
        savedNames.push_back(collider->GetName());
    }

    // GUIで削除したコライダーのJSONを消す。
    // 残しておくと、次に読み込んだときに復活してしまう
    for (const SavedColliderFile &file : ListSavedColliderFiles(objectName_)) {
        if (std::find(savedNames.begin(), savedNames.end(), file.name) == savedNames.end()) {
            DataHandler("Collider", file.name).DeleteJson(file.name);
        }
    }
}

void BaseObject::LoadColliders() {
    // 既存のコライダーをクリア（登録解除後、unique_ptr が自動解放）
    for (auto &collider : colliders_) {
        if (collider) {
            CollisionManager::GetInstance()->Unregister(collider.get());
        }
    }
    colliders_.clear();

    const std::vector<SavedColliderFile> savedFiles = ListSavedColliderFiles(objectName_);
    if (savedFiles.empty()) {
        // 個別JSONがまだ無いなら、旧形式で保存された内容を移し替える
        MigrateLegacyColliders();
        return;
    }

    for (const SavedColliderFile &file : savedFiles) {
        AttachColliderFromJson(file.name, file.type);
    }
}

ColliderBase *BaseObject::AttachColliderFromJson(const std::string &colliderName, ColliderType type) {
    std::unique_ptr<ColliderBase> colliderOwner = CreateColliderOfType(type);
    if (!colliderOwner) {
        return nullptr;
    }

    ColliderBase *collider = colliderOwner.get();
    collider->SetName(colliderName);
    collider->SetOwnerName(objectName_);

    // 位置と回転の取得関数を設定
    collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
    collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });

    if (auto *mesh = dynamic_cast<MeshCollider *>(collider)) {
        // 三角形データは保存していないので、オブジェクトのモデルから組み直す
        mesh->SetMatrixGetter([this]() { return this->GetWorldMatrix(); });
        mesh->SetSourceModelPath(modelPath_);
        if (obj3d_) {
            mesh->BuildFromModel(obj3d_->GetModel());
        }
    }

    // 登録より前に保存値を反映する（保存されたタグで CollisionManager に登録されるようにする）
    collider->LoadFromJson();

    colliders_.push_back(std::move(colliderOwner));
    CollisionManager::GetInstance()->Register(collider);
    return collider;
}

void BaseObject::MigrateLegacyColliders() {
    if (!objectData_) {
        return;
    }

    const int colliderCount = objectData_->Load<int>("colliderCount", 0);
    if (colliderCount <= 0) {
        return;
    }

    for (int i = 0; i < colliderCount; ++i) {
        const std::string prefix = "collider_" + std::to_string(i) + "_";
        const ColliderType type = static_cast<ColliderType>(objectData_->Load<int>(prefix + "type", 0));

        std::unique_ptr<ColliderBase> colliderOwner = CreateColliderOfType(type);
        if (!colliderOwner) {
            continue;
        }
        ColliderBase *collider = colliderOwner.get();

        // 型別の詳細情報
        if (auto *sphere = dynamic_cast<SphereCollider *>(collider)) {
            sphere->SetRadius(objectData_->Load<float>(prefix + "radius", 1.0f));
            sphere->SetOffset(objectData_->Load<Vector3>(prefix + "offset", {0.0f, 0.0f, 0.0f}));
        } else if (auto *aabb = dynamic_cast<AABBCollider *>(collider)) {
            aabb->SetSize(objectData_->Load<Vector3>(prefix + "size", {1.0f, 1.0f, 1.0f}));
            aabb->SetOffset(objectData_->Load<Vector3>(prefix + "offset", {0.0f, 0.0f, 0.0f}));
        } else if (auto *obb = dynamic_cast<OBBCollider *>(collider)) {
            obb->SetSize(objectData_->Load<Vector3>(prefix + "size", {1.0f, 1.0f, 1.0f}));
            obb->SetRotationOffset(objectData_->Load<Vector3>(prefix + "rotationOffset", {0.0f, 0.0f, 0.0f}));
            obb->SetPositionOffSet(objectData_->Load<Vector3>(prefix + "scaleOffset", {0.0f, 0.0f, 0.0f}));
        } else if (auto *cyl = dynamic_cast<CylinderCollider *>(collider)) {
            cyl->SetRadius(objectData_->Load<float>(prefix + "radius", 30.0f));
            cyl->SetHeight(objectData_->Load<float>(prefix + "height", 100.0f));
            cyl->SetInward(objectData_->Load<bool>(prefix + "inward", true));
        } else if (auto *mesh = dynamic_cast<MeshCollider *>(collider)) {
            // メッシュ形状（三角形データ）は保存されていないので、モデルから再構築する
            mesh->SetSourceModelPath(objectData_->Load<std::string>(prefix + "sourceModelPath", modelPath_));
            mesh->SetWireframeVisible(objectData_->Load<bool>(prefix + "wireframeVisible", true));
            mesh->SetMatrixGetter([this]() { return this->GetWorldMatrix(); });
            if (obj3d_) {
                mesh->BuildFromModel(obj3d_->GetModel());
            }
        }

        // 共通情報。名前は新しい規則（<オブジェクト名>_<種別>Collider_<連番>）に付け替える
        collider->SetName(MakeColliderName(type));
        collider->SetOwnerName(objectName_);
        collider->SetTag(objectData_->Load<std::string>(prefix + "tag", "None"));
        collider->SetEnabled(objectData_->Load<bool>(prefix + "isEnabled", true));
        collider->SetVisible(objectData_->Load<bool>(prefix + "isVisible", true));

        const auto maskList = objectData_->Load<std::vector<std::string>>(
            prefix + "collisionMask", std::vector<std::string>());
        for (const auto &maskTag : maskList) {
            collider->AddCollisionMask(maskTag);
        }

        // 位置と回転の取得関数を設定
        collider->SetPositionGetter([this]() { return this->GetWorldPosition(); });
        collider->SetRotationGetter([this]() { return this->GetWorldRotation(); });

        // 個別JSONへ書き出す。以降はこちらだけが読まれる
        collider->SaveToJson();

        // 連番の無い旧ファイル名で保存されていた分は消す（設定が混在する原因だった）
        const std::string legacyName = objectData_->Load<std::string>(prefix + "name", "");
        if (!legacyName.empty() && legacyName != collider->GetName()) {
            DataHandler("Collider", legacyName).DeleteJson(legacyName);
        }

        colliders_.push_back(std::move(colliderOwner));
        CollisionManager::GetInstance()->Register(collider);
    }

    // 移行が済んだので旧キーを落とす
    objectData_->Remove("colliderCount");
    objectData_->RemoveByPrefix("collider_");
    objectData_->Flush();
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
