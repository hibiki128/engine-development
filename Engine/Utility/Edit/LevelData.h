#pragma once
#include "Object/Base/BaseObjectManager.h"
#include "externals/nlohmann/json.hpp"
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <type/Vector3.h>
#include <Object/Base/BaseObject.h>

namespace Hagine {
class LevelData {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// JSONファイルからレベルデータを読み込み
    /// </summary>
    /// <param name="filePath">JSONファイルのパス</param>
    void LoadFromJson(const std::string &filePath);

    /// <summary>
    /// 読み込んだデータを元にオブジェクトを生成・配置
    /// </summary>
    void CreateObjects();

    /// <summary>
    /// 全オブジェクトをクリア
    /// </summary>
    void Clear();

  private:
    using json = nlohmann::json;

    /// ===================================================
    /// private structs
    /// ===================================================

    struct Transform {
        Vector3 translation = {0.0f, 0.0f, 0.0f};
        Vector3 rotation = {0.0f, 0.0f, 0.0f};
        Vector3 scaling = {1.0f, 1.0f, 1.0f};
    };

    struct ColliderData {
        std::string type;
        Vector3 center = {0.0f, 0.0f, 0.0f};
        Vector3 size = {1.0f, 1.0f, 1.0f};
    };

    struct ObjectData {
        std::string type;
        std::string name;
        Transform transform;
        ColliderData collider;
        std::vector<ObjectData> children;
        bool hasCollider = false;
    };

    /// ===================================================
    /// private methods
    /// ===================================================

    /// <summary>
    /// JSONからTransformデータを解析
    /// </summary>
    Transform ParseTransform(const json &transformJson);

    /// <summary>
    /// JSONからColliderデータを解析
    /// </summary>
    ColliderData ParseCollider(const json &colliderJson);

    /// <summary>
    /// JSONからObjectDataを再帰的に解析
    /// </summary>
    ObjectData ParseObject(const json &objectJson);

    /// <summary>
    /// ObjectDataからBaseObjectを生成
    /// </summary>
    std::unique_ptr<BaseObject> CreateBaseObject(const ObjectData &objectData);

    /// <summary>
    /// オブジェクトの親子関係を設定
    /// </summary>
    void SetupParentChild(BaseObject *parent, const std::vector<ObjectData> &children);

    /// <summary>
    /// 度からラジアンに変換
    /// </summary>
    float DegreesToRadians(float degrees);

    /// ===================================================
    /// private variables
    /// ===================================================

    std::vector<ObjectData> objectsData_;
    std::unordered_map<std::string, BaseObject *> createdObjects_;
};
} // namespace Hagine
