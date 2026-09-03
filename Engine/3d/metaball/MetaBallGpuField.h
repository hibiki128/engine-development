#pragma once
#include "MetaBall.h"
#include "d3d12.h"
#include "type/Vector3.h"
#include "wrl.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hagine {
class DirectXCommon;
class Object3d;
class SrvManager;

/// <summary>
/// GPU メタボールの生成パラメータ。CPU 版の MetaBallWorldParams に相当する。
/// </summary>
struct MetaBallGpuParams
{
    float voxelSize = 0.15f; // セル1辺の長さ。小さいほど滑らかで重い
    float threshold = 0.5f;  // 等値面のしきい値
    float uvScale = 1.0f;    // 平面投影 UV のスケール
    // 脈動（0 なら完全に静止する）。半径を時間で伸び縮みさせて、殻が呼吸しているように見せる
    float wobbleAmplitude = 0.0f;
    float wobbleSpeed = 3.0f;
    float wobbleFrequency = 1.0f;
};

/// <summary>
/// 直近の生成規模。CPU 版と違い頂点数は GPU 側でしか分からないので、
/// ここに入るのは「CPU が決めた入力の大きさ」だけ（ImGui 表示用）。
/// </summary>
struct MetaBallGpuStats
{
    uint32_t ballCount = 0;    // 入力したボールの数
    uint32_t gridX = 0;        // サンプル点の数（X）
    uint32_t gridY = 0;        // サンプル点の数（Y）
    uint32_t gridZ = 0;        // サンプル点の数（Z）
    uint32_t cellCount = 0;    // セルの総数（＝マーチングキューブスのスレッド数）
    uint32_t maxVertexCount = 0; // 出力できる頂点数の上限
    float cellSize = 0.0f;     // セル1辺の長さ
};

/// <summary>
/// メタボールをコンピュートシェーダーで三角形化する。
///
/// CPU 版（MetaBallBuilder）と同じ密度関数・同じ Marching Cubes テーブルを使うので
/// 出てくる形は揃うが、格子を毎フレーム切り直しても CPU 時間を使わない。
/// 形が動き続けるもの（脈打つ殻など）はこちらが向いている。
///
/// 出力先は Object3d::CreateGpuWritableModel() で作ったモデルの頂点バッファ。
/// 書かれなかった頂点は面積 0 の三角形として残るので、描画側は常に容量ぶん描けばよい。
/// </summary>
class MetaBallGpuField
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    ~MetaBallGpuField();

    /// <summary>
    /// 生成に使うバッファを用意する
    /// </summary>
    /// <param name="name">デバッグ名（リソース名の接頭辞になる）</param>
    /// <param name="maxBallCount">扱えるボールの数の上限</param>
    /// <param name="maxGridSamples">格子のサンプル点数の上限（1軸あたり）</param>
    /// <param name="ringSlots">
    /// ボール配列を何面持つか。1フレームに何回も Dispatch する（色ごとに分けるなど）場合、
    /// 1面しか無いと GPU が読む前に上書きしてしまうので、呼ぶ回数×2〜3面ぶん確保しておく
    /// </param>
    void Initialize(const std::string &name, uint32_t maxBallCount, uint32_t maxGridSamples = 96,
                    uint32_t ringSlots = 16);

    /// <summary>
    /// 今フレームのボールを差し替える。位置と半径はメッシュを描く空間の値で渡す
    /// </summary>
    /// <param name="positions">ボールの中心</param>
    /// <param name="radius">影響半径（全ボール共通）</param>
    /// <param name="stiffness">中心での密度の高さ</param>
    void SetBalls(const std::vector<Vector3> &positions, float radius, float stiffness = 1.0f);

    /// <summary>
    /// コンピュートコマンドリストへ生成を積む（Clear → Density → March）。
    /// 呼ぶ前に DirectXCommon::BeginComputeFrame() でリストを開けておくこと
    /// </summary>
    /// <param name="pCommandList">コンピュートコマンドリスト</param>
    /// <param name="target">出力先（CreateGpuWritableModel 済みの Object3d）</param>
    /// <param name="params">生成パラメータ</param>
    /// <param name="time">脈動に使う時間（秒）</param>
    void Dispatch(ID3D12GraphicsCommandList *pCommandList, Object3d *target,
                  const MetaBallGpuParams &params, float time);

    /// <summary>
    /// 出力先に張った UAV の記憶を捨てる。
    /// 出力先の Object3d を作り直すときは必ず呼ぶこと。
    /// 呼ばずに作り直すと、解放されたリソースと同じアドレスに新しいリソースが載ったときに
    /// 古い UAV をそのまま使ってしまう
    /// </summary>
    void ClearTargets() { targetUavIndices_.clear(); }

    /// <summary>ボールが1個も無いか（無ければ Dispatch しても何も出ない）</summary>
    bool IsEmpty() const { return ballCount_ == 0; }

    /// <summary>直近の生成規模</summary>
    const MetaBallGpuStats &GetStats() const { return stats_; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// 出力先の頂点バッファに UAV を張る（同じ出力先なら作り直さない）。
    /// 色ごとに出力先を変えて使い回せるよう、リソースごとに記述子を覚えておく
    /// </summary>
    /// <returns>uint32_t: UAV の記述子番号</returns>
    uint32_t EnsureTargetUav(ID3D12Resource *pVertexResource, uint32_t vertexCount);

    /// <summary>Marching Cubes の三角形テーブルを GPU へ載せる</summary>
    void UploadTriTable();

    /// ===================================================
    /// private variables
    /// ===================================================

    DirectXCommon *pDxCommon_ = nullptr;
    SrvManager *pSrvManager_ = nullptr;
    std::string name_;

    // 入力: ボール配列（float4 × 2 / 1個。HLSL と解釈がずれない形で持つ）。
    // GPU がまだ読んでいる面を上書きしないよう、複数面を順ぐりに使う
    Microsoft::WRL::ComPtr<ID3D12Resource> ballResource_;
    Vector4 *pBallData_ = nullptr;
    uint32_t maxBallCount_ = 0;
    uint32_t ballCount_ = 0;
    uint32_t ringSlots_ = 1;      // ボール配列の面数
    uint32_t nextSlot_ = 0;       // 次に書き込む面
    uint32_t currentSlot_ = 0;    // 直近に書き込んだ面（Dispatch が読む）
    float ballRadius_ = 1.0f;
    float ballStiffness_ = 1.0f;
    Vector3 boundsMin_{};
    Vector3 boundsMax_{};

    // 入力: Marching Cubes の三角形テーブル（256 × 16）
    Microsoft::WRL::ComPtr<ID3D12Resource> triTableResource_;

    // 中間: 密度場
    Microsoft::WRL::ComPtr<ID3D12Resource> densityResource_;
    uint32_t densityUavIndex_ = 0;
    uint32_t maxGridSamples_ = 0;

    // 出力: 頂点数のカウンタ
    Microsoft::WRL::ComPtr<ID3D12Resource> counterResource_;
    uint32_t counterUavIndex_ = 0;

    // 出力先（非所有）の頂点バッファごとに張った UAV。色ごとに出力先を切り替えて使い回す
    std::unordered_map<ID3D12Resource *, uint32_t> targetUavIndices_;

    MetaBallGpuStats stats_{};
};

} // namespace Hagine
