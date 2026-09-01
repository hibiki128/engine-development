#include "ModelManager.h"
#include <asset/AssetPath.h>
#include "utility/debug/imgui/ImGuiNotification.h"
#include <fstream>
#include <functional>
#include <sstream>

namespace Hagine {
void ModelManager::LoadModel(const std::string &filePath)
{

    // .gltfファイルの場合、内容に基づくハッシュを生成しない（毎回新しいモデルを作成）
    if (filePath.substr(filePath.find_last_of(".") + 1) == "gltf")
    {
        // 新しいユニークな識別子を生成する（例えば、インデックスなど）
        static int modelIndex = 0;
        std::string uniqueKey = filePath + "_" + std::to_string(modelIndex++);

        // モデルの生成とファイル読み込み、初期化
        std::unique_ptr<Model> model = std::make_unique<Model>();
        model->Initialize(pModelCommon_);
        model->CreateModel(AssetPath::ModelsRoot(filePath), filePath);
        model->SetSrv(pSrvManager_);

        // モデルをmapコンテナに格納する
        models_.insert(std::make_pair(uniqueKey, std::move(model)));
        ImGuiNotification::Post("モデルを読み込みました: " + filePath, {0.2f, 0.8f, 0.8f, 1.0f});
        return;
    }

    // .gltf以外のファイルは元のパスで検索（重複チェック）
    if (models_.contains(filePath))
    {
        return;
    }

    std::unique_ptr<Model> model = std::make_unique<Model>();
    model->Initialize(pModelCommon_);
    model->CreateModel(AssetPath::ModelsRoot(filePath), filePath);
    model->SetSrv(pSrvManager_);
    models_.insert(std::make_pair(filePath, std::move(model)));
    ImGuiNotification::Post("モデルを読み込みました: " + filePath, {0.2f, 0.8f, 0.8f, 1.0f});
}

std::string ModelManager::CreatePrimitiveModel(PrimitiveType type, std::string texPath)
{
    std::unique_ptr<Model> model = std::make_unique<Model>();
    model->Initialize(pModelCommon_);
    model->CreatePrimitiveModel(type, texPath);
    model->SetSrv(pSrvManager_);
    // モデルのユニークな識別子を生成
    static int modelIndex = 0;
    std::string uniqueKey = "PrimitiveModel_" + std::to_string(modelIndex++);
    // モデルをmapコンテナに格納する
    models_.insert(std::make_pair(uniqueKey, std::move(model)));
    ImGuiNotification::Post("プリミティブモデルを作成しました: " + uniqueKey, {0.4f, 0.8f, 1.0f, 1.0f});
    return uniqueKey;
}

std::string ModelManager::CreatePrimitiveModel(PrimitiveType type, std::string texPath, const PrimitiveParams &params)
{
    std::unique_ptr<Model> model = std::make_unique<Model>();
    model->Initialize(pModelCommon_);
    model->CreatePrimitiveModel(type, texPath, params);
    model->SetSrv(pSrvManager_);
    // パラメータ版は頻繁に作り直されるので通知は出さない（ユニークキーのみ生成）
    static int paramModelIndex = 0;
    std::string uniqueKey = "PrimitiveParamModel_" + std::to_string(paramModelIndex++);
    models_.insert(std::make_pair(uniqueKey, std::move(model)));
    return uniqueKey;
}

std::string ModelManager::CreateDynamicModel(uint32_t vertexCapacity, uint32_t indexCapacity)
{
    std::unique_ptr<Model> model = std::make_unique<Model>();
    model->Initialize(pModelCommon_);
    model->CreateDynamicModel(vertexCapacity, indexCapacity);
    model->SetSrv(pSrvManager_);
    // 動的モデルはオブジェクトごとに 1 個できるので通知は出さない
    static int dynamicModelIndex = 0;
    std::string uniqueKey = "DynamicModel_" + std::to_string(dynamicModelIndex++);
    models_.insert(std::make_pair(uniqueKey, std::move(model)));
    return uniqueKey;
}

void ModelManager::RemoveModel(const std::string &key)
{
    models_.erase(key);
}

Model *ModelManager::FindModel(const std::string &filePath)
{
    // .gltfファイルの場合はファイルパスにユニークな識別子を使って検索
    if (filePath.substr(filePath.find_last_of(".") + 1) == "gltf")
    {
        // 同じファイルパスで複数のモデルがある可能性があるので、それを確認
        std::vector<Model *> matchedModels;

        // キーがファイルパスを含むモデルをすべて収集
        for (const auto &[key, model] : models_)
        {
            if (key.find(filePath) != std::string::npos)
            {
                matchedModels.push_back(model.get());
            }
        }

        // 一致するモデルがあれば、必要に応じて最も新しいモデルなどを選んで返す
        if (!matchedModels.empty())
        {
            // 例えば、最も新しい（インデックスが最大）ものを返す
            return matchedModels.back();
        }
    }
    else
    {
        // .gltf以外のファイルはファイルパスそのもので検索
        if (models_.contains(filePath))
        {
            return models_.at(filePath).get();
        }
    }

    return nullptr;
}

void ModelManager::Initialize(SrvManager *pSrvManager, ModelCommon *modelCommon)
{
    pModelCommon_ = modelCommon;
    pSrvManager_ = pSrvManager;
}

void ModelManager::Finalize()
{
    models_.clear();
}
} // namespace Hagine
