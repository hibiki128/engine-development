#include "ModelManager.h"
#include <asset/AssetPath.h>
#include "utility/debug/imgui/ImGuiNotification.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <render/raytracing/RaytracingScene.h>
#include <sstream>

namespace Hagine {
std::string ModelManager::LoadModel(const std::string &filePath)
{
    // .gltf は呼ぶたびに別の実体を作る。
    // スキンの出力頂点バッファとパレットSRVを Model が持っていて、描画のときに
    // そこから引いているので、同じファイルでも体ごとに分けないと
    // 「最後にバインドした1体のポーズ」を全員が使うことになる
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
        return uniqueKey;
    }

    // .gltf以外のファイルは元のパスで検索（重複チェック）
    if (models_.contains(filePath))
    {
        return filePath;
    }

    std::unique_ptr<Model> model = std::make_unique<Model>();
    model->Initialize(pModelCommon_);
    model->CreateModel(AssetPath::ModelsRoot(filePath), filePath);
    model->SetSrv(pSrvManager_);
    models_.insert(std::make_pair(filePath, std::move(model)));
    ImGuiNotification::Post("モデルを読み込みました: " + filePath, {0.2f, 0.8f, 0.8f, 1.0f});
    return filePath;
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
    // 「.」を含めないこと。LoadModel が拡張子で gltf 判定をしているため
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
           bits(params.ringOuterRadius) + "_" + bits(params.ringInnerRadius) + "_" +
           // 岩は種とノイズ設定で形が変わるので、これらもキーに混ぜる
           std::to_string(params.rockSeed) + "_" + bits(params.rockNoiseScale) + "_" +
           bits(params.rockNoiseAmount) + "_" + std::to_string(params.rockOctaves) + "_" +
           bits(params.rockFlattenY);
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
    auto it = models_.find(key);
    if (it == models_.end())
    {
        return;
    }

    // 一覧からは今すぐ外す（以降 FindModelByKey では引けない）。
    // 実体は GPU が触り終わるまで持っておく
    pendingRelease_.push_back({std::move(it->second), kReleaseDelayFrames});
    models_.erase(it);
}

void ModelManager::Update()
{
    for (auto it = pendingRelease_.begin(); it != pendingRelease_.end();)
    {
        if (--it->framesLeft > 0)
        {
            ++it;
            continue;
        }

        // レイトレの加速構造は BLAS を Model* で覚えている。
        // 実体を捨てたあと、後から作られた別のモデルが同じアドレスに乗ることがあり、
        // そのとき古い形のBLASが引き当てられてしまう。捨てる直前に必ず外す。
        //
        // 外すのを「一覧から消した時点」ではなくここにしているのは、BLAS自体も
        // GPUリソースだから。猶予フレームを待った今なら、このモデルを使った
        // 描画コマンドは GPU 側で完了している
        RaytracingScene::GetInstance()->OnModelDestroyed(it->model.get());

        it = pendingRelease_.erase(it); // ここで実体が解放される
    }
}

Model *ModelManager::FindModelByKey(const std::string &key) const
{
    // キーは LoadModel / CreatePrimitiveModel / CreateDynamicModel の戻り値。
    //
    // 以前はここでファイルパスの部分一致を取り、当てはまった中の1つを返していた。
    // models_ は unordered_map で並びが不定なので、**読み込んだ直後でも別の実体が
    // 返ってくることがあり**、同じ gltf を使うキャラ同士が1つの Model を共有していた。
    // Model はスキン（出力頂点バッファとパレット）を指しているので、共有すると
    // 後からバインドしたほうのポーズで全員が描かれる（プレイヤーが敵と同じ動きをする）。
    // キーの完全一致にして、この曖昧さごと無くしてある
    auto it = models_.find(key);
    return (it == models_.end()) ? nullptr : it->second.get();
}

void ModelManager::Initialize(SrvManager *pSrvManager, ModelCommon *modelCommon)
{
    pModelCommon_ = modelCommon;
    pSrvManager_ = pSrvManager;
}

void ModelManager::Finalize()
{
    models_.clear();
    pendingRelease_.clear();
}
} // namespace Hagine
