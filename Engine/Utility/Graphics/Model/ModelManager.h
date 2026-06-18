#pragma once
#include "map"
#include "memory"
#include "string"
#include <Graphics/Srv/SrvManager.h>
#include <Model/Model.h>

namespace Hagine {
class ModelManager {
  private:
    ModelManager() = default;
    ~ModelManager() = default;
    ModelManager(ModelManager &) = default;
    ModelManager &operator=(ModelManager &) = default;

  public:
    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="dxCommon"></param>
    void Initialize(SrvManager *srvManager_);

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns></returns>
    static ModelManager *GetInstance() {
        static ModelManager instance;
        return &instance;
    }

    /// <summary>
    /// モデルの検索
    /// </summary>
    /// <param name="filePath"></param>
    /// <returns></returns>
    Model *FindModel(const std::string &filePath);

    /// <summary>
    /// モデルファイルの読み込み
    /// </summary>
    /// <param name="filePath"></param>
    void LoadModel(const std::string &filePath);

    /// <summary>
    /// プリミティブモデルの作成
    /// </summary>
    /// <param name="type"></param>
    std::string CreatePrimitiveModel(PrimitiveType type, std::string texPath);

  public:
    std::unordered_map<std::string, std::unique_ptr<Model>> models_;

  private:
    ModelCommon *modelCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;
};
} // namespace Hagine
