#pragma once
#include "MetaBall.h"
#include "d3d12.h"
#include "type/Vector3.h"
#include "type/Vector4.h"
#include "wrl.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hagine {
class DirectXCommon;
class Object3d;
class SrvManager;
class ViewProjection;
class WorldTransform;

/// <summary>
/// GPU メタボールの生成パラメータ（CPU 版の MetaBallWorldParams に相当）
/// </summary>
struct MetaBallGpuParams
{
    float voxelSize = 0.15f;      // セル1辺の長さ
    float threshold = 0.5f;       // 等値面のしきい値
    float uvScale = 1.0f;         // 平面投影 UV のスケール
    float wobbleAmplitude = 0.0f; // 脈動の振幅（0 で静止）
    float wobbleSpeed = 3.0f;     // 脈動の速さ
    float wobbleFrequency = 1.0f; // 位置による位相のばらけ具合
};

/// <summary>
/// GPU メタボールの要素1個（halfExtents が 0 なら球、それ以外は角の丸い箱）
/// </summary>
struct MetaBallGpuBall
{
    Vector3 position{};    // 中心
    float radius = 1.0f;   // 影響半径（箱は表面からの距離）
    float stiffness = 1.0f; // 中心での密度の高さ
    Vector3 halfExtents{}; // 箱の半分の大きさ
};

/// <summary>
/// 直近の生成規模（ImGui 表示用）
/// </summary>
struct MetaBallGpuStats
{
    uint32_t ballCount = 0;      // 入力した要素の数
    uint32_t gridX = 0;          // サンプル点の数（X）
    uint32_t gridY = 0;          // サンプル点の数（Y）
    uint32_t gridZ = 0;          // サンプル点の数（Z）
    uint32_t cellCount = 0;      // セルの総数
    uint32_t maxVertexCount = 0; // 出力できる頂点数の上限
    float cellSize = 0.0f;       // セル1辺の長さ
};

/// <summary>
/// メタボールをコンピュートシェーダーで三角形化するクラス（出力先は CreateGpuWritableModel で作った Object3d）
/// </summary>
class MetaBallGpuField
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// バッファとディスクリプタの解放
    /// </summary>
    ~MetaBallGpuField();

    /// <summary>
    /// 生成に使うバッファを用意
    /// </summary>
    /// <param name="name">デバッグ名</param>
    /// <param name="maxBallCount">要素数の上限</param>
    /// <param name="maxGridSamples">1軸あたりのサンプル点数の上限</param>
    /// <param name="ringSlots">要素配列の面数（1フレームの Dispatch 回数×2〜3）</param>
    void Initialize(const std::string &name, uint32_t maxBallCount, uint32_t maxGridSamples = 96,
                    uint32_t ringSlots = 16);

    /// <summary>
    /// 今フレームの要素を差し替え（半径・強さは全要素共通）
    /// </summary>
    /// <param name="positions">球の中心</param>
    /// <param name="radius">影響半径</param>
    /// <param name="stiffness">中心での密度の高さ</param>
    void SetBalls(const std::vector<Vector3> &positions, float radius, float stiffness = 1.0f);

    /// <summary>
    /// 今フレームの要素を差し替え（要素ごとに半径・強さ・箱の大きさを指定）
    /// </summary>
    /// <param name="balls">要素</param>
    void SetBalls(const std::vector<MetaBallGpuBall> &balls);

    /// <summary>
    /// コンピュートコマンドリストへ生成を積む（Clear → Density → March。事前に BeginComputeFrame が必要）
    /// </summary>
    /// <param name="pCommandList">コンピュートコマンドリスト</param>
    /// <param name="target">出力先（CreateGpuWritableModel 済みの Object3d）</param>
    /// <param name="params">生成パラメータ</param>
    /// <param name="time">脈動に使う時間（秒）</param>
    void Dispatch(ID3D12GraphicsCommandList *pCommandList, Object3d *target,
                  const MetaBallGpuParams &params, float time);

    /// <summary>
    /// 出力先に張った UAV の記録を破棄（出力先の Object3d を作り直すときに呼ぶ）
    /// </summary>
    void ClearTargets();

    bool IsEmpty() const { return ballCount_ == 0; }
    const MetaBallGpuStats &GetStats() const { return stats_; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// 出力先の頂点バッファに UAV を張り、記述子番号を返す（同じ出力先なら使い回し）
    /// </summary>
    uint32_t EnsureTargetUav(ID3D12Resource *pVertexResource, uint32_t vertexCount);

    /// <summary>
    /// Marching Cubes の三角形テーブルを GPU へ転送
    /// </summary>
    void UploadTriTable();

    /// <summary>
    /// 要素数を決めて今回書き込む面の先頭を返す（要素が無ければ nullptr）
    /// </summary>
    Vector4 *BeginBalls(size_t count);

    /// <summary>
    /// 要素1個を今回の面へ書き込み、範囲を広げる
    /// </summary>
    void WriteBall(Vector4 *pSlot, uint32_t index, const MetaBallGpuBall &ball);

    /// ===================================================
    /// private variables
    /// ===================================================

    DirectXCommon *pDxCommon_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;
    std::string name_;

    // 要素配列（float4 × 3 / 1個）
    Microsoft::WRL::ComPtr<ID3D12Resource> ballResource_;
    Vector4 *pBallData_ = nullptr;
    uint32_t maxBallCount_ = 0;
    uint32_t ballCount_ = 0;
    uint32_t ringSlots_ = 1;    // 要素配列の面数
    uint32_t nextSlot_ = 0;     // 次に書き込む面
    uint32_t currentSlot_ = 0;  // 直近に書き込んだ面
    float maxBallRadius_ = 0.0f; // 一番大きい影響半径
    Vector3 boundsMin_{};
    Vector3 boundsMax_{};

    // Marching Cubes の三角形テーブル（256 × 16）
    Microsoft::WRL::ComPtr<ID3D12Resource> triTableResource_;

    // 密度場
    Microsoft::WRL::ComPtr<ID3D12Resource> densityResource_;
    uint32_t densityUavIndex_ = 0;
    uint32_t maxGridSamples_ = 0;

    // 頂点数のカウンタ
    Microsoft::WRL::ComPtr<ID3D12Resource> counterResource_;
    uint32_t counterUavIndex_ = 0;

    // 出力先の頂点バッファごとの UAV 番号（非所有）
    std::unordered_map<ID3D12Resource *, uint32_t> targetUavIndices_;

    MetaBallGpuStats stats_{};
};

