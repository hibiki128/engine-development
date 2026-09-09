#include "ModelManager.h"
#include <asset/AssetPath.h>
#include "utility/debug/imgui/ImGuiNotification.h"
#include <cstdint>
#include <cstring>
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
    // 形は PrimitiveType だけで決まる（テクスチャとマテリアルは Object3d 側が1体ずつ持つ）。
    // なので同じ形はモデル実体を共有する。共有すると2つ効く:
    //   ・頂点／インデックスバッファが形ごとに1本で済む。
    //     以前は呼ぶたびに作っていて、しかもプリミティブのモデルは RemoveModel されないので、
    //     シーンを読み込み直すたびに置きっぱなしのモデルが増え続けていた
    //   ・Object3dInstancing がモデルのポインタでバッチをまとめるので、
    //     同じ形のオブジェクトが1回の描画にまとまるようになる
    const std::string key = MakePrimitiveKey(type);
    if (models_.find(key) != models_.end())
    {
        return key;
    }

    std::unique_ptr<Model> model = std::make_unique<Model>();
    model->Initialize(pModelCommon_);
    model->CreatePrimitiveModel(type, texPath);
    model->SetSrv(pSrvManager_);
    models_.insert(std::make_pair(key, std::move(model)));
    ImGuiNotification::Post("プリミティブモデルを作成しました: " + key, {0.4f, 0.8f, 1.0f, 1.0f});
    return key;
}

std::string ModelManager::CreatePrimitiveModel(PrimitiveType type, std::string texPath, const PrimitiveParams &params)
{
    // パラメータ版も形が同じなら共有する。分割数のスライダーを動かすと毎フレーム呼ばれるので、
    // 作り捨てにすると触っただけモデルが積み上がっていた
    const std::string key = MakePrimitiveKey(type, params);
    if (models_.find(key) != models_.end())
    {
        return key;
    }

    std::unique_ptr<Model> model = std::make_unique<Model>();
    model->Initialize(pModelCommon_);
    model->CreatePrimitiveModel(type, texPath, params);
    model->SetSrv(pSrvManager_);
    models_.insert(std::make_pair(key, std::move(model)));
    // パラメータ版は頻繁に呼ばれるので通知は出さない
    return key;
}

std::string ModelManager::MakePrimitiveKey(PrimitiveType type)
{
    // 「.」を含めないこと。FindModel が拡張子で gltf 判定をしているため
    return "PrimitiveModel_" + std::to_string(static_cast<int>(type));
}

std::string ModelManager::MakePrimitiveKey(PrimitiveType type, const PrimitiveParams &params)
{
    // 形が変わる値だけをキーに混ぜる。小数はビット列にして、
    // 表示桁で丸めた別の形が同じキーにならないようにする
    auto bits = [](float value) {
        uint32_t raw = 0;
        std::memcpy(&raw, &value, sizeof(raw));
        return std::to_string(raw);
    };
    return "PrimitiveParamModel_" + std::to_string(static_cast<int>(type)) + "_" +
           std::to_string(params.divide) + "_" + std::to_string(params.heightDivide) + "_" +
           bits(params.ringOuterRadius) + "_" + bits(params.ringInnerRadius);
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

std::string ModelManager::CreateGpuWritableModel(uint32_t maxVertexCount)
{
    std::unique_ptr<Model> model = std::make_unique<Model>();
    model->Initialize(pModelCommon_);
    model->CreateGpuWritableModel(maxVertexCount);
    model->SetSrv(pSrvManager_);
    // 動的モデルと同じくオブジェクトごとに 1 個できるので通知は出さない
    static int gpuModelIndex = 0;
    std::string uniqueKey = "GpuWritableModel_" + std::to_string(gpuModelIndex++);
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
