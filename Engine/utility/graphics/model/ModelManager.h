#pragma once
#include "map"
#include "memory"
#include "string"
#include <graphics/srv/SrvManager.h>
#include <model/Model.h>
#include <vector>

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
    /// キーでモデルを引く（完全一致）。
    ///
    /// キーは LoadModel / CreatePrimitiveModel / CreateDynamicModel /
    /// CreateGpuWritableModel の戻り値。**ファイルパスで引いてはいけない。**
    /// gltf は同じパスから複数の実体ができるので、パスからでは
    /// 「どの実体か」を決められない（下の LoadModel の説明を参照）
    /// </summary>
    /// <param name="key">モデルのキー</param>
    /// <returns>Model*: 見つからなければ nullptr</returns>
    Model *FindModelByKey(const std::string &key) const;

    /// <summary>
    /// モデルファイルを読み込み、以降そのモデルを引くためのキーを返す。
    ///
    /// **gltf は呼ぶたびに別の実体を作る。** スキンの出力頂点バッファとパレットSRVを
    /// Model が持っており、描画時に `pSkin_` から引いているため、同じファイルでも
    /// 体ごとに分けないと全員が最後にバインドされた1体と同じポーズになるため。
    ///
    /// したがって**戻り値のキーを必ず保持し、FindModelByKey で引くこと。**
    /// パスで引き直すと、同じパスの別の実体が返ってきて他のキャラと共有してしまう。
    /// </summary>
    /// <param name="filePath">モデルファイルのパス</param>
    /// <returns>std::string: FindModelByKey に渡すキー</returns>
    std::string LoadModel(const std::string &filePath);

    /// <summary>
    /// プリミティブモデルの作成
    /// </summary>
    /// <param name="type"></param>
    std::string CreatePrimitiveModel(PrimitiveType type, std::string texPath);

    /// <summary>
    /// プリミティブモデルの作成（分割数・形状パラメータ指定版）
    /// </summary>
    std::string CreatePrimitiveModel(PrimitiveType type, std::string texPath, const PrimitiveParams &params);

    /// <summary>
    /// 動的メッシュのモデルを作る（メタボールなど、オブジェクトごとに形が違うもの用）。
    /// プリミティブと違って共有できないので、呼ぶたびに新しい実体ができる。
    /// </summary>
    /// <returns>std::string: 生成したモデルのキー</returns>
    std::string CreateDynamicModel(uint32_t vertexCapacity = 4096, uint32_t indexCapacity = 8192);

    /// <summary>
    /// GPU が中身を書くモデルを作る（GPUメタボールなど、形をコンピュートシェーダーが決めるもの用）。
    /// 動的モデルと同じく共有できないので、呼ぶたびに新しい実体ができる。
    /// </summary>
    /// <param name="maxVertexCount">確保する頂点数の上限</param>
    /// <returns>std::string: 生成したモデルのキー</returns>
    std::string CreateGpuWritableModel(uint32_t maxVertexCount);

    /// <summary>
    /// モデルを破棄する（オブジェクト専用に作られたモデルを、そのオブジェクトの破棄時に返す）。
    ///
    /// 一覧からは即座に外すが、**実体を捨てるのは数フレーム後**。
    /// GPU はまだ前のフレームのコマンドを実行している最中で、そこで使われている
    /// 頂点バッファをその場で解放すると、デバッグレイヤーが
    /// 「使用中リソースの解放」を ERROR で止める。
    /// 実際に捨てるのは Update() の仕事
    /// </summary>
    /// <param name="key">破棄するモデルのキー</param>
    void RemoveModel(const std::string &key);

    /// <summary>
    /// ファイルが書き換わったモデルを、次の LoadModel で読み直させる（ホットリロード用）。
    /// 共有している .obj などの実体は別のキーへ退かし（今使っている体は差し替えるまで古い形のまま描ける）、
    /// gltf の体同士で共有している読み込み結果も捨てる
    /// </summary>
    /// <param name="filePath">models ルートからの相対パス（LoadModel に渡すのと同じ物）</param>
    void ForgetModelFile(const std::string &filePath);

    /// <summary>
    /// 破棄を待っているモデルのうち、GPU が触り終わったものを実際に捨てる。
    /// フレームの先頭で1回だけ呼ぶこと
    /// </summary>
    void Update();

    /// <summary>
    /// プリミティブモデルの共有キー（同じ形なら同じキー＝同じ実体になる）
    /// </summary>
    /// <param name="type">プリミティブの種類</param>
    /// <returns>std::string: models_ のキー</returns>
    static std::string MakePrimitiveKey(PrimitiveType type);

    /// <summary>
    /// パラメータ指定のプリミティブモデルの共有キー（形が変わる値だけを混ぜる）
    /// </summary>
    /// <param name="type">プリミティブの種類</param>
    /// <param name="params">形状パラメータ</param>
    /// <returns>std::string: models_ のキー</returns>
    static std::string MakePrimitiveKey(PrimitiveType type, const PrimitiveParams &params);

  public:
    std::unordered_map<std::string, std::unique_ptr<Model>> models_;

  private:
    /// <summary>
    /// 破棄待ちのモデル。GPU が触り終わるまで持っておくための待避所
    /// </summary>
    struct PendingRelease
    {
        std::unique_ptr<Model> model; //!< 捨てる実体
        int framesLeft = 0;           //!< あと何フレーム持っておくか
    };

    // バックバッファは2枚で、PostDraw は「2フレーム前の完了」までしか保証しない。
    // それより1フレーム多く待ってから捨てる
    static constexpr int kReleaseDelayFrames = 3;

    std::vector<PendingRelease> pendingRelease_;

    ModelCommon *pModelCommon_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;
};
} // namespace Hagine