/// <summary>
/// GPU メタボールの生成から描画までをまとめたクラス（出力を複数面で回し、毎フレーム作り直してもちらつかない）
/// </summary>
class MetaBallGpuSurface
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    MetaBallGpuSurface();

    /// <summary>
    /// 出力モデルの解放
    /// </summary>
    ~MetaBallGpuSurface();

    MetaBallGpuSurface(const MetaBallGpuSurface &) = delete;
    MetaBallGpuSurface &operator=(const MetaBallGpuSurface &) = delete;

    /// <summary>
    /// 出力モデルと生成用バッファを用意
    /// </summary>
    /// <param name="name">デバッグ名</param>
    /// <param name="texturePath">貼るテクスチャ</param>
    /// <param name="maxBallCount">要素数の上限</param>
    /// <param name="maxTriangleCount">三角形数の上限</param>
    /// <param name="maxGridSamples">1軸あたりのサンプル点数の上限</param>
    void Initialize(const std::string &name, const std::string &texturePath, uint32_t maxBallCount,
                    uint32_t maxTriangleCount, uint32_t maxGridSamples = 96);

    /// <summary>
    /// 今フレームの要素を差し替え
    /// </summary>
    void SetBalls(const std::vector<MetaBallGpuBall> &balls);

    /// <summary>
    /// 生成をコンピュートキューへ積む（DrawSystem::kGPUParticleCompute の段で呼ぶ。変化が無ければ何もしない）
    /// </summary>
    /// <param name="params">生成パラメータ</param>
    /// <param name="time">脈動に使う時間（秒）</param>
    void Dispatch(const MetaBallGpuParams &params, float time);

    /// <summary>
    /// 直近に生成した表面を描画
    /// </summary>
    /// <param name="viewProjection">ビュープロジェクション</param>
    /// <param name="lighting">ライティングを受けるか</param>
    void Draw(const ViewProjection &viewProjection, bool lighting = true);

    /// <summary>
    /// 全出力モデルの色を設定（アルファを下げる場合は SetUseDeferred(false) と併用）
    /// </summary>
    void SetColor(const Vector4 &color);

    /// <summary>
    /// ディファードの G-Buffer に載せるか（false で前方描画、半透明に使用）
    /// </summary>
    void SetUseDeferred(bool use);

    const MetaBallGpuStats &GetStats() const { return field_.GetStats(); }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// 前回の Dispatch から要素・パラメータが変わったか
    /// </summary>
    bool HasChanged(const MetaBallGpuParams &params) const;

    /// ===================================================
    /// private variables
    /// ===================================================

    MetaBallGpuField field_{};
    std::vector<std::unique_ptr<Object3d>> outputs_{}; // 出力モデル（順に書き込む）
    std::unique_ptr<WorldTransform> transform_{};      // 単位行列（メッシュがワールド空間のため）
    std::vector<MetaBallGpuBall> balls_{};             // 今フレームの要素
    std::vector<MetaBallGpuBall> dispatchedBalls_{};   // 直近に生成した要素
    MetaBallGpuParams dispatchedParams_{};             // 直近に生成したパラメータ
    int drawIndex_ = -1;                               // 直近に生成した出力（-1 は未生成）
    bool hasMesh_ = false;                             // 描く表面があるか
};

} // namespace Hagine
