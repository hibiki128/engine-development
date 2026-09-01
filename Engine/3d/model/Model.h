#pragma once
#include "material/Material.h"
#include "mesh/Mesh.h"
#include "ModelCommon.h"
#include "object/Object3dCommon.h"
#include "animation/Animator.h"
#include "animation/Bone.h"
#include "animation/Skin.h"
#include "array"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "map"
#include "span"
#include "type/Matrix4x4.h"
#include "type/Quaternion.h"
#include "type/Vector2.h"
#include "type/Vector3.h"
#include "type/Vector4.h"
#include <graphics/srv/SrvManager.h>
#include <primitive/PrimitiveModel.h>
#include <transform/ObjColor.h>
#include <unordered_set>

/// <summary>
/// モデルクラス
/// 3Dモデルのメッシュ、マテリアル、アニメーションを管理する
/// </summary>
namespace Hagine {
class Model
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="modelCommon">モデル共通クラス</param>
    void Initialize(ModelCommon *modelCommon);

    /// <summary>
    /// モデル作成
    /// </summary>
    /// <param name="directorypath">ディレクトリパス</param>
    /// <param name="filename">ファイル名</param>
    void CreateModel(const std::string &directorypath, const std::string &filename);

    /// <summary>
    /// プリミティブモデル作成
    /// </summary>
    /// <param name="type">プリミティブタイプ</param>
    /// <param name="texPath">テクスチャパス</param>
    void CreatePrimitiveModel(const PrimitiveType &type, std::string texPath);

    /// <summary>
    /// プリミティブモデル作成（分割数・形状パラメータ指定版）
    /// </summary>
    void CreatePrimitiveModel(const PrimitiveType &type, std::string texPath, const PrimitiveParams &params);

    /// <summary>
    /// 動的メッシュのモデルを作る。
    /// メタボールのように毎回頂点数が変わるもの用で、中身は RebuildDynamicMesh() で入れる。
    /// </summary>
    /// <param name="vertexCapacity">最初に確保する頂点数</param>
    /// <param name="indexCapacity">最初に確保するインデックス数</param>
    void CreateDynamicModel(uint32_t vertexCapacity = 4096, uint32_t indexCapacity = 8192);

    /// <summary>
    /// 動的メッシュの中身を差し替える。バッファを切り替えるので 1 フレームに 1 回まで。
    /// </summary>
    /// <param name="data">新しいメッシュデータ（ムーブされる）</param>
    void RebuildDynamicMesh(MeshData &&data);

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="materials">マテリアル配列</param>
    /// <param name="color">色配列</param>
    /// <param name="lighting">ライティング有効フラグ</param>
    /// <param name="reflect">反射有効フラグ</param>
    /// <param name="instanceCount">インスタンス数（1より大きいとインスタンシング描画になる）</param>
    void Draw(const std::vector<std::unique_ptr<Material>> &materials, std::vector<ObjColor> &color, bool lighting, bool reflect, uint32_t instanceCount = 1);

    /// <summary>
    /// シャドウパス描画（深度のみ書き込む）
    /// </summary>
    /// <param name="instanceCount">インスタンス数（1より大きいとインスタンシング描画になる）</param>
    void DrawShadow(uint32_t instanceCount = 1);

    /// <summary>
    /// Getter
    /// </summary>
    // ModelData は全メッシュの頂点配列を抱えるため、値返しにすると
    // 参照するたびにフルコピーが走る。必ず参照で受けること。
    const ModelData &GetModelData() const { return modelData_; }
    bool IsGltf() { return isGltf_; }

    /// <summary>
    /// モデルのローカル空間AABBを取得する（ワールド行列を掛ける前の実際の広がり）
    /// マウス選択のレイ判定や視点フォーカスなど、モデルの実サイズが要る場所で使う
    /// </summary>
    /// <returns>const AABB&: ローカル空間の境界ボックス</returns>
    const AABB &GetLocalBounds() const { return localBounds_; }
    size_t GetMeshCount() const { return meshes_.size(); }
    Mesh *GetMesh(uint32_t index) { return (index < meshes_.size()) ? meshes_[index].get() : nullptr; }
    Animator *GetAnimator() { return pAnimator_; }

    /// <summary>
    /// Setter
    /// </summary>
    void SetSrv(SrvManager *pSrvManager) { pSrvManager_ = pSrvManager; }
    void SetAnimator(Animator *pAnimator)
    {
        pAnimator_ = pAnimator;
        pAnimator_->SetModelData(modelData_);
    }
    void SetSkin(Skin *skin) { pSkin_ = skin; }
    void SetBone(Bone *bone) { pBone_ = bone; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// モデルファイル読み込み
    /// </summary>
    /// <param name="directoryPath">ディレクトリパス</param>
    /// <param name="filename">ファイル名</param>
    /// <returns>ModelData: 読み込んだモデルデータ</returns>
    ModelData LoadModelFile(const std::string &directoryPath, const std::string &filename);

    /// <summary>
    /// ノード読み取り
    /// </summary>
    /// <param name="node">Assimpノード</param>
    /// <returns>Node: 変換したノードデータ</returns>
    static Node ReadNode(aiNode *node);

    /// <summary>
    /// 全メッシュの頂点からローカル空間AABBを計算して localBounds_ に格納する
    /// 頂点が無い場合は単位サイズのボックスにフォールバックする
    /// </summary>
    void CalcLocalBounds();

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    ModelCommon *pModelCommon_;  // モデル共通クラス
    SrvManager *pSrvManager_;    // SRVマネージャー
    ModelData modelData_;       // モデルデータ
    std::string filename_;      // ファイル名
    std::string directorypath_; // ディレクトリパス
    bool isGltf_;               // GLTFフォーマットフラグ
    Matrix4x4 localMatrix_;     // ローカル行列

    // ローカル空間の境界ボックス（モデル読み込み時に頂点から算出）
    AABB localBounds_ = {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};

    // マルチメッシュ対応
    std::vector<std::unique_ptr<Mesh>> meshes_; // メッシュ配列

    // アニメーション関連
    Animator *pAnimator_; // アニメーター
    Skin *pSkin_;         // スキン
    Bone *pBone_;         // ボーン

    bool skinOutputInVertexState_ = false; // skin出力バッファの現在の状態追跡
};
} // namespace Hagine
