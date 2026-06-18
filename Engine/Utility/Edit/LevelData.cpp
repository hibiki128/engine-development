#include "LevelData.h"
#include "Engine/Utility/Debug/ImGui/ImGuiNotification.h"
#include <iostream>

namespace Hagine {
void LevelData::LoadFromJson(const std::string &fileName) {
    // 既存データをクリア
    objectsData_.clear();
    createdObjects_.clear();

    std::string filePath = fileName;
    if (filePath.find(".json") == std::string::npos) {
        filePath += ".json";
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filePath << std::endl;
        return;
    }

    json jsonData;
    try {
        file >> jsonData;
    } catch (const json::parse_error &e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return;
    }

    // オブジェクト配列を解析
    if (jsonData.contains("objects") && jsonData["objects"].is_array()) {
        for (const auto &objectJson : jsonData["objects"]) {
            ObjectData objectData = ParseObject(objectJson);
            objectsData_.push_back(objectData);
        }
    }
    ImGuiNotification::Post("レベルデータを読み込みました: " + fileName, {0.2f, 0.8f, 0.8f, 1.0f});
}

void LevelData::CreateObjects() {
    BaseObjectManager *manager = BaseObjectManager::GetInstance();

    for (const auto &objectData : objectsData_) {
        // カメラとライトは飛ばす
        if (objectData.type == "CAMERA" || objectData.type == "LIGHT") {
            continue;
        }

        if (objectData.type == "MESH") {
            auto baseObject = CreateBaseObject(objectData);
            if (baseObject) {
                createdObjects_[objectData.name] = baseObject.get();

                // BaseObjectManagerに追加
                manager->AddObject(std::move(baseObject));
            }
        }
    }

    // 親子関係を設定
    for (const auto &objectData : objectsData_) {
        if (objectData.type == "MESH" && !objectData.children.empty()) {
            auto parentIt = createdObjects_.find(objectData.name);
            if (parentIt != createdObjects_.end()) {
                SetupParentChild(parentIt->second, objectData.children);
            }
        }
    }
    ImGuiNotification::Post("レベルオブジェクトを生成しました", {0.4f, 0.8f, 1.0f, 1.0f});
}

void LevelData::Clear() {
    objectsData_.clear();
    createdObjects_.clear();

    BaseObjectManager::GetInstance()->RemoveAllObjects();
}

LevelData::Transform LevelData::ParseTransform(const json &transformJson) {
    Transform transform;

    if (transformJson.contains("translation") && transformJson["translation"].is_array()) {
        auto trans = transformJson["translation"];
        if (trans.size() >= 3) {
            // Blender座標系から自作エンジン座標系に変換
            transform.translation.x = trans[0].get<float>(); // X軸はそのまま
            transform.translation.y = trans[2].get<float>(); // BlenderのZ → エンジンのY
            transform.translation.z = trans[1].get<float>(); // BlenderのY → エンジンのZ
        }
    }

    if (transformJson.contains("rotation") && transformJson["rotation"].is_array()) {
        auto rot = transformJson["rotation"];
        if (rot.size() >= 3) {
            // Blender座標系から自作エンジン座標系に変換
            transform.rotation.x = -DegreesToRadians(rot[0].get<float>()); // X軸はそのまま
            transform.rotation.y = DegreesToRadians(rot[2].get<float>());  // BlenderのZ → エンジンのY
            transform.rotation.z = DegreesToRadians(rot[1].get<float>());  // BlenderのY → エンジンのZ
        }
    }

    if (transformJson.contains("scaling") && transformJson["scaling"].is_array()) {
        auto scale = transformJson["scaling"];
        if (scale.size() >= 3) {
            // Blender座標系から自作エンジン座標系に変換
            transform.scaling.x = scale[0].get<float>(); // X軸はそのまま
            transform.scaling.y = scale[2].get<float>(); // BlenderのZ → エンジンのY
            transform.scaling.z = scale[1].get<float>(); // BlenderのY → エンジンのZ
        }
    }

    return transform;
}

LevelData::ColliderData LevelData::ParseCollider(const json &colliderJson) {
    ColliderData collider;

    if (colliderJson.contains("type") && colliderJson["type"].is_string()) {
        collider.type = colliderJson["type"].get<std::string>();
    }

    if (colliderJson.contains("center") && colliderJson["center"].is_array()) {
        auto center = colliderJson["center"];
        if (center.size() >= 3) {
            // Blender座標系から自作エンジン座標系に変換
            collider.center.x = center[0].get<float>(); // X軸はそのまま
            collider.center.y = center[2].get<float>(); // BlenderのZ → エンジンのY
            collider.center.z = center[1].get<float>(); // BlenderのY → エンジンのZ
        }
    }

    if (colliderJson.contains("size") && colliderJson["size"].is_array()) {
        auto size = colliderJson["size"];
        if (size.size() >= 3) {
            // Blender座標系から自作エンジン座標系に変換
            collider.size.x = size[0].get<float>() / 2.0f; // X軸はそのまま
            collider.size.y = size[2].get<float>() / 2.0f; // BlenderのZ → エンジンのY
            collider.size.z = size[1].get<float>() / 2.0f; // BlenderのY → エンジンのZ
        }
    }

    return collider;
}

LevelData::ObjectData LevelData::ParseObject(const json &objectJson) {
    ObjectData objectData;

    // タイプを取得
    if (objectJson.contains("type") && objectJson["type"].is_string()) {
        objectData.type = objectJson["type"].get<std::string>();
    }

    // 名前を取得
    if (objectJson.contains("name") && objectJson["name"].is_string()) {
        objectData.name = objectJson["name"].get<std::string>();
    }

    // トランスフォームを解析
    if (objectJson.contains("transform")) {
        objectData.transform = ParseTransform(objectJson["transform"]);
    }

    // コライダーを解析
    if (objectJson.contains("collider")) {
        objectData.collider = ParseCollider(objectJson["collider"]);
        objectData.hasCollider = true;
    }

    // 子オブジェクトを再帰的に解析
    if (objectJson.contains("children") && objectJson["children"].is_array()) {
        for (const auto &childJson : objectJson["children"]) {
            ObjectData childData = ParseObject(childJson);
            objectData.children.push_back(childData);
        }
    }

    return objectData;
}

std::unique_ptr<BaseObject> LevelData::CreateBaseObject(const ObjectData &objectData) {
    auto baseObject = std::make_unique<BaseObject>();

    // 初期化
    baseObject->Init(objectData.name);

    // モデルファイルのパスを生成
    std::string modelPath = "LevelData/" + objectData.name + ".obj";

    // モデルを作成
    baseObject->CreateModel(modelPath);

    // トランスフォームを設定
    baseObject->GetLocalPosition() = objectData.transform.translation;
    baseObject->GetLocalRotation() = Quaternion::FromEulerAngles(objectData.transform.rotation);
    baseObject->GetLocalScale() = objectData.transform.scaling;

    // コライダーを追加（OBBを使用）
    if (objectData.hasCollider) {
        // OBBコライダーを追加
        auto *obbCollider = baseObject->AddOBBCollider(objectData.name + "_Collider");

        // サイズを設定（スケールを考慮）
        Vector3 colliderSize = objectData.collider.size +
                               Vector3(objectData.transform.scaling.x - 1.0f,
                                       objectData.transform.scaling.y - 1.0f,
                                       objectData.transform.scaling.z - 1.0f);
        obbCollider->SetSize(colliderSize);

        // センター（スケールオフセット）を設定
        obbCollider->SetPositionOffSet(objectData.collider.center);

        // タグを設定（必要に応じて）
        obbCollider->SetTag("Environment");

        // 衝突マスクを設定（必要に応じて）
        obbCollider->AddCollisionMask("Player");
        obbCollider->AddCollisionMask("Enemy");
    }

    return baseObject;
}

void LevelData::SetupParentChild(BaseObject *parent, const std::vector<ObjectData> &children) {
    for (const auto &childData : children) {
        if (childData.type == "MESH") {
            // 子オブジェクトを作成
            auto childObject = CreateBaseObject(childData);
            if (childObject) {
                // 親子関係を設定
                childObject->SetParent(parent);

                // 作成したオブジェクトを記録
                createdObjects_[childData.name] = childObject.get();

                // BaseObjectManagerに追加
                BaseObjectManager::GetInstance()->AddObject(std::move(childObject));

                // 再帰的に孫オブジェクトも処理
                if (!childData.children.empty()) {
                    auto childIt = createdObjects_.find(childData.name);
                    if (childIt != createdObjects_.end()) {
                        SetupParentChild(childIt->second, childData.children);
                    }
                }
            }
        }
    }
}

float LevelData::DegreesToRadians(float degrees) {
    return degrees * (3.14159265359f / 180.0f);
}
} // namespace Hagine
