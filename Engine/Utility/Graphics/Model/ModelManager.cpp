#include "ModelManager.h"
#include "Utility/Debug/ImGui/ImGuiNotification.h"
#include <fstream>
#include <functional>
#include <sstream>

namespace Hagine {
void ModelManager::LoadModel(const std::string &filePath) {

    // .gltfファイルの場合、内容に基づくハッシュを生成しない（毎回新しいモデルを作成）
    if (filePath.substr(filePath.find_last_of(".") + 1) == "gltf") {
        // 新しいユニークな識別子を生成する（例えば、インデックスなど）
        static int modelIndex = 0;
        std::string uniqueKey = filePath + "_" + std::to_string(modelIndex++);

        // モデルの生成とファイル読み込み、初期化
        std::unique_ptr<Model> model = std::make_unique<Model>();
        model->Initialize(modelCommon_);
        model->CreateModel("resources/models/", filePath);
        model->SetSrv(srvManager_);

        // モデルをmapコンテナに格納する
        models_.insert(std::make_pair(uniqueKey, std::move(model)));
        ImGuiNotification::Post("モデルを読み込みました: " + filePath, {0.2f, 0.8f, 0.8f, 1.0f});
        return;
    }

    // .gltf以外のファイルは元のパスで検索（重複チェック）
    if (models_.contains(filePath)) {
        return;
    }

    std::unique_ptr<Model> model = std::make_unique<Model>();
    model->Initialize(modelCommon_);
    model->CreateModel("resources/models/", filePath);
    model->SetSrv(srvManager_);
    models_.insert(std::make_pair(filePath, std::move(model)));
    ImGuiNotification::Post("モデルを読み込みました: " + filePath, {0.2f, 0.8f, 0.8f, 1.0f});
}

std::string ModelManager::CreatePrimitiveModel(PrimitiveType type, std::string texPath) {
    std::unique_ptr<Model> model = std::make_unique<Model>();
    model->Initialize(modelCommon_);
    model->CreatePrimitiveModel(type,texPath);
    model->SetSrv(srvManager_);
    // モデルのユニークな識別子を生成
    static int modelIndex = 0;
    std::string uniqueKey = "PrimitiveModel_" + std::to_string(modelIndex++);
    // モデルをmapコンテナに格納する
    models_.insert(std::make_pair(uniqueKey, std::move(model)));
    ImGuiNotification::Post("プリミティブモデルを作成しました: " + uniqueKey, {0.4f, 0.8f, 1.0f, 1.0f});
    return uniqueKey;
}

Model *ModelManager::FindModel(const std::string &filePath) {
    // .gltfファイルの場合はファイルパスにユニークな識別子を使って検索
    if (filePath.substr(filePath.find_last_of(".") + 1) == "gltf") {
        // 同じファイルパスで複数のモデルがある可能性があるので、それを確認
        std::vector<Model *> matchedModels;

        // キーがファイルパスを含むモデルをすべて収集
        for (const auto &[key, model] : models_) {
            if (key.find(filePath) != std::string::npos) {
                matchedModels.push_back(model.get());
            }
        }

        // 一致するモデルがあれば、必要に応じて最も新しいモデルなどを選んで返す
        if (!matchedModels.empty()) {
            // 例えば、最も新しい（インデックスが最大）ものを返す
            return matchedModels.back();
        }
    } else {
        // .gltf以外のファイルはファイルパスそのもので検索
        if (models_.contains(filePath)) {
            return models_.at(filePath).get();
        }
    }

    return nullptr;
}

void ModelManager::Initialize(SrvManager *srvManager_) {
    modelCommon_ = ModelCommon::GetInstance();
    modelCommon_->Initialize();
    this->srvManager_ = srvManager_;
}

void ModelManager::Finalize() {
    models_.clear();
}
} // namespace Hagine
