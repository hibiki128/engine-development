#pragma once
#include "Graphics/PipeLine/PipeLineManager.h"
#include "Transform/WorldTransform.h"
#include "array"
#include "wrl.h"
#include <Transform/WorldTransform.h>
#include <d3d12.h>
#include <list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <type/Matrix4x4.h>
#include <type/Quaternion.h>
#include <type/Vector2.h>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <vector>

/// <summary>
/// クォータニオン変換データ
/// </summary>
namespace Hagine {
struct QuaternionTransform {
    Vector3 scale{};   // スケール
    Quaternion rotate{}; // 回転
    Vector3 translate{}; // 平行移動
};

/// <summary>
/// 頂点データ
/// </summary>
struct VertexData {
    Vector4 position{}; // 座標
    Vector2 texcoord{}; // UV座標
    Vector3 normal{};   // 法線
};

/// <summary>
/// マテリアルデータ（CPU側）
/// </summary>
struct MaterialData {
    Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};  // 色
    bool enableLighting = true;                // ライティング有効フラグ
    Matrix4x4 uvTransform = MakeIdentity4x4(); // UV変換行列
    float shininess = 20.0f;                   // 光沢度
    std::string textureFilePath{};             // テクスチャファイルパス
    uint32_t textureIndex = 0;                 // テクスチャインデックス
    float environmentCoefficient = 1.0f;       // 環境マップ係数
    Vector2 uvPosition{};                      // UV座標
    Vector2 uvSize = {1.0f, 1.0f};             // UVサイズ
    float uvRotate = 0.0f;                     // UV回転
};

/// <summary>
/// マテリアルデータ（GPU側）
/// </summary>
struct MaterialDataGPU {
    Vector4 color{};              // 色
    int32_t enableLighting{};     // ライティング有効フラグ
    float padding[3]{};           // パディング
    Matrix4x4 uvTransform{};      // UV変換行列
    float shininess{};            // 光沢度
    float environmentCoefficient{}; // 環境マップ係数
    float padding2[2]{};            // パディング
};

/// <summary>
/// メッシュデータ
/// </summary>
struct MeshData {
    std::vector<VertexData> vertices{}; // 頂点配列
    std::vector<uint32_t> indices{};    // インデックス配列
    uint32_t materialIndex = 0;       // マテリアルインデックス
};

/// <summary>
/// ノードデータ
/// </summary>
struct Node {
    QuaternionTransform transform{}; // 変換
    Matrix4x4 localMatrix{};         // ローカル行列
    std::string name{};              // ノード名
    std::vector<Node> children{};    // 子ノード配列
};

/// <summary>
/// ジョイントデータ
/// </summary>
struct Joint {
    QuaternionTransform transform{}; // 変換
    Matrix4x4 localMatrix{};         // ローカル行列
    Matrix4x4 skeletonSpaceMatrix{}; // スケルトン空間行列
    std::string name{};              // ジョイント名
    std::vector<int32_t> children{}; // 子ジョイントインデックス配列
    int32_t index{};                 // インデックス
    std::optional<int32_t> parent{}; // 親ジョイントインデックス
};

/// <summary>
/// スケルトンデータ
/// </summary>
struct Skeleton {
    int32_t root{};                          // ルートジョイントインデックス
    std::map<std::string, int32_t> jointMap{}; // ジョイント名マップ
    std::vector<Joint> joints{};               // ジョイント配列
};

/// <summary>
/// 頂点ウェイトデータ
/// </summary>
struct VertexWeightData {
    float weight{};         // ウェイト値
    uint32_t vertexIndex{}; // 頂点インデックス
    uint32_t meshIndex{};   // メッシュインデックス
};

/// <summary>
/// ジョイントウェイトデータ
/// </summary>
struct JointWeightData {
    Matrix4x4 inverseBindPoseMatrix{};           // 逆バインドポーズ行列
    std::vector<VertexWeightData> vertexWeights{}; // 頂点ウェイト配列
};

/// <summary>
/// モデルデータ
/// </summary>
struct ModelData {
    std::vector<MeshData> meshes{};                         // メッシュ配列
    std::vector<MaterialData> materials{};                  // マテリアル配列
    std::map<std::string, JointWeightData> skinClusterData{}; // スキンクラスターデータ
    Node rootNode{};                                          // ルートノード
    bool hasBones{};                                          // ボーン有無フラグ
    bool hasAnimations{};                                     // アニメーション有無フラグ
};

static const uint32_t kNumMaxInfluence = 4; // 最大影響数

/// <summary>
/// 頂点影響度データ
/// </summary>
struct VertexInfluence {
    std::array<float, kNumMaxInfluence> weights{};      // ウェイト配列
    std::array<int32_t, kNumMaxInfluence> jointIndices{}; // ジョイントインデックス配列
};

/// <summary>
/// GPU用ウェルデータ
/// </summary>
struct WellForGPU {
    Matrix4x4 skeletonSpaceMatrix{};               // スケルトン空間行列
    Matrix4x4 skeletonSpaceInverseTransposeMatrix{}; // スケルトン空間逆転置行列
};

/// <summary>
/// GPU用スキニング情報
/// </summary>
struct SkinningInformationForGPU {
    uint32_t numVertices; // 頂点数
};

/// <summary>
/// スキンクラスターデータ
/// </summary>
struct SkinCluster {
    std::vector<Matrix4x4> inverseBindPoseMatrices{}; // 逆バインドポーズ行列配列

    // 影響度データ
    Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource{};                             // リソース
    std::span<VertexInfluence> mappedInfluence{};                                           // マップデータ
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> influenceSrvHandle{}; // SRVハンドル

    // パレットデータ
    Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource{};                             // リソース
    std::span<WellForGPU> mappedPalette{};                                                // マップデータ
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> paletteSrvHandle{}; // SRVハンドル

    // 入力頂点データ
    Microsoft::WRL::ComPtr<ID3D12Resource> inputVertexResource{};                             // リソース
    std::span<VertexData> mappedVertex{};                                                     // マップデータ
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> inputVertexSrvHandle{}; // SRVハンドル

    // 出力頂点データ
    Microsoft::WRL::ComPtr<ID3D12Resource> outputVertexResource{};                             // リソース
    D3D12_VERTEX_BUFFER_VIEW outputVertexBufferView{};                                         // 頂点バッファビュー
    std::pair<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> outputVertexSrvHandle{}; // SRVハンドル

    // スキニング情報
    Microsoft::WRL::ComPtr<ID3D12Resource> skinningInformationResource{}; // リソース
    SkinningInformationForGPU *SkinningInfomationData = nullptr;        // データポインタ
};

/// <summary>
/// Vector3キーフレーム
/// </summary>
struct KeyframeVector3 {
    Vector3 value{}; // 値
    float time{};    // 時間
};

/// <summary>
/// Quaternionキーフレーム
/// </summary>
struct KeyframeQuaternion {
    Quaternion value{}; // 値
    float time{};       // 時間
};

/// <summary>
/// ノードアニメーションデータ
/// </summary>
struct NodeAnimation {
    std::vector<KeyframeVector3> translate{}; // 平行移動キーフレーム
    std::vector<KeyframeQuaternion> rotate{}; // 回転キーフレーム
    std::vector<KeyframeVector3> scale{};     // スケールキーフレーム
};

/// <summary>
/// アニメーションデータ
/// </summary>
struct Animation {
    float duration{};                                    // アニメーション時間
    std::map<std::string, NodeAnimation> nodeAnimations{}; // ノードアニメーションマップ
};
} // namespace Hagine
