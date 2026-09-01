#pragma once
#include "map"
#include "memory"
#include "string"
#include <graphics/srv/SrvManager.h>
#include <model/Model.h>

namespace Hagine {
class ModelManager
{
  private:
    ModelManager() = default;
    ~ModelManager() = default;
    ModelManager(ModelManager &) = default;
    ModelManager &operator=(ModelManager &) = default;

  public:
    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="pSrvManager">SRVマネージャー</param>
    /// <param name="modelCommon">モデル共通部（Framework が所有・注入する）</param>
    void Initialize(SrvManager *pSrvManager, ModelCommon *modelCommon);

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns></returns>
    static ModelManager *GetInstance()
    {
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

    /// <summary>
    /// プリミティブモデルの作成（分割数・形状パラメータ指定版）
    /// </summary>
    std::string CreatePrimitiveModel(PrimitiveType type, std::string texPath, const PrimitiveParams &params);

  public:
    std::unordered_map<std::string, std::unique_ptr<Model>> models_;

  private:
    ModelCommon *pModelCommon_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;
};
} // namespace Hagine
